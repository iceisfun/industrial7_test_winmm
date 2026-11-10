/* time_event_lifetime: timeKillEvent semantics while callbacks run.
 * timeKillEvent (winmm 0x2e50) clears the slot and cancels the NT timer under the timer lock, then
 * waits on the "callback finished" event only if: a callback is running, it belongs to uTimerID,
 * the caller is not the timer thread, and the event was created with TIME_KILL_SYNCHRONOUS (0x100).
 * timeThread (0x26c0) runs callbacks outside the lock and, after a callback, re-arms/frees only if the
 * slot still holds the same id, so a kill during or inside a callback ends the event for good.
 */
#include "tl.h"

/* ---- a callback that blocks for a while ---------------------------------------------------- */
typedef struct {
    HANDLE entered;            /* set when the callback starts */
    volatile LONG calls;
    volatile LONG done;        /* set to 1 right before the callback returns */
    DWORD sleep_ms;
    UINT seen_id;
} kl_block;
static void CALLBACK kl_block_cb(UINT id, UINT msg, DWORD_PTR user, DWORD_PTR d1, DWORD_PTR d2) {
    kl_block *b = (kl_block *)user;
    InterlockedIncrement(&b->calls);
    b->seen_id = id;
    InterlockedExchange(&b->done, 0);
    SetEvent(b->entered);
    Sleep(b->sleep_ms);
    InterlockedExchange(&b->done, 1);
}

/* kill from the main thread while the callback runs */
static void kl_sync_case(UINT flags, DWORD sleep_ms, UINT delay) {
    kl_block b; memset(&b, 0, sizeof b);
    b.entered = CreateEventA(NULL, TRUE, FALSE, NULL); b.sleep_ms = sleep_ms;
    UINT id = TSE(delay, 1, kl_block_cb, (DWORD_PTR)&b, flags);
    CHECKF(id != 0, "flags 0x%x", flags);
    if (!id) { CloseHandle(b.entered); return; }
    DWORD w = WaitForSingleObject(b.entered, 3000);
    EXPECT_EQ(w, WAIT_OBJECT_0);
    LONG done_before = b.done;
    DWORD t0 = GetTickCount();
    MMRESULT r = TKE(id);
    DWORD dt = GetTickCount() - t0;
    LONG done_at_return = b.done;
    if (flags & TIME_KILL_SYNCHRONOUS) {
        EXPECT_EQ(r, 0);
        CHECKF(done_before == 0, "callback finished before the kill was issued (sleep %lu)", sleep_ms);
        CHECKF(done_at_return == 1, "synchronous kill returned while the callback was still running (flags 0x%x, %lu ms)", flags, dt);
    } else {
        /* one-shot without SYNCHRONOUS: the slot is still occupied while the callback runs -> kill 0 */
        EXPECT_EQ(r, 0);
        CHECKF(done_at_return == 0, "non-synchronous kill waited for the callback (flags 0x%x, %lu ms)", flags, dt);
        t_note("non-synchronous kill flags 0x%x returned after %lu ms, callback done=%ld", flags, dt, done_at_return);
    }
    /* wait for the callback to finish in any case, then nothing else happens */
    tl_wait_ge(&b.done, 1, 3000);
    LONG c = b.calls;
    Sleep(delay * 2 + 100 < 400 ? delay * 2 + 100 : 400);
    EXPECT_EQ(b.calls, c);
    EXPECT_EQ(c, 1);
    EXPECT_EQ(b.seen_id, id);
    EXPECT_EQ(TKE(id), TL_NOCANDO);
    CloseHandle(b.entered);
}
TEST(tl_kill_sync_periodic_waits) { TL_REQUIRE(); kl_sync_case(TIME_PERIODIC | TIME_KILL_SYNCHRONOUS, 200, 10); }
TEST(tl_kill_sync_oneshot_waits) { TL_REQUIRE(); kl_sync_case(TIME_ONESHOT | TIME_KILL_SYNCHRONOUS, 200, 10); }
TEST(tl_kill_sync_periodic_waits_short) { TL_REQUIRE(); kl_sync_case(TIME_PERIODIC | TIME_KILL_SYNCHRONOUS, 300, 5); }
TEST(tl_kill_sync_periodic_waits_1ms) { TL_REQUIRE(); kl_sync_case(TIME_PERIODIC | TIME_KILL_SYNCHRONOUS, 300, 1); }
TEST(tl_kill_sync_oneshot_waits_long_delay) { TL_REQUIRE(); kl_sync_case(TIME_ONESHOT | TIME_KILL_SYNCHRONOUS, 120, 40); }
TEST(tl_kill_async_periodic_returns) { TL_REQUIRE(); kl_sync_case(TIME_PERIODIC, 200, 10); }
TEST(tl_kill_async_oneshot_returns) { TL_REQUIRE(); kl_sync_case(TIME_ONESHOT, 200, 10); }

