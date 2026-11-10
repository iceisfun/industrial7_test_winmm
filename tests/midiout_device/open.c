/* Live open/close/GetID on the synth (id 0) and the mapper, and the single-open rule.
 *
 * midiOutOpen = NewHandle(type 3) + MODM_OPEN to the driver; midiOutClose = MODM_CLOSE then FreeHandle [D].
 * midiOutGetID copies the device id stored in the handle; it only accepts type-3 handles, so a midiStream handle
 * (type 9) answers 5 [D winmmbase!midiOutGetID 0x94a0].
 * The synth is single-open per process [VM]: a second open of id 0, of the mapper, or a midiStreamOpen on either,
 * answers MMSYSERR_ALLOCATED while a handle is open.
 */
#include "mod.h"

/* ================================================================ basic opens */

TEST(midiout_device_open_id0) {
    MOD_NEED_LIVE(); REQUIRE(U(midiOutGetID));
    HMIDIOUT h = MOD_SENT;
    EXPECT_EQ(mod_open(&h, 0, 0, 0, CALLBACK_NULL), MMSYSERR_NOERROR);
    CHECK(h != NULL && h != MOD_SENT);
    CHECKF((ULONG_PTR)h > 0xFFFF, "handle %p", h);
    UINT id = 0xA5A5;
    EXPECT_EQ(U(midiOutGetID)(h, &id), MMSYSERR_NOERROR);
    EXPECT_EQ(id, 0);
    mod_close(h);
}

TEST(midiout_device_open_mapper) {
    MOD_NEED_LIVE(); REQUIRE(U(midiOutGetID));
    HMIDIOUT h = MOD_SENT;
    EXPECT_EQ(mod_open(&h, MIDI_MAPPER, 0, 0, CALLBACK_NULL), MMSYSERR_NOERROR);
    CHECK(h != NULL && h != MOD_SENT);
    UINT id = 0xA5A5;
    EXPECT_EQ(U(midiOutGetID)(h, &id), MMSYSERR_NOERROR);
    EXPECT_EQ(id, 0xFFFFFFFF);   /* the mapper id, not the synth it maps to [VM] */
    mod_close(h);
}

TEST(midiout_device_getid_null_11_live) {
    MOD_NEED_LIVE(); REQUIRE(U(midiOutGetID));
    static const UINT ids[] = { 0, MIDI_MAPPER };
    for (int i = 0; i < NELEM(ids); i++) {
        HMIDIOUT h = mod_open_null(ids[i]);
        if (!h) continue;
        EXPECT_EQ(U(midiOutGetID)(h, NULL), MMSYSERR_INVALPARAM);
        mod_close(h);
    }
}

/* GetID is stable over the life of the handle: after messages, a reset, volume reads */
TEST(midiout_device_getid_stable) {
    MOD_NEED_LIVE(); REQUIRE(U(midiOutGetID)); REQUIRE(U(midiOutGetVolume));
    static const UINT ids[] = { 0, MIDI_MAPPER };
    for (int i = 0; i < NELEM(ids); i++) {
        HMIDIOUT h = mod_open_null(ids[i]);
        if (!h) continue;
        for (int k = 0; k < 4; k++) {
            UINT id = 0x1234;
            if (k == 1) U(midiOutShortMsg)(h, MOD_MSG(0xC0, 0, 0));
            if (k == 2) U(midiOutReset)(h);
            if (k == 3) { DWORD v; U(midiOutGetVolume)(h, &v); }
            CHECKF(U(midiOutGetID)(h, &id) == MMSYSERR_NOERROR, "id 0x%x step %d", ids[i], k);
            CHECKF(id == ids[i], "id 0x%x step %d got 0x%x", ids[i], k, id);
        }
        mod_close(h);
    }
}

/* every flag value the open masks let through opens the synth (0x4, 0x8, 0x4000 are outside 0x3FF3/0xBFF3) */
/* UNVERIFIED: pin after the first Win10 run */
TEST(midiout_device_open_tolerated_flag_bits) {
    MOD_NEED_LIVE();
    static const DWORD extra[] = { 0, 0x4, 0x8, 0xC, 0x4000, 0x400C };
    static const UINT ids[] = { 0, MIDI_MAPPER };
    for (int i = 0; i < NELEM(ids); i++)
        for (int j = 0; j < NELEM(extra); j++) {
            HMIDIOUT h = MOD_SENT;
            MMRESULT r = mod_open(&h, ids[i], 0, 0, CALLBACK_NULL | extra[j]);
            CHECKF(r == MMSYSERR_NOERROR, "id 0x%x flags 0x%lx r=%u", ids[i], extra[j], r);
            if (r == MMSYSERR_NOERROR) mod_close(h);
            else CHECK(h == NULL);
        }
}

