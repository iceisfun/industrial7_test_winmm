/* time_basic: timeBeginPeriod / timeEndPeriod counting contract.
 * kernel32!timeBeginPeriod: uPeriod < wPeriodMin -> 97; uPeriod >= uMax -> 0 and nothing is
 *   counted; else count[uPeriod - wPeriodMin]++ (a count already at 0xFFFF -> 97); on the 0->1
 *   transition, if uPeriod is finer than the process' current setting, NtSetTimerResolution
 *   (failure undoes the count and returns 97).
 * kernel32!timeEndPeriod: uPeriod < wPeriodMin -> 97; uPeriod >= uMax -> 0; count already 0 -> 97;
 *   else count--, and on the last release of the active period the resolution falls back to the
 *   finest period still counted, or is released entirely when none is.
 * uMax = ceil(NtQueryTimerResolution coarsest / 10000) (16 for the 15.625 ms tick), computed at
 * run time here; every Begin is balanced before a test returns.
 */
#include "tb.h"

#define B(p) tb_begin(p)
#define E(p) tb_end(p)

/* periods that are accepted and never counted (all >= uMax for any uMax <= 55) */
static const UINT k_uncounted_abs[] = { 55, 56, 60, 64, 99, 100, 255, 256, 500, 999, 1000, 1001, 4096, 65535, 65536,
                                         999999, 1000000, 1000001, 0x00FFFFFF, 0x7FFFFFFE, 0x7FFFFFFF, 0x80000000,
                                         0xFFFFFFFE, 0xFFFFFFFF };

TEST(time_basic_period_bounds_note) {
    REQUIRE(U(timeBeginPeriod)); REQUIRE(U(timeEndPeriod));
    ULONG mx = 0, mn = 0, cur = 0;
    CHECK(tb_res(&mx, &mn, &cur));
    t_note("NtQueryTimerResolution coarsest=%lu finest=%lu current=%lu -> wPeriodMin=%u uMax=%u", mx, mn, cur, tb_pmin(), tb_umax());
    CHECK(tb_pmin() >= 1);
    CHECK(tb_umax() > tb_pmin());
    CHECK(tb_umax() <= 55);
}

TEST(time_basic_period_begin0) {
    REQUIRE(U(timeBeginPeriod));
    EXPECT_EQ(B(0), TB_NOCANDO);
}

TEST(time_basic_period_end0) {
    REQUIRE(U(timeEndPeriod));
    EXPECT_EQ(E(0), TB_NOCANDO);
}

TEST(time_basic_period_below_min_table) {
    REQUIRE(U(timeBeginPeriod)); REQUIRE(U(timeEndPeriod));
    UINT pmin = tb_pmin();
    for (UINT p = 0; p < pmin; p++) {
        CHECKF(B(p) == TB_NOCANDO, "Begin(%u)", p);
        CHECKF(E(p) == TB_NOCANDO, "End(%u)", p);
    }
    /* Begin(0) does not count anything: End(0) after it is still 97 */
    EXPECT_EQ(B(0), TB_NOCANDO);
    EXPECT_EQ(E(0), TB_NOCANDO);
}

TEST(time_basic_period_1_pair_then_unbalanced) {
    REQUIRE(U(timeBeginPeriod)); REQUIRE(U(timeEndPeriod));
    if (tb_pmin() > 1) { t_skip("wPeriodMin=%u", tb_pmin()); return; }
    EXPECT_EQ(B(1), 0);
    EXPECT_EQ(E(1), 0);
    EXPECT_EQ(E(1), TB_NOCANDO);
}

TEST(time_basic_period_2_nested_twice) {
    REQUIRE(U(timeBeginPeriod)); REQUIRE(U(timeEndPeriod));
    EXPECT_EQ(B(2), 0);
    EXPECT_EQ(B(2), 0);
    EXPECT_EQ(E(2), 0);
    EXPECT_EQ(E(2), 0);
    EXPECT_EQ(E(2), TB_NOCANDO);
}

TEST(time_basic_period_end7_unbalanced) {
    REQUIRE(U(timeEndPeriod));
    EXPECT_EQ(E(7), TB_NOCANDO);
    EXPECT_EQ(E(7), TB_NOCANDO);
}

