/* midiOutOpen argument ladder (winmmbase !0x7dd0).
 *
 *   1. phmo NULL                                          -> 11 (before anything else)
 *   2. fdwOpen & (id == MIDI_MAPPER ? 0xBFF3 : 0x3FF3)    -> 10 (*phmo untouched)
 *   3. dwCallback != 0 && ((fdwOpen >> 16) & 7) == 1 (CALLBACK_WINDOW) && !IsWindow -> 11 (*phmo untouched)
 *   4. *phmo = NULL
 *   5. device lookup by id (no translation of 0xFFFF to the mapper) -> 2
 *   6. driver open (no wave device: the mapper and the synth answer MMSYSERR_ERROR [VM])
 */
#include "moc.h"

#define OPEN(ph, id, cb, fl) U(midiOutOpen)((ph), (id), (DWORD_PTR)(cb), 0, (fl))

/* ---------------------------------------------------------------- step 1: phmo NULL */

TEST(midiout_contract_open_null_phmo_11_ids) {
    REQUIRE(U(midiOutOpen));
    static const UINT ids[] = { 0, 1, 2, 9, 0xFF, 0xFFFF, 0x10000, 0x7FFFFFFF, 0xFFFFFFFE, MIDI_MAPPER };
    for (int i = 0; i < NELEM(ids); i++)
        CHECKF(OPEN(NULL, ids[i], 0, CALLBACK_NULL) == MMSYSERR_INVALPARAM, "id=0x%x", ids[i]);
}

TEST(midiout_contract_open_null_phmo_beats_bad_flags) {
    REQUIRE(U(midiOutOpen));
    static const DWORD fl[] = { 1, 2, 0x10, 0x20, 0x100, 0x1000, 0x3FF3, 0xBFF3, 0xFFFFFFFF };
    for (int i = 0; i < NELEM(fl); i++) {
        CHECKF(OPEN(NULL, 0, 0, fl[i]) == MMSYSERR_INVALPARAM, "id 0 fl=0x%lx", fl[i]);
        CHECKF(OPEN(NULL, MIDI_MAPPER, 0, fl[i]) == MMSYSERR_INVALPARAM, "mapper fl=0x%lx", fl[i]);
        CHECKF(OPEN(NULL, 0xFFFF, 0, fl[i]) == MMSYSERR_INVALPARAM, "id 0xFFFF fl=0x%lx", fl[i]);
    }
}

TEST(midiout_contract_open_null_phmo_beats_bad_window) {
    REQUIRE(U(midiOutOpen));
    static const ULONG_PTR cbs[] = { 1, 0x12345678, 0xFFFF, 0xDEADBEEF };
    for (int i = 0; i < NELEM(cbs); i++) {
        CHECKF(OPEN(NULL, 0, cbs[i], CALLBACK_WINDOW) == MMSYSERR_INVALPARAM, "cb=0x%llx", (unsigned long long)cbs[i]);
        CHECKF(OPEN(NULL, MIDI_MAPPER, cbs[i], CALLBACK_WINDOW) == MMSYSERR_INVALPARAM, "mapper cb=0x%llx", (unsigned long long)cbs[i]);
        CHECKF(OPEN(NULL, 9, cbs[i], CALLBACK_WINDOW) == MMSYSERR_INVALPARAM, "id 9 cb=0x%llx", (unsigned long long)cbs[i]);
    }
}

/* ---------------------------------------------------------------- step 2: flags */

/* Every single bit, on an out-of-range id: bits in 0x3FF3 -> 10 with *phmo untouched, the rest reach the
 * device lookup -> 2 with *phmo NULLed. */
TEST(midiout_contract_open_flag_bits_single_bad_id) {
    REQUIRE(U(midiOutOpen));
    UINT bad = moc_nout() + 8;
    for (int b = 0; b < 32; b++) {
        DWORD fl = 1u << b;
        HMIDIOUT h = MOC_SENT_HANDLE;
        MMRESULT r = OPEN(&h, bad, 0, fl);
        if (fl & MOC_OPEN_BADMASK_ID) {
            CHECKF(r == MMSYSERR_INVALFLAG, "bit %d: got %u want 10", b, r);
            CHECKF(h == MOC_SENT_HANDLE, "bit %d: *phmo written (%p)", b, h);
        } else {
            CHECKF(r == MMSYSERR_BADDEVICEID, "bit %d: got %u want 2", b, r);
            CHECKF(h == NULL, "bit %d: *phmo not NULLed (%p)", b, h);
        }
    }
}

