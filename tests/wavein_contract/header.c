/* WAVEHDR protocol of waveInPrepareHeader (!0x11760), waveInUnprepareHeader (!0x11800), waveInAddBuffer (!0x118c0).
 *
 * Common front check (before any handle check): pwh NULL || cbwh < 0x30 || dwFlags & 0xFFFFCFE0 -> 11.
 * Prepare: PREPARED already set -> 0 (no handle check, nothing changed); else dwFlags = 0 (ALL bits, including
 *   DONE/BEGINLOOP/ENDLOOP/INQUEUE and the internal 0x1000/0x2000, BEFORE the handle check), handle (5),
 *   WIDM_PREPARE; driver 8 -> success; success sets PREPARED.
 * Unprepare: INQUEUE -> 33; not PREPARED -> 0 (no handle check); handle (5); WIDM_UNPREPARE; success clears PREPARED.
 * AddBuffer: not PREPARED -> 34; INQUEUE -> 33; handle (5); WIDM_ADDBUFFER.
 */
#include "wic.h"

#define P(h, w, c)  U(waveInPrepareHeader)((h), (w), (c))
#define UP(h, w, c) U(waveInUnprepareHeader)((h), (w), (c))
#define AB(h, w, c) U(waveInAddBuffer)((h), (w), (c))
#define REQ3() do { REQUIRE(U(waveInPrepareHeader)); REQUIRE(U(waveInUnprepareHeader)); REQUIRE(U(waveInAddBuffer)); } while (0)

static char g_buf[4096];

/* ---------------------------------------------------------------- front check: NULL header */

TEST(wavein_contract_hdr_null_11_all_three) {
    REQ3();
    for (int i = 0; i < wic_nbad; i++) {
        HWAVEIN h = (HWAVEIN)wic_bad_handles[i];
        CHECKF(P(h, NULL, sizeof(WAVEHDR)) == MMSYSERR_INVALPARAM, "prepare h=%p", h);
        CHECKF(UP(h, NULL, sizeof(WAVEHDR)) == MMSYSERR_INVALPARAM, "unprepare h=%p", h);
        CHECKF(AB(h, NULL, sizeof(WAVEHDR)) == MMSYSERR_INVALPARAM, "addbuffer h=%p", h);
        CHECKF(P(h, NULL, 0) == MMSYSERR_INVALPARAM, "prepare cb0 h=%p", h);
    }
}

/* ---------------------------------------------------------------- front check: cbwh */

static const UINT small_cbs[] = { 0, 1, 4, 8, 16, 32, 40, 44, 47, 0x2F };

static void cb_table(DWORD flags) {
    for (unsigned i = 0; i < sizeof(small_cbs) / sizeof(small_cbs[0]); i++) {
        WAVEHDR w; wic_hdr(&w, g_buf, 64, flags);
        CHECKF(P(NULL, &w, small_cbs[i]) == MMSYSERR_INVALPARAM, "prepare cb=%u fl=0x%lx", small_cbs[i], flags);
        CHECKF(w.dwFlags == flags, "prepare cb=%u flags changed to 0x%lx", small_cbs[i], w.dwFlags);
        CHECKF(UP(NULL, &w, small_cbs[i]) == MMSYSERR_INVALPARAM, "unprepare cb=%u fl=0x%lx", small_cbs[i], flags);
        CHECKF(AB(NULL, &w, small_cbs[i]) == MMSYSERR_INVALPARAM, "addbuffer cb=%u fl=0x%lx", small_cbs[i], flags);
        CHECKF(w.dwFlags == flags, "cb=%u flags changed to 0x%lx", small_cbs[i], w.dwFlags);
    }
}
TEST(wavein_contract_hdr_small_cb_11_flags0) { REQ3(); cb_table(0); }
TEST(wavein_contract_hdr_small_cb_11_prepared) { REQ3(); cb_table(WHDR_PREPARED); }
TEST(wavein_contract_hdr_small_cb_11_inqueue) { REQ3(); cb_table(WHDR_PREPARED | WHDR_INQUEUE); }
TEST(wavein_contract_hdr_small_cb_11_done) { REQ3(); cb_table(WHDR_DONE); }