TEST(time_basic_period_15_is_counted) {
    REQUIRE(U(timeBeginPeriod)); REQUIRE(U(timeEndPeriod));
    if (tb_umax() <= 15) { t_skip("uMax=%u", tb_umax()); return; }
    EXPECT_EQ(B(15), 0);
    EXPECT_EQ(E(15), 0);
    EXPECT_EQ(E(15), TB_NOCANDO);
}

TEST(time_basic_period_umax_minus1_counted) {
    REQUIRE(U(timeBeginPeriod)); REQUIRE(U(timeEndPeriod));
    UINT u = tb_umax();
    EXPECT_EQ(B(u - 1), 0);
    EXPECT_EQ(E(u - 1), 0);
    EXPECT_EQ(E(u - 1), TB_NOCANDO);
}

TEST(time_basic_period_umax_uncounted) {
    REQUIRE(U(timeBeginPeriod)); REQUIRE(U(timeEndPeriod));
    UINT u = tb_umax();
    EXPECT_EQ(E(u), 0);          /* End without any Begin: still 0 */
    EXPECT_EQ(B(u), 0);
    EXPECT_EQ(E(u), 0);
    EXPECT_EQ(E(u), 0);
    EXPECT_EQ(E(u), 0);
}

TEST(time_basic_period_16_1000_ffffffff) {
    REQUIRE(U(timeBeginPeriod)); REQUIRE(U(timeEndPeriod));
    static const UINT ps[] = { 16, 1000, 0xFFFFFFFF };
    UINT u = tb_umax();
    for (size_t i = 0; i < 3; i++) {
        UINT p = ps[i];
        if (p < u) { t_note("period %u is below uMax=%u here, counted", p); EXPECT_EQ(B(p), 0); EXPECT_EQ(E(p), 0); EXPECT_EQ(E(p), TB_NOCANDO); continue; }
        CHECKF(B(p) == 0, "Begin(%u)", p);
        CHECKF(E(p) == 0, "End(%u)", p);
        CHECKF(E(p) == 0, "End(%u) again", p);
    }
}

TEST(time_basic_period_counted_range_table) {
    REQUIRE(U(timeBeginPeriod)); REQUIRE(U(timeEndPeriod));
    UINT pmin = tb_pmin(), u = tb_umax();
    for (UINT p = pmin; p < u; p++) {
        CHECKF(E(p) == TB_NOCANDO, "End(%u) before Begin", p);
        CHECKF(B(p) == 0, "Begin(%u)", p);
        CHECKF(E(p) == 0, "End(%u)", p);
        CHECKF(E(p) == TB_NOCANDO, "End(%u) unbalanced", p);
    }
}

TEST(time_basic_period_counted_range_depth3_table) {
    REQUIRE(U(timeBeginPeriod)); REQUIRE(U(timeEndPeriod));
    UINT pmin = tb_pmin(), u = tb_umax();
    for (UINT p = pmin; p < u; p++) {
        for (int k = 0; k < 3; k++) CHECKF(B(p) == 0, "Begin(%u)#%d", p, k);
        for (int k = 0; k < 3; k++) CHECKF(E(p) == 0, "End(%u)#%d", p, k);
        CHECKF(E(p) == TB_NOCANDO, "End(%u) #4", p);
    }
}

TEST(time_basic_period_uncounted_from_umax_to_100) {
    REQUIRE(U(timeBeginPeriod)); REQUIRE(U(timeEndPeriod));
    for (UINT p = tb_umax(); p <= 100; p++) {
        CHECKF(E(p) == 0, "End(%u) cold", p);
        CHECKF(B(p) == 0, "Begin(%u)", p);
        CHECKF(E(p) == 0, "End(%u)", p);
        CHECKF(E(p) == 0, "End(%u) again", p);
    }
}

TEST(time_basic_period_uncounted_large_table) {
    REQUIRE(U(timeBeginPeriod)); REQUIRE(U(timeEndPeriod));
    for (size_t i = 0; i < sizeof k_uncounted_abs / sizeof k_uncounted_abs[0]; i++) {
        UINT p = k_uncounted_abs[i];
        CHECKF(B(p) == 0, "Begin(%u)", p);
        CHECKF(B(p) == 0, "Begin(%u) #2", p);
        CHECKF(E(p) == 0, "End(%u)", p);
        CHECKF(E(p) == 0, "End(%u) #2", p);
        CHECKF(E(p) == 0, "End(%u) #3", p);
    }
}