/* The same table on id 0xFFFF: an ordinary id, so the 0x3FF3 mask applies (0x8000 accepted). */
TEST(midiout_contract_open_flag_bits_single_id_ffff) {
    REQUIRE(U(midiOutOpen));
    for (int b = 0; b < 32; b++) {
        DWORD fl = 1u << b;
        HMIDIOUT h = MOC_SENT_HANDLE;
        MMRESULT r = OPEN(&h, 0xFFFF, 0, fl);
        if (fl & MOC_OPEN_BADMASK_ID) CHECKF(r == MMSYSERR_INVALFLAG && h == MOC_SENT_HANDLE, "bit %d: r=%u h=%p", b, r, h);
        else CHECKF(r == MMSYSERR_BADDEVICEID && h == NULL, "bit %d: r=%u h=%p", b, r, h);
    }
}

/* Mapper: bits in 0xBFF3 -> 10 (0x8000 rejected only for the mapper). */
TEST(midiout_contract_open_flag_bits_single_mapper_rejected) {
    REQUIRE(U(midiOutOpen));
    for (int b = 0; b < 32; b++) {
        DWORD fl = 1u << b;
        if (!(fl & MOC_OPEN_BADMASK_MAPPER)) continue;
        HMIDIOUT h = MOC_SENT_HANDLE;
        MMRESULT r = OPEN(&h, MIDI_MAPPER, 0, fl);
        CHECKF(r == MMSYSERR_INVALFLAG, "bit %d: got %u want 10", b, r);
        CHECKF(h == MOC_SENT_HANDLE, "bit %d: *phmo written (%p)", b, h);
    }
}

TEST(midiout_contract_open_flag_bits_single_id0_rejected) {
    REQUIRE(U(midiOutOpen));
    for (int b = 0; b < 32; b++) {
        DWORD fl = 1u << b;
        if (!(fl & MOC_OPEN_BADMASK_ID)) continue;
        HMIDIOUT h = MOC_SENT_HANDLE;
        MMRESULT r = OPEN(&h, 0, 0, fl);
        CHECKF(r == MMSYSERR_INVALFLAG, "bit %d: got %u want 10", b, r);
        CHECKF(h == MOC_SENT_HANDLE, "bit %d: *phmo written (%p)", b, h);
    }
}

TEST(midiout_contract_open_flag_0x8000_mapper_only) {
    REQUIRE(U(midiOutOpen));
    HMIDIOUT h = MOC_SENT_HANDLE;
    EXPECT_EQ(OPEN(&h, MIDI_MAPPER, 0, 0x8000), MMSYSERR_INVALFLAG);
    CHECK(h == MOC_SENT_HANDLE);
    h = MOC_SENT_HANDLE;
    EXPECT_EQ(OPEN(&h, moc_nout(), 0, 0x8000), MMSYSERR_BADDEVICEID);
    CHECK(h == NULL);
    h = MOC_SENT_HANDLE;
    EXPECT_EQ(OPEN(&h, 0xFFFFFFFE, 0, 0x8000), MMSYSERR_BADDEVICEID);
    CHECK(h == NULL);
}

/* MIDI_IO_STATUS (0x20) is a midiIn flag: midiOutOpen rejects it */
TEST(midiout_contract_open_midi_io_status_10) {
    REQUIRE(U(midiOutOpen));
    static const UINT ids[] = { 0, 1, 9, 0xFFFF, MIDI_MAPPER };
    for (int i = 0; i < NELEM(ids); i++) {
        HMIDIOUT h = MOC_SENT_HANDLE;
        CHECKF(OPEN(&h, ids[i], 0, MIDI_IO_STATUS) == MMSYSERR_INVALFLAG, "id=0x%x", ids[i]);
        CHECKF(h == MOC_SENT_HANDLE, "id=0x%x", ids[i]);
    }
}

