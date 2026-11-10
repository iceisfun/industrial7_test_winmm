/* time_events: TIME_CALLBACK_EVENT_SET and TIME_CALLBACK_EVENT_PULSE.
 * Decomp (winmm!timeThread 0x26c0): (fuEvent & 0xF0) == 0x10 -> SetEvent((HANDLE)fptc), == 0x20 ->
 * PulseEvent((HANDLE)fptc), both while holding the timer lock, in the same lock hold that frees a one-shot's slot or
 * re-arms a periodic one; so once a waiter wakes, a timeKillEvent of a one-shot is already 97, and once
 * timeKillEvent of a periodic one returns, the event is never signalled again by it.
 */
#include "te.h"

TEST(te_ev_set_oneshot_auto_reset) {
    TE_REQUIRE();
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    UINT id = te_set(30, 1, (LPTIMECALLBACK)ev, 0, TIME_ONESHOT | TIME_CALLBACK_EVENT_SET);
    CHECK(id != 0);
    EXPECT_EQ(WaitForSingleObject(ev, 2000), WAIT_OBJECT_0);
    EXPECT_EQ(te_kill(id), TE_NOCANDO);
    /* signalled exactly once */
    EXPECT_EQ(WaitForSingleObject(ev, 300), WAIT_TIMEOUT);
    CloseHandle(ev);
}

TEST(te_ev_set_oneshot_manual_reset_stays_signalled) {
    TE_REQUIRE();
    HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    UINT id = te_set(30, 1, (LPTIMECALLBACK)ev, 0, TIME_ONESHOT | TIME_CALLBACK_EVENT_SET);
    CHECK(id != 0);
    EXPECT_EQ(WaitForSingleObject(ev, 2000), WAIT_OBJECT_0);
    Sleep(50);
    EXPECT_EQ(WaitForSingleObject(ev, 0), WAIT_OBJECT_0);
    EXPECT_EQ(WaitForSingleObject(ev, 0), WAIT_OBJECT_0);
    EXPECT_EQ(te_kill(id), TE_NOCANDO);
    CloseHandle(ev);
}

TEST(te_ev_set_not_before_due) {
    TE_REQUIRE();
    HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    UINT id = te_set(400, 1, (LPTIMECALLBACK)ev, 0, TIME_ONESHOT | TIME_CALLBACK_EVENT_SET);
    CHECK(id != 0);
    EXPECT_EQ(WaitForSingleObject(ev, 150), WAIT_TIMEOUT);
    EXPECT_EQ(WaitForSingleObject(ev, 2000), WAIT_OBJECT_0);
    EXPECT_EQ(te_kill(id), TE_NOCANDO);
    CloseHandle(ev);
}

TEST(te_ev_set_periodic_repeats) {
    TE_REQUIRE();
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    UINT id = te_set(50, 1, (LPTIMECALLBACK)ev, 0, TIME_PERIODIC | TIME_CALLBACK_EVENT_SET);
    CHECK(id != 0);
    int n = 0; DWORD t0 = GetTickCount();
    while (GetTickCount() - t0 < 500) if (WaitForSingleObject(ev, 100) == WAIT_OBJECT_0) n++;
    CHECKF(n >= 3, "signals in 500 ms at 50 ms: %d", n);
    CHECKF(n <= 15, "signals in 500 ms at 50 ms: %d", n);
    EXPECT_EQ(te_kill(id), 0);
    ResetEvent(ev);
    EXPECT_EQ(WaitForSingleObject(ev, 200), WAIT_TIMEOUT);
    CloseHandle(ev);
}

TEST(te_ev_set_periodic_manual_stays_signalled) {
    TE_REQUIRE();
    HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    UINT id = te_set(20, 1, (LPTIMECALLBACK)ev, 0, TIME_PERIODIC | TIME_CALLBACK_EVENT_SET);
    CHECK(id != 0);
    EXPECT_EQ(WaitForSingleObject(ev, 2000), WAIT_OBJECT_0);
    for (int i = 0; i < 3; i++) {
        ResetEvent(ev);
        EXPECT_EQ(WaitForSingleObject(ev, 2000), WAIT_OBJECT_0);   /* signalled again by the next tick */
    }
    EXPECT_EQ(te_kill(id), 0);
    ResetEvent(ev);
    EXPECT_EQ(WaitForSingleObject(ev, 150), WAIT_TIMEOUT);
    CloseHandle(ev);
}

