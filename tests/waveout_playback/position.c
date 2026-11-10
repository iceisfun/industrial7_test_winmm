#include "wp.h"
/* waveOutGetPosition on a playing device, and the in-order completion of queued headers.
 *
 * winmmbase forwards WODM_GETPOS (0x0D) with (pmmt, cbmmt) after the NULL and handle checks (decomp); the cbmmt check
 * and the wType rewrite are the driver's (wdmaud.drv, not in the decomp corpus): those expectations come from the VM.
 * VM_TIMEOUT: 240 */

#define FR100 WP_MS2FR(100)

static const char *type_name(UINT t) {
    switch (t) {
    case TIME_MS: return "MS"; case TIME_SAMPLES: return "SAMPLES"; case TIME_BYTES: return "BYTES";
    case TIME_SMPTE: return "SMPTE"; case TIME_MIDI: return "MIDI"; case TIME_TICKS: return "TICKS";
    default: return "?";
    }
}

/* Opens the mapper, writes n headers of ms each, waits for all. Returns 1 when everything completed. */
static int play_n(wp_dev *d, WAVEHDR *hs, int n, DWORD ms) {
    for (int i = 0; i < n; i++) { wp_hdr_ms(&hs[i], d, ms); EXPECT_EQ(wp_prep(d, &hs[i]), MMSYSERR_NOERROR); }
    for (int i = 0; i < n; i++) EXPECT_EQ(wp_write(d, &hs[i]), MMSYSERR_NOERROR);
    int ok = wp_wait_all(d, hs, n, n * ms + WP_DONE_SLACK);
    CHECKF(ok, "%d x %lu ms did not complete", n, ms);
    return ok;
}
static void free_n(wp_dev *d, WAVEHDR *hs, int n) {
    if (d->h) U(waveOutReset)(d->h);
    for (int i = 0; i < n; i++) { if (d->h) wp_unprep(d, &hs[i]); wp_free_hdr(&hs[i]); }
}

TEST(wp_pos_initial_zero_samples_bytes) {
    REQUIRE(U(waveOutGetPosition)); REQUIRE(U(waveOutOpen));
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    DWORD v = 1; UINT t = 0;
    EXPECT_EQ(wp_pos(d.h, TIME_SAMPLES, &v, &t), MMSYSERR_NOERROR); EXPECT_EQ(t, TIME_SAMPLES); EXPECT_EQ(v, 0);
    v = 1;
    EXPECT_EQ(wp_pos(d.h, TIME_BYTES, &v, &t), MMSYSERR_NOERROR); EXPECT_EQ(t, TIME_BYTES); EXPECT_EQ(v, 0);
    v = 1;
    EXPECT_EQ(wp_pos(d.h, TIME_MS, &v, &t), MMSYSERR_NOERROR); EXPECT_EQ(t, TIME_BYTES); EXPECT_EQ(v, 0);
    EXPECT_EQ(wp_close(&d), MMSYSERR_NOERROR);
}

