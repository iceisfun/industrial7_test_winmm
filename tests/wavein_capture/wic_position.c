/* wavein_capture: waveInGetPosition on a capture stream.
 * winmmbase only checks pmmt (NULL -> 11) and the handle (5) and forwards WIDM_GETPOS (0x3C) with cbmmt
 * [decomp waveInGetPosition 0x11a60]; the time format handling is the driver's. Device half only.
 */
#include "wic.h"

#define FIRST_MS 3000

/* open + record `nbuf` buffers of `ms` each, then Stop (position frozen); returns 0 on failure */
static int recorded_stream(wic_s *s, const WAVEFORMATEX *f, int nbuf, DWORD ms) {
    if (wic_open(s, WAVE_MAPPER, f)) return 0;
    DWORD len = wic_bytes_ms(f, ms);
    for (int i = 0; i < nbuf; i++) { if (wic_buf(s, i, len) || wic_add(s, i)) return 0; }
    if (U(waveInStart)(s->h)) return 0;
    if (!wic_wait_all(s, 0, nbuf, FIRST_MS + 200 * nbuf)) return 0;
    return U(waveInStop)(s->h) == 0;
}

TEST(wic_pos_before_start_zero) {
    REQUIRE(U(waveInGetPosition)); NEED_DEV();
    wic_s s; if (wic_open_std(&s, WAVE_MAPPER)) { wic_teardown(&s); return; }
    static const UINT types[] = {TIME_BYTES, TIME_SAMPLES, TIME_MS};
    for (int i = 0; i < 3; i++) {
        MMTIME m; memset(&m, 0x5A, sizeof m); m.wType = types[i];
        EXPECT_EQ(U(waveInGetPosition)(s.h, &m, sizeof m), 0);
        CHECKF(m.u.cb == 0, "type %u -> wType %u value %lu", types[i], m.wType, m.u.cb);
    }
    /* buffers queued but not started: still 0 */
    EXPECT_EQ(wic_buf(&s, 0, 4410), 0); EXPECT_EQ(wic_add(&s, 0), 0);
    Sleep(100);
    EXPECT_EQ(wic_pos(s.h, TIME_BYTES, NULL), 0);
    EXPECT_EQ(wic_teardown(&s), 0);
}

TEST(wic_pos_null_mmtime_real_handle) {
    REQUIRE(U(waveInGetPosition)); NEED_DEV();
    wic_s s; if (wic_open_std(&s, WAVE_MAPPER)) { wic_teardown(&s); return; }
    EXPECT_EQ(U(waveInGetPosition)(s.h, NULL, sizeof(MMTIME)), MMSYSERR_INVALPARAM);
    EXPECT_EQ(U(waveInGetPosition)(s.h, NULL, 0), MMSYSERR_INVALPARAM);
    EXPECT_EQ(wic_teardown(&s), 0);
}

TEST(wic_pos_cbmmt_sizes) {
    REQUIRE(U(waveInGetPosition)); NEED_DEV();
    wic_s s; if (wic_open_std(&s, WAVE_MAPPER)) { wic_teardown(&s); return; }
    static const UINT sz[] = {0, 1, 4, 8, 11, 12, 16, 64};
    for (int i = 0; i < 8; i++) {
        struct { MMTIME m; char pad[64]; } b; memset(&b, 0x5A, sizeof b); b.m.wType = TIME_BYTES;
        MMRESULT r = U(waveInGetPosition)(s.h, &b.m, sz[i]);
        if (sz[i] >= sizeof(MMTIME)) {
            CHECKF(r == 0, "cbmmt %u -> %u", sz[i], r); EXPECT_EQ(b.m.wType, TIME_BYTES); EXPECT_EQ(b.m.u.cb, 0);
        } else {
            /* [VM] a short MMTIME is refused by the driver with MMSYSERR_ERROR and left untouched */
            CHECKF(r == MMSYSERR_ERROR, "cbmmt %u -> %u", sz[i], r);
            EXPECT_EQ(b.m.wType, TIME_BYTES); EXPECT_EQ(b.m.u.cb, 0x5A5A5A5A);
        }
    }
    EXPECT_EQ(wic_teardown(&s), 0);
}

