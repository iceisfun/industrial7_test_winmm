/* time_event_lifetime: the 16-slot table, id allocation and slot reuse.
 * Every scenario runs in a CHILD process so the slot table starts empty and no other test's timer
 * perturbs it. VM_TIMEOUT: 150
 */
#include "tl.h"

static void CALLBACK cap_nop(UINT id, UINT msg, DWORD_PTR user, DWORD_PTR d1, DWORD_PTR d2) { (void)id; (void)msg; (void)user; (void)d1; (void)d2; }
static volatile LONG cap_hits;
static void CALLBACK cap_count(UINT id, UINT msg, DWORD_PTR user, DWORD_PTR d1, DWORD_PTR d2) { InterlockedIncrement(&cap_hits); }

/* ---- child: fill the table -------------------------------------------------------------------- */
/* argv[2] = fuEvent kind: "fn" periodic function, "ev" periodic EVENT_SET, "os" one-shot function */
CHILD(tl_cap_fill) {
    TL_CHILD_BEGIN();
    const char *kind = argc > 2 ? argv[2] : "fn";
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    UINT flags = TIME_PERIODIC; LPTIMECALLBACK cb = cap_nop;
    if (!strcmp(kind, "ev")) { flags = TIME_PERIODIC | TIME_CALLBACK_EVENT_SET; cb = (LPTIMECALLBACK)ev; }
    if (!strcmp(kind, "os")) { flags = TIME_ONESHOT; }
    if (!strcmp(kind, "ks")) { flags = TIME_PERIODIC | TIME_KILL_SYNCHRONOUS; }
    UINT ids[20] = {0};
    for (int i = 0; i < 20; i++) { ids[i] = TSE(10000, 0, cb, (DWORD_PTR)i, flags); tl_recf(ids[i], "id%d", i); }
    /* the 17th..20th fail; the table is full for every callback kind */
    tl_rec("full_periodic", TSE(10000, 0, cap_nop, 0, TIME_PERIODIC));
    tl_rec("full_oneshot", TSE(10000, 0, cap_nop, 0, TIME_ONESHOT));
    tl_rec("full_evset", TSE(10000, 0, (LPTIMECALLBACK)ev, 0, TIME_CALLBACK_EVENT_SET));
    tl_rec("full_short", TSE(1, 1, cap_nop, 0, TIME_ONESHOT));
    tl_rec("full_ksync", TSE(10000, 0, cap_nop, 0, TIME_PERIODIC | TIME_KILL_SYNCHRONOUS));
    /* timeKillEvent(0) while slot 0 is occupied does not match an empty slot -> 97 */
    tl_rec("kill0_full", TKE(0));
    /* kill slot 7, a new event takes slot 7 with a new generation */
    UINT k7 = ids[7];
    tl_rec("kill7", TKE(k7));
    tl_rec("kill7_again", TKE(k7));
    UINT n1 = TSE(10000, 0, cb, 0, flags);
    tl_rec("new_after_kill", n1);
    tl_rec("new_after_kill_full", TSE(10000, 0, cap_nop, 0, TIME_PERIODIC));
    tl_rec("kill_old7_after_reuse", TKE(k7));
    /* kill three, three succeed, the fourth fails */
    UINT k[3] = { ids[0], ids[15], ids[9] };
    for (int i = 0; i < 3; i++) tl_recf(TKE(k[i]), "kill3_%d", i);
    UINT m[4];
    for (int i = 0; i < 4; i++) { m[i] = TSE(10000, 0, cb, 0, flags); tl_recf(m[i], "re%d", i); }
    /* tear down: every live id kills with 0, then again with 97 */
    UINT live[16]; int nl = 0;
    for (int i = 0; i < 16; i++) if (i != 7 && i != 0 && i != 15 && i != 9) live[nl++] = ids[i];
    live[nl++] = n1; for (int i = 0; i < 3; i++) live[nl++] = m[i];
    tl_rec("nlive", nl);
    for (int i = 0; i < nl; i++) tl_recf(TKE(live[i]), "fin%d", i);
    for (int i = 0; i < nl; i++) tl_recf(TKE(live[i]), "fin2_%d", i);
    /* after teardown, 16 fit again */
    UINT again[16]; int ok = 0;
    for (int i = 0; i < 16; i++) { again[i] = TSE(10000, 0, cap_nop, 0, TIME_PERIODIC); ok += again[i] != 0; }
    tl_rec("refill_ok", ok);
    tl_rec("refill_17", TSE(10000, 0, cap_nop, 0, TIME_PERIODIC));
    int kz = 0; for (int i = 0; i < 16; i++) kz += TKE(again[i]) == 0;
    tl_rec("refill_kills", kz);
    CloseHandle(ev);
    tl_rec("done", 1); tl_rec_close();
    return 0;
}

