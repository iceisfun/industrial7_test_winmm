/* time_events: the timer period an event holds.
 * Decomp (winmm!timeSetEvent 0x2a20): period = TimerData if uResolution > TimerData, else max(uResolution,
 * wPeriodMin); if uDelay < period then period = TimerData. timeBeginPeriod(period) is taken before the slot is
 * allocated (failure -> 0), timeEndPeriod(period) on a full table, on timeKillEvent and when a one-shot finishes
 * (timeThread 0x26c0). kernel32's timeBeginPeriod/timeEndPeriod keep a per-period count for wPeriodMin <= p < uMax
 * (uMax == TimerData, normally 16) and are no-ops above, so the reference is observable: while the event lives
 * timeEndPeriod(period) returns 0 (we immediately give it back with timeBeginPeriod), afterwards it returns 97.
 * Every probe here is balanced; no test leaves a period counted.
 */
#include "te.h"

#define TE_PREQ() do { TE_REQUIRE(); REQUIRE(U(timeBeginPeriod)); REQUIRE(U(timeEndPeriod)); REQUIRE(U(timeGetDevCaps)); \
    if (te_umax() <= te_pmin()) { t_skip("no counted period window (wPeriodMin %u, uMax %u)", te_pmin(), te_umax()); return; } } while (0)

static void te_period_case(UINT delay, UINT res, UINT fl) {
    UINT umax = te_umax(), pmin = te_pmin();
    UINT want = te_expect_period(delay, res);
    unsigned long long before = te_held_mask();
    EXPECT_EQ(before, 0);
    UINT id = te_set(delay, res, NULL, 0, fl);
    CHECKF(id != 0, "delay %u res %u", delay, res);
    if (!id) return;
    unsigned long long m = te_held_mask();
    unsigned long long expect = (want < umax) ? 1ull << (want - pmin) : 0;
    CHECKF(m == expect, "delay %u res %u: held mask 0x%llx want 0x%llx (period %u)", delay, res, m, expect, want);
    EXPECT_EQ(te_kill(id), 0);
    EXPECT_EQ(te_held_mask(), 0);
}

TEST(te_period_umax_matches_coarsest_resolution) {
    TE_PREQ();
    UINT umax = te_umax();
    /* timeBeginPeriod/timeEndPeriod at uMax and above are counted-free no-ops */
    EXPECT_EQ(U(timeEndPeriod)(umax), 0);
    EXPECT_EQ(U(timeEndPeriod)(umax + 1), 0);
    EXPECT_EQ(U(timeEndPeriod)(1000), 0);
    /* below uMax an unbalanced End is 97 */
    EXPECT_EQ(U(timeEndPeriod)(umax - 1), TE_NOCANDO);
    EXPECT_EQ(U(timeEndPeriod)(te_pmin()), TE_NOCANDO);
    t_note("uMax (TimerData) = %u", umax);
}

TEST(te_period_res0_takes_pmin) { TE_PREQ(); te_period_case(100, 0, TIME_PERIODIC); }
TEST(te_period_res1_takes_1) { TE_PREQ(); te_period_case(100, 1, TIME_PERIODIC); }
TEST(te_period_res_huge_takes_nothing) { TE_PREQ(); te_period_case(100, 0xFFFFFFFFu, TIME_PERIODIC); }
TEST(te_period_res_1000_takes_nothing) { TE_PREQ(); te_period_case(TE_LONG, 1000, TIME_ONESHOT); }

TEST(te_period_table_res_below_umax) {
    TE_PREQ();
    UINT umax = te_umax();
    for (UINT r = 0; r <= umax + 2; r++) te_period_case(TE_LONG, r, TIME_ONESHOT);
}

TEST(te_period_table_delay_below_res_uses_timerdata) {
    TE_PREQ();
    UINT umax = te_umax(), pmin = te_pmin();
    /* uDelay < clamped resolution -> TimerData (no reference); uDelay == resolution keeps it */
    for (UINT r = pmin + 1; r < umax; r++) {
        te_period_case(r - 1 < pmin ? pmin : r - 1, r, TIME_PERIODIC);
        te_period_case(r, r, TIME_PERIODIC);
    }
}

