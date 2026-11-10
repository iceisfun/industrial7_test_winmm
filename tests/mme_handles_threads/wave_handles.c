#include "mh.h"
/* waveOut / waveIn handle validation: every entry point that takes an HWAVEOUT / HWAVEIN rejects NULL, small
 * integers, -1, freed handles and handles of other types with MMSYSERR_INVALHANDLE, after the parameter checks that
 * the decomp places before the handle check. VM_TIMEOUT: 240 */

static char g_wdata[64];
static void hdr_init(WAVEHDR *wh, DWORD flags) {
    memset(wh, 0, sizeof *wh); wh->lpData = g_wdata; wh->dwBufferLength = sizeof g_wdata; wh->dwFlags = flags;
}

/* ---- waveOut ops (args chosen so that a valid handle would not be reached for a side effect-free failure) -- */
static ULONG_PTR wo_close(ULONG_PTR h) { return U(waveOutClose)((HWAVEOUT)h); }
static ULONG_PTR wo_reset(ULONG_PTR h) { return U(waveOutReset)((HWAVEOUT)h); }
static ULONG_PTR wo_pause(ULONG_PTR h) { return U(waveOutPause)((HWAVEOUT)h); }
static ULONG_PTR wo_restart(ULONG_PTR h) { return U(waveOutRestart)((HWAVEOUT)h); }
static ULONG_PTR wo_breakloop(ULONG_PTR h) { return U(waveOutBreakLoop)((HWAVEOUT)h); }
static ULONG_PTR wo_getid(ULONG_PTR h) {
    UINT id = 0xA5A5A5A5; MMRESULT r = U(waveOutGetID)((HWAVEOUT)h, &id);
    if (r) CHECKF(id == 0xA5A5A5A5, "waveOutGetID failure wrote id 0x%x", id);
    return r;
}
static ULONG_PTR wo_getpos(ULONG_PTR h) {
    MMTIME mt; memset(&mt, 0, sizeof mt); mt.wType = TIME_BYTES; return U(waveOutGetPosition)((HWAVEOUT)h, &mt, sizeof mt);
}
static ULONG_PTR wo_getpitch(ULONG_PTR h) { DWORD d = 0; return U(waveOutGetPitch)((HWAVEOUT)h, &d); }
static ULONG_PTR wo_getrate(ULONG_PTR h) { DWORD d = 0; return U(waveOutGetPlaybackRate)((HWAVEOUT)h, &d); }
static ULONG_PTR wo_setpitch(ULONG_PTR h) { return U(waveOutSetPitch)((HWAVEOUT)h, 0x10000); }
static ULONG_PTR wo_setrate(ULONG_PTR h) { return U(waveOutSetPlaybackRate)((HWAVEOUT)h, 0x10000); }
static ULONG_PTR wo_prepare(ULONG_PTR h) {
    WAVEHDR wh; hdr_init(&wh, WHDR_BEGINLOOP | WHDR_DONE);
    MMRESULT r = U(waveOutPrepareHeader)((HWAVEOUT)h, &wh, sizeof wh);
    /* waveOut validates the handle before it masks dwFlags: a rejected header keeps its flags. */
    if (r == MMSYSERR_INVALHANDLE) CHECKF(wh.dwFlags == (WHDR_BEGINLOOP | WHDR_DONE), "flags 0x%lx", wh.dwFlags);
    return r;
}
static ULONG_PTR wo_unprepare(ULONG_PTR h) {
    WAVEHDR wh; hdr_init(&wh, WHDR_PREPARED);
    MMRESULT r = U(waveOutUnprepareHeader)((HWAVEOUT)h, &wh, sizeof wh);
    if (r == MMSYSERR_INVALHANDLE) CHECKF(wh.dwFlags == WHDR_PREPARED, "flags 0x%lx", wh.dwFlags);
    return r;
}
static ULONG_PTR wo_write(ULONG_PTR h) {
    WAVEHDR wh; hdr_init(&wh, WHDR_PREPARED | WHDR_DONE);
    MMRESULT r = U(waveOutWrite)((HWAVEOUT)h, &wh, sizeof wh);
    /* DONE is cleared only after the handle check. */
    if (r == MMSYSERR_INVALHANDLE) CHECKF(wh.dwFlags == (WHDR_PREPARED | WHDR_DONE), "flags 0x%lx", wh.dwFlags);
    return r;
}

