/* time_events: TIME_CALLBACK_FUNCTION semantics.
 * Decomp (winmm!timeThread 0x26c0): fptc(uTimerID, 0, dwUser, 0, 0) on the single timer thread created by
 * TimeInitThread (0x2d7c: CreateThread + SetThreadPriority(15 = THREAD_PRIORITY_TIME_CRITICAL)); the thread waits
 * alertably (NtWaitForMultipleObjects Alertable=TRUE) and runs dispatches one at a time; the callback is called
 * outside the timer lock, so timeSetEvent/timeKillEvent may be called from inside it. A one-shot frees its slot
 * after the callback only if the slot still carries its id. TIME_KILL_SYNCHRONOUS: timeKillEvent from another
 * thread while that event's callback runs waits for it to return; from the timer thread itself it never waits.
 * All contexts are static and every periodic event uses TIME_KILL_SYNCHRONOUS, so no callback outlives a test.
 */
#include "te.h"

static volatile DWORD te_timer_tid;   /* the timer thread, learned from the first callback */

TEST(te_cb_oneshot_args_exact) {
    TE_REQUIRE();
    static te_ctx c; te_ctx_init(&c);
    DWORD me = GetCurrentThreadId();
    UINT id = te_set(25, 1, te_cb_ctx, (DWORD_PTR)&c, TIME_ONESHOT);
    CHECK(id != 0);
    EXPECT_EQ(WaitForSingleObject(c.fired, 2000), WAIT_OBJECT_0);
    Sleep(100);
    EXPECT_EQ(c.count, 1);
    EXPECT_EQ(c.id, id);
    EXPECT_EQ(c.msg, 0);
    EXPECT_EQ(c.user, (DWORD_PTR)&c);
    EXPECT_EQ(c.dw1, 0);
    EXPECT_EQ(c.dw2, 0);
    EXPECT_EQ(c.bad_args, 0);
    CHECK(c.tid != 0 && c.tid != me);
    if (c.tid) te_timer_tid = c.tid;
    EXPECT_EQ(te_kill(id), TE_NOCANDO);
    te_ctx_free(&c);
}

TEST(te_cb_oneshot_fires_exactly_once) {
    TE_REQUIRE();
    static te_ctx c; te_ctx_init(&c);
    UINT id = te_set(10, 1, te_cb_ctx, (DWORD_PTR)&c, TIME_ONESHOT);
    CHECK(id != 0);
    EXPECT_EQ(WaitForSingleObject(c.fired, 2000), WAIT_OBJECT_0);
    Sleep(300);
    EXPECT_EQ(c.count, 1);
    te_ctx_free(&c);
}

/* dwUser is passed through as a full pointer-sized value */
static volatile DWORD_PTR te_u_seen[8];
static volatile LONG te_u_n;
static HANDLE te_u_ev;
static void CALLBACK te_cb_user(UINT id, UINT msg, DWORD_PTR user, DWORD_PTR a, DWORD_PTR b) {
    LONG i = InterlockedIncrement(&te_u_n) - 1;
    if (i < 8) te_u_seen[i] = user;
    SetEvent(te_u_ev);
}

TEST(te_cb_dwuser_full_64bit_table) {
    TE_REQUIRE();
    DWORD_PTR vals[] = { 0, 1, 0xFFFFFFFFull, 0x100000000ull, 0xFEDCBA9876543210ull, 0x8000000000000000ull,
                         0xFFFFFFFFFFFFFFFFull, 0x00000001DEADBEEFull };
    te_u_ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    for (size_t i = 0; i < sizeof vals / sizeof vals[0]; i++) {
        te_u_n = 0; te_u_seen[0] = 0x5A5A;
        UINT id = te_set(5, 1, te_cb_user, vals[i], TIME_ONESHOT);
        CHECK(id != 0);
        EXPECT_EQ(WaitForSingleObject(te_u_ev, 2000), WAIT_OBJECT_0);
        EXPECT_EQ(te_u_seen[0], vals[i]);
        Sleep(20);
        EXPECT_EQ(te_u_n, 1);
    }
    CloseHandle(te_u_ev); te_u_ev = NULL;
}

