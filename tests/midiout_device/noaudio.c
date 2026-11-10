/* The no-device contract: the synth and the mapper are enumerated but cannot be opened without a wave device.
 * Every test here runs only when waveOutGetNumDevs() == 0 (the `noaudio` VM configuration) and skips otherwise;
 * the live areas skip in that configuration, so this file is what that configuration checks.
 */
#include "mod.h"

#define NEED_NOAUDIO() do { REQUIRE(U(midiOutOpen)); REQUIRE(U(midiOutGetNumDevs)); REQUIRE(U(waveOutGetNumDevs)); \
    if (mod_nwave() != 0) { t_skip("a wave device exists (%u): live tests cover this", mod_nwave()); return; } } while (0)

/* midiOutOpen(MIDI_MAPPER) = MMSYSERR_ERROR although the synth is listed [VM] */
TEST(midiout_device_noaudio_open_mapper_error) {
    NEED_NOAUDIO();
    static const DWORD fl[] = { CALLBACK_NULL, CALLBACK_EVENT, CALLBACK_FUNCTION, CALLBACK_THREAD };
    for (int i = 0; i < NELEM(fl); i++) {
        HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
        DWORD_PTR cb = 0;
        if (fl[i] == CALLBACK_EVENT) cb = (DWORD_PTR)ev;
        else if (fl[i] == CALLBACK_FUNCTION) cb = (DWORD_PTR)mod_cb;
        else if (fl[i] == CALLBACK_THREAD) cb = GetCurrentThreadId();
        mod_rec_reset();
        HMIDIOUT h = MOD_SENT;
        MMRESULT r = mod_open(&h, MIDI_MAPPER, cb, 0, fl[i]);
        CHECKF(r == MMSYSERR_ERROR, "flags 0x%lx r=%u", fl[i], r);
        CHECKF(h == NULL, "flags 0x%lx handle %p", fl[i], h);
        if (r == MMSYSERR_NOERROR) mod_close(h);
        /* a failed open sends no MOM_OPEN */
        Sleep(20);
        CHECKF(mod_rec_count() == 0, "flags 0x%lx: %d callbacks", fl[i], mod_rec_count());
        if (fl[i] == CALLBACK_EVENT) CHECK(WaitForSingleObject(ev, 0) == WAIT_TIMEOUT);
        CloseHandle(ev);
    }
}

TEST(midiout_device_noaudio_open_id0) {
    NEED_NOAUDIO();
    HMIDIOUT h = MOD_SENT;
    MMRESULT r = mod_open(&h, 0, 0, 0, CALLBACK_NULL);
    t_note("noaudio midiOutOpen(0) = %u h=%p", r, h);
    CHECK(r != MMSYSERR_NOERROR);
    CHECK(h == NULL);
    if (r == MMSYSERR_NOERROR) mod_close(h);
}

/* the counts and caps are unaffected: id 0 is the synth, the mapper answers */
TEST(midiout_device_noaudio_caps_still_answer) {
    NEED_NOAUDIO(); REQUIRE(U(midiOutGetDevCapsW)); REQUIRE(U(midiOutGetDevCapsA));
    CHECK(mod_nout() >= 1);
    MIDIOUTCAPSW cw; memset(&cw, 0, sizeof(cw));
    EXPECT_EQ(U(midiOutGetDevCapsW)(0, &cw, sizeof(cw)), MMSYSERR_NOERROR);
    EXPECT_EQ(cw.wTechnology, MOD_SWSYNTH);
    CHECK(cw.dwSupport & MIDICAPS_VOLUME);
    MIDIOUTCAPSA ca; memset(&ca, 0, sizeof(ca));
    EXPECT_EQ(U(midiOutGetDevCapsA)(MIDI_MAPPER, &ca, sizeof(ca)), MMSYSERR_NOERROR);
    EXPECT_EQ(ca.wTechnology, MOD_MAPPER);
    EXPECT_EQ(U(midiOutGetDevCapsA)(mod_nout(), &ca, sizeof(ca)), MMSYSERR_BADDEVICEID);
}