TEST(time_basic_period_wperiodmax_uncounted) {
    REQUIRE(U(timeBeginPeriod)); REQUIRE(U(timeEndPeriod)); REQUIRE(U(timeGetDevCaps));
    TIMECAPS tc; EXPECT_EQ(U(timeGetDevCaps)(&tc, sizeof tc), 0);
    EXPECT_EQ(B(tc.wPeriodMax), 0);
    EXPECT_EQ(E(tc.wPeriodMax), 0);
    EXPECT_EQ(E(tc.wPeriodMax), 0);
    EXPECT_EQ(B(tc.wPeriodMax + 1), 0);
    EXPECT_EQ(E(tc.wPeriodMax + 1), 0);
}

TEST(time_basic_period_wperiodmin_counted) {
    REQUIRE(U(timeBeginPeriod)); REQUIRE(U(timeEndPeriod)); REQUIRE(U(timeGetDevCaps));
    TIMECAPS tc; EXPECT_EQ(U(timeGetDevCaps)(&tc, sizeof tc), 0);
    EXPECT_EQ(B(tc.wPeriodMin), 0);
    EXPECT_EQ(E(tc.wPeriodMin), 0);
    EXPECT_EQ(E(tc.wPeriodMin), TB_NOCANDO);
    if (tc.wPeriodMin > 0) EXPECT_EQ(B(tc.wPeriodMin - 1), TB_NOCANDO);
}

TEST(time_basic_period_counts_are_per_period) {
    REQUIRE(U(timeBeginPeriod)); REQUIRE(U(timeEndPeriod));
    if (tb_umax() <= 4) { t_skip("uMax"); return; }
    EXPECT_EQ(B(2), 0);
    EXPECT_EQ(E(3), TB_NOCANDO);   /* a different period has no count */
    EXPECT_EQ(E(4), TB_NOCANDO);
    EXPECT_EQ(E(2), 0);
    EXPECT_EQ(E(2), TB_NOCANDO);
}

TEST(time_basic_period_mismatched_end_does_not_consume) {
    REQUIRE(U(timeBeginPeriod)); REQUIRE(U(timeEndPeriod));
    if (tb_umax() <= 6) { t_skip("uMax"); return; }
    EXPECT_EQ(B(5), 0);
    EXPECT_EQ(E(6), TB_NOCANDO);
    EXPECT_EQ(E(4), TB_NOCANDO);
    EXPECT_EQ(E(0), TB_NOCANDO);
    EXPECT_EQ(E(1000), 0);
    EXPECT_EQ(E(5), 0);     /* the count for 5 is still there */
    EXPECT_EQ(E(5), TB_NOCANDO);
}

TEST(time_basic_period_interleaved_two_periods) {
    REQUIRE(U(timeBeginPeriod)); REQUIRE(U(timeEndPeriod));
    UINT a = tb_pmin(), b = tb_pmin() + 4;
    if (b >= tb_umax()) { t_skip("range"); return; }
    EXPECT_EQ(B(b), 0);
    EXPECT_EQ(B(a), 0);
    EXPECT_EQ(E(b), 0);     /* release the coarser one while the finer one is active */
    EXPECT_EQ(E(a), 0);
    EXPECT_EQ(E(a), TB_NOCANDO);
    EXPECT_EQ(E(b), TB_NOCANDO);
}

TEST(time_basic_period_fine_then_coarse_release_order) {
    REQUIRE(U(timeBeginPeriod)); REQUIRE(U(timeEndPeriod));
    UINT a = tb_pmin(), b = tb_umax() - 1;
    EXPECT_EQ(B(a), 0);
    EXPECT_EQ(B(b), 0);
    EXPECT_EQ(E(a), 0);     /* last release of the active period: falls back to b */
    EXPECT_EQ(E(b), 0);
    EXPECT_EQ(E(a), TB_NOCANDO);
    EXPECT_EQ(E(b), TB_NOCANDO);
}

TEST(time_basic_period_all_counted_at_once) {
    REQUIRE(U(timeBeginPeriod)); REQUIRE(U(timeEndPeriod));
    UINT pmin = tb_pmin(), u = tb_umax();
    for (UINT p = u - 1; p >= pmin && p < u; p--) CHECKF(B(p) == 0, "Begin(%u)", p);
    /* release finest first, each step falls back to the next counted period */
    for (UINT p = pmin; p < u; p++) CHECKF(E(p) == 0, "End(%u)", p);
    for (UINT p = pmin; p < u; p++) CHECKF(E(p) == TB_NOCANDO, "End(%u) after", p);
}