/* The task's core sequence: mapper 44100/16/2, 3 x 100 ms, write all, DONE observed in submission order. */
TEST(wp_pos_three_headers_in_order) {
    REQUIRE(U(waveOutGetPosition)); REQUIRE(U(waveOutWrite));
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    WAVEHDR hs[3];
    for (int i = 0; i < 3; i++) { wp_hdr_ms(&hs[i], &d, 100); EXPECT_EQ(wp_prep(&d, &hs[i]), MMSYSERR_NOERROR); EXPECT_EQ(hs[i].dwFlags, WHDR_PREPARED); }
    for (int i = 0; i < 3; i++) {
        EXPECT_EQ(wp_write(&d, &hs[i]), MMSYSERR_NOERROR);
        CHECKF(hs[i].dwFlags & WHDR_INQUEUE, "hdr %d flags 0x%lx after write", i, hs[i].dwFlags);
    }
    int order[3], seen = 0, bad = 0;
    DWORD t0 = GetTickCount();
    while (seen < 3 && GetTickCount() - t0 < 300 + WP_DONE_SLACK) {
        for (int i = 0; i < 3; i++) {
            int already = 0;
            for (int k = 0; k < seen; k++) if (order[k] == i) already = 1;
            if (!already && (hs[i].dwFlags & WHDR_DONE)) {
                for (int j = 0; j < i; j++) if (!(hs[j].dwFlags & WHDR_DONE)) bad++;
                order[seen++] = i;
            }
        }
        Sleep(1);
    }
    EXPECT_EQ(seen, 3);
    EXPECT_EQ(bad, 0);
    for (int k = 0; k < seen; k++) EXPECT_EQ(order[k], k);
    for (int i = 0; i < 3; i++) EXPECT_EQ(hs[i].dwFlags, WHDR_DONE | WHDR_PREPARED);
    if (seen == 3) {
        EXPECT_EQ(wp_samples(d.h), 3 * FR100);
        EXPECT_EQ(wp_bytes(d.h), 3 * FR100 * WP_ALIGN);
    }
    free_n(&d, hs, 3);
    EXPECT_EQ(wp_close(&d), MMSYSERR_NOERROR);
}

/* WOM_DONE through CALLBACK_FUNCTION: one per header, dwParam1 = the header, in submission order. */
static volatile LONG g_ncb;
static DWORD_PTR g_cb_msg[64], g_cb_p1[64];
static void CALLBACK wp_cb(HWAVEOUT h, UINT msg, DWORD_PTR inst, DWORD_PTR p1, DWORD_PTR p2) {
    LONG i = InterlockedIncrement(&g_ncb) - 1;
    if (i < 64) { g_cb_msg[i] = msg; g_cb_p1[i] = p1; }
}
TEST(wp_pos_callback_function_done_order) {
    REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutWrite));
    WP_NEED_DEV();
    WAVEFORMATEX wf; wp_fmt_std(&wf);
    HWAVEOUT h = NULL;
    g_ncb = 0;
    MMRESULT r = U(waveOutOpen)(&h, WAVE_MAPPER, &wf, (DWORD_PTR)wp_cb, 0x1234, CALLBACK_FUNCTION);
    EXPECT_EQ(r, MMSYSERR_NOERROR);
    if (r) return;
    EXPECT_EQ(g_ncb, 1);
    EXPECT_EQ(g_cb_msg[0], WOM_OPEN);
    wp_dev d; memset(&d, 0, sizeof d); d.h = h; d.wf = wf;
    WAVEHDR hs[5];
    for (int i = 0; i < 5; i++) { wp_hdr_ms(&hs[i], &d, 40); EXPECT_EQ(wp_prep(&d, &hs[i]), MMSYSERR_NOERROR); }
    for (int i = 0; i < 5; i++) EXPECT_EQ(wp_write(&d, &hs[i]), MMSYSERR_NOERROR);
    CHECK(wp_wait_all(NULL, hs, 5, 200 + WP_DONE_SLACK));
    DWORD t0 = GetTickCount();
    while (g_ncb < 6 && GetTickCount() - t0 < 1000) Sleep(5);
    EXPECT_EQ(g_ncb, 6);
    for (int i = 0; i < 5 && i + 1 < g_ncb; i++) {
        EXPECT_EQ(g_cb_msg[i + 1], WOM_DONE);
        EXPECT_EQ(g_cb_p1[i + 1], (DWORD_PTR)&hs[i]);
    }
    EXPECT_EQ(wp_samples(h), 5 * WP_MS2FR(40));
    for (int i = 0; i < 5; i++) { EXPECT_EQ(wp_unprep(&d, &hs[i]), MMSYSERR_NOERROR); wp_free_hdr(&hs[i]); }
    EXPECT_EQ(U(waveOutClose)(h), MMSYSERR_NOERROR);
    EXPECT_EQ(g_ncb, 7);
    EXPECT_EQ(g_cb_msg[6], WOM_CLOSE);
}