const mh_op MH_WAVEOUT_OPS[] = {
    { "waveOutClose", wo_close, 5, 1u << MHT_WAVEOUT },
    { "waveOutReset", wo_reset, 5, 1u << MHT_WAVEOUT },
    { "waveOutPause", wo_pause, 5, 1u << MHT_WAVEOUT },
    { "waveOutRestart", wo_restart, 5, 1u << MHT_WAVEOUT },
    { "waveOutBreakLoop", wo_breakloop, 5, 1u << MHT_WAVEOUT },
    { "waveOutGetID", wo_getid, 5, 1u << MHT_WAVEOUT },
    { "waveOutGetPosition", wo_getpos, 5, 1u << MHT_WAVEOUT },
    { "waveOutGetPitch", wo_getpitch, 5, 1u << MHT_WAVEOUT },
    { "waveOutGetPlaybackRate", wo_getrate, 5, 1u << MHT_WAVEOUT },
    { "waveOutSetPitch", wo_setpitch, 5, 1u << MHT_WAVEOUT },
    { "waveOutSetPlaybackRate", wo_setrate, 5, 1u << MHT_WAVEOUT },
    { "waveOutPrepareHeader", wo_prepare, 5, 1u << MHT_WAVEOUT },
    { "waveOutUnprepareHeader", wo_unprepare, 5, 1u << MHT_WAVEOUT },
    { "waveOutWrite", wo_write, 5, 1u << MHT_WAVEOUT },
};
const int MH_NWAVEOUT_OPS = sizeof MH_WAVEOUT_OPS / sizeof MH_WAVEOUT_OPS[0];

/* ---- waveIn ops ---- */
static ULONG_PTR wi_close(ULONG_PTR h) { return U(waveInClose)((HWAVEIN)h); }
static ULONG_PTR wi_reset(ULONG_PTR h) { return U(waveInReset)((HWAVEIN)h); }
static ULONG_PTR wi_start(ULONG_PTR h) { return U(waveInStart)((HWAVEIN)h); }
static ULONG_PTR wi_stop(ULONG_PTR h) { return U(waveInStop)((HWAVEIN)h); }
static ULONG_PTR wi_getid(ULONG_PTR h) {
    UINT id = 0x5A5A5A5A; MMRESULT r = U(waveInGetID)((HWAVEIN)h, &id);
    if (r) CHECKF(id == 0x5A5A5A5A, "waveInGetID failure wrote id 0x%x", id);
    return r;
}
static ULONG_PTR wi_getpos(ULONG_PTR h) {
    MMTIME mt; memset(&mt, 0, sizeof mt); mt.wType = TIME_BYTES; return U(waveInGetPosition)((HWAVEIN)h, &mt, sizeof mt);
}
static ULONG_PTR wi_prepare(ULONG_PTR h) {
    WAVEHDR wh; hdr_init(&wh, WHDR_BEGINLOOP | WHDR_DONE);
    MMRESULT r = U(waveInPrepareHeader)((HWAVEIN)h, &wh, sizeof wh);
    /* waveIn zeroes dwFlags BEFORE the handle check (unlike waveOut). */
    if (r == MMSYSERR_INVALHANDLE) CHECKF(wh.dwFlags == 0, "flags 0x%lx", wh.dwFlags);
    return r;
}
static ULONG_PTR wi_unprepare(ULONG_PTR h) {
    WAVEHDR wh; hdr_init(&wh, WHDR_PREPARED); return U(waveInUnprepareHeader)((HWAVEIN)h, &wh, sizeof wh);
}
static ULONG_PTR wi_add(ULONG_PTR h) {
    WAVEHDR wh; hdr_init(&wh, WHDR_PREPARED); return U(waveInAddBuffer)((HWAVEIN)h, &wh, sizeof wh);
}