TEST(time_basic_period_all_counted_release_coarse_first) {
    REQUIRE(U(timeBeginPeriod)); REQUIRE(U(timeEndPeriod));
    UINT pmin = tb_pmin(), u = tb_umax();
    for (UINT p = pmin; p < u; p++) CHECKF(B(p) == 0, "Begin(%u)", p);
    for (UINT p = u - 1; p >= pmin && p < u; p--) CHECKF(E(p) == 0, "End(%u)", p);
    for (UINT p = pmin; p < u; p++) CHECKF(E(p) == TB_NOCANDO, "End(%u) after", p);
}

TEST(time_basic_period_deep_nesting_100) {
    REQUIRE(U(timeBeginPeriod)); REQUIRE(U(timeEndPeriod));
    UINT p = tb_pmin() + 3 < tb_umax() ? tb_pmin() + 3 : tb_pmin();
    int okb = 1, oke = 1;
    for (int i = 0; i < 100; i++) if (B(p) != 0) okb = 0;
    for (int i = 0; i < 100; i++) if (E(p) != 0) oke = 0;
    CHECK(okb); CHECK(oke);
    EXPECT_EQ(E(p), TB_NOCANDO);
}

TEST(time_basic_period_begin_end_cycles) {
    REQUIRE(U(timeBeginPeriod)); REQUIRE(U(timeEndPeriod));
    UINT p = tb_pmin();
    int bad = 0;
    for (int i = 0; i < 200; i++) { if (B(p) != 0) bad++; if (E(p) != 0) bad++; }
    EXPECT_EQ(bad, 0);
    EXPECT_EQ(E(p), TB_NOCANDO);
}

TEST(time_basic_period_uncounted_does_not_affect_counted) {
    REQUIRE(U(timeBeginPeriod)); REQUIRE(U(timeEndPeriod));
    UINT p = tb_umax() - 1, u = tb_umax();
    EXPECT_EQ(B(p), 0);
    EXPECT_EQ(B(u), 0);
    EXPECT_EQ(E(u), 0);
    EXPECT_EQ(E(u), 0);
    EXPECT_EQ(E(p), 0);
    EXPECT_EQ(E(p), TB_NOCANDO);
}

TEST(time_basic_period_preserves_last_error) {
    REQUIRE(U(timeBeginPeriod)); REQUIRE(U(timeEndPeriod));
    SET_ERR(0x4242); EXPECT_EQ(B(tb_pmin()), 0); EXPECT_ERR(0x4242);
    SET_ERR(0x4242); EXPECT_EQ(E(tb_pmin()), 0); EXPECT_ERR(0x4242);
    SET_ERR(0x4242); EXPECT_EQ(E(tb_pmin()), TB_NOCANDO); EXPECT_ERR(0x4242);
    SET_ERR(0x4242); EXPECT_EQ(B(0), TB_NOCANDO); EXPECT_ERR(0x4242);
    SET_ERR(0x4242); EXPECT_EQ(B(1000), 0); EXPECT_ERR(0x4242);
    SET_ERR(0x4242); EXPECT_EQ(E(1000), 0); EXPECT_ERR(0x4242);
}

/* ---- shared state between the winmm thunk and kernel32 ----------------- */
TEST(time_basic_period_winmm_begin_kernel32_end) {
    REQUIRE(UIN("winmm.dll", timeBeginPeriod)); REQUIRE(UIN("kernel32.dll", timeEndPeriod));
    UINT p = tb_pmin();
    EXPECT_EQ(UIN("winmm.dll", timeBeginPeriod)(p), 0);
    EXPECT_EQ(UIN("kernel32.dll", timeEndPeriod)(p), 0);
    EXPECT_EQ(UIN("winmm.dll", timeEndPeriod)(p), TB_NOCANDO);
}

TEST(time_basic_period_kernel32_begin_winmm_end) {
    REQUIRE(UIN("kernel32.dll", timeBeginPeriod)); REQUIRE(UIN("winmm.dll", timeEndPeriod));
    UINT p = tb_umax() - 1;
    EXPECT_EQ(UIN("kernel32.dll", timeBeginPeriod)(p), 0);
    EXPECT_EQ(UIN("kernel32.dll", timeBeginPeriod)(p), 0);
    EXPECT_EQ(UIN("winmm.dll", timeEndPeriod)(p), 0);
    EXPECT_EQ(UIN("kernel32.dll", timeEndPeriod)(p), 0);
    EXPECT_EQ(UIN("kernel32.dll", timeEndPeriod)(p), TB_NOCANDO);
    EXPECT_EQ(UIN("winmm.dll", timeEndPeriod)(p), TB_NOCANDO);
}