/* cb 48 (x64 sizeof(WAVEHDR)) is the minimum; larger values pass the front check. */
TEST(wavein_contract_hdr_cb_48_and_larger_pass_front) {
    REQ3();
    static const UINT cbs[] = { 0x30, 0x31, 0x40, 0x100, 0x10000, 0xFFFFFFFF };
    for (unsigned i = 0; i < sizeof(cbs) / sizeof(cbs[0]); i++) {
        WAVEHDR w; wic_hdr(&w, g_buf, 64, 0);
        CHECKF(P(NULL, &w, cbs[i]) == MMSYSERR_INVALHANDLE, "prepare cb=0x%x", cbs[i]);
        wic_hdr(&w, g_buf, 64, 0);
        CHECKF(UP(NULL, &w, cbs[i]) == MMSYSERR_NOERROR, "unprepare cb=0x%x", cbs[i]);
        CHECKF(AB(NULL, &w, cbs[i]) == WAVERR_UNPREPARED, "addbuffer cb=0x%x", cbs[i]);
        wic_hdr(&w, g_buf, 64, WHDR_PREPARED);
        CHECKF(P(NULL, &w, cbs[i]) == MMSYSERR_NOERROR, "prepare(prepared) cb=0x%x", cbs[i]);
    }
}

/* ---------------------------------------------------------------- front check: dwFlags bits */

/* every bit of 0xFFFFCFE0 -> 11 in all three, with and without PREPARED/INQUEUE, header untouched */
static void bad_flag_bits(DWORD base) {
    for (int b = 0; b < 32; b++) {
        DWORD bit = 1u << b;
        if (!(bit & WIC_HDR_BADMASK)) continue;
        DWORD fl = base | bit;
        WAVEHDR w; wic_hdr(&w, g_buf, 64, fl);
        CHECKF(P(NULL, &w, sizeof(w)) == MMSYSERR_INVALPARAM, "prepare fl=0x%lx", fl);
        CHECKF(UP(NULL, &w, sizeof(w)) == MMSYSERR_INVALPARAM, "unprepare fl=0x%lx", fl);
        CHECKF(AB(NULL, &w, sizeof(w)) == MMSYSERR_INVALPARAM, "addbuffer fl=0x%lx", fl);
        CHECKF(w.dwFlags == fl, "fl=0x%lx changed to 0x%lx", fl, w.dwFlags);
    }
}
TEST(wavein_contract_hdr_bad_flag_bits_base0) { REQ3(); bad_flag_bits(0); }
TEST(wavein_contract_hdr_bad_flag_bits_prepared) { REQ3(); bad_flag_bits(WHDR_PREPARED); }
TEST(wavein_contract_hdr_bad_flag_bits_inqueue) { REQ3(); bad_flag_bits(WHDR_PREPARED | WHDR_INQUEUE); }
TEST(wavein_contract_hdr_bad_flag_bits_all_valid) { REQ3(); bad_flag_bits(0x301F); }

/* The internal bits 0x1000 / 0x2000 pass the front check. */
TEST(wavein_contract_hdr_internal_bits_pass_front) {
    REQ3();
    static const DWORD fl[] = { 0x1000, 0x2000, 0x3000 };
    for (int i = 0; i < 3; i++) {
        WAVEHDR w; wic_hdr(&w, g_buf, 64, fl[i] | WHDR_PREPARED);
        CHECKF(P(NULL, &w, sizeof(w)) == MMSYSERR_NOERROR, "prepare fl=0x%lx", fl[i]);
        CHECKF(w.dwFlags == (fl[i] | WHDR_PREPARED), "prepare changed flags");
        CHECKF(UP(NULL, &w, sizeof(w)) == MMSYSERR_INVALHANDLE, "unprepare fl=0x%lx", fl[i]);
        CHECKF(AB(NULL, &w, sizeof(w)) == MMSYSERR_INVALHANDLE, "addbuffer fl=0x%lx", fl[i]);
        wic_hdr(&w, g_buf, 64, fl[i]);
        CHECKF(AB(NULL, &w, sizeof(w)) == WAVERR_UNPREPARED, "addbuffer unprepared fl=0x%lx", fl[i]);
        CHECKF(UP(NULL, &w, sizeof(w)) == MMSYSERR_NOERROR, "unprepare unprepared fl=0x%lx", fl[i]);
    }
}