const mh_op MH_WAVEIN_OPS[] = {
    { "waveInClose", wi_close, 5, 1u << MHT_WAVEIN },
    { "waveInReset", wi_reset, 5, 1u << MHT_WAVEIN },
    { "waveInStart", wi_start, 5, 1u << MHT_WAVEIN },
    { "waveInStop", wi_stop, 5, 1u << MHT_WAVEIN },
    { "waveInGetID", wi_getid, 5, 1u << MHT_WAVEIN },
    { "waveInGetPosition", wi_getpos, 5, 1u << MHT_WAVEIN },
    { "waveInPrepareHeader", wi_prepare, 5, 1u << MHT_WAVEIN },
    { "waveInUnprepareHeader", wi_unprepare, 5, 1u << MHT_WAVEIN },
    { "waveInAddBuffer", wi_add, 5, 1u << MHT_WAVEIN },
};
const int MH_NWAVEIN_OPS = sizeof MH_WAVEIN_OPS / sizeof MH_WAVEIN_OPS[0];

#define REQ_WAVEOUT() do { REQUIRE(U(waveOutClose)); REQUIRE(U(waveOutReset)); REQUIRE(U(waveOutPause)); \
    REQUIRE(U(waveOutRestart)); REQUIRE(U(waveOutBreakLoop)); REQUIRE(U(waveOutGetID)); REQUIRE(U(waveOutGetPosition)); \
    REQUIRE(U(waveOutGetPitch)); REQUIRE(U(waveOutGetPlaybackRate)); REQUIRE(U(waveOutSetPitch)); \
    REQUIRE(U(waveOutSetPlaybackRate)); REQUIRE(U(waveOutPrepareHeader)); REQUIRE(U(waveOutUnprepareHeader)); \
    REQUIRE(U(waveOutWrite)); REQUIRE(U(mmioOpenA)); REQUIRE(U(mmioClose)); } while (0)
#define REQ_WAVEIN() do { REQUIRE(U(waveInClose)); REQUIRE(U(waveInReset)); REQUIRE(U(waveInStart)); \
    REQUIRE(U(waveInStop)); REQUIRE(U(waveInGetID)); REQUIRE(U(waveInGetPosition)); REQUIRE(U(waveInPrepareHeader)); \
    REQUIRE(U(waveInUnprepareHeader)); REQUIRE(U(waveInAddBuffer)); REQUIRE(U(mmioOpenA)); REQUIRE(U(mmioClose)); } while (0)

/* Device gating record: counts and caps names (never timed: the first call may wait for AudioSrv). */
TEST(mh_wave_device_gating) {
    REQUIRE(U(waveOutGetNumDevs)); REQUIRE(U(waveInGetNumDevs)); REQUIRE(U(waveOutGetDevCapsA)); REQUIRE(U(waveInGetDevCapsA));
    const mh_counts *c = mh_devs();
    t_note("devices waveOut=%u waveIn=%u midiOut=%u midiIn=%u mixer=%u", c->wo, c->wi, c->mo, c->mi, c->mx);
    for (UINT i = 0; i < c->wo; i++) {
        WAVEOUTCAPSA oc; memset(&oc, 0, sizeof oc);
        EXPECT_EQ(U(waveOutGetDevCapsA)(i, &oc, sizeof oc), 0);
        t_note("waveOut %u \"%s\"", i, oc.szPname);
    }
    for (UINT i = 0; i < c->wi; i++) {
        WAVEINCAPSA ic; memset(&ic, 0, sizeof ic);
        EXPECT_EQ(U(waveInGetDevCapsA)(i, &ic, sizeof ic), 0);
        t_note("waveIn %u \"%s\"", i, ic.szPname);
    }
    /* Repeated reads return the cached count. */
    EXPECT_EQ(U(waveOutGetNumDevs)(), c->wo);
    EXPECT_EQ(U(waveInGetNumDevs)(), c->wi);
}


