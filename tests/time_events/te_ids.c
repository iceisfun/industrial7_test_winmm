/* time_events: event ids, the 16-slot table and timeKillEvent's id lookup.
 * Decomp: timeThreadSetEvent (0x2b90) takes the lowest free slot of 16, bumps a 16-bit generation counter by 0x10
 * (skipping 0) and stores id = generation + slot; a full table returns 0 without bumping the generation.
 * timeKillEvent (0x2e50) looks at slot (id & 0xF) and requires slots[slot].id == id, else 97.
 * id 0 is never used as an "unknown id" here (a free slot holds 0; see te_child.c for the dedicated checks).
 */
#include "te.h"

TEST(te_ids_nonzero_and_slot_tagged) {
    TE_REQUIRE();
    UINT ids[TE_SLOTS];
    for (int i = 0; i < TE_SLOTS; i++) ids[i] = te_set(TE_LONG, TE_COARSE, NULL, 0, TIME_ONESHOT);
    unsigned slots = 0;
    for (int i = 0; i < TE_SLOTS; i++) {
        CHECKF(ids[i] != 0, "allocation %d", i);
        CHECKF(ids[i] < 0x10010, "id 0x%x above the 16-bit generation + slot range", ids[i]);
        CHECKF((ids[i] & ~0xFu) != 0, "id 0x%x has no generation", ids[i]);
        slots |= 1u << (ids[i] & 0xF);
    }
    EXPECT_EQ(slots, 0xFFFF);   /* 16 live events occupy 16 distinct slots */
    for (int i = 0; i < TE_SLOTS; i++) if (ids[i]) EXPECT_EQ(te_kill(ids[i]), 0);
}

TEST(te_ids_lowest_free_slot_in_order) {
    TE_REQUIRE();
    UINT ids[TE_SLOTS];
    for (int i = 0; i < TE_SLOTS; i++) {
        ids[i] = te_set(TE_LONG, TE_COARSE, NULL, 0, TIME_ONESHOT);
        EXPECT_EQ(ids[i] & 0xF, (UINT)i);
    }
    for (int i = 1; i < TE_SLOTS; i++) {
        /* consecutive allocations: generation + 0x10, slot + 1 (unless the 16-bit generation wrapped) */
        if (ids[i - 1] && ids[i] && ids[i] > ids[i - 1]) EXPECT_EQ(ids[i], ids[i - 1] + 0x11);
    }
    for (int i = 0; i < TE_SLOTS; i++) if (ids[i]) te_kill(ids[i]);
}

TEST(te_ids_seventeenth_fails) {
    TE_REQUIRE();
    UINT ids[TE_SLOTS];
    for (int i = 0; i < TE_SLOTS; i++) { ids[i] = te_set(TE_LONG, TE_COARSE, NULL, 0, TIME_ONESHOT); CHECK(ids[i] != 0); }
    EXPECT_EQ(te_set(TE_LONG, TE_COARSE, NULL, 0, TIME_ONESHOT), 0);
    EXPECT_EQ(te_set(TE_LONG, 0, NULL, 0, TIME_PERIODIC), 0);
    EXPECT_EQ(te_set(1, 1, NULL, 0, TIME_PERIODIC | TIME_CALLBACK_EVENT_SET), 0);
    for (int i = 0; i < TE_SLOTS; i++) if (ids[i]) EXPECT_EQ(te_kill(ids[i]), 0);
}

TEST(te_ids_full_table_failure_keeps_generation) {
    TE_REQUIRE();
    UINT ids[TE_SLOTS];
    for (int i = 0; i < TE_SLOTS; i++) ids[i] = te_set(TE_LONG, TE_COARSE, NULL, 0, TIME_ONESHOT);
    for (int k = 0; k < 5; k++) EXPECT_EQ(te_set(TE_LONG, TE_COARSE, NULL, 0, TIME_ONESHOT), 0);
    /* free slot 7; the next id is the last generation + 0x10, in slot 7 */
    UINT last = ids[TE_SLOTS - 1];
    CHECK(ids[7] != 0);
    EXPECT_EQ(te_kill(ids[7]), 0);
    UINT n = te_set(TE_LONG, TE_COARSE, NULL, 0, TIME_ONESHOT);
    EXPECT_EQ(n & 0xF, 7);
    if (last < 0xFFE0 && n) EXPECT_EQ(n & ~0xFu, (last & ~0xFu) + 0x10);
    if (n) EXPECT_EQ(te_kill(n), 0);
    for (int i = 0; i < TE_SLOTS; i++) if (i != 7 && ids[i]) EXPECT_EQ(te_kill(ids[i]), 0);
}