/* Front check precedes the protocol checks: cb 47 with INQUEUE is 11 for Unprepare/AddBuffer, not 33. */
TEST(wavein_contract_hdr_front_beats_state) {
    REQ3();
    WAVEHDR w; wic_hdr(&w, g_buf, 64, WHDR_PREPARED | WHDR_INQUEUE);
    EXPECT_EQ(UP(NULL, &w, 47), MMSYSERR_INVALPARAM);
    EXPECT_EQ(AB(NULL, &w, 47), MMSYSERR_INVALPARAM);
    wic_hdr(&w, g_buf, 64, 0);
    EXPECT_EQ(AB(NULL, &w, 47), MMSYSERR_INVALPARAM);
    wic_hdr(&w, g_buf, 64, WHDR_PREPARED | 0x20);
    EXPECT_EQ(P(NULL, &w, sizeof(w)), MMSYSERR_INVALPARAM);
}

/* ---------------------------------------------------------------- Prepare */

/* Already PREPARED -> 0 for any handle, header untouched. */
TEST(wavein_contract_prepare_prepared_any_handle_0) {
    REQUIRE(U(waveInPrepareHeader));
    static const DWORD fl[] = { WHDR_PREPARED, WHDR_PREPARED | WHDR_DONE, WHDR_PREPARED | WHDR_INQUEUE,
                                WHDR_PREPARED | WHDR_BEGINLOOP | WHDR_ENDLOOP, 0x301F, 0x1002 };
    for (int i = 0; i < wic_nbad; i++)
        for (unsigned j = 0; j < sizeof(fl) / sizeof(fl[0]); j++) {
            WAVEHDR w; wic_hdr(&w, g_buf, 64, fl[j]); w.dwBytesRecorded = 0x77; w.dwUser = 0x55;
            MMRESULT r = P((HWAVEIN)wic_bad_handles[i], &w, sizeof(w));
            CHECKF(r == MMSYSERR_NOERROR, "h=%p fl=0x%lx r=%u", (void *)wic_bad_handles[i], fl[j], r);
            CHECKF(w.dwFlags == fl[j] && w.dwBytesRecorded == 0x77 && w.dwUser == 0x55 && w.lpData == g_buf && w.dwBufferLength == 64,
                   "h=%p fl=0x%lx header changed", (void *)wic_bad_handles[i], fl[j]);
        }
}

/* Unprepared + invalid handle -> 5, and dwFlags has already been zeroed (all bits). */
TEST(wavein_contract_prepare_bad_handle_5_zeroes_flags) {
    REQUIRE(U(waveInPrepareHeader));
    static const DWORD fl[] = { 0, WHDR_DONE, WHDR_BEGINLOOP, WHDR_ENDLOOP, WHDR_INQUEUE, 0x1D, 0x1000, 0x2000, 0x301D };
    for (int i = 0; i < wic_nbad; i++)
        for (unsigned j = 0; j < sizeof(fl) / sizeof(fl[0]); j++) {
            WAVEHDR w; wic_hdr(&w, g_buf, 64, fl[j]); w.dwBytesRecorded = 0x77;
            MMRESULT r = P((HWAVEIN)wic_bad_handles[i], &w, sizeof(w));
            CHECKF(r == MMSYSERR_INVALHANDLE, "h=%p fl=0x%lx r=%u", (void *)wic_bad_handles[i], fl[j], r);
            CHECKF(w.dwFlags == 0, "h=%p fl=0x%lx -> 0x%lx", (void *)wic_bad_handles[i], fl[j], w.dwFlags);
            CHECK(w.dwBytesRecorded == 0x77 && w.lpData == g_buf && w.dwBufferLength == 64);
        }
}

