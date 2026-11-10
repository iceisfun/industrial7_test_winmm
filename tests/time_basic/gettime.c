/* time_basic: timeGetTime.
 * kernel32!timeGetTime = (int)((InterruptTime - base) / 10000) + tickBase, where base and
 * tickBase were captured together (InterruptTime, GetTickCount) in TimeInit. So it is a
 * millisecond view of KUSER_SHARED_DATA.InterruptTime, started in step with GetTickCount,
 * monotonic, and its granularity follows the current timer resolution.
 */
#include "tb.h"

TEST(time_basic_gettime_nonzero_and_tick_aligned) {
    REQUIRE(U(timeGetTime));
    DWORD g0 = GetTickCount();
    DWORD t = U(timeGetTime)();
    DWORD g1 = GetTickCount();
    LONG d0 = tb_diff(t, g0), d1 = tb_diff(t, g1);
    t_note("timeGetTime=%lu GetTickCount=[%lu,%lu] delta=%ld", t, g0, g1, d0);
    CHECKF(d0 > -1000 && d0 < 1000 && d1 > -1000 && d1 < 1000, "delta=%ld", d0);
}

TEST(time_basic_gettime_monotonic_1000) {
    REQUIRE(U(timeGetTime));
    DWORD prev = U(timeGetTime)(); int dec = 0;
    for (int i = 0; i < 1000; i++) {
        DWORD t = U(timeGetTime)();
        if (tb_diff(t, prev) < 0) dec++;
        prev = t;
    }
    EXPECT_EQ(dec, 0);
}

TEST(time_basic_gettime_monotonic_200k) {
    REQUIRE(U(timeGetTime));
    DWORD (WINAPI *f)(void) = U(timeGetTime);
    DWORD first = f(), prev = first; int dec = 0; LONG maxstep = 0;
    for (int i = 0; i < 200000; i++) {
        DWORD t = f();
        LONG d = tb_diff(t, prev);
        if (d < 0) dec++;
        if (d > maxstep) maxstep = d;
        prev = t;
    }
    EXPECT_EQ(dec, 0);
    t_note("200k calls spanned %ld ms, largest step %ld ms", tb_diff(prev, first), maxstep);
}

TEST(time_basic_gettime_advances_sleep50) {
    REQUIRE(U(timeGetTime));
    DWORD a = U(timeGetTime)();
    Sleep(50);
    DWORD b = U(timeGetTime)();
    LONG d = tb_diff(b, a);
    CHECKF(d >= 30 && d < 2000, "delta=%ld", d);
    t_note("Sleep(50) -> %ld ms", d);
}

TEST(time_basic_gettime_advances_sleep_table) {
    REQUIRE(U(timeGetTime));
    static const DWORD ms[] = { 20, 100, 250 };
    for (size_t i = 0; i < sizeof ms / sizeof ms[0]; i++) {
        DWORD a = U(timeGetTime)();
        Sleep(ms[i]);
        DWORD b = U(timeGetTime)();
        LONG d = tb_diff(b, a);
        /* Sleep never returns early by more than one clock tick; the upper bound is very loose */
        CHECKF(d >= (LONG)ms[i] - 16 && d < (LONG)ms[i] + 2000, "Sleep(%lu) delta=%ld", ms[i], d);
    }
}

TEST(time_basic_gettime_in_step_with_tickcount) {
    REQUIRE(U(timeGetTime));
    LONG lo = 0x7FFFFFFF, hi = -0x7FFFFFFF;
    for (int i = 0; i < 20; i++) {
        DWORD g = GetTickCount();
        DWORD t = U(timeGetTime)();
        LONG d = tb_diff(t, g);
        if (d < lo) lo = d;
        if (d > hi) hi = d;
        Sleep(5);
    }
    t_note("timeGetTime - GetTickCount in [%ld, %ld] ms", lo, hi);
    CHECKF(lo > -1000 && hi < 1000, "[%ld,%ld]", lo, hi);
}

TEST(time_basic_gettime_rate_matches_tickcount) {
    REQUIRE(U(timeGetTime));
    DWORD t0 = U(timeGetTime)(), g0 = GetTickCount();
    Sleep(300);
    DWORD t1 = U(timeGetTime)(), g1 = GetTickCount();
    LONG dt = tb_diff(t1, t0), dg = tb_diff(g1, g0);
    t_note("over Sleep(300): timeGetTime +%ld, GetTickCount +%ld", dt, dg);
    CHECKF(dt - dg > -100 && dt - dg < 100, "dt=%ld dg=%ld", dt, dg);
}

/* timeGetTime is floor((I - base)/10000) + C for the interrupt time I read inside the call.
 * Bracket each call with reads of I: over any number of samples,
 *   max(t - floor(I_after/10000)) - min(t - floor(I_before/10000)) <= 1. */
