#include "mh.h"
/* midiOut / midiIn / midiStream handle validation. midiOut entry points that also drive streams accept type 3
 * (midiOut) or 9 (midiStream); midiOutClose / midiOutGetID accept only type 3; midiConnect's first argument may be a
 * midiIn (4) or a midiOut (3) handle. VM_TIMEOUT: 240 */

static char g_mdata[16];
static void mhdr_init(MIDIHDR *mh, DWORD flags, DWORD len) {
    memset(mh, 0, sizeof *mh); mh->lpData = g_mdata; mh->dwBufferLength = len; mh->dwFlags = flags;
}
#define ALL_OFF 0x000078B0u   /* controller 120 (All Sound Off), channel 0: silent */

/* ---- midiOut ---- */
static ULONG_PTR mo_close(ULONG_PTR h) { return U(midiOutClose)((HMIDIOUT)h); }
static ULONG_PTR mo_reset(ULONG_PTR h) { return U(midiOutReset)((HMIDIOUT)h); }
static ULONG_PTR mo_short(ULONG_PTR h) { return U(midiOutShortMsg)((HMIDIOUT)h, ALL_OFF); }
static ULONG_PTR mo_long(ULONG_PTR h) {
    MIDIHDR mh; mhdr_init(&mh, MHDR_PREPARED, 4); return U(midiOutLongMsg)((HMIDIOUT)h, &mh, sizeof mh);
}
static ULONG_PTR mo_prep_null(ULONG_PTR h) { return U(midiOutPrepareHeader)((HMIDIOUT)h, NULL, sizeof(MIDIHDR)); }
static ULONG_PTR mo_unprep_null(ULONG_PTR h) { return U(midiOutUnprepareHeader)((HMIDIOUT)h, NULL, sizeof(MIDIHDR)); }
static ULONG_PTR mo_getid(ULONG_PTR h) {
    UINT id = 0xA5A5A5A5; MMRESULT r = U(midiOutGetID)((HMIDIOUT)h, &id);
    if (r) CHECKF(id == 0xA5A5A5A5, "midiOutGetID failure wrote 0x%x", id);
    return r;
}
static ULONG_PTR mo_cache(ULONG_PTR h) {
    WORD pa[MIDIPATCHSIZE]; memset(pa, 0, sizeof pa); return U(midiOutCachePatches)((HMIDIOUT)h, 0, pa, MIDI_CACHE_QUERY);
}
static ULONG_PTR mo_cachedrum(ULONG_PTR h) {
    WORD ka[MIDIPATCHSIZE]; memset(ka, 0, sizeof ka); return U(midiOutCacheDrumPatches)((HMIDIOUT)h, 0, ka, MIDI_CACHE_QUERY);
}
#define M3 (1u << MHT_MIDIOUT)
#define M39 ((1u << MHT_MIDIOUT) | (1u << MHT_STREAM))
const mh_op MH_MIDIOUT_OPS[] = {
    { "midiOutClose", mo_close, 5, M3 },
    { "midiOutReset", mo_reset, 5, M39 },
    { "midiOutShortMsg", mo_short, 5, M39 },
    { "midiOutLongMsg", mo_long, 5, M39 },
    { "midiOutPrepareHeader(NULL hdr)", mo_prep_null, 5, M39 },
    { "midiOutUnprepareHeader(NULL hdr)", mo_unprep_null, 5, M39 },
    { "midiOutGetID", mo_getid, 5, M3 },
    { "midiOutCachePatches", mo_cache, 5, M39 },
    { "midiOutCacheDrumPatches", mo_cachedrum, 5, M39 },
};
const int MH_NMIDIOUT_OPS = sizeof MH_MIDIOUT_OPS / sizeof MH_MIDIOUT_OPS[0];