/* lpData / dwBufferLength are not validated before the handle check. */
TEST(wavein_contract_prepare_data_not_checked_first) {
    REQUIRE(U(waveInPrepareHeader));
    WAVEHDR w; wic_hdr(&w, NULL, 0, 0);
    EXPECT_EQ(P(NULL, &w, sizeof(w)), MMSYSERR_INVALHANDLE);
    wic_hdr(&w, NULL, 0x7FFFFFFF, WHDR_PREPARED);
    EXPECT_EQ(P(NULL, &w, sizeof(w)), MMSYSERR_NOERROR);
}

TEST(wavein_contract_prepare_mmio_handle_5) {
    REQUIRE(U(waveInPrepareHeader));
    HMMIO m = wic_mmio_open();
    if (!m) { t_skip("mmioOpen(memory) failed"); return; }
    WAVEHDR w; wic_hdr(&w, g_buf, 64, WHDR_DONE);
    EXPECT_EQ(P((HWAVEIN)m, &w, sizeof(w)), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(w.dwFlags, 0);
    wic_hdr(&w, g_buf, 64, WHDR_PREPARED);
    EXPECT_EQ(P((HWAVEIN)m, &w, sizeof(w)), MMSYSERR_NOERROR);
    wic_mmio_close(m);
}

TEST(wavein_contract_prepare_closed_mmio_handle_5) {
    REQUIRE(U(waveInPrepareHeader));
    HMMIO m = wic_mmio_open();
    if (!m) { t_skip("mmioOpen(memory) failed"); return; }
    wic_mmio_close(m);
    WAVEHDR w; wic_hdr(&w, g_buf, 64, 0);
    EXPECT_EQ(P((HWAVEIN)m, &w, sizeof(w)), MMSYSERR_INVALHANDLE);
}

/* ---------------------------------------------------------------- Unprepare */

/* INQUEUE -> 33 for any handle (even without PREPARED), header untouched. */
TEST(wavein_contract_unprepare_inqueue_33_any_handle) {
    REQUIRE(U(waveInUnprepareHeader));
    static const DWORD fl[] = { WHDR_INQUEUE, WHDR_INQUEUE | WHDR_PREPARED, WHDR_INQUEUE | WHDR_DONE, 0x301F };
    for (int i = 0; i < wic_nbad; i++)
        for (unsigned j = 0; j < 4; j++) {
            WAVEHDR w; wic_hdr(&w, g_buf, 64, fl[j]);
            MMRESULT r = UP((HWAVEIN)wic_bad_handles[i], &w, sizeof(w));
            CHECKF(r == WAVERR_STILLPLAYING, "h=%p fl=0x%lx r=%u", (void *)wic_bad_handles[i], fl[j], r);
            CHECKF(w.dwFlags == fl[j], "flags changed");
        }
}

/* Not PREPARED (and not INQUEUE) -> 0 for any handle, header untouched. */
TEST(wavein_contract_unprepare_unprepared_0_any_handle) {
    REQUIRE(U(waveInUnprepareHeader));
    static const DWORD fl[] = { 0, WHDR_DONE, WHDR_BEGINLOOP, WHDR_ENDLOOP, 0xD, 0x1000, 0x2000, 0x300D };
    for (int i = 0; i < wic_nbad; i++)
        for (unsigned j = 0; j < sizeof(fl) / sizeof(fl[0]); j++) {
            WAVEHDR w; wic_hdr(&w, g_buf, 64, fl[j]);
            MMRESULT r = UP((HWAVEIN)wic_bad_handles[i], &w, sizeof(w));
            CHECKF(r == MMSYSERR_NOERROR, "h=%p fl=0x%lx r=%u", (void *)wic_bad_handles[i], fl[j], r);
            CHECKF(w.dwFlags == fl[j], "flags changed");
        }
}

/* PREPARED + invalid handle -> 5, flags untouched (still PREPARED). */
TEST(wavein_contract_unprepare_prepared_bad_handle_5) {
    REQUIRE(U(waveInUnprepareHeader));
    static const DWORD fl[] = { WHDR_PREPARED, WHDR_PREPARED | WHDR_DONE, 0x300F };
    for (int i = 0; i < wic_nbad; i++)
        for (unsigned j = 0; j < 3; j++) {
            WAVEHDR w; wic_hdr(&w, g_buf, 64, fl[j]);
            MMRESULT r = UP((HWAVEIN)wic_bad_handles[i], &w, sizeof(w));
            CHECKF(r == MMSYSERR_INVALHANDLE, "h=%p fl=0x%lx r=%u", (void *)wic_bad_handles[i], fl[j], r);
            CHECKF(w.dwFlags == fl[j], "flags changed to 0x%lx", w.dwFlags);
        }
}

TEST(wavein_contract_unprepare_mmio_handle_5) {
    REQUIRE(U(waveInUnprepareHeader));
    HMMIO m = wic_mmio_open();
    if (!m) { t_skip("mmioOpen(memory) failed"); return; }
    WAVEHDR w; wic_hdr(&w, g_buf, 64, WHDR_PREPARED);
    EXPECT_EQ(UP((HWAVEIN)m, &w, sizeof(w)), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(w.dwFlags, WHDR_PREPARED);
    wic_mmio_close(m);
}

/* ---------------------------------------------------------------- AddBuffer */

/* Not PREPARED -> 34 for any handle, even with INQUEUE set. */
TEST(wavein_contract_addbuffer_unprepared_34_any_handle) {
    REQUIRE(U(waveInAddBuffer));
    static const DWORD fl[] = { 0, WHDR_DONE, WHDR_INQUEUE, WHDR_INQUEUE | WHDR_DONE, 0xD, 0x301D };
    for (int i = 0; i < wic_nbad; i++)
        for (unsigned j = 0; j < sizeof(fl) / sizeof(fl[0]); j++) {
            WAVEHDR w; wic_hdr(&w, g_buf, 64, fl[j]);
            MMRESULT r = AB((HWAVEIN)wic_bad_handles[i], &w, sizeof(w));
            CHECKF(r == WAVERR_UNPREPARED, "h=%p fl=0x%lx r=%u", (void *)wic_bad_handles[i], fl[j], r);
            CHECKF(w.dwFlags == fl[j], "flags changed");
        }
}

/* PREPARED|INQUEUE -> 33 for any handle. */
TEST(wavein_contract_addbuffer_inqueue_33_any_handle) {
    REQUIRE(U(waveInAddBuffer));
    for (int i = 0; i < wic_nbad; i++) {
        WAVEHDR w; wic_hdr(&w, g_buf, 64, WHDR_PREPARED | WHDR_INQUEUE);
        CHECKF(AB((HWAVEIN)wic_bad_handles[i], &w, sizeof(w)) == WAVERR_STILLPLAYING, "h=%p", (void *)wic_bad_handles[i]);
        CHECK(w.dwFlags == (WHDR_PREPARED | WHDR_INQUEUE));
    }
}

/* PREPARED, not INQUEUE + invalid handle -> 5, flags untouched. */
TEST(wavein_contract_addbuffer_prepared_bad_handle_5) {
    REQUIRE(U(waveInAddBuffer));
    static const DWORD fl[] = { WHDR_PREPARED, WHDR_PREPARED | WHDR_DONE, 0x300F };
    for (int i = 0; i < wic_nbad; i++)
        for (unsigned j = 0; j < 3; j++) {
            WAVEHDR w; wic_hdr(&w, g_buf, 64, fl[j]);
            MMRESULT r = AB((HWAVEIN)wic_bad_handles[i], &w, sizeof(w));
            CHECKF(r == MMSYSERR_INVALHANDLE, "h=%p fl=0x%lx r=%u", (void *)wic_bad_handles[i], fl[j], r);
            CHECKF(w.dwFlags == fl[j], "flags changed");
        }
}

TEST(wavein_contract_addbuffer_mmio_handle_5) {
    REQUIRE(U(waveInAddBuffer));
    HMMIO m = wic_mmio_open();
    if (!m) { t_skip("mmioOpen(memory) failed"); return; }
    WAVEHDR w; wic_hdr(&w, g_buf, 64, WHDR_PREPARED);
    EXPECT_EQ(AB((HWAVEIN)m, &w, sizeof(w)), MMSYSERR_INVALHANDLE);
    wic_hdr(&w, g_buf, 64, 0);
    EXPECT_EQ(AB((HWAVEIN)m, &w, sizeof(w)), WAVERR_UNPREPARED);
    wic_mmio_close(m);
}

/* ---------------------------------------------------------------- device half: real handle */

static HWAVEIN open_dev(void) { return wic_open_any(); }

/* Prepare on a real handle sets dwFlags to exactly PREPARED (DONE/BEGINLOOP/ENDLOOP cleared). */
TEST(wavein_contract_prepare_dev_sets_exactly_prepared) {
    REQ3();
    HWAVEIN h = open_dev(); if (!h) return;
    static const DWORD fl[] = { 0, WHDR_DONE, WHDR_BEGINLOOP, WHDR_ENDLOOP, WHDR_BEGINLOOP | WHDR_ENDLOOP, 0xD, 0x1000, 0x2000, WHDR_INQUEUE };
    for (unsigned j = 0; j < sizeof(fl) / sizeof(fl[0]); j++) {
        WAVEHDR w; wic_hdr(&w, g_buf, 1024, fl[j]); w.dwBytesRecorded = 0x77; w.dwUser = 0x1234;
        MMRESULT r = P(h, &w, sizeof(w));
        CHECKF(r == MMSYSERR_NOERROR, "fl=0x%lx r=%u", fl[j], r);
        CHECKF(w.dwFlags == WHDR_PREPARED, "fl=0x%lx -> 0x%lx", fl[j], w.dwFlags);
        CHECK(w.lpData == g_buf && w.dwBufferLength == 1024 && w.dwUser == 0x1234);
        if (w.dwBytesRecorded != 0x77) t_note("prepare fl=0x%lx changed dwBytesRecorded to %lu", fl[j], w.dwBytesRecorded);
        EXPECT_EQ(UP(h, &w, sizeof(w)), MMSYSERR_NOERROR);
        CHECKF(w.dwFlags == 0, "after unprepare 0x%lx", w.dwFlags);
    }
    EXPECT_EQ(U(waveInClose)(h), MMSYSERR_NOERROR);
}

TEST(wavein_contract_prepare_dev_twice_0) {
    REQ3();
    HWAVEIN h = open_dev(); if (!h) return;
    WAVEHDR w; wic_hdr(&w, g_buf, 1024, 0);
    EXPECT_EQ(P(h, &w, sizeof(w)), MMSYSERR_NOERROR);
    EXPECT_EQ(P(h, &w, sizeof(w)), MMSYSERR_NOERROR);
    EXPECT_EQ(w.dwFlags, WHDR_PREPARED);
    EXPECT_EQ(UP(h, &w, sizeof(w)), MMSYSERR_NOERROR);
    EXPECT_EQ(UP(h, &w, sizeof(w)), MMSYSERR_NOERROR);
    EXPECT_EQ(w.dwFlags, 0);
    EXPECT_EQ(U(waveInClose)(h), MMSYSERR_NOERROR);
}

/* Prepare with a large cbwh (bigger than WAVEHDR) on a real handle. */
TEST(wavein_contract_prepare_dev_large_cb) {
    REQ3();
    HWAVEIN h = open_dev(); if (!h) return;
    struct { WAVEHDR w; char pad[64]; } x; memset(&x, 0, sizeof(x));
    wic_hdr(&x.w, g_buf, 1024, 0);
    MMRESULT r = P(h, &x.w, sizeof(x));
    t_note("prepare cb=%u -> %u flags=0x%lx", (unsigned)sizeof(x), r, x.w.dwFlags);
    CHECK(r == MMSYSERR_NOERROR);
    if (r == MMSYSERR_NOERROR) EXPECT_EQ(UP(h, &x.w, sizeof(x)), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveInClose)(h), MMSYSERR_NOERROR);
}

TEST(wavein_contract_prepare_dev_zero_length) {
    REQ3();
    HWAVEIN h = open_dev(); if (!h) return;
    WAVEHDR w; wic_hdr(&w, g_buf, 0, 0);
    MMRESULT r = P(h, &w, sizeof(w));
    t_note("prepare len 0 -> %u flags=0x%lx", r, w.dwFlags);
    if (r == MMSYSERR_NOERROR) EXPECT_EQ(UP(h, &w, sizeof(w)), MMSYSERR_NOERROR);
    wic_hdr(&w, NULL, 0, 0);
    r = P(h, &w, sizeof(w));
    t_note("prepare NULL data len 0 -> %u flags=0x%lx", r, w.dwFlags);
    if (r == MMSYSERR_NOERROR) EXPECT_EQ(UP(h, &w, sizeof(w)), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveInClose)(h), MMSYSERR_NOERROR);
}