TEST(time_basic_gettime_is_interrupt_time_ms) {
    REQUIRE(U(timeGetTime));
    LONGLONG kmax = -0x7FFFFFFFFFFFLL, kmin = 0x7FFFFFFFFFFFLL;
    for (int i = 0; i < 5000; i++) {
        ULONGLONG ib = tb_itime();
        DWORD t = U(timeGetTime)();
        ULONGLONG ia = tb_itime();
        /* compare in 32-bit wrapping space */
        LONG hiv = (LONG)(t - (DWORD)(ia / 10000));
        LONG lov = (LONG)(t - (DWORD)(ib / 10000));
        if (hiv > kmax) kmax = hiv;
        if (lov < kmin) kmin = lov;
        if ((i % 1000) == 999) Sleep(7);
    }
    CHECKF(kmax - kmin <= 1, "offset spread kmin=%lld kmax=%lld", kmin, kmax);
}

TEST(time_basic_gettime_interrupt_delta_over_sleep) {
    REQUIRE(U(timeGetTime));
    ULONGLONG ib0 = tb_itime(); DWORD t0 = U(timeGetTime)(); ULONGLONG ia0 = tb_itime();
    Sleep(120);
    ULONGLONG ib1 = tb_itime(); DWORD t1 = U(timeGetTime)(); ULONGLONG ia1 = tb_itime();
    LONG dt = tb_diff(t1, t0);
    LONGLONG lo = (LONGLONG)(ib1 / 10000) - (LONGLONG)(ia0 / 10000) - 1;
    LONGLONG hi = (LONGLONG)(ia1 / 10000) - (LONGLONG)(ib0 / 10000) + 1;
    CHECKF(dt >= lo && dt <= hi, "dt=%ld expected [%lld,%lld]", dt, lo, hi);
}

TEST(time_basic_gettime_independent_of_system_clock) {
    REQUIRE(U(timeGetTime));
    /* query only: the wall clock is read, never set */
    FILETIME f0, f1;
    GetSystemTimeAsFileTime(&f0);
    DWORD t0 = U(timeGetTime)();
    Sleep(30);
    GetSystemTimeAsFileTime(&f1);
    DWORD t1 = U(timeGetTime)();
    ULONGLONG w = (((ULONGLONG)f1.dwHighDateTime << 32 | f1.dwLowDateTime) - ((ULONGLONG)f0.dwHighDateTime << 32 | f0.dwLowDateTime)) / 10000;
    t_note("wall +%llu ms, timeGetTime +%ld ms", w, tb_diff(t1, t0));
    CHECK(tb_diff(t1, t0) >= 14);
}

TEST(time_basic_gettime_granularity_note) {
    REQUIRE(U(timeGetTime));
    DWORD prev = U(timeGetTime)(), start = prev; LONG minstep = 0x7FFFFFFF, maxstep = 0; int steps = 0;
    while (tb_diff(U(timeGetTime)(), start) < 100) {
        DWORD t = U(timeGetTime)();
        LONG d = tb_diff(t, prev);
        if (d > 0) { steps++; if (d < minstep) minstep = d; if (d > maxstep) maxstep = d; }
        prev = t;
    }
    t_note("default resolution: %d steps in 100 ms, step min=%ld max=%ld, NtQueryTimerResolution cur=%lu",
           steps, minstep, maxstep, tb_cur_res());
    CHECK(steps >= 1);
}

TEST(time_basic_gettime_granularity_with_period1) {
    REQUIRE(U(timeGetTime)); REQUIRE(U(timeBeginPeriod)); REQUIRE(U(timeEndPeriod));
    UINT pmin = tb_pmin();
    EXPECT_EQ(tb_begin(pmin), 0);
    Sleep(20);
    DWORD prev = U(timeGetTime)(), start = prev; LONG maxstep = 0; int steps = 0, dec = 0;
    while (tb_diff(U(timeGetTime)(), start) < 100) {
        DWORD t = U(timeGetTime)();
        LONG d = tb_diff(t, prev);
        if (d < 0) dec++;
        if (d > 0) { steps++; if (d > maxstep) maxstep = d; }
        prev = t;
    }
    EXPECT_EQ(tb_end(pmin), 0);
    EXPECT_EQ(dec, 0);
    t_note("after timeBeginPeriod(%u): %d steps in 100 ms, largest step %ld, cur res %lu", pmin, steps, maxstep, tb_cur_res());
    CHECK(steps >= 1);
}

TEST(time_basic_gettime_winmm_vs_kernel32) {
    REQUIRE(U(timeGetTime));
    REQUIRE(UIN("kernel32.dll", timeGetTime));
    DWORD (WINAPI *w)(void) = UIN("winmm.dll", timeGetTime);
    DWORD (WINAPI *k)(void) = UIN("kernel32.dll", timeGetTime);
    int ok = 1;
    for (int i = 0; i < 2000; i++) {
        DWORD a = w(), b = k(), c = w();
        if (tb_diff(b, a) < 0 || tb_diff(c, b) < 0) ok = 0;
    }
    CHECK(ok);
}

TEST(time_basic_gettime_preserves_last_error) {
    REQUIRE(U(timeGetTime));
    SET_ERR(0xC0FFEE);
    (void)U(timeGetTime)();
    EXPECT_ERR(0xC0FFEE);
}

