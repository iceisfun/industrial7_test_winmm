/* time_event_lifetime: the timeBeginPeriod reference each event holds.
 * timeSetEvent (winmm 0x2a20) computes
 *     period = uResolution > TimerData ? TimerData : max(uResolution, wPeriodMin);  if (uDelay < period) period = TimerData;
 * calls timeBeginPeriod(period) before allocating a slot (timeEndPeriod(period) if allocation fails),
 * and the event gives the reference back exactly once: timeKillEvent, one-shot completion in timeThread,
 * or a failed NtSetTimer re-arm. kernel32 timeBeginPeriod/timeEndPeriod (0x1e110 / 0x1d190) count each
 * period in [wPeriodMin, uMax) separately (16-bit counters) and treat periods >= uMax as uncounted no-ops;
 * timeEndPeriod on a zero count -> 97. So the reference an event holds is observable with timeEndPeriod.
 * Every test drains/restores what it touches and leaves the process with no references.
 */
#include "tl.h"

static void CALLBACK pe_nop(UINT id, UINT msg, DWORD_PTR user, DWORD_PTR d1, DWORD_PTR d2) { }

/* all counted periods are free in this process right now */
static int pe_clean(void) {
    UINT mx = tl_umax();
    for (UINT p = tl_pmin(); p < mx; p++) if (tl_refs(p, 64)) { t_skip("process already holds period %u", p); return 0; }
    return 1;
}

/* ---- table: which period a (delay, resolution) event holds ---------------------------------- */
typedef struct { UINT delay, res; } pe_row;
static const pe_row pe_rows[] = {
    { 1000, 0 }, { 1000, 1 }, { 1000, 2 }, { 1000, 3 }, { 1000, 5 }, { 1000, 7 }, { 1000, 10 }, { 1000, 12 },
    { 1000, 15 }, { 1000, 16 }, { 1000, 17 }, { 1000, 50 }, { 1000, 100 }, { 1000, 999 }, { 1000, 1000 },
    { 1000, 0xFFFFFFFF }, { 5, 5 }, { 4, 5 }, { 3, 10 }, { 10, 10 }, { 15, 15 }, { 16, 15 }, { 14, 15 },
    { 9, 10 }, { 2, 1 }, { 2, 2 }, { 2, 3 }, { 1, 0 }, { 1, 1 }, { 1, 2 }, { 1000000, 1 }, { 1000000, 0 },
    { 1000000, 8 }, { 500, 500 }, { 12, 13 }, { 13, 13 }, { 100, 4 }, { 100, 6 }, { 100, 9 }, { 100, 11 },
    { 100, 13 }, { 100, 14 }, { 20, 0 }, { 20, 20 }, { 7, 8 }, { 8, 8 },
};
static void pe_row_case(UINT flags) {
    if (!pe_clean()) return;
    UINT mx = tl_umax(), mn = tl_pmin();
    t_note("wPeriodMin %u, TimerData/uMax %u", mn, mx);
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    for (size_t r = 0; r < sizeof pe_rows / sizeof pe_rows[0]; r++) {
        UINT d = pe_rows[r].delay, res = pe_rows[r].res;
        UINT want = tl_event_period(d, res);
        LPTIMECALLBACK cb = (flags & TIME_CALLBACK_EVENT_SET) ? (LPTIMECALLBACK)ev : pe_nop;
        UINT id = TSE(d, res, cb, 0, flags);
        CHECKF(id != 0, "delay %u res %u", d, res);
        if (!id) continue;
        for (UINT p = mn; p < mx; p++) {
            int n = tl_refs(p, 8);
            int exp = (p == want) ? 1 : 0;
            CHECKF(n == exp, "delay %u res %u flags 0x%x: period %u refs %d want %d (event period %u)", d, res, flags, p, n, exp, want);
        }
        EXPECT_EQ(TKE(id), 0);
        for (UINT p = mn; p < mx; p++) CHECKF(tl_refs(p, 8) == 0, "after kill: delay %u res %u period %u still held", d, res, p);
    }
    CloseHandle(ev);
}
TEST(tl_period_table_periodic) { TL_REQUIRE(); pe_row_case(TIME_PERIODIC); }
TEST(tl_period_table_periodic_sync) { TL_REQUIRE(); pe_row_case(TIME_PERIODIC | TIME_KILL_SYNCHRONOUS); }
TEST(tl_period_table_eventset) { TL_REQUIRE(); pe_row_case(TIME_PERIODIC | TIME_CALLBACK_EVENT_SET); }

