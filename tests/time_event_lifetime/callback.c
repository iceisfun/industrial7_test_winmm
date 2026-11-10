/* time_event_lifetime: what a callback may do and what the worker does with each callback kind.
 * timeThread (winmm 0x26c0) dispatches on fuEvent & 0xF0: 0x00 -> fptc(id, 0, dwUser, 0, 0) when fptc is
 * not NULL; 0x10 -> SetEvent(fptc); 0x20 -> PulseEvent(fptc); any other value (0x30..0xF0, accepted by
 * timeSetEvent because bits 6 and 7 are valid) -> nothing is called, but the event still lives: a
 * one-shot frees its slot, a periodic one keeps re-arming. Callbacks run outside the timer lock, so they
 * may call timeSetEvent / timeKillEvent / timeGetTime.
 */
#include "tl.h"

/* ---- arguments --------------------------------------------------------------------------- */
typedef struct { volatile LONG calls; UINT id; UINT msg; DWORD_PTR user, d1, d2; HANDLE ev; } cb_args;
static void CALLBACK cb_args_cb(UINT id, UINT msg, DWORD_PTR user, DWORD_PTR d1, DWORD_PTR d2) {
    cb_args *a = (cb_args *)(user & ~(DWORD_PTR)0); /* user is the struct pointer */
    if (InterlockedIncrement(&a->calls) == 1) { a->id = id; a->msg = msg; a->user = user; a->d1 = d1; a->d2 = d2; SetEvent(a->ev); }
}
static void cb_args_case(UINT delay, UINT res, UINT flags) {
    cb_args a; memset(&a, 0, sizeof a); a.ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    a.d1 = a.d2 = 0xCCCC; a.msg = 0xCCCC;
    UINT id = TSE(delay, res, cb_args_cb, (DWORD_PTR)&a, flags);
    REQUIRE(id);
    EXPECT_EQ(WaitForSingleObject(a.ev, 3000), WAIT_OBJECT_0);
    if (flags & TIME_PERIODIC) EXPECT_EQ(TKE(id), 0);
    Sleep(30);
    EXPECT_EQ(a.id, id);
    EXPECT_EQ(a.msg, 0);
    EXPECT_EQ(a.user, (DWORD_PTR)&a);
    EXPECT_EQ(a.d1, 0);
    EXPECT_EQ(a.d2, 0);
    if (!(flags & TIME_PERIODIC)) { EXPECT_EQ(a.calls, 1); EXPECT_EQ(TKE(id), TL_NOCANDO); }
    CloseHandle(a.ev);
}
TEST(tl_cb_args_oneshot) { TL_REQUIRE(); cb_args_case(10, 1, TIME_ONESHOT); }
TEST(tl_cb_args_oneshot_sync) { TL_REQUIRE(); cb_args_case(10, 0, TIME_ONESHOT | TIME_KILL_SYNCHRONOUS); }
TEST(tl_cb_args_periodic) { TL_REQUIRE(); cb_args_case(5, 1, TIME_PERIODIC); }
TEST(tl_cb_args_periodic_sync) { TL_REQUIRE(); cb_args_case(5, 5, TIME_PERIODIC | TIME_KILL_SYNCHRONOUS); }
TEST(tl_cb_args_oneshot_res_big) { TL_REQUIRE(); cb_args_case(30, 1000, TIME_ONESHOT); }

/* a full 64-bit dwUser is passed through unchanged */
static volatile DWORD_PTR cb_user_seen;
static void CALLBACK cb_user_cb(UINT id, UINT msg, DWORD_PTR user, DWORD_PTR d1, DWORD_PTR d2) { cb_user_seen = user; }
TEST(tl_cb_user_64bit_values) {
    TL_REQUIRE();
    static const DWORD_PTR vals[] = { 0, 1, 0xFFFFFFFF, (DWORD_PTR)0x100000000ULL, (DWORD_PTR)0xDEADBEEFCAFEF00DULL, (DWORD_PTR)-1 };
    for (int i = 0; i < 6; i++) {
        cb_user_seen = 0x5A5A;
        UINT id = TSE(5, 1, cb_user_cb, vals[i], TIME_ONESHOT);
        CHECK(id != 0);
        DWORD t0 = GetTickCount();
        while (cb_user_seen == 0x5A5A && GetTickCount() - t0 < 3000) Sleep(2);
        EXPECT_EQ(cb_user_seen, vals[i]);
        Sleep(20);
        EXPECT_EQ(TKE(id), TL_NOCANDO);
    }
}