/* A live HMMIO (type 5) is never a wave handle. */
TEST(mh_waveout_live_hmmio) {
    REQ_WAVEOUT();
    HMMIO m = mh_mmio_open();
    if (!m) { t_skip("mmio memory open failed"); return; }
    mh_expect_bad(MH_WAVEOUT_OPS, MH_NWAVEOUT_OPS, (ULONG_PTR)m, "live HMMIO");
    /* the mmio handle is still intact afterwards (waveOutClose did not free it) */
    MMIOINFO mi; memset(&mi, 0, sizeof mi);
    EXPECT_EQ(U(mmioGetInfo)(m, &mi, 0), 0);
    EXPECT_EQ(mi.fccIOProc, FOURCC_MEM);
    EXPECT_EQ(U(mmioClose)(m, 0), 0);
}
TEST(mh_wavein_live_hmmio) {
    REQ_WAVEIN();
    HMMIO m = mh_mmio_open();
    if (!m) { t_skip("mmio memory open failed"); return; }
    mh_expect_bad(MH_WAVEIN_OPS, MH_NWAVEIN_OPS, (ULONG_PTR)m, "live HMMIO");
    EXPECT_EQ(U(mmioClose)(m, 0), 0);
}

/* Parameter checks that come BEFORE the handle check (so a bad handle does not change the result). */
TEST(mh_waveout_param_before_handle) {
    REQ_WAVEOUT();
    for (int b = 0; b < MH_NBAD; b++) {
        HWAVEOUT h = (HWAVEOUT)MH_BAD[b].v; WAVEHDR wh;
        EXPECT_EQ(U(waveOutGetID)(h, NULL), MMSYSERR_INVALPARAM);
        EXPECT_EQ(U(waveOutGetPosition)(h, NULL, sizeof(MMTIME)), MMSYSERR_INVALPARAM);
        EXPECT_EQ(U(waveOutGetPitch)(h, NULL), MMSYSERR_INVALPARAM);
        EXPECT_EQ(U(waveOutGetPlaybackRate)(h, NULL), MMSYSERR_INVALPARAM);
        /* header front check: NULL, cb < 48, undefined flag bits */
        EXPECT_EQ(U(waveOutPrepareHeader)(h, NULL, sizeof wh), MMSYSERR_INVALPARAM);
        hdr_init(&wh, 0);
        EXPECT_EQ(U(waveOutPrepareHeader)(h, &wh, sizeof wh - 1), MMSYSERR_INVALPARAM);
        EXPECT_EQ(U(waveOutUnprepareHeader)(h, &wh, 0), MMSYSERR_INVALPARAM);
        EXPECT_EQ(U(waveOutWrite)(h, &wh, 8), MMSYSERR_INVALPARAM);
        hdr_init(&wh, 0x20);
        EXPECT_EQ(U(waveOutPrepareHeader)(h, &wh, sizeof wh), MMSYSERR_INVALPARAM);
        EXPECT_EQ(U(waveOutUnprepareHeader)(h, &wh, sizeof wh), MMSYSERR_INVALPARAM);
        EXPECT_EQ(U(waveOutWrite)(h, &wh, sizeof wh), MMSYSERR_INVALPARAM);
        hdr_init(&wh, 0x80000000u);
        EXPECT_EQ(U(waveOutWrite)(h, &wh, sizeof wh), MMSYSERR_INVALPARAM);
        /* header state decided before the handle */
        hdr_init(&wh, WHDR_PREPARED);
        EXPECT_EQ(U(waveOutPrepareHeader)(h, &wh, sizeof wh), MMSYSERR_NOERROR);   /* already prepared: no handle check */
        EXPECT_EQ(wh.dwFlags, WHDR_PREPARED);
        hdr_init(&wh, 0);
        EXPECT_EQ(U(waveOutUnprepareHeader)(h, &wh, sizeof wh), MMSYSERR_NOERROR); /* not prepared: no handle check */
        EXPECT_EQ(wh.dwFlags, 0);
        hdr_init(&wh, WHDR_DONE);
        EXPECT_EQ(U(waveOutUnprepareHeader)(h, &wh, sizeof wh), MMSYSERR_NOERROR);
        EXPECT_EQ(wh.dwFlags, WHDR_DONE);
        hdr_init(&wh, WHDR_PREPARED | WHDR_INQUEUE);
        EXPECT_EQ(U(waveOutUnprepareHeader)(h, &wh, sizeof wh), WAVERR_STILLPLAYING);
        EXPECT_EQ(U(waveOutWrite)(h, &wh, sizeof wh), WAVERR_STILLPLAYING);
        hdr_init(&wh, 0);
        EXPECT_EQ(U(waveOutWrite)(h, &wh, sizeof wh), WAVERR_UNPREPARED);
        hdr_init(&wh, WHDR_INQUEUE);
        EXPECT_EQ(U(waveOutWrite)(h, &wh, sizeof wh), WAVERR_UNPREPARED);           /* PREPARED tested before INQUEUE */
        /* the internal 0x1000 / 0x2000 bits pass the front mask */
        hdr_init(&wh, WHDR_PREPARED | 0x1000);
        EXPECT_EQ(U(waveOutPrepareHeader)(h, &wh, sizeof wh), MMSYSERR_NOERROR);
        hdr_init(&wh, 0x2000);
        EXPECT_EQ(U(waveOutUnprepareHeader)(h, &wh, sizeof wh), MMSYSERR_NOERROR);
    }
}