/* Flag check beats the device check: bad flags on bad ids answer 10, not 2 */
TEST(midiout_contract_open_flags_before_device) {
    REQUIRE(U(midiOutOpen));
    static const UINT ids[] = { 1, 9, 0xFF, 0xFFFF, 0x10000, 0xFFFFFFFE };
    static const DWORD fl[] = { 1, 2, 3, 0x10, 0x40, 0x80, 0x100, 0x200, 0x400, 0x800, 0x1000, 0x2000, 0x3FF3 };
    for (int i = 0; i < NELEM(ids); i++)
        for (int j = 0; j < NELEM(fl); j++) {
            HMIDIOUT h = MOC_SENT_HANDLE;
            CHECKF(OPEN(&h, ids[i], 0, fl[j]) == MMSYSERR_INVALFLAG, "id=0x%x fl=0x%lx", ids[i], fl[j]);
            CHECKF(h == MOC_SENT_HANDLE, "id=0x%x fl=0x%lx", ids[i], fl[j]);
        }
}

/* Multi-bit combinations, including a valid callback type plus one bad bit */
TEST(midiout_contract_open_flag_combos) {
    REQUIRE(U(midiOutOpen));
    static const struct { DWORD fl; int mapper_bad, id_bad; } t[] = {
        { 0xFFFFFFFF, 1, 1 }, { 0x0000FFFF, 1, 1 }, { 0x0000C00C, 1, 0 }, { 0x0000400C, 0, 0 },
        { 0x0000000C, 0, 0 }, { 0x00034000, 0, 0 }, { 0x00038000, 1, 0 }, { 0x00030002, 1, 1 },
        { 0x00050001, 1, 1 }, { 0x00010010, 1, 1 }, { 0xFFFF0000, 0, 0 }, { 0xFFFF000C, 0, 0 },
        { 0xFFFF800C, 1, 0 }, { 0x7FFF4000, 0, 0 },
    };
    UINT bad = moc_nout() + 8;
    for (int i = 0; i < NELEM(t); i++) {
        HMIDIOUT h = MOC_SENT_HANDLE;
        MMRESULT r = OPEN(&h, bad, 0, t[i].fl);
        CHECKF(r == (t[i].id_bad ? MMSYSERR_INVALFLAG : MMSYSERR_BADDEVICEID), "id fl=0x%lx r=%u", t[i].fl, r);
        if (t[i].mapper_bad) {
            h = MOC_SENT_HANDLE;
            r = OPEN(&h, MIDI_MAPPER, 0, t[i].fl);
            CHECKF(r == MMSYSERR_INVALFLAG && h == MOC_SENT_HANDLE, "mapper fl=0x%lx r=%u", t[i].fl, r);
        }
    }
}

/* Accepted flag bits on the mapper and on id 0 never answer 10 (they reach the driver: open and close
 * when a wave device exists; MMSYSERR_ERROR without one). */
static void accepted_bits_on(UINT id, DWORD badmask) {
    for (int b = 0; b < 32; b++) {
        DWORD fl = 1u << b;
        if (fl & badmask) continue;
        if ((fl & CALLBACK_TYPEMASK) && fl != CALLBACK_WINDOW && fl != CALLBACK_TASK) continue; /* typed callbacks need a target */
        HMIDIOUT h = MOC_SENT_HANDLE;
        MMRESULT r = OPEN(&h, id, 0, fl);
        CHECKF(r != MMSYSERR_INVALFLAG, "id=0x%x bit %d answered 10", id, b);
        if (r == MMSYSERR_NOERROR) {
            CHECKF(h && h != MOC_SENT_HANDLE, "id=0x%x bit %d: handle %p", id, b, h);
            moc_close(h);
        } else {
            CHECKF(h == NULL, "id=0x%x bit %d r=%u: *phmo %p", id, b, r, h);
            if (!moc_can_open()) CHECKF(r == MMSYSERR_ERROR, "id=0x%x bit %d no wave device: r=%u want 1", id, b, r);
            else t_note("id=0x%x bit %d: midiOutOpen=%u", id, b, r);
        }
    }
}
TEST(midiout_contract_open_flag_bits_accepted_mapper) { REQUIRE(U(midiOutOpen)); REQUIRE(U(midiOutClose)); accepted_bits_on(MIDI_MAPPER, MOC_OPEN_BADMASK_MAPPER); }
TEST(midiout_contract_open_flag_bits_accepted_id0) { REQUIRE(U(midiOutOpen)); REQUIRE(U(midiOutClose)); accepted_bits_on(0, MOC_OPEN_BADMASK_ID); }