/* ---- callback kinds by fuEvent & 0xF0 ----------------------------------------------------- */
static volatile LONG cb_kind_hits;
static void CALLBACK cb_kind_cb(UINT id, UINT msg, DWORD_PTR user, DWORD_PTR d1, DWORD_PTR d2) { InterlockedIncrement(&cb_kind_hits); }
/* kinds 0x30..0xF0 call nothing: pass a counting function, it is never invoked */
static void cb_silent_kind(UINT kind, UINT extra) {
    cb_kind_hits = 0;
    UINT os = TSE(10, 1, cb_kind_cb, 0, kind | extra);
    CHECKF(os != 0, "one-shot kind 0x%x extra 0x%x", kind, extra);
    UINT pe = TSE(5, 1, cb_kind_cb, 0, kind | extra | TIME_PERIODIC);
    CHECKF(pe != 0, "periodic kind 0x%x extra 0x%x", kind, extra);
    Sleep(150);
    EXPECT_EQ(cb_kind_hits, 0);
    /* the one-shot fired (silently) and freed its slot; the periodic is still live */
    MMRESULT r = TL_NOCANDO + 1; for (int i = 0; i < 50; i++) { r = TKE(os); if (r == TL_NOCANDO) break; Sleep(10); }
    CHECKF(r == TL_NOCANDO, "kind 0x%x one-shot still live: %u", kind, r);
    EXPECT_EQ(TKE(pe), 0);
    EXPECT_EQ(TKE(pe), TL_NOCANDO);
}
TEST(tl_cb_kind_0x30_silent) { TL_REQUIRE(); cb_silent_kind(0x30, 0); }
TEST(tl_cb_kind_0x40_silent) { TL_REQUIRE(); cb_silent_kind(0x40, 0); }
TEST(tl_cb_kind_0x50_silent) { TL_REQUIRE(); cb_silent_kind(0x50, 0); }
TEST(tl_cb_kind_0x60_silent) { TL_REQUIRE(); cb_silent_kind(0x60, 0); }
TEST(tl_cb_kind_0x70_silent) { TL_REQUIRE(); cb_silent_kind(0x70, 0); }
TEST(tl_cb_kind_0x80_silent) { TL_REQUIRE(); cb_silent_kind(0x80, 0); }
TEST(tl_cb_kind_0x90_silent) { TL_REQUIRE(); cb_silent_kind(0x90, 0); }
TEST(tl_cb_kind_0xa0_silent) { TL_REQUIRE(); cb_silent_kind(0xA0, 0); }
TEST(tl_cb_kind_0xb0_silent) { TL_REQUIRE(); cb_silent_kind(0xB0, 0); }
TEST(tl_cb_kind_0xc0_silent) { TL_REQUIRE(); cb_silent_kind(0xC0, 0); }
TEST(tl_cb_kind_0xd0_silent) { TL_REQUIRE(); cb_silent_kind(0xD0, 0); }
TEST(tl_cb_kind_0xe0_silent) { TL_REQUIRE(); cb_silent_kind(0xE0, 0); }
TEST(tl_cb_kind_0xf0_silent) { TL_REQUIRE(); cb_silent_kind(0xF0, 0); }
TEST(tl_cb_kind_0x40_sync_silent) { TL_REQUIRE(); cb_silent_kind(0x40, TIME_KILL_SYNCHRONOUS); }
TEST(tl_cb_kind_0xc0_sync_silent) { TL_REQUIRE(); cb_silent_kind(0xC0, TIME_KILL_SYNCHRONOUS); }