/* ---- threads ---------------------------------------------------------- */
typedef struct { volatile LONG *go; int dec; DWORD first, last; LONG maxstep; } gt_thr;
static DWORD WINAPI gt_thread(LPVOID p) {
    gt_thr *t = (gt_thr *)p;
    DWORD (WINAPI *f)(void) = U(timeGetTime);
    while (!*t->go) YieldProcessor();
    DWORD prev = f(); t->first = prev;
    for (int i = 0; i < 100000; i++) {
        DWORD v = f();
        LONG d = tb_diff(v, prev);
        if (d < 0) t->dec++;
        if (d > t->maxstep) t->maxstep = d;
        prev = v;
    }
    t->last = prev;
    return 0;
}
static void gt_run(int n) {
    static gt_thr th[8]; HANDLE h[8]; volatile LONG go = 0;
    for (int i = 0; i < n; i++) { memset(&th[i], 0, sizeof th[i]); th[i].go = &go; h[i] = CreateThread(NULL, 0, gt_thread, &th[i], 0, NULL); CHECK(h[i] != NULL); }
    InterlockedExchange(&go, 1);
    EXPECT_EQ(WaitForMultipleObjects(n, h, TRUE, 20000), WAIT_OBJECT_0);
    for (int i = 0; i < n; i++) {
        CHECKF(th[i].dec == 0, "thread %d saw %d decreases", i, th[i].dec);
        CHECKF(tb_diff(th[i].last, th[i].first) >= 0, "thread %d", i);
        CloseHandle(h[i]);
    }
}
TEST(time_basic_gettime_threads4_monotonic) { REQUIRE(U(timeGetTime)); gt_run(4); }
TEST(time_basic_gettime_threads8_monotonic) { REQUIRE(U(timeGetTime)); gt_run(8); }

/* Cross-thread ordering: a value published by one thread is never ahead of what another
 * thread reads afterwards (both read the same shared counter). */
typedef struct { volatile LONG stop; volatile DWORD pub; volatile LONG seq; } gt_x;
static DWORD WINAPI gt_pub(LPVOID p) {
    gt_x *x = (gt_x *)p;
    while (!x->stop) { x->pub = U(timeGetTime)(); InterlockedIncrement(&x->seq); }
    return 0;
}
TEST(time_basic_gettime_cross_thread_order) {
    REQUIRE(U(timeGetTime));
    static gt_x x; memset((void *)&x, 0, sizeof x);
    HANDLE h = CreateThread(NULL, 0, gt_pub, &x, 0, NULL);
    CHECK(h != NULL); if (!h) return;
    while (x.seq == 0) Sleep(0);
    int bad = 0;
    for (int i = 0; i < 50000; i++) {
        LONG s = x.seq; MemoryBarrier();
        DWORD seen = x.pub; MemoryBarrier();
        DWORD mine = U(timeGetTime)();
        if (s && tb_diff(mine, seen) < 0) bad++;
    }
    InterlockedExchange(&x.stop, 1);
    EXPECT_EQ(WaitForSingleObject(h, 5000), WAIT_OBJECT_0);
    CloseHandle(h);
    EXPECT_EQ(bad, 0);
}

/* ---- fresh process ------------------------------------------------------ */
CHILD(tb_gettime_first) {
    DWORD g0 = GetTickCount();
    DWORD t = U(timeGetTime)();
    DWORD g1 = GetTickCount();
    if (tb_diff(t, g0) < -100 || tb_diff(t, g1) > 100) return 10;
    DWORD prev = t;
    for (int i = 0; i < 10000; i++) { DWORD v = U(timeGetTime)(); if (tb_diff(v, prev) < 0) return 11; prev = v; }
    return 0;
}
TEST(time_basic_gettime_first_call_in_fresh_process) {
    REQUIRE(U(timeGetTime));
    EXPECT_EQ(t_run_child("tb_gettime_first", NULL, 20000), 0);
}

/* Two processes read the same system-wide interrupt time, so a child started after the
 * parent read timeGetTime reports a value that is not behind the parent's (both bases come
 * from GetTickCount at init; allow the GetTickCount alignment slack). */
CHILD(tb_gettime_print) { return (int)(U(timeGetTime)() & 0x7FFFFFFF); }
TEST(time_basic_gettime_cross_process_order) {
    REQUIRE(U(timeGetTime));
    DWORD before = U(timeGetTime)();
    DWORD c = t_run_child("tb_gettime_print", NULL, 20000);
    DWORD after = U(timeGetTime)();
    if (c >= 0xDEAD0000) { CHECKF(0, "child failed 0x%lx", c); return; }
    LONG d0 = (LONG)((c - (before & 0x7FFFFFFF)) << 1) >> 1, d1 = (LONG)(((after & 0x7FFFFFFF) - c) << 1) >> 1;
    t_note("child timeGetTime - parent before = %ld, parent after - child = %ld", d0, d1);
    CHECKF(d0 > -100 && d1 > -100, "d0=%ld d1=%ld", d0, d1);
}
