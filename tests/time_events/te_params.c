/* time_events: argument validation of timeSetEvent (fuEvent mask, uDelay window, uResolution clamping) and the
 * no-side-effect contract of rejected calls.
 * Decomp (winmm!timeSetEvent 0x2a20): (fuEvent & 0xFFFFFE0E) != 0 -> 0; uDelay > 1000000 or uDelay < wPeriodMin
 * -> 0; both rejections return before timeBeginPeriod and before any slot is touched.
 */
#include "te.h"

/* ---------------------------------------------------------------- fuEvent, one bit at a time */

static int te_bit_valid(int bit) { return !((1u << bit) & TE_FLAG_MASK); }

TEST(te_flags_each_bit_table) {
    TE_REQUIRE();
    for (int bit = 0; bit < 32; bit++) {
        UINT fl = 1u << bit;
        UINT id = te_set(TE_LONG, TE_COARSE, NULL, 0, fl);
        if (te_bit_valid(bit)) {
            CHECKF(id != 0, "bit %d (0x%x) must be accepted", bit, fl);
            if (id) EXPECT_EQ(te_kill(id), 0);
        } else {
            CHECKF(id == 0, "bit %d (0x%x) must be rejected, got id %u", bit, fl, id);
            if (id) te_kill(id);
        }
    }
    EXPECT_EQ(te_free_slots(), TE_SLOTS);
}

TEST(te_flags_valid_bits_are_exactly_0x1f1) {
    TE_REQUIRE();
    UINT valid = 0;
    for (int bit = 0; bit < 32; bit++) {
        UINT id = te_set(TE_LONG, TE_COARSE, NULL, 0, 1u << bit);
        if (id) { valid |= 1u << bit; te_kill(id); }
    }
    EXPECT_EQ(valid, 0x1F1u);
    EXPECT_EQ(valid, ~TE_FLAG_MASK);
}

static const UINT te_bad_flags[] = {
    2, 4, 8, 0xE, 0x200, 0x400, 0x800, 0x1000, 0x8000, 0x10000, 0x80000000u, 0xFFFFFFFFu, 0xFFFFFE0Eu,
    TIME_PERIODIC | 2, TIME_CALLBACK_EVENT_SET | 8, TIME_KILL_SYNCHRONOUS | 0x200, 0x1F1 | 0x200, 0x1F1 | 2,
    0x3, 0x5, 0x9, 0x101 | 0x400, 0x7FFFFFFF, 0x1FF, 0x3F1,
};

TEST(te_flags_bad_table_oneshot_delay) {
    TE_REQUIRE();
    for (size_t i = 0; i < sizeof te_bad_flags / sizeof te_bad_flags[0]; i++) {
        UINT id = te_set(TE_LONG, TE_COARSE, NULL, 0, te_bad_flags[i]);
        CHECKF(id == 0, "flags 0x%x -> id %u", te_bad_flags[i], id);
        if (id) te_kill(id);
    }
}

TEST(te_flags_bad_table_with_real_callback_never_fires) {
    TE_REQUIRE();
    static te_ctx c; te_ctx_init(&c);
    for (size_t i = 0; i < sizeof te_bad_flags / sizeof te_bad_flags[0]; i++) {
        UINT fl = te_bad_flags[i] & ~0x30u;  /* keep the callback type FUNCTION so a wrongly accepted call would call us */
        if (!(fl & TE_FLAG_MASK)) continue;
        UINT id = te_set(10, 1, te_cb_ctx, (DWORD_PTR)&c, fl);
        CHECKF(id == 0, "flags 0x%x -> id %u", fl, id);
        if (id) te_kill(id);
    }
    Sleep(100);
    EXPECT_EQ(c.count, 0);
    te_ctx_free(&c);
}

static const UINT te_good_flags[] = {
    0, 1, 0x10, 0x11, 0x20, 0x21, 0x30, 0x31, 0x40, 0x41, 0x80, 0x81, 0xC0, 0xF0, 0xF1,
    0x100, 0x101, 0x110, 0x111, 0x120, 0x121, 0x140, 0x180, 0x1F0, 0x1F1,
};

TEST(te_flags_good_table_accepted) {
    TE_REQUIRE();
    for (size_t i = 0; i < sizeof te_good_flags / sizeof te_good_flags[0]; i++) {
        UINT id = te_set(TE_LONG, TE_COARSE, NULL, 0, te_good_flags[i]);
        CHECKF(id != 0, "flags 0x%x rejected", te_good_flags[i]);
        if (id) EXPECT_EQ(te_kill(id), 0);
    }
    EXPECT_EQ(te_free_slots(), TE_SLOTS);
}

