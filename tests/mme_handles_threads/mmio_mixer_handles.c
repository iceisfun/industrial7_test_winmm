#include "mh.h"
/* mmio (type 5) and mixer (type 8) handle validation, the mixer object-type resolution of handles, and the
 * winmmbase WOW handle-value table (winmmbaseHandle32FromHandle16 / Get / SetWOWHandle on real handles only).
 * VM_TIMEOUT: 240 */

/* ---- mmio ---- */
static ULONG_PTR io_close(ULONG_PTR h) { return U(mmioClose)((HMMIO)h, 0); }
static ULONG_PTR io_getinfo(ULONG_PTR h) {
    MMIOINFO mi; memset(&mi, 0xCC, sizeof mi);
    ULONG_PTR r = U(mmioGetInfo)((HMMIO)h, &mi, 0);
    if (r) CHECKF(mi.dwFlags == 0xCCCCCCCC, "mmioGetInfo failure wrote dwFlags 0x%lx", mi.dwFlags);
    return r;
}
static ULONG_PTR io_getinfo_null(ULONG_PTR h) { return U(mmioGetInfo)((HMMIO)h, NULL, 0); }
static ULONG_PTR io_setinfo_null(ULONG_PTR h) { return U(mmioSetInfo)((HMMIO)h, NULL, 0); }
static ULONG_PTR io_read(ULONG_PTR h) { char b[4]; return (ULONG_PTR)(LONG_PTR)U(mmioRead)((HMMIO)h, b, 4); }
static ULONG_PTR io_read_null(ULONG_PTR h) { return (ULONG_PTR)(LONG_PTR)U(mmioRead)((HMMIO)h, NULL, 0); }
static ULONG_PTR io_write(ULONG_PTR h) { char b[4] = { 1, 2, 3, 4 }; return (ULONG_PTR)(LONG_PTR)U(mmioWrite)((HMMIO)h, b, 4); }
static ULONG_PTR io_seek(ULONG_PTR h) { return (ULONG_PTR)(LONG_PTR)U(mmioSeek)((HMMIO)h, 0, SEEK_SET); }
static ULONG_PTR io_seek_end(ULONG_PTR h) { return (ULONG_PTR)(LONG_PTR)U(mmioSeek)((HMMIO)h, 0, SEEK_END); }
static ULONG_PTR io_flush(ULONG_PTR h) { return U(mmioFlush)((HMMIO)h, 0); }
static ULONG_PTR io_advance(ULONG_PTR h) { return U(mmioAdvance)((HMMIO)h, NULL, MMIO_READ); }
static ULONG_PTR io_setbuffer(ULONG_PTR h) { return U(mmioSetBuffer)((HMMIO)h, NULL, 0, 0); }
static ULONG_PTR io_sendmsg(ULONG_PTR h) { return (ULONG_PTR)U(mmioSendMessage)((HMMIO)h, MMIOM_USER + 7, 1, 2); }
static ULONG_PTR io_createchunk(ULONG_PTR h) { MMCKINFO ck; memset(&ck, 0, sizeof ck); ck.ckid = mmioFOURCC('t','e','s','t');
    return U(mmioCreateChunk)((HMMIO)h, &ck, 0); }
static ULONG_PTR io_ascend(ULONG_PTR h) { MMCKINFO ck; memset(&ck, 0, sizeof ck); ck.dwDataOffset = 8; ck.cksize = 4;
    return U(mmioAscend)((HMMIO)h, &ck, 0); }
#define M5 (1u << MHT_MMIO)
const mh_op MH_MMIO_OPS[] = {
    { "mmioClose", io_close, 5, M5 },
    { "mmioGetInfo", io_getinfo, 5, M5 },
    { "mmioGetInfo(NULL)", io_getinfo_null, 5, M5 },          /* handle before pointer */
    { "mmioSetInfo(NULL)", io_setinfo_null, 5, M5 },          /* handle before pointer */
    { "mmioRead", io_read, (ULONG_PTR)-1, M5 },
    { "mmioRead(NULL, 0)", io_read_null, (ULONG_PTR)-1, M5 },
    { "mmioWrite", io_write, (ULONG_PTR)-1, M5 },
    { "mmioSeek(SET)", io_seek, (ULONG_PTR)-1, M5 },
    { "mmioSeek(END)", io_seek_end, (ULONG_PTR)-1, M5 },
    { "mmioFlush", io_flush, 5, M5 },
    { "mmioAdvance", io_advance, 5, M5 },
    { "mmioSetBuffer", io_setbuffer, 5, M5 },
    { "mmioSendMessage", io_sendmsg, 0, M5 },                 /* an invalid handle yields 0, not an error code */
    { "mmioCreateChunk", io_createchunk, MMIOERR_CANNOTSEEK, M5 },   /* fails in its internal mmioSeek */
    { "mmioAscend", io_ascend, MMIOERR_CANNOTSEEK, M5 },
};
const int MH_NMMIO_OPS = sizeof MH_MMIO_OPS / sizeof MH_MMIO_OPS[0];