/* After draining N frames, TIME_SAMPLES == N and TIME_BYTES == N * nBlockAlign, for several buffer counts. */
static void drain_exact(int n, DWORD ms) {
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    WAVEHDR hs[16];
    if (play_n(&d, hs, n, ms)) {
        EXPECT_EQ(wp_samples(d.h), n * WP_MS2FR(ms));
        EXPECT_EQ(wp_bytes(d.h), n * WP_MS2FR(ms) * WP_ALIGN);
    }
    free_n(&d, hs, n);
    EXPECT_EQ(wp_close(&d), MMSYSERR_NOERROR);
}
TEST(wp_pos_drain_1x100) { REQUIRE(U(waveOutGetPosition)); drain_exact(1, 100); }
TEST(wp_pos_drain_2x50) { REQUIRE(U(waveOutGetPosition)); drain_exact(2, 50); }
TEST(wp_pos_drain_4x25) { REQUIRE(U(waveOutGetPosition)); drain_exact(4, 25); }
TEST(wp_pos_drain_10x10) { REQUIRE(U(waveOutGetPosition)); drain_exact(10, 10); }
TEST(wp_pos_drain_16x20) { REQUIRE(U(waveOutGetPosition)); drain_exact(16, 20); }
TEST(wp_pos_drain_1x250) { REQUIRE(U(waveOutGetPosition)); drain_exact(1, 250); }

/* Positions accumulate across separate write/drain rounds on one open handle. */
TEST(wp_pos_accumulates_across_rounds) {
    REQUIRE(U(waveOutGetPosition));
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    WAVEHDR wh; wp_hdr_ms(&wh, &d, 60);
    EXPECT_EQ(wp_prep(&d, &wh), MMSYSERR_NOERROR);
    for (int round = 1; round <= 3; round++) {
        EXPECT_EQ(wp_write(&d, &wh), MMSYSERR_NOERROR);
        if (!wp_wait_done(&d, &wh, 60 + WP_DONE_SLACK)) { CHECKF(0, "round %d did not complete", round); break; }
        EXPECT_EQ(wp_samples(d.h), round * WP_MS2FR(60));
        EXPECT_EQ(wp_bytes(d.h), round * WP_MS2FR(60) * WP_ALIGN);
    }
    U(waveOutReset)(d.h);
    EXPECT_EQ(wp_unprep(&d, &wh), MMSYSERR_NOERROR);
    wp_free_hdr(&wh);
    EXPECT_EQ(wp_close(&d), MMSYSERR_NOERROR);
}

/* Unprepare does not move the position. */
TEST(wp_pos_unprepare_keeps_position) {
    REQUIRE(U(waveOutGetPosition));
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    WAVEHDR hs[2];
    if (play_n(&d, hs, 2, 50)) {
        EXPECT_EQ(wp_unprep(&d, &hs[0]), MMSYSERR_NOERROR);
        EXPECT_EQ(wp_unprep(&d, &hs[1]), MMSYSERR_NOERROR);
        EXPECT_EQ(hs[0].dwFlags, WHDR_DONE);
        EXPECT_EQ(wp_samples(d.h), 2 * WP_MS2FR(50));
    }
    free_n(&d, hs, 2);
    EXPECT_EQ(wp_close(&d), MMSYSERR_NOERROR);
}

/* ---- wType handling after a drain (driver behaviour, VM) -------------------------------------------------------- */