/* ---- the spec scenario: set and kill a 1 ms event, then an unowned End(1) fails ------------- */
TEST(tl_period_kill_releases_1ms) {
    TL_REQUIRE();
    UINT p = tl_pmin();
    if (!tl_counted(p)) { t_skip("period %u uncounted", p); return; }
    if (tl_refs(p, 64)) { t_skip("process already holds period %u", p); return; }
    EXPECT_EQ(TEP(p), TL_NOCANDO);
    UINT id = TSE(1000, p, pe_nop, 0, TIME_PERIODIC);
    REQUIRE(id);
    /* live: the event's reference makes an unowned End succeed; give it back */
    EXPECT_EQ(TEP(p), 0);
    EXPECT_EQ(TEP(p), TL_NOCANDO);
    EXPECT_EQ(TBP(p), 0);
    EXPECT_EQ(TKE(id), 0);
    EXPECT_EQ(TEP(p), TL_NOCANDO);
    EXPECT_EQ(TEP(p), TL_NOCANDO);
}
/* the stolen reference is not returned twice: steal it, kill (its End fails silently), still clean */
TEST(tl_period_kill_after_reference_stolen) {
    TL_REQUIRE();
    UINT p = 3;
    if (!tl_counted(p) || !pe_clean()) { t_skip("period %u", p); return; }
    UINT id = TSE(1000, p, pe_nop, 0, TIME_PERIODIC);
    REQUIRE(id);
    EXPECT_EQ(TEP(p), 0);
    EXPECT_EQ(TKE(id), 0);          /* kill ignores the timeEndPeriod result */
    EXPECT_EQ(TEP(p), TL_NOCANDO);
    EXPECT_EQ(TKE(id), TL_NOCANDO);
}

/* ---- stacking: N events on one period hold N references ------------------------------------ */
static void pe_stack_case(UINT p, int n, UINT flags) {
    if (!tl_counted(p)) { t_skip("period %u uncounted", p); return; }
    if (tl_refs(p, 64)) { t_skip("process already holds period %u", p); return; }
    UINT ids[16] = {0};
    for (int i = 0; i < n; i++) { ids[i] = TSE(1000 + i, p, pe_nop, 0, flags); CHECK(ids[i] != 0); }
    EXPECT_EQ(tl_refs(p, 64), n);
    for (int i = 0; i < n; i++) {
        EXPECT_EQ(TKE(ids[i]), 0);
        EXPECT_EQ(tl_refs(p, 64), n - 1 - i);
    }
    EXPECT_EQ(TEP(p), TL_NOCANDO);
}
TEST(tl_period_stack_1x8) { TL_REQUIRE(); pe_stack_case(1, 8, TIME_PERIODIC); }
TEST(tl_period_stack_2x4) { TL_REQUIRE(); pe_stack_case(2, 4, TIME_PERIODIC); }
TEST(tl_period_stack_5x12) { TL_REQUIRE(); pe_stack_case(5, 12, TIME_PERIODIC); }
TEST(tl_period_stack_15x3) { TL_REQUIRE(); pe_stack_case(15, 3, TIME_ONESHOT); }
TEST(tl_period_stack_7x6_sync) { TL_REQUIRE(); pe_stack_case(7, 6, TIME_PERIODIC | TIME_KILL_SYNCHRONOUS); }