/* ---- mixer: entry points whose result for a non-handle does not depend on the mixer count ---- */
static ULONG_PTR mx_close(ULONG_PTR h) { return U(mixerClose)((HMIXER)h); }
static ULONG_PTR mx_getid_obj(ULONG_PTR h, DWORD f) {
    UINT id = 0x1234; MMRESULT r = U(mixerGetID)((HMIXEROBJ)h, &id, f);
    if (r) CHECKF(id == 0xFFFFFFFF, "mixerGetID(0x%lx) failure left id 0x%x (want -1 written first)", f, id);
    return r;
}
static ULONG_PTR mx_getid_hwo(ULONG_PTR h) { return mx_getid_obj(h, MIXER_OBJECTF_HWAVEOUT); }
static ULONG_PTR mx_getid_hwi(ULONG_PTR h) { return mx_getid_obj(h, MIXER_OBJECTF_HWAVEIN); }
static ULONG_PTR mx_getid_hmo(ULONG_PTR h) { return mx_getid_obj(h, MIXER_OBJECTF_HMIDIOUT); }
static ULONG_PTR mx_lineinfo_hwo(ULONG_PTR h) {
    MIXERLINEW ml; memset(&ml, 0, sizeof ml); ml.cbStruct = sizeof ml;
    return U(mixerGetLineInfoW)((HMIXEROBJ)h, &ml, MIXER_OBJECTF_HWAVEOUT | MIXER_GETLINEINFOF_DESTINATION);
}
static ULONG_PTR mx_linectl_hwo(ULONG_PTR h) {
    MIXERCONTROLW c; MIXERLINECONTROLSW lc; memset(&lc, 0, sizeof lc); lc.cbStruct = sizeof lc; lc.dwLineID = 0;
    lc.cControls = 1; lc.cbmxctrl = sizeof c; lc.pamxctrl = &c;
    return U(mixerGetLineControlsW)((HMIXEROBJ)h, &lc, MIXER_OBJECTF_HWAVEOUT | MIXER_GETLINECONTROLSF_ALL);
}
static ULONG_PTR mx_details_hwo(ULONG_PTR h) {
    MIXERCONTROLDETAILS_UNSIGNED v; MIXERCONTROLDETAILS d; memset(&d, 0, sizeof d); d.cbStruct = sizeof d;
    d.dwControlID = 1; d.cChannels = 1; d.cbDetails = sizeof v; d.paDetails = &v;
    return U(mixerGetControlDetailsW)((HMIXEROBJ)h, &d, MIXER_OBJECTF_HWAVEOUT | MIXER_GETCONTROLDETAILSF_VALUE);
}
#define M8 (1u << MHT_MIXER)
#define M1 (1u << MHT_WAVEOUT)
const mh_op MH_MIXER_OPS[] = {
    { "mixerClose", mx_close, 5, M8 },
    { "mixerGetID(HWAVEOUT)", mx_getid_hwo, 5, M1 },
    { "mixerGetID(HWAVEIN)", mx_getid_hwi, 5, 1u << MHT_WAVEIN },
    { "mixerGetID(HMIDIOUT)", mx_getid_hmo, 5, 1u << MHT_MIDIOUT },
    { "mixerGetLineInfoW(HWAVEOUT)", mx_lineinfo_hwo, 5, M1 },
    { "mixerGetLineControlsW(HWAVEOUT)", mx_linectl_hwo, 5, M1 },
    { "mixerGetControlDetailsW(HWAVEOUT)", mx_details_hwo, 5, M1 },
};
const int MH_NMIXER_OPS = sizeof MH_MIXER_OPS / sizeof MH_MIXER_OPS[0];

#define REQ_MMIO() do { REQUIRE(U(mmioOpenA)); REQUIRE(U(mmioClose)); REQUIRE(U(mmioGetInfo)); REQUIRE(U(mmioSetInfo)); \
    REQUIRE(U(mmioRead)); REQUIRE(U(mmioWrite)); REQUIRE(U(mmioSeek)); REQUIRE(U(mmioFlush)); REQUIRE(U(mmioAdvance)); \
    REQUIRE(U(mmioSetBuffer)); REQUIRE(U(mmioSendMessage)); REQUIRE(U(mmioCreateChunk)); REQUIRE(U(mmioAscend)); } while (0)
#define REQ_MIXER() do { REQUIRE(U(mixerClose)); REQUIRE(U(mixerGetID)); REQUIRE(U(mixerGetLineInfoW)); \
    REQUIRE(U(mixerGetLineControlsW)); REQUIRE(U(mixerGetControlDetailsW)); REQUIRE(U(mmioOpenA)); } while (0)