static void cap_fill_check(const char *kind) {
    tl_kv *kv = calloc(1, sizeof *kv);
    DWORD code = tl_run("tl_cap_fill", kind, 30000, kv);
    EXPECT_EQ(code, 0);
    KV_EQ(kv, "done", 1);
    UINT ids[20];
    for (int i = 0; i < 20; i++) ids[i] = (UINT)tl_getf(kv, TL_MISSING, "id%d", i);
    for (int i = 0; i < 16; i++) {
        CHECKF(ids[i] != 0, "kind %s id%d", kind, i);
        CHECKF(ids[i] < 0x10000, "id 0x%x fits 16 bits (16-bit generation + slot)", ids[i]);
        for (int j = 0; j < i; j++) CHECKF(ids[i] != ids[j], "kind %s id%d == id%d", kind, i, j);
    }
    for (int i = 16; i < 20; i++) CHECKF(ids[i] == 0, "kind %s id%d = 0x%x (17th+ must fail)", kind, i, ids[i]);
    KV_EQ(kv, "full_periodic", 0);
    KV_EQ(kv, "full_oneshot", 0);
    KV_EQ(kv, "full_evset", 0);
    KV_EQ(kv, "full_short", 0);
    KV_EQ(kv, "full_ksync", 0);
    KV_EQ(kv, "kill0_full", TL_NOCANDO);
    KV_EQ(kv, "kill7", 0);
    KV_EQ(kv, "kill7_again", TL_NOCANDO);
    long long n1 = tl_get(kv, "new_after_kill", TL_MISSING);
    CHECK(n1 != 0 && n1 != TL_MISSING);
    EXPECT_EQ((UINT)n1 & 0xF, ids[7] & 0xF);          /* the freed slot is reused */
    EXPECT_NE((UINT)n1, ids[7]);                      /* ...under a new generation */
    for (int i = 0; i < 16; i++) CHECKF((UINT)n1 != ids[i], "new id 0x%llx collides with id%d", n1, i);
    KV_EQ(kv, "new_after_kill_full", 0);
    KV_EQ(kv, "kill_old7_after_reuse", TL_NOCANDO); /* the stale id does not reach the new occupant */
    for (int i = 0; i < 3; i++) KV_EQ(kv, (i == 0 ? "kill3_0" : i == 1 ? "kill3_1" : "kill3_2"), 0);
    for (int i = 0; i < 3; i++) {
        long long r = tl_getf(kv, TL_MISSING, "re%d", i);
        CHECKF(r != 0 && r != TL_MISSING, "re%d", i);
        for (int j = 0; j < 16; j++) CHECKF((UINT)r != ids[j], "re%d reuses id%d", i, j);
    }
    KV_EQ(kv, "re3", 0);
    long long nl = tl_get(kv, "nlive", 0);
    EXPECT_EQ(nl, 16);
    for (int i = 0; i < nl; i++) { CHECKF(tl_getf(kv, -1, "fin%d", i) == 0, "fin%d", i); CHECKF(tl_getf(kv, -1, "fin2_%d", i) == TL_NOCANDO, "fin2_%d", i); }
    KV_EQ(kv, "refill_ok", 16);
    KV_EQ(kv, "refill_17", 0);
    KV_EQ(kv, "refill_kills", 16);
    free(kv);
}
TEST(tl_cap_16_periodic_function) { TL_REQUIRE(); cap_fill_check("fn"); }
TEST(tl_cap_16_periodic_eventset) { TL_REQUIRE(); cap_fill_check("ev"); }
TEST(tl_cap_16_oneshot_long) { TL_REQUIRE(); cap_fill_check("os"); }
TEST(tl_cap_16_kill_synchronous) { TL_REQUIRE(); cap_fill_check("ks"); }

