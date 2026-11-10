/* time_events: shared helpers and the basic contract smoke tests.
 * VM_TIMEOUT: 300
 */
#include "te.h"

void CALLBACK te_cb_ctx(UINT id, UINT msg, DWORD_PTR user, DWORD_PTR dw1, DWORD_PTR dw2) {
    te_ctx *c = (te_ctx *)user;
    LONG in = InterlockedIncrement(&c->inside);
    LONG m;
    while (in > (m = c->max_inside) && InterlockedCompareExchange(&c->max_inside, in, m) != m) {}
    if (c->entered) SetEvent(c->entered);
    LONG n = InterlockedIncrement(&c->count);
    DWORD now = GetTickCount();
    if (n == 1) c->t_first = now;
    c->t_last = now;
    c->id = id; c->msg = msg; c->user = user; c->dw1 = dw1; c->dw2 = dw2;
    if (msg != 0 || dw1 != 0 || dw2 != 0) InterlockedIncrement(&c->bad_args);
    c->tid = GetCurrentThreadId();
    c->prio = GetThreadPriority(GetCurrentThread());
    if (c->sleep_ms && (!c->sleep_once || n == 1)) Sleep(c->sleep_ms);
    InterlockedDecrement(&c->inside);
    InterlockedIncrement(&c->done);
    if (c->fired) SetEvent(c->fired);
}

void te_ctx_init(te_ctx *c) { memset(c, 0, sizeof *c); c->prio = 0x7fff; c->fired = CreateEventA(NULL, FALSE, FALSE, NULL); }
void te_ctx_free(te_ctx *c) {
    if (c->fired) CloseHandle(c->fired);
    if (c->entered) CloseHandle(c->entered);
    c->fired = c->entered = NULL;
}

LONG te_wait_count(te_ctx *c, LONG n, DWORD max_ms) {
    DWORD t0 = GetTickCount();
    while (c->count < n && GetTickCount() - t0 < max_ms) {
        if (c->fired) WaitForSingleObject(c->fired, 50); else Sleep(5);
    }
    return c->count;
}

UINT te_probe_id(void) {
    UINT id = te_set(TE_LONG, TE_COARSE, NULL, 0, TIME_ONESHOT);
    if (id) te_kill(id);
    return id;
}

int te_free_slots(void) {
    UINT ids[TE_SLOTS + 1]; int n = 0;
    for (int i = 0; i < TE_SLOTS + 1; i++) { ids[i] = te_set(TE_LONG, TE_COARSE, NULL, 0, TIME_ONESHOT); if (ids[i]) n++; }
    for (int i = 0; i < TE_SLOTS + 1; i++) if (ids[i]) te_kill(ids[i]);
    return n;
}

UINT te_pmin(void) {
    TIMECAPS tc; memset(&tc, 0, sizeof tc);
    if (U(timeGetDevCaps)(&tc, sizeof tc) != 0 || !tc.wPeriodMin) return 1;
    return tc.wPeriodMin;
}

UINT te_umax(void) {
    static UINT cached;
    if (cached) return cached;
    UINT pmin = te_pmin();
    for (UINT p = pmin; p < 200; p++) {
        if (U(timeEndPeriod)(p) != 0) continue;           /* counted range, nothing held */
        if (U(timeEndPeriod)(p) == 0) { cached = p; break; } /* no-op range: End always 0 */
        U(timeBeginPeriod)(p);                             /* a held reference: restore it */
    }
    if (!cached) cached = 55;
    return cached;
}

int te_period_held(UINT p) {
    if (U(timeEndPeriod)(p) == 0) { U(timeBeginPeriod)(p); return 1; }
    return 0;
}

unsigned long long te_held_mask(void) {
    unsigned long long m = 0; UINT pmin = te_pmin(), umax = te_umax();
    for (UINT p = pmin; p < umax && p - pmin < 64; p++) if (te_period_held(p)) m |= 1ull << (p - pmin);
    return m;
}

UINT te_expect_period(UINT delay, UINT res) {
    UINT pmin = te_pmin(), td = te_umax();
    UINT p = td;
    if (res <= td) { p = res; if (res < pmin) p = pmin; }
    if (delay < p) p = td;
    return p;
}

DWORD te_now(void) { return U(timeGetTime)(); }

DWORD te_thread_priority(DWORD tid) {
    HANDLE h = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, tid);
    if (!h) return 0x7fffffff;
    int p = GetThreadPriority(h);
    CloseHandle(h);
    return (DWORD)p;
}