TEST(mh_mmio_bad_values) { REQ_MMIO(); mh_expect_all_bad(MH_MMIO_OPS, MH_NMMIO_OPS); }
TEST(mh_mixer_bad_values) { REQ_MIXER(); mh_expect_all_bad(MH_MIXER_OPS, MH_NMIXER_OPS); }

TEST(mh_mmio_freed_value) {
    REQ_MMIO();
    ULONG_PTR dead = mh_closed_mmio();
    if (!dead) { t_skip("mmio memory open failed"); return; }
    mh_expect_bad(MH_MMIO_OPS, MH_NMMIO_OPS, dead, "freed HMMIO");
}
TEST(mh_mixer_freed_value) {
    REQ_MIXER();
    ULONG_PTR dead = mh_closed_mmio();
    if (!dead) { t_skip("mmio memory open failed"); return; }
    mh_expect_bad(MH_MIXER_OPS, MH_NMIXER_OPS, dead, "freed HMMIO");
}
TEST(mh_mixer_live_hmmio) {
    REQ_MIXER();
    HMMIO m = mh_mmio_open();
    if (!m) { t_skip("mmio memory open failed"); return; }
    mh_expect_bad(MH_MIXER_OPS, MH_NMIXER_OPS, (ULONG_PTR)m, "live HMMIO");
    EXPECT_EQ(U(mmioClose)(m, 0), 0);
}

/* Two live memory files: each one's handle is valid only for itself; closing one leaves the other intact. */
TEST(mh_mmio_two_files_independent) {
    REQ_MMIO();
    HMMIO a = mh_mmio_open(), b = mh_mmio_open();
    CHECK(a != NULL); CHECK(b != NULL);
    if (!a || !b) { if (a) U(mmioClose)(a, 0); if (b) U(mmioClose)(b, 0); return; }
    CHECK(a != b);
    CHECK((ULONG_PTR)a > 0xFFFF);
    EXPECT_EQ(U(mmioWrite)(a, "AAAA", 4), 4);
    EXPECT_EQ(U(mmioWrite)(b, "BB", 2), 2);
    EXPECT_EQ(U(mmioClose)(a, 0), 0);
    mh_expect_bad(MH_MMIO_OPS, MH_NMMIO_OPS, (ULONG_PTR)a, "closed HMMIO");
    EXPECT_EQ(U(mmioSeek)(b, 0, SEEK_CUR), 2);
    EXPECT_EQ(U(mmioSeek)(b, 0, SEEK_SET), 0);
    char buf[4] = { 0 };
    EXPECT_EQ(U(mmioRead)(b, buf, 2), 2);
    CHECK(buf[0] == 'B' && buf[1] == 'B');
    EXPECT_EQ(U(mmioClose)(b, 0), 0);
    EXPECT_EQ(U(mmioClose)(b, 0), MMSYSERR_INVALHANDLE);
}

/* Double / triple close, then the stale value in every mmio entry point. */
TEST(mh_mmio_double_close) {
    REQ_MMIO();
    for (int i = 0; i < 8; i++) {
        HMMIO h = mh_mmio_open();
        CHECK(h != NULL);
        if (!h) return;
        EXPECT_EQ(U(mmioClose)(h, 0), 0);
        EXPECT_EQ(U(mmioClose)(h, 0), MMSYSERR_INVALHANDLE);
        EXPECT_EQ(U(mmioClose)(h, MMIO_FHOPEN), MMSYSERR_INVALHANDLE);
    }
}