static void type_after_drain(UINT req) {
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    WAVEHDR hs[2];
    if (play_n(&d, hs, 2, 50)) {
        DWORD frames = 2 * WP_MS2FR(50), v = 0; UINT t = 0;
        EXPECT_EQ(wp_pos(d.h, req, &v, &t), MMSYSERR_NOERROR);
        if (req == TIME_SAMPLES || req == TIME_BYTES) EXPECT_EQ(t, req);
        else {
            t_note("wType %s(0x%x) came back as %s(0x%x) value %lu", type_name(req), req, type_name(t), t, v);
            EXPECT_EQ(t, TIME_BYTES);
        }
        if (t == TIME_SAMPLES) EXPECT_EQ(v, frames);
        else if (t == TIME_BYTES) EXPECT_EQ(v, frames * WP_ALIGN);
    }
    free_n(&d, hs, 2);
    EXPECT_EQ(wp_close(&d), MMSYSERR_NOERROR);
}
TEST(wp_pos_type_samples) { REQUIRE(U(waveOutGetPosition)); type_after_drain(TIME_SAMPLES); }
TEST(wp_pos_type_bytes) { REQUIRE(U(waveOutGetPosition)); type_after_drain(TIME_BYTES); }
TEST(wp_pos_type_ms_rewritten) { REQUIRE(U(waveOutGetPosition)); type_after_drain(TIME_MS); }
TEST(wp_pos_type_smpte_rewritten) { REQUIRE(U(waveOutGetPosition)); type_after_drain(TIME_SMPTE); }
TEST(wp_pos_type_midi_rewritten) { REQUIRE(U(waveOutGetPosition)); type_after_drain(TIME_MIDI); }
TEST(wp_pos_type_ticks_rewritten) { REQUIRE(U(waveOutGetPosition)); type_after_drain(TIME_TICKS); }
TEST(wp_pos_type_zero) { REQUIRE(U(waveOutGetPosition)); type_after_drain(0); }
TEST(wp_pos_type_undefined_0x40) { REQUIRE(U(waveOutGetPosition)); type_after_drain(0x40); }
TEST(wp_pos_type_undefined_0xffff) { REQUIRE(U(waveOutGetPosition)); type_after_drain(0xFFFF); }
TEST(wp_pos_type_combined_bits) { REQUIRE(U(waveOutGetPosition)); type_after_drain(TIME_SAMPLES | TIME_BYTES); }

/* ---- cbmmt (driver validated: VM) ------------------------------------------------------------------------------- */

static void cb_case(UINT cb) {
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    WAVEHDR hs[1];
    if (play_n(&d, hs, 1, 50)) {
        unsigned char buf[64];
        memset(buf, 0xA5, sizeof buf);
        ((MMTIME *)buf)->wType = TIME_BYTES;
        MMRESULT r = U(waveOutGetPosition)(d.h, (MMTIME *)buf, cb);
        MMTIME *mt = (MMTIME *)buf;
        t_note("cbmmt %u -> %u (wType 0x%x value 0x%lx, byte[12] 0x%02x)", cb, r, mt->wType, mt->u.cb, buf[12]);
        if (cb < sizeof(MMTIME)) {
            EXPECT_EQ(r, MMSYSERR_ERROR);
            EXPECT_EQ(mt->u.cb, 0xA5A5A5A5);
        } else {
            EXPECT_EQ(r, MMSYSERR_NOERROR);
            EXPECT_EQ(mt->wType, TIME_BYTES);
            EXPECT_EQ(mt->u.cb, WP_MS2FR(50) * WP_ALIGN);
            EXPECT_EQ(buf[12], 0xA5);   /* nothing written past the MMTIME */
        }
    }
    free_n(&d, hs, 1);
    EXPECT_EQ(wp_close(&d), MMSYSERR_NOERROR);
}
TEST(wp_pos_cbmmt_0) { REQUIRE(U(waveOutGetPosition)); cb_case(0); }
TEST(wp_pos_cbmmt_1) { REQUIRE(U(waveOutGetPosition)); cb_case(1); }
TEST(wp_pos_cbmmt_4) { REQUIRE(U(waveOutGetPosition)); cb_case(4); }
TEST(wp_pos_cbmmt_8) { REQUIRE(U(waveOutGetPosition)); cb_case(8); }
TEST(wp_pos_cbmmt_11) { REQUIRE(U(waveOutGetPosition)); cb_case(sizeof(MMTIME) - 1); }
TEST(wp_pos_cbmmt_12) { REQUIRE(U(waveOutGetPosition)); cb_case(sizeof(MMTIME)); }
TEST(wp_pos_cbmmt_13) { REQUIRE(U(waveOutGetPosition)); cb_case(sizeof(MMTIME) + 1); }
TEST(wp_pos_cbmmt_16) { REQUIRE(U(waveOutGetPosition)); cb_case(16); }
TEST(wp_pos_cbmmt_64) { REQUIRE(U(waveOutGetPosition)); cb_case(64); }