/* events on different periods hold independent references */
TEST(tl_period_mixed_periods_independent) {
    TL_REQUIRE();
    if (!pe_clean()) return;
    static const UINT ps[] = { 1, 2, 3, 4, 6, 9, 12, 15 };
    UINT ids[8];
    for (int i = 0; i < 8; i++) ids[i] = TSE(1000, ps[i], pe_nop, 0, TIME_PERIODIC);
    for (int i = 0; i < 8; i++) {
        CHECK(ids[i] != 0);
        if (tl_counted(ps[i])) EXPECT_EQ(tl_refs(ps[i], 8), 1);
    }
    /* kill in a scrambled order; each kill releases only its own period */
    static const int order[] = { 3, 0, 7, 5, 1, 6, 2, 4 };
    for (int k = 0; k < 8; k++) {
        EXPECT_EQ(TKE(ids[order[k]]), 0);
        for (int i = 0; i < 8; i++) {
            int dead = 0; for (int j = 0; j <= k; j++) if (order[j] == i) dead = 1;
            if (tl_counted(ps[i])) CHECKF(tl_refs(ps[i], 8) == (dead ? 0 : 1), "after kill #%d period %u", k, ps[i]);
        }
    }
}

/* our own Begin and an event's reference on the same period are counted together */
TEST(tl_period_own_begin_survives_event) {
    TL_REQUIRE();
    UINT p = 2;
    if (!tl_counted(p) || tl_refs(p, 64)) { t_skip("period %u", p); return; }
    EXPECT_EQ(TBP(p), 0);
    UINT id = TSE(1000, p, pe_nop, 0, TIME_PERIODIC);
    REQUIRE(id);
    EXPECT_EQ(tl_refs(p, 8), 2);
    EXPECT_EQ(TKE(id), 0);
    EXPECT_EQ(tl_refs(p, 8), 1);
    EXPECT_EQ(TEP(p), 0);
    EXPECT_EQ(TEP(p), TL_NOCANDO);
}

/* invalid timeSetEvent calls take no reference */
TEST(tl_period_invalid_set_takes_nothing) {
    TL_REQUIRE();
    if (!pe_clean()) return;
    struct { UINT d, r, f; } bad[] = {
        { 0, 1, TIME_PERIODIC }, { 1000001, 1, TIME_PERIODIC }, { 0xFFFFFFFF, 1, TIME_ONESHOT },
        { 1000, 1, 0x2 }, { 1000, 1, 0x4 }, { 1000, 1, 0x8 }, { 1000, 1, 0x200 }, { 1000, 1, 0x8000 },
        { 1000, 1, 0x80000000 }, { 1000, 2, 0x203 }, { 0, 5, TIME_ONESHOT }, { 1000001, 5, TIME_ONESHOT },
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        UINT id = TSE(bad[i].d, bad[i].r, pe_nop, 0, bad[i].f);
        CHECKF(id == 0, "delay %u res %u flags 0x%x -> 0x%x", bad[i].d, bad[i].r, bad[i].f, id);
        if (id) TKE(id);
        CHECKF(tl_refs(bad[i].r, 8) == 0, "row %u leaked a reference on period %u", (unsigned)i, bad[i].r);
    }
}