/* A header prepared on one handle: Unprepare is handle-checked but accepts another waveIn handle. */
TEST(wavein_contract_unprepare_dev_wrong_but_valid_handle) {
    REQ3();
    if (!wic_nin()) { t_skip("no waveIn device"); return; }
    HWAVEIN a = NULL, b = NULL;
    if (wic_open(&a, WAVE_MAPPER) || wic_open(&b, WAVE_MAPPER)) {
        t_skip("two concurrent opens not possible");
        if (a) U(waveInClose)(a);
        return;
    }
    WAVEHDR w; wic_hdr(&w, g_buf, 1024, 0);
    EXPECT_EQ(P(a, &w, sizeof(w)), MMSYSERR_NOERROR);
    MMRESULT r = UP(b, &w, sizeof(w));
    t_note("unprepare on other handle -> %u flags=0x%lx", r, w.dwFlags);
    if (w.dwFlags & WHDR_PREPARED) EXPECT_EQ(UP(a, &w, sizeof(w)), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveInClose)(b), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveInClose)(a), MMSYSERR_NOERROR);
}

/* AddBuffer protocol on a stopped device: queued -> PREPARED|INQUEUE, second add 33, unprepare 33,
 * close 33 (handle stays valid), reset returns it DONE with nothing recorded, then unprepare/close. */
