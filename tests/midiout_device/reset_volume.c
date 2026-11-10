/* midiOutReset, volume, patch caching and midiOutMessage on live handles.
 *
 * winmmbase!midiOutReset (0x86f0) [D]: type 3 -> midiMessage(MODM_RESET = 9); type 9 -> broadcast to the
 * stream's drivers; a result of 6 (NODRIVER) on a handle whose 'gone' bit is set is turned into 0.
 * The driver turns every note off and returns queued long buffers DONE.
 * Volume: only ever read, and written back with the value just read (no audible or persistent change).
 */
#include "mod.h"

#define PH(h, m) U(midiOutPrepareHeader)((h), (m), sizeof(MIDIHDR))
#define UH(h, m) U(midiOutUnprepareHeader)((h), (m), sizeof(MIDIHDR))
#define LM(h, m) U(midiOutLongMsg)((h), (m), sizeof(MIDIHDR))
#define REQ_LONG() do { REQUIRE(U(midiOutLongMsg)); REQUIRE(U(midiOutPrepareHeader)); REQUIRE(U(midiOutUnprepareHeader)); } while (0)

static const UINT g_ids[] = { 0, MIDI_MAPPER };
#define FOR_IDS(h) for (int ii_ = 0; ii_ < NELEM(g_ids); ii_++) \
    for (HMIDIOUT h = mod_open_null(g_ids[ii_]), *once_ = (HMIDIOUT *)1; once_ && h; once_ = NULL, mod_close(h))

/* ================================================================ reset */

TEST(midiout_device_reset_idle) {
    MOD_NEED_LIVE();
    FOR_IDS(h) {
        for (int k = 0; k < 3; k++) CHECKF(U(midiOutReset)(h) == MMSYSERR_NOERROR, "id 0x%x round %d", g_ids[ii_], k);
    }
}

/* reset with notes sounding (quiet, on several channels), then the handle keeps working */
TEST(midiout_device_reset_sounding_notes) {
    MOD_NEED_LIVE();
    FOR_IDS(h) {
        for (int ch = 0; ch < 4; ch++) U(midiOutShortMsg)(h, MOD_MSG(0x90 | ch, 60 + ch * 4, 12));
        U(midiOutShortMsg)(h, MOD_MSG(0xB0, 64, 127));    /* sustain pedal down */
        Sleep(40);
        EXPECT_EQ(U(midiOutReset)(h), MMSYSERR_NOERROR);
        EXPECT_EQ(U(midiOutShortMsg)(h, MOD_MSG(0xB0, 64, 0)), MMSYSERR_NOERROR);
        EXPECT_EQ(mod_note(h, 0, 72, 10, 30), MMSYSERR_NOERROR);
    }
}

/* reset right after LongMsg: every header comes back DONE (not INQUEUE) and unprepares */
TEST(midiout_device_reset_returns_headers_done) {
    MOD_NEED_LIVE(); REQ_LONG();
    for (int i = 0; i < NELEM(g_ids); i++) {
        mod_rec_reset();
        HMIDIOUT h = NULL;
        if (mod_open(&h, g_ids[i], (DWORD_PTR)mod_cb, 0, CALLBACK_FUNCTION) != MMSYSERR_NOERROR) { CHECK(0); continue; }
        MIDIHDR m[4];
        for (int k = 0; k < 4; k++) { mod_hdr(&m[k], mod_gs_reset, sizeof(mod_gs_reset)); EXPECT_EQ(PH(h, &m[k]), MMSYSERR_NOERROR); }
        for (int k = 0; k < 4; k++) EXPECT_EQ(LM(h, &m[k]), MMSYSERR_NOERROR);
        EXPECT_EQ(U(midiOutReset)(h), MMSYSERR_NOERROR);
        for (int k = 0; k < 4; k++) {
            CHECKF(mod_wait_done(&m[k], 2000), "id 0x%x hdr %d flags 0x%lx", g_ids[i], k, m[k].dwFlags);
            EXPECT_EQ(m[k].dwFlags & MHDR_INQUEUE, 0);
            CHECKF(UH(h, &m[k]) == MMSYSERR_NOERROR, "hdr %d", k);
        }
        /* each header was returned exactly once (by completion or by the reset) */
        mod_wait_msg(MOM_DONE, 4, 1000);
        Sleep(20);
        EXPECT_EQ(mod_rec_count_msg(MOM_DONE), 4);
        mod_close(h);
    }
}