/* Without SYNCHRONOUS the kill does not wait (decomp: no wait without bit 8). Measured, not timed hard:
 * the callback sleeps 300 ms, the kill must come back while it still sleeps. */
TEST(tl_kill_async_does_not_wait_for_callback) {
    TL_REQUIRE();
    kl_block b; memset(&b, 0, sizeof b);
    b.entered = CreateEventA(NULL, TRUE, FALSE, NULL); b.sleep_ms = 300;
    UINT id = TSE(5, 1, kl_block_cb, (DWORD_PTR)&b, TIME_PERIODIC);
    REQUIRE(id);
    EXPECT_EQ(WaitForSingleObject(b.entered, 3000), WAIT_OBJECT_0);
    DWORD t0 = GetTickCount();
    EXPECT_EQ(TKE(id), 0);
    DWORD dt = GetTickCount() - t0;
    LONG d = b.done;
    CHECKF(d == 0, "async kill waited for the running callback (%lu ms)", dt);
    t_note("async kill took %lu ms, callback done=%ld", dt, d);
    tl_wait_ge(&b.done, 1, 3000);
    Sleep(50);
    EXPECT_EQ(b.calls, 1);
    CloseHandle(b.entered);
}

/* A synchronous kill of event A while event B's callback runs does not wait (id differs). */
TEST(tl_kill_sync_other_event_does_not_wait_for_running_callback) {
    TL_REQUIRE();
    kl_block b; memset(&b, 0, sizeof b);
    b.entered = CreateEventA(NULL, TRUE, FALSE, NULL); b.sleep_ms = 300;
    UINT a = TSE(10000, 1, kl_block_cb, (DWORD_PTR)&b, TIME_PERIODIC | TIME_KILL_SYNCHRONOUS);
    UINT bb = TSE(5, 1, kl_block_cb, (DWORD_PTR)&b, TIME_ONESHOT | TIME_KILL_SYNCHRONOUS);
    CHECK(a && bb);
    EXPECT_EQ(WaitForSingleObject(b.entered, 3000), WAIT_OBJECT_0);
    EXPECT_EQ(TKE(a), 0);
    LONG d = b.done;
    CHECKF(d == 0, "killing a different SYNCHRONOUS event waited for the running callback");
    tl_wait_ge(&b.done, 1, 3000);
    Sleep(30);
    EXPECT_EQ(b.calls, 1);
    EXPECT_EQ(TKE(bb), TL_NOCANDO);   /* one-shot completed */
    CloseHandle(b.entered);
}

/* A synchronous kill of an event that is not running returns at once with 0 */
static void CALLBACK kl_nop(UINT id, UINT msg, DWORD_PTR user, DWORD_PTR d1, DWORD_PTR d2) { }
TEST(tl_kill_sync_idle_event) {
    TL_REQUIRE();
    static const UINT fl[] = { TIME_PERIODIC | TIME_KILL_SYNCHRONOUS, TIME_ONESHOT | TIME_KILL_SYNCHRONOUS,
        TIME_PERIODIC, TIME_ONESHOT };
    for (int i = 0; i < 4; i++) {
        UINT id = TSE(10000, 0, kl_nop, 0, fl[i]);
        CHECKF(id != 0, "flags 0x%x", fl[i]);
        EXPECT_EQ(TKE(id), 0);
        EXPECT_EQ(TKE(id), TL_NOCANDO);
        EXPECT_EQ(TKE(id), TL_NOCANDO);
    }
}