TEST(mh_wavein_param_before_handle) {
    REQ_WAVEIN();
    for (int b = 0; b < MH_NBAD; b++) {
        HWAVEIN h = (HWAVEIN)MH_BAD[b].v; WAVEHDR wh;
        EXPECT_EQ(U(waveInGetID)(h, NULL), MMSYSERR_INVALPARAM);
        EXPECT_EQ(U(waveInGetPosition)(h, NULL, sizeof(MMTIME)), MMSYSERR_INVALPARAM);
        EXPECT_EQ(U(waveInPrepareHeader)(h, NULL, sizeof wh), MMSYSERR_INVALPARAM);
        hdr_init(&wh, 0);
        EXPECT_EQ(U(waveInPrepareHeader)(h, &wh, sizeof wh - 1), MMSYSERR_INVALPARAM);
        EXPECT_EQ(U(waveInAddBuffer)(h, &wh, 0), MMSYSERR_INVALPARAM);
        hdr_init(&wh, 0x40);
        EXPECT_EQ(U(waveInPrepareHeader)(h, &wh, sizeof wh), MMSYSERR_INVALPARAM);
        EXPECT_EQ(wh.dwFlags, 0x40);                                                  /* rejected before the reset */
        EXPECT_EQ(U(waveInUnprepareHeader)(h, &wh, sizeof wh), MMSYSERR_INVALPARAM);
        EXPECT_EQ(U(waveInAddBuffer)(h, &wh, sizeof wh), MMSYSERR_INVALPARAM);
        hdr_init(&wh, WHDR_PREPARED | WHDR_DONE);
        EXPECT_EQ(U(waveInPrepareHeader)(h, &wh, sizeof wh), MMSYSERR_NOERROR);
        EXPECT_EQ(wh.dwFlags, WHDR_PREPARED | WHDR_DONE);
        hdr_init(&wh, 0);
        EXPECT_EQ(U(waveInUnprepareHeader)(h, &wh, sizeof wh), MMSYSERR_NOERROR);
        hdr_init(&wh, WHDR_PREPARED | WHDR_INQUEUE);
        EXPECT_EQ(U(waveInUnprepareHeader)(h, &wh, sizeof wh), WAVERR_STILLPLAYING);
        EXPECT_EQ(U(waveInAddBuffer)(h, &wh, sizeof wh), WAVERR_STILLPLAYING);
        hdr_init(&wh, 0);
        EXPECT_EQ(U(waveInAddBuffer)(h, &wh, sizeof wh), WAVERR_UNPREPARED);
        hdr_init(&wh, WHDR_INQUEUE);
        EXPECT_EQ(U(waveInAddBuffer)(h, &wh, sizeof wh), WAVERR_UNPREPARED);
    }
}

/* waveOutMessage / waveInMessage take a handle OR a device id: an invalid handle value is reinterpreted as an id.
 * Values >= the device count (except 0xFFFFFFFF, the mapper) give BADDEVICEID. */
