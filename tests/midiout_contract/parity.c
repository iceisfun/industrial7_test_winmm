/* winmm.dll exports midiOut* as thin thunks into winmmbase.dll (same implementation). Each of the 15 entry
 * points, resolved from each module, answers the same error ladder. Ordinals in winmm: CacheDrumPatches 67,
 * CachePatches 68, Close 69, GetDevCapsA 70, GetDevCapsW 71, GetID 74, GetVolume 76, LongMsg 77,
 * Message 78, Open 79, PrepareHeader 80, Reset 81, SetVolume 82, ShortMsg 83, UnprepareHeader 84.
 */
#include "moc.h"

static const char *mods[] = { "winmm.dll", "winmmbase.dll" };

TEST(midiout_contract_parity_all_exports_present) {
    static const char *names[] = { "midiOutOpen", "midiOutClose", "midiOutGetDevCapsA", "midiOutGetDevCapsW",
        "midiOutShortMsg", "midiOutLongMsg", "midiOutPrepareHeader", "midiOutUnprepareHeader", "midiOutReset",
        "midiOutGetID", "midiOutGetVolume", "midiOutSetVolume", "midiOutCachePatches", "midiOutCacheDrumPatches",
        "midiOutMessage" };
    for (int m = 0; m < 2; m++)
        for (int i = 0; i < NELEM(names); i++)
            CHECKF(t_proc_in(mods[m], names[i]) != NULL, "%s!%s missing", mods[m], names[i]);
}

TEST(midiout_contract_parity_winmm_ordinals) {
    static const struct { unsigned ord; const char *name; } t[] = {
        { 67, "midiOutCacheDrumPatches" }, { 68, "midiOutCachePatches" }, { 69, "midiOutClose" },
        { 70, "midiOutGetDevCapsA" }, { 71, "midiOutGetDevCapsW" }, { 74, "midiOutGetID" },
        { 76, "midiOutGetVolume" }, { 77, "midiOutLongMsg" }, { 78, "midiOutMessage" }, { 79, "midiOutOpen" },
        { 80, "midiOutPrepareHeader" }, { 81, "midiOutReset" }, { 82, "midiOutSetVolume" },
        { 83, "midiOutShortMsg" }, { 84, "midiOutUnprepareHeader" },
    };
    for (int i = 0; i < NELEM(t); i++) {
        FARPROC byname = t_proc_in("winmm.dll", t[i].name);
        FARPROC byord = t_proc_ord("winmm.dll", t[i].ord);
        REQUIRE(byname);
        CHECKF(byname == byord, "winmm ordinal %u is not %s", t[i].ord, t[i].name);
    }
}

#define P(mod, fn) ((__typeof__(&fn))t_proc_in((mod), #fn))

TEST(midiout_contract_parity_error_ladders) {
    for (int m = 0; m < 2; m++) {
        const char *d = mods[m];
        REQUIRE(P(d, midiOutOpen)); REQUIRE(P(d, midiOutClose)); REQUIRE(P(d, midiOutShortMsg));
        REQUIRE(P(d, midiOutLongMsg)); REQUIRE(P(d, midiOutPrepareHeader)); REQUIRE(P(d, midiOutUnprepareHeader));
        REQUIRE(P(d, midiOutReset)); REQUIRE(P(d, midiOutGetID)); REQUIRE(P(d, midiOutGetVolume));
        REQUIRE(P(d, midiOutSetVolume)); REQUIRE(P(d, midiOutCachePatches)); REQUIRE(P(d, midiOutCacheDrumPatches));
        REQUIRE(P(d, midiOutMessage)); REQUIRE(P(d, midiOutGetDevCapsA)); REQUIRE(P(d, midiOutGetDevCapsW));
        HMIDIOUT h = MOC_SENT_HANDLE; UINT id; DWORD v; WORD arr[128]; char b[8];
        MIDIHDR mh; moc_hdr(&mh, b, 8, 0);
        MIDIOUTCAPSA ca; MIDIOUTCAPSW cw;
        CHECKF(P(d, midiOutOpen)(NULL, 0, 0, 0, 0) == MMSYSERR_INVALPARAM, "%s open NULL", d);
        CHECKF(P(d, midiOutOpen)(&h, 0, 0, 0, 2) == MMSYSERR_INVALFLAG, "%s open flag", d);
        CHECKF(P(d, midiOutOpen)(&h, 9, 0, 0, 0) == MMSYSERR_BADDEVICEID && h == NULL, "%s open id", d);
        CHECKF(P(d, midiOutClose)(NULL) == MMSYSERR_INVALHANDLE, "%s close", d);
        CHECKF(P(d, midiOutShortMsg)(NULL, 0) == MMSYSERR_INVALHANDLE, "%s shortmsg", d);
        CHECKF(P(d, midiOutLongMsg)(NULL, &mh, sizeof(mh)) == MIDIERR_UNPREPARED, "%s longmsg", d);
        CHECKF(P(d, midiOutPrepareHeader)(NULL, &mh, sizeof(mh)) == MMSYSERR_INVALHANDLE, "%s prepare", d);
        CHECKF(P(d, midiOutUnprepareHeader)(NULL, &mh, sizeof(mh)) == MMSYSERR_INVALHANDLE, "%s unprepare", d);
        CHECKF(P(d, midiOutReset)(NULL) == MMSYSERR_INVALHANDLE, "%s reset", d);
        CHECKF(P(d, midiOutGetID)(NULL, NULL) == MMSYSERR_INVALPARAM, "%s getid NULL", d);
        CHECKF(P(d, midiOutGetID)(NULL, &id) == MMSYSERR_INVALHANDLE, "%s getid", d);
        CHECKF(P(d, midiOutGetVolume)((HMIDIOUT)9, &v) == MMSYSERR_BADDEVICEID, "%s getvolume", d);
        CHECKF(P(d, midiOutGetVolume)((HMIDIOUT)9, NULL) == MMSYSERR_INVALPARAM, "%s getvolume NULL", d);
        CHECKF(P(d, midiOutSetVolume)((HMIDIOUT)9, 0) == MMSYSERR_BADDEVICEID, "%s setvolume", d);
        CHECKF(P(d, midiOutCachePatches)(NULL, 0, NULL, 0) == MMSYSERR_INVALPARAM, "%s cache NULL", d);
        CHECKF(P(d, midiOutCachePatches)(NULL, 0, arr, 8) == MMSYSERR_INVALFLAG, "%s cache flag", d);
        CHECKF(P(d, midiOutCacheDrumPatches)(NULL, 0, arr, 0) == MMSYSERR_INVALHANDLE, "%s drum", d);
        CHECKF(P(d, midiOutMessage)((HMIDIOUT)9, 0, 0, 0) == MMSYSERR_BADDEVICEID, "%s message", d);
        CHECKF(P(d, midiOutGetDevCapsA)(9, &ca, sizeof(ca)) == MMSYSERR_BADDEVICEID, "%s capsA", d);
        CHECKF(P(d, midiOutGetDevCapsW)(9, &cw, sizeof(cw)) == MMSYSERR_BADDEVICEID, "%s capsW", d);
        CHECKF(P(d, midiOutGetDevCapsA)(0, NULL, 0) == MMSYSERR_NOERROR, "%s capsA cb0", d);
        CHECKF(P(d, midiOutGetDevCapsW)(0, NULL, 4) == MMSYSERR_INVALPARAM, "%s capsW NULL", d);
    }
}