TEST(te_period_table_matrix) {
    TE_PREQ();
    UINT delays[] = { 1, 2, 3, 5, 8, 10, 15, 16, 20, 100, 1000 };
    UINT res[] = { 0, 1, 2, 4, 5, 7, 10, 15, 16, 17, 50 };
    for (size_t i = 0; i < sizeof delays / sizeof delays[0]; i++)
        for (size_t j = 0; j < sizeof res / sizeof res[0]; j++) {
            if (delays[i] < te_pmin()) continue;
            te_period_case(delays[i], res[j], TIME_PERIODIC);
        }
}

TEST(te_period_two_events_same_period_two_refs) {
    TE_PREQ();
    UINT p = te_pmin() + 4 < te_umax() ? te_pmin() + 4 : te_pmin();
    UINT a = te_set(TE_LONG, p, NULL, 0, TIME_ONESHOT);
    UINT b = te_set(TE_LONG, p, NULL, 0, TIME_ONESHOT);
    CHECK(a && b);
    /* two references: End twice succeeds, the third is 97; restore both */
    EXPECT_EQ(U(timeEndPeriod)(p), 0);
    EXPECT_EQ(U(timeEndPeriod)(p), 0);
    EXPECT_EQ(U(timeEndPeriod)(p), TE_NOCANDO);
    EXPECT_EQ(U(timeBeginPeriod)(p), 0);
    EXPECT_EQ(U(timeBeginPeriod)(p), 0);
    if (a) EXPECT_EQ(te_kill(a), 0);
    CHECK(te_period_held(p));
    if (b) EXPECT_EQ(te_kill(b), 0);
    CHECK(!te_period_held(p));
}

TEST(te_period_distinct_periods_held_independently) {
    TE_PREQ();
    UINT pmin = te_pmin(), umax = te_umax();
    if (umax - pmin < 4) { t_skip("period window too small (%u..%u)", pmin, umax); return; }
    UINT p1 = pmin, p2 = pmin + 2, p3 = umax - 1;
    UINT a = te_set(TE_LONG, p1, NULL, 0, TIME_ONESHOT);
    UINT b = te_set(TE_LONG, p2, NULL, 0, TIME_PERIODIC);
    UINT c = te_set(TE_LONG, p3, NULL, 0, TIME_ONESHOT | TIME_CALLBACK_EVENT_SET);
    CHECK(a && b && c);
    EXPECT_EQ(te_held_mask(), (1ull << (p1 - pmin)) | (1ull << (p2 - pmin)) | (1ull << (p3 - pmin)));
    if (b) EXPECT_EQ(te_kill(b), 0);
    EXPECT_EQ(te_held_mask(), (1ull << (p1 - pmin)) | (1ull << (p3 - pmin)));
    if (a) EXPECT_EQ(te_kill(a), 0);
    EXPECT_EQ(te_held_mask(), 1ull << (p3 - pmin));
    if (c) EXPECT_EQ(te_kill(c), 0);
    EXPECT_EQ(te_held_mask(), 0);
}

TEST(te_period_oneshot_releases_after_firing) {
    TE_PREQ();
    UINT pmin = te_pmin();
    UINT p = pmin + 3 < te_umax() ? pmin + 3 : pmin;
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    UINT id = te_set(40, p, (LPTIMECALLBACK)ev, 0, TIME_ONESHOT | TIME_CALLBACK_EVENT_SET);
    CHECK(id != 0);
    CHECK(te_period_held(p));
    EXPECT_EQ(WaitForSingleObject(ev, 2000), WAIT_OBJECT_0);
    /* SetEvent and the slot/period release happen under the same timer lock: a kill now is 97, nothing held */
    EXPECT_EQ(te_kill(id), TE_NOCANDO);
    EXPECT_EQ(te_held_mask(), 0);
    CloseHandle(ev);
}

