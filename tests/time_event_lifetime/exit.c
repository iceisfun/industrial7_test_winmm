/* time_event_lifetime: process exit and module lifetime with live events (all in CHILD processes).
 * TimeInitThread (winmm 0x2d7c) pins winmm with an extra LoadLibraryEx of its own path (LoadWINMM
 * 0x3104) before it creates the worker, so the module cannot be unloaded under a running worker.
 * A process may exit with events live: the worker is terminated with the process, exit code intact.
 */
#include "tl.h"

static volatile LONG ex_hits;
static HANDLE ex_ev;
static void CALLBACK ex_cb(UINT id, UINT msg, DWORD_PTR user, DWORD_PTR d1, DWORD_PTR d2) { InterlockedIncrement(&ex_hits); }
static void CALLBACK ex_slow_cb(UINT id, UINT msg, DWORD_PTR user, DWORD_PTR d1, DWORD_PTR d2) {
    InterlockedIncrement(&ex_hits); SetEvent(ex_ev); Sleep(2000);
}
static void CALLBACK ex_exit_cb(UINT id, UINT msg, DWORD_PTR user, DWORD_PTR d1, DWORD_PTR d2) {
    if (InterlockedIncrement(&ex_hits) == 3) ExitProcess((UINT)user);
}

/* argv[2] = scenario */
CHILD(tl_exit_live) {
    TL_CHILD_BEGIN();
    const char *sc = argc > 2 ? argv[2] : "periodic";
    ex_ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!strcmp(sc, "periodic")) {
        UINT id = TSE(10, 1, ex_cb, 0, TIME_PERIODIC);
        tl_rec("id", id != 0); tl_rec("hits3", tl_wait_ge(&ex_hits, 3, 3000));
        tl_rec_close(); return 0x5A;                         /* return from main with the timer live */
    }
    if (!strcmp(sc, "periodic_sync")) {
        UINT id = TSE(10, 1, ex_cb, 0, TIME_PERIODIC | TIME_KILL_SYNCHRONOUS);
        tl_rec("id", id != 0); tl_rec("hits3", tl_wait_ge(&ex_hits, 3, 3000));
        tl_rec_close(); ExitProcess(0x5B);
    }
    if (!strcmp(sc, "full16")) {
        int ok = 0; for (int i = 0; i < 16; i++) ok += TSE(1 + i, 1, ex_cb, 0, TIME_PERIODIC) != 0;
        tl_rec("ok", ok); tl_rec("hits3", tl_wait_ge(&ex_hits, 30, 3000));
        tl_rec_close(); return 0x5C;
    }
    if (!strcmp(sc, "inside_callback")) {
        /* main thread exits while the worker sleeps inside a callback */
        UINT id = TSE(5, 1, ex_slow_cb, 0, TIME_PERIODIC);
        tl_rec("id", id != 0); tl_rec("entered", WaitForSingleObject(ex_ev, 3000));
        tl_rec_close(); return 0x5D;
    }
    if (!strcmp(sc, "exit_from_callback")) {
        UINT id = TSE(5, 1, ex_exit_cb, 0x5E, TIME_PERIODIC);
        tl_rec("id", id != 0); tl_rec_close();
        Sleep(5000);
        return 0x77;                                         /* not reached: the callback exits the process */
    }
    if (!strcmp(sc, "eventset")) {
        HANDLE e = CreateEventA(NULL, FALSE, FALSE, NULL);
        UINT id = TSE(5, 1, (LPTIMECALLBACK)e, 0, TIME_PERIODIC | TIME_CALLBACK_EVENT_SET);
        tl_rec("id", id != 0); tl_rec("sig", WaitForSingleObject(e, 3000));
        tl_rec_close(); return 0x5F;
    }
    if (!strcmp(sc, "oneshot_pending")) {
        UINT id = TSE(100000, 1, ex_cb, 0, TIME_ONESHOT);
        tl_rec("id", id != 0); tl_rec_close(); return 0x60;
    }
    if (!strcmp(sc, "other_thread_exit")) {
        /* ExitProcess from the main thread while a worker thread of ours keeps creating events */
        UINT id = TSE(3, 1, ex_cb, 0, TIME_PERIODIC);
        tl_rec("id", id != 0); tl_rec("hits3", tl_wait_ge(&ex_hits, 3, 3000));
        tl_rec_close(); TerminateProcess(GetCurrentProcess(), 0x61); return 0x99;
    }
    tl_rec_close();
    return 0xBAD2;
}
static void ex_case(const char *sc, DWORD want_code, const char *k1, long long v1, const char *k2, long long v2) {
    tl_kv *kv = calloc(1, sizeof *kv);
    DWORD t0 = GetTickCount();
    DWORD code = tl_run("tl_exit_live", sc, 15000, kv);
    DWORD dt = GetTickCount() - t0;
    CHECKF(code == want_code, "scenario %s exit code 0x%lx want 0x%lx", sc, code, want_code);
    if (k1) { long long g = tl_get(kv, k1, TL_MISSING); CHECKF(g == v1, "scenario %s %s=%lld want %lld", sc, k1, g, v1); }
    if (k2) { long long g = tl_get(kv, k2, TL_MISSING); CHECKF(g == v2, "scenario %s %s=%lld want %lld", sc, k2, g, v2); }
    t_note("scenario %s exited in %lu ms", sc, dt);
    free(kv);
}
TEST(tl_exit_return_with_live_periodic) { TL_REQUIRE(); ex_case("periodic", 0x5A, "id", 1, "hits3", 1); }
TEST(tl_exit_exitprocess_with_live_sync_periodic) { TL_REQUIRE(); ex_case("periodic_sync", 0x5B, "id", 1, "hits3", 1); }
TEST(tl_exit_with_full_table) { TL_REQUIRE(); ex_case("full16", 0x5C, "ok", 16, "hits3", 1); }
TEST(tl_exit_while_callback_running) { TL_REQUIRE(); ex_case("inside_callback", 0x5D, "id", 1, "entered", WAIT_OBJECT_0); }
TEST(tl_exit_from_inside_callback) { TL_REQUIRE(); ex_case("exit_from_callback", 0x5E, "id", 1, NULL, 0); }
TEST(tl_exit_with_live_eventset) { TL_REQUIRE(); ex_case("eventset", 0x5F, "id", 1, "sig", WAIT_OBJECT_0); }
TEST(tl_exit_with_pending_oneshot) { TL_REQUIRE(); ex_case("oneshot_pending", 0x60, "id", 1, NULL, 0); }
TEST(tl_exit_terminate_with_live_periodic) { TL_REQUIRE(); ex_case("other_thread_exit", 0x61, "id", 1, "hits3", 1); }