TEST(te_ev_set_kill_before_due_never_signals) {
    TE_REQUIRE();
    HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    UINT id = te_set(200, 1, (LPTIMECALLBACK)ev, 0, TIME_ONESHOT | TIME_CALLBACK_EVENT_SET);
    UINT idp = te_set(200, 1, (LPTIMECALLBACK)ev, 0, TIME_PERIODIC | TIME_CALLBACK_EVENT_SET);
    CHECK(id && idp);
    EXPECT_EQ(te_kill(id), 0);
    EXPECT_EQ(te_kill(idp), 0);
    EXPECT_EQ(WaitForSingleObject(ev, 450), WAIT_TIMEOUT);
    CloseHandle(ev);
}

TEST(te_ev_set_many_events_one_handle) {
    TE_REQUIRE();
    HANDLE sem_like = CreateEventA(NULL, FALSE, FALSE, NULL);
    UINT ids[8];
    for (int i = 0; i < 8; i++) { ids[i] = te_set(20 + 80 * i, 1, (LPTIMECALLBACK)sem_like, 0, TIME_ONESHOT | TIME_CALLBACK_EVENT_SET); CHECK(ids[i] != 0); }
    int n = 0;
    for (int i = 0; i < 8; i++) if (WaitForSingleObject(sem_like, 2000) == WAIT_OBJECT_0) n++;
    CHECKF(n >= 6, "woken %d of 8", n);   /* 80 ms apart: each one-shot wakes one wait (loose: wakeups can merge) */
    if (n != 8) t_note("auto-reset signals merged: %d of 8", n);
    for (int i = 0; i < 8; i++) if (ids[i]) EXPECT_EQ(te_kill(ids[i]), TE_NOCANDO);
    CloseHandle(sem_like);
}

TEST(te_ev_set_distinct_handles_each_signalled) {
    TE_REQUIRE();
    HANDLE ev[TE_SLOTS]; UINT ids[TE_SLOTS];
    for (int i = 0; i < TE_SLOTS; i++) {
        ev[i] = CreateEventA(NULL, TRUE, FALSE, NULL);
        ids[i] = te_set(10 + i * 3, 1, (LPTIMECALLBACK)ev[i], 0, TIME_ONESHOT | TIME_CALLBACK_EVENT_SET);
        CHECK(ids[i] != 0);
    }
    for (int i = 0; i < TE_SLOTS; i++) EXPECT_EQ(WaitForSingleObject(ev[i], 2000), WAIT_OBJECT_0);
    for (int i = 0; i < TE_SLOTS; i++) { if (ids[i]) EXPECT_EQ(te_kill(ids[i]), TE_NOCANDO); CloseHandle(ev[i]); }
}

TEST(te_ev_set_dwuser_ignored) {
    TE_REQUIRE();
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    UINT id = te_set(20, 1, (LPTIMECALLBACK)ev, (DWORD_PTR)0xFFFFFFFFFFFFFFFFull, TIME_ONESHOT | TIME_CALLBACK_EVENT_SET);
    CHECK(id != 0);
    EXPECT_EQ(WaitForSingleObject(ev, 2000), WAIT_OBJECT_0);
    CloseHandle(ev);
}

TEST(te_ev_set_kill_synchronous_flag_accepted) {
    TE_REQUIRE();
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    UINT id = te_set(10, 1, (LPTIMECALLBACK)ev, 0, TIME_PERIODIC | TIME_CALLBACK_EVENT_SET | TIME_KILL_SYNCHRONOUS);
    CHECK(id != 0);
    EXPECT_EQ(WaitForSingleObject(ev, 2000), WAIT_OBJECT_0);
    EXPECT_EQ(te_kill(id), 0);
    ResetEvent(ev);
    EXPECT_EQ(WaitForSingleObject(ev, 100), WAIT_TIMEOUT);
    CloseHandle(ev);
}

TEST(te_ev_set_handle_not_duplicated) {
    TE_REQUIRE();
    /* the event handle is used as given: after we kill the event and close our handle the object is gone
       (no extra handle kept by winmm, decomp stores fptc as-is) */
    HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    DWORD before = 0, during = 0;
    te_free_slots();   /* make sure the worker thread (and its handles) exist before sampling */
    GetProcessHandleCount(GetCurrentProcess(), &before);
    UINT id = te_set(TE_LONG, TE_COARSE, (LPTIMECALLBACK)ev, 0, TIME_ONESHOT | TIME_CALLBACK_EVENT_SET);
    CHECK(id != 0);
    GetProcessHandleCount(GetCurrentProcess(), &during);
    t_note("handle count before %lu during %lu", before, during);
    CHECKF(during <= before, "handle count grew %lu -> %lu (winmm must not duplicate the event handle)", before, during);
    if (id) EXPECT_EQ(te_kill(id), 0);
    CloseHandle(ev);
}