/* unsupported time formats come back as TIME_BYTES (driver rewrites wType) [VM waveOut analogue] */
TEST(wic_pos_time_format_table) {
    REQUIRE(U(waveInGetPosition)); NEED_DEV();
    wic_s s; WAVEFORMATEX f; wic_pcm(&f, 44100, 16, 1);
    if (!recorded_stream(&s, &f, 2, 50)) { CHECK(!"stream setup"); wic_teardown(&s); return; }
    DWORD bytes = wic_pos(s.h, TIME_BYTES, NULL);
    CHECK(bytes != 0xFFFFFFFF && bytes >= 2 * wic_bytes_ms(&f, 50));
    static const struct { UINT req, want; } t[] = {
        {TIME_BYTES, TIME_BYTES}, {TIME_SAMPLES, TIME_SAMPLES}, {TIME_MS, TIME_BYTES},
        {TIME_SMPTE, TIME_BYTES}, {TIME_MIDI, TIME_BYTES}, {TIME_TICKS, TIME_BYTES},
    };
    for (int i = 0; i < 6; i++) {
        MMTIME m; memset(&m, 0, sizeof m); m.wType = t[i].req;
        EXPECT_EQ(U(waveInGetPosition)(s.h, &m, sizeof m), 0);
        CHECKF(m.wType == t[i].want, "requested %u got wType %u", t[i].req, m.wType);
        DWORD v = m.u.cb;
        if (m.wType == TIME_BYTES) CHECKF(v == bytes, "requested %u: %lu bytes vs %lu", t[i].req, v, bytes);
        if (m.wType == TIME_SAMPLES) CHECKF(v == bytes / 2, "samples %lu bytes %lu", v, bytes);
    }
    EXPECT_EQ(wic_teardown(&s), 0);
}

TEST(wic_pos_unknown_wtype) {
    REQUIRE(U(waveInGetPosition)); NEED_DEV();
    wic_s s; if (wic_open_std(&s, WAVE_MAPPER)) { wic_teardown(&s); return; }
    static const UINT ty[] = {0, 3, 0x40, 0x1234};
    for (int i = 0; i < 4; i++) {
        MMTIME m; memset(&m, 0, sizeof m); m.wType = ty[i];
        MMRESULT r = U(waveInGetPosition)(s.h, &m, sizeof m);
        /* [VM] any type other than SAMPLES/BYTES is answered as TIME_BYTES */
        CHECKF(r == 0, "wType %u -> %u", ty[i], r);
        CHECKF(m.wType == TIME_BYTES, "wType %u came back as %u", ty[i], m.wType);
    }
    EXPECT_EQ(wic_teardown(&s), 0);
}

TEST(wic_pos_ge_recorded_during_stream) {
    REQUIRE(U(waveInGetPosition)); NEED_DEV();
    wic_s s; if (wic_open_std(&s, WAVE_MAPPER)) { wic_teardown(&s); return; }
    DWORD len = wic_bytes_ms(wic_wfx(&s), 40);
    for (int i = 0; i < 4; i++) { EXPECT_EQ(wic_buf(&s, i, len), 0); EXPECT_EQ(wic_add(&s, i), 0); }
    EXPECT_EQ(U(waveInStart)(s.h), 0);
    for (int i = 0; i < 4; i++) {
        if (!wic_wait_done(&s, i, FIRST_MS)) { CHECK(!"timeout"); break; }
        DWORD p = wic_pos(s.h, TIME_BYTES, NULL);
        CHECKF(p != 0xFFFFFFFF && p >= (DWORD)(i + 1) * len, "after %d buffers pos %lu", i + 1, p);
    }
    EXPECT_EQ(wic_teardown(&s), 0);
}