/* ---- module pinning ----------------------------------------------------------------------- */
typedef MMRESULT (WINAPI *ex_tse_fn)(UINT, UINT, LPTIMECALLBACK, DWORD_PTR, UINT);
typedef MMRESULT (WINAPI *ex_tke_fn)(UINT);
/* argv[2]: "timer" (set+kill an event first) or "plain" */
CHILD(tl_exit_pin) {
    if (argc < 2) return 0xBAD;
    tl_rec_open(argv[1]);
    int timer = argc > 2 && !strcmp(argv[2], "timer");
    tl_rec("preloaded", GetModuleHandleA("winmm.dll") != NULL);
    HMODULE h = LoadLibraryA("winmm.dll");
    tl_rec("loaded", h != NULL);
    if (h && timer) {
        ex_tse_fn tse = (ex_tse_fn)GetProcAddress(h, "timeSetEvent");
        ex_tke_fn tke = (ex_tke_fn)GetProcAddress(h, "timeKillEvent");
        UINT id = tse ? tse(10000, 0, ex_cb, 0, TIME_PERIODIC) : 0;
        tl_rec("id", id != 0);
        tl_rec("kill", tke ? tke(id) : 99);
    }
    if (h) tl_rec("free", FreeLibrary(h) != 0);
    tl_rec("after", GetModuleHandleA("winmm.dll") != NULL);
    tl_rec("done", 1); tl_rec_close();
    return 0;
}
TEST(tl_exit_worker_pins_winmm) {
    TL_REQUIRE();
    tl_kv *kv = calloc(1, sizeof *kv);
    EXPECT_EQ(tl_run("tl_exit_pin", "timer", 15000, kv), 0);
    KV_EQ(kv, "done", 1);
    KV_EQ(kv, "preloaded", 0);
    KV_EQ(kv, "loaded", 1);
    KV_EQ(kv, "id", 1);
    KV_EQ(kv, "kill", 0);
    KV_EQ(kv, "free", 1);
    KV_EQ(kv, "after", 1);       /* the worker's own reference keeps winmm loaded */
    tl_kv *k2 = calloc(1, sizeof *k2);
    EXPECT_EQ(tl_run("tl_exit_pin", "plain", 15000, k2), 0);
    KV_EQ(k2, "done", 1);
    t_note("without a timer event, winmm after FreeLibrary still loaded = %lld (other loads may pin it)", tl_get(k2, "after", -1));
    free(kv); free(k2);
}

/* After the module's worker exists, events keep working across many set/kill cycles in a child that
 * also unloads and reloads its LoadLibrary reference. */
CHILD(tl_exit_reload) {
    if (argc < 2) return 0xBAD;
    tl_rec_open(argv[1]);
    int ok = 0;
    for (int round = 0; round < 5; round++) {
        HMODULE h = LoadLibraryA("winmm.dll");
        ex_tse_fn tse = (ex_tse_fn)GetProcAddress(h, "timeSetEvent");
        ex_tke_fn tke = (ex_tke_fn)GetProcAddress(h, "timeKillEvent");
        UINT id = tse(5, 1, ex_cb, 0, TIME_PERIODIC);
        if (id && tl_wait_ge(&ex_hits, (round + 1) * 2, 3000) && tke(id) == 0) ok++;
        Sleep(20);
        FreeLibrary(h);
    }
    tl_rec("ok", ok); tl_rec("done", 1); tl_rec_close();
    return 0;
}
TEST(tl_exit_reload_cycles) {
    TL_REQUIRE();
    tl_kv *kv = calloc(1, sizeof *kv);
    EXPECT_EQ(tl_run("tl_exit_reload", "", 20000, kv), 0);
    KV_EQ(kv, "done", 1);
    KV_EQ(kv, "ok", 5);
    free(kv);
}