TEST(te_cb_periodic_args_every_call) {
    TE_REQUIRE();
    static te_ctx c; te_ctx_init(&c);
    UINT id = te_set(10, 1, te_cb_ctx, (DWORD_PTR)&c, TIME_PERIODIC | TIME_KILL_SYNCHRONOUS);
    CHECK(id != 0);
    LONG n = te_wait_count(&c, 10, 3000);
    CHECKF(n >= 10, "count %ld", n);
    EXPECT_EQ(te_kill(id), 0);
    EXPECT_EQ(c.bad_args, 0);
    EXPECT_EQ(c.id, id);
    EXPECT_EQ(c.user, (DWORD_PTR)&c);
    te_ctx_free(&c);
}

TEST(te_cb_not_on_caller_thread) {
    TE_REQUIRE();
    static te_ctx c; te_ctx_init(&c);
    UINT id = te_set(10, 1, te_cb_ctx, (DWORD_PTR)&c, TIME_ONESHOT);
    CHECK(id != 0);
    EXPECT_EQ(WaitForSingleObject(c.fired, 2000), WAIT_OBJECT_0);
    CHECK(c.tid != GetCurrentThreadId());
    /* the thread belongs to this process */
    HANDLE h = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, c.tid);
    CHECK(h != NULL);
    if (h) { EXPECT_EQ(GetProcessIdOfThread(h), GetCurrentProcessId()); CloseHandle(h); }
    te_ctx_free(&c);
}

TEST(te_cb_thread_priority_time_critical) {
    TE_REQUIRE();
    static te_ctx c; te_ctx_init(&c);
    UINT id = te_set(10, 1, te_cb_ctx, (DWORD_PTR)&c, TIME_ONESHOT);
    CHECK(id != 0);
    EXPECT_EQ(WaitForSingleObject(c.fired, 2000), WAIT_OBJECT_0);
    t_note("timer thread priority %d", c.prio);
    EXPECT_EQ(c.prio, THREAD_PRIORITY_TIME_CRITICAL);
    if (c.tid) EXPECT_EQ(te_thread_priority(c.tid), THREAD_PRIORITY_TIME_CRITICAL);
    te_ctx_free(&c);
}

TEST(te_cb_single_shared_thread) {
    TE_REQUIRE();
    static te_ctx c[4];
    UINT ids[4];
    for (int i = 0; i < 4; i++) te_ctx_init(&c[i]);
    for (int i = 0; i < 4; i++) {
        ids[i] = te_set(10 + 5 * i, 1, te_cb_ctx, (DWORD_PTR)&c[i], i & 1 ? (TIME_PERIODIC | TIME_KILL_SYNCHRONOUS) : TIME_ONESHOT);
        CHECK(ids[i] != 0);
    }
    for (int i = 0; i < 4; i++) EXPECT_EQ(WaitForSingleObject(c[i].fired, 2000), WAIT_OBJECT_0);
    for (int i = 1; i < 4; i++) EXPECT_EQ(c[i].tid, c[0].tid);
    if (te_timer_tid) EXPECT_EQ(c[0].tid, te_timer_tid);   /* the same thread for the whole process lifetime */
    EXPECT_EQ(te_kill(ids[1]), 0);
    EXPECT_EQ(te_kill(ids[3]), 0);
    for (int i = 0; i < 4; i++) te_ctx_free(&c[i]);
}

TEST(te_cb_dispatches_are_serialized) {
    TE_REQUIRE();
    /* two periodic events sharing one context; every callback sleeps: a single thread never overlaps them */
    static te_ctx c; te_ctx_init(&c);
    c.sleep_ms = 4;
    UINT a = te_set(5, 1, te_cb_ctx, (DWORD_PTR)&c, TIME_PERIODIC | TIME_KILL_SYNCHRONOUS);
    UINT b = te_set(7, 1, te_cb_ctx, (DWORD_PTR)&c, TIME_PERIODIC | TIME_KILL_SYNCHRONOUS);
    CHECK(a && b);
    te_wait_count(&c, 30, 3000);
    if (a) EXPECT_EQ(te_kill(a), 0);
    if (b) EXPECT_EQ(te_kill(b), 0);
    CHECKF(c.count >= 30, "count %ld", c.count);
    EXPECT_EQ(c.max_inside, 1);
    te_ctx_free(&c);
}