TEST(time_basic_period_kernel32_gates) {
    REQUIRE(UIN("kernel32.dll", timeBeginPeriod)); REQUIRE(UIN("kernel32.dll", timeEndPeriod));
    EXPECT_EQ(UIN("kernel32.dll", timeBeginPeriod)(0), TB_NOCANDO);
    EXPECT_EQ(UIN("kernel32.dll", timeEndPeriod)(0), TB_NOCANDO);
    EXPECT_EQ(UIN("kernel32.dll", timeBeginPeriod)(1000), 0);
    EXPECT_EQ(UIN("kernel32.dll", timeEndPeriod)(1000), 0);
    EXPECT_EQ(UIN("kernel32.dll", timeEndPeriod)(1000), 0);
}

/* ---- observable resolution --------------------------------------------- */
TEST(time_basic_period_resolution_not_coarser_after_begin_min) {
    REQUIRE(U(timeBeginPeriod)); REQUIRE(U(timeEndPeriod));
    ULONG before = 0, during = 0, after = 0;
    if (!tb_res(NULL, NULL, &before)) { t_skip("NtQueryTimerResolution"); return; }
    UINT p = tb_pmin();
    EXPECT_EQ(B(p), 0);
    tb_res(NULL, NULL, &during);
    EXPECT_EQ(E(p), 0);
    tb_res(NULL, NULL, &after);
    t_note("current resolution before=%lu during Begin(%u)=%lu after End=%lu (100ns)", before, p, during, after);
    CHECKF(during <= before, "during=%lu before=%lu", during, before);
    if (during > (ULONG)p * 10000) t_note("during=%lu is coarser than the %u ms request", during, p);
}

TEST(time_basic_period_resolution_each_counted_period_note) {
    REQUIRE(U(timeBeginPeriod)); REQUIRE(U(timeEndPeriod));
    UINT pmin = tb_pmin(), u = tb_umax();
    char line[512]; int n = 0; line[0] = 0;
    for (UINT p = pmin; p < u; p++) {
        ULONG before = tb_cur_res();
        CHECKF(B(p) == 0, "Begin(%u)", p);
        ULONG during = tb_cur_res();
        CHECKF(E(p) == 0, "End(%u)", p);
        CHECKF(during <= before, "p=%u during=%lu before=%lu", p, during, before);
        n += snprintf(line + n, sizeof line - n, " %u:%lu", p, during);
        if (n >= (int)sizeof line - 32) break;
    }
    t_note("resolution while each period is held:%s", line);
}

TEST(time_basic_period_uncounted_does_not_change_resolution_note) {
    REQUIRE(U(timeBeginPeriod)); REQUIRE(U(timeEndPeriod));
    ULONG a = tb_cur_res();
    EXPECT_EQ(B(1000), 0);
    ULONG b = tb_cur_res();
    EXPECT_EQ(E(1000), 0);
    t_note("Begin(1000): current resolution %lu -> %lu (other processes may change it)", a, b);
}

TEST(time_basic_period_sleep1_with_period_note) {
    REQUIRE(U(timeBeginPeriod)); REQUIRE(U(timeEndPeriod)); REQUIRE(U(timeGetTime));
    DWORD t0 = U(timeGetTime)();
    for (int i = 0; i < 10; i++) Sleep(1);
    LONG dflt = tb_diff(U(timeGetTime)(), t0);
    UINT p = tb_pmin();
    EXPECT_EQ(B(p), 0);
    t0 = U(timeGetTime)();
    for (int i = 0; i < 10; i++) Sleep(1);
    LONG fine = tb_diff(U(timeGetTime)(), t0);
    EXPECT_EQ(E(p), 0);
    t_note("10 x Sleep(1): %ld ms by default, %ld ms with timeBeginPeriod(%u)", dflt, fine, p);
    CHECK(fine >= 0 && dflt >= 0);
}