/* Parameter checks that precede the handle in the chunk helpers; NULL handle special cases. */
TEST(mh_mmio_chunk_helpers_order) {
    REQUIRE(U(mmioCreateChunk)); REQUIRE(U(mmioAscend)); REQUIRE(U(mmioDescend));
    MMCKINFO ck;
    for (int b = 0; b < MH_NBAD; b++) {
        HMMIO h = (HMMIO)MH_BAD[b].v;
        memset(&ck, 0, sizeof ck);
        EXPECT_EQ(U(mmioCreateChunk)(h, &ck, 0x1), MMSYSERR_INVALFLAG);
        EXPECT_EQ(U(mmioCreateChunk)(h, &ck, 0x80), MMSYSERR_INVALFLAG);
        EXPECT_EQ(U(mmioCreateChunk)(h, NULL, 0), MMSYSERR_INVALPARAM);
        EXPECT_EQ(U(mmioCreateChunk)(h, &ck, MMIO_CREATERIFF), MMIOERR_CANNOTSEEK);
        EXPECT_EQ(U(mmioCreateChunk)(h, &ck, MMIO_CREATELIST), MMIOERR_CANNOTSEEK);
        EXPECT_EQ(U(mmioAscend)(h, &ck, 1), MMSYSERR_INVALFLAG);
        EXPECT_EQ(U(mmioAscend)(h, NULL, 0), MMSYSERR_INVALPARAM);
        /* clean chunk with an offset range that does not wrap: seek through the bad handle fails */
        ck.dwDataOffset = 0; ck.cksize = 0;
        EXPECT_EQ(U(mmioAscend)(h, &ck, 0), MMIOERR_CANNOTSEEK);
        /* negative data offset -> chunk error before any seek */
        ck.dwDataOffset = 0x80000000u; ck.cksize = 2;
        EXPECT_EQ(U(mmioAscend)(h, &ck, 0), MMIOERR_INVALIDFILE);
        /* dirty chunk: seek fails through a non-NULL bad handle */
        if (h) { ck.dwFlags = MMIO_DIRTY; ck.dwDataOffset = 8; ck.cksize = 0;
                 EXPECT_EQ(U(mmioAscend)(h, &ck, 0), MMIOERR_CANNOTSEEK); }
        /* mmioDescend: flags, then ckinfo (only NULL is used as the handle: the decomp reads the handle's
         * fields before validating it) */
        EXPECT_EQ(U(mmioDescend)(h, &ck, NULL, 0x8), MMSYSERR_INVALFLAG);
        EXPECT_EQ(U(mmioDescend)(h, NULL, NULL, 0), MMSYSERR_INVALPARAM);
    }
    /* NULL handle: the dirty-chunk ascend and any descend report MMIOERR_OUTOFMEMORY (258) */
    memset(&ck, 0, sizeof ck); ck.dwFlags = MMIO_DIRTY; ck.dwDataOffset = 8;
    EXPECT_EQ(U(mmioAscend)(NULL, &ck, 0), MMIOERR_OUTOFMEMORY);
    memset(&ck, 0x55, sizeof ck);
    EXPECT_EQ(U(mmioDescend)(NULL, &ck, NULL, 0), MMIOERR_OUTOFMEMORY);
    EXPECT_EQ(ck.dwFlags, 0);   /* cleared before the handle test */
    memset(&ck, 0, sizeof ck);
    EXPECT_EQ(U(mmioDescend)(NULL, &ck, NULL, MMIO_FINDRIFF), MMIOERR_OUTOFMEMORY);
}

/* mmioSetInfo with a valid handle and a NULL pIOProc is rejected (11) after the handle check. */
TEST(mh_mmio_setinfo_order) {
    REQ_MMIO();
    MMIOINFO mi; memset(&mi, 0, sizeof mi);
    for (int b = 0; b < MH_NBAD; b++) EXPECT_EQ(U(mmioSetInfo)((HMMIO)MH_BAD[b].v, &mi, 0), MMSYSERR_INVALHANDLE);
    HMMIO h = mh_mmio_open();
    if (!h) return;
    EXPECT_EQ(U(mmioSetInfo)(h, NULL, 0), MMSYSERR_INVALPARAM);
    EXPECT_EQ(U(mmioSetInfo)(h, &mi, 0), MMSYSERR_INVALPARAM);
    EXPECT_EQ(U(mmioGetInfo)(h, NULL, 0), MMSYSERR_INVALPARAM);
    EXPECT_EQ(U(mmioGetInfo)(h, &mi, 0), 0);
    CHECK(mi.pIOProc != NULL);
    EXPECT_EQ(mi.fccIOProc, FOURCC_MEM);
    EXPECT_EQ(U(mmioSetInfo)(h, &mi, 0), 0);
    EXPECT_EQ(U(mmioClose)(h, 0), 0);
}

/* ---- mixer object-type resolution ---- */

/* MIXER_OBJECTF_HMIXER / MIXER_OBJECTF_MIXER: a value below the mixer count is an id; anything else must be a live
 * HMIXER (5 for HMIXER, 2 for MIXER). The out id is written as -1 first. */
TEST(mh_mixer_getid_hmixer_values) {
    REQUIRE(U(mixerGetID)); REQUIRE(U(mixerGetNumDevs));
    const mh_counts *c = mh_devs();
    for (int b = 0; b < MH_NBAD; b++) {
        ULONG_PTR v = MH_BAD[b].v; UINT id = 0x4242;
        MMRESULT r = U(mixerGetID)((HMIXEROBJ)v, &id, MIXER_OBJECTF_HMIXER);
        if (v < c->mx) { EXPECT_EQ(r, 0); EXPECT_EQ(id, (UINT)v); }
        else { CHECKF(r == MMSYSERR_INVALHANDLE, "HMIXER %s -> %u", MH_BAD[b].name, r); EXPECT_EQ(id, 0xFFFFFFFF); }
        id = 0x4242;
        r = U(mixerGetID)((HMIXEROBJ)v, &id, MIXER_OBJECTF_MIXER);
        if (v < c->mx) { EXPECT_EQ(r, 0); EXPECT_EQ(id, (UINT)v); }
        else { CHECKF(r == MMSYSERR_BADDEVICEID, "MIXER %s -> %u", MH_BAD[b].name, r); EXPECT_EQ(id, 0xFFFFFFFF); }
    }
    UINT id;
    EXPECT_EQ(U(mixerGetID)(NULL, NULL, MIXER_OBJECTF_HMIXER), MMSYSERR_INVALPARAM);
    EXPECT_EQ(U(mixerGetID)(NULL, &id, MIXER_OBJECTF_HMIXER | 1), MMSYSERR_INVALFLAG);
    EXPECT_EQ(U(mixerGetID)(NULL, NULL, 0x08000000), MMSYSERR_INVALFLAG);   /* flags before the pointer */
    ULONG_PTR dead = mh_closed_mmio();
    if (dead) {
        id = 7;
        EXPECT_EQ(U(mixerGetID)((HMIXEROBJ)dead, &id, MIXER_OBJECTF_HMIXER), MMSYSERR_INVALHANDLE);
        EXPECT_EQ(id, 0xFFFFFFFF);
    }
}