TEST(te_cb_blocking_callback_delays_others) {
    TE_REQUIRE();
    /* while one callback blocks the timer thread, another due event is not dispatched */
    static te_ctx slow, fast;
    te_ctx_init(&slow); te_ctx_init(&fast);
    slow.sleep_ms = 400;
    slow.entered = CreateEventA(NULL, TRUE, FALSE, NULL);
    UINT a = te_set(10, 1, te_cb_ctx, (DWORD_PTR)&slow, TIME_ONESHOT);
    CHECK(a != 0);
    EXPECT_EQ(WaitForSingleObject(slow.entered, 2000), WAIT_OBJECT_0);
    UINT b = te_set(10, 1, te_cb_ctx, (DWORD_PTR)&fast, TIME_ONESHOT);
    CHECK(b != 0);
    Sleep(150);
    EXPECT_EQ(fast.count, 0);       /* due ~140 ms ago, but the only timer thread is busy */
    EXPECT_EQ(slow.done, 0);
    EXPECT_EQ(WaitForSingleObject(fast.fired, 2000), WAIT_OBJECT_0);
    EXPECT_EQ(slow.done, 1);
    CHECK(fast.tid == slow.tid);
    te_ctx_free(&slow); te_ctx_free(&fast);
}

TEST(te_cb_kill_before_fire_never_calls) {
    TE_REQUIRE();
    static te_ctx c; te_ctx_init(&c);
    UINT id = te_set(300, 1, te_cb_ctx, (DWORD_PTR)&c, TIME_ONESHOT);
    CHECK(id != 0);
    EXPECT_EQ(te_kill(id), 0);
    Sleep(500);
    EXPECT_EQ(c.count, 0);
    te_ctx_free(&c);
}

TEST(te_cb_kill_periodic_before_first_tick) {
    TE_REQUIRE();
    static te_ctx c; te_ctx_init(&c);
    UINT id = te_set(200, 1, te_cb_ctx, (DWORD_PTR)&c, TIME_PERIODIC);
    CHECK(id != 0);
    EXPECT_EQ(te_kill(id), 0);
    Sleep(450);
    EXPECT_EQ(c.count, 0);
    te_ctx_free(&c);
}

TEST(te_cb_no_calls_after_kill_returns) {
    TE_REQUIRE();
    static te_ctx c; te_ctx_init(&c);
    UINT id = te_set(5, 1, te_cb_ctx, (DWORD_PTR)&c, TIME_PERIODIC | TIME_KILL_SYNCHRONOUS);
    CHECK(id != 0);
    te_wait_count(&c, 5, 2000);
    EXPECT_EQ(te_kill(id), 0);
    LONG n = c.count;
    EXPECT_EQ(c.inside, 0);
    Sleep(200);
    EXPECT_EQ(c.count, n);
    te_ctx_free(&c);
}

/* ---------------------------------------------------------------- 0x30 / 0x40 / 0x80: accepted, nothing dispatched */

TEST(te_cb_type_bits_without_dispatch_table) {
    TE_REQUIRE();
    static te_ctx c; te_ctx_init(&c);
    UINT types[] = { 0x30, 0x40, 0x50, 0x60, 0x70, 0x80, 0x90, 0xA0, 0xB0, 0xC0, 0xD0, 0xE0, 0xF0 };
    for (size_t i = 0; i < sizeof types / sizeof types[0]; i++) {
        UINT id = te_set(10, 1, te_cb_ctx, (DWORD_PTR)&c, TIME_ONESHOT | types[i]);
        CHECKF(id != 0, "type 0x%x", types[i]);
        UINT idp = te_set(10, 1, te_cb_ctx, (DWORD_PTR)&c, TIME_PERIODIC | types[i]);
        CHECKF(idp != 0, "type 0x%x periodic", types[i]);
        Sleep(60);
        /* the one-shot still "fired" (slot freed), the periodic still runs; neither called fptc */
        if (id) EXPECT_EQ(te_kill(id), TE_NOCANDO);
        if (idp) EXPECT_EQ(te_kill(idp), 0);
    }
    EXPECT_EQ(c.count, 0);
    te_ctx_free(&c);
}

