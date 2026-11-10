/* time_event_lifetime: the single timer worker thread, callback serialization, multi-threaded set/kill.
 * TimeInitThread (winmm 0x2d7c) creates exactly one thread (timeThread) and raises it to
 * THREAD_PRIORITY_TIME_CRITICAL (SetThreadPriority 15); timeThread runs every callback itself, one at
 * a time, so callbacks of different events never overlap and all see the same thread id.
 */
#include "tl.h"

typedef struct {
    volatile LONG inside, max_inside, calls;
    volatile LONG tid_mismatch;
    volatile DWORD tid;
    volatile LONG prio;
    DWORD work_ms;
} th_ser;
static void CALLBACK th_ser_cb(UINT id, UINT msg, DWORD_PTR user, DWORD_PTR d1, DWORD_PTR d2) {
    th_ser *s = (th_ser *)user;
    LONG in = InterlockedIncrement(&s->inside);
    LONG m;
    while ((m = s->max_inside) < in && InterlockedCompareExchange(&s->max_inside, in, m) != m) {}
    DWORD me = GetCurrentThreadId();
    DWORD prev = InterlockedCompareExchange((volatile LONG *)&s->tid, (LONG)me, 0);
    if (prev && prev != me) InterlockedIncrement(&s->tid_mismatch);
    s->prio = GetThreadPriority(GetCurrentThread());
    if (s->work_ms) Sleep(s->work_ms);
    else { volatile int x = 0; for (int i = 0; i < 20000; i++) x += i; }
    InterlockedIncrement(&s->calls);
    InterlockedDecrement(&s->inside);
}