/* ---- midiIn ---- */
static ULONG_PTR mi_close(ULONG_PTR h) { return U(midiInClose)((HMIDIIN)h); }
static ULONG_PTR mi_reset(ULONG_PTR h) { return U(midiInReset)((HMIDIIN)h); }
static ULONG_PTR mi_start(ULONG_PTR h) { return U(midiInStart)((HMIDIIN)h); }
static ULONG_PTR mi_stop(ULONG_PTR h) { return U(midiInStop)((HMIDIIN)h); }
static ULONG_PTR mi_getid(ULONG_PTR h) {
    UINT id = 0x5A5A5A5A; MMRESULT r = U(midiInGetID)((HMIDIIN)h, &id);
    if (r) CHECKF(id == 0x5A5A5A5A, "midiInGetID failure wrote 0x%x", id);
    return r;
}
static ULONG_PTR mi_prepare(ULONG_PTR h) {
    MIDIHDR mh; mhdr_init(&mh, MHDR_DONE, sizeof g_mdata);
    MMRESULT r = U(midiInPrepareHeader)((HMIDIIN)h, &mh, sizeof mh);
    if (r == MMSYSERR_INVALHANDLE) CHECKF(mh.dwFlags == 0, "midiInPrepareHeader left flags 0x%lx", mh.dwFlags);
    return r;
}
static ULONG_PTR mi_unprepare(ULONG_PTR h) {
    MIDIHDR mh; mhdr_init(&mh, MHDR_PREPARED, sizeof g_mdata); return U(midiInUnprepareHeader)((HMIDIIN)h, &mh, sizeof mh);
}
static ULONG_PTR mi_add(ULONG_PTR h) {
    MIDIHDR mh; mhdr_init(&mh, MHDR_PREPARED, sizeof g_mdata); return U(midiInAddBuffer)((HMIDIIN)h, &mh, sizeof mh);
}
static ULONG_PTR mi_connect(ULONG_PTR h) { return U(midiConnect)((HMIDI)h, NULL, NULL); }
static ULONG_PTR mi_disconnect(ULONG_PTR h) { return U(midiDisconnect)((HMIDI)h, NULL, NULL); }
#define M4 (1u << MHT_MIDIIN)
const mh_op MH_MIDIIN_OPS[] = {
    { "midiInClose", mi_close, 5, M4 },
    { "midiInReset", mi_reset, 5, M4 },
    { "midiInStart", mi_start, 5, M4 },
    { "midiInStop", mi_stop, 5, M4 },
    { "midiInGetID", mi_getid, 5, M4 },
    { "midiInPrepareHeader", mi_prepare, 5, M4 },
    { "midiInUnprepareHeader", mi_unprepare, 5, M4 },
    { "midiInAddBuffer", mi_add, 5, M4 },
    /* the thru source may be a midiIn or a midiOut handle; with a valid source the NULL target is rejected (5) */
    { "midiConnect(h, NULL)", mi_connect, 5, 0 },
    { "midiDisconnect(h, NULL)", mi_disconnect, 5, M4 | M3 },
};
const int MH_NMIDIIN_OPS = sizeof MH_MIDIIN_OPS / sizeof MH_MIDIIN_OPS[0];