TEST(te_cb_type_bits_without_dispatch_do_not_touch_event) {
    TE_REQUIRE();
    HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    UINT types[] = { 0x30, 0x40, 0x80, 0xC0, 0xF0 };
    for (size_t i = 0; i < sizeof types / sizeof types[0]; i++) {
        UINT id = te_set(10, 1, (LPTIMECALLBACK)ev, 0, TIME_PERIODIC | types[i]);
        CHECKF(id != 0, "type 0x%x", types[i]);
        EXPECT_EQ(WaitForSingleObject(ev, 120), WAIT_TIMEOUT);
        if (id) EXPECT_EQ(te_kill(id), 0);
    }
    CloseHandle(ev);
}

TEST(te_cb_kill_synchronous_bit_alone_is_function_type) {
    TE_REQUIRE();
    static te_ctx c; te_ctx_init(&c);
    UINT id = te_set(10, 1, te_cb_ctx, (DWORD_PTR)&c, TIME_KILL_SYNCHRONOUS);
    CHECK(id != 0);
    EXPECT_EQ(WaitForSingleObject(c.fired, 2000), WAIT_OBJECT_0);
    Sleep(50);
    EXPECT_EQ(c.count, 1);
    EXPECT_EQ(c.id, id);
    te_ctx_free(&c);
}

/* ---------------------------------------------------------------- calls from inside the callback */

static volatile UINT te_self_id, te_self_res, te_self_res2;
static volatile LONG te_self_n;
static HANDLE te_self_ev;
static void CALLBACK te_cb_kill_self(UINT id, UINT msg, DWORD_PTR user, DWORD_PTR a, DWORD_PTR b) {
    LONG n = InterlockedIncrement(&te_self_n);
    if (n == 1) { te_self_res = te_kill(id); te_self_res2 = te_kill(id); }
    SetEvent(te_self_ev);
}

TEST(te_cb_oneshot_kills_itself) {
    TE_REQUIRE();
    te_self_ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    te_self_n = 0; te_self_res = te_self_res2 = 0xFFFF;
    UINT id = te_set(10, 1, te_cb_kill_self, 0, TIME_ONESHOT);
    CHECK(id != 0);
    EXPECT_EQ(WaitForSingleObject(te_self_ev, 2000), WAIT_OBJECT_0);
    Sleep(50);
    /* the slot still carries the id during the callback: the first kill succeeds, the second is 97 */
    EXPECT_EQ(te_self_res, 0);
    EXPECT_EQ(te_self_res2, TE_NOCANDO);
    EXPECT_EQ(te_self_n, 1);
    EXPECT_EQ(te_kill(id), TE_NOCANDO);
    CloseHandle(te_self_ev);
}

TEST(te_cb_oneshot_kills_itself_period_released_once) {
    TE_REQUIRE();
    REQUIRE(U(timeEndPeriod));
    UINT p = te_pmin() + 5 < te_umax() ? te_pmin() + 5 : te_pmin();
    EXPECT_EQ(te_held_mask(), 0);
    te_self_ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    te_self_n = 0; te_self_res = 0xFFFF;
    /* our own reference on p: a double release by the timer thread would eat it */
    EXPECT_EQ(U(timeBeginPeriod)(p), 0);
    UINT id = te_set(10, p, te_cb_kill_self, 0, TIME_ONESHOT);
    CHECK(id != 0);
    EXPECT_EQ(WaitForSingleObject(te_self_ev, 2000), WAIT_OBJECT_0);
    Sleep(100);
    EXPECT_EQ(te_self_res, 0);
    EXPECT_EQ(U(timeEndPeriod)(p), 0);           /* ours is still there */
    EXPECT_EQ(U(timeEndPeriod)(p), TE_NOCANDO);  /* and the event's is gone */
    CloseHandle(te_self_ev);
}

TEST(te_cb_periodic_kills_itself_sync_no_deadlock) {
    TE_REQUIRE();
    te_self_ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    te_self_n = 0; te_self_res = te_self_res2 = 0xFFFF;
    /* TIME_KILL_SYNCHRONOUS never waits on the timer thread itself */
    UINT id = te_set(10, 1, te_cb_kill_self, 0, TIME_PERIODIC | TIME_KILL_SYNCHRONOUS);
    CHECK(id != 0);
    EXPECT_EQ(WaitForSingleObject(te_self_ev, 2000), WAIT_OBJECT_0);
    Sleep(150);
    EXPECT_EQ(te_self_res, 0);
    EXPECT_EQ(te_self_res2, TE_NOCANDO);
    EXPECT_EQ(te_self_n, 1);   /* killed itself: no further ticks */
    EXPECT_EQ(te_kill(id), TE_NOCANDO);
    CloseHandle(te_self_ev);
}