/* kind 0x00 with a NULL function: nothing called; one-shot frees, periodic stays live */
TEST(tl_cb_null_function) {
    TL_REQUIRE();
    static const UINT ex[] = { 0, TIME_KILL_SYNCHRONOUS };
    for (int i = 0; i < 2; i++) {
        UINT os = TSE(10, 1, NULL, 0, TIME_ONESHOT | ex[i]);
        UINT pe = TSE(5, 1, NULL, 0, TIME_PERIODIC | ex[i]);
        CHECK(os != 0); CHECK(pe != 0);
        Sleep(120);
        MMRESULT r = 0; for (int k = 0; k < 50; k++) { r = TKE(os); if (r == TL_NOCANDO) break; Sleep(10); }
        EXPECT_EQ(r, TL_NOCANDO);
        EXPECT_EQ(TKE(pe), 0);
        EXPECT_EQ(TKE(pe), TL_NOCANDO);
    }
}

/* EVENT_SET: one-shot signals once, periodic keeps signalling; manual-reset events stay signalled */
TEST(tl_cb_eventset_auto_and_manual) {
    TL_REQUIRE();
    HANDLE au = CreateEventA(NULL, FALSE, FALSE, NULL), ma = CreateEventA(NULL, TRUE, FALSE, NULL);
    UINT a = TSE(10, 1, (LPTIMECALLBACK)au, 0, TIME_ONESHOT | TIME_CALLBACK_EVENT_SET);
    UINT m = TSE(10, 1, (LPTIMECALLBACK)ma, 0, TIME_ONESHOT | TIME_CALLBACK_EVENT_SET);
    CHECK(a && m);
    EXPECT_EQ(WaitForSingleObject(au, 3000), WAIT_OBJECT_0);
    EXPECT_EQ(WaitForSingleObject(ma, 3000), WAIT_OBJECT_0);
    Sleep(60);
    EXPECT_EQ(WaitForSingleObject(au, 0), WAIT_TIMEOUT);       /* auto-reset consumed, one-shot set once */
    EXPECT_EQ(WaitForSingleObject(ma, 0), WAIT_OBJECT_0);      /* manual-reset stays set */
    EXPECT_EQ(TKE(a), TL_NOCANDO); EXPECT_EQ(TKE(m), TL_NOCANDO);
    /* periodic: auto-reset is signalled again after each consume */
    UINT p = TSE(5, 1, (LPTIMECALLBACK)au, 0, TIME_PERIODIC | TIME_CALLBACK_EVENT_SET);
    CHECK(p != 0);
    int got = 0; for (int i = 0; i < 5; i++) got += WaitForSingleObject(au, 2000) == WAIT_OBJECT_0;
    EXPECT_EQ(got, 5);
    EXPECT_EQ(TKE(p), 0);
    Sleep(30); ResetEvent(au); Sleep(60);
    EXPECT_EQ(WaitForSingleObject(au, 0), WAIT_TIMEOUT);       /* no signal after the kill */
    CloseHandle(au); CloseHandle(ma);
}
/* EVENT_PULSE on a manual-reset event with no waiter leaves it non-signalled */
TEST(tl_cb_eventpulse_leaves_unsignalled) {
    TL_REQUIRE();
    HANDLE ma = CreateEventA(NULL, TRUE, FALSE, NULL);
    UINT id = TSE(10, 1, (LPTIMECALLBACK)ma, 0, TIME_ONESHOT | TIME_CALLBACK_EVENT_PULSE);
    CHECK(id != 0);
    Sleep(150);
    EXPECT_EQ(WaitForSingleObject(ma, 0), WAIT_TIMEOUT);
    MMRESULT r = 0; for (int k = 0; k < 50; k++) { r = TKE(id); if (r == TL_NOCANDO) break; Sleep(10); }
    EXPECT_EQ(r, TL_NOCANDO);
    CloseHandle(ma);
}