/* Handle object types that the resolver does not know (midiIn, HMIDIIN, undefined nibbles) -> INVALPARAM. */
TEST(mh_mixer_getid_unhandled_objtypes) {
    REQUIRE(U(mixerGetID));
    static const DWORD objs[] = { MIXER_OBJECTF_MIDIIN, MIXER_OBJECTF_HMIDIIN, 0x60000000, 0x70000000,
                                  0xD0000000, 0xE0000000, 0xF0000000 };
    for (int i = 0; i < (int)(sizeof objs / sizeof objs[0]); i++) {
        for (int b = 0; b < MH_NBAD; b++) {
            UINT id = 0x99;
            MMRESULT r = U(mixerGetID)((HMIXEROBJ)MH_BAD[b].v, &id, objs[i]);
            CHECKF(r == MMSYSERR_INVALPARAM, "objtype 0x%lx value %s -> %u", objs[i], MH_BAD[b].name, r);
            EXPECT_EQ(id, 0xFFFFFFFF);
        }
    }
}

/* The same HMIXER rule in the line / control entry points (after their own parameter checks). */
TEST(mh_mixer_hmixer_flag_in_line_calls) {
    REQ_MIXER();
    const mh_counts *c = mh_devs();
    for (int b = 0; b < MH_NBAD; b++) {
        ULONG_PTR v = MH_BAD[b].v;
        if (v < c->mx) continue;
        MIXERLINEW ml; memset(&ml, 0, sizeof ml); ml.cbStruct = sizeof ml;
        EXPECT_EQ(U(mixerGetLineInfoW)((HMIXEROBJ)v, &ml, MIXER_OBJECTF_HMIXER | MIXER_GETLINEINFOF_DESTINATION),
                  MMSYSERR_INVALHANDLE);
        EXPECT_EQ(U(mixerGetLineInfoW)((HMIXEROBJ)v, &ml, MIXER_OBJECTF_MIXER | MIXER_GETLINEINFOF_DESTINATION),
                  MMSYSERR_BADDEVICEID);
        /* parameter errors win over the handle */
        EXPECT_EQ(U(mixerGetLineInfoW)((HMIXEROBJ)v, NULL, MIXER_OBJECTF_HMIXER), MMSYSERR_INVALPARAM);
        EXPECT_EQ(U(mixerGetLineInfoW)((HMIXEROBJ)v, &ml, MIXER_OBJECTF_HMIXER | 0x100), MMSYSERR_INVALFLAG);
        ml.cbStruct = sizeof ml - 1;
        EXPECT_EQ(U(mixerGetLineInfoW)((HMIXEROBJ)v, &ml, MIXER_OBJECTF_HMIXER), MMSYSERR_INVALPARAM);
        MIXERCONTROLW ctl; MIXERLINECONTROLSW lc; memset(&lc, 0, sizeof lc); lc.cbStruct = sizeof lc;
        lc.cControls = 1; lc.cbmxctrl = sizeof ctl; lc.pamxctrl = &ctl;
        EXPECT_EQ(U(mixerGetLineControlsW)((HMIXEROBJ)v, &lc, MIXER_OBJECTF_HMIXER | MIXER_GETLINECONTROLSF_ALL),
                  MMSYSERR_INVALHANDLE);
        lc.pamxctrl = NULL;
        EXPECT_EQ(U(mixerGetLineControlsW)((HMIXEROBJ)v, &lc, MIXER_OBJECTF_HMIXER | MIXER_GETLINECONTROLSF_ALL),
                  MMSYSERR_INVALPARAM);
        MIXERCONTROLDETAILS_UNSIGNED du; MIXERCONTROLDETAILS d; memset(&d, 0, sizeof d); d.cbStruct = sizeof d;
        d.dwControlID = 1; d.cChannels = 1; d.cbDetails = sizeof du; d.paDetails = &du;
        EXPECT_EQ(U(mixerGetControlDetailsW)((HMIXEROBJ)v, &d, MIXER_OBJECTF_HMIXER | MIXER_GETCONTROLDETAILSF_VALUE),
                  MMSYSERR_INVALHANDLE);
        d.paDetails = NULL;
        EXPECT_EQ(U(mixerGetControlDetailsW)((HMIXEROBJ)v, &d, MIXER_OBJECTF_HMIXER | MIXER_GETCONTROLDETAILSF_VALUE),
                  MMSYSERR_INVALPARAM);
    }
}