TEST(te_flags_good_table_all_live_at_once) {
    TE_REQUIRE();
    /* 16 accepted flag combinations live together, each with its own id; killing each one returns 0. */
    UINT ids[TE_SLOTS];
    for (int i = 0; i < TE_SLOTS; i++) {
        ids[i] = te_set(TE_LONG, TE_COARSE, NULL, 0, te_good_flags[i]);
        CHECKF(ids[i] != 0, "flags 0x%x", te_good_flags[i]);
    }
    for (int i = 0; i < TE_SLOTS; i++)
        for (int j = i + 1; j < TE_SLOTS; j++) CHECK(!ids[i] || ids[i] != ids[j]);
    for (int i = 0; i < TE_SLOTS; i++) if (ids[i]) EXPECT_EQ(te_kill(ids[i]), 0);
    for (int i = 0; i < TE_SLOTS; i++) if (ids[i]) EXPECT_EQ(te_kill(ids[i]), TE_NOCANDO);
}

TEST(te_flags_named_constants) {
    TE_REQUIRE();
    EXPECT_EQ(TIME_ONESHOT, 0);
    EXPECT_EQ(TIME_PERIODIC, 1);
    EXPECT_EQ(TIME_CALLBACK_FUNCTION, 0);
    EXPECT_EQ(TIME_CALLBACK_EVENT_SET, 0x10);
    EXPECT_EQ(TIME_CALLBACK_EVENT_PULSE, 0x20);
    EXPECT_EQ(TIME_KILL_SYNCHRONOUS, 0x100);
    EXPECT_EQ(TIMERR_NOCANDO, 97);
    EXPECT_EQ(TIMERR_NOERROR, 0);
}

/* ---------------------------------------------------------------- uDelay */

typedef struct { UINT delay; int ok; } te_delay_row;

TEST(te_delay_table_oneshot) {
    TE_REQUIRE();
    UINT pmin = te_pmin();
    te_delay_row rows[] = {
        { 0, 0 }, { pmin, 1 }, { pmin + 1, 1 }, { 2, 1 }, { 10, 1 }, { 15, 1 }, { 16, 1 }, { 17, 1 }, { 100, 1 },
        { 1000, 1 }, { 65535, 1 }, { 65536, 1 }, { 999999, 1 }, { TE_MAX_DELAY, 1 }, { TE_MAX_DELAY + 1, 0 },
        { TE_MAX_DELAY + 2, 0 }, { 2000000, 0 }, { 0x7FFFFFFF, 0 }, { 0x80000000u, 0 }, { 0xFFFFFFFEu, 0 },
        { 0xFFFFFFFFu, 0 },
    };
    for (size_t i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        if (rows[i].delay < pmin) rows[i].ok = 0;
        /* Short accepted one-shots may fire before the kill: kill is then 97. Use a periodic for those below. */
        UINT id = te_set(rows[i].delay, TE_COARSE, NULL, 0, TIME_ONESHOT);
        if (rows[i].ok) {
            CHECKF(id != 0, "delay %u rejected", rows[i].delay);
            if (id) { UINT k = te_kill(id); CHECKF(k == 0 || (k == TE_NOCANDO && rows[i].delay < 1000), "delay %u kill %u", rows[i].delay, k); }
        } else {
            CHECKF(id == 0, "delay %u accepted (id %u)", rows[i].delay, id);
            if (id) te_kill(id);
        }
    }
    Sleep(50);
    EXPECT_EQ(te_free_slots(), TE_SLOTS);
}

TEST(te_delay_table_periodic) {
    TE_REQUIRE();
    UINT pmin = te_pmin();
    te_delay_row rows[] = {
        { 0, 0 }, { pmin, 1 }, { 2, 1 }, { 5, 1 }, { 15, 1 }, { 16, 1 }, { 17, 1 }, { 1000, 1 }, { TE_MAX_DELAY, 1 },
        { TE_MAX_DELAY + 1, 0 }, { 0x80000000u, 0 }, { 0xFFFFFFFFu, 0 },
    };
    for (size_t i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        if (rows[i].delay < pmin) rows[i].ok = 0;
        UINT id = te_set(rows[i].delay, 0, NULL, 0, TIME_PERIODIC);
        if (rows[i].ok) {
            CHECKF(id != 0, "delay %u rejected", rows[i].delay);
            if (id) EXPECT_EQ(te_kill(id), 0);   /* a periodic event stays live until killed */
        } else {
            CHECKF(id == 0, "delay %u accepted", rows[i].delay);
            if (id) te_kill(id);
        }
    }
    EXPECT_EQ(te_free_slots(), TE_SLOTS);
}