TEST(wavein_contract_addbuffer_dev_queue_protocol) {
    REQ3(); REQUIRE(U(waveInReset)); REQUIRE(U(waveInClose));
    HWAVEIN h = open_dev(); if (!h) return;
    WAVEHDR w; wic_hdr(&w, g_buf, 2048, 0);
    EXPECT_EQ(P(h, &w, sizeof(w)), MMSYSERR_NOERROR);
    EXPECT_EQ(AB(h, &w, sizeof(w)), MMSYSERR_NOERROR);
    EXPECT_EQ(w.dwFlags, WHDR_PREPARED | WHDR_INQUEUE);
    EXPECT_EQ(AB(h, &w, sizeof(w)), WAVERR_STILLPLAYING);
    EXPECT_EQ(UP(h, &w, sizeof(w)), WAVERR_STILLPLAYING);
    EXPECT_EQ(w.dwFlags, WHDR_PREPARED | WHDR_INQUEUE);
    MMRESULT rc = U(waveInClose)(h);
    EXPECT_EQ(rc, WAVERR_STILLPLAYING);
    if (rc == MMSYSERR_NOERROR) return;  /* handle is gone */
    EXPECT_EQ(U(waveInReset)(h), MMSYSERR_NOERROR);
    for (int i = 0; i < 100 && (w.dwFlags & WHDR_INQUEUE); i++) Sleep(10);
    EXPECT_EQ(w.dwFlags, WHDR_PREPARED | WHDR_DONE);
    EXPECT_EQ(w.dwBytesRecorded, 0);
    EXPECT_EQ(UP(h, &w, sizeof(w)), MMSYSERR_NOERROR);
    EXPECT_EQ(w.dwFlags, WHDR_DONE);
    EXPECT_EQ(U(waveInClose)(h), MMSYSERR_NOERROR);
}