TEST(wp_pos_mmtime_size_is_12) {
    REQUIRE(U(waveOutGetPosition));
    EXPECT_EQ(sizeof(MMTIME), 12);
    EXPECT_EQ(offsetof(MMTIME, u), 4);
}

/* ---- monotonic while playing ------------------------------------------------------------------------------------ */

TEST(wp_pos_monotonic_while_playing) {
    REQUIRE(U(waveOutGetPosition));
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    WAVEHDR hs[3];
    for (int i = 0; i < 3; i++) { wp_hdr_ms(&hs[i], &d, 100); EXPECT_EQ(wp_prep(&d, &hs[i]), MMSYSERR_NOERROR); }
    for (int i = 0; i < 3; i++) EXPECT_EQ(wp_write(&d, &hs[i]), MMSYSERR_NOERROR);
    DWORD prev = 0, back = 0, over = 0, samples = 0, distinct = 0, total = 3 * FR100, t0 = GetTickCount();
    while (!(hs[2].dwFlags & WHDR_DONE) && GetTickCount() - t0 < 300 + WP_DONE_SLACK) {
        DWORD p = wp_samples(d.h);
        if (p == 0xFFFFFFFF) { CHECKF(0, "GetPosition failed while playing"); break; }
        if (p < prev) back++;
        if (p > total) over++;
        if (p != prev) distinct++;
        prev = p; samples++;
        Sleep(3);
    }
    CHECK(hs[2].dwFlags & WHDR_DONE);
    EXPECT_EQ(back, 0);
    EXPECT_EQ(over, 0);
    CHECKF(distinct >= 3, "only %lu distinct positions in %lu reads", distinct, samples);
    t_note("monotonic: %lu reads, %lu distinct values", samples, distinct);
    EXPECT_EQ(wp_samples(d.h), total);
    free_n(&d, hs, 3);
    EXPECT_EQ(wp_close(&d), MMSYSERR_NOERROR);
}

/* Bytes and samples agree (bytes = samples * nBlockAlign) and never run ahead of what is completed + queued. */
TEST(wp_pos_bytes_samples_agree_while_playing) {
    REQUIRE(U(waveOutGetPosition));
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    WAVEHDR wh; wp_hdr_ms(&wh, &d, 300);
    EXPECT_EQ(wp_prep(&d, &wh), MMSYSERR_NOERROR);
    EXPECT_EQ(wp_write(&d, &wh), MMSYSERR_NOERROR);
    DWORD misaligned = 0, reads = 0, t0 = GetTickCount();
    while (!(wh.dwFlags & WHDR_DONE) && GetTickCount() - t0 < 300 + WP_DONE_SLACK) {
        DWORD b = wp_bytes(d.h);
        if (b != 0xFFFFFFFF && b % WP_ALIGN) misaligned++;
        CHECKF(b == 0xFFFFFFFF || b <= wh.dwBufferLength, "bytes %lu > %lu", b, wh.dwBufferLength);
        reads++;
        Sleep(7);
    }
    EXPECT_EQ(misaligned, 0);
    CHECK(wh.dwFlags & WHDR_DONE);
    EXPECT_EQ(wp_bytes(d.h), wh.dwBufferLength);
    U(waveOutReset)(d.h);
    EXPECT_EQ(wp_unprep(&d, &wh), MMSYSERR_NOERROR);
    wp_free_hdr(&wh);
    EXPECT_EQ(wp_close(&d), MMSYSERR_NOERROR);
}

/* ---- formats --------------------------------------------------------------------------------------------------- */