TEST(te_ids_freed_slot_reused_new_generation) {
    TE_REQUIRE();
    UINT a = te_set(TE_LONG, TE_COARSE, NULL, 0, TIME_ONESHOT);
    UINT b = te_set(TE_LONG, TE_COARSE, NULL, 0, TIME_ONESHOT);
    CHECK(a && b);
    EXPECT_EQ(a & 0xF, 0); EXPECT_EQ(b & 0xF, 1);
    EXPECT_EQ(te_kill(a), 0);
    UINT c = te_set(TE_LONG, TE_COARSE, NULL, 0, TIME_ONESHOT);
    EXPECT_EQ(c & 0xF, 0);
    CHECK(c != a);
    if (b < 0xFFE0) EXPECT_EQ(c, (b & ~0xFu) + 0x10);
    /* the stale id of the old occupant of slot 0 does not kill the new occupant */
    EXPECT_EQ(te_kill(a), TE_NOCANDO);
    EXPECT_EQ(te_kill(c), 0);
    EXPECT_EQ(te_kill(b), 0);
}

TEST(te_ids_sequence_of_reallocations) {
    TE_REQUIRE();
    UINT prev = te_probe_id();
    CHECK(prev != 0);
    for (int i = 0; i < 64; i++) {
        UINT id = te_probe_id();
        CHECK(id != 0);
        EXPECT_EQ(id & 0xF, 0);
        if (prev < 0xFFF0) EXPECT_EQ(id, prev + 0x10); else EXPECT_EQ(id, 0x10);
        prev = id;
    }
}

TEST(te_ids_not_reused_by_next_allocation) {
    TE_REQUIRE();
    UINT seen[200]; int n = 0;
    for (int i = 0; i < 200; i++) {
        UINT id = te_set(TE_LONG, TE_COARSE, NULL, 0, i & 1 ? TIME_PERIODIC : TIME_ONESHOT);
        CHECK(id != 0);
        for (int j = 0; j < n; j++) if (seen[j] == id) { CHECKF(0, "id 0x%x repeated after %d allocations", id, i - j); break; }
        seen[n++] = id;
        te_kill(id);
    }
}

/* ---------------------------------------------------------------- timeKillEvent id matching */

TEST(te_kill_unknown_ids_table) {
    TE_REQUIRE();
    UINT ids[] = { TE_UNKNOWN_ID, 1, 2, 0xF, 0x11, 0x105, 0x10000, 0x10010, 0x7FFFFFFF, 0x80000000u, 0xFFFFFFF0u,
                   0xFFFFFFFFu, 0xDEADBEEFu };
    /* nothing live: no slot carries any of these */
    EXPECT_EQ(te_free_slots(), TE_SLOTS);
    for (size_t i = 0; i < sizeof ids / sizeof ids[0]; i++) EXPECT_EQ(te_kill(ids[i]), TE_NOCANDO);
}

TEST(te_kill_id_with_high_bits_does_not_match) {
    TE_REQUIRE();
    UINT id = te_set(TE_LONG, TE_COARSE, NULL, 0, TIME_PERIODIC);
    CHECK(id != 0);
    if (!id) return;
    UINT variants[] = { id | 0x10000, id + 0x10000, id | 0x80000000u, id ^ 0x10, id + 0x10, id - 0x10 + 0x10000,
                        id ^ 0xFFFF0000u, (id & 0xF) };
    for (size_t i = 0; i < sizeof variants / sizeof variants[0]; i++) {
        if (variants[i] == id || variants[i] == 0) continue;
        EXPECT_EQ(te_kill(variants[i]), TE_NOCANDO);
    }
    /* still live */
    EXPECT_EQ(te_kill(id), 0);
    EXPECT_EQ(te_kill(id), TE_NOCANDO);
}

TEST(te_kill_neighbour_slot_ids) {
    TE_REQUIRE();
    UINT a = te_set(TE_LONG, TE_COARSE, NULL, 0, TIME_ONESHOT);
    UINT b = te_set(TE_LONG, TE_COARSE, NULL, 0, TIME_ONESHOT);
    CHECK(a && b);
    if (!a || !b) { if (a) te_kill(a); if (b) te_kill(b); return; }
    /* a's generation with b's slot, and b's generation with a's slot */
    UINT x = (a & ~0xFu) | (b & 0xF), y = (b & ~0xFu) | (a & 0xF);
    if (x != a && x != b) EXPECT_EQ(te_kill(x), TE_NOCANDO);
    if (y != a && y != b) EXPECT_EQ(te_kill(y), TE_NOCANDO);
    EXPECT_EQ(te_kill(b), 0);
    EXPECT_EQ(te_kill(a), 0);
}