TEST(mh_wave_message_bad_handle_is_id) {
    REQUIRE(U(waveOutMessage)); REQUIRE(U(waveInMessage));
    const mh_counts *c = mh_devs();
    ULONG_PTR vals[] = { 0xFFFF, 0x8000, (ULONG_PTR)c->wo + 5, 0x12345678 };
    for (int i = 0; i < 4; i++) {
        if ((DWORD)vals[i] < c->wo) continue;
        EXPECT_EQ(U(waveOutMessage)((HWAVEOUT)vals[i], WODM_GETPOS, 0, 0), MMSYSERR_BADDEVICEID);
    }
    ULONG_PTR ivals[] = { 0xFFFF, 0x8000, (ULONG_PTR)c->wi + 5, 0x12345678 };
    for (int i = 0; i < 4; i++) {
        if ((DWORD)ivals[i] < c->wi) continue;
        EXPECT_EQ(U(waveInMessage)((HWAVEIN)ivals[i], WIDM_GETPOS, 0, 0), MMSYSERR_BADDEVICEID);
    }
    if (c->wo == 0) EXPECT_EQ(U(waveOutMessage)(NULL, WODM_GETPOS, 0, 0), MMSYSERR_BADDEVICEID);
    if (c->wi == 0) EXPECT_EQ(U(waveInMessage)(NULL, WIDM_GETPOS, 0, 0), MMSYSERR_BADDEVICEID);
    /* a freed handle's low 32 bits are almost always out of range */
    ULONG_PTR dead = mh_closed_mmio();
    if (dead && (DWORD)dead >= c->wo && (DWORD)dead != 0xFFFFFFFF)
        EXPECT_EQ(U(waveOutMessage)((HWAVEOUT)dead, WODM_GETPOS, 0, 0), MMSYSERR_BADDEVICEID);
}

/* ---- device half ---- */

TEST(mh_waveout_cross_types) {
    REQ_WAVEOUT();
    mh_zoo z; mh_zoo_open(&z);
    t_note("zoo mmio=%d waveOut=%d waveIn=%d mixer=%d midiOut=%d", !!z.mmio, !!z.wo, !!z.wi, !!z.mx, !!z.mo);
    mh_zoo_cross(&z, MH_WAVEOUT_OPS, MH_NWAVEOUT_OPS, "waveOut");
    mh_zoo_close(&z);
}
TEST(mh_wavein_cross_types) {
    REQ_WAVEIN();
    mh_zoo z; mh_zoo_open(&z);
    mh_zoo_cross(&z, MH_WAVEIN_OPS, MH_NWAVEIN_OPS, "waveIn");
    mh_zoo_close(&z);
}

/* After close every op on the stale value is INVALHANDLE; GetID reports the stored id while open. */
TEST(mh_waveout_closed_handle) {
    REQ_WAVEOUT(); REQUIRE(U(waveOutOpen));
    const mh_counts *c = mh_devs();
    if (!c->wo) {
        HWAVEOUT h = (HWAVEOUT)(ULONG_PTR)0x1234;
        EXPECT_EQ(mh_waveout_open(&h, NULL), MMSYSERR_BADDEVICEID);
        EXPECT_EQ(h, NULL);   /* *phwo is cleared before the device check */
        return;
    }
    WAVEFORMATEX wf; mh_pcm(&wf);
    UINT ids[] = { WAVE_MAPPER, 0, c->wo - 1 };
    for (int k = 0; k < 3; k++) {
        HWAVEOUT h = NULL;
        MMRESULT r = U(waveOutOpen)(&h, ids[k], &wf, 0, 0, CALLBACK_NULL);
        EXPECT_EQ(r, 0);
        if (r) continue;
        CHECK((ULONG_PTR)h > 0xFFFF);
        UINT id = 0xDEAD;
        EXPECT_EQ(U(waveOutGetID)(h, &id), 0);
        EXPECT_EQ(id, ids[k]);
        EXPECT_EQ(U(waveOutClose)(h), 0);
        mh_expect_bad(MH_WAVEOUT_OPS, MH_NWAVEOUT_OPS, (ULONG_PTR)h, "closed HWAVEOUT");
    }
}