/* ---------------------------------------------------------------- step 3: CALLBACK_WINDOW */

static const ULONG_PTR not_windows[] = { 1, 2, 0x10, 0xFFFF, 0x12345678, 0xDEADBEE0, 0x7FFFFFFF };

TEST(midiout_contract_open_window_not_window_11) {
    REQUIRE(U(midiOutOpen));
    static const UINT ids[] = { 0, MIDI_MAPPER };
    for (int i = 0; i < NELEM(ids); i++)
        for (int j = 0; j < NELEM(not_windows); j++) {
            HMIDIOUT h = MOC_SENT_HANDLE;
            MMRESULT r = OPEN(&h, ids[i], not_windows[j], CALLBACK_WINDOW);
            CHECKF(r == MMSYSERR_INVALPARAM, "id=0x%x hwnd=0x%llx r=%u", ids[i], (unsigned long long)not_windows[j], r);
            CHECKF(h == MOC_SENT_HANDLE, "id=0x%x: *phmo written", ids[i]);
        }
}

/* The window check comes before the device check: bad ids answer 11, not 2 */
TEST(midiout_contract_open_window_check_before_device) {
    REQUIRE(U(midiOutOpen));
    static const UINT ids[] = { 1, 9, 0xFFFF, 0x10000, 0xFFFFFFFE };
    for (int i = 0; i < NELEM(ids); i++)
        for (int j = 0; j < NELEM(not_windows); j++) {
            HMIDIOUT h = MOC_SENT_HANDLE;
            CHECKF(OPEN(&h, ids[i], not_windows[j], CALLBACK_WINDOW) == MMSYSERR_INVALPARAM, "id=0x%x", ids[i]);
            CHECKF(h == MOC_SENT_HANDLE, "id=0x%x", ids[i]);
        }
}

/* ... but after the flag check */
TEST(midiout_contract_open_flags_before_window_check) {
    REQUIRE(U(midiOutOpen));
    static const DWORD extra[] = { 1, 2, 0x10, 0x20, 0x100, 0x2000 };
    for (int i = 0; i < NELEM(extra); i++) {
        HMIDIOUT h = MOC_SENT_HANDLE;
        CHECKF(OPEN(&h, 0, 0x12345678, CALLBACK_WINDOW | extra[i]) == MMSYSERR_INVALFLAG, "fl=0x%lx", extra[i]);
        CHECKF(OPEN(&h, MIDI_MAPPER, 0x12345678, CALLBACK_WINDOW | extra[i]) == MMSYSERR_INVALFLAG, "mapper fl=0x%lx", extra[i]);
        CHECK(h == MOC_SENT_HANDLE);
    }
}

/* CALLBACK_WINDOW with dwCallback 0 is not checked (no window, no callback) */
TEST(midiout_contract_open_window_null_not_checked) {
    REQUIRE(U(midiOutOpen));
    HMIDIOUT h = MOC_SENT_HANDLE;
    EXPECT_EQ(OPEN(&h, moc_nout() + 3, 0, CALLBACK_WINDOW), MMSYSERR_BADDEVICEID);
    CHECK(h == NULL);
    h = MOC_SENT_HANDLE;
    EXPECT_EQ(OPEN(&h, 0xFFFF, 0, CALLBACK_WINDOW), MMSYSERR_BADDEVICEID);
    CHECK(h == NULL);
}

/* Only the 3-bit field (fdwOpen >> 16) & 7 == 1 counts as a window: 0x90000 is a window callback too,
 * while CALLBACK_FUNCTION/THREAD/EVENT and the unused type values take dwCallback unchecked. */