TEST(wic_pos_monotonic_while_recording) {
    REQUIRE(U(waveInGetPosition)); NEED_DEV();
    wic_s s; if (wic_open_std(&s, WAVE_MAPPER)) { wic_teardown(&s); return; }
    DWORD len = wic_bytes_ms(wic_wfx(&s), 250);
    for (int i = 0; i < 2; i++) { EXPECT_EQ(wic_buf(&s, i, len), 0); EXPECT_EQ(wic_add(&s, i), 0); }
    EXPECT_EQ(U(waveInStart)(s.h), 0);
    DWORD prev = 0, t0 = wic_now(); int dec = 0, n = 0, grew = 0;
    while (wic_now() - t0 < 2500 && !(s.hdr[1].dwFlags & WHDR_DONE)) {
        DWORD p = wic_pos(s.h, TIME_BYTES, NULL);
        if (p == 0xFFFFFFFF) { CHECK(!"GetPosition failed"); break; }
        if (p < prev) dec++;
        if (p > prev) grew++;
        prev = p; n++;
        Sleep(20);
    }
    EXPECT_EQ(dec, 0);
    CHECKF(grew > 1, "position grew %d times in %d polls", grew, n);
    CHECKF(prev % 2 == 0, "position %lu not block aligned", prev);
    EXPECT_EQ(wic_teardown(&s), 0);
}

TEST(wic_pos_frozen_after_stop) {
    REQUIRE(U(waveInGetPosition)); NEED_DEV();
    wic_s s; WAVEFORMATEX f; wic_pcm(&f, 44100, 16, 1);
    if (!recorded_stream(&s, &f, 1, 60)) { CHECK(!"stream setup"); wic_teardown(&s); return; }
    DWORD a = wic_pos(s.h, TIME_BYTES, NULL);
    Sleep(150);
    DWORD b = wic_pos(s.h, TIME_BYTES, NULL);
    EXPECT_EQ(a, b);
    CHECK(a >= wic_bytes_ms(&f, 60));
    EXPECT_EQ(wic_pos(s.h, TIME_SAMPLES, NULL) * 2, a);
    EXPECT_EQ(wic_teardown(&s), 0);
}

TEST(wic_pos_reset_to_zero) {
    REQUIRE(U(waveInGetPosition)); NEED_DEV();
    wic_s s; WAVEFORMATEX f; wic_pcm(&f, 44100, 16, 1);
    if (!recorded_stream(&s, &f, 1, 50)) { CHECK(!"stream setup"); wic_teardown(&s); return; }
    CHECK(wic_pos(s.h, TIME_BYTES, NULL) > 0);
    EXPECT_EQ(U(waveInReset)(s.h), 0);
    EXPECT_EQ(wic_pos(s.h, TIME_BYTES, NULL), 0);
    EXPECT_EQ(wic_pos(s.h, TIME_SAMPLES, NULL), 0);
    /* and counting restarts from 0 on the next Start */
    EXPECT_EQ(wic_add(&s, 0), 0);
    EXPECT_EQ(U(waveInStart)(s.h), 0);
    CHECK(wic_wait_done(&s, 0, FIRST_MS));
    EXPECT_EQ(U(waveInStop)(s.h), 0);
    DWORD p = wic_pos(s.h, TIME_BYTES, NULL);
    DWORD len = wic_bytes_ms(&f, 50);
    CHECKF(p >= len && p < 20 * len, "pos after restart %lu (len %lu)", p, len);
    EXPECT_EQ(wic_teardown(&s), 0);
}

TEST(wic_pos_reset_while_running_zero) {
    REQUIRE(U(waveInGetPosition)); NEED_DEV();
    wic_s s; if (wic_open_std(&s, WAVE_MAPPER)) { wic_teardown(&s); return; }
    EXPECT_EQ(wic_buf(&s, 0, wic_bytes_ms(wic_wfx(&s), 30)), 0); EXPECT_EQ(wic_add(&s, 0), 0);
    EXPECT_EQ(wic_buf(&s, 1, wic_bytes_ms(wic_wfx(&s), 250)), 0); EXPECT_EQ(wic_add(&s, 1), 0);
    EXPECT_EQ(U(waveInStart)(s.h), 0);
    CHECK(wic_wait_done(&s, 0, FIRST_MS));
    EXPECT_EQ(U(waveInReset)(s.h), 0);
    EXPECT_EQ(wic_pos(s.h, TIME_BYTES, NULL), 0);
    Sleep(100);
    EXPECT_EQ(wic_pos(s.h, TIME_BYTES, NULL), 0);   /* Reset also stops the stream */
    EXPECT_EQ(wic_teardown(&s), 0);
}