/* a large buffer reset while (possibly) still queued */
TEST(midiout_device_reset_large_buffer) {
    MOD_NEED_LIVE(); REQ_LONG();
    DWORD n = 16500;
    BYTE *buf = (BYTE *)HeapAlloc(GetProcessHeap(), 0, n);
    if (!buf) { t_skip("no memory"); return; }
    for (DWORD i = 0; i < n; i++) buf[i] = mod_gs_reset[i % sizeof(mod_gs_reset)];
    FOR_IDS(h) {
        MIDIHDR m; mod_hdr(&m, buf, n);
        EXPECT_EQ(PH(h, &m), MMSYSERR_NOERROR);
        EXPECT_EQ(LM(h, &m), MMSYSERR_NOERROR);
        DWORD f = m.dwFlags;
        EXPECT_EQ(U(midiOutReset)(h), MMSYSERR_NOERROR);
        CHECK(mod_wait_done(&m, 2000));
        t_note("id 0x%x flags before reset 0x%lx after 0x%lx", g_ids[ii_], f, m.dwFlags);
        EXPECT_EQ(UH(h, &m), MMSYSERR_NOERROR);
    }
    HeapFree(GetProcessHeap(), 0, buf);
}

/* reset from another thread than the one that opened */
static DWORD WINAPI reset_thread(LPVOID p) { return U(midiOutReset)((HMIDIOUT)p); }
TEST(midiout_device_reset_other_thread) {
    MOD_NEED_LIVE();
    FOR_IDS(h) {
        HANDLE th = CreateThread(NULL, 0, reset_thread, h, 0, NULL);
        CHECK(WaitForSingleObject(th, 5000) == WAIT_OBJECT_0);
        DWORD code = 0xFFFF;
        GetExitCodeThread(th, &code);
        CloseHandle(th);
        EXPECT_EQ(code, MMSYSERR_NOERROR);
    }
}

/* a reset after a GS reset sysex and controller changes: the handle still answers 0 for everything */
TEST(midiout_device_reset_then_traffic) {
    MOD_NEED_LIVE(); REQ_LONG();
    FOR_IDS(h) {
        EXPECT_EQ(mod_send_long(h, mod_gs_reset, sizeof(mod_gs_reset)), MMSYSERR_NOERROR);
        EXPECT_EQ(U(midiOutShortMsg)(h, MOD_MSG(0xB0, 7, 80)), MMSYSERR_NOERROR);
        EXPECT_EQ(U(midiOutReset)(h), MMSYSERR_NOERROR);
        EXPECT_EQ(U(midiOutShortMsg)(h, MOD_MSG(0xB0, 7, 100)), MMSYSERR_NOERROR);
        EXPECT_EQ(mod_send_long(h, mod_gm_on, sizeof(mod_gm_on)), MMSYSERR_NOERROR);
        EXPECT_EQ(U(midiOutReset)(h), MMSYSERR_NOERROR);
    }
}

/* ================================================================ volume */

TEST(midiout_device_volume_caps_flag) {
    MOD_NEED_LIVE(); REQUIRE(U(midiOutGetDevCapsW));
    MIDIOUTCAPSW c; memset(&c, 0, sizeof(c));
    EXPECT_EQ(U(midiOutGetDevCapsW)(0, &c, sizeof(c)), MMSYSERR_NOERROR);
    CHECK(c.dwSupport & MIDICAPS_VOLUME);
    t_note("synth dwSupport 0x%lx (LRVOLUME %d)", c.dwSupport, !!(c.dwSupport & MIDICAPS_LRVOLUME));
    memset(&c, 0, sizeof(c));
    EXPECT_EQ(U(midiOutGetDevCapsW)(MIDI_MAPPER, &c, sizeof(c)), MMSYSERR_NOERROR);
    CHECK(c.dwSupport & MIDICAPS_VOLUME);
    CHECK(c.dwSupport & MIDICAPS_STREAM);
}