/* ---------------------------------------------------------------- PULSE */

typedef struct { HANDLE ev; volatile DWORD res; HANDLE ready; } te_waiter;
static DWORD WINAPI te_wait_thread(LPVOID p) {
    te_waiter *w = (te_waiter *)p;
    SetEvent(w->ready);
    w->res = WaitForSingleObject(w->ev, 2000);
    return 0;
}

TEST(te_ev_pulse_manual_wakes_waiter_and_resets) {
    TE_REQUIRE();
    static te_waiter w;
    w.ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    w.ready = CreateEventA(NULL, TRUE, FALSE, NULL);
    w.res = 0xFFFF;
    HANDLE th = CreateThread(NULL, 0, te_wait_thread, &w, 0, NULL);
    CHECK(th != NULL);
    WaitForSingleObject(w.ready, 2000);
    Sleep(50);   /* the waiter is blocked well before the tick */
    UINT id = te_set(150, 1, (LPTIMECALLBACK)w.ev, 0, TIME_ONESHOT | TIME_CALLBACK_EVENT_PULSE);
    CHECK(id != 0);
    if (th) { EXPECT_EQ(WaitForSingleObject(th, 3000), WAIT_OBJECT_0); CloseHandle(th); }
    EXPECT_EQ(w.res, WAIT_OBJECT_0);
    /* a pulse does not leave the event signalled */
    EXPECT_EQ(WaitForSingleObject(w.ev, 0), WAIT_TIMEOUT);
    EXPECT_EQ(te_kill(id), TE_NOCANDO);
    CloseHandle(w.ev); CloseHandle(w.ready);
}

TEST(te_ev_pulse_auto_wakes_waiter_and_resets) {
    TE_REQUIRE();
    static te_waiter w;
    w.ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    w.ready = CreateEventA(NULL, TRUE, FALSE, NULL);
    w.res = 0xFFFF;
    HANDLE th = CreateThread(NULL, 0, te_wait_thread, &w, 0, NULL);
    CHECK(th != NULL);
    WaitForSingleObject(w.ready, 2000);
    Sleep(50);
    UINT id = te_set(150, 1, (LPTIMECALLBACK)w.ev, 0, TIME_ONESHOT | TIME_CALLBACK_EVENT_PULSE);
    CHECK(id != 0);
    if (th) { EXPECT_EQ(WaitForSingleObject(th, 3000), WAIT_OBJECT_0); CloseHandle(th); }
    EXPECT_EQ(w.res, WAIT_OBJECT_0);
    EXPECT_EQ(WaitForSingleObject(w.ev, 0), WAIT_TIMEOUT);
    CloseHandle(w.ev); CloseHandle(w.ready);
}

TEST(te_ev_pulse_no_waiter_leaves_nonsignalled) {
    TE_REQUIRE();
    HANDLE m = CreateEventA(NULL, TRUE, FALSE, NULL), a = CreateEventA(NULL, FALSE, FALSE, NULL);
    UINT i1 = te_set(20, 1, (LPTIMECALLBACK)m, 0, TIME_ONESHOT | TIME_CALLBACK_EVENT_PULSE);
    UINT i2 = te_set(20, 1, (LPTIMECALLBACK)a, 0, TIME_ONESHOT | TIME_CALLBACK_EVENT_PULSE);
    CHECK(i1 && i2);
    Sleep(250);
    EXPECT_EQ(te_kill(i1), TE_NOCANDO);   /* fired */
    EXPECT_EQ(te_kill(i2), TE_NOCANDO);
    EXPECT_EQ(WaitForSingleObject(m, 0), WAIT_TIMEOUT);
    EXPECT_EQ(WaitForSingleObject(a, 0), WAIT_TIMEOUT);
    CloseHandle(m); CloseHandle(a);
}

TEST(te_ev_pulse_resets_already_signalled_manual_event) {
    TE_REQUIRE();
    /* PulseEvent on a signalled manual event releases nobody new and leaves it reset */
    HANDLE m = CreateEventA(NULL, TRUE, TRUE, NULL);
    UINT id = te_set(20, 1, (LPTIMECALLBACK)m, 0, TIME_ONESHOT | TIME_CALLBACK_EVENT_PULSE);
    CHECK(id != 0);
    Sleep(250);
    EXPECT_EQ(te_kill(id), TE_NOCANDO);
    EXPECT_EQ(WaitForSingleObject(m, 0), WAIT_TIMEOUT);
    CloseHandle(m);
}

