/* time_basic: timeGetDevCaps.
 * kernel32!timeGetDevCaps: (TimeInit failed || cbtc < 8 || ptc == NULL) -> 97 TIMERR_NOCANDO
 * (not TIMERR_STRUCT, not MMSYSERR_INVALPARAM); otherwise writes exactly two UINTs
 * { wPeriodMin = TDD_MAXRESOLUTION, wPeriodMax = 1000000 } and returns 0.
 */
#include "tb.h"

static void fill(BYTE *b, size_t n, BYTE v) { memset(b, v, n); }
static int untouched(const BYTE *b, size_t from, size_t to, BYTE v) {
    for (size_t i = from; i < to; i++) if (b[i] != v) return 0;
    return 1;
}

TEST(time_basic_devcaps_constants) {
    REQUIRE(U(timeGetDevCaps));
    EXPECT_EQ(TIMERR_NOERROR, 0);
    EXPECT_EQ(TIMERR_NOCANDO, 97);
    EXPECT_EQ(TIMERR_STRUCT, 129);
    EXPECT_EQ(TIMERR_BASE, 96);
    EXPECT_EQ(sizeof(TIMECAPS), 8);
    EXPECT_EQ(offsetof(TIMECAPS, wPeriodMin), 0);
    EXPECT_EQ(offsetof(TIMECAPS, wPeriodMax), 4);
}

TEST(time_basic_devcaps_ok) {
    REQUIRE(U(timeGetDevCaps));
    TIMECAPS tc; memset(&tc, 0xCC, sizeof tc);
    EXPECT_EQ(U(timeGetDevCaps)(&tc, sizeof tc), TIMERR_NOERROR);
    CHECKF(tc.wPeriodMin >= 1, "wPeriodMin=%u", tc.wPeriodMin);
    EXPECT_EQ(tc.wPeriodMax, 1000000);
    CHECK(tc.wPeriodMin <= tc.wPeriodMax);
    t_note("wPeriodMin=%u wPeriodMax=%u", tc.wPeriodMin, tc.wPeriodMax);
}

TEST(time_basic_devcaps_min_matches_ntquery) {
    REQUIRE(U(timeGetDevCaps));
    ULONG mx, mn, cur;
    if (!tb_res(&mx, &mn, &cur)) { t_skip("NtQueryTimerResolution unavailable"); return; }
    t_note("NtQueryTimerResolution max=%lu min=%lu cur=%lu (100ns)", mx, mn, cur);
    TIMECAPS tc; memset(&tc, 0, sizeof tc);
    EXPECT_EQ(U(timeGetDevCaps)(&tc, sizeof tc), 0);
    EXPECT_EQ(tc.wPeriodMin, tb_pmin());
    /* wPeriodMax is a constant, unrelated to the coarsest kernel resolution */
    EXPECT_EQ(tc.wPeriodMax, 1000000);
    CHECK(tc.wPeriodMax != tb_umax());
}

TEST(time_basic_devcaps_null_ptr_sizeof) {
    REQUIRE(U(timeGetDevCaps));
    EXPECT_EQ(U(timeGetDevCaps)(NULL, sizeof(TIMECAPS)), TB_NOCANDO);
}

TEST(time_basic_devcaps_null_ptr_zero) {
    REQUIRE(U(timeGetDevCaps));
    EXPECT_EQ(U(timeGetDevCaps)(NULL, 0), TB_NOCANDO);
}

TEST(time_basic_devcaps_null_ptr_table) {
    REQUIRE(U(timeGetDevCaps));
    static const UINT cbs[] = { 0, 1, 4, 7, 8, 9, 12, 16, 64, 4096, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF };
    for (size_t i = 0; i < sizeof cbs / sizeof cbs[0]; i++) {
        MMRESULT r = U(timeGetDevCaps)(NULL, cbs[i]);
        CHECKF(r == TB_NOCANDO, "cb=%u r=%u", cbs[i], r);
    }
}

TEST(time_basic_devcaps_not_struct_or_invalparam) {
    REQUIRE(U(timeGetDevCaps));
    TIMECAPS tc;
    MMRESULT r = U(timeGetDevCaps)(&tc, 7);
    CHECK(r != TIMERR_STRUCT);
    CHECK(r != MMSYSERR_INVALPARAM);
    CHECK(r != MMSYSERR_ERROR);
    EXPECT_EQ(r, TIMERR_NOCANDO);
}

TEST(time_basic_devcaps_small_cb_table) {
    REQUIRE(U(timeGetDevCaps));
    for (UINT cb = 0; cb < 8; cb++) {
        BYTE b[32]; fill(b, sizeof b, 0xA5);
        MMRESULT r = U(timeGetDevCaps)((LPTIMECAPS)b, cb);
        CHECKF(r == TB_NOCANDO, "cb=%u r=%u", cb, r);
        CHECKF(untouched(b, 0, sizeof b, 0xA5), "cb=%u wrote into the buffer on failure", cb);
    }
}