/* ---- midiStream ---- */
static ULONG_PTR ms_close(ULONG_PTR h) { return U(midiStreamClose)((HMIDISTRM)h); }
static ULONG_PTR ms_pause(ULONG_PTR h) { return U(midiStreamPause)((HMIDISTRM)h); }
static ULONG_PTR ms_restart(ULONG_PTR h) { return U(midiStreamRestart)((HMIDISTRM)h); }
static ULONG_PTR ms_stop(ULONG_PTR h) { return U(midiStreamStop)((HMIDISTRM)h); }
static ULONG_PTR ms_pos(ULONG_PTR h) {
    MMTIME mt; memset(&mt, 0, sizeof mt); mt.wType = TIME_MS; return U(midiStreamPosition)((HMIDISTRM)h, &mt, sizeof mt);
}
static ULONG_PTR ms_pos_null(ULONG_PTR h) { return U(midiStreamPosition)((HMIDISTRM)h, NULL, sizeof(MMTIME)); }
static ULONG_PTR ms_prop(ULONG_PTR h) {
    MIDIPROPTEMPO t; t.cbStruct = sizeof t; t.dwTempo = 0;
    return U(midiStreamProperty)((HMIDISTRM)h, (LPBYTE)&t, MIDIPROP_GET | MIDIPROP_TEMPO);
}
static ULONG_PTR ms_prop_null(ULONG_PTR h) { return U(midiStreamProperty)((HMIDISTRM)h, NULL, 0); }
static ULONG_PTR ms_out_null(ULONG_PTR h) { return U(midiStreamOut)((HMIDISTRM)h, NULL, 0); }
#define M9 (1u << MHT_STREAM)
const mh_op MH_STREAM_OPS[] = {
    { "midiStreamClose", ms_close, 5, M9 },
    { "midiStreamPause", ms_pause, 5, M9 },
    { "midiStreamRestart", ms_restart, 5, M9 },
    { "midiStreamStop", ms_stop, 5, M9 },
    { "midiStreamPosition", ms_pos, 5, M9 },
    { "midiStreamPosition(NULL)", ms_pos_null, 5, M9 },       /* handle checked before the pointer */
    { "midiStreamProperty", ms_prop, 5, M9 },
    { "midiStreamProperty(NULL, 0)", ms_prop_null, 5, M9 },   /* handle checked before property/pointer */
    { "midiStreamOut(NULL)", ms_out_null, 5, M9 },
};
const int MH_NSTREAM_OPS = sizeof MH_STREAM_OPS / sizeof MH_STREAM_OPS[0];

#define REQ_MIDIOUT() do { REQUIRE(U(midiOutClose)); REQUIRE(U(midiOutReset)); REQUIRE(U(midiOutShortMsg)); \
    REQUIRE(U(midiOutLongMsg)); REQUIRE(U(midiOutPrepareHeader)); REQUIRE(U(midiOutUnprepareHeader)); \
    REQUIRE(U(midiOutGetID)); REQUIRE(U(midiOutCachePatches)); REQUIRE(U(midiOutCacheDrumPatches)); \
    REQUIRE(U(mmioOpenA)); REQUIRE(U(mmioClose)); } while (0)
#define REQ_MIDIIN() do { REQUIRE(U(midiInClose)); REQUIRE(U(midiInReset)); REQUIRE(U(midiInStart)); \
    REQUIRE(U(midiInStop)); REQUIRE(U(midiInGetID)); REQUIRE(U(midiInPrepareHeader)); REQUIRE(U(midiInUnprepareHeader)); \
    REQUIRE(U(midiInAddBuffer)); REQUIRE(U(midiConnect)); REQUIRE(U(midiDisconnect)); REQUIRE(U(mmioOpenA)); } while (0)
#define REQ_STREAM() do { REQUIRE(U(midiStreamClose)); REQUIRE(U(midiStreamPause)); REQUIRE(U(midiStreamRestart)); \
    REQUIRE(U(midiStreamStop)); REQUIRE(U(midiStreamPosition)); REQUIRE(U(midiStreamProperty)); REQUIRE(U(midiStreamOut)); \
    REQUIRE(U(midiStreamOpen)); REQUIRE(U(mmioOpenA)); } while (0)

TEST(mh_midi_device_gating) {
    REQUIRE(U(midiOutGetNumDevs)); REQUIRE(U(midiInGetNumDevs)); REQUIRE(U(midiOutGetDevCapsA));
    const mh_counts *c = mh_devs();
    for (UINT i = 0; i < c->mo; i++) {
        MIDIOUTCAPSA oc; memset(&oc, 0, sizeof oc);
        EXPECT_EQ(U(midiOutGetDevCapsA)(i, &oc, sizeof oc), 0);
        t_note("midiOut %u \"%s\" tech %u", i, oc.szPname, oc.wTechnology);
    }
    t_note("midiIn devices %u", c->mi);
}