TEST(midiout_device_volume_synth_handle) {
    MOD_NEED_LIVE(); REQUIRE(U(midiOutGetVolume)); REQUIRE(U(midiOutSetVolume));
    HMIDIOUT h = mod_open_null(0);
    if (!h) return;
    DWORD v = 0xA5A5A5A5;
    MMRESULT r = U(midiOutGetVolume)(h, &v);
    t_note("synth handle volume r=%u v=0x%08lx", r, v);
    EXPECT_EQ(r, MMSYSERR_NOERROR);
    if (r == MMSYSERR_NOERROR) {
        EXPECT_EQ(U(midiOutSetVolume)(h, v), MMSYSERR_NOERROR);
        DWORD v2 = ~v;
        EXPECT_EQ(U(midiOutGetVolume)(h, &v2), MMSYSERR_NOERROR);
        EXPECT_EQ(v2, v);
        /* by id while the handle is open: the same instance */
        v2 = ~v;
        EXPECT_EQ(U(midiOutGetVolume)((HMIDIOUT)0, &v2), MMSYSERR_NOERROR);
        EXPECT_EQ(v2, v);
    }
    EXPECT_EQ(U(midiOutGetVolume)(h, NULL), MMSYSERR_INVALPARAM);
    mod_close(h);
}

/* volume is per open: what a fresh open reads */
TEST(midiout_device_volume_fresh_open_value) {
    MOD_NEED_LIVE(); REQUIRE(U(midiOutGetVolume));
    DWORD first = 0;
    for (int k = 0; k < 2; k++) {
        HMIDIOUT h = mod_open_null(0);
        if (!h) return;
        DWORD v = 0xA5A5A5A5;
        EXPECT_EQ(U(midiOutGetVolume)(h, &v), MMSYSERR_NOERROR);
        if (k == 0) first = v; else EXPECT_EQ(v, first);
        mod_close(h);
    }
    t_note("fresh synth volume 0x%08lx", first);
    EXPECT_EQ(first, 0xFFFFFFFF);
}

/* the mapper handle refuses volume (NOTSUPPORTED) although its caps announce MIDICAPS_VOLUME [VM] */
TEST(midiout_device_volume_mapper_handle) {
    MOD_NEED_LIVE(); REQUIRE(U(midiOutGetVolume)); REQUIRE(U(midiOutSetVolume));
    HMIDIOUT h = mod_open_null(MIDI_MAPPER);
    if (!h) return;
    DWORD v = 0xA5A5A5A5;
    EXPECT_EQ(U(midiOutGetVolume)(h, &v), MMSYSERR_NOTSUPPORTED);
    EXPECT_EQ(v, 0xA5A5A5A5);
    EXPECT_EQ(U(midiOutSetVolume)(h, 0xFFFFFFFF), MMSYSERR_NOTSUPPORTED);
    /* the synth behind the mapper is open: its id now answers */
    v = 0xA5A5A5A5;
    MMRESULT r = U(midiOutGetVolume)((HMIDIOUT)0, &v);
    t_note("synth volume by id while the mapper is open: r=%u v=0x%08lx", r, v);
    mod_close(h);
}

/* volume write-back from another thread */
typedef struct { HMIDIOUT h; MMRESULT rg, rs; DWORD v; } volt_t;
static DWORD WINAPI vol_thread(LPVOID p) {
    volt_t *t = (volt_t *)p;
    t->rg = U(midiOutGetVolume)(t->h, &t->v);
    if (t->rg == MMSYSERR_NOERROR) t->rs = U(midiOutSetVolume)(t->h, t->v);
    return 0;
}
TEST(midiout_device_volume_other_thread) {
    MOD_NEED_LIVE(); REQUIRE(U(midiOutGetVolume)); REQUIRE(U(midiOutSetVolume));
    HMIDIOUT h = mod_open_null(0);
    if (!h) return;
    volt_t t; memset(&t, 0, sizeof(t)); t.h = h; t.rs = 0xFFFF;
    HANDLE th = CreateThread(NULL, 0, vol_thread, &t, 0, NULL);
    CHECK(WaitForSingleObject(th, 5000) == WAIT_OBJECT_0);
    CloseHandle(th);
    EXPECT_EQ(t.rg, MMSYSERR_NOERROR);
    EXPECT_EQ(t.rs, MMSYSERR_NOERROR);
    mod_close(h);
}