TEST(time_basic_devcaps_cb7) {
    REQUIRE(U(timeGetDevCaps));
    TIMECAPS tc; memset(&tc, 0x5A, sizeof tc);
    EXPECT_EQ(U(timeGetDevCaps)(&tc, 7), TB_NOCANDO);
    EXPECT_EQ(tc.wPeriodMin, 0x5A5A5A5A);
    EXPECT_EQ(tc.wPeriodMax, 0x5A5A5A5A);
}

TEST(time_basic_devcaps_cb8_exact) {
    REQUIRE(U(timeGetDevCaps));
    BYTE b[16]; fill(b, sizeof b, 0xEE);
    EXPECT_EQ(U(timeGetDevCaps)((LPTIMECAPS)b, 8), 0);
    TIMECAPS *tc = (TIMECAPS *)b;
    EXPECT_EQ(tc->wPeriodMin, tb_pmin());
    EXPECT_EQ(tc->wPeriodMax, 1000000);
    CHECK(untouched(b, 8, 16, 0xEE));
}

TEST(time_basic_devcaps_large_cb_table) {
    REQUIRE(U(timeGetDevCaps));
    /* only 8 bytes are written, whatever cbtc says */
    static const UINT cbs[] = { 8, 9, 10, 11, 12, 15, 16, 24, 32, 64, 0x100, 0x1000, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF };
    UINT pmin = tb_pmin();
    for (size_t i = 0; i < sizeof cbs / sizeof cbs[0]; i++) {
        BYTE b[64]; fill(b, sizeof b, 0x3C);
        MMRESULT r = U(timeGetDevCaps)((LPTIMECAPS)b, cbs[i]);
        CHECKF(r == 0, "cb=%u r=%u", cbs[i], r);
        TIMECAPS *tc = (TIMECAPS *)b;
        CHECKF(tc->wPeriodMin == pmin, "cb=%u min=%u", cbs[i], tc->wPeriodMin);
        CHECKF(tc->wPeriodMax == 1000000, "cb=%u max=%u", cbs[i], tc->wPeriodMax);
        CHECKF(untouched(b, 8, sizeof b, 0x3C), "cb=%u wrote past 8 bytes", cbs[i]);
    }
}

TEST(time_basic_devcaps_every_cb_0_to_64) {
    REQUIRE(U(timeGetDevCaps));
    UINT pmin = tb_pmin();
    for (UINT cb = 0; cb <= 64; cb++) {
        BYTE b[80]; fill(b, sizeof b, 0x77);
        MMRESULT r = U(timeGetDevCaps)((LPTIMECAPS)b, cb);
        if (cb < 8) {
            CHECKF(r == TB_NOCANDO, "cb=%u r=%u", cb, r);
            CHECKF(untouched(b, 0, sizeof b, 0x77), "cb=%u", cb);
        } else {
            CHECKF(r == 0, "cb=%u r=%u", cb, r);
            CHECKF(((TIMECAPS *)b)->wPeriodMin == pmin, "cb=%u", cb);
            CHECKF(((TIMECAPS *)b)->wPeriodMax == 1000000, "cb=%u", cb);
            CHECKF(untouched(b, 8, sizeof b, 0x77), "cb=%u", cb);
        }
    }
}

TEST(time_basic_devcaps_overwrites_garbage) {
    REQUIRE(U(timeGetDevCaps));
    static const UINT seeds[] = { 0, 1, 0xFFFFFFFF, 0x80000000, 1000000, 16 };
    for (size_t i = 0; i < sizeof seeds / sizeof seeds[0]; i++) {
        TIMECAPS tc; tc.wPeriodMin = seeds[i]; tc.wPeriodMax = ~seeds[i];
        EXPECT_EQ(U(timeGetDevCaps)(&tc, sizeof tc), 0);
        EXPECT_EQ(tc.wPeriodMin, tb_pmin());
        EXPECT_EQ(tc.wPeriodMax, 1000000);
    }
}

TEST(time_basic_devcaps_unaligned_buffer) {
    REQUIRE(U(timeGetDevCaps));
    BYTE b[32]; fill(b, sizeof b, 0x11);
    EXPECT_EQ(U(timeGetDevCaps)((LPTIMECAPS)(b + 1), 8), 0);
    UINT mn, mx; memcpy(&mn, b + 1, 4); memcpy(&mx, b + 5, 4);
    EXPECT_EQ(mn, tb_pmin());
    EXPECT_EQ(mx, 1000000);
    EXPECT_EQ(b[0], 0x11);
    CHECK(untouched(b, 9, sizeof b, 0x11));
}

TEST(time_basic_devcaps_stable_repeated) {
    REQUIRE(U(timeGetDevCaps));
    TIMECAPS a; EXPECT_EQ(U(timeGetDevCaps)(&a, sizeof a), 0);
    int same = 1;
    for (int i = 0; i < 500; i++) {
        TIMECAPS b; memset(&b, 0, sizeof b);
        if (U(timeGetDevCaps)(&b, sizeof b) != 0 || b.wPeriodMin != a.wPeriodMin || b.wPeriodMax != a.wPeriodMax) same = 0;
    }
    CHECK(same);
}