TEST(mh_wavein_closed_handle) {
    REQ_WAVEIN(); REQUIRE(U(waveInOpen));
    const mh_counts *c = mh_devs();
    if (!c->wi) {
        HWAVEIN h = (HWAVEIN)(ULONG_PTR)0x1234;
        EXPECT_EQ(mh_wavein_open(&h), MMSYSERR_BADDEVICEID);
        EXPECT_EQ(h, NULL);
        return;
    }
    HWAVEIN h = NULL;
    MMRESULT r = mh_wavein_open(&h);
    EXPECT_EQ(r, 0);
    if (r) return;
    UINT id = 0xDEAD;
    EXPECT_EQ(U(waveInGetID)(h, &id), 0);
    EXPECT_EQ(id, WAVE_MAPPER);
    EXPECT_EQ(U(waveInClose)(h), 0);
    mh_expect_bad(MH_WAVEIN_OPS, MH_NWAVEIN_OPS, (ULONG_PTR)h, "closed HWAVEIN");
}

/* A live HWAVEOUT and HWAVEIN are distinct types for each other. */
TEST(mh_wave_inout_not_interchangeable) {
    REQ_WAVEOUT(); REQ_WAVEIN();
    const mh_counts *c = mh_devs();
    if (!c->wo || !c->wi) { t_note("needs waveOut and waveIn (have %u/%u)", c->wo, c->wi); return; }
    HWAVEOUT ho = NULL; HWAVEIN hi = NULL;
    EXPECT_EQ(mh_waveout_open(&ho, NULL), 0);
    EXPECT_EQ(mh_wavein_open(&hi), 0);
    if (ho && hi) {
        EXPECT_EQ(U(waveOutClose)((HWAVEOUT)hi), MMSYSERR_INVALHANDLE);
        EXPECT_EQ(U(waveInClose)((HWAVEIN)ho), MMSYSERR_INVALHANDLE);
        UINT id;
        EXPECT_EQ(U(waveOutGetID)((HWAVEOUT)hi, &id), MMSYSERR_INVALHANDLE);
        EXPECT_EQ(U(waveInGetID)((HWAVEIN)ho, &id), MMSYSERR_INVALHANDLE);
        EXPECT_EQ(U(waveOutReset)((HWAVEOUT)hi), MMSYSERR_INVALHANDLE);
        EXPECT_EQ(U(waveInReset)((HWAVEIN)ho), MMSYSERR_INVALHANDLE);
        /* both still work */
        EXPECT_EQ(U(waveOutReset)(ho), 0);
        EXPECT_EQ(U(waveInReset)(hi), 0);
    }
    if (hi) EXPECT_EQ(U(waveInClose)(hi), 0);
    if (ho) EXPECT_EQ(U(waveOutClose)(ho), 0);
}

/* Double close of each wave handle kind; a second open after close gives a working handle again. */
TEST(mh_waveout_double_close) {
    REQ_WAVEOUT(); REQUIRE(U(waveOutOpen));
    if (!mh_devs()->wo) { t_note("no waveOut device"); return; }
    for (int i = 0; i < 3; i++) {
        HWAVEOUT h = NULL;
        EXPECT_EQ(mh_waveout_open(&h, NULL), 0);
        if (!h) return;
        EXPECT_EQ(U(waveOutClose)(h), 0);
        EXPECT_EQ(U(waveOutClose)(h), MMSYSERR_INVALHANDLE);
        EXPECT_EQ(U(waveOutClose)(h), MMSYSERR_INVALHANDLE);
    }
}
TEST(mh_wavein_double_close) {
    REQ_WAVEIN();
    if (!mh_devs()->wi) { t_note("no waveIn device"); return; }
    HWAVEIN h = NULL;
    EXPECT_EQ(mh_wavein_open(&h), 0);
    if (!h) return;
    EXPECT_EQ(U(waveInClose)(h), 0);
    EXPECT_EQ(U(waveInClose)(h), MMSYSERR_INVALHANDLE);
}