/* mixerMessage: a non-handle is a mixer id (out of range -> 2); a live HMIXER refuses messages below 0x4000 (11). */
TEST(mh_mixer_message_handle_or_id) {
    REQUIRE(U(mixerMessage)); REQUIRE(U(mixerOpen)); REQUIRE(U(mixerClose));
    const mh_counts *c = mh_devs();
    ULONG_PTR vals[] = { 0xFFFF, 0x8000, (ULONG_PTR)c->mx, (ULONG_PTR)c->mx + 1 };
    for (int i = 0; i < 4; i++)
        EXPECT_EQ(U(mixerMessage)((HMIXER)vals[i], MXDM_GETDEVCAPS, 0, 0), MMSYSERR_BADDEVICEID);
    if (!c->mx) { EXPECT_EQ(U(mixerMessage)(NULL, MXDM_GETDEVCAPS, 0, 0), MMSYSERR_BADDEVICEID); return; }
    HMIXER hm = NULL;
    EXPECT_EQ(U(mixerOpen)(&hm, 0, 0, 0, 0), 0);
    if (!hm) return;
    EXPECT_EQ(U(mixerMessage)(hm, MXDM_GETDEVCAPS, 0, 0), MMSYSERR_INVALPARAM);
    EXPECT_EQ(U(mixerMessage)(hm, 0x100, 0, 0), MMSYSERR_INVALPARAM);
    EXPECT_EQ(U(mixerMessage)(hm, 0x3FFF, 0, 0), MMSYSERR_INVALPARAM);
    EXPECT_EQ(U(mixerClose)(hm), 0);
    EXPECT_EQ(U(mixerClose)(hm), MMSYSERR_INVALHANDLE);
    /* closed handle value: treated as an id again (out of range) */
    if ((ULONG_PTR)hm >= c->mx) EXPECT_EQ(U(mixerMessage)(hm, MXDM_GETDEVCAPS, 0, 0), MMSYSERR_BADDEVICEID);
}

/* Live mixer handles: id resolution, cross-type, close twice; two opens of one mixer are separate handles. */
TEST(mh_mixer_live_handles) {
    REQ_MIXER(); REQUIRE(U(mixerOpen)); REQUIRE(U(mixerGetDevCapsW));
    const mh_counts *c = mh_devs();
    if (!c->mx) {
        HMIXER h = (HMIXER)(ULONG_PTR)0x55;
        EXPECT_EQ(U(mixerOpen)(&h, 0, 0, 0, 0), MMSYSERR_BADDEVICEID);
        t_note("no mixer: handle after failed open %p", h);
        return;
    }
    for (UINT m = 0; m < c->mx; m++) {
        HMIXER a = NULL, b = NULL;
        EXPECT_EQ(U(mixerOpen)(&a, m, 0, 0, 0), 0);
        EXPECT_EQ(U(mixerOpen)(&b, m, 0, 0, 0), 0);
        if (!a || !b) { if (a) U(mixerClose)(a); if (b) U(mixerClose)(b); continue; }
        CHECK(a != b);
        UINT id = 0x77;
        EXPECT_EQ(U(mixerGetID)((HMIXEROBJ)a, &id, MIXER_OBJECTF_HMIXER), 0);
        EXPECT_EQ(id, m);
        id = 0x77;
        EXPECT_EQ(U(mixerGetID)((HMIXEROBJ)a, &id, MIXER_OBJECTF_MIXER), 0);   /* a live handle also passes as MIXER */
        EXPECT_EQ(id, m);
        MIXERCAPSW caps; memset(&caps, 0, sizeof caps);
        EXPECT_EQ(U(mixerGetDevCapsW)((UINT_PTR)a, &caps, sizeof caps), 0);    /* handle accepted in place of an id */
        CHECK(caps.szPname[0] != 0);
        t_note("mixer %u \"%ls\" via handle", m, caps.szPname);
        /* other families reject it */
        mh_expect_bad(MH_WAVEOUT_OPS, MH_NWAVEOUT_OPS, (ULONG_PTR)a, "HMIXER");
        mh_expect_bad(MH_MIDIOUT_OPS, MH_NMIDIOUT_OPS, (ULONG_PTR)a, "HMIXER");
        mh_expect_bad(MH_MMIO_OPS, MH_NMMIO_OPS, (ULONG_PTR)a, "HMIXER");
        mh_expect_bad(MH_STREAM_OPS, MH_NSTREAM_OPS, (ULONG_PTR)a, "HMIXER");
        mh_expect_bad(MH_MIDIIN_OPS, MH_NMIDIIN_OPS, (ULONG_PTR)a, "HMIXER");
        /* closing one leaves the other usable */
        EXPECT_EQ(U(mixerClose)(a), 0);
        EXPECT_EQ(U(mixerClose)(a), MMSYSERR_INVALHANDLE);
        id = 0x77;
        EXPECT_EQ(U(mixerGetID)((HMIXEROBJ)a, &id, MIXER_OBJECTF_HMIXER), MMSYSERR_INVALHANDLE);
        id = 0x77;
        EXPECT_EQ(U(mixerGetID)((HMIXEROBJ)b, &id, MIXER_OBJECTF_HMIXER), 0);
        EXPECT_EQ(id, m);
        MIXERLINEW ml; memset(&ml, 0, sizeof ml); ml.cbStruct = sizeof ml;
        EXPECT_EQ(U(mixerGetLineInfoW)((HMIXEROBJ)b, &ml, MIXER_OBJECTF_HMIXER | MIXER_GETLINEINFOF_DESTINATION), 0);
        EXPECT_EQ(U(mixerGetLineInfoW)((HMIXEROBJ)a, &ml, MIXER_OBJECTF_HMIXER | MIXER_GETLINEINFOF_DESTINATION),
                  MMSYSERR_INVALHANDLE);
        EXPECT_EQ(U(mixerClose)(b), 0);
    }
}