/* ---- kill from inside the callback ---------------------------------------------------------- */
typedef struct {
    volatile LONG calls;
    volatile LONG kill_r1, kill_r2;
    UINT self;                   /* set by the test after timeSetEvent returns; callback uses the id arg */
    UINT other;                  /* another event to kill from the callback, or 0 */
    volatile LONG kill_other_r;
    HANDLE fired;
    int kill_twice;
} kl_self;
static void CALLBACK kl_self_cb(UINT id, UINT msg, DWORD_PTR user, DWORD_PTR d1, DWORD_PTR d2) {
    kl_self *s = (kl_self *)user;
    LONG n = InterlockedIncrement(&s->calls);
    if (n == 1) {
        s->kill_r1 = (LONG)U(timeKillEvent)(id);
        if (s->kill_twice) s->kill_r2 = (LONG)U(timeKillEvent)(id);
        if (s->other) s->kill_other_r = (LONG)U(timeKillEvent)(s->other);
        SetEvent(s->fired);
    }
}
static void kl_self_case(UINT flags, UINT delay, int twice) {
    kl_self s; memset(&s, 0, sizeof s);
    s.kill_r1 = s.kill_r2 = -1; s.kill_twice = twice;
    s.fired = CreateEventA(NULL, TRUE, FALSE, NULL);
    UINT id = TSE(delay, 1, kl_self_cb, (DWORD_PTR)&s, flags);
    CHECKF(id != 0, "flags 0x%x delay %u", flags, delay);
    if (!id) { CloseHandle(s.fired); return; }
    EXPECT_EQ(WaitForSingleObject(s.fired, 3000), WAIT_OBJECT_0);   /* no deadlock */
    EXPECT_EQ(s.kill_r1, 0);
    if (twice) EXPECT_EQ(s.kill_r2, TL_NOCANDO);
    Sleep(200);
    EXPECT_EQ(s.calls, 1);                                             /* no further callbacks */
    EXPECT_EQ(TKE(id), TL_NOCANDO);
    CloseHandle(s.fired);
}
TEST(tl_kill_self_periodic) { TL_REQUIRE(); kl_self_case(TIME_PERIODIC, 10, 0); }
TEST(tl_kill_self_periodic_sync) { TL_REQUIRE(); kl_self_case(TIME_PERIODIC | TIME_KILL_SYNCHRONOUS, 10, 0); }
TEST(tl_kill_self_periodic_1ms) { TL_REQUIRE(); kl_self_case(TIME_PERIODIC, 1, 0); }
TEST(tl_kill_self_periodic_sync_1ms) { TL_REQUIRE(); kl_self_case(TIME_PERIODIC | TIME_KILL_SYNCHRONOUS, 1, 0); }
TEST(tl_kill_self_oneshot) { TL_REQUIRE(); kl_self_case(TIME_ONESHOT, 10, 0); }
TEST(tl_kill_self_oneshot_sync) { TL_REQUIRE(); kl_self_case(TIME_ONESHOT | TIME_KILL_SYNCHRONOUS, 10, 0); }
TEST(tl_kill_self_twice_periodic) { TL_REQUIRE(); kl_self_case(TIME_PERIODIC, 10, 1); }
TEST(tl_kill_self_twice_oneshot_sync) { TL_REQUIRE(); kl_self_case(TIME_ONESHOT | TIME_KILL_SYNCHRONOUS, 10, 1); }
TEST(tl_kill_self_twice_periodic_sync) { TL_REQUIRE(); kl_self_case(TIME_PERIODIC | TIME_KILL_SYNCHRONOUS, 3, 1); }

/* Killing itself inside a one-shot releases the period exactly once: a reference of our own on the
 * same period survives (a double release would consume it). */
TEST(tl_kill_self_oneshot_releases_period_once) {
    TL_REQUIRE();
    UINT p = tl_pmin() > 2 ? tl_pmin() : 2;
    if (!tl_counted(p)) { t_skip("period %u not counted", p); return; }
    if (tl_refs(p, 8) != 0) { t_skip("process already holds period %u", p); return; }
    EXPECT_EQ(TBP(p), 0);
    for (int sync = 0; sync < 2; sync++) {
        kl_self s; memset(&s, 0, sizeof s); s.fired = CreateEventA(NULL, TRUE, FALSE, NULL);
        UINT id = TSE(20, p, kl_self_cb, (DWORD_PTR)&s, TIME_ONESHOT | (sync ? TIME_KILL_SYNCHRONOUS : 0));
        CHECK(id != 0);
        EXPECT_EQ(WaitForSingleObject(s.fired, 3000), WAIT_OBJECT_0);
        Sleep(100);
        EXPECT_EQ(s.kill_r1, 0);
        EXPECT_EQ(tl_refs(p, 8), 1);    /* only ours left */
        CloseHandle(s.fired);
    }
    EXPECT_EQ(TEP(p), 0);
    EXPECT_EQ(TEP(p), TL_NOCANDO);
}