/* ---- threads ------------------------------------------------------------ */
typedef struct { UINT period; int iters; int bad; } pt_arg;
static DWORD WINAPI pt_pairs(LPVOID a) {
    pt_arg *t = (pt_arg *)a;
    for (int i = 0; i < t->iters; i++) {
        if (B(t->period) != 0) t->bad++;
        if (E(t->period) != 0) t->bad++;
    }
    return 0;
}
TEST(time_basic_period_threads_same_period) {
    REQUIRE(U(timeBeginPeriod)); REQUIRE(U(timeEndPeriod));
    static pt_arg a[4]; HANDLE h[4];
    UINT p = tb_umax() - 2 >= tb_pmin() ? tb_umax() - 2 : tb_pmin();
    /* hold one count so every thread's Begin/End is a plain increment/decrement */
    EXPECT_EQ(B(p), 0);
    for (int i = 0; i < 4; i++) { a[i].period = p; a[i].iters = 2000; a[i].bad = 0; h[i] = CreateThread(NULL, 0, pt_pairs, &a[i], 0, NULL); CHECK(h[i] != NULL); }
    EXPECT_EQ(WaitForMultipleObjects(4, h, TRUE, 20000), WAIT_OBJECT_0);
    for (int i = 0; i < 4; i++) { CHECKF(a[i].bad == 0, "thread %d bad=%d", i, a[i].bad); CloseHandle(h[i]); }
    EXPECT_EQ(E(p), 0);
    EXPECT_EQ(E(p), TB_NOCANDO);
}

TEST(time_basic_period_threads_zero_to_one_transitions) {
    REQUIRE(U(timeBeginPeriod)); REQUIRE(U(timeEndPeriod));
    static pt_arg a[4]; HANDLE h[4];
    UINT p = tb_umax() - 1;
    for (int i = 0; i < 4; i++) { a[i].period = p; a[i].iters = 500; a[i].bad = 0; h[i] = CreateThread(NULL, 0, pt_pairs, &a[i], 0, NULL); CHECK(h[i] != NULL); }
    EXPECT_EQ(WaitForMultipleObjects(4, h, TRUE, 20000), WAIT_OBJECT_0);
    for (int i = 0; i < 4; i++) { CHECKF(a[i].bad == 0, "thread %d bad=%d", i, a[i].bad); CloseHandle(h[i]); }
    EXPECT_EQ(E(p), TB_NOCANDO);
}

TEST(time_basic_period_threads_distinct_periods) {
    REQUIRE(U(timeBeginPeriod)); REQUIRE(U(timeEndPeriod));
    static pt_arg a[4]; HANDLE h[4]; int n = 0;
    UINT pmin = tb_pmin(), u = tb_umax();
    for (int i = 0; i < 4; i++) {
        UINT p = pmin + 1 + (UINT)i * 2;
        if (p >= u) break;
        a[n].period = p; a[n].iters = 1000; a[n].bad = 0;
        h[n] = CreateThread(NULL, 0, pt_pairs, &a[n], 0, NULL); CHECK(h[n] != NULL); n++;
    }
    if (!n) { t_skip("range"); return; }
    EXPECT_EQ(WaitForMultipleObjects(n, h, TRUE, 20000), WAIT_OBJECT_0);
    for (int i = 0; i < n; i++) {
        CHECKF(a[i].bad == 0, "period %u bad=%d", a[i].period, a[i].bad);
        CHECKF(E(a[i].period) == TB_NOCANDO, "period %u left counted", a[i].period);
        CloseHandle(h[i]);
    }
}

static DWORD WINAPI pt_begin_only(LPVOID a) { pt_arg *t = (pt_arg *)a; for (int i = 0; i < t->iters; i++) if (B(t->period) != 0) t->bad++; return 0; }
static DWORD WINAPI pt_end_only(LPVOID a) { pt_arg *t = (pt_arg *)a; for (int i = 0; i < t->iters; i++) if (E(t->period) != 0) t->bad++; return 0; }
TEST(time_basic_period_counts_are_process_wide) {
    REQUIRE(U(timeBeginPeriod)); REQUIRE(U(timeEndPeriod));
    pt_arg b = { tb_pmin(), 10, 0 }, e = { tb_pmin(), 10, 0 };
    HANDLE h = CreateThread(NULL, 0, pt_begin_only, &b, 0, NULL);
    CHECK(h != NULL); if (!h) return;
    EXPECT_EQ(WaitForSingleObject(h, 5000), WAIT_OBJECT_0); CloseHandle(h);
    h = CreateThread(NULL, 0, pt_end_only, &e, 0, NULL);
    CHECK(h != NULL); if (!h) { tb_drain(tb_pmin()); return; }
    EXPECT_EQ(WaitForSingleObject(h, 5000), WAIT_OBJECT_0); CloseHandle(h);
    EXPECT_EQ(b.bad, 0);
    EXPECT_EQ(e.bad, 0);
    EXPECT_EQ(E(tb_pmin()), TB_NOCANDO);
}