/* a flag bit inside the mask is refused before the device is touched, also while the synth is open */
TEST(midiout_device_open_bad_flags_while_open) {
    MOD_NEED_LIVE();
    HMIDIOUT h = mod_open_null(0);
    if (!h) return;
    static const DWORD bad[] = { 0x1, 0x2, 0x10, 0x20, 0x100, 0x1000, 0x2000 };
    for (int j = 0; j < NELEM(bad); j++) {
        HMIDIOUT h2 = MOD_SENT;
        MMRESULT r = U(midiOutOpen)(&h2, 0, 0, 0, bad[j]);
        CHECKF(r == MMSYSERR_INVALFLAG, "flags 0x%lx r=%u", bad[j], r);
        if (r == MMSYSERR_NOERROR) U(midiOutClose)(h2);
    }
    mod_close(h);
}

/* ================================================================ single-open rule */

static void check_allocated_while(UINT open_id) {
    HMIDIOUT h = mod_open_null(open_id);
    if (!h) return;
    static const UINT ids[] = { 0, MIDI_MAPPER };
    for (int i = 0; i < NELEM(ids); i++) {
        HMIDIOUT h2 = MOD_SENT;
        MMRESULT r = U(midiOutOpen)(&h2, ids[i], 0, 0, CALLBACK_NULL);
        CHECKF(r == MMSYSERR_ALLOCATED, "open 0x%x while 0x%x open: r=%u", ids[i], open_id, r);
        CHECKF(h2 == NULL, "open 0x%x while 0x%x open: handle %p", ids[i], open_id, h2);
        if (r == MMSYSERR_NOERROR) U(midiOutClose)(h2);
    }
    /* the first handle is unaffected */
    EXPECT_EQ(U(midiOutShortMsg)(h, MOD_MSG(0xB0, 123, 0)), MMSYSERR_NOERROR);
    mod_close(h);
}

TEST(midiout_device_allocated_while_id0_open) { MOD_NEED_LIVE(); check_allocated_while(0); }
TEST(midiout_device_allocated_while_mapper_open) { MOD_NEED_LIVE(); check_allocated_while(MIDI_MAPPER); }

TEST(midiout_device_allocated_stream_open) {
    MOD_NEED_LIVE(); REQUIRE(U(midiStreamOpen)); REQUIRE(U(midiStreamClose));
    static const UINT first[] = { 0, MIDI_MAPPER };
    for (int f = 0; f < NELEM(first); f++) {
        HMIDIOUT h = mod_open_null(first[f]);
        if (!h) continue;
        static const UINT ids[] = { 0, MIDI_MAPPER };
        for (int i = 0; i < NELEM(ids); i++) {
            HMIDISTRM s = (HMIDISTRM)MOD_SENT;
            UINT id = ids[i];
            MMRESULT r = U(midiStreamOpen)(&s, &id, 1, 0, 0, CALLBACK_NULL);
            CHECKF(r == MMSYSERR_ALLOCATED, "stream 0x%x while midiOut 0x%x open: r=%u", ids[i], first[f], r);
            CHECKF(s == NULL, "stream handle %p", s);
            if (r == MMSYSERR_NOERROR) U(midiStreamClose)(s);
        }
        mod_close(h);
    }
}

/* the other direction: a stream open on the synth blocks midiOutOpen */
TEST(midiout_device_allocated_by_stream) {
    MOD_NEED_LIVE(); REQUIRE(U(midiStreamOpen)); REQUIRE(U(midiStreamClose));
    HMIDISTRM s = NULL; UINT id = 0;
    MMRESULT r = U(midiStreamOpen)(&s, &id, 1, 0, 0, CALLBACK_NULL);
    EXPECT_EQ(r, MMSYSERR_NOERROR);
    if (r) return;
    HMIDIOUT h = MOD_SENT;
    r = U(midiOutOpen)(&h, 0, 0, 0, CALLBACK_NULL);
    EXPECT_EQ(r, MMSYSERR_ALLOCATED);
    if (r == MMSYSERR_NOERROR) U(midiOutClose)(h);
    h = MOD_SENT;
    r = U(midiOutOpen)(&h, MIDI_MAPPER, 0, 0, CALLBACK_NULL);
    EXPECT_EQ(r, MMSYSERR_ALLOCATED);
    if (r == MMSYSERR_NOERROR) U(midiOutClose)(h);
    EXPECT_EQ(U(midiStreamClose)(s), MMSYSERR_NOERROR);
    /* free again */
    h = mod_open_null(0);
    if (h) mod_close(h);
}