/* n periodic events with the given delays share one callback; run for run_ms then kill all */
static void th_ser_case(int n, const UINT *delays, UINT flags, DWORD work_ms, DWORD run_ms, LONG min_calls) {
    th_ser s; memset(&s, 0, sizeof s); s.work_ms = work_ms;
    UINT ids[16] = {0};
    for (int i = 0; i < n; i++) { ids[i] = TSE(delays[i], 1, th_ser_cb, (DWORD_PTR)&s, flags); CHECKF(ids[i] != 0, "event %d", i); }
    CHECK(tl_wait_ge(&s.calls, min_calls, run_ms + 3000));
    Sleep(run_ms);
    for (int i = 0; i < n; i++) if (ids[i]) EXPECT_EQ(TKE(ids[i]), 0);
    tl_wait_ge(&s.inside, 0, 100);
    Sleep(work_ms + 30);
    EXPECT_EQ(s.max_inside, 1);
    EXPECT_EQ(s.tid_mismatch, 0);
    CHECK(s.tid != 0);
    CHECK(s.tid != GetCurrentThreadId());
    CHECKF(s.calls >= min_calls, "calls %ld", s.calls);
    LONG c = s.calls; Sleep(60); EXPECT_EQ(s.calls, c);   /* nothing after the kills */
}
static const UINT th_d2[] = { 10, 15 };
static const UINT th_d4[] = { 1, 2, 3, 5 };
static const UINT th_d8[] = { 1, 1, 2, 2, 3, 3, 4, 4 };
static const UINT th_d16[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
TEST(tl_thread_serial_two_periodic) { TL_REQUIRE(); th_ser_case(2, th_d2, TIME_PERIODIC, 0, 300, 10); }
TEST(tl_thread_serial_two_periodic_sleepy) { TL_REQUIRE(); th_ser_case(2, th_d2, TIME_PERIODIC, 8, 300, 6); }
TEST(tl_thread_serial_four_1ms) { TL_REQUIRE(); th_ser_case(4, th_d4, TIME_PERIODIC, 0, 200, 20); }
TEST(tl_thread_serial_four_1ms_sync) { TL_REQUIRE(); th_ser_case(4, th_d4, TIME_PERIODIC | TIME_KILL_SYNCHRONOUS, 0, 200, 20); }
TEST(tl_thread_serial_eight_sleepy) { TL_REQUIRE(); th_ser_case(8, th_d8, TIME_PERIODIC, 2, 200, 10); }

/* 16 events (the whole table) at once */
TEST(tl_thread_serial_sixteen) {
    TL_REQUIRE();
    th_ser_case(16, th_d16, TIME_PERIODIC, 1, 200, 16);
}

/* The callback thread is the same for every event, across kills, and persists after all events die. */
static volatile DWORD th_last_tid;
static volatile LONG th_last_prio;
static HANDLE th_sig;
static void CALLBACK th_tid_cb(UINT id, UINT msg, DWORD_PTR user, DWORD_PTR d1, DWORD_PTR d2) {
    th_last_tid = GetCurrentThreadId();
    th_last_prio = GetThreadPriority(GetCurrentThread());
    SetEvent(th_sig);
}
static DWORD th_fire_once(UINT flags) {
    th_last_tid = 0; ResetEvent(th_sig);
    UINT id = TSE(5, 1, th_tid_cb, 0, flags);
    if (!id) return 0;
    DWORD w = WaitForSingleObject(th_sig, 3000);
    TKE(id);
    Sleep(20);
    return w == WAIT_OBJECT_0 ? th_last_tid : 0;
}
TEST(tl_thread_same_worker_across_events) {
    TL_REQUIRE();
    th_sig = CreateEventA(NULL, TRUE, FALSE, NULL);
    DWORD first = th_fire_once(TIME_ONESHOT);
    CHECK(first != 0);
    CHECK(first != GetCurrentThreadId());
    static const UINT fl[] = { TIME_ONESHOT, TIME_PERIODIC, TIME_ONESHOT | TIME_KILL_SYNCHRONOUS,
        TIME_PERIODIC | TIME_KILL_SYNCHRONOUS, TIME_ONESHOT, TIME_PERIODIC };
    for (int i = 0; i < 6; i++) {
        DWORD t = th_fire_once(fl[i]);
        CHECKF(t == first, "flags 0x%x callback tid %lu, first %lu", fl[i], t, first);
    }
    /* the worker still exists while no event is live (it is never torn down) */
    HANDLE h = OpenThread(SYNCHRONIZE | THREAD_QUERY_LIMITED_INFORMATION, FALSE, first);
    CHECK(h != NULL);
    if (h) { EXPECT_EQ(WaitForSingleObject(h, 0), WAIT_TIMEOUT); CloseHandle(h); }
    CloseHandle(th_sig);
}
TEST(tl_thread_worker_priority_time_critical) {
    TL_REQUIRE();
    th_sig = CreateEventA(NULL, TRUE, FALSE, NULL);
    th_last_prio = -99;
    DWORD t = th_fire_once(TIME_ONESHOT);
    CHECK(t != 0);
    EXPECT_EQ(th_last_prio, THREAD_PRIORITY_TIME_CRITICAL);
    CloseHandle(th_sig);
}
/* the worker thread belongs to this process */
TEST(tl_thread_worker_in_process) {
    TL_REQUIRE();
    th_sig = CreateEventA(NULL, TRUE, FALSE, NULL);
    DWORD t = th_fire_once(TIME_PERIODIC);
    REQUIRE(t);
    HANDLE h = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, t);
    CHECK(h != NULL);
    if (h) { EXPECT_EQ(GetProcessIdOfThread(h), GetCurrentProcessId()); CloseHandle(h); }
    CloseHandle(th_sig);
}