TEST(te_cb_periodic_kills_itself_async) {
    TE_REQUIRE();
    te_self_ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    te_self_n = 0; te_self_res = 0xFFFF;
    UINT id = te_set(10, 1, te_cb_kill_self, 0, TIME_PERIODIC);
    CHECK(id != 0);
    EXPECT_EQ(WaitForSingleObject(te_self_ev, 2000), WAIT_OBJECT_0);
    Sleep(150);
    EXPECT_EQ(te_self_res, 0);
    EXPECT_EQ(te_self_n, 1);
    EXPECT_EQ(te_kill(id), TE_NOCANDO);
    CloseHandle(te_self_ev);
}

/* a periodic callback kills another live periodic event */
static volatile UINT te_victim, te_victim_res;
static volatile LONG te_killer_n;
static void CALLBACK te_cb_kill_other(UINT id, UINT msg, DWORD_PTR user, DWORD_PTR a, DWORD_PTR b) {
    if (InterlockedIncrement(&te_killer_n) == 1) te_victim_res = te_kill(te_victim);
}

TEST(te_cb_kills_other_event) {
    TE_REQUIRE();
    static te_ctx v; te_ctx_init(&v);
    te_killer_n = 0; te_victim_res = 0xFFFF;
    te_victim = te_set(10, 1, te_cb_ctx, (DWORD_PTR)&v, TIME_PERIODIC | TIME_KILL_SYNCHRONOUS);
    CHECK(te_victim != 0);
    te_wait_count(&v, 2, 2000);
    UINT k = te_set(30, 1, te_cb_kill_other, 0, TIME_ONESHOT);
    CHECK(k != 0);
    DWORD t0 = GetTickCount();
    while (te_killer_n == 0 && GetTickCount() - t0 < 2000) Sleep(5);
    Sleep(50);
    EXPECT_EQ(te_victim_res, 0);
    LONG n = v.count;
    Sleep(150);
    EXPECT_EQ(v.count, n);
    EXPECT_EQ(te_kill(te_victim), TE_NOCANDO);
    te_ctx_free(&v);
}

/* chained one-shots: each callback arms the next */
static volatile UINT te_chain_ids[8];
static volatile LONG te_chain_n;
static HANDLE te_chain_done;
static void CALLBACK te_cb_chain(UINT id, UINT msg, DWORD_PTR user, DWORD_PTR a, DWORD_PTR b) {
    LONG n = InterlockedIncrement(&te_chain_n);
    if (n <= 8) te_chain_ids[n - 1] = id;
    if (n < 5) { if (!te_set(5, 1, te_cb_chain, user + 1, TIME_ONESHOT)) SetEvent(te_chain_done); }
    else SetEvent(te_chain_done);
}

TEST(te_cb_set_event_from_callback_chain) {
    TE_REQUIRE();
    te_chain_n = 0; memset((void *)te_chain_ids, 0, sizeof te_chain_ids);
    te_chain_done = CreateEventA(NULL, TRUE, FALSE, NULL);
    UINT id = te_set(5, 1, te_cb_chain, 0, TIME_ONESHOT);
    CHECK(id != 0);
    EXPECT_EQ(WaitForSingleObject(te_chain_done, 3000), WAIT_OBJECT_0);
    Sleep(100);
    EXPECT_EQ(te_chain_n, 5);
    EXPECT_EQ(te_chain_ids[0], id);
    for (int i = 1; i < 5; i++) {
        CHECK(te_chain_ids[i] != 0);
        CHECK(te_chain_ids[i] != te_chain_ids[i - 1]);
    }
    CloseHandle(te_chain_done);
    EXPECT_EQ(te_free_slots(), TE_SLOTS);
}

/* ---------------------------------------------------------------- TIME_KILL_SYNCHRONOUS from another thread */