TEST(te_delay_zero_every_flag_combination) {
    TE_REQUIRE();
    for (size_t i = 0; i < sizeof te_good_flags / sizeof te_good_flags[0]; i++) {
        UINT id = te_set(0, 0, NULL, 0, te_good_flags[i]);
        CHECKF(id == 0, "delay 0 flags 0x%x -> %u", te_good_flags[i], id);
        if (id) te_kill(id);
        id = te_set(TE_MAX_DELAY + 1, 0, NULL, 0, te_good_flags[i]);
        CHECKF(id == 0, "delay 1000001 flags 0x%x -> %u", te_good_flags[i], id);
        if (id) te_kill(id);
    }
}

TEST(te_delay_max_accepted_periodic_and_oneshot) {
    TE_REQUIRE();
    UINT a = te_set(TE_MAX_DELAY, TE_MAX_DELAY, NULL, 0, TIME_PERIODIC);
    UINT b = te_set(TE_MAX_DELAY, 0, NULL, 0, TIME_ONESHOT);
    CHECK(a != 0); CHECK(b != 0); CHECK(a != b);
    if (a) EXPECT_EQ(te_kill(a), 0);
    if (b) EXPECT_EQ(te_kill(b), 0);
}

TEST(te_delay_pmin_minus_one_rejected) {
    TE_REQUIRE();
    UINT pmin = te_pmin();
    UINT id = te_set(pmin - 1, 0, NULL, 0, TIME_PERIODIC);
    EXPECT_EQ(id, 0);
    if (id) te_kill(id);
    id = te_set(pmin, 0, NULL, 0, TIME_PERIODIC);
    CHECK(id != 0);
    if (id) EXPECT_EQ(te_kill(id), 0);
}

TEST(te_delay_bad_with_real_callback_never_fires) {
    TE_REQUIRE();
    static te_ctx c; te_ctx_init(&c);
    UINT bad[] = { 0, TE_MAX_DELAY + 1, 0xFFFFFFFFu };
    for (int i = 0; i < 3; i++) for (int per = 0; per < 2; per++) {
        UINT id = te_set(bad[i], 1, te_cb_ctx, (DWORD_PTR)&c, per ? TIME_PERIODIC : TIME_ONESHOT);
        CHECKF(id == 0, "delay %u per %d", bad[i], per);
        if (id) te_kill(id);
    }
    Sleep(100);
    EXPECT_EQ(c.count, 0);
    te_ctx_free(&c);
}

/* ---------------------------------------------------------------- uResolution: never a reason to fail */

TEST(te_resolution_table_accepted) {
    TE_REQUIRE();
    UINT res[] = { 0, 1, 2, 5, 10, 15, 16, 17, 54, 55, 56, 100, 1000, 65535, 65536, TE_MAX_DELAY, TE_MAX_DELAY + 1,
                   0x7FFFFFFF, 0x80000000u, 0xFFFFFFFFu };
    UINT delays[] = { 1, 10, 100, TE_LONG };
    for (size_t d = 0; d < 4; d++) for (size_t i = 0; i < sizeof res / sizeof res[0]; i++) {
        UINT delay = delays[d] < te_pmin() ? te_pmin() : delays[d];
        UINT id = te_set(delay, res[i], NULL, 0, TIME_PERIODIC);
        CHECKF(id != 0, "delay %u res %u rejected", delay, res[i]);
        if (id) EXPECT_EQ(te_kill(id), 0);
    }
    EXPECT_EQ(te_free_slots(), TE_SLOTS);
}

TEST(te_resolution_larger_than_delay_accepted) {
    TE_REQUIRE();
    UINT pairs[][2] = { { 1, 2 }, { 1, 16 }, { 2, 10 }, { 5, 100 }, { 9, 10 }, { 15, 16 }, { 16, 17 }, { 100, 1000 } };
    for (size_t i = 0; i < sizeof pairs / sizeof pairs[0]; i++) {
        if (pairs[i][0] < te_pmin()) continue;
        UINT id = te_set(pairs[i][0], pairs[i][1], NULL, 0, TIME_PERIODIC);
        CHECKF(id != 0, "delay %u res %u", pairs[i][0], pairs[i][1]);
        if (id) EXPECT_EQ(te_kill(id), 0);
    }
}