TEST(te_kill_twice_periodic) {
    TE_REQUIRE();
    UINT id = te_set(50, 1, NULL, 0, TIME_PERIODIC);
    CHECK(id != 0);
    if (!id) return;
    EXPECT_EQ(te_kill(id), 0);
    EXPECT_EQ(te_kill(id), TE_NOCANDO);
    EXPECT_EQ(te_kill(id), TE_NOCANDO);
}

TEST(te_kill_pending_oneshot) {
    TE_REQUIRE();
    UINT id = te_set(5000, 1, NULL, 0, TIME_ONESHOT);
    CHECK(id != 0);
    if (!id) return;
    EXPECT_EQ(te_kill(id), 0);
    EXPECT_EQ(te_kill(id), TE_NOCANDO);
}

TEST(te_kill_fired_oneshot_every_callback_kind) {
    TE_REQUIRE();
    static te_ctx c; te_ctx_init(&c);
    HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    struct { LPTIMECALLBACK f; DWORD_PTR u; UINT fl; } rows[] = {
        { te_cb_ctx, (DWORD_PTR)&c, TIME_ONESHOT },
        { te_cb_ctx, (DWORD_PTR)&c, TIME_ONESHOT | TIME_KILL_SYNCHRONOUS },
        { NULL, 0, TIME_ONESHOT },
        { (LPTIMECALLBACK)ev, 0, TIME_ONESHOT | TIME_CALLBACK_EVENT_SET },
        { (LPTIMECALLBACK)ev, 0, TIME_ONESHOT | TIME_CALLBACK_EVENT_PULSE },
        { NULL, 0, TIME_ONESHOT | 0x40 },
        { NULL, 0, TIME_ONESHOT | 0x80 },
        { NULL, 0, TIME_ONESHOT | 0x30 },
    };
    UINT ids[8];
    for (int i = 0; i < 8; i++) { ids[i] = te_set(10 + i, 1, rows[i].f, rows[i].u, rows[i].fl); CHECKF(ids[i] != 0, "row %d", i); }
    Sleep(400);
    for (int i = 0; i < 8; i++) if (ids[i]) EXPECT_EQ(te_kill(ids[i]), TE_NOCANDO);
    EXPECT_EQ(c.count, 2);
    CloseHandle(ev);
    te_ctx_free(&c);
}

static volatile UINT te_kid, te_kres;
static DWORD WINAPI te_killer(LPVOID p) { (void)p; te_kres = te_kill(te_kid); return 0; }
static DWORD WINAPI te_setter(LPVOID p) { *(volatile UINT *)p = te_set(TE_LONG, TE_COARSE, NULL, 0, TIME_PERIODIC); return 0; }

TEST(te_kill_from_other_thread) {
    TE_REQUIRE();
    UINT id = te_set(TE_LONG, TE_COARSE, NULL, 0, TIME_PERIODIC);
    CHECK(id != 0);
    if (!id) return;
    te_kid = id; te_kres = 0xFFFF;
    HANDLE th = CreateThread(NULL, 0, te_killer, NULL, 0, NULL);
    CHECK(th != NULL);
    if (th) { EXPECT_EQ(WaitForSingleObject(th, 2000), WAIT_OBJECT_0); CloseHandle(th); }
    EXPECT_EQ(te_kres, 0);
    EXPECT_EQ(te_kill(id), TE_NOCANDO);
}

TEST(te_set_on_other_thread_kill_here) {
    TE_REQUIRE();
    /* An event belongs to the process, not to the thread that created it (the creator thread id is only stored). */
    static volatile UINT id;
    id = 0;
    HANDLE th = CreateThread(NULL, 0, te_setter, (LPVOID)&id, 0, NULL);
    CHECK(th != NULL);
    if (th) { EXPECT_EQ(WaitForSingleObject(th, 2000), WAIT_OBJECT_0); CloseHandle(th); }
    CHECK(id != 0);
    if (id) { EXPECT_EQ(te_kill(id), 0); EXPECT_EQ(te_kill(id), TE_NOCANDO); }
}

TEST(te_ids_no_leak_after_file) {
    TE_REQUIRE();
    EXPECT_EQ(te_free_slots(), TE_SLOTS);
}
