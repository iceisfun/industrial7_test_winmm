/* time_events: timing with loose bounds only (the guest is a VM; nothing here is a tight timing assert).
 * Decomp: one-shots use a relative NT timer of uDelay ms; periodic events keep an absolute due time
 * (interrupt time at arm + k * uDelay, timeSetTimerEvent 0x2c90 / timeThread 0x26c0) and re-arm from the scheduled
 * time, not from the dispatch time; when a dispatch is late, the next due time may already be past, which arms the
 * NT timer with 0 (fires immediately): missed periods are caught up back to back.
 */
#include "te.h"

static volatile DWORD te_t_set, te_t_cb[64];
static volatile LONG te_t_n;
static HANDLE te_t_ev;
static volatile DWORD te_t_stall;
static void CALLBACK te_cb_time(UINT id, UINT msg, DWORD_PTR user, DWORD_PTR a, DWORD_PTR b) {
    LONG n = InterlockedIncrement(&te_t_n);
    DWORD now = U(timeGetTime)();
    if (n <= 64) te_t_cb[n - 1] = now;
    if (n == 1 && te_t_stall) Sleep(te_t_stall);
    if (te_t_ev) SetEvent(te_t_ev);
}
static void te_t_reset(void) { te_t_n = 0; te_t_stall = 0; memset((void *)te_t_cb, 0, sizeof te_t_cb); }

TEST(te_time_oneshot_latency_table) {
    TE_REQUIRE();
    REQUIRE(U(timeGetTime));
    UINT delays[] = { 10, 30, 50, 100, 250 };
    te_t_ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    for (size_t i = 0; i < sizeof delays / sizeof delays[0]; i++) {
        te_t_reset();
        DWORD t0 = te_now();
        UINT id = te_set(delays[i], 1, te_cb_time, 0, TIME_ONESHOT);
        CHECK(id != 0);
        EXPECT_EQ(WaitForSingleObject(te_t_ev, 3000), WAIT_OBJECT_0);
        long dt = (long)(te_t_cb[0] - t0);
        t_note("one-shot %u ms: fired after %ld ms", delays[i], dt);
        CHECKF(dt >= (long)delays[i] - 2, "delay %u fired after %ld ms", delays[i], dt);
        CHECKF(dt <= (long)delays[i] + 500, "delay %u fired after %ld ms", delays[i], dt);
    }
    Sleep(30);
    CloseHandle(te_t_ev); te_t_ev = NULL;
}

TEST(te_time_oneshot_coarse_resolution_still_fires) {
    TE_REQUIRE();
    te_t_ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    te_t_reset();
    DWORD t0 = te_now();
    UINT id = te_set(40, 0xFFFFFFFFu, te_cb_time, 0, TIME_ONESHOT);
    CHECK(id != 0);
    EXPECT_EQ(WaitForSingleObject(te_t_ev, 3000), WAIT_OBJECT_0);
    long dt = (long)(te_t_cb[0] - t0);
    t_note("one-shot 40 ms, resolution TimerData: %ld ms", dt);
    CHECK(dt >= 30 && dt <= 600);
    Sleep(30);
    CloseHandle(te_t_ev); te_t_ev = NULL;
}

TEST(te_time_periodic_count_1s_at_20ms) {
    TE_REQUIRE();
    static te_ctx c; te_ctx_init(&c);
    UINT id = te_set(20, 1, te_cb_ctx, (DWORD_PTR)&c, TIME_PERIODIC | TIME_KILL_SYNCHRONOUS);
    CHECK(id != 0);
    Sleep(1000);
    EXPECT_EQ(te_kill(id), 0);
    t_note("periodic 20 ms over 1000 ms: %ld callbacks", c.count);
    CHECKF(c.count >= 25 && c.count <= 75, "count %ld", c.count);
    te_ctx_free(&c);
}

TEST(te_time_periodic_count_1s_at_10ms) {
    TE_REQUIRE();
    static te_ctx c; te_ctx_init(&c);
    UINT id = te_set(10, 1, te_cb_ctx, (DWORD_PTR)&c, TIME_PERIODIC | TIME_KILL_SYNCHRONOUS);
    CHECK(id != 0);
    Sleep(1000);
    EXPECT_EQ(te_kill(id), 0);
    t_note("periodic 10 ms over 1000 ms: %ld callbacks", c.count);
    CHECKF(c.count >= 50 && c.count <= 150, "count %ld", c.count);
    te_ctx_free(&c);
}

TEST(te_time_periodic_1ms) {
    TE_REQUIRE();
    static te_ctx c; te_ctx_init(&c);
    UINT id = te_set(te_pmin(), 1, te_cb_ctx, (DWORD_PTR)&c, TIME_PERIODIC | TIME_KILL_SYNCHRONOUS);
    CHECK(id != 0);
    Sleep(500);
    EXPECT_EQ(te_kill(id), 0);
    t_note("periodic %u ms over 500 ms: %ld callbacks", te_pmin(), c.count);
    CHECKF(c.count >= 50, "count %ld", c.count);
    CHECKF(c.count <= 1500, "count %ld", c.count);
    te_ctx_free(&c);
}