TEST(mh_midiout_live_hmmio) {
    REQ_MIDIOUT();
    HMMIO m = mh_mmio_open();
    if (!m) { t_skip("mmio memory open failed"); return; }
    mh_expect_bad(MH_MIDIOUT_OPS, MH_NMIDIOUT_OPS, (ULONG_PTR)m, "live HMMIO");
    EXPECT_EQ(U(mmioClose)(m, 0), 0);
}
TEST(mh_midiin_live_hmmio) {
    REQ_MIDIIN();
    HMMIO m = mh_mmio_open();
    if (!m) { t_skip("mmio memory open failed"); return; }
    mh_expect_bad(MH_MIDIIN_OPS, MH_NMIDIIN_OPS, (ULONG_PTR)m, "live HMMIO");
    EXPECT_EQ(U(mmioClose)(m, 0), 0);
}
TEST(mh_stream_live_hmmio) {
    REQ_STREAM();
    HMMIO m = mh_mmio_open();
    if (!m) { t_skip("mmio memory open failed"); return; }
    mh_expect_bad(MH_STREAM_OPS, MH_NSTREAM_OPS, (ULONG_PTR)m, "live HMMIO");
    EXPECT_EQ(U(mmioClose)(m, 0), 0);
}

/* Order of checks that come before the handle (bad handle does not change these results). */
TEST(mh_midiout_param_before_handle) {
    REQ_MIDIOUT();
    for (int b = 0; b < MH_NBAD; b++) {
        HMIDIOUT h = (HMIDIOUT)MH_BAD[b].v; MIDIHDR mh; WORD pa[MIDIPATCHSIZE];
        EXPECT_EQ(U(midiOutGetID)(h, NULL), MMSYSERR_INVALPARAM);
        /* midiOutLongMsg: header first */
        EXPECT_EQ(U(midiOutLongMsg)(h, NULL, sizeof mh), MMSYSERR_INVALPARAM);
        mhdr_init(&mh, MHDR_PREPARED, 4);
        EXPECT_EQ(U(midiOutLongMsg)(h, &mh, 43), MMSYSERR_INVALPARAM);
        mhdr_init(&mh, MHDR_PREPARED | 0x20, 4);
        EXPECT_EQ(U(midiOutLongMsg)(h, &mh, sizeof mh), MMSYSERR_INVALPARAM);
        mhdr_init(&mh, 0, 4);
        EXPECT_EQ(U(midiOutLongMsg)(h, &mh, sizeof mh), MIDIERR_UNPREPARED);
        mhdr_init(&mh, MHDR_PREPARED | MHDR_INQUEUE, 4);
        EXPECT_EQ(U(midiOutLongMsg)(h, &mh, sizeof mh), MIDIERR_STILLPLAYING);
        mhdr_init(&mh, MHDR_PREPARED, 0);
        EXPECT_EQ(U(midiOutLongMsg)(h, &mh, sizeof mh), MMSYSERR_INVALPARAM);
        /* the 44-byte legacy MIDIHDR size is enough for midiOutLongMsg */
        mhdr_init(&mh, MHDR_PREPARED, 4);
        EXPECT_EQ(U(midiOutLongMsg)(h, &mh, 44), MMSYSERR_INVALHANDLE);
        /* midiOutPrepareHeader / Unprepare: handle first, so even a NULL or prepared header gives 5 */
        mhdr_init(&mh, MHDR_PREPARED, 4);
        EXPECT_EQ(U(midiOutPrepareHeader)(h, &mh, sizeof mh), MMSYSERR_INVALHANDLE);
        EXPECT_EQ(mh.dwFlags, MHDR_PREPARED);
        mhdr_init(&mh, 0, 4);
        EXPECT_EQ(U(midiOutUnprepareHeader)(h, &mh, sizeof mh), MMSYSERR_INVALHANDLE);
        EXPECT_EQ(U(midiOutPrepareHeader)(h, &mh, 1), MMSYSERR_INVALHANDLE);
        /* cache patches: pointer, then flags, then handle */
        EXPECT_EQ(U(midiOutCachePatches)(h, 0, NULL, 0), MMSYSERR_INVALPARAM);
        EXPECT_EQ(U(midiOutCachePatches)(h, 0, pa, 8), MMSYSERR_INVALFLAG);
        EXPECT_EQ(U(midiOutCachePatches)(h, 0, pa, 0x80000000u), MMSYSERR_INVALFLAG);
        EXPECT_EQ(U(midiOutCacheDrumPatches)(h, 0, NULL, 0), MMSYSERR_INVALPARAM);
        EXPECT_EQ(U(midiOutCacheDrumPatches)(h, 0, pa, 0x10), MMSYSERR_INVALFLAG);
        EXPECT_EQ(U(midiOutCachePatches)(h, 0, pa, MIDI_CACHE_ALL), MMSYSERR_INVALHANDLE);
        EXPECT_EQ(U(midiOutCacheDrumPatches)(h, 0, pa, MIDI_UNCACHE), MMSYSERR_INVALHANDLE);
    }
}