/* bytes / samples consistency per format (position frozen after Stop) */
typedef struct { const char *n; DWORD rate; WORD bits, ch; int flt; } prow;
static const prow k_prows[] = {
    {"8000_8_1", 8000, 8, 1, 0}, {"8000_16_1", 8000, 16, 1, 0}, {"11025_8_2", 11025, 8, 2, 0},
    {"22050_16_2", 22050, 16, 2, 0}, {"44100_16_2", 44100, 16, 2, 0}, {"48000_16_2", 48000, 16, 2, 0},
    {"48000_16_1", 48000, 16, 1, 0}, {"96000_16_2", 96000, 16, 2, 0}, {"48000_float_2", 48000, 32, 2, 1},
    {"44100_float_1", 44100, 32, 1, 1},
};

static void pos_consistency(const prow *r) {
    wic_s s; WAVEFORMATEX f;
    if (r->flt) wic_float(&f, r->rate, r->ch); else wic_pcm(&f, r->rate, r->bits, r->ch);
    if (!recorded_stream(&s, &f, 2, 40)) { CHECKF(0, "%s: stream setup", r->n); wic_teardown(&s); return; }
    DWORD len = wic_bytes_ms(&f, 40);
    UINT tb, ts;
    DWORD b = wic_pos(s.h, TIME_BYTES, &tb), sm = wic_pos(s.h, TIME_SAMPLES, &ts);
    EXPECT_EQ(tb, TIME_BYTES); EXPECT_EQ(ts, TIME_SAMPLES);
    CHECKF(b >= 2 * len, "%s: bytes %lu < %lu", r->n, b, 2 * len);
    CHECKF(b % f.nBlockAlign == 0, "%s: bytes %lu not aligned to %u", r->n, b, f.nBlockAlign);
    CHECKF(sm * f.nBlockAlign == b, "%s: samples %lu * %u != bytes %lu", r->n, sm, f.nBlockAlign, b);
    for (int i = 0; i < 2; i++) CHECKF(s.hdr[i].dwBytesRecorded == len, "%s: buffer %d recorded %lu", r->n, i, s.hdr[i].dwBytesRecorded);
    EXPECT_EQ(wic_teardown(&s), 0);
}
#define PTEST(i) TEST(wic_pos_consistency_##i) { REQUIRE(U(waveInGetPosition)); NEED_DEV(); pos_consistency(&k_prows[i]); }
PTEST(0) PTEST(1) PTEST(2) PTEST(3) PTEST(4) PTEST(5) PTEST(6) PTEST(7) PTEST(8) PTEST(9)

TEST(wic_pos_device0_vs_mapper) {
    REQUIRE(U(waveInGetPosition)); NEED_DEV();
    wic_s s; WAVEFORMATEX f; wic_pcm(&f, 44100, 16, 1);
    memset(&s, 0, sizeof s);
    s.ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    if (wic_open_cb(&s, 0, &f, (DWORD_PTR)s.ev, 0, CALLBACK_EVENT)) { wic_teardown(&s); return; }
    DWORD len = wic_bytes_ms(&f, 50);
    EXPECT_EQ(wic_buf(&s, 0, len), 0); EXPECT_EQ(wic_add(&s, 0), 0);
    EXPECT_EQ(U(waveInStart)(s.h), 0);
    CHECK(wic_wait_done(&s, 0, FIRST_MS));
    EXPECT_EQ(U(waveInStop)(s.h), 0);
    DWORD b = wic_pos(s.h, TIME_BYTES, NULL);
    CHECK(b >= len);
    EXPECT_EQ(wic_pos(s.h, TIME_SAMPLES, NULL) * 2, b);
    UINT t; wic_pos(s.h, TIME_MS, &t);
    EXPECT_EQ(t, TIME_BYTES);
    EXPECT_EQ(wic_teardown(&s), 0);
}
