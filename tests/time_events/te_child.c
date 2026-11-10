/* time_events: checks that need a fresh process (no timeSetEvent called yet, generation counter at 0).
 * Children return 0 on success or 100 + the number of the first failed step; the parent checks the exit code.
 * Decomp: before the first timeSetEvent the timer thread does not exist and timeKillEvent returns 97 for any id
 * (0x2e50: the "timer thread exists" handle is NULL). The generation counter starts at 0 and the first
 * allocation stores 0x10 + slot. A free slot holds id 0, so once a slot has been used (it then owns an NT timer
 * handle) timeKillEvent(0) on that free slot matches and returns the NtCancelTimer result (0), releasing a stale
 * period; that is why id 0 is only ever probed here, in throw-away processes.
 */
#include "te.h"
#include <tlhelp32.h>

#define STEP(n, cond) do { if (!(cond)) return 100 + (n); } while (0)

CHILD(te_fresh_kill_any_id) {
    if (!U(timeKillEvent)) return 99;
    STEP(1, te_kill(0) == TE_NOCANDO);
    STEP(2, te_kill(0x10) == TE_NOCANDO);
    STEP(3, te_kill(1) == TE_NOCANDO);
    STEP(4, te_kill(TE_UNKNOWN_ID) == TE_NOCANDO);
    STEP(5, te_kill(0xFFFFFFFFu) == TE_NOCANDO);
    STEP(6, te_kill(0) == TE_NOCANDO);
    return 0;
}

CHILD(te_fresh_first_ids) {
    if (!U(timeSetEvent)) return 99;
    UINT ids[TE_SLOTS];
    for (int i = 0; i < TE_SLOTS; i++) {
        ids[i] = te_set(TE_LONG, TE_COARSE, NULL, 0, TIME_ONESHOT);
        STEP(1 + i, ids[i] == 0x10u * (i + 1) + i);   /* 0x10, 0x21, 0x32, ... 0x10F */
    }
    STEP(20, te_set(TE_LONG, TE_COARSE, NULL, 0, TIME_ONESHOT) == 0);
    STEP(21, te_set(TE_LONG, TE_COARSE, NULL, 0, TIME_PERIODIC) == 0);
    STEP(22, te_kill(ids[5]) == 0);
    UINT n = te_set(TE_LONG, TE_COARSE, NULL, 0, TIME_ONESHOT);
    STEP(23, n == 0x10u * 17 + 5);                    /* failed calls did not bump the generation */
    for (int i = 0; i < TE_SLOTS; i++) if (i != 5) STEP(30 + i, te_kill(ids[i]) == 0);
    STEP(50, te_kill(n) == 0);
    STEP(51, te_kill(n) == TE_NOCANDO);
    STEP(52, te_kill(ids[0]) == TE_NOCANDO);
    return 0;
}

CHILD(te_fresh_rejected_first) {
    if (!U(timeSetEvent)) return 99;
    /* rejected calls before the first good one: no thread, no generation bump */
    STEP(1, te_set(0, 0, NULL, 0, 0) == 0);
    STEP(2, te_set(TE_LONG, 0, NULL, 0, 2) == 0);
    STEP(3, te_kill(0) == TE_NOCANDO);
    UINT id = te_set(TE_LONG, TE_COARSE, NULL, 0, TIME_ONESHOT);
    STEP(4, id == 0x10);
    STEP(5, te_kill(id) == 0);
    return 0;
}

CHILD(te_fresh_generation_wraps) {
    if (!U(timeSetEvent)) return 99;
    /* 16-bit generation: slot-0 ids 0x10, 0x20, ... 0xFFF0, then 0x10 again (0 is skipped) */
    UINT expect = 0x10;
    for (int i = 0; i < 4096 + 8; i++) {
        UINT id = te_set(TE_LONG, TE_COARSE, NULL, 0, TIME_ONESHOT);
        STEP(1, id != 0);
        STEP(2, id == expect);
        STEP(3, te_kill(id) == 0);
        expect = expect == 0xFFF0 ? 0x10 : expect + 0x10;
    }
    return 0;
}

CHILD(te_fresh_wrap_with_slot) {
    if (!U(timeSetEvent)) return 99;
    /* keep slot 0 busy, cycle slot 1 across the wrap: ids stay nonzero, slot-tagged, below 0x10000 */
    UINT keep = te_set(TE_LONG, TE_COARSE, NULL, 0, TIME_PERIODIC);
    STEP(1, keep == 0x10);
    UINT prev = 0;
    for (int i = 0; i < 4100; i++) {
        UINT id = te_set(TE_LONG, TE_COARSE, NULL, 0, TIME_ONESHOT);
        STEP(2, id != 0 && (id & 0xF) == 1 && id < 0x10000);
        STEP(3, id != keep);
        STEP(4, te_kill(id) == 0);
        prev = id;
    }
    STEP(5, te_kill(keep) == 0);
    (void)prev;
    return 0;
}