TEST(mh_midiin_param_before_handle) {
    REQ_MIDIIN();
    for (int b = 0; b < MH_NBAD; b++) {
        HMIDIIN h = (HMIDIIN)MH_BAD[b].v; MIDIHDR mh;
        EXPECT_EQ(U(midiInGetID)(h, NULL), MMSYSERR_INVALPARAM);
        EXPECT_EQ(U(midiInPrepareHeader)(h, NULL, sizeof mh), MMSYSERR_INVALPARAM);
        mhdr_init(&mh, 0, 4);
        EXPECT_EQ(U(midiInPrepareHeader)(h, &mh, 43), MMSYSERR_INVALPARAM);
        EXPECT_EQ(U(midiInAddBuffer)(h, &mh, 43), MMSYSERR_INVALPARAM);
        EXPECT_EQ(U(midiInUnprepareHeader)(h, &mh, 43), MMSYSERR_INVALPARAM);
        mhdr_init(&mh, 0x10, 4);
        EXPECT_EQ(U(midiInPrepareHeader)(h, &mh, sizeof mh), MMSYSERR_INVALPARAM);
        EXPECT_EQ(mh.dwFlags, 0x10);
        mhdr_init(&mh, MHDR_PREPARED, 4);
        EXPECT_EQ(U(midiInPrepareHeader)(h, &mh, sizeof mh), MMSYSERR_NOERROR);      /* already prepared */
        mhdr_init(&mh, 0, 4);
        EXPECT_EQ(U(midiInUnprepareHeader)(h, &mh, sizeof mh), MMSYSERR_NOERROR);    /* not prepared */
        EXPECT_EQ(U(midiInAddBuffer)(h, &mh, sizeof mh), MIDIERR_UNPREPARED);
        mhdr_init(&mh, MHDR_PREPARED | MHDR_INQUEUE, 4);
        EXPECT_EQ(U(midiInAddBuffer)(h, &mh, sizeof mh), MIDIERR_STILLPLAYING);
        EXPECT_EQ(U(midiInUnprepareHeader)(h, &mh, sizeof mh), MIDIERR_STILLPLAYING);
        mhdr_init(&mh, MHDR_DONE, 4);
        EXPECT_EQ(U(midiInPrepareHeader)(h, &mh, 44), MMSYSERR_INVALHANDLE);
        EXPECT_EQ(mh.dwFlags, 0);                                                     /* zeroed before the handle */
    }
}

TEST(mh_stream_param_after_handle) {
    REQ_STREAM();
    /* every stream entry point checks the handle first: bad handle + bad params -> 5 */
    for (int b = 0; b < MH_NBAD; b++) {
        HMIDISTRM h = (HMIDISTRM)MH_BAD[b].v; MIDIHDR mh; DWORD d = 0;
        EXPECT_EQ(U(midiStreamProperty)(h, (LPBYTE)&d, 0), MMSYSERR_INVALHANDLE);
        EXPECT_EQ(U(midiStreamProperty)(h, NULL, MIDIPROP_SET | MIDIPROP_TIMEDIV), MMSYSERR_INVALHANDLE);
        mhdr_init(&mh, 0, 4);
        EXPECT_EQ(U(midiStreamOut)(h, &mh, sizeof mh), MMSYSERR_INVALHANDLE);
        mhdr_init(&mh, MHDR_PREPARED | MHDR_INQUEUE, 4);
        EXPECT_EQ(U(midiStreamOut)(h, &mh, 1), MMSYSERR_INVALHANDLE);
        EXPECT_EQ(U(midiStreamPosition)(h, NULL, 0), MMSYSERR_INVALHANDLE);
    }
}