/* ================================================================ patch caching */

TEST(midiout_device_cache_patches_live) {
    MOD_NEED_LIVE(); REQUIRE(U(midiOutCachePatches)); REQUIRE(U(midiOutCacheDrumPatches));
    static const UINT fl[] = { MIDI_CACHE_ALL, MIDI_CACHE_BESTFIT, MIDI_CACHE_QUERY, MIDI_UNCACHE };
    FOR_IDS(h) {
        PATCHARRAY pa; KEYARRAY ka;
        for (int i = 0; i < NELEM(fl); i++) {
            memset(pa, 0, sizeof(pa)); memset(ka, 0, sizeof(ka));
            pa[0] = 0xFFFF; ka[35] = 1;
            MMRESULT rp = U(midiOutCachePatches)(h, 0, pa, fl[i]);
            MMRESULT rd = U(midiOutCacheDrumPatches)(h, 0, ka, fl[i]);
            CHECKF(rp == MMSYSERR_NOTSUPPORTED, "id 0x%x patches fl %u r=%u", g_ids[ii_], fl[i], rp);
            CHECKF(rd == MMSYSERR_NOTSUPPORTED, "id 0x%x drums fl %u r=%u", g_ids[ii_], fl[i], rd);
            CHECKF(pa[0] == 0xFFFF && ka[35] == 1, "array written");
        }
    }
}

/* ================================================================ midiOutMessage on a live handle */

TEST(midiout_device_message_handle) {
    MOD_NEED_LIVE(); REQUIRE(U(midiOutMessage));
    HMIDIOUT h = mod_open_null(0);
    if (!h) return;
    DWORD v = 0xA5A5A5A5;
    MMRESULT r = U(midiOutMessage)(h, MODM_GETVOLUME, (DWORD_PTR)&v, 0);
    EXPECT_EQ(r, MMSYSERR_NOERROR);
    EXPECT_EQ(v, 0xFFFFFFFF);
    /* MODM_DATA (7) through midiOutMessage is a short message */
    r = U(midiOutMessage)(h, 7, MOD_MSG(0xB0, 123, 0), 0);
    t_note("midiOutMessage(MODM_DATA) = %u", r);
    EXPECT_EQ(r, MMSYSERR_NOERROR);
    /* an unknown driver message */
    r = U(midiOutMessage)(h, 0x7FFF, 0, 0);
    t_note("midiOutMessage(0x7FFF) = %u", r);
    EXPECT_EQ(r, MMSYSERR_NOTSUPPORTED);
    DWORD sz = 0xA5A5A5A5;
    r = U(midiOutMessage)(h, DRV_QUERYFUNCTIONINSTANCEIDSIZE, (DWORD_PTR)&sz, 0);
    t_note("handle DRV_QUERYFUNCTIONINSTANCEIDSIZE = %u size 0x%lx", r, sz);
    EXPECT_EQ(r, MMSYSERR_NOTSUPPORTED);
    mod_close(h);
}

/* MODM_GETDEVCAPS through the handle fills the same caps as midiOutGetDevCaps */
TEST(midiout_device_message_getdevcaps_handle) {
    MOD_NEED_LIVE(); REQUIRE(U(midiOutMessage)); REQUIRE(U(midiOutGetDevCapsW));
    HMIDIOUT h = mod_open_null(0);
    if (!h) return;
    MIDIOUTCAPSW a, b; memset(&a, 0, sizeof(a)); memset(&b, 0, sizeof(b));
    MMRESULT r = U(midiOutMessage)(h, MODM_GETDEVCAPS, (DWORD_PTR)&a, sizeof(a));
    t_note("handle MODM_GETDEVCAPS = %u", r);
    EXPECT_EQ(U(midiOutGetDevCapsW)(0, &b, sizeof(b)), MMSYSERR_NOERROR);
    if (r == MMSYSERR_NOERROR) {
        EXPECT_WSTR(a.szPname, b.szPname);
        EXPECT_EQ(a.wTechnology, b.wTechnology);
    }
    mod_close(h);
}