/* A slow callback of event A delays event B: B starts only after A returned. */
typedef struct { volatile LONG a_end_seq, b_start_seq, seq; HANDLE b_done; DWORD a_sleep; } th_order;
static void CALLBACK th_a_cb(UINT id, UINT msg, DWORD_PTR user, DWORD_PTR d1, DWORD_PTR d2) {
    th_order *o = (th_order *)user; Sleep(o->a_sleep); o->a_end_seq = InterlockedIncrement(&o->seq);
}
static void CALLBACK th_b_cb(UINT id, UINT msg, DWORD_PTR user, DWORD_PTR d1, DWORD_PTR d2) {
    th_order *o = (th_order *)user; o->b_start_seq = InterlockedIncrement(&o->seq); SetEvent(o->b_done);
}
TEST(tl_thread_slow_callback_delays_other_event) {
    TL_REQUIRE();
    th_order o; memset(&o, 0, sizeof o); o.b_done = CreateEventA(NULL, TRUE, FALSE, NULL); o.a_sleep = 150;
    UINT a = TSE(10, 1, th_a_cb, (DWORD_PTR)&o, TIME_ONESHOT);
    UINT b = TSE(30, 1, th_b_cb, (DWORD_PTR)&o, TIME_ONESHOT);
    CHECK(a && b);
    EXPECT_EQ(WaitForSingleObject(o.b_done, 3000), WAIT_OBJECT_0);
    EXPECT_EQ(o.a_end_seq, 1);
    EXPECT_EQ(o.b_start_seq, 2);
    Sleep(30);
    EXPECT_EQ(TKE(a), TL_NOCANDO); EXPECT_EQ(TKE(b), TL_NOCANDO);
    CloseHandle(o.b_done);
}
/* The same with an EVENT_SET event: the SetEvent happens on the worker too, after A returns */
typedef struct { th_order *o; HANDLE ev; } th_evw;
static DWORD WINAPI th_evwait(LPVOID p) {
    th_evw *w = (th_evw *)p;
    if (WaitForSingleObject(w->ev, 3000) == WAIT_OBJECT_0) w->o->b_start_seq = InterlockedIncrement(&w->o->seq);
    return 0;
}
TEST(tl_thread_slow_callback_delays_eventset) {
    TL_REQUIRE();
    th_order o; memset(&o, 0, sizeof o); o.a_sleep = 150;
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    th_evw w = { &o, ev };
    HANDLE t = CreateThread(NULL, 0, th_evwait, &w, 0, NULL);
    UINT a = TSE(10, 1, th_a_cb, (DWORD_PTR)&o, TIME_ONESHOT);
    UINT b = TSE(30, 1, (LPTIMECALLBACK)ev, 0, TIME_ONESHOT | TIME_CALLBACK_EVENT_SET);
    CHECK(a && b);
    EXPECT_EQ(WaitForSingleObject(t, 4000), WAIT_OBJECT_0);
    EXPECT_EQ(o.a_end_seq, 1);
    EXPECT_EQ(o.b_start_seq, 2);
    CloseHandle(t); CloseHandle(ev);
    Sleep(30);
    EXPECT_EQ(TKE(a), TL_NOCANDO); EXPECT_EQ(TKE(b), TL_NOCANDO);
}