/* ---- re-arm chain: a one-shot callback sets the next one-shot --------------------------------- */
typedef struct { volatile LONG n; LONG target; UINT ids[8]; DWORD tgt[8]; volatile LONG set_fail; HANDLE done; UINT flags; UINT inner_id_mismatch; } cb_chain;
static void CALLBACK cb_chain_cb(UINT id, UINT msg, DWORD_PTR user, DWORD_PTR d1, DWORD_PTR d2) {
    cb_chain *c = (cb_chain *)user;
    LONG k = c->n;
    if (k < 8 && c->ids[k] && c->ids[k] != id) c->inner_id_mismatch++;
    c->tgt[k < 8 ? k : 7] = U(timeGetTime)();
    k = InterlockedIncrement(&c->n);
    if (k < c->target) {
        UINT nid = U(timeSetEvent)(10, 1, cb_chain_cb, user, c->flags);
        if (!nid) c->set_fail++;
        c->ids[k] = nid;
    } else SetEvent(c->done);
}
static void cb_chain_case(LONG target, UINT flags) {
    cb_chain c; memset(&c, 0, sizeof c); c.target = target; c.flags = flags; c.done = CreateEventA(NULL, TRUE, FALSE, NULL);
    c.ids[0] = TSE(10, 1, cb_chain_cb, (DWORD_PTR)&c, flags);
    REQUIRE(c.ids[0]);
    EXPECT_EQ(WaitForSingleObject(c.done, 5000), WAIT_OBJECT_0);
    Sleep(60);
    EXPECT_EQ(c.n, target);
    EXPECT_EQ(c.set_fail, 0);
    EXPECT_EQ(c.inner_id_mismatch, 0);
    for (int i = 0; i < target; i++) {
        CHECKF(c.ids[i] != 0, "link %d", i);
        for (int j = 0; j < i; j++) CHECKF(c.ids[i] != c.ids[j], "link %d reuses id of link %d", i, j);
        if (i) CHECKF((LONG)(c.tgt[i] - c.tgt[i - 1]) >= 0, "timeGetTime went backwards between links %d and %d", i - 1, i);
        /* the next link is created while the current one still occupies its slot: a different slot */
        if (i) CHECKF((c.ids[i] & 0xF) != (c.ids[i - 1] & 0xF), "link %d slot %u equals previous", i, c.ids[i] & 0xF);
        EXPECT_EQ(TKE(c.ids[i]), TL_NOCANDO);
    }
    CloseHandle(c.done);
}
TEST(tl_cb_rearm_chain_3) { TL_REQUIRE(); cb_chain_case(3, TIME_ONESHOT); }
TEST(tl_cb_rearm_chain_3_sync) { TL_REQUIRE(); cb_chain_case(3, TIME_ONESHOT | TIME_KILL_SYNCHRONOUS); }
TEST(tl_cb_rearm_chain_8) { TL_REQUIRE(); cb_chain_case(8, TIME_ONESHOT); }

/* a periodic callback creates and kills an event inside the callback */
typedef struct { volatile LONG calls; volatile LONG set_ok, kill_ok, kill_again; HANDLE done; } cb_inner;
static void CALLBACK cb_nop(UINT id, UINT msg, DWORD_PTR user, DWORD_PTR d1, DWORD_PTR d2) { }
static void CALLBACK cb_inner_cb(UINT id, UINT msg, DWORD_PTR user, DWORD_PTR d1, DWORD_PTR d2) {
    cb_inner *c = (cb_inner *)user;
    LONG n = InterlockedIncrement(&c->calls);
    if (n > 5) return;
    UINT x = U(timeSetEvent)(10000, 0, cb_nop, 0, TIME_PERIODIC | TIME_KILL_SYNCHRONOUS);
    if (x) c->set_ok++;
    if (U(timeKillEvent)(x) == 0) c->kill_ok++;
    if (U(timeKillEvent)(x) == TL_NOCANDO) c->kill_again++;
    if (n == 5) SetEvent(c->done);
}
TEST(tl_cb_set_and_kill_inside_periodic) {
    TL_REQUIRE();
    cb_inner c; memset(&c, 0, sizeof c); c.done = CreateEventA(NULL, TRUE, FALSE, NULL);
    UINT id = TSE(5, 1, cb_inner_cb, (DWORD_PTR)&c, TIME_PERIODIC | TIME_KILL_SYNCHRONOUS);
    REQUIRE(id);
    EXPECT_EQ(WaitForSingleObject(c.done, 3000), WAIT_OBJECT_0);
    EXPECT_EQ(TKE(id), 0);
    EXPECT_EQ(c.set_ok, 5); EXPECT_EQ(c.kill_ok, 5); EXPECT_EQ(c.kill_again, 5);
    CloseHandle(c.done);
}