/* a failed (ALLOCATED) open notifies nobody: no MOM_OPEN / MOM_CLOSE for its callback */
TEST(midiout_device_allocated_no_callback) {
    MOD_NEED_LIVE();
    HMIDIOUT h = mod_open_null(0);
    if (!h) return;
    mod_rec_reset();
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    HMIDIOUT h2 = MOD_SENT;
    EXPECT_EQ(U(midiOutOpen)(&h2, 0, (DWORD_PTR)mod_cb, 7, CALLBACK_FUNCTION), MMSYSERR_ALLOCATED);
    h2 = MOD_SENT;
    EXPECT_EQ(U(midiOutOpen)(&h2, MIDI_MAPPER, (DWORD_PTR)ev, 0, CALLBACK_EVENT), MMSYSERR_ALLOCATED);
    Sleep(50);
    EXPECT_EQ(mod_rec_count(), 0);
    EXPECT_EQ(WaitForSingleObject(ev, 0), WAIT_TIMEOUT);
    CloseHandle(ev);
    mod_close(h);
}

/* after the close the synth is free again, in either order of ids */
TEST(midiout_device_reopen_sequences) {
    MOD_NEED_LIVE();
    static const UINT seq[] = { 0, 0, MIDI_MAPPER, MIDI_MAPPER, 0, MIDI_MAPPER, 0 };
    for (int i = 0; i < NELEM(seq); i++) {
        HMIDIOUT h = MOD_SENT;
        MMRESULT r = mod_open(&h, seq[i], 0, 0, CALLBACK_NULL);
        CHECKF(r == MMSYSERR_NOERROR, "step %d id 0x%x r=%u", i, seq[i], r);
        if (r) continue;
        CHECKF(U(midiOutShortMsg)(h, MOD_MSG(0xB0, 121, 0)) == MMSYSERR_NOERROR, "step %d", i);  /* reset controllers */
        mod_close(h);
    }
}

/* racing opens from several threads: exactly one wins, the rest are ALLOCATED */
typedef struct { UINT id; HANDLE go; MMRESULT r; HMIDIOUT h; } race_t;
static DWORD WINAPI race_thread(LPVOID p) {
    race_t *t = (race_t *)p;
    WaitForSingleObject(t->go, 2000);
    t->h = NULL;
    t->r = U(midiOutOpen)(&t->h, t->id, 0, 0, CALLBACK_NULL);
    return 0;
}

TEST(midiout_device_open_race_one_winner) {
    MOD_NEED_LIVE();
    for (int round = 0; round < 3; round++) {
        race_t t[4]; HANDLE th[4];
        HANDLE go = CreateEventA(NULL, TRUE, FALSE, NULL);
        for (int i = 0; i < 4; i++) {
            t[i].id = (i & 1) ? MIDI_MAPPER : 0; t[i].go = go; t[i].r = 0xFFFF; t[i].h = NULL;
            th[i] = CreateThread(NULL, 0, race_thread, &t[i], 0, NULL);
        }
        int made = 1;
        for (int i = 0; i < 4; i++) if (!th[i]) made = 0;
        SetEvent(go);
        if (!made) {
            for (int i = 0; i < 4; i++) if (th[i]) { WaitForSingleObject(th[i], 5000); CloseHandle(th[i]); }
            CloseHandle(go);
            CHECKF(0, "round %d: CreateThread failed", round);
            return;
        }
        DWORD w = WaitForMultipleObjects(4, th, TRUE, 5000);
        CHECKF(w != WAIT_TIMEOUT, "round %d: threads did not finish", round);
        int ok = 0, alloc = 0;
        for (int i = 0; i < 4; i++) {
            if (t[i].r == MMSYSERR_NOERROR) ok++;
            else if (t[i].r == MMSYSERR_ALLOCATED) alloc++;
            else CHECKF(0, "round %d thread %d r=%u", round, i, t[i].r);
        }
        CHECKF(ok == 1, "round %d: %d winners", round, ok);
        EXPECT_EQ(alloc, 4 - ok);
        for (int i = 0; i < 4; i++)
            if (t[i].r == MMSYSERR_NOERROR) {
                mod_quiet(t[i].h);
                EXPECT_EQ(U(midiOutClose)(t[i].h), MMSYSERR_NOERROR);
            }
        for (int i = 0; i < 4; i++) CloseHandle(th[i]);
        CloseHandle(go);
    }
}