/* A callback kills a different live event (no wait from the timer thread even with SYNCHRONOUS). */
static void kl_other_case(UINT other_flags) {
    kl_self s; memset(&s, 0, sizeof s);
    s.fired = CreateEventA(NULL, TRUE, FALSE, NULL);
    UINT other = TSE(10000, 1, kl_nop, 0, other_flags);
    CHECK(other != 0);
    s.other = other; s.kill_other_r = -1;
    UINT id = TSE(10, 1, kl_self_cb, (DWORD_PTR)&s, TIME_PERIODIC | TIME_KILL_SYNCHRONOUS);
    CHECK(id != 0);
    EXPECT_EQ(WaitForSingleObject(s.fired, 3000), WAIT_OBJECT_0);
    EXPECT_EQ(s.kill_r1, 0);
    EXPECT_EQ(s.kill_other_r, 0);
    EXPECT_EQ(TKE(other), TL_NOCANDO);
    EXPECT_EQ(TKE(id), TL_NOCANDO);
    CloseHandle(s.fired);
}
TEST(tl_kill_other_from_callback_periodic) { TL_REQUIRE(); kl_other_case(TIME_PERIODIC); }
TEST(tl_kill_other_from_callback_periodic_sync) { TL_REQUIRE(); kl_other_case(TIME_PERIODIC | TIME_KILL_SYNCHRONOUS); }
TEST(tl_kill_other_from_callback_oneshot) { TL_REQUIRE(); kl_other_case(TIME_ONESHOT); }
TEST(tl_kill_other_from_callback_oneshot_sync) { TL_REQUIRE(); kl_other_case(TIME_ONESHOT | TIME_KILL_SYNCHRONOUS); }

/* A callback that kills a stale / unknown id gets 97 and keeps running. */
typedef struct { volatile LONG calls; volatile LONG r[3]; HANDLE fired; UINT stale; } kl_bad;
static void CALLBACK kl_bad_cb(UINT id, UINT msg, DWORD_PTR user, DWORD_PTR d1, DWORD_PTR d2) {
    kl_bad *s = (kl_bad *)user;
    if (InterlockedIncrement(&s->calls) == 1) {
        s->r[0] = (LONG)U(timeKillEvent)(0x12345);
        s->r[1] = (LONG)U(timeKillEvent)(s->stale);
        s->r[2] = (LONG)U(timeKillEvent)(id ^ 0x10000);
        SetEvent(s->fired);
    }
}
TEST(tl_kill_unknown_from_callback) {
    TL_REQUIRE();
    kl_bad s; memset(&s, 0, sizeof s); s.fired = CreateEventA(NULL, TRUE, FALSE, NULL);
    UINT st = TSE(10000, 0, kl_nop, 0, TIME_PERIODIC); CHECK(st); EXPECT_EQ(TKE(st), 0);
    s.stale = st;
    UINT id = TSE(5, 1, kl_bad_cb, (DWORD_PTR)&s, TIME_PERIODIC);
    REQUIRE(id);
    EXPECT_EQ(WaitForSingleObject(s.fired, 3000), WAIT_OBJECT_0);
    EXPECT_EQ(s.r[0], TL_NOCANDO); EXPECT_EQ(s.r[1], TL_NOCANDO); EXPECT_EQ(s.r[2], TL_NOCANDO);
    CHECK(tl_wait_ge(&s.calls, 3, 3000));     /* still periodic */
    EXPECT_EQ(TKE(id), 0);
    LONG c = s.calls; Sleep(100); CHECK(s.calls - c <= 1);
    EXPECT_EQ(TKE(id), TL_NOCANDO);
    CloseHandle(s.fired);
}