CHILD(te_fresh_kill_zero_hazard) {
    if (!U(timeSetEvent)) return 99;
    /* slot 0 live: id 0 does not match it */
    UINT a = te_set(TE_LONG, TE_COARSE, NULL, 0, TIME_ONESHOT);
    STEP(1, a == 0x10);
    STEP(2, te_kill(0) == TE_NOCANDO);
    /* slot 1 never used (no NT timer yet): 1 does not match, 0x11 neither */
    STEP(3, te_kill(1) == TE_NOCANDO);
    STEP(4, te_kill(0x11) == TE_NOCANDO);
    STEP(5, te_kill(a) == 0);
    /* slot 0 now free (id field 0) and owns a timer handle: timeKillEvent(0) "succeeds" */
    STEP(6, te_kill(0) == 0);
    STEP(7, te_kill(0) == 0);
    /* a real id is still matched exactly afterwards */
    UINT b = te_set(TE_LONG, TE_COARSE, NULL, 0, TIME_ONESHOT);
    STEP(8, b == 0x20);
    STEP(9, te_kill(0) == TE_NOCANDO);
    STEP(10, te_kill(b) == 0);
    return 0;
}

CHILD(te_fresh_exit_with_live_periodic) {
    if (!U(timeSetEvent)) return 99;
    /* the process exits normally while a periodic event and its callback thread are live */
    static te_ctx c; te_ctx_init(&c);
    UINT id = te_set(5, 1, te_cb_ctx, (DWORD_PTR)&c, TIME_PERIODIC);
    STEP(1, id != 0);
    STEP(2, te_wait_count(&c, 3, 2000) >= 3);
    return 42;
}

CHILD(te_fresh_exit_during_callback) {
    if (!U(timeSetEvent)) return 99;
    static te_ctx c; te_ctx_init(&c);
    c.sleep_ms = 5000; c.entered = CreateEventA(NULL, TRUE, FALSE, NULL);
    UINT id = te_set(5, 1, te_cb_ctx, (DWORD_PTR)&c, TIME_ONESHOT);
    STEP(1, id != 0);
    STEP(2, WaitForSingleObject(c.entered, 2000) == WAIT_OBJECT_0);
    ExitProcess(43);   /* terminates the sleeping timer thread with the process */
}

CHILD(te_fresh_thread_priority) {
    if (!U(timeSetEvent)) return 99;
    static te_ctx c; te_ctx_init(&c);
    UINT id = te_set(5, 1, te_cb_ctx, (DWORD_PTR)&c, TIME_ONESHOT);
    STEP(1, id == 0x10);
    STEP(2, WaitForSingleObject(c.fired, 2000) == WAIT_OBJECT_0);
    STEP(3, c.prio == THREAD_PRIORITY_TIME_CRITICAL);
    STEP(4, c.tid != GetCurrentThreadId());
    STEP(5, c.id == 0x10);
    return 0;
}

CHILD(te_fresh_thread_created_lazily) {
    if (!U(timeSetEvent) || !U(timeGetTime)) return 99;
    /* loading winmm and using other time* exports creates no thread; the first timeSetEvent creates one */
    DWORD pid = GetCurrentProcessId();
    U(timeGetTime)();
    int before = 0, after = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return 98;
    THREADENTRY32 te; te.dwSize = sizeof te;
    if (Thread32First(snap, &te)) do { if (te.th32OwnerProcessID == pid) before++; } while (Thread32Next(snap, &te));
    CloseHandle(snap);
    UINT id = te_set(TE_LONG, TE_COARSE, NULL, 0, TIME_ONESHOT);
    STEP(1, id != 0);
    snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return 98;
    te.dwSize = sizeof te;
    if (Thread32First(snap, &te)) do { if (te.th32OwnerProcessID == pid) after++; } while (Thread32Next(snap, &te));
    CloseHandle(snap);
    STEP(2, after >= before + 1);
    STEP(3, te_kill(id) == 0);
    /* the thread stays: a second event creates no more threads */
    UINT id2 = te_set(TE_LONG, TE_COARSE, NULL, 0, TIME_ONESHOT);
    int after2 = 0;
    snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return 98;
    te.dwSize = sizeof te;
    if (Thread32First(snap, &te)) do { if (te.th32OwnerProcessID == pid) after2++; } while (Thread32Next(snap, &te));
    CloseHandle(snap);
    STEP(4, after2 == after);
    STEP(5, te_kill(id2) == 0);
    return 0;
}

static void te_child_case(const char *name, DWORD want) {
    DWORD code = t_run_child(name, NULL, 20000);
    CHECKF(code == want, "child %s exit %lu (want %lu; 100+N = step N failed)", name, code, want);
}

TEST(te_child_fresh_kill_any_id) { TE_REQUIRE(); te_child_case("te_fresh_kill_any_id", 0); }
TEST(te_child_fresh_first_ids) { TE_REQUIRE(); te_child_case("te_fresh_first_ids", 0); }
TEST(te_child_fresh_rejected_first) { TE_REQUIRE(); te_child_case("te_fresh_rejected_first", 0); }
TEST(te_child_fresh_generation_wraps) { TE_REQUIRE(); te_child_case("te_fresh_generation_wraps", 0); }
TEST(te_child_fresh_wrap_with_slot) { TE_REQUIRE(); te_child_case("te_fresh_wrap_with_slot", 0); }
TEST(te_child_fresh_kill_zero_hazard) { TE_REQUIRE(); te_child_case("te_fresh_kill_zero_hazard", 0); }
TEST(te_child_fresh_exit_with_live_periodic) { TE_REQUIRE(); te_child_case("te_fresh_exit_with_live_periodic", 42); }
TEST(te_child_fresh_exit_during_callback) { TE_REQUIRE(); te_child_case("te_fresh_exit_during_callback", 43); }
TEST(te_child_fresh_thread_priority) { TE_REQUIRE(); te_child_case("te_fresh_thread_priority", 0); }
TEST(te_child_fresh_thread_created_lazily) { TE_REQUIRE(); te_child_case("te_fresh_thread_created_lazily", 0); }