/* ---- timeSetEvent from several threads concurrently (in a child: a clean 16-slot table) ------- */
static void CALLBACK th_nop(UINT id, UINT msg, DWORD_PTR user, DWORD_PTR d1, DWORD_PTR d2) { }
typedef struct { HANDLE go; int per; UINT flags; UINT ids[16]; } th_setter;
static DWORD WINAPI th_set_thread(LPVOID p) {
    th_setter *s = (th_setter *)p;
    WaitForSingleObject(s->go, 5000);
    for (int i = 0; i < s->per; i++) s->ids[i] = U(timeSetEvent)(10000, 0, th_nop, 0, s->flags);
    return 0;
}
typedef struct { HANDLE go; int n; UINT *ids; LONG r[16]; } th_killer;
static DWORD WINAPI th_kill_thread(LPVOID p) {
    th_killer *k = (th_killer *)p;
    WaitForSingleObject(k->go, 5000);
    for (int i = 0; i < k->n; i++) k->r[i] = (LONG)U(timeKillEvent)(k->ids[i]);
    return 0;
}
/* argv[2] = threads, argv[3] = per thread, argv[4] = flags */
CHILD(tl_thread_concurrent_set) {
    TL_CHILD_BEGIN();
    int nt = argc > 2 ? atoi(argv[2]) : 4, per = argc > 3 ? atoi(argv[3]) : 4;
    UINT flags = argc > 4 ? (UINT)strtoul(argv[4], NULL, 0) : TIME_PERIODIC;
    if (nt > 8) nt = 8;
    if (per > 16) per = 16;
    HANDLE go = CreateEventA(NULL, TRUE, FALSE, NULL);
    th_setter s[8]; HANDLE th[8];
    for (int i = 0; i < nt; i++) { memset(&s[i], 0, sizeof s[i]); s[i].go = go; s[i].per = per; s[i].flags = flags; th[i] = CreateThread(NULL, 0, th_set_thread, &s[i], 0, NULL); }
    Sleep(30); SetEvent(go);
    tl_rec("joined", WaitForMultipleObjects(nt, th, TRUE, 10000));
    UINT all[128]; int n = 0, nonzero = 0, dup = 0, badrange = 0;
    for (int i = 0; i < nt; i++) for (int j = 0; j < per; j++) {
        UINT id = s[i].ids[j];
        if (id) {
            nonzero++;
            if (id >= 0x10000) badrange++;
            for (int k = 0; k < n; k++) if (all[k] == id) dup++;
            all[n++] = id;
        }
    }
    int slots = 0; for (int k = 0; k < n; k++) slots |= 1 << (all[k] & 0xF);
    tl_rec("nonzero", nonzero); tl_rec("dup", dup); tl_rec("badrange", badrange);
    tl_rec("slotmask", slots);
    /* kill concurrently: split the ids among the threads */
    ResetEvent(go);
    th_killer k[8]; HANDLE kt[8]; int per_k = (n + nt - 1) / nt, pos = 0;
    for (int i = 0; i < nt; i++) {
        memset(&k[i], 0, sizeof k[i]); k[i].go = go; k[i].ids = all + pos;
        k[i].n = n - pos < per_k ? n - pos : per_k; if (k[i].n < 0) k[i].n = 0; pos += k[i].n;
        kt[i] = CreateThread(NULL, 0, th_kill_thread, &k[i], 0, NULL);
    }
    Sleep(30); SetEvent(go);
    tl_rec("kjoined", WaitForMultipleObjects(nt, kt, TRUE, 10000));
    int kz = 0, kother = 0;
    for (int i = 0; i < nt; i++) for (int j = 0; j < k[i].n; j++) { if (k[i].r[j] == 0) kz++; else kother++; }
    tl_rec("kills0", kz); tl_rec("killsother", kother);
    int again = 0; for (int i = 0; i < n; i++) again += TKE(all[i]) == TL_NOCANDO;
    tl_rec("again97", again);
    /* the table is empty again */
    UINT f[16]; int ok = 0; for (int i = 0; i < 16; i++) { f[i] = TSE(10000, 0, th_nop, 0, TIME_PERIODIC); ok += f[i] != 0; }
    tl_rec("refill", ok);
    for (int i = 0; i < 16; i++) TKE(f[i]);
    for (int i = 0; i < nt; i++) { CloseHandle(th[i]); CloseHandle(kt[i]); }
    tl_rec("done", 1); tl_rec_close();
    return 0;
}
static void th_conc_case(int nt, int per, UINT flags) {
    char extra[64]; snprintf(extra, sizeof extra, "%d %d 0x%x", nt, per, flags);
    tl_kv *kv = calloc(1, sizeof *kv);
    EXPECT_EQ(tl_run("tl_thread_concurrent_set", extra, 30000, kv), 0);
    KV_EQ(kv, "done", 1);
    int total = nt * per, expect = total < 16 ? total : 16;
    KV_EQ(kv, "joined", WAIT_OBJECT_0);
    KV_EQ(kv, "nonzero", expect);
    KV_EQ(kv, "dup", 0);
    KV_EQ(kv, "badrange", 0);
    KV_EQ(kv, "slotmask", (1 << expect) - 1);     /* slots fill from 0 upwards */
    KV_EQ(kv, "kjoined", WAIT_OBJECT_0);
    KV_EQ(kv, "kills0", expect);
    KV_EQ(kv, "killsother", 0);
    KV_EQ(kv, "again97", expect);
    KV_EQ(kv, "refill", 16);
    free(kv);
}
TEST(tl_thread_concurrent_4x4_periodic) { TL_REQUIRE(); th_conc_case(4, 4, TIME_PERIODIC); }
TEST(tl_thread_concurrent_4x4_oneshot) { TL_REQUIRE(); th_conc_case(4, 4, TIME_ONESHOT); }
TEST(tl_thread_concurrent_4x4_sync) { TL_REQUIRE(); th_conc_case(4, 4, TIME_PERIODIC | TIME_KILL_SYNCHRONOUS); }
TEST(tl_thread_concurrent_2x8_periodic) { TL_REQUIRE(); th_conc_case(2, 8, TIME_PERIODIC); }
TEST(tl_thread_concurrent_8x2_periodic) { TL_REQUIRE(); th_conc_case(8, 2, TIME_PERIODIC); }
TEST(tl_thread_concurrent_4x5_over_cap) { TL_REQUIRE(); th_conc_case(4, 5, TIME_PERIODIC); }
TEST(tl_thread_concurrent_8x4_over_cap) { TL_REQUIRE(); th_conc_case(8, 4, TIME_PERIODIC); }
TEST(tl_thread_concurrent_3x3_partial) { TL_REQUIRE(); th_conc_case(3, 3, TIME_PERIODIC); }