TEST(mh_mixer_cross_types) {
    REQ_MIXER();
    mh_zoo z; mh_zoo_open(&z);
    mh_zoo_cross(&z, MH_MIXER_OPS, MH_NMIXER_OPS, "mixer");
    if (z.mo) EXPECT_EQ(U(mixerClose)((HMIXER)z.mo), MMSYSERR_INVALHANDLE);
    if (z.wo) {
        /* waveOut handle resolves through MIXER_OBJECTF_HWAVEOUT, but not through HMIXER */
        UINT id = 0x31;
        MMRESULT r = U(mixerGetID)((HMIXEROBJ)z.wo, &id, MIXER_OBJECTF_HWAVEOUT);
        t_note("mixerGetID(mapper HWAVEOUT, HWAVEOUT) -> %u id %u", r, id);
        CHECK(r == 0 || r == MMSYSERR_NODRIVER);
        id = 0x31;
        EXPECT_EQ(U(mixerGetID)((HMIXEROBJ)z.wo, &id, MIXER_OBJECTF_HMIXER), MMSYSERR_INVALHANDLE);
        EXPECT_EQ(id, 0xFFFFFFFF);
    }
    if (z.mx) {
        UINT id = 0x31;
        EXPECT_EQ(U(mixerGetID)((HMIXEROBJ)z.mx, &id, MIXER_OBJECTF_HWAVEOUT), MMSYSERR_INVALHANDLE);
        EXPECT_EQ(U(mixerGetID)((HMIXEROBJ)z.mx, &id, MIXER_OBJECTF_HMIDIOUT), MMSYSERR_INVALHANDLE);
    }
    mh_zoo_close(&z);
}

TEST(mh_mmio_cross_types) {
    REQ_MMIO();
    mh_zoo z; mh_zoo_open(&z);
    mh_zoo_cross(&z, MH_MMIO_OPS, MH_NMMIO_OPS, "mmio");
    /* the foreign handles still work afterwards (mmioClose did not touch them) */
    if (z.wo) { UINT id; EXPECT_EQ(U(waveOutGetID)(z.wo, &id), 0); }
    if (z.mx) { UINT id; EXPECT_EQ(U(mixerGetID)((HMIXEROBJ)z.mx, &id, MIXER_OBJECTF_HMIXER), 0); }
    mh_zoo_close(&z);
}

/* ---- WOW handle-value table (winmmbase-only exports) ---- */

TEST(mh_wow_handle32_unknown_value) {
    winmmbaseHandle32FromHandle16_pfn h32 = WB(winmmbaseHandle32FromHandle16);
    REQUIRE(h32);
    EXPECT_EQ(h32(0xBEEF), 0);
    EXPECT_EQ(h32(0xFFFF), 0);
    EXPECT_EQ(h32(0x0001), 0);
    /* not exported by winmm itself */
    CHECK(t_proc_in("winmm.dll", "winmmbaseHandle32FromHandle16") == NULL);
    CHECK(t_proc_in("winmm.dll", "winmmbaseGetWOWHandle") == NULL);
}