/* AddBuffer on a real handle preserves DONE? record what the driver leaves in dwFlags/dwBytesRecorded. */
TEST(wavein_contract_addbuffer_dev_flags_after_add) {
    REQ3(); REQUIRE(U(waveInReset)); REQUIRE(U(waveInClose));
    HWAVEIN h = open_dev(); if (!h) return;
    WAVEHDR w; wic_hdr(&w, g_buf, 2048, 0);
    EXPECT_EQ(P(h, &w, sizeof(w)), MMSYSERR_NOERROR);
    w.dwFlags |= WHDR_DONE; w.dwBytesRecorded = 99;
    EXPECT_EQ(AB(h, &w, sizeof(w)), MMSYSERR_NOERROR);
    t_note("add with DONE preset -> flags=0x%lx rec=%lu", w.dwFlags, w.dwBytesRecorded);
    CHECK(w.dwFlags & WHDR_INQUEUE);
    CHECK(!(w.dwFlags & WHDR_DONE));
    EXPECT_EQ(U(waveInReset)(h), MMSYSERR_NOERROR);
    for (int i = 0; i < 100 && (w.dwFlags & WHDR_INQUEUE); i++) Sleep(10);
    EXPECT_EQ(UP(h, &w, sizeof(w)), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveInClose)(h), MMSYSERR_NOERROR);
}