TEST(time_basic_period_begin_in_thread_survives_thread_exit) {
    REQUIRE(U(timeBeginPeriod)); REQUIRE(U(timeEndPeriod));
    pt_arg b = { tb_umax() - 1, 3, 0 };
    HANDLE h = CreateThread(NULL, 0, pt_begin_only, &b, 0, NULL);
    CHECK(h != NULL); if (!h) return;
    EXPECT_EQ(WaitForSingleObject(h, 5000), WAIT_OBJECT_0); CloseHandle(h);
    EXPECT_EQ(b.bad, 0);
    EXPECT_EQ(E(b.period), 0);
    EXPECT_EQ(E(b.period), 0);
    EXPECT_EQ(E(b.period), 0);
    EXPECT_EQ(E(b.period), TB_NOCANDO);
}

/* ---- child processes: counts are per process ----------------------------- */
CHILD(tb_period_fresh_ends) {
    /* nothing counted yet in this process */
    UINT pmin = tb_pmin(), u = tb_umax();
    if (U(timeEndPeriod)(0) != 97) return 10;
    for (UINT p = pmin; p < u; p++) if (U(timeEndPeriod)(p) != 97) return 20 + (int)p;
    if (U(timeEndPeriod)(u) != 0) return 90;
    if (U(timeEndPeriod)(1000) != 0) return 91;
    if (U(timeEndPeriod)(0xFFFFFFFF) != 0) return 92;
    return 0;
}
TEST(time_basic_period_fresh_process_all_ends_fail) {
    REQUIRE(U(timeEndPeriod));
    EXPECT_EQ(t_run_child("tb_period_fresh_ends", NULL, 20000), 0);
}

CHILD(tb_period_leak) {
    /* Begin without End and exit: the count dies with the process */
    if (U(timeBeginPeriod)(tb_pmin()) != 0) return 10;
    if (U(timeBeginPeriod)(tb_umax() - 1) != 0) return 11;
    return 0;
}
TEST(time_basic_period_child_leak_not_inherited) {
    REQUIRE(U(timeBeginPeriod)); REQUIRE(U(timeEndPeriod));
    EXPECT_EQ(t_run_child("tb_period_leak", NULL, 20000), 0);
    EXPECT_EQ(E(tb_pmin()), TB_NOCANDO);
    EXPECT_EQ(E(tb_umax() - 1), TB_NOCANDO);
}

CHILD(tb_period_parent_held) {
    /* the parent holds a count; this process starts from zero */
    return U(timeEndPeriod)(tb_pmin()) == 97 ? 0 : 1;
}
TEST(time_basic_period_parent_count_not_visible_in_child) {
    REQUIRE(U(timeBeginPeriod)); REQUIRE(U(timeEndPeriod));
    EXPECT_EQ(B(tb_pmin()), 0);
    EXPECT_EQ(t_run_child("tb_period_parent_held", NULL, 20000), 0);
    EXPECT_EQ(E(tb_pmin()), 0);
    EXPECT_EQ(E(tb_pmin()), TB_NOCANDO);
}

/* 16-bit counter saturation: 0xFFFF successful Begins, the next one -> 97. Runs in a child so a
 * failure cannot leave counts behind in this process. */
CHILD(tb_period_saturate) {
    UINT p = tb_umax() - 1;
    for (int i = 0; i < 0xFFFF; i++) if (U(timeBeginPeriod)(p) != 0) return 10;
    if (U(timeBeginPeriod)(p) != 97) return 11;
    if (U(timeBeginPeriod)(p) != 97) return 12;
    /* other periods are independent */
    if (U(timeBeginPeriod)(tb_pmin()) != 0) return 13;
    if (U(timeEndPeriod)(tb_pmin()) != 0) return 14;
    /* the refused Begins did not count: exactly 0xFFFF Ends succeed */
    for (int i = 0; i < 0xFFFF; i++) if (U(timeEndPeriod)(p) != 0) return 15;
    if (U(timeEndPeriod)(p) != 97) return 16;
    if (U(timeBeginPeriod)(p) != 0) return 17;
    if (U(timeEndPeriod)(p) != 0) return 18;
    return 0;
}
TEST(time_basic_period_count_saturates_at_ffff) {
    REQUIRE(U(timeBeginPeriod)); REQUIRE(U(timeEndPeriod));
    EXPECT_EQ(t_run_child("tb_period_saturate", NULL, 60000), 0);
}