/* Two handles on the same device are independent: closing one leaves the other valid. */
TEST(mh_waveout_two_handles_independent) {
    REQ_WAVEOUT();
    if (!mh_devs()->wo) { t_note("no waveOut device"); return; }
    HWAVEOUT a = NULL, b = NULL;
    EXPECT_EQ(mh_waveout_open(&a, NULL), 0);
    EXPECT_EQ(mh_waveout_open(&b, NULL), 0);
    if (a && b) {
        CHECK(a != b);
        EXPECT_EQ(U(waveOutClose)(a), 0);
        UINT id = 0;
        EXPECT_EQ(U(waveOutGetID)(a, &id), MMSYSERR_INVALHANDLE);
        EXPECT_EQ(U(waveOutGetID)(b, &id), 0);
        EXPECT_EQ(id, WAVE_MAPPER);
        EXPECT_EQ(mh_play_silence(b, 50, 3000), 0);
        EXPECT_EQ(U(waveOutClose)(b), 0);
    } else {
        if (a) U(waveOutClose)(a);
        if (b) U(waveOutClose)(b);
    }
}

/* waveOutClose with a queued buffer returns STILLPLAYING and the handle stays valid; after Reset close works. */
TEST(mh_waveout_close_while_queued_keeps_handle) {
    REQ_WAVEOUT();
    if (!mh_devs()->wo) { t_note("no waveOut device"); return; }
    HWAVEOUT h = NULL;
    EXPECT_EQ(mh_waveout_open(&h, NULL), 0);
    if (!h) return;
    static char silence[44100 * 4];   /* 1 s */
    WAVEHDR wh; memset(&wh, 0, sizeof wh); wh.lpData = silence; wh.dwBufferLength = sizeof silence;
    EXPECT_EQ(U(waveOutPrepareHeader)(h, &wh, sizeof wh), 0);
    EXPECT_EQ(U(waveOutWrite)(h, &wh, sizeof wh), 0);
    MMRESULT r = U(waveOutClose)(h);
    EXPECT_EQ(r, WAVERR_STILLPLAYING);
    if (r == 0) return;
    UINT id;
    EXPECT_EQ(U(waveOutGetID)(h, &id), 0);   /* still on the handle list */
    EXPECT_EQ(U(waveOutReset)(h), 0);
    CHECKF(wh.dwFlags & WHDR_DONE, "flags 0x%lx after reset", wh.dwFlags);
    EXPECT_EQ(wh.dwFlags & WHDR_INQUEUE, 0);
    EXPECT_EQ(U(waveOutUnprepareHeader)(h, &wh, sizeof wh), 0);
    EXPECT_EQ(U(waveOutClose)(h), 0);
    EXPECT_EQ(U(waveOutGetID)(h, &id), MMSYSERR_INVALHANDLE);
}

/* Header ops with a header that belongs to another live handle: the handle is what is validated. */
TEST(mh_waveout_header_ladder_live_handle) {
    REQ_WAVEOUT();
    if (!mh_devs()->wo) { t_note("no waveOut device"); return; }
    HWAVEOUT h = NULL;
    EXPECT_EQ(mh_waveout_open(&h, NULL), 0);
    if (!h) return;
    WAVEHDR wh; hdr_init(&wh, 0);
    EXPECT_EQ(U(waveOutWrite)(h, &wh, sizeof wh), WAVERR_UNPREPARED);
    EXPECT_EQ(U(waveOutPrepareHeader)(h, &wh, sizeof wh), 0);
    EXPECT_EQ(wh.dwFlags, WHDR_PREPARED);
    EXPECT_EQ(U(waveOutPrepareHeader)(h, &wh, sizeof wh), 0);   /* second prepare: no-op */
    EXPECT_EQ(U(waveOutUnprepareHeader)(h, &wh, sizeof wh), 0);
    EXPECT_EQ(wh.dwFlags, 0);
    EXPECT_EQ(U(waveOutUnprepareHeader)(h, &wh, sizeof wh), 0);
    EXPECT_EQ(U(waveOutClose)(h), 0);
}