/* AddBuffer of a header prepared on the handle with cb 47 fails at the front check, queue unchanged. */
TEST(wavein_contract_addbuffer_dev_front_check) {
    REQ3(); REQUIRE(U(waveInClose));
    HWAVEIN h = open_dev(); if (!h) return;
    WAVEHDR w; wic_hdr(&w, g_buf, 2048, 0);
    EXPECT_EQ(AB(h, &w, sizeof(w)), WAVERR_UNPREPARED);
    EXPECT_EQ(P(h, &w, sizeof(w)), MMSYSERR_NOERROR);
    EXPECT_EQ(AB(h, &w, 47), MMSYSERR_INVALPARAM);
    EXPECT_EQ(AB(h, NULL, sizeof(w)), MMSYSERR_INVALPARAM);
    EXPECT_EQ(w.dwFlags, WHDR_PREPARED);
    EXPECT_EQ(UP(h, &w, sizeof(w)), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveInClose)(h), MMSYSERR_NOERROR);
}

/* Header ladder on a closed handle: prepare 5 (flags zeroed), unprepare(prepared) 5, add 5. */
TEST(wavein_contract_hdr_dev_closed_handle) {
    REQ3();
    HWAVEIN h = wic_closed_handle(); if (!h) return;
    WAVEHDR w; wic_hdr(&w, g_buf, 64, WHDR_DONE);
    EXPECT_EQ(P(h, &w, sizeof(w)), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(w.dwFlags, 0);
    w.dwFlags = WHDR_PREPARED;
    EXPECT_EQ(UP(h, &w, sizeof(w)), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(AB(h, &w, sizeof(w)), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(P(h, &w, sizeof(w)), MMSYSERR_NOERROR);
}

/* Header ladder with an HWAVEOUT: type 1 is not type 2 -> 5. */
TEST(wavein_contract_hdr_waveout_handle_5) {
    REQ3();
    HWAVEOUT o = wic_waveout_open();
    if (!o) { t_skip("no waveOut device to open"); return; }
    HWAVEIN h = (HWAVEIN)o;
    WAVEHDR w; wic_hdr(&w, g_buf, 64, WHDR_DONE);
    EXPECT_EQ(P(h, &w, sizeof(w)), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(w.dwFlags, 0);
    w.dwFlags = WHDR_PREPARED;
    EXPECT_EQ(UP(h, &w, sizeof(w)), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(AB(h, &w, sizeof(w)), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveOutClose)(o), MMSYSERR_NOERROR);
}

/* And vice versa: waveOut header calls reject an HWAVEIN. */
TEST(wavein_contract_hdr_wavein_handle_to_waveout_5) {
    REQUIRE(U(waveOutPrepareHeader)); REQUIRE(U(waveOutUnprepareHeader)); REQUIRE(U(waveOutWrite));
    HWAVEIN h = open_dev(); if (!h) return;
    HWAVEOUT o = (HWAVEOUT)h;
    WAVEHDR w; wic_hdr(&w, g_buf, 64, 0);
    EXPECT_EQ(U(waveOutPrepareHeader)(o, &w, sizeof(w)), MMSYSERR_INVALHANDLE);
    w.dwFlags = WHDR_PREPARED;
    EXPECT_EQ(U(waveOutUnprepareHeader)(o, &w, sizeof(w)), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveOutWrite)(o, &w, sizeof(w)), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveInClose)(h), MMSYSERR_NOERROR);
}