/* midiOutMessage / midiInMessage reinterpret a non-handle as a device id; out of range ids -> BADDEVICEID. */
TEST(mh_midi_message_bad_handle_is_id) {
    REQUIRE(U(midiOutMessage)); REQUIRE(U(midiInMessage)); REQUIRE(U(midiOutGetVolume));
    const mh_counts *c = mh_devs();
    ULONG_PTR vals[] = { 0xFFFF, 0x8000, 0x12345678, (ULONG_PTR)c->mo + 3 };
    for (int i = 0; i < 4; i++) {
        if (vals[i] < c->mo) continue;
        EXPECT_EQ(U(midiOutMessage)((HMIDIOUT)vals[i], MODM_GETVOLUME, 0, 0), MMSYSERR_BADDEVICEID);
    }
    for (int i = 0; i < 4; i++) {
        if (vals[i] < c->mi) continue;
        EXPECT_EQ(U(midiInMessage)((HMIDIIN)vals[i], MIDM_GETDEVCAPS, 0, 0), MMSYSERR_BADDEVICEID);
    }
    if (c->mi == 0) EXPECT_EQ(U(midiInMessage)(NULL, MIDM_GETDEVCAPS, 0, 0), MMSYSERR_BADDEVICEID);
    /* midiOutGetVolume: NULL pointer first, then id-or-handle */
    for (int b = 0; b < MH_NBAD; b++) EXPECT_EQ(U(midiOutGetVolume)((HMIDIOUT)MH_BAD[b].v, NULL), MMSYSERR_INVALPARAM);
    DWORD v = 0x12345;
    EXPECT_EQ(U(midiOutGetVolume)((HMIDIOUT)(ULONG_PTR)0xFFFF, &v), MMSYSERR_BADDEVICEID);
    EXPECT_EQ(U(midiOutGetVolume)((HMIDIOUT)(ULONG_PTR)0x8000, &v), MMSYSERR_BADDEVICEID);
    EXPECT_EQ(v, 0x12345);
}

/* ---- device half ---- */

TEST(mh_midiout_cross_types) {
    REQ_MIDIOUT();
    mh_zoo z; mh_zoo_open(&z);
    t_note("zoo mmio=%d waveOut=%d waveIn=%d mixer=%d midiOut=%d", !!z.mmio, !!z.wo, !!z.wi, !!z.mx, !!z.mo);
    mh_zoo_cross(&z, MH_MIDIOUT_OPS, MH_NMIDIOUT_OPS, "midiOut");
    mh_zoo_close(&z);
}
TEST(mh_midiin_cross_types) {
    REQ_MIDIIN();
    mh_zoo z; mh_zoo_open(&z);
    mh_zoo_cross(&z, MH_MIDIIN_OPS, MH_NMIDIIN_OPS, "midiIn");
    if (z.mo) {
        /* a midiOut handle is a valid thru SOURCE; the NULL target is then rejected as a bad midiOut handle */
        EXPECT_EQ(U(midiConnect)((HMIDI)z.mo, NULL, NULL), MMSYSERR_INVALHANDLE);
        /* target of another type */
        if (z.mmio) EXPECT_EQ(U(midiConnect)((HMIDI)z.mo, (HMIDIOUT)z.mmio, NULL), MMSYSERR_INVALHANDLE);
        if (z.wo) EXPECT_EQ(U(midiConnect)((HMIDI)z.mo, (HMIDIOUT)z.wo, NULL), MMSYSERR_INVALHANDLE);
    }
    mh_zoo_close(&z);
}
TEST(mh_stream_cross_types) {
    REQ_STREAM();
    mh_zoo z; mh_zoo_open(&z);
    mh_zoo_cross(&z, MH_STREAM_OPS, MH_NSTREAM_OPS, "midiStream");
    mh_zoo_close(&z);
}