/* repeated failing opens leave nothing behind and keep failing the same way */
TEST(midiout_device_noaudio_open_repeat_stable) {
    NEED_NOAUDIO();
    MMRESULT first = (MMRESULT)-1;
    for (int k = 0; k < 8; k++) {
        HMIDIOUT h = MOD_SENT;
        MMRESULT r = mod_open(&h, (k & 1) ? 0 : MIDI_MAPPER, 0, 0, CALLBACK_NULL);
        if (r == MMSYSERR_NOERROR) { mod_close(h); continue; }
        CHECK(h == NULL);
        if (!(k & 1)) CHECKF(r == MMSYSERR_ERROR, "round %d r=%u", k, r);
        else if (first == (MMRESULT)-1) first = r;
        else CHECKF(r == first, "round %d r=%u first=%u", k, r, first);
    }
    EXPECT_EQ(U(midiOutGetNumDevs)(), mod_nout());
}

/* argument gates still come first: the device answer is reached only by valid arguments */
TEST(midiout_device_noaudio_gates_before_device) {
    NEED_NOAUDIO();
    HMIDIOUT h = MOD_SENT;
    EXPECT_EQ(U(midiOutOpen)(NULL, MIDI_MAPPER, 0, 0, 0), MMSYSERR_INVALPARAM);
    EXPECT_EQ(U(midiOutOpen)(&h, MIDI_MAPPER, 0, 0, 2), MMSYSERR_INVALFLAG);
    CHECK(h == MOD_SENT);
    EXPECT_EQ(U(midiOutOpen)(&h, mod_nout(), 0, 0, 0), MMSYSERR_BADDEVICEID);
    CHECK(h == NULL);
}

TEST(midiout_device_noaudio_stream_open) {
    NEED_NOAUDIO(); REQUIRE(U(midiStreamOpen)); REQUIRE(U(midiStreamClose));
    static const UINT ids[] = { 0, MIDI_MAPPER };
    for (int i = 0; i < NELEM(ids); i++) {
        HMIDISTRM s = (HMIDISTRM)MOD_SENT;
        UINT id = ids[i];
        MMRESULT r = U(midiStreamOpen)(&s, &id, 1, 0, 0, CALLBACK_NULL);
        t_note("noaudio midiStreamOpen(0x%x) = %u", ids[i], r);
        CHECK(r != MMSYSERR_NOERROR);
        if (r == MMSYSERR_NOERROR) { U(midiStreamClose)(s); continue; }
        CHECK(s == NULL);
    }
}

/* volume by id with nothing open: the synth has no driver instance [VM, midiout_contract] */
TEST(midiout_device_noaudio_volume_by_id) {
    NEED_NOAUDIO(); REQUIRE(U(midiOutGetVolume));
    DWORD v = 0xA5A5A5A5;
    MMRESULT r = U(midiOutGetVolume)((HMIDIOUT)0, &v);
    t_note("noaudio midiOutGetVolume(0) = %u v=0x%lx", r, v);
    CHECK(r != MMSYSERR_NOERROR);
    EXPECT_EQ(v, 0xA5A5A5A5);
    v = 0xA5A5A5A5;
    EXPECT_EQ(U(midiOutGetVolume)((HMIDIOUT)(ULONG_PTR)MIDI_MAPPER, &v), MMSYSERR_NOTSUPPORTED);
    EXPECT_EQ(v, 0xA5A5A5A5);
}

/* with no handle in this process the handle-taking calls on NULL answer INVALHANDLE as everywhere */
TEST(midiout_device_noaudio_null_handle_calls) {
    NEED_NOAUDIO(); REQUIRE(U(midiOutShortMsg)); REQUIRE(U(midiOutReset)); REQUIRE(U(midiOutClose)); REQUIRE(U(midiOutGetID));
    UINT id = 77;
    EXPECT_EQ(U(midiOutShortMsg)(NULL, MOD_MSG(0xB0, 123, 0)), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(midiOutReset)(NULL), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(midiOutClose)(NULL), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(midiOutGetID)(NULL, &id), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(id, 77);
}