/* ---------------------------------------------------------------- smoke */

TEST(te_exports_resolve) {
    REQUIRE(U(timeSetEvent));
    REQUIRE(U(timeKillEvent));
    CHECK(UIN("winmm.dll", timeSetEvent) != NULL);
    CHECK(UIN("winmm.dll", timeKillEvent) != NULL);
    /* Ordinals 143 / 142 name the same bodies. */
    EXPECT_EQ(t_proc_ord("winmm.dll", 143), (FARPROC)UIN("winmm.dll", timeSetEvent));
    EXPECT_EQ(t_proc_ord("winmm.dll", 142), (FARPROC)UIN("winmm.dll", timeKillEvent));
    /* The bodies live in winmm itself (not forwarders into another module). */
    HMODULE w = t_module("winmm.dll");
    MEMORY_BASIC_INFORMATION mbi;
    CHECK(VirtualQuery((void *)UIN("winmm.dll", timeSetEvent), &mbi, sizeof mbi) && mbi.AllocationBase == (void *)w);
    CHECK(VirtualQuery((void *)UIN("winmm.dll", timeKillEvent), &mbi, sizeof mbi) && mbi.AllocationBase == (void *)w);
}

TEST(te_smoke_oneshot_function) {
    TE_REQUIRE();
    static te_ctx c; te_ctx_init(&c);
    UINT id = te_set(30, 1, te_cb_ctx, (DWORD_PTR)&c, TIME_ONESHOT | TIME_CALLBACK_FUNCTION);
    CHECK(id != 0);
    if (id) {
        CHECK(WaitForSingleObject(c.fired, 2000) == WAIT_OBJECT_0);
        Sleep(50);
        EXPECT_EQ(c.count, 1);
        EXPECT_EQ(c.id, id);
        EXPECT_EQ(c.msg, 0);
        EXPECT_EQ(c.user, (DWORD_PTR)&c);
        EXPECT_EQ(c.dw1, 0);
        EXPECT_EQ(c.dw2, 0);
        EXPECT_EQ(te_kill(id), TE_NOCANDO);
    }
    te_ctx_free(&c);
}

TEST(te_smoke_periodic_function) {
    TE_REQUIRE();
    static te_ctx c; te_ctx_init(&c);
    UINT id = te_set(20, 1, te_cb_ctx, (DWORD_PTR)&c, TIME_PERIODIC | TIME_KILL_SYNCHRONOUS);
    CHECK(id != 0);
    if (id) {
        LONG n = te_wait_count(&c, 3, 2000);
        CHECKF(n >= 3, "count %ld", n);
        EXPECT_EQ(te_kill(id), 0);
        LONG after = c.count;
        Sleep(150);
        EXPECT_EQ(c.count, after);
        EXPECT_EQ(te_kill(id), TE_NOCANDO);
        EXPECT_EQ(c.bad_args, 0);
        EXPECT_EQ(c.id, id);
    }
    te_ctx_free(&c);
}

TEST(te_smoke_event_set) {
    TE_REQUIRE();
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    UINT id = te_set(30, 1, (LPTIMECALLBACK)ev, 0, TIME_ONESHOT | TIME_CALLBACK_EVENT_SET);
    CHECK(id != 0);
    if (id) {
        EXPECT_EQ(WaitForSingleObject(ev, 2000), WAIT_OBJECT_0);
        Sleep(30);
        EXPECT_EQ(te_kill(id), TE_NOCANDO);
    }
    CloseHandle(ev);
}

TEST(te_smoke_unknown_id) {
    TE_REQUIRE();
    EXPECT_EQ(te_kill(TE_UNKNOWN_ID), TE_NOCANDO);
    EXPECT_EQ(te_kill(0xFFFFFFFFu), TE_NOCANDO);
}

TEST(te_smoke_slots_all_free) {
    TE_REQUIRE();
    EXPECT_EQ(te_free_slots(), TE_SLOTS);
}

TEST(te_smoke_environment_notes) {
    TE_REQUIRE();
    REQUIRE(U(timeGetDevCaps));
    t_note("wPeriodMin=%u uMax/TimerData=%u", te_pmin(), te_umax());
    /* Decomp: TimerData = ceil(max timer resolution / 1 ms), 16 for the 15.625 ms tick; wPeriodMin normally 1. */
    CHECK(te_pmin() >= 1);
    CHECK(te_umax() > te_pmin());
    CHECK(te_umax() <= 55);
    EXPECT_EQ(te_held_mask(), 0);
}