/* ---- concurrent kills of one id: exactly one winner ----------------------------------------- */
typedef struct { UINT id; HANDLE go; volatile LONG r; } kl_race;
static DWORD WINAPI kl_race_thread(LPVOID p) {
    kl_race *k = (kl_race *)p; WaitForSingleObject(k->go, 5000); k->r = (LONG)U(timeKillEvent)(k->id); return 0;
}
static void kl_race_case(UINT flags, int nthreads) {
    HANDLE go = CreateEventA(NULL, TRUE, FALSE, NULL);
    UINT id = TSE(flags & TIME_PERIODIC ? 3 : 10000, 1, kl_nop, 0, flags);
    CHECK(id != 0);
    kl_race k[8]; HANDLE th[8];
    for (int i = 0; i < nthreads; i++) { k[i].id = id; k[i].go = go; k[i].r = -1; th[i] = CreateThread(NULL, 0, kl_race_thread, &k[i], 0, NULL); }
    Sleep(20); SetEvent(go);
    WaitForMultipleObjects(nthreads, th, TRUE, 5000);
    int zero = 0, nocando = 0;
    for (int i = 0; i < nthreads; i++) { if (k[i].r == 0) zero++; else if (k[i].r == (LONG)TL_NOCANDO) nocando++; CloseHandle(th[i]); }
    EXPECT_EQ(zero, 1);
    EXPECT_EQ(nocando, nthreads - 1);
    EXPECT_EQ(TKE(id), TL_NOCANDO);
    CloseHandle(go);
}
TEST(tl_kill_race_periodic_2) { TL_REQUIRE(); kl_race_case(TIME_PERIODIC, 2); }
TEST(tl_kill_race_periodic_8) { TL_REQUIRE(); kl_race_case(TIME_PERIODIC, 8); }
TEST(tl_kill_race_periodic_sync_8) { TL_REQUIRE(); kl_race_case(TIME_PERIODIC | TIME_KILL_SYNCHRONOUS, 8); }
TEST(tl_kill_race_oneshot_long_4) { TL_REQUIRE(); kl_race_case(TIME_ONESHOT, 4); }
TEST(tl_kill_race_oneshot_long_sync_8) { TL_REQUIRE(); kl_race_case(TIME_ONESHOT | TIME_KILL_SYNCHRONOUS, 8); }

/* ---- kill vs fired one-shot ----------------------------------------------------------------- */
TEST(tl_kill_after_oneshot_fired_table) {
    TL_REQUIRE();
    static const UINT fl[] = { TIME_ONESHOT, TIME_ONESHOT | TIME_KILL_SYNCHRONOUS,
        TIME_ONESHOT | TIME_CALLBACK_EVENT_SET, TIME_ONESHOT | TIME_CALLBACK_EVENT_SET | TIME_KILL_SYNCHRONOUS };
    for (int i = 0; i < 4; i++) {
        HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
        kl_self s; memset(&s, 0, sizeof s); s.fired = ev;
        /* function variant: a callback that only signals (calls==1 path kills itself: avoid) -> use event */
        UINT id = TSE(10, 1, (fl[i] & TIME_CALLBACK_EVENT_SET) ? (LPTIMECALLBACK)ev : kl_nop, 0, fl[i]);
        CHECKF(id != 0, "flags 0x%x", fl[i]);
        if (fl[i] & TIME_CALLBACK_EVENT_SET) EXPECT_EQ(WaitForSingleObject(ev, 3000), WAIT_OBJECT_0);
        Sleep(150);
        EXPECT_EQ(TKE(id), TL_NOCANDO);
        EXPECT_EQ(TKE(id), TL_NOCANDO);
        CloseHandle(ev);
    }
}

/* Killing from a different thread than the creator works (no thread ownership). */
typedef struct { UINT id; LONG r; } kl_xt;
static DWORD WINAPI kl_kill_thread(LPVOID p) { kl_xt *x = (kl_xt *)p; x->r = (LONG)U(timeKillEvent)(x->id); return 0; }
static DWORD WINAPI kl_set_thread(LPVOID p) { kl_xt *x = (kl_xt *)p; x->id = U(timeSetEvent)(10000, 0, kl_nop, 0, TIME_PERIODIC); return 0; }
TEST(tl_kill_from_other_thread) {
    TL_REQUIRE();
    kl_xt x = { 0, -1 };
    x.id = TSE(10000, 0, kl_nop, 0, TIME_PERIODIC | TIME_KILL_SYNCHRONOUS);
    CHECK(x.id);
    HANDLE t = CreateThread(NULL, 0, kl_kill_thread, &x, 0, NULL);
    EXPECT_EQ(WaitForSingleObject(t, 5000), WAIT_OBJECT_0); CloseHandle(t);
    EXPECT_EQ(x.r, 0);
    EXPECT_EQ(TKE(x.id), TL_NOCANDO);
}
TEST(tl_kill_event_created_by_exited_thread) {
    TL_REQUIRE();
    kl_xt x = { 0, -1 };
    HANDLE t = CreateThread(NULL, 0, kl_set_thread, &x, 0, NULL);
    EXPECT_EQ(WaitForSingleObject(t, 5000), WAIT_OBJECT_0); CloseHandle(t);
    CHECK(x.id != 0);
    EXPECT_EQ(TKE(x.id), 0);
    EXPECT_EQ(TKE(x.id), TL_NOCANDO);
}