CHILD(tb_period_saturate_min) {
    UINT p = tb_pmin();
    for (int i = 0; i < 0xFFFF; i++) if (U(timeBeginPeriod)(p) != 0) return 10;
    if (U(timeBeginPeriod)(p) != 97) return 11;
    for (int i = 0; i < 0xFFFF; i++) if (U(timeEndPeriod)(p) != 0) return 12;
    if (U(timeEndPeriod)(p) != 97) return 13;
    return 0;
}
TEST(time_basic_period_count_saturates_at_ffff_min_period) {
    REQUIRE(U(timeBeginPeriod)); REQUIRE(U(timeEndPeriod));
    EXPECT_EQ(t_run_child("tb_period_saturate_min", NULL, 60000), 0);
}

CHILD(tb_period_uncounted_unbounded) {
    /* periods >= uMax have no counter, so there is nothing to saturate */
    UINT u = tb_umax();
    for (int i = 0; i < 70000; i++) if (U(timeBeginPeriod)(u) != 0) return 10;
    for (int i = 0; i < 70001; i++) if (U(timeEndPeriod)(u) != 0) return 11;
    return 0;
}
TEST(time_basic_period_uncounted_never_saturates) {
    REQUIRE(U(timeBeginPeriod)); REQUIRE(U(timeEndPeriod));
    EXPECT_EQ(t_run_child("tb_period_uncounted_unbounded", NULL, 60000), 0);
}

CHILD(tb_period_first_call_begin) {
    /* timeBeginPeriod as the first time API in the process (runs TimeInit itself) */
    if (U(timeBeginPeriod)(0) != 97) return 10;
    if (U(timeBeginPeriod)(1) != (tb_pmin() <= 1 ? 0u : 97u)) return 11;
    if (tb_pmin() <= 1 && U(timeEndPeriod)(1) != 0) return 12;
    if (U(timeEndPeriod)(1) != 97) return 13;
    return 0;
}
TEST(time_basic_period_first_call_in_fresh_process) {
    REQUIRE(U(timeBeginPeriod));
    EXPECT_EQ(t_run_child("tb_period_first_call_begin", NULL, 20000), 0);
}

/* ---- consistency with timeGetDevCaps ------------------------------------ */
TEST(time_basic_period_devcaps_min_is_first_counted) {
    REQUIRE(U(timeBeginPeriod)); REQUIRE(U(timeEndPeriod)); REQUIRE(U(timeGetDevCaps));
    TIMECAPS tc; EXPECT_EQ(U(timeGetDevCaps)(&tc, sizeof tc), 0);
    for (UINT p = 0; p < tc.wPeriodMin; p++) CHECKF(B(p) == TB_NOCANDO, "Begin(%u)", p);
    EXPECT_EQ(B(tc.wPeriodMin), 0);
    EXPECT_EQ(E(tc.wPeriodMin), 0);
    EXPECT_EQ(E(tc.wPeriodMin), TB_NOCANDO);
}

TEST(time_basic_period_every_value_0_to_64) {
    REQUIRE(U(timeBeginPeriod)); REQUIRE(U(timeEndPeriod));
    UINT pmin = tb_pmin(), u = tb_umax();
    for (UINT p = 0; p <= 64; p++) {
        MMRESULT rb = B(p), re = E(p), re2 = E(p);
        if (p < pmin)      { CHECKF(rb == 97 && re == 97 && re2 == 97, "p=%u %u/%u/%u", p, rb, re, re2); }
        else if (p < u)    { CHECKF(rb == 0 && re == 0 && re2 == 97, "p=%u %u/%u/%u", p, rb, re, re2); }
        else               { CHECKF(rb == 0 && re == 0 && re2 == 0, "p=%u %u/%u/%u", p, rb, re, re2); }
    }
}

TEST(time_basic_period_balanced_at_end_of_area) {
    /* all tests above balanced their Begins: nothing is left counted in this process */
    REQUIRE(U(timeEndPeriod));
    UINT pmin = tb_pmin(), u = tb_umax();
    for (UINT p = pmin; p < u; p++) CHECKF(E(p) == TB_NOCANDO, "period %u still counted", p);
}