/* ---- child: id layout ------------------------------------------------------------------------- */
/* In a fresh process the generation starts at 0x10 and steps 0x10 per allocation; slot = lowest free. */
CHILD(tl_cap_layout) {
    TL_CHILD_BEGIN();
    UINT ids[16];
    for (int i = 0; i < 16; i++) { ids[i] = TSE(10000, 0, cap_nop, 0, TIME_PERIODIC); tl_recf(ids[i], "a%d", i); }
    /* failed 17th: no generation consumed */
    tl_rec("a16", TSE(10000, 0, cap_nop, 0, TIME_PERIODIC));
    /* free slots 3, 11, 5 (in that order); refill picks the lowest free slot first */
    TKE(ids[3]); TKE(ids[11]); TKE(ids[5]);
    for (int i = 0; i < 3; i++) tl_recf(TSE(10000, 0, cap_nop, 0, TIME_PERIODIC), "b%d", i);
    /* invalid calls (bad flags, delay 0, delay > 1000000) do not consume a generation */
    TKE(ids[0]);
    tl_rec("bad_flag", TSE(10000, 0, cap_nop, 0, 0x200));
    tl_rec("bad_delay0", TSE(0, 0, cap_nop, 0, TIME_PERIODIC));
    tl_rec("bad_delaybig", TSE(1000001, 0, cap_nop, 0, TIME_PERIODIC));
    tl_rec("c0", TSE(10000, 0, cap_nop, 0, TIME_PERIODIC));
    /* tear down everything that is live */
    for (int i = 0; i < 16; i++) TKE(ids[i]);
    tl_rec("done", 1); tl_rec_close();
    return 0; /* process exit reclaims the b and c events */
}
TEST(tl_cap_id_layout_fresh_process) {
    TL_REQUIRE();
    tl_kv *kv = calloc(1, sizeof *kv);
    EXPECT_EQ(tl_run("tl_cap_layout", "", 30000, kv), 0);
    KV_EQ(kv, "done", 1);
    UINT a0 = (UINT)tl_get(kv, "a0", 0);
    t_note("first id in a fresh process = 0x%x", a0);
    for (int i = 0; i < 16; i++) {
        UINT id = (UINT)tl_getf(kv, 0, "a%d", i);
        CHECKF((id & 0xF) == (UINT)i, "a%d = 0x%x: slot %d expected", i, id, i);
        CHECKF((id & ~0xFu) == 0x10u * (i + 1), "a%d = 0x%x: generation 0x%x expected", i, id, 0x10 * (i + 1));
    }
    KV_EQ(kv, "a16", 0);
    /* b: slots 3, 5, 11 in ascending order, generations 17, 18, 19 (the failed 17th consumed none) */
    UINT b0 = (UINT)tl_get(kv, "b0", 0), b1 = (UINT)tl_get(kv, "b1", 0), b2 = (UINT)tl_get(kv, "b2", 0);
    EXPECT_EQ(b0, 0x110 + 3);
    EXPECT_EQ(b1, 0x120 + 5);
    EXPECT_EQ(b2, 0x130 + 11);
    KV_EQ(kv, "bad_flag", 0);
    KV_EQ(kv, "bad_delay0", 0);
    KV_EQ(kv, "bad_delaybig", 0);
    EXPECT_EQ(tl_get(kv, "c0", 0), 0x140 + 0);
    free(kv);
}