static void fmt_case(UINT id, DWORD rate, WORD ch, WORD bits) {
    WP_NEED_DEV();
    WAVEFORMATEX wf; wp_fmt(&wf, rate, ch, bits);
    wp_dev d;
    MMRESULT r = wp_open_ex(&d, id, &wf);
    EXPECT_EQ(r, MMSYSERR_NOERROR);
    if (r) { t_note("open %lu/%u/%u on id %d -> %u", rate, bits, ch, (int)id, r); return; }
    WAVEHDR wh; wp_hdr_ms(&wh, &d, 80);
    DWORD frames = wh.dwBufferLength / wf.nBlockAlign;
    EXPECT_EQ(wp_prep(&d, &wh), MMSYSERR_NOERROR);
    EXPECT_EQ(wp_write(&d, &wh), MMSYSERR_NOERROR);
    if (wp_wait_done(&d, &wh, 80 + WP_DONE_SLACK)) {
        EXPECT_EQ(wp_samples(d.h), frames);
        EXPECT_EQ(wp_bytes(d.h), wh.dwBufferLength);
        DWORD v; UINT t;
        EXPECT_EQ(wp_pos(d.h, TIME_MS, &v, &t), MMSYSERR_NOERROR);
        EXPECT_EQ(t, TIME_BYTES);
        EXPECT_EQ(v, wh.dwBufferLength);
    } else CHECKF(0, "%lu/%u/%u did not complete", rate, bits, ch);
    U(waveOutReset)(d.h);
    EXPECT_EQ(wp_unprep(&d, &wh), MMSYSERR_NOERROR);
    wp_free_hdr(&wh);
    EXPECT_EQ(wp_close(&d), MMSYSERR_NOERROR);
}
#define FMT(nm, rate, ch, bits) \
    TEST(wp_pos_fmt_mapper_##nm) { REQUIRE(U(waveOutGetPosition)); fmt_case(WAVE_MAPPER, rate, ch, bits); } \
    TEST(wp_pos_fmt_dev0_##nm) { REQUIRE(U(waveOutGetPosition)); fmt_case(0, rate, ch, bits); }
FMT(8000_8_1, 8000, 1, 8)
FMT(11025_8_2, 11025, 2, 8)
FMT(22050_16_1, 22050, 1, 16)
FMT(44100_16_1, 44100, 1, 16)
FMT(44100_16_2, 44100, 2, 16)
FMT(48000_16_2, 48000, 2, 16)
FMT(96000_16_2, 96000, 2, 16)
FMT(44100_24_2, 44100, 2, 24)
FMT(48000_32_2, 48000, 2, 32)

/* Every enumerated device (not just the mapper) plays and reports exact positions. */
TEST(wp_pos_every_device) {
    REQUIRE(U(waveOutGetPosition));
    WP_NEED_DEV();
    for (UINT id = 0; id < wp_nout(); id++) {
        WAVEFORMATEX wf; wp_fmt_std(&wf);
        wp_dev d;
        MMRESULT r = wp_open_ex(&d, id, &wf);
        CHECKF(r == MMSYSERR_NOERROR, "open id %u = %u", id, r);
        if (r) continue;
        WAVEHDR hs[2];
        if (play_n(&d, hs, 2, 50)) EXPECT_EQ(wp_samples(d.h), 2 * WP_MS2FR(50));
        free_n(&d, hs, 2);
        EXPECT_EQ(wp_close(&d), MMSYSERR_NOERROR);
    }
}

/* ---- buffer lengths that are not whole frames, zero length -------------------------------------------------------- */

static void odd_len_case(DWORD extra) {
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    WAVEHDR wh; wp_hdr_bytes(&wh, FR100 * WP_ALIGN + extra);
    EXPECT_EQ(wp_prep(&d, &wh), MMSYSERR_NOERROR);
    EXPECT_EQ(wp_write(&d, &wh), MMSYSERR_NOERROR);
    if (wp_wait_done(&d, &wh, 100 + WP_DONE_SLACK)) {
        DWORD b = wp_bytes(d.h), s = wp_samples(d.h);
        t_note("buffer of %lu bytes (+%lu): bytes %lu samples %lu", wh.dwBufferLength, extra, b, s);
        /* VM: the trailing partial frame is dropped from the count */
        EXPECT_EQ(s, FR100);
        EXPECT_EQ(b, FR100 * WP_ALIGN);
    } else CHECKF(0, "did not complete");
    U(waveOutReset)(d.h);
    EXPECT_EQ(wp_unprep(&d, &wh), MMSYSERR_NOERROR);
    wp_free_hdr(&wh);
    EXPECT_EQ(wp_close(&d), MMSYSERR_NOERROR);
}
TEST(wp_pos_partial_frame_plus1) { REQUIRE(U(waveOutGetPosition)); odd_len_case(1); }
TEST(wp_pos_partial_frame_plus2) { REQUIRE(U(waveOutGetPosition)); odd_len_case(2); }
TEST(wp_pos_partial_frame_plus3) { REQUIRE(U(waveOutGetPosition)); odd_len_case(3); }

TEST(wp_pos_zero_length_buffer) {
    REQUIRE(U(waveOutGetPosition));
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    WAVEHDR z; wp_hdr_bytes(&z, 0);
    WAVEHDR wh; wp_hdr_ms(&wh, &d, 50);
    EXPECT_EQ(wp_prep(&d, &z), MMSYSERR_NOERROR);
    EXPECT_EQ(wp_prep(&d, &wh), MMSYSERR_NOERROR);
    MMRESULT r = wp_write(&d, &z);
    t_note("write of a zero-length header -> %u", r);
    EXPECT_EQ(wp_write(&d, &wh), MMSYSERR_NOERROR);
    int ok = wp_wait_done(&d, &wh, 50 + WP_DONE_SLACK);
    CHECK(ok);
    if (ok) {
        if (r == MMSYSERR_NOERROR) EXPECT_EQ(z.dwFlags & (WHDR_DONE | WHDR_INQUEUE), WHDR_DONE);
        EXPECT_EQ(wp_samples(d.h), WP_MS2FR(50));
    }
    U(waveOutReset)(d.h);
    wp_unprep(&d, &z); wp_unprep(&d, &wh);
    wp_free_hdr(&z); wp_free_hdr(&wh);
    EXPECT_EQ(wp_close(&d), MMSYSERR_NOERROR);
}

/* CALLBACK_NULL: completion is visible through the header flags alone. */
TEST(wp_pos_callback_null_completes) {
    REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutGetPosition));
    WP_NEED_DEV();
    WAVEFORMATEX wf; wp_fmt_std(&wf);
    wp_dev d; memset(&d, 0, sizeof d); d.wf = wf;
    EXPECT_EQ(U(waveOutOpen)(&d.h, WAVE_MAPPER, &wf, 0, 0, CALLBACK_NULL), MMSYSERR_NOERROR);
    if (!d.h) return;
    WAVEHDR hs[2];
    if (play_n(&d, hs, 2, 50)) {
        EXPECT_EQ(wp_samples(d.h), 2 * WP_MS2FR(50));
    }
    free_n(&d, hs, 2);
    EXPECT_EQ(wp_close(&d), MMSYSERR_NOERROR);
}

/* A quiet audible tone for host-side verification (tools/wav_capture.sh). */
TEST(wp_pos_tone_position_exact) {
    REQUIRE(U(waveOutGetPosition));
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    WAVEHDR wh; wp_hdr_ms(&wh, &d, 300);
    wp_fill_tone(&wh, &d, 660);
    EXPECT_EQ(wp_prep(&d, &wh), MMSYSERR_NOERROR);
    t_note("TONE hz=%d ms=%d", 660, 300);
    EXPECT_EQ(wp_write(&d, &wh), MMSYSERR_NOERROR);
    if (wp_wait_done(&d, &wh, 300 + WP_DONE_SLACK)) EXPECT_EQ(wp_samples(d.h), WP_MS2FR(300));
    else CHECKF(0, "tone did not complete");
    U(waveOutReset)(d.h);
    EXPECT_EQ(wp_unprep(&d, &wh), MMSYSERR_NOERROR);
    wp_free_hdr(&wh);
    EXPECT_EQ(wp_close(&d), MMSYSERR_NOERROR);
}