/* ---------------------------------------------------------------- callback pointer is not validated */

TEST(te_null_function_oneshot_accepted_and_consumed) {
    TE_REQUIRE();
    UINT id = te_set(20, 1, NULL, 0, TIME_ONESHOT | TIME_CALLBACK_FUNCTION);
    CHECK(id != 0);
    if (id) {
        Sleep(300);   /* fires (nothing to call) and frees its slot */
        EXPECT_EQ(te_kill(id), TE_NOCANDO);
    }
}

TEST(te_null_function_periodic_stays_live) {
    TE_REQUIRE();
    UINT id = te_set(10, 1, NULL, 0x1234, TIME_PERIODIC);
    CHECK(id != 0);
    if (id) {
        Sleep(120);   /* ~10 dispatches that call nothing */
        EXPECT_EQ(te_kill(id), 0);
        EXPECT_EQ(te_kill(id), TE_NOCANDO);
    }
}

TEST(te_null_event_handle_set_and_pulse_harmless) {
    TE_REQUIRE();
    /* SetEvent(NULL)/PulseEvent(NULL) just fail inside the timer thread. */
    UINT a = te_set(10, 1, NULL, 0, TIME_ONESHOT | TIME_CALLBACK_EVENT_SET);
    UINT b = te_set(10, 1, NULL, 0, TIME_ONESHOT | TIME_CALLBACK_EVENT_PULSE);
    UINT c = te_set(10, 1, NULL, 0, TIME_PERIODIC | TIME_CALLBACK_EVENT_SET);
    CHECK(a != 0); CHECK(b != 0); CHECK(c != 0);
    Sleep(200);
    if (a) EXPECT_EQ(te_kill(a), TE_NOCANDO);
    if (b) EXPECT_EQ(te_kill(b), TE_NOCANDO);
    if (c) EXPECT_EQ(te_kill(c), 0);
}

/* ---------------------------------------------------------------- rejected calls have no side effects */

TEST(te_rejected_calls_do_not_consume_generation) {
    TE_REQUIRE();
    UINT a = te_probe_id();
    CHECK(a != 0);
    for (int i = 0; i < 20; i++) {
        EXPECT_EQ(te_set(0, 0, NULL, 0, 0), 0);
        EXPECT_EQ(te_set(TE_LONG, 0, NULL, 0, 2), 0);
        EXPECT_EQ(te_set(TE_MAX_DELAY + 1, 1, NULL, 0, TIME_PERIODIC), 0);
    }
    UINT b = te_probe_id();
    CHECK(b != 0);
    /* Same slot (lowest free, 0 when nothing else is live), next generation. */
    EXPECT_EQ(b & 0xF, a & 0xF);
    if (a < 0xFFF0) EXPECT_EQ(b, a + 0x10);
}

TEST(te_rejected_calls_take_no_period_reference) {
    TE_REQUIRE();
    REQUIRE(U(timeEndPeriod));
    UINT pmin = te_pmin(), umax = te_umax();
    EXPECT_EQ(te_held_mask(), 0);
    for (UINT p = pmin; p < umax; p++) {
        EXPECT_EQ(te_set(0, p, NULL, 0, TIME_PERIODIC), 0);
        EXPECT_EQ(te_set(TE_LONG, p, NULL, 0, 0x200), 0);
        EXPECT_EQ(te_set(TE_MAX_DELAY + 1, p, NULL, 0, 0), 0);
    }
    EXPECT_EQ(te_held_mask(), 0);
}

TEST(te_rejected_calls_keep_last_error) {
    TE_REQUIRE();
    SetLastError(0xBEEF);
    EXPECT_EQ(te_set(0, 0, NULL, 0, 0), 0);
    EXPECT_EQ(GetLastError(), 0xBEEF);
    SetLastError(0xBEEF);
    EXPECT_EQ(te_set(TE_LONG, 0, NULL, 0, 0x8000), 0);
    EXPECT_EQ(GetLastError(), 0xBEEF);
    SetLastError(0xBEEF);
    EXPECT_EQ(te_kill(TE_UNKNOWN_ID), TE_NOCANDO);
    EXPECT_EQ(GetLastError(), 0xBEEF);
}

TEST(te_params_no_leak_after_file) {
    TE_REQUIRE();
    EXPECT_EQ(te_free_slots(), TE_SLOTS);
    EXPECT_EQ(te_held_mask(), 0);
}