/* ---- child: generation wrap ------------------------------------------------------------------- */
/* 4096 allocations wrap the 16-bit generation; 0 is skipped so ids stay nonzero. */
CHILD(tl_cap_wrap) {
    TL_CHILD_BEGIN();
    int zero = 0, bad = 0; UINT first = 0, prev = 0, minid = 0xFFFFFFFF, maxid = 0;
    for (int i = 0; i < 4200; i++) {
        UINT id = TSE(10000, 0, cap_nop, 0, TIME_PERIODIC);
        if (!id) { zero++; continue; }
        if (!first) first = id;
        if ((id & 0xF) != 0) bad++;
        if (id < minid) minid = id;
        if (id > maxid) maxid = id;
        if (i == 4094) tl_rec("id4094", id);
        if (i == 4095) tl_rec("id4095", id);
        if (i == 4096) tl_rec("id4096", id);
        if (TKE(id) != 0) bad++;
        prev = id;
    }
    tl_rec("first", first); tl_rec("zero", zero); tl_rec("bad", bad);
    tl_rec("minid", minid); tl_rec("maxid", maxid);
    tl_rec("done", 1); tl_rec_close();
    return 0;
}
TEST(tl_cap_generation_wraps_skipping_zero) {
    TL_REQUIRE();
    tl_kv *kv = calloc(1, sizeof *kv);
    EXPECT_EQ(tl_run("tl_cap_wrap", "", 60000, kv), 0);
    KV_EQ(kv, "done", 1);
    KV_EQ(kv, "zero", 0);
    KV_EQ(kv, "bad", 0);
    KV_EQ(kv, "first", 0x10);
    KV_EQ(kv, "minid", 0x10);
    KV_EQ(kv, "maxid", 0xFFF0);
    KV_EQ(kv, "id4094", 0xFFF0);   /* allocation 4095 has generation 0xFFF0 */
    KV_EQ(kv, "id4095", 0x10);     /* 0xFFF0 + 0x10 wraps to 0, which is skipped */
    KV_EQ(kv, "id4096", 0x20);
    free(kv);
}

/* ---- child: timeKillEvent before the timer thread exists, and the id-0 quirk ------------------ */
CHILD(tl_cap_kill_fresh) {
    TL_CHILD_BEGIN();
    static const UINT probe[] = { 0, 1, 0x10, 0x11, 0x12345, 0xFFFF, 0xFFFFFFFF };
    for (int i = 0; i < (int)(sizeof probe / sizeof probe[0]); i++) tl_recf(TKE(probe[i]), "fresh%d", i);
    /* create and kill one event: the thread now exists and slot 0 is empty again */
    UINT id = TSE(10000, 0, cap_nop, 0, TIME_PERIODIC);
    tl_rec("id", id);
    tl_rec("kill", TKE(id));
    tl_rec("kill_again", TKE(id));
    tl_rec("kill_12345", TKE(0x12345));
    tl_rec("kill_ffff", TKE(0xFFFF));
    tl_rec("kill_slot0_gen_other", TKE(0x20));
    /* an empty slot holds id 0, so timeKillEvent(0) matches it */
    tl_rec("kill0_after", TKE(0));
    tl_rec("done", 1); tl_rec_close();
    return 0;
}
TEST(tl_cap_kill_before_and_after_thread) {
    TL_REQUIRE();
    tl_kv *kv = calloc(1, sizeof *kv);
    EXPECT_EQ(tl_run("tl_cap_kill_fresh", "", 20000, kv), 0);
    KV_EQ(kv, "done", 1);
    for (int i = 0; i < 7; i++) CHECKF(tl_getf(kv, -1, "fresh%d", i) == TL_NOCANDO, "fresh%d", i);
    CHECK(tl_get(kv, "id", 0) != 0);
    KV_EQ(kv, "kill", 0);
    KV_EQ(kv, "kill_again", TL_NOCANDO);
    KV_EQ(kv, "kill_12345", TL_NOCANDO);
    KV_EQ(kv, "kill_ffff", TL_NOCANDO);
    KV_EQ(kv, "kill_slot0_gen_other", TL_NOCANDO);
    KV_EQ(kv, "kill0_after", 0);
    free(kv);
}