/* A fresh handle carries WOW value 0; Set/Get round-trip; the lookup returns the low 32 bits of the handle. */
TEST(mh_wow_set_get_lookup_mmio) {
    winmmbaseHandle32FromHandle16_pfn h32 = WB(winmmbaseHandle32FromHandle16);
    winmmbaseGetWOWHandle_pfn get = WB(winmmbaseGetWOWHandle);
    winmmbaseSetWOWHandle_pfn set = WB(winmmbaseSetWOWHandle);
    REQUIRE(h32); REQUIRE(get); REQUIRE(set); REQUIRE(U(mmioClose));
    HMMIO a = mh_mmio_open();
    if (!a) { t_skip("mmio memory open failed"); return; }
    EXPECT_EQ(get((HANDLE)a), 0);
    /* the newest handle is the list head, so the 0 lookup finds it first */
    EXPECT_EQ(h32(0), (DWORD)(ULONG_PTR)a);
    set((HANDLE)a, 0xBEEF);
    EXPECT_EQ(get((HANDLE)a), 0xBEEF);
    EXPECT_EQ(h32(0xBEEF), (DWORD)(ULONG_PTR)a);
    EXPECT_EQ(h32(0xBEEE), 0);
    HMMIO b = mh_mmio_open();
    if (b) {
        EXPECT_EQ(get((HANDLE)b), 0);
        EXPECT_EQ(h32(0), (DWORD)(ULONG_PTR)b);
        EXPECT_EQ(h32(0xBEEF), (DWORD)(ULONG_PTR)a);
        /* duplicate value: the newest handle wins */
        set((HANDLE)b, 0xBEEF);
        EXPECT_EQ(h32(0xBEEF), (DWORD)(ULONG_PTR)b);
        set((HANDLE)b, 0x1234);
        EXPECT_EQ(h32(0x1234), (DWORD)(ULONG_PTR)b);
        EXPECT_EQ(h32(0xBEEF), (DWORD)(ULONG_PTR)a);
        set((HANDLE)b, 0);
        EXPECT_EQ(U(mmioClose)(b, 0), 0);
        EXPECT_EQ(h32(0x1234), 0);
    }
    /* still valid as an mmio handle after the WOW update */
    MMIOINFO mi; EXPECT_EQ(U(mmioGetInfo)(a, &mi, 0), 0);
    /* closing removes the entry from the lookup */
    EXPECT_EQ(U(mmioClose)(a, 0), 0);
    EXPECT_EQ(h32(0xBEEF), 0);
}

TEST(mh_wow_set_reset_to_zero) {
    winmmbaseHandle32FromHandle16_pfn h32 = WB(winmmbaseHandle32FromHandle16);
    winmmbaseGetWOWHandle_pfn get = WB(winmmbaseGetWOWHandle);
    winmmbaseSetWOWHandle_pfn set = WB(winmmbaseSetWOWHandle);
    REQUIRE(h32); REQUIRE(get); REQUIRE(set);
    HMMIO a = mh_mmio_open();
    if (!a) { t_skip("mmio memory open failed"); return; }
    static const WORD vals[] = { 1, 0x7FFF, 0x8000, 0xFFFE, 0xFFFF, 0xBEEF };
    for (int i = 0; i < 6; i++) {
        set((HANDLE)a, vals[i]);
        EXPECT_EQ(get((HANDLE)a), vals[i]);
        EXPECT_EQ(h32(vals[i]), (DWORD)(ULONG_PTR)a);
    }
    set((HANDLE)a, 0);
    EXPECT_EQ(get((HANDLE)a), 0);
    EXPECT_EQ(h32(0xBEEF), 0);
    EXPECT_EQ(U(mmioClose)(a, 0), 0);
}

/* The WOW slot exists on every handle type (it is in the common header). */
TEST(mh_wow_on_device_handles) {
    winmmbaseHandle32FromHandle16_pfn h32 = WB(winmmbaseHandle32FromHandle16);
    winmmbaseGetWOWHandle_pfn get = WB(winmmbaseGetWOWHandle);
    winmmbaseSetWOWHandle_pfn set = WB(winmmbaseSetWOWHandle);
    REQUIRE(h32); REQUIRE(get); REQUIRE(set);
    mh_zoo z; mh_zoo_open(&z);
    HANDLE hs[] = { (HANDLE)z.mmio, (HANDLE)z.wo, (HANDLE)z.wi, (HANDLE)z.mx, (HANDLE)z.mo };
    WORD base = 0xA100;
    for (int i = 0; i < 5; i++) {
        if (!hs[i]) continue;
        EXPECT_EQ(get(hs[i]), 0);
        set(hs[i], (WORD)(base + i));
    }
    for (int i = 0; i < 5; i++) {
        if (!hs[i]) continue;
        EXPECT_EQ(get(hs[i]), base + i);
        EXPECT_EQ(h32((WORD)(base + i)), (DWORD)(ULONG_PTR)hs[i]);
        set(hs[i], 0);
    }
    mh_zoo_close(&z);
    for (int i = 0; i < 5; i++) EXPECT_EQ(h32((WORD)(base + i)), 0);
}