/* timeGetTime inside periodic callbacks is monotonic and advances */
typedef struct { volatile LONG n; DWORD t[64]; } cb_tgt;
static void CALLBACK cb_tgt_cb(UINT id, UINT msg, DWORD_PTR user, DWORD_PTR d1, DWORD_PTR d2) {
    cb_tgt *c = (cb_tgt *)user; LONG k = c->n; if (k < 64) { c->t[k] = U(timeGetTime)(); c->n = k + 1; }
}
TEST(tl_cb_timegettime_monotonic_in_callbacks) {
    TL_REQUIRE();
    cb_tgt c; memset(&c, 0, sizeof c);
    UINT id = TSE(10, 1, cb_tgt_cb, (DWORD_PTR)&c, TIME_PERIODIC | TIME_KILL_SYNCHRONOUS);
    REQUIRE(id);
    CHECK(tl_wait_ge(&c.n, 20, 5000));
    EXPECT_EQ(TKE(id), 0);
    LONG n = c.n; int back = 0;
    for (int i = 1; i < n && i < 64; i++) if ((LONG)(c.t[i] - c.t[i - 1]) < 0) back++;
    EXPECT_EQ(back, 0);
    if (n >= 20) {
        DWORD span = c.t[19] - c.t[0];
        CHECKF(span >= 100 && span < 5000, "19 periods of 10 ms took %lu ms", span);
        t_note("19 periods of 10 ms: %lu ms", span);
    }
}

/* the callback may block the worker briefly; later events still fire */
static volatile LONG cb_slow_n;
static void CALLBACK cb_slow_cb(UINT id, UINT msg, DWORD_PTR user, DWORD_PTR d1, DWORD_PTR d2) { if (InterlockedIncrement(&cb_slow_n) == 1) Sleep(100); }
TEST(tl_cb_periodic_after_slow_first_call) {
    TL_REQUIRE();
    cb_slow_n = 0;
    UINT id = TSE(10, 1, cb_slow_cb, 0, TIME_PERIODIC);
    REQUIRE(id);
    CHECK(tl_wait_ge(&cb_slow_n, 6, 3000));
    EXPECT_EQ(TKE(id), 0);
    LONG c = cb_slow_n; Sleep(60);
    CHECK(cb_slow_n - c <= 1);
}

/* the callback runs with the caller's last-error untouched and its own last-error isolated */
static volatile DWORD cb_le;
static void CALLBACK cb_le_cb(UINT id, UINT msg, DWORD_PTR user, DWORD_PTR d1, DWORD_PTR d2) { SetLastError(0xBEEF); cb_le = 1; SetEvent((HANDLE)user); }
TEST(tl_cb_lasterror_not_leaked_to_caller) {
    TL_REQUIRE();
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    SetLastError(0x1234);
    UINT id = TSE(5, 1, cb_le_cb, (DWORD_PTR)ev, TIME_ONESHOT);
    REQUIRE(id);
    DWORD le = GetLastError();
    t_note("GetLastError after timeSetEvent = %lu", le);
    EXPECT_EQ(WaitForSingleObject(ev, 3000), WAIT_OBJECT_0);
    CHECK(GetLastError() != 0xBEEF);
    CloseHandle(ev);
}