TEST(time_basic_devcaps_unaffected_by_begin_period) {
    REQUIRE(U(timeGetDevCaps)); REQUIRE(U(timeBeginPeriod)); REQUIRE(U(timeEndPeriod));
    TIMECAPS a, b, c;
    EXPECT_EQ(U(timeGetDevCaps)(&a, sizeof a), 0);
    EXPECT_EQ(tb_begin(a.wPeriodMin), 0);
    EXPECT_EQ(U(timeGetDevCaps)(&b, sizeof b), 0);
    EXPECT_EQ(tb_end(a.wPeriodMin), 0);
    EXPECT_EQ(U(timeGetDevCaps)(&c, sizeof c), 0);
    EXPECT_EQ(b.wPeriodMin, a.wPeriodMin); EXPECT_EQ(b.wPeriodMax, a.wPeriodMax);
    EXPECT_EQ(c.wPeriodMin, a.wPeriodMin); EXPECT_EQ(c.wPeriodMax, a.wPeriodMax);
}

TEST(time_basic_devcaps_winmm_vs_kernel32) {
    REQUIRE(U(timeGetDevCaps));
    REQUIRE(UIN("kernel32.dll", timeGetDevCaps));
    TIMECAPS a, b; memset(&a, 1, sizeof a); memset(&b, 2, sizeof b);
    EXPECT_EQ(UIN("winmm.dll", timeGetDevCaps)(&a, sizeof a), 0);
    EXPECT_EQ(UIN("kernel32.dll", timeGetDevCaps)(&b, sizeof b), 0);
    EXPECT_EQ(a.wPeriodMin, b.wPeriodMin);
    EXPECT_EQ(a.wPeriodMax, b.wPeriodMax);
    EXPECT_EQ(UIN("kernel32.dll", timeGetDevCaps)(NULL, 8), TB_NOCANDO);
    EXPECT_EQ(UIN("kernel32.dll", timeGetDevCaps)(&b, 7), TB_NOCANDO);
}

TEST(time_basic_devcaps_preserves_last_error) {
    REQUIRE(U(timeGetDevCaps));
    TIMECAPS tc;
    SET_ERR(0xBEEF);
    EXPECT_EQ(U(timeGetDevCaps)(&tc, sizeof tc), 0);
    EXPECT_ERR(0xBEEF);
    SET_ERR(0xBEEF);
    EXPECT_EQ(U(timeGetDevCaps)(NULL, sizeof tc), TB_NOCANDO);
    EXPECT_ERR(0xBEEF);
    SET_ERR(0xBEEF);
    EXPECT_EQ(U(timeGetDevCaps)(&tc, 3), TB_NOCANDO);
    EXPECT_ERR(0xBEEF);
}

typedef struct { UINT bad; MMRESULT r[64]; TIMECAPS tc[64]; } dc_thr;
static DWORD WINAPI dc_thread(LPVOID p) {
    dc_thr *t = (dc_thr *)p;
    for (int i = 0; i < 64; i++) {
        memset(&t->tc[i], 0, sizeof t->tc[i]);
        t->r[i] = U(timeGetDevCaps)(&t->tc[i], (i & 1) ? 8 : 7);
    }
    return 0;
}
TEST(time_basic_devcaps_threads) {
    REQUIRE(U(timeGetDevCaps));
    static dc_thr th[4]; HANDLE h[4];
    for (int i = 0; i < 4; i++) { memset(&th[i], 0, sizeof th[i]); h[i] = CreateThread(NULL, 0, dc_thread, &th[i], 0, NULL); CHECK(h[i] != NULL); }
    EXPECT_EQ(WaitForMultipleObjects(4, h, TRUE, 5000), WAIT_OBJECT_0);
    UINT pmin = tb_pmin();
    for (int i = 0; i < 4; i++) {
        int ok = 1;
        for (int k = 0; k < 64; k++) {
            if (k & 1) { if (th[i].r[k] != 0 || th[i].tc[k].wPeriodMin != pmin || th[i].tc[k].wPeriodMax != 1000000) ok = 0; }
            else if (th[i].r[k] != TB_NOCANDO || th[i].tc[k].wPeriodMin != 0) ok = 0;
        }
        CHECKF(ok, "thread %d", i);
        CloseHandle(h[i]);
    }
}

/* A fresh process: timeGetDevCaps is the very first time API called (it runs TimeInit). */
CHILD(tb_devcaps_first) {
    TIMECAPS tc; memset(&tc, 0, sizeof tc);
    if (U(timeGetDevCaps)(NULL, 8) != 97) return 10;
    if (U(timeGetDevCaps)(&tc, 7) != 97) return 11;
    if (U(timeGetDevCaps)(&tc, 8) != 0) return 12;
    if (tc.wPeriodMin != tb_pmin()) return 13;
    if (tc.wPeriodMax != 1000000) return 14;
    return 0;
}
TEST(time_basic_devcaps_first_call_in_fresh_process) {
    REQUIRE(U(timeGetDevCaps));
    EXPECT_EQ(t_run_child("tb_devcaps_first", NULL, 20000), 0);
}