TEST(te_period_oneshot_function_releases_after_firing) {
    TE_PREQ();
    static te_ctx c; te_ctx_init(&c);
    UINT p = te_pmin() + 1 < te_umax() ? te_pmin() + 1 : te_pmin();
    UINT id = te_set(30, p, te_cb_ctx, (DWORD_PTR)&c, TIME_ONESHOT);
    CHECK(id != 0);
    EXPECT_EQ(WaitForSingleObject(c.fired, 2000), WAIT_OBJECT_0);
    Sleep(100);
    EXPECT_EQ(te_held_mask(), 0);
    EXPECT_EQ(te_kill(id), TE_NOCANDO);
    te_ctx_free(&c);
}

TEST(te_period_periodic_keeps_reference_while_firing) {
    TE_PREQ();
    UINT p = te_pmin();
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    UINT id = te_set(10, p, (LPTIMECALLBACK)ev, 0, TIME_PERIODIC | TIME_CALLBACK_EVENT_SET);
    CHECK(id != 0);
    for (int i = 0; i < 5; i++) EXPECT_EQ(WaitForSingleObject(ev, 2000), WAIT_OBJECT_0);
    CHECK(te_period_held(p));
    EXPECT_EQ(te_kill(id), 0);
    EXPECT_EQ(te_held_mask(), 0);
    CloseHandle(ev);
}

TEST(te_period_full_table_failure_releases_period) {
    TE_PREQ();
    UINT ids[TE_SLOTS];
    for (int i = 0; i < TE_SLOTS; i++) ids[i] = te_set(TE_LONG, TE_COARSE, NULL, 0, TIME_ONESHOT);
    UINT pmin = te_pmin(), umax = te_umax();
    for (UINT p = pmin; p < umax; p++) EXPECT_EQ(te_set(TE_LONG, p, NULL, 0, TIME_ONESHOT), 0);
    /* every timeBeginPeriod taken for the 17th event was given back */
    EXPECT_EQ(te_held_mask(), 0);
    for (int i = 0; i < TE_SLOTS; i++) if (ids[i]) EXPECT_EQ(te_kill(ids[i]), 0);
}

TEST(te_period_user_reference_survives_event) {
    TE_PREQ();
    UINT p = te_pmin() + 2 < te_umax() ? te_pmin() + 2 : te_pmin();
    EXPECT_EQ(U(timeBeginPeriod)(p), 0);
    UINT id = te_set(TE_LONG, p, NULL, 0, TIME_PERIODIC);
    CHECK(id != 0);
    if (id) EXPECT_EQ(te_kill(id), 0);
    /* our own reference is still there */
    EXPECT_EQ(U(timeEndPeriod)(p), 0);
    EXPECT_EQ(U(timeEndPeriod)(p), TE_NOCANDO);
}

TEST(te_period_kill_unknown_releases_nothing) {
    TE_PREQ();
    UINT p = te_pmin();
    EXPECT_EQ(U(timeBeginPeriod)(p), 0);
    EXPECT_EQ(te_kill(TE_UNKNOWN_ID), TE_NOCANDO);
    EXPECT_EQ(te_kill(0xFFFFFFF1u), TE_NOCANDO);
    EXPECT_EQ(U(timeEndPeriod)(p), 0);
    EXPECT_EQ(U(timeEndPeriod)(p), TE_NOCANDO);
}

TEST(te_period_many_cycles_balanced) {
    TE_PREQ();
    UINT pmin = te_pmin(), umax = te_umax();
    for (int i = 0; i < 300; i++) {
        UINT p = pmin + (UINT)i % (umax - pmin);
        UINT id = te_set(TE_LONG, p, NULL, 0, i & 1 ? TIME_PERIODIC : TIME_ONESHOT);
        if (!id) { CHECKF(0, "cycle %d", i); continue; }
        te_kill(id);
    }
    EXPECT_EQ(te_held_mask(), 0);
}

TEST(te_period_no_leak_after_file) {
    TE_PREQ();
    EXPECT_EQ(te_free_slots(), TE_SLOTS);
    EXPECT_EQ(te_held_mask(), 0);
}