TEST(te_cb_kill_synchronous_waits_for_running_callback) {
    TE_REQUIRE();
    static te_ctx c; te_ctx_init(&c);
    c.sleep_ms = 300; c.sleep_once = 1;
    c.entered = CreateEventA(NULL, TRUE, FALSE, NULL);
    UINT id = te_set(10, 1, te_cb_ctx, (DWORD_PTR)&c, TIME_PERIODIC | TIME_KILL_SYNCHRONOUS);
    CHECK(id != 0);
    EXPECT_EQ(WaitForSingleObject(c.entered, 2000), WAIT_OBJECT_0);
    DWORD t0 = GetTickCount();
    EXPECT_EQ(te_kill(id), 0);
    DWORD dt = GetTickCount() - t0;
    EXPECT_EQ(c.done, 1);          /* returned only after the running callback finished */
    EXPECT_EQ(c.inside, 0);
    t_note("sync kill waited %lu ms (callback sleeps 300)", dt);
    LONG n = c.count;
    Sleep(100);
    EXPECT_EQ(c.count, n);
    te_ctx_free(&c);
}

TEST(te_cb_kill_without_synchronous_returns_while_running) {
    TE_REQUIRE();
    static te_ctx c; te_ctx_init(&c);
    c.sleep_ms = 400; c.sleep_once = 1;
    c.entered = CreateEventA(NULL, TRUE, FALSE, NULL);
    UINT id = te_set(10, 1, te_cb_ctx, (DWORD_PTR)&c, TIME_PERIODIC);
    CHECK(id != 0);
    EXPECT_EQ(WaitForSingleObject(c.entered, 2000), WAIT_OBJECT_0);
    EXPECT_EQ(te_kill(id), 0);
    EXPECT_EQ(c.done, 0);          /* no wait: the 400 ms callback is still running */
    /* let it finish before the context goes away; it is not dispatched again */
    DWORD t0 = GetTickCount();
    while (c.done == 0 && GetTickCount() - t0 < 2000) Sleep(10);
    EXPECT_EQ(c.done, 1);
    Sleep(100);
    EXPECT_EQ(c.count, 1);
    te_ctx_free(&c);
}

TEST(te_cb_kill_synchronous_oneshot_running) {
    TE_REQUIRE();
    static te_ctx c; te_ctx_init(&c);
    c.sleep_ms = 250;
    c.entered = CreateEventA(NULL, TRUE, FALSE, NULL);
    UINT id = te_set(10, 1, te_cb_ctx, (DWORD_PTR)&c, TIME_ONESHOT | TIME_KILL_SYNCHRONOUS);
    CHECK(id != 0);
    EXPECT_EQ(WaitForSingleObject(c.entered, 2000), WAIT_OBJECT_0);
    /* a running one-shot still owns its slot: kill is 0 and waits */
    EXPECT_EQ(te_kill(id), 0);
    EXPECT_EQ(c.done, 1);
    Sleep(50);
    EXPECT_EQ(te_kill(id), TE_NOCANDO);
    te_ctx_free(&c);
}

TEST(te_cb_kill_synchronous_other_event_does_not_wait) {
    TE_REQUIRE();
    /* the wait applies only to the event whose callback is running */
    static te_ctx slow; te_ctx_init(&slow);
    slow.sleep_ms = 400; slow.sleep_once = 1;
    slow.entered = CreateEventA(NULL, TRUE, FALSE, NULL);
    UINT other = te_set(TE_LONG, 1, NULL, 0, TIME_PERIODIC | TIME_KILL_SYNCHRONOUS);
    UINT id = te_set(10, 1, te_cb_ctx, (DWORD_PTR)&slow, TIME_PERIODIC | TIME_KILL_SYNCHRONOUS);
    CHECK(other && id);
    EXPECT_EQ(WaitForSingleObject(slow.entered, 2000), WAIT_OBJECT_0);
    EXPECT_EQ(te_kill(other), 0);
    EXPECT_EQ(slow.done, 0);
    EXPECT_EQ(te_kill(id), 0);     /* this one waits */
    EXPECT_EQ(slow.done, 1);
    te_ctx_free(&slow);
}

/* ---------------------------------------------------------------- the timer thread waits alertably */