/* a handle opened on one thread is used and closed on another */
typedef struct { HMIDIOUT h; MMRESULT rs, rr, rc; UINT id; MMRESULT rid; } xthr_t;
static DWORD WINAPI xthr_use(LPVOID p) {
    xthr_t *x = (xthr_t *)p;
    x->rs = U(midiOutShortMsg)(x->h, MOD_MSG(0xB0, 123, 0));
    x->rid = U(midiOutGetID)(x->h, &x->id);
    x->rr = U(midiOutReset)(x->h);
    x->rc = U(midiOutClose)(x->h);
    return 0;
}

TEST(midiout_device_cross_thread_close) {
    MOD_NEED_LIVE(); REQUIRE(U(midiOutGetID));
    static const UINT ids[] = { 0, MIDI_MAPPER };
    for (int i = 0; i < NELEM(ids); i++) {
        HMIDIOUT h = mod_open_null(ids[i]);
        if (!h) continue;
        xthr_t x; memset(&x, 0, sizeof(x)); x.h = h; x.id = 0x77;
        HANDLE th = CreateThread(NULL, 0, xthr_use, &x, 0, NULL);
        CHECK(WaitForSingleObject(th, 5000) == WAIT_OBJECT_0);
        CloseHandle(th);
        EXPECT_EQ(x.rs, MMSYSERR_NOERROR);
        EXPECT_EQ(x.rid, MMSYSERR_NOERROR);
        EXPECT_EQ(x.id, ids[i]);
        EXPECT_EQ(x.rr, MMSYSERR_NOERROR);
        EXPECT_EQ(x.rc, MMSYSERR_NOERROR);
        if (x.rc == MMSYSERR_NOERROR) mod_forget(h);
        else mod_close(h);
        /* closed from the other thread: gone here too */
        EXPECT_EQ(U(midiOutShortMsg)(h, MOD_MSG(0xB0, 123, 0)), MMSYSERR_INVALHANDLE);
    }
}

/* ================================================================ closed handles */

TEST(midiout_device_closed_handle_calls_5) {
    MOD_NEED_LIVE(); REQUIRE(U(midiOutGetID)); REQUIRE(U(midiOutLongMsg)); REQUIRE(U(midiOutPrepareHeader));
    static const UINT ids[] = { 0, MIDI_MAPPER };
    for (int i = 0; i < NELEM(ids); i++) {
        HMIDIOUT h = mod_open_null(ids[i]);
        if (!h) continue;
        mod_close(h);
        UINT id = 0x77;
        MIDIHDR m; mod_hdr(&m, mod_gs_reset, sizeof(mod_gs_reset)); m.dwFlags = MHDR_PREPARED;
        EXPECT_EQ(U(midiOutClose)(h), MMSYSERR_INVALHANDLE);
        EXPECT_EQ(U(midiOutShortMsg)(h, MOD_MSG(0xB0, 123, 0)), MMSYSERR_INVALHANDLE);
        EXPECT_EQ(U(midiOutReset)(h), MMSYSERR_INVALHANDLE);
        EXPECT_EQ(U(midiOutGetID)(h, &id), MMSYSERR_INVALHANDLE);
        EXPECT_EQ(id, 0x77);
        EXPECT_EQ(U(midiOutLongMsg)(h, &m, sizeof(m)), MMSYSERR_INVALHANDLE);
        EXPECT_EQ(m.dwFlags, MHDR_PREPARED);
    }
}

/* ================================================================ caps through a handle */