/* ---- one-shot completion releases the reference --------------------------------------------- */
static volatile LONG pe_hits;
static void CALLBACK pe_sig_cb(UINT id, UINT msg, DWORD_PTR user, DWORD_PTR d1, DWORD_PTR d2) { InterlockedIncrement(&pe_hits); SetEvent((HANDLE)user); }
static void pe_oneshot_case(UINT p, UINT flags, int use_event) {
    if (!tl_counted(p) || tl_refs(p, 64)) { t_skip("period %u", p); return; }
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    UINT id = TSE(20, p, use_event ? (LPTIMECALLBACK)ev : pe_sig_cb, (DWORD_PTR)ev, flags | (use_event ? TIME_CALLBACK_EVENT_SET : 0));
    REQUIRE(id);
    EXPECT_EQ(WaitForSingleObject(ev, 3000), WAIT_OBJECT_0);
    /* the worker (TIME_CRITICAL) frees the slot and calls timeEndPeriod right after the callback;
     * poll the slot through timeKillEvent: a 97 means it is gone (a 0 would have released it too) */
    MMRESULT r = 0; for (int i = 0; i < 100; i++) { Sleep(5); r = TKE(id); if (r == TL_NOCANDO) break; }
    EXPECT_EQ(r, TL_NOCANDO);
    EXPECT_EQ(tl_refs(p, 8), 0);
    EXPECT_EQ(TEP(p), TL_NOCANDO);
    CloseHandle(ev);
}
TEST(tl_period_oneshot_fn_releases_1) { TL_REQUIRE(); pe_oneshot_case(1, TIME_ONESHOT, 0); }
TEST(tl_period_oneshot_fn_releases_4) { TL_REQUIRE(); pe_oneshot_case(4, TIME_ONESHOT, 0); }
TEST(tl_period_oneshot_fn_sync_releases_2) { TL_REQUIRE(); pe_oneshot_case(2, TIME_ONESHOT | TIME_KILL_SYNCHRONOUS, 0); }
TEST(tl_period_oneshot_evset_releases_1) { TL_REQUIRE(); pe_oneshot_case(1, TIME_ONESHOT, 1); }
TEST(tl_period_oneshot_evset_releases_10) { TL_REQUIRE(); pe_oneshot_case(10, TIME_ONESHOT, 1); }

/* a periodic event keeps its reference across many firings */
TEST(tl_period_periodic_keeps_reference_while_firing) {
    TL_REQUIRE();
    UINT p = 1;
    if (!tl_counted(p) || tl_refs(p, 64)) { t_skip("period %u", p); return; }
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    pe_hits = 0;
    UINT id = TSE(5, p, pe_sig_cb, (DWORD_PTR)ev, TIME_PERIODIC);
    REQUIRE(id);
    CHECK(tl_wait_ge(&pe_hits, 10, 3000));
    EXPECT_EQ(tl_refs(p, 8), 1);
    CHECK(tl_wait_ge(&pe_hits, 20, 3000));
    EXPECT_EQ(tl_refs(p, 8), 1);
    EXPECT_EQ(TKE(id), 0);
    EXPECT_EQ(tl_refs(p, 8), 0);
    CloseHandle(ev);
}

/* while a 1 ms event lives, the system timer resolution is at most 1 ms */
TEST(tl_period_event_raises_resolution) {
    TL_REQUIRE();
    if (tl_pmin() != 1) { t_skip("wPeriodMin %u", tl_pmin()); return; }
    ULONG before = tl_cur_res();
    UINT id = TSE(1000, 1, pe_nop, 0, TIME_PERIODIC);
    REQUIRE(id);
    ULONG during = tl_cur_res();
    CHECKF(during != 0 && during <= 10000, "current resolution %lu (100 ns) while a 1 ms event lives", during);
    EXPECT_EQ(TKE(id), 0);
    ULONG after = tl_cur_res();
    t_note("timer resolution before %lu during %lu after %lu (100 ns units; other processes also request)", before, during, after);
}