/* ---- set/kill churn from several threads: ids never repeat within 4095 allocations ----------- */
typedef struct { HANDLE go; int iters; UINT *out; volatile LONG bad; } th_churn;
static DWORD WINAPI th_churn_thread(LPVOID p) {
    th_churn *c = (th_churn *)p;
    WaitForSingleObject(c->go, 5000);
    for (int i = 0; i < c->iters; i++) {
        UINT id = U(timeSetEvent)(1000, 0, th_nop, 0, (i & 1) ? TIME_PERIODIC : TIME_ONESHOT);
        c->out[i] = id;
        if (!id) { InterlockedIncrement(&c->bad); continue; }
        if (U(timeKillEvent)(id) != 0) InterlockedIncrement(&c->bad);
        if (U(timeKillEvent)(id) != TL_NOCANDO) InterlockedIncrement(&c->bad);
    }
    return 0;
}
TEST(tl_thread_churn_ids_unique) {
    TL_REQUIRE();
    enum { NT = 4, IT = 150 };
    static UINT out[NT][IT];
    memset(out, 0, sizeof out);
    HANDLE go = CreateEventA(NULL, TRUE, FALSE, NULL);
    th_churn c[NT]; HANDLE th[NT];
    for (int i = 0; i < NT; i++) { c[i].go = go; c[i].iters = IT; c[i].out = out[i]; c[i].bad = 0; th[i] = CreateThread(NULL, 0, th_churn_thread, &c[i], 0, NULL); }
    SetEvent(go);
    EXPECT_EQ(WaitForMultipleObjects(NT, th, TRUE, 20000), WAIT_OBJECT_0);
    int dup = 0, zero = 0;
    static UINT seen[NT * IT]; int ns = 0;
    for (int i = 0; i < NT; i++) for (int j = 0; j < IT; j++) {
        UINT id = out[i][j]; if (!id) { zero++; continue; }
        for (int k = 0; k < ns; k++) if (seen[k] == id) { dup++; break; }
        seen[ns++] = id;
    }
    for (int i = 0; i < NT; i++) { EXPECT_EQ(c[i].bad, 0); CloseHandle(th[i]); }
    EXPECT_EQ(zero, 0);
    EXPECT_EQ(dup, 0);
    CloseHandle(go);
}