TEST(midiout_device_caps_by_handle) {
    MOD_NEED_LIVE(); REQUIRE(U(midiOutGetDevCapsW)); REQUIRE(U(midiOutGetDevCapsA));
    static const UINT ids[] = { 0, MIDI_MAPPER };
    for (int i = 0; i < NELEM(ids); i++) {
        MIDIOUTCAPSW byid; memset(&byid, 0, sizeof(byid));
        EXPECT_EQ(U(midiOutGetDevCapsW)(ids[i], &byid, sizeof(byid)), MMSYSERR_NOERROR);
        HMIDIOUT h = mod_open_null(ids[i]);
        if (!h) continue;
        MIDIOUTCAPSW byh; memset(&byh, 0, sizeof(byh));
        EXPECT_EQ(U(midiOutGetDevCapsW)((UINT_PTR)h, &byh, sizeof(byh)), MMSYSERR_NOERROR);
        EXPECT_WSTR(byh.szPname, byid.szPname);
        EXPECT_EQ(byh.wTechnology, byid.wTechnology);
        EXPECT_EQ(byh.wMid, byid.wMid);
        EXPECT_EQ(byh.wPid, byid.wPid);
        EXPECT_EQ(byh.dwSupport, byid.dwSupport);
        EXPECT_EQ(byh.wChannelMask, byid.wChannelMask);
        if (byh.wVoices != byid.wVoices || byh.wNotes != byid.wNotes || byh.vDriverVersion != byid.vDriverVersion)
            t_note("caps 0x%x by handle voices=%u notes=%u ver=0x%x, by id voices=%u notes=%u ver=0x%x", ids[i],
                   byh.wVoices, byh.wNotes, byh.vDriverVersion, byid.wVoices, byid.wNotes, byid.vDriverVersion);
        MIDIOUTCAPSA ca; memset(&ca, 0, sizeof(ca));
        EXPECT_EQ(U(midiOutGetDevCapsA)((UINT_PTR)h, &ca, sizeof(ca)), MMSYSERR_NOERROR);
        EXPECT_EQ(ca.wTechnology, byid.wTechnology);
        mod_close(h);
        /* after the close the handle value is no longer a device: 2 */
        EXPECT_EQ(U(midiOutGetDevCapsW)((UINT_PTR)h, &byh, sizeof(byh)), MMSYSERR_BADDEVICEID);
    }
}

/* ================================================================ midiOut* on a stream handle [D] */

TEST(midiout_device_stream_handle_in_midiout_calls) {
    MOD_NEED_LIVE(); REQUIRE(U(midiStreamOpen)); REQUIRE(U(midiStreamClose)); REQUIRE(U(midiOutGetID));
    REQUIRE(U(midiOutLongMsg)); REQUIRE(U(midiOutPrepareHeader)); REQUIRE(U(midiOutUnprepareHeader));
    HMIDISTRM s = NULL; UINT id = 0;
    MMRESULT r = U(midiStreamOpen)(&s, &id, 1, 0, 0, CALLBACK_NULL);
    EXPECT_EQ(r, MMSYSERR_NOERROR);
    if (r) return;
    HMIDIOUT h = (HMIDIOUT)s;
    UINT got = 0x77;
    EXPECT_EQ(U(midiOutGetID)(h, &got), MMSYSERR_INVALHANDLE);     /* type 9 is not accepted by GetID [D] */
    EXPECT_EQ(got, 0x77);
    EXPECT_EQ(U(midiOutClose)(h), MMSYSERR_INVALHANDLE);           /* nor by Close [D] */
    EXPECT_EQ(U(midiOutShortMsg)(h, MOD_MSG(0xB0, 123, 0)), MMSYSERR_NOERROR);  /* routed to the stream [D] */
    EXPECT_EQ(U(midiOutReset)(h), MMSYSERR_NOERROR);
    MIDIHDR m; mod_hdr(&m, mod_gs_reset, sizeof(mod_gs_reset));
    EXPECT_EQ(U(midiOutPrepareHeader)(h, &m, sizeof(m)), MMSYSERR_NOERROR);
    EXPECT_EQ(U(midiOutLongMsg)(h, &m, sizeof(m)), MMSYSERR_NOTSUPPORTED);      /* [D] */
    EXPECT_EQ(m.dwFlags & MHDR_DONE, 0);
    EXPECT_EQ(U(midiOutUnprepareHeader)(h, &m, sizeof(m)), MMSYSERR_NOERROR);
    EXPECT_EQ(U(midiStreamClose)(s), MMSYSERR_NOERROR);
}