static HMIDISTRM open_stream(MMRESULT *pr) {
    HMIDISTRM s = NULL; UINT dev = 0;
    *pr = U(midiStreamOpen)(&s, &dev, 1, 0, 0, CALLBACK_NULL);
    return *pr ? NULL : s;
}

/* midiOut handle lifetime: open device 0 (GS synth), stored id, close, stale value. */
TEST(mh_midiout_closed_handle) {
    REQ_MIDIOUT(); REQUIRE(U(midiOutOpen));
    const mh_counts *c = mh_devs();
    HMIDIOUT h = (HMIDIOUT)(ULONG_PTR)0x777;
    if (!c->wo || !c->mo) {
        MMRESULT r = U(midiOutOpen)(&h, 0, 0, 0, CALLBACK_NULL);
        t_note("no waveOut device: midiOutOpen(0) -> %u", r);
        if (r == 0) U(midiOutClose)(h); else EXPECT_EQ(h, NULL);
        return;
    }
    MMRESULT r = U(midiOutOpen)(&h, 0, 0, 0, CALLBACK_NULL);
    EXPECT_EQ(r, 0);
    if (r) return;
    UINT id = 0xDEAD;
    EXPECT_EQ(U(midiOutGetID)(h, &id), 0);
    EXPECT_EQ(id, 0);
    EXPECT_EQ(U(midiOutShortMsg)(h, ALL_OFF), 0);
    EXPECT_EQ(U(midiOutReset)(h), 0);
    EXPECT_EQ(U(midiOutClose)(h), 0);
    mh_expect_bad(MH_MIDIOUT_OPS, MH_NMIDIOUT_OPS, (ULONG_PTR)h, "closed HMIDIOUT");
    /* the device can be opened again after the close (single-open synth released) */
    HMIDIOUT h2 = NULL;
    EXPECT_EQ(U(midiOutOpen)(&h2, 0, 0, 0, CALLBACK_NULL), 0);
    if (h2) EXPECT_EQ(U(midiOutClose)(h2), 0);
}