/* An event set from a thread that has exited still fires (callbacks run on the worker). */
typedef struct { HANDLE ev; UINT id; UINT flags; } th_orphan;
static volatile LONG th_orphan_hits;
static void CALLBACK th_orphan_cb(UINT id, UINT msg, DWORD_PTR user, DWORD_PTR d1, DWORD_PTR d2) {
    InterlockedIncrement(&th_orphan_hits); SetEvent((HANDLE)user);
}
static DWORD WINAPI th_orphan_thread(LPVOID p) {
    th_orphan *o = (th_orphan *)p; o->id = U(timeSetEvent)(50, 1, th_orphan_cb, (DWORD_PTR)o->ev, o->flags); return 0;
}
TEST(tl_thread_event_outlives_creating_thread) {
    TL_REQUIRE();
    static const UINT fl[] = { TIME_ONESHOT, TIME_PERIODIC, TIME_PERIODIC | TIME_KILL_SYNCHRONOUS };
    for (int i = 0; i < 3; i++) {
        th_orphan o; o.ev = CreateEventA(NULL, FALSE, FALSE, NULL); o.id = 0; o.flags = fl[i];
        th_orphan_hits = 0;
        HANDLE t = CreateThread(NULL, 0, th_orphan_thread, &o, 0, NULL);
        EXPECT_EQ(WaitForSingleObject(t, 5000), WAIT_OBJECT_0); CloseHandle(t);
        CHECK(o.id != 0);
        EXPECT_EQ(WaitForSingleObject(o.ev, 3000), WAIT_OBJECT_0);
        if (fl[i] & TIME_PERIODIC) { EXPECT_EQ(WaitForSingleObject(o.ev, 3000), WAIT_OBJECT_0); EXPECT_EQ(TKE(o.id), 0); }
        else { Sleep(30); EXPECT_EQ(TKE(o.id), TL_NOCANDO); }
        Sleep(80);
        CloseHandle(o.ev);
    }
}

/* Callbacks of an event set from a worker thread run on the timer worker, not on the creator */
static volatile DWORD th_cb_tid;
static void CALLBACK th_tid2_cb(UINT id, UINT msg, DWORD_PTR user, DWORD_PTR d1, DWORD_PTR d2) { th_cb_tid = GetCurrentThreadId(); SetEvent((HANDLE)user); }
typedef struct { HANDLE ev; UINT id; DWORD tid; HANDLE done; } th_cr;
static DWORD WINAPI th_creator(LPVOID p) {
    th_cr *c = (th_cr *)p; c->tid = GetCurrentThreadId();
    c->id = U(timeSetEvent)(10, 1, th_tid2_cb, (DWORD_PTR)c->ev, TIME_ONESHOT);
    WaitForSingleObject(c->done, 5000);
    return 0;
}
TEST(tl_thread_callback_not_on_creator) {
    TL_REQUIRE();
    th_cr c; c.ev = CreateEventA(NULL, FALSE, FALSE, NULL); c.done = CreateEventA(NULL, TRUE, FALSE, NULL); c.id = 0;
    th_cb_tid = 0;
    HANDLE t = CreateThread(NULL, 0, th_creator, &c, 0, NULL);
    EXPECT_EQ(WaitForSingleObject(c.ev, 3000), WAIT_OBJECT_0);
    CHECK(th_cb_tid != 0);
    CHECK(th_cb_tid != c.tid);
    CHECK(th_cb_tid != GetCurrentThreadId());
    SetEvent(c.done);
    WaitForSingleObject(t, 5000); CloseHandle(t);
    CloseHandle(c.ev); CloseHandle(c.done);
}