static volatile DWORD te_apc_tid;
static HANDLE te_apc_ev;
static void CALLBACK te_apc(ULONG_PTR p) { te_apc_tid = GetCurrentThreadId(); SetEvent(te_apc_ev); }

TEST(te_cb_timer_thread_runs_queued_apc) {
    TE_REQUIRE();
    static te_ctx c; te_ctx_init(&c);
    UINT id = te_set(5, 1, te_cb_ctx, (DWORD_PTR)&c, TIME_ONESHOT);
    CHECK(id != 0);
    EXPECT_EQ(WaitForSingleObject(c.fired, 2000), WAIT_OBJECT_0);
    HANDLE th = OpenThread(THREAD_SET_CONTEXT, FALSE, c.tid);
    CHECK(th != NULL);
    if (th) {
        te_apc_ev = CreateEventA(NULL, FALSE, FALSE, NULL);
        te_apc_tid = 0;
        CHECK(QueueUserAPC(te_apc, th, 0) != 0);
        EXPECT_EQ(WaitForSingleObject(te_apc_ev, 2000), WAIT_OBJECT_0);
        EXPECT_EQ(te_apc_tid, c.tid);
        CloseHandle(te_apc_ev); te_apc_ev = NULL;
        CloseHandle(th);
    }
    /* the thread keeps serving events afterwards */
    static te_ctx d; te_ctx_init(&d);
    id = te_set(5, 1, te_cb_ctx, (DWORD_PTR)&d, TIME_ONESHOT);
    CHECK(id != 0);
    EXPECT_EQ(WaitForSingleObject(d.fired, 2000), WAIT_OBJECT_0);
    EXPECT_EQ(d.tid, c.tid);
    te_ctx_free(&c); te_ctx_free(&d);
}

TEST(te_cb_many_concurrent_oneshots_all_fire) {
    TE_REQUIRE();
    static te_ctx c[TE_SLOTS];
    UINT ids[TE_SLOTS];
    for (int i = 0; i < TE_SLOTS; i++) te_ctx_init(&c[i]);
    for (int i = 0; i < TE_SLOTS; i++) { ids[i] = te_set(20 + (i % 4) * 10, 1, te_cb_ctx, (DWORD_PTR)&c[i], TIME_ONESHOT); CHECK(ids[i] != 0); }
    for (int i = 0; i < TE_SLOTS; i++) EXPECT_EQ(WaitForSingleObject(c[i].fired, 2000), WAIT_OBJECT_0);
    Sleep(100);
    for (int i = 0; i < TE_SLOTS; i++) { EXPECT_EQ(c[i].count, 1); EXPECT_EQ(c[i].id, ids[i]); }
    EXPECT_EQ(te_free_slots(), TE_SLOTS);
    for (int i = 0; i < TE_SLOTS; i++) te_ctx_free(&c[i]);
}

TEST(te_cb_oneshot_order_by_delay) {
    TE_REQUIRE();
    static volatile LONG seq;
    static te_ctx c[3];
    LONG order[3];
    for (int i = 0; i < 3; i++) te_ctx_init(&c[i]);
    seq = 0;
    /* armed in reverse order, 150 ms apart */
    UINT a = te_set(320, 1, te_cb_ctx, (DWORD_PTR)&c[2], TIME_ONESHOT);
    UINT b = te_set(170, 1, te_cb_ctx, (DWORD_PTR)&c[1], TIME_ONESHOT);
    UINT d = te_set(20, 1, te_cb_ctx, (DWORD_PTR)&c[0], TIME_ONESHOT);
    CHECK(a && b && d);
    for (int i = 0; i < 3; i++) { EXPECT_EQ(WaitForSingleObject(c[i].fired, 2000), WAIT_OBJECT_0); order[i] = InterlockedIncrement(&seq); }
    CHECK(c[0].t_first <= c[1].t_first);
    CHECK(c[1].t_first <= c[2].t_first);
    for (int i = 0; i < 3; i++) te_ctx_free(&c[i]);
}

TEST(te_callback_no_leak_after_file) {
    TE_REQUIRE();
    EXPECT_EQ(te_free_slots(), TE_SLOTS);
    EXPECT_EQ(te_held_mask(), 0);
}