TEST(midiout_contract_open_window_type_field) {
    REQUIRE(U(midiOutOpen));
    UINT bad = moc_nout() + 5;
    static const struct { DWORD fl; int window; } t[] = {
        { 0x00010000, 1 }, { 0x00090000, 1 }, { 0x00110000, 1 }, { 0xFFF90000, 1 }, { 0x00014000, 1 },
        { 0x00020000, 0 }, { 0x00030000, 0 }, { 0x00040000, 0 }, { 0x00050000, 0 }, { 0x00060000, 0 },
        { 0x00070000, 0 }, { 0x00080000, 0 }, { 0x00100000, 0 }, { 0x000F0000, 0 },
    };
    for (int i = 0; i < NELEM(t); i++) {
        HMIDIOUT h = MOC_SENT_HANDLE;
        MMRESULT r = OPEN(&h, bad, 0x12345678, t[i].fl);
        if (t[i].window) CHECKF(r == MMSYSERR_INVALPARAM && h == MOC_SENT_HANDLE, "fl=0x%lx r=%u", t[i].fl, r);
        else CHECKF(r == MMSYSERR_BADDEVICEID && h == NULL, "fl=0x%lx r=%u", t[i].fl, r);
    }
}

/* A real window passes the check; a destroyed one is rejected again */
TEST(midiout_contract_open_window_real_then_destroyed) {
    REQUIRE(U(midiOutOpen)); REQUIRE(U(DestroyWindow));
    HWND w = moc_msg_window();
    if (!w) { t_skip("no window"); return; }
    HMIDIOUT h = MOC_SENT_HANDLE;
    EXPECT_EQ(OPEN(&h, moc_nout() + 2, w, CALLBACK_WINDOW), MMSYSERR_BADDEVICEID);
    CHECK(h == NULL);
    U(DestroyWindow)(w);
    h = MOC_SENT_HANDLE;
    EXPECT_EQ(OPEN(&h, moc_nout() + 2, w, CALLBACK_WINDOW), MMSYSERR_INVALPARAM);
    CHECK(h == MOC_SENT_HANDLE);
    EXPECT_EQ(OPEN(&h, MIDI_MAPPER, w, CALLBACK_WINDOW), MMSYSERR_INVALPARAM);
    EXPECT_EQ(OPEN(&h, 0, w, CALLBACK_WINDOW), MMSYSERR_INVALPARAM);
    CHECK(h == MOC_SENT_HANDLE);
}

/* Non-window callback targets are not validated before the device check */
TEST(midiout_contract_open_other_callbacks_unchecked_bad_id) {
    REQUIRE(U(midiOutOpen));
    static const DWORD types[] = { CALLBACK_NULL, CALLBACK_TASK, CALLBACK_FUNCTION, CALLBACK_EVENT };
    static const ULONG_PTR cbs[] = { 0, 1, 0x12345678, (ULONG_PTR)-1 };
    UINT bad = moc_nout() + 1;
    for (int i = 0; i < NELEM(types); i++)
        for (int j = 0; j < NELEM(cbs); j++) {
            HMIDIOUT h = MOC_SENT_HANDLE;
            CHECKF(OPEN(&h, bad, cbs[j], types[i]) == MMSYSERR_BADDEVICEID, "type=0x%lx cb=0x%llx", types[i], (unsigned long long)cbs[j]);
            CHECK(h == NULL);
        }
}

/* ---------------------------------------------------------------- step 4/5: device lookup */

TEST(midiout_contract_open_bad_ids_2_nulls_handle) {
    REQUIRE(U(midiOutOpen));
    for (int i = 0; i < moc_nbadid; i++) {
        HMIDIOUT h = MOC_SENT_HANDLE;
        MMRESULT r = OPEN(&h, (UINT)moc_bad_ids[i], 0, CALLBACK_NULL);
        CHECKF(r == MMSYSERR_BADDEVICEID, "id=0x%llx r=%u", (unsigned long long)moc_bad_ids[i], r);
        CHECKF(h == NULL, "id=0x%llx *phmo=%p", (unsigned long long)moc_bad_ids[i], h);
    }
}

TEST(midiout_contract_open_id_count_2) {
    REQUIRE(U(midiOutOpen));
    UINT n = moc_nout();
    for (UINT k = 0; k < 4; k++) {
        HMIDIOUT h = MOC_SENT_HANDLE;
        CHECKF(OPEN(&h, n + k, 0, CALLBACK_NULL) == MMSYSERR_BADDEVICEID, "id=%u", n + k);
        CHECK(h == NULL);
    }
}