TEST(te_time_periodic_coarse_resolution_count) {
    TE_REQUIRE();
    /* uResolution is a hint: a coarse period does not change the delay between dispatches by much */
    static te_ctx c; te_ctx_init(&c);
    UINT id = te_set(50, TE_COARSE, te_cb_ctx, (DWORD_PTR)&c, TIME_PERIODIC | TIME_KILL_SYNCHRONOUS);
    CHECK(id != 0);
    Sleep(1000);
    EXPECT_EQ(te_kill(id), 0);
    t_note("periodic 50 ms, coarse resolution, over 1000 ms: %ld", c.count);
    CHECKF(c.count >= 10 && c.count <= 30, "count %ld", c.count);
    te_ctx_free(&c);
}

TEST(te_time_periodic_first_tick_not_early) {
    TE_REQUIRE();
    te_t_ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    te_t_reset();
    DWORD t0 = te_now();
    UINT id = te_set(200, 1, te_cb_time, 0, TIME_PERIODIC | TIME_KILL_SYNCHRONOUS);
    CHECK(id != 0);
    EXPECT_EQ(WaitForSingleObject(te_t_ev, 3000), WAIT_OBJECT_0);
    EXPECT_EQ(te_kill(id), 0);
    long dt = (long)(te_t_cb[0] - t0);
    t_note("periodic 200 ms: first tick after %ld ms", dt);
    CHECKF(dt >= 198 && dt <= 800, "first tick %ld", dt);
    CloseHandle(te_t_ev); te_t_ev = NULL;
}

TEST(te_time_periodic_rearms_from_schedule) {
    TE_REQUIRE();
    /* 25 ticks at 20 ms: the last tick is ~500 ms after arming whatever the per-tick latency (drift-free) */
    te_t_ev = NULL;
    te_t_reset();
    DWORD t0 = te_now();
    UINT id = te_set(20, 1, te_cb_time, 0, TIME_PERIODIC | TIME_KILL_SYNCHRONOUS);
    CHECK(id != 0);
    DWORD w0 = GetTickCount();
    while (te_t_n < 25 && GetTickCount() - w0 < 3000) Sleep(5);
    EXPECT_EQ(te_kill(id), 0);
    CHECK(te_t_n >= 25);
    long last = (long)(te_t_cb[24] - t0);
    long sum = 0;
    for (int i = 1; i < 25; i++) sum += (long)(te_t_cb[i] - te_t_cb[i - 1]);
    t_note("tick 25 at %ld ms (ideal 500), mean interval %ld.%02ld ms, drift %ld ms", last, sum / 24, (sum % 24) * 100 / 24, last - 500);
    CHECKF(last >= 480 && last <= 1000, "tick 25 at %ld ms", last);
}

TEST(te_time_periodic_catches_up_after_stall) {
    TE_REQUIRE();
    /* the first callback blocks the timer thread 400 ms; missed ticks are dispatched back to back afterwards */
    te_t_ev = NULL;
    te_t_reset();
    te_t_stall = 400;
    DWORD t0 = te_now();
    UINT id = te_set(20, 1, te_cb_time, 0, TIME_PERIODIC | TIME_KILL_SYNCHRONOUS);
    CHECK(id != 0);
    Sleep(700);
    LONG n = te_t_n;
    EXPECT_EQ(te_kill(id), 0);
    long burst = 0;
    if (n >= 10) burst = (long)(te_t_cb[9] - te_t_cb[1]);
    t_note("after a 400 ms stall: %ld callbacks in 700 ms (ideal 35), ticks 2..10 within %ld ms", n, burst);
    CHECKF(n >= 25, "count %ld (no catch-up would give ~15)", n);
    if (n >= 10) CHECKF(burst <= 300, "ticks 2..10 took %ld ms (catch-up runs them back to back)", burst);
    te_t_stall = 0;
}

TEST(te_time_long_delay_not_fired) {
    TE_REQUIRE();
    static te_ctx c; te_ctx_init(&c);
    UINT a = te_set(TE_MAX_DELAY, 1, te_cb_ctx, (DWORD_PTR)&c, TIME_PERIODIC | TIME_KILL_SYNCHRONOUS);
    UINT b = te_set(TE_MAX_DELAY, 1, te_cb_ctx, (DWORD_PTR)&c, TIME_ONESHOT | TIME_KILL_SYNCHRONOUS);
    CHECK(a && b);
    Sleep(200);
    EXPECT_EQ(c.count, 0);
    if (a) EXPECT_EQ(te_kill(a), 0);
    if (b) EXPECT_EQ(te_kill(b), 0);
    te_ctx_free(&c);
}

TEST(te_time_two_periodics_rates) {
    TE_REQUIRE();
    static te_ctx f, s;
    te_ctx_init(&f); te_ctx_init(&s);
    UINT a = te_set(10, 1, te_cb_ctx, (DWORD_PTR)&f, TIME_PERIODIC | TIME_KILL_SYNCHRONOUS);
    UINT b = te_set(50, 1, te_cb_ctx, (DWORD_PTR)&s, TIME_PERIODIC | TIME_KILL_SYNCHRONOUS);
    CHECK(a && b);
    Sleep(1000);
    if (a) EXPECT_EQ(te_kill(a), 0);
    if (b) EXPECT_EQ(te_kill(b), 0);
    t_note("10 ms: %ld, 50 ms: %ld in 1 s", f.count, s.count);
    CHECK(f.count > s.count * 2);
    CHECK(s.count >= 10 && s.count <= 30);
    te_ctx_free(&f); te_ctx_free(&s);
}

TEST(te_timing_no_leak_after_file) {
    TE_REQUIRE();
    EXPECT_EQ(te_free_slots(), TE_SLOTS);
    EXPECT_EQ(te_held_mask(), 0);
}