TEST(te_ev_set_versus_pulse_contrast) {
    TE_REQUIRE();
    HANDLE s = CreateEventA(NULL, TRUE, FALSE, NULL), p = CreateEventA(NULL, TRUE, FALSE, NULL);
    UINT a = te_set(20, 1, (LPTIMECALLBACK)s, 0, TIME_ONESHOT | TIME_CALLBACK_EVENT_SET);
    UINT b = te_set(20, 1, (LPTIMECALLBACK)p, 0, TIME_ONESHOT | TIME_CALLBACK_EVENT_PULSE);
    CHECK(a && b);
    Sleep(250);
    EXPECT_EQ(WaitForSingleObject(s, 0), WAIT_OBJECT_0);
    EXPECT_EQ(WaitForSingleObject(p, 0), WAIT_TIMEOUT);
    CloseHandle(s); CloseHandle(p);
}

TEST(te_ev_pulse_manual_releases_all_waiters) {
    TE_REQUIRE();
    static te_waiter w[3];
    HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    HANDLE th[3];
    for (int i = 0; i < 3; i++) {
        w[i].ev = ev; w[i].res = 0xFFFF; w[i].ready = CreateEventA(NULL, TRUE, FALSE, NULL);
        th[i] = CreateThread(NULL, 0, te_wait_thread, &w[i], 0, NULL);
        CHECK(th[i] != NULL);
        WaitForSingleObject(w[i].ready, 2000);
    }
    Sleep(60);
    UINT id = te_set(150, 1, (LPTIMECALLBACK)ev, 0, TIME_ONESHOT | TIME_CALLBACK_EVENT_PULSE);
    CHECK(id != 0);
    for (int i = 0; i < 3; i++) {
        if (th[i]) { EXPECT_EQ(WaitForSingleObject(th[i], 3000), WAIT_OBJECT_0); CloseHandle(th[i]); }
        EXPECT_EQ(w[i].res, WAIT_OBJECT_0);
        CloseHandle(w[i].ready);
    }
    EXPECT_EQ(WaitForSingleObject(ev, 0), WAIT_TIMEOUT);
    CloseHandle(ev);
}

static volatile LONG te_pw_count, te_pw_stop;
static HANDLE te_pw_ev;
static DWORD WINAPI te_pulse_counter(LPVOID p) {
    while (!te_pw_stop) if (WaitForSingleObject(te_pw_ev, 100) == WAIT_OBJECT_0) InterlockedIncrement(&te_pw_count);
    return 0;
}

TEST(te_ev_pulse_periodic_wakes_repeatedly) {
    TE_REQUIRE();
    te_pw_ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    te_pw_count = 0; te_pw_stop = 0;
    HANDLE th = CreateThread(NULL, 0, te_pulse_counter, NULL, 0, NULL);
    CHECK(th != NULL);
    Sleep(50);
    UINT id = te_set(50, 1, (LPTIMECALLBACK)te_pw_ev, 0, TIME_PERIODIC | TIME_CALLBACK_EVENT_PULSE);
    CHECK(id != 0);
    Sleep(600);
    if (id) EXPECT_EQ(te_kill(id), 0);
    te_pw_stop = 1;
    if (th) { WaitForSingleObject(th, 2000); CloseHandle(th); }
    CHECKF(te_pw_count >= 3, "woken %ld times in 600 ms at 50 ms", te_pw_count);
    CHECKF(te_pw_count <= 15, "woken %ld times", te_pw_count);
    EXPECT_EQ(WaitForSingleObject(te_pw_ev, 0), WAIT_TIMEOUT);
    CloseHandle(te_pw_ev);
}

TEST(te_ev_kill_from_event_waiter_thread) {
    TE_REQUIRE();
    /* typical consumer loop: wait on the timer event, kill the timer from the same thread after N ticks */
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    UINT id = te_set(15, 1, (LPTIMECALLBACK)ev, 0, TIME_PERIODIC | TIME_CALLBACK_EVENT_SET);
    CHECK(id != 0);
    int n = 0;
    for (int i = 0; i < 10; i++) if (WaitForSingleObject(ev, 1000) == WAIT_OBJECT_0) n++;
    EXPECT_EQ(n, 10);
    EXPECT_EQ(te_kill(id), 0);
    CloseHandle(ev);
}

TEST(te_events_no_leak_after_file) {
    TE_REQUIRE();
    EXPECT_EQ(te_free_slots(), TE_SLOTS);
    EXPECT_EQ(te_held_mask(), 0);
}