/* ---- child: a failed 17th set does not leak a timeBeginPeriod reference ----------------------- */
CHILD(tl_cap_period_leak) {
    TL_CHILD_BEGIN();
    UINT p = 1 < tl_pmin() ? tl_pmin() : 1;
    tl_rec("pre_end", TEP(p));                 /* no reference yet */
    UINT ids[16];
    for (int i = 0; i < 16; i++) ids[i] = TSE(10000, p, cap_nop, 0, TIME_PERIODIC);
    int ok = 0; for (int i = 0; i < 16; i++) ok += ids[i] != 0; tl_rec("ok", ok);
    for (int i = 0; i < 5; i++) tl_recf(TSE(10000, p, cap_nop, 0, TIME_PERIODIC), "over%d", i);
    tl_rec("refs16", tl_refs(p, 40));          /* exactly one per live event */
    for (int i = 0; i < 16; i++) TKE(ids[i]);
    tl_rec("refs0", tl_refs(p, 40));
    tl_rec("post_end", TEP(p));
    tl_rec("done", 1); tl_rec_close();
    return 0;
}
TEST(tl_cap_failed_set_releases_period) {
    TL_REQUIRE();
    tl_kv *kv = calloc(1, sizeof *kv);
    EXPECT_EQ(tl_run("tl_cap_period_leak", "", 20000, kv), 0);
    KV_EQ(kv, "done", 1);
    KV_EQ(kv, "pre_end", TL_NOCANDO);
    KV_EQ(kv, "ok", 16);
    for (int i = 0; i < 5; i++) CHECKF(tl_getf(kv, -1, "over%d", i) == 0, "over%d", i);
    KV_EQ(kv, "refs16", 16);
    KV_EQ(kv, "refs0", 0);
    KV_EQ(kv, "post_end", TL_NOCANDO);
    free(kv);
}

/* ---- child: one-shot completion frees its slot ------------------------------------------------- */
CHILD(tl_cap_oneshot_frees) {
    TL_CHILD_BEGIN();
    UINT ids[16];
    for (int i = 0; i < 15; i++) ids[i] = TSE(10000, 0, cap_nop, 0, TIME_PERIODIC);
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    UINT os = TSE(20, 0, (LPTIMECALLBACK)ev, 0, TIME_ONESHOT | TIME_CALLBACK_EVENT_SET);
    tl_rec("os", os);
    tl_rec("full_before", TSE(10000, 0, cap_nop, 0, TIME_PERIODIC));
    tl_rec("wait", WaitForSingleObject(ev, 3000));
    /* the slot is freed under the timer lock right after the callback; allow the worker to finish */
    UINT n = 0;
    for (int i = 0; i < 200 && !n; i++) { n = TSE(10000, 0, cap_nop, 0, TIME_PERIODIC); if (!n) Sleep(10); }
    tl_rec("after", n);
    tl_rec("after_slot", n & 0xF);
    tl_rec("kill_fired", TKE(os));
    tl_rec("full_after", TSE(10000, 0, cap_nop, 0, TIME_PERIODIC));
    for (int i = 0; i < 15; i++) TKE(ids[i]);
    TKE(n);
    CloseHandle(ev);
    tl_rec("done", 1); tl_rec_close();
    return 0;
}
TEST(tl_cap_oneshot_completion_frees_slot) {
    TL_REQUIRE();
    tl_kv *kv = calloc(1, sizeof *kv);
    EXPECT_EQ(tl_run("tl_cap_oneshot_frees", "", 20000, kv), 0);
    KV_EQ(kv, "done", 1);
    long long os = tl_get(kv, "os", 0);
    CHECK(os != 0);
    EXPECT_EQ(os & 0xF, 15);
    KV_EQ(kv, "full_before", 0);
    KV_EQ(kv, "wait", WAIT_OBJECT_0);
    CHECK(tl_get(kv, "after", 0) != 0);
    KV_EQ(kv, "after_slot", 15);
    KV_EQ(kv, "kill_fired", TL_NOCANDO);
    KV_EQ(kv, "full_after", 0);
    free(kv);
}