/* ---- a failing timeBeginPeriod makes timeSetEvent fail (16-bit counter saturated) ----------- */
CHILD(tl_period_saturate) {
    TL_CHILD_BEGIN();
    UINT p = 6;
    if (!tl_counted(p)) { tl_rec("uncounted", 1); tl_rec_close(); return 0; }
    int ok = 0; MMRESULT r = 0;
    for (int i = 0; i < 65535; i++) if (TBP(p) == 0) ok++;
    tl_rec("begins", ok);
    tl_rec("begin_over", TBP(p));                         /* counter at 0xFFFF -> 97 */
    tl_rec("set_sat", TSE(1000, p, pe_nop, 0, TIME_PERIODIC));   /* timeBeginPeriod fails -> 0 */
    tl_rec("set_sat_os", TSE(1000, p, pe_nop, 0, TIME_ONESHOT));
    UINT other = TSE(1000, p + 1, pe_nop, 0, TIME_PERIODIC);       /* other periods unaffected */
    tl_rec("set_other", other != 0);
    tl_rec("kill_other", TKE(other));
    UINT td = TSE(1000, 1000, pe_nop, 0, TIME_PERIODIC);           /* TimerData: uncounted, works */
    tl_rec("set_td", td != 0);
    tl_rec("kill_td", TKE(td));
    tl_rec("end1", TEP(p));                               /* 0xFFFE now */
    UINT id = TSE(1000, p, pe_nop, 0, TIME_PERIODIC);
    tl_rec("set_after", id != 0);
    tl_rec("begin_full_again", TBP(p));                   /* the event took the last count */
    tl_rec("kill_after", TKE(id));
    int ends = 0; for (int i = 0; i < 70000; i++) { if (TEP(p) == 0) ends++; else break; }
    tl_rec("ends", ends);
    tl_rec("done", 1); tl_rec_close();
    return 0;
}
TEST(tl_period_saturated_counter_fails_set) {
    TL_REQUIRE();
    tl_kv *kv = calloc(1, sizeof *kv);
    EXPECT_EQ(tl_run("tl_period_saturate", "", 30000, kv), 0);
    if (tl_get(kv, "uncounted", 0)) { t_skip("period 6 uncounted here"); free(kv); return; }
    KV_EQ(kv, "done", 1);
    KV_EQ(kv, "begins", 65535);
    KV_EQ(kv, "begin_over", TL_NOCANDO);
    KV_EQ(kv, "set_sat", 0);
    KV_EQ(kv, "set_sat_os", 0);
    KV_EQ(kv, "set_other", 1);
    KV_EQ(kv, "kill_other", 0);
    KV_EQ(kv, "set_td", 1);
    KV_EQ(kv, "kill_td", 0);
    KV_EQ(kv, "end1", 0);
    KV_EQ(kv, "set_after", 1);
    KV_EQ(kv, "begin_full_again", TL_NOCANDO);
    KV_EQ(kv, "kill_after", 0);
    KV_EQ(kv, "ends", 65534);
    free(kv);
}

/* ---- the id-0 quirk takes a stale slot's period ---------------------------------------------- */
/* After slot 0 is freed its stored period stays; timeKillEvent(0) matches the empty slot (id 0) and
 * calls timeEndPeriod with that stale period, consuming somebody else's reference. */
CHILD(tl_period_kill0_stale) {
    TL_CHILD_BEGIN();
    UINT p = 4;
    UINT a = TSE(1000, p, pe_nop, 0, TIME_PERIODIC);     /* slot 0 */
    UINT b = TSE(1000, p, pe_nop, 0, TIME_PERIODIC);     /* slot 1 */
    tl_rec("a_slot", a & 0xF); tl_rec("b_slot", b & 0xF);
    tl_rec("kill_a", TKE(a));
    tl_rec("refs_b", tl_refs(p, 8));                      /* 1: b's */
    tl_rec("kill0", TKE(0));                              /* takes period p from b */
    tl_rec("refs_after", tl_refs(p, 8));                  /* 0 */
    tl_rec("kill_b", TKE(b));                             /* b's own End fails silently */
    tl_rec("refs_end", tl_refs(p, 8));
    tl_rec("done", 1); tl_rec_close();
    return 0;
}
TEST(tl_period_kill0_consumes_stale_period) {
    TL_REQUIRE();
    tl_kv *kv = calloc(1, sizeof *kv);
    EXPECT_EQ(tl_run("tl_period_kill0_stale", "", 20000, kv), 0);
    KV_EQ(kv, "done", 1);
    KV_EQ(kv, "a_slot", 0); KV_EQ(kv, "b_slot", 1);
    KV_EQ(kv, "kill_a", 0);
    KV_EQ(kv, "refs_b", 1);
    KV_EQ(kv, "kill0", 0);
    KV_EQ(kv, "refs_after", 0);
    KV_EQ(kv, "kill_b", 0);
    KV_EQ(kv, "refs_end", 0);
    free(kv);
}