/* A midiStream handle is accepted by the shared midiOut entry points but not by midiOutClose / midiOutGetID. */
TEST(mh_stream_handle_in_midiout_calls) {
    REQ_STREAM(); REQ_MIDIOUT();
    const mh_counts *c = mh_devs();
    MMRESULT r;
    if (!c->wo || !c->mo) {
        HMIDISTRM s = open_stream(&r);
        t_note("no waveOut device: midiStreamOpen(0) -> %u", r);
        if (s) U(midiStreamClose)(s);
        return;
    }
    HMIDISTRM s = open_stream(&r);
    EXPECT_EQ(r, 0);
    if (!s) return;
    ULONG_PTR hs = (ULONG_PTR)s;
    EXPECT_EQ(mo_close(hs), MMSYSERR_INVALHANDLE);    /* type 3 only */
    EXPECT_EQ(mo_getid(hs), MMSYSERR_INVALHANDLE);    /* type 3 only */
    ULONG_PTR sr = mo_short(hs);
    CHECKF(sr != MMSYSERR_INVALHANDLE, "midiOutShortMsg(stream) -> %llu", (unsigned long long)sr);
    t_note("midiOutShortMsg(stream handle) -> %llu", (unsigned long long)sr);
    EXPECT_EQ(mo_long(hs), MMSYSERR_NOTSUPPORTED);     /* long messages are not sent through a stream handle */
    EXPECT_EQ(mo_prep_null(hs), MMSYSERR_INVALPARAM);  /* accepted handle, then header validation */
    EXPECT_EQ(mo_unprep_null(hs), MMSYSERR_INVALPARAM);
    MIDIHDR mh; mhdr_init(&mh, 0, 4);
    EXPECT_EQ(U(midiOutPrepareHeader)((HMIDIOUT)s, &mh, 44), MMSYSERR_INVALPARAM);  /* stream needs a full MIDIHDR */
    ULONG_PTR rr = mo_reset(hs);
    CHECKF(rr != MMSYSERR_INVALHANDLE, "midiOutReset(stream) -> %llu", (unsigned long long)rr);
    t_note("midiOutReset(stream handle) -> %llu", (unsigned long long)rr);
    /* the stream is still valid after these calls */
    MMTIME mt; memset(&mt, 0, sizeof mt); mt.wType = TIME_MS;
    EXPECT_EQ(U(midiStreamPosition)(s, &mt, sizeof mt), 0);
    EXPECT_EQ(U(midiStreamClose)(s), 0);
    /* and invalid afterwards for every family */
    mh_expect_bad(MH_STREAM_OPS, MH_NSTREAM_OPS, hs, "closed HMIDISTRM");
    EXPECT_EQ(mo_short(hs), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(mo_reset(hs), MMSYSERR_INVALHANDLE);
}

/* A live stream handle in every other family's entry points -> 5. */
TEST(mh_stream_handle_in_other_families) {
    REQ_STREAM();
    const mh_counts *c = mh_devs();
    if (!c->wo || !c->mo) { t_note("no waveOut device: stream handle not available"); return; }
    MMRESULT r; HMIDISTRM s = open_stream(&r);
    EXPECT_EQ(r, 0);
    if (!s) return;
    mh_expect_bad(MH_WAVEOUT_OPS, MH_NWAVEOUT_OPS, (ULONG_PTR)s, "HMIDISTRM");
    mh_expect_bad(MH_WAVEIN_OPS, MH_NWAVEIN_OPS, (ULONG_PTR)s, "HMIDISTRM");
    mh_expect_bad(MH_MIDIIN_OPS, MH_NMIDIIN_OPS, (ULONG_PTR)s, "HMIDISTRM");
    mh_expect_bad(MH_MMIO_OPS, MH_NMMIO_OPS, (ULONG_PTR)s, "HMIDISTRM");
    mh_expect_bad(MH_MIXER_OPS, MH_NMIXER_OPS, (ULONG_PTR)s, "HMIDISTRM");
    EXPECT_EQ(U(midiStreamClose)(s), 0);
}

/* Live midiOut handle into the stream family and the midiIn family. */
TEST(mh_midiout_handle_in_stream_calls) {
    REQ_STREAM(); REQ_MIDIOUT(); REQUIRE(U(midiOutOpen));
    const mh_counts *c = mh_devs();
    if (!c->wo || !c->mo) { t_note("no waveOut device"); return; }
    HMIDIOUT h = NULL;
    EXPECT_EQ(U(midiOutOpen)(&h, 0, 0, 0, CALLBACK_NULL), 0);
    if (!h) return;
    mh_expect_bad(MH_STREAM_OPS, MH_NSTREAM_OPS, (ULONG_PTR)h, "HMIDIOUT");
    EXPECT_EQ(U(midiInClose)((HMIDIIN)h), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(midiInReset)((HMIDIIN)h), MMSYSERR_INVALHANDLE);
    /* the GS synth is single-open: a stream on the same device is refused while h is open */
    MMRESULT r; HMIDISTRM s = open_stream(&r);
    EXPECT_EQ(r, MMSYSERR_ALLOCATED);
    if (s) U(midiStreamClose)(s);
    EXPECT_EQ(U(midiOutClose)(h), 0);
}

/* Double close for midiOut and midiStream. */
TEST(mh_midi_double_close) {
    REQ_STREAM(); REQ_MIDIOUT(); REQUIRE(U(midiOutOpen));
    const mh_counts *c = mh_devs();
    if (!c->wo || !c->mo) { t_note("no waveOut device"); return; }
    HMIDIOUT h = NULL;
    EXPECT_EQ(U(midiOutOpen)(&h, 0, 0, 0, CALLBACK_NULL), 0);
    if (h) {
        EXPECT_EQ(U(midiOutClose)(h), 0);
        EXPECT_EQ(U(midiOutClose)(h), MMSYSERR_INVALHANDLE);
    }
    MMRESULT r; HMIDISTRM s = open_stream(&r);
    EXPECT_EQ(r, 0);
    if (s) {
        EXPECT_EQ(U(midiStreamClose)(s), 0);
        EXPECT_EQ(U(midiStreamClose)(s), MMSYSERR_INVALHANDLE);
    }
}