/* 0xFFFF (the 16-bit MIDI_MAPPER) is NOT translated to the mapper on Win32 */
TEST(midiout_contract_open_id_ffff_not_mapper) {
    REQUIRE(U(midiOutOpen));
    HMIDIOUT h = MOC_SENT_HANDLE;
    EXPECT_EQ(OPEN(&h, 0xFFFF, 0, CALLBACK_NULL), MMSYSERR_BADDEVICEID);
    CHECK(h == NULL);
    h = MOC_SENT_HANDLE;
    EXPECT_EQ(OPEN(&h, 0xFFFF, 0, 0x4000), MMSYSERR_BADDEVICEID);
    CHECK(h == NULL);
}

TEST(midiout_contract_open_bad_id_any_dwinstance) {
    REQUIRE(U(midiOutOpen));
    static const DWORD_PTR inst[] = { 0, 1, 0x12345678, (DWORD_PTR)-1 };
    for (int i = 0; i < NELEM(inst); i++) {
        HMIDIOUT h = MOC_SENT_HANDLE;
        CHECKF(U(midiOutOpen)(&h, 9, 0, inst[i], CALLBACK_NULL) == MMSYSERR_BADDEVICEID, "inst=%llx", (unsigned long long)inst[i]);
        CHECK(h == NULL);
    }
}

/* ---------------------------------------------------------------- step 6: the driver */

/* No wave device: the mapper is enumerated but cannot be opened: MMSYSERR_ERROR, *phmo NULL [VM] */
TEST(midiout_contract_open_no_wave_device_mapper_1) {
    REQUIRE(U(midiOutOpen));
    if (moc_can_open()) { t_skip("a wave device exists (covered by the live tests)"); return; }
    HMIDIOUT h = MOC_SENT_HANDLE;
    EXPECT_EQ(OPEN(&h, MIDI_MAPPER, 0, CALLBACK_NULL), MMSYSERR_ERROR);
    CHECK(h == NULL);
    h = MOC_SENT_HANDLE;
    EXPECT_EQ(OPEN(&h, MIDI_MAPPER, 0, 0x4000), MMSYSERR_ERROR);
    CHECK(h == NULL);
}

TEST(midiout_contract_open_no_wave_device_synth) {
    REQUIRE(U(midiOutOpen));
    if (moc_can_open()) { t_skip("a wave device exists (covered by the live tests)"); return; }
    HMIDIOUT h = MOC_SENT_HANDLE;
    MMRESULT r = OPEN(&h, 0, 0, CALLBACK_NULL);
    t_note("noaudio midiOutOpen(0) = %u h=%p", r, h);
    CHECK(r != MMSYSERR_NOERROR);
    CHECK(h == NULL);
    if (r == MMSYSERR_NOERROR && h) U(midiOutClose)(h);
}

/* Repeated failing opens leave no state behind: the answer does not change */
TEST(midiout_contract_open_failures_repeatable) {
    REQUIRE(U(midiOutOpen));
    for (int k = 0; k < 20; k++) {
        HMIDIOUT h = MOC_SENT_HANDLE;
        CHECKF(OPEN(&h, 9, 0, 0) == MMSYSERR_BADDEVICEID, "round %d", k);
        CHECKF(OPEN(&h, 0, 0, 2) == MMSYSERR_INVALFLAG, "round %d", k);
        CHECKF(OPEN(NULL, 0, 0, 0) == MMSYSERR_INVALPARAM, "round %d", k);
    }
}

/* A-W independent: winmm's export and winmmbase's export give the same ladder */
TEST(midiout_contract_open_winmm_and_winmmbase_agree) {
    __typeof__(&midiOutOpen) a = UIN("winmm.dll", midiOutOpen);
    __typeof__(&midiOutOpen) b = UIN("winmmbase.dll", midiOutOpen);
    REQUIRE(a); REQUIRE(b);
    HMIDIOUT h1 = MOC_SENT_HANDLE, h2 = MOC_SENT_HANDLE;
    EXPECT_EQ(a(NULL, 0, 0, 0, 0), b(NULL, 0, 0, 0, 0));
    EXPECT_EQ(a(&h1, 0, 0, 0, 2), b(&h2, 0, 0, 0, 2));
    EXPECT_EQ(a(&h1, 9, 0, 0, 0), MMSYSERR_BADDEVICEID);
    EXPECT_EQ(b(&h2, 9, 0, 0, 0), MMSYSERR_BADDEVICEID);
    CHECK(h1 == NULL && h2 == NULL);
}