/* ---- child: process-local table --------------------------------------------------------------- */
/* Two children each fill 16 slots at the same time: the cap is per process. */
CHILD(tl_cap_hold) {
    TL_CHILD_BEGIN();
    HANDLE go = OpenEventA(SYNCHRONIZE, FALSE, argc > 2 ? argv[2] : "x");
    UINT ids[16]; int ok = 0;
    for (int i = 0; i < 16; i++) { ids[i] = TSE(10000, 0, cap_nop, 0, TIME_PERIODIC); ok += ids[i] != 0; }
    tl_rec("ok", ok);
    tl_rec("over", TSE(10000, 0, cap_nop, 0, TIME_PERIODIC));
    tl_rec("go", go ? WaitForSingleObject(go, 10000) : 99);
    int kz = 0; for (int i = 0; i < 16; i++) kz += TKE(ids[i]) == 0;
    tl_rec("kills", kz);
    if (go) CloseHandle(go);
    tl_rec("done", 1); tl_rec_close();
    return 0;
}
TEST(tl_cap_is_per_process) {
    TL_REQUIRE();
    char name[64]; snprintf(name, sizeof name, "tl_cap_go_%lu", GetCurrentProcessId());
    HANDLE go = CreateEventA(NULL, TRUE, FALSE, name);
    REQUIRE(go);
    char p1[MAX_PATH], p2[MAX_PATH], a1[MAX_PATH + 100], a2[MAX_PATH + 100];
    t_tmpfile(p1, sizeof p1, "hold1.txt"); t_tmpfile(p2, sizeof p2, "hold2.txt");
    snprintf(a1, sizeof a1, "\"%s\" %s", p1, name); snprintf(a2, sizeof a2, "\"%s\" %s", p2, name);
    PROCESS_INFORMATION pi1, pi2;
    HANDLE h1 = t_spawn_child("tl_cap_hold", a1, 0, NULL, NULL, NULL, FALSE, NULL, &pi1);
    HANDLE h2 = t_spawn_child("tl_cap_hold", a2, 0, NULL, NULL, NULL, FALSE, NULL, &pi2);
    CHECK(h1 && h2);
    /* the parent itself can still allocate while both children hold 16 each */
    Sleep(500);
    UINT mine = TSE(10000, 0, cap_nop, 0, TIME_PERIODIC);
    CHECK(mine != 0);
    EXPECT_EQ(TKE(mine), 0);
    SetEvent(go);
    DWORD c1 = 0xDEAD, c2 = 0xDEAD;
    if (h1) { if (WaitForSingleObject(h1, 20000) != WAIT_OBJECT_0) TerminateProcess(h1, 1); GetExitCodeProcess(h1, &c1); CloseHandle(h1); CloseHandle(pi1.hThread); }
    if (h2) { if (WaitForSingleObject(h2, 20000) != WAIT_OBJECT_0) TerminateProcess(h2, 1); GetExitCodeProcess(h2, &c2); CloseHandle(h2); CloseHandle(pi2.hThread); }
    EXPECT_EQ(c1, 0); EXPECT_EQ(c2, 0);
    const char *ps[2] = { p1, p2 };
    for (int k = 0; k < 2; k++) {
        FILE *f = fopen(ps[k], "r"); CHECK(f != NULL); if (!f) continue;
        char key[80]; long long v; int ok = -1, over = -1, gw = -1, kills = -1;
        while (fscanf(f, "%79s %lld", key, &v) == 2) {
            if (!strcmp(key, "ok")) ok = (int)v; else if (!strcmp(key, "over")) over = (int)v;
            else if (!strcmp(key, "go")) gw = (int)v; else if (!strcmp(key, "kills")) kills = (int)v;
        }
        fclose(f);
        EXPECT_EQ(ok, 16); EXPECT_EQ(over, 0); EXPECT_EQ(gw, WAIT_OBJECT_0); EXPECT_EQ(kills, 16);
    }
    CloseHandle(go);
}