/* the id-based paths give byte-identical caps through both modules */
TEST(midiout_contract_parity_caps_bytes) {
    for (int k = 0; k < 2; k++) {
        UINT id = k ? MIDI_MAPPER : 0;
        MIDIOUTCAPS2A a1, a2; MIDIOUTCAPS2W w1, w2;
        /* the same fill in both: bytes past the name's NUL are left as the caller had them (Win10 22H2 oracle: a 0 vs 0x11 fill differed) */
        memset(&a1, 0x11, sizeof(a1)); memset(&a2, 0x11, sizeof(a2)); memset(&w1, 0x11, sizeof(w1)); memset(&w2, 0x11, sizeof(w2));
        REQUIRE(P(mods[0], midiOutGetDevCapsA)); REQUIRE(P(mods[1], midiOutGetDevCapsA));
        EXPECT_EQ(P(mods[0], midiOutGetDevCapsA)(id, (MIDIOUTCAPSA *)&a1, sizeof(a1)), MMSYSERR_NOERROR);
        EXPECT_EQ(P(mods[1], midiOutGetDevCapsA)(id, (MIDIOUTCAPSA *)&a2, sizeof(a2)), MMSYSERR_NOERROR);
        CHECKF(!memcmp(&a1, &a2, sizeof(a1)), "A id 0x%x", id);
        EXPECT_EQ(P(mods[0], midiOutGetDevCapsW)(id, (MIDIOUTCAPSW *)&w1, sizeof(w1)), MMSYSERR_NOERROR);
        EXPECT_EQ(P(mods[1], midiOutGetDevCapsW)(id, (MIDIOUTCAPSW *)&w2, sizeof(w2)), MMSYSERR_NOERROR);
        CHECKF(!memcmp(&w1, &w2, sizeof(w1)), "W id 0x%x", id);
    }
}

/* a handle opened through winmm is usable through winmmbase and the reverse (one handle table) */
TEST(midiout_contract_parity_handle_shared) {
    REQUIRE(P(mods[0], midiOutOpen)); REQUIRE(P(mods[1], midiOutClose)); REQUIRE(P(mods[1], midiOutGetID));
    if (!moc_can_open()) { t_skip("no wave device"); return; }
    HMIDIOUT h = NULL; UINT id = 0xA5;
    EXPECT_EQ(P(mods[0], midiOutOpen)(&h, 0, 0, 0, CALLBACK_NULL), MMSYSERR_NOERROR);
    if (!h) return;
    EXPECT_EQ(P(mods[1], midiOutGetID)(h, &id), MMSYSERR_NOERROR);
    EXPECT_EQ(id, 0);
    EXPECT_EQ(P(mods[1], midiOutClose)(h), MMSYSERR_NOERROR);
    EXPECT_EQ(P(mods[0], midiOutClose)(h), MMSYSERR_INVALHANDLE);
}
