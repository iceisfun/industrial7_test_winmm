#include "wp.h"
/* Device-independent contract of waveOutPause / Restart / Reset / BreakLoop / GetPosition.
 *
 * winmmbase (decomp): every one of the five calls ClientUpdatePnpInfo, then ValidateHandle(h, 1) -> 5
 * (MMSYSERR_INVALHANDLE) when the value is not a live waveOut handle. ValidateHandle only compares the value against
 * its handle list (values <= 0xFFFF, 0xFFFFFFFF and ~0 are rejected before the list walk), so these values never get
 * dereferenced. waveOutGetPosition checks pmmt == NULL first -> 11 (MMSYSERR_INVALPARAM), before the handle.
 * waveOutReset turns a driver 6 on a "device gone" handle into 0; Pause/Restart/BreakLoop do not.
 * VM_TIMEOUT: 240 */

#define NBAD 16

typedef MMRESULT (WINAPI *wp_hfn)(HWAVEOUT);

static wp_hfn fn_of(int k) {
    switch (k) {
    case 0: return (wp_hfn)U(waveOutPause);
    case 1: return (wp_hfn)U(waveOutRestart);
    case 2: return (wp_hfn)U(waveOutReset);
    default: return (wp_hfn)U(waveOutBreakLoop);
    }
}
static const char *fn_name[] = { "waveOutPause", "waveOutRestart", "waveOutReset", "waveOutBreakLoop" };

/* One handle-only function x one bad value: 5, twice (no state). */
static void bad_handle_case(int k, int idx) {
    wp_badh b[NBAD];
    int n = wp_bad_handles(b, NBAD);
    if (idx >= n) { t_skip("no bad handle %d", idx); return; }
    wp_hfn f = fn_of(k);
    if (!f) { t_skip("missing %s", fn_name[k]); return; }
    MMRESULT r1 = f(b[idx].h), r2 = f(b[idx].h);
    CHECKF(r1 == MMSYSERR_INVALHANDLE, "%s(%s) = %u", fn_name[k], b[idx].name, r1);
    CHECKF(r2 == MMSYSERR_INVALHANDLE, "%s(%s) second call = %u", fn_name[k], b[idx].name, r2);
}

#define BADH(fnk, nm, idx) TEST(wp_contract_##nm##_badh_##idx) { \
    REQUIRE(U(waveOutPause)); REQUIRE(U(waveOutRestart)); REQUIRE(U(waveOutReset)); REQUIRE(U(waveOutBreakLoop)); \
    bad_handle_case(fnk, idx); }

BADH(0, pause, 0) BADH(0, pause, 1) BADH(0, pause, 2) BADH(0, pause, 3) BADH(0, pause, 4)
BADH(0, pause, 5) BADH(0, pause, 6) BADH(0, pause, 7) BADH(0, pause, 8) BADH(0, pause, 9)
BADH(1, restart, 0) BADH(1, restart, 1) BADH(1, restart, 2) BADH(1, restart, 3) BADH(1, restart, 4)
BADH(1, restart, 5) BADH(1, restart, 6) BADH(1, restart, 7) BADH(1, restart, 8) BADH(1, restart, 9)
BADH(2, reset, 0) BADH(2, reset, 1) BADH(2, reset, 2) BADH(2, reset, 3) BADH(2, reset, 4)
BADH(2, reset, 5) BADH(2, reset, 6) BADH(2, reset, 7) BADH(2, reset, 8) BADH(2, reset, 9)
BADH(3, breakloop, 0) BADH(3, breakloop, 1) BADH(3, breakloop, 2) BADH(3, breakloop, 3) BADH(3, breakloop, 4)
BADH(3, breakloop, 5) BADH(3, breakloop, 6) BADH(3, breakloop, 7) BADH(3, breakloop, 8) BADH(3, breakloop, 9)

/* ---- waveOutGetPosition ------------------------------------------------------------------------------------ */

static const UINT k_types[] = { TIME_MS, TIME_SAMPLES, TIME_BYTES, TIME_SMPTE, TIME_MIDI, TIME_TICKS, 0, 0x40, 0xFFFF };

/* pmmt NULL is checked before the handle: 11 for every bad handle and any cbmmt. */
static void pos_null_case(int idx) {
    wp_badh b[NBAD];
    int n = wp_bad_handles(b, NBAD);
    if (idx >= n) { t_skip("no bad handle %d", idx); return; }
    static const UINT cbs[] = { 0, 1, sizeof(MMTIME) - 1, sizeof(MMTIME), sizeof(MMTIME) + 1, 0xFFFFFFFF };
    for (int i = 0; i < (int)(sizeof cbs / sizeof cbs[0]); i++) {
        MMRESULT r = U(waveOutGetPosition)(b[idx].h, NULL, cbs[i]);
        CHECKF(r == MMSYSERR_INVALPARAM, "GetPosition(%s, NULL, %u) = %u", b[idx].name, cbs[i], r);
    }
}
/* A bad handle with a real MMTIME: 5 for every wType and cbmmt, and the structure is not touched (no driver call). */
static void pos_badh_case(int idx) {
    wp_badh b[NBAD];
    int n = wp_bad_handles(b, NBAD);
    if (idx >= n) { t_skip("no bad handle %d", idx); return; }
    static const UINT cbs[] = { 0, 1, sizeof(MMTIME) - 1, sizeof(MMTIME), sizeof(MMTIME) + 4 };
    for (int t = 0; t < (int)(sizeof k_types / sizeof k_types[0]); t++)
        for (int i = 0; i < (int)(sizeof cbs / sizeof cbs[0]); i++) {
            unsigned char buf[sizeof(MMTIME) + 8], ref[sizeof buf];
            memset(buf, 0xA5, sizeof buf);
            ((MMTIME *)buf)->wType = k_types[t];
            memcpy(ref, buf, sizeof buf);
            MMRESULT r = U(waveOutGetPosition)(b[idx].h, (MMTIME *)buf, cbs[i]);
            CHECKF(r == MMSYSERR_INVALHANDLE, "GetPosition(%s, type 0x%x, cb %u) = %u", b[idx].name, k_types[t], cbs[i], r);
            CHECKF(!memcmp(buf, ref, sizeof buf), "MMTIME modified for %s type 0x%x cb %u", b[idx].name, k_types[t], cbs[i]);
        }
}
#define POSNULL(idx) TEST(wp_contract_getpos_null_badh_##idx) { REQUIRE(U(waveOutGetPosition)); pos_null_case(idx); }
#define POSBAD(idx)  TEST(wp_contract_getpos_badh_##idx) { REQUIRE(U(waveOutGetPosition)); pos_badh_case(idx); }
POSNULL(0) POSNULL(1) POSNULL(2) POSNULL(3) POSNULL(4) POSNULL(5) POSNULL(6) POSNULL(7) POSNULL(8) POSNULL(9)
POSBAD(0) POSBAD(1) POSBAD(2) POSBAD(3) POSBAD(4) POSBAD(5) POSBAD(6) POSBAD(7) POSBAD(8) POSBAD(9)

/* The NULL handle with NULL pmmt is the classic "which check first" case: the pointer wins. */
TEST(wp_contract_getpos_null_null) {
    REQUIRE(U(waveOutGetPosition));
    EXPECT_EQ(U(waveOutGetPosition)(NULL, NULL, sizeof(MMTIME)), MMSYSERR_INVALPARAM);
    EXPECT_EQ(U(waveOutGetPosition)(NULL, NULL, 0), MMSYSERR_INVALPARAM);
}

/* The same ladder holds whatever the device count is (nothing here depends on a device). */
TEST(wp_contract_ladder_independent_of_devices) {
    REQUIRE(U(waveOutGetPosition)); REQUIRE(U(waveOutPause));
    UINT n = wp_nout();
    t_note("waveOutGetNumDevs = %u", n);
    MMTIME mt; mt.wType = TIME_BYTES;
    HWAVEOUT h = (HWAVEOUT)(ULONG_PTR)n;   /* a device id is not a handle */
    EXPECT_EQ(U(waveOutGetPosition)(h, &mt, sizeof mt), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveOutGetPosition)((HWAVEOUT)(ULONG_PTR)WAVE_MAPPER, &mt, sizeof mt), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveOutPause)((HWAVEOUT)(ULONG_PTR)WAVE_MAPPER), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveOutRestart)((HWAVEOUT)(ULONG_PTR)WAVE_MAPPER), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveOutReset)((HWAVEOUT)(ULONG_PTR)WAVE_MAPPER), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveOutBreakLoop)((HWAVEOUT)(ULONG_PTR)WAVE_MAPPER), MMSYSERR_INVALHANDLE);
    for (UINT id = 0; id < 4; id++) {
        EXPECT_EQ(U(waveOutPause)((HWAVEOUT)(ULONG_PTR)id), MMSYSERR_INVALHANDLE);
        EXPECT_EQ(U(waveOutReset)((HWAVEOUT)(ULONG_PTR)id), MMSYSERR_INVALHANDLE);
    }
}

/* The exports resolve from winmm.dll (the module under test) and from winmmbase.dll (the implementation). */
TEST(wp_contract_exports_resolve) {
    static const char *names[] = { "waveOutPause", "waveOutRestart", "waveOutReset", "waveOutBreakLoop", "waveOutGetPosition" };
    for (int i = 0; i < 5; i++) {
        FARPROC a = t_proc_in("winmm.dll", names[i]), b = t_proc_in("winmmbase.dll", names[i]);
        CHECKF(a != NULL, "winmm!%s", names[i]);
        CHECKF(b != NULL, "winmmbase!%s", names[i]);
    }
    /* The winmm export reaches the same code: a bad handle gives the same answer through either module. */
    REQUIRE(UIN("winmm.dll", waveOutPause)); REQUIRE(UIN("winmmbase.dll", waveOutPause));
    EXPECT_EQ(UIN("winmm.dll", waveOutPause)(NULL), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(UIN("winmmbase.dll", waveOutPause)(NULL), MMSYSERR_INVALHANDLE);
    REQUIRE(UIN("winmm.dll", waveOutGetPosition)); REQUIRE(UIN("winmmbase.dll", waveOutGetPosition));
    EXPECT_EQ(UIN("winmm.dll", waveOutGetPosition)(NULL, NULL, 12), MMSYSERR_INVALPARAM);
    EXPECT_EQ(UIN("winmmbase.dll", waveOutGetPosition)(NULL, NULL, 12), MMSYSERR_INVALPARAM);
}

/* ---- handles that were valid, or are valid for another kind ---------------------------------------------------- */

TEST(wp_contract_closed_handle_all_five) {
    REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutGetPosition));
    WP_NEED_DEV();
    HWAVEOUT h = wp_closed_handle();
    if (!h) { CHECKF(0, "could not open/close a waveOut handle"); return; }
    MMTIME mt; mt.wType = TIME_BYTES;
    EXPECT_EQ(U(waveOutPause)(h), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveOutRestart)(h), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveOutReset)(h), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveOutBreakLoop)(h), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveOutGetPosition)(h, &mt, sizeof mt), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveOutGetPosition)(h, NULL, sizeof mt), MMSYSERR_INVALPARAM);
    EXPECT_EQ(U(waveOutClose)(h), MMSYSERR_INVALHANDLE);
}

/* ValidateHandle(h, 1): a live HWAVEIN (type 2) is not a waveOut handle. */
TEST(wp_contract_wavein_handle_rejected) {
    REQUIRE(U(waveInOpen)); REQUIRE(U(waveInClose)); REQUIRE(U(waveOutGetPosition));
    if (wp_nin() == 0) { t_skip("no waveIn device"); return; }
    WAVEFORMATEX wf; wp_fmt_std(&wf);
    HWAVEIN hi = NULL;
    MMRESULT r = U(waveInOpen)(&hi, WAVE_MAPPER, &wf, 0, 0, CALLBACK_NULL);
    if (r != MMSYSERR_NOERROR) { t_skip("waveInOpen = %u", r); return; }
    HWAVEOUT h = (HWAVEOUT)hi;
    MMTIME mt; memset(&mt, 0xA5, sizeof mt); mt.wType = TIME_BYTES;
    EXPECT_EQ(U(waveOutPause)(h), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveOutRestart)(h), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveOutReset)(h), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveOutBreakLoop)(h), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveOutGetPosition)(h, &mt, sizeof mt), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(mt.u.cb, 0xA5A5A5A5);
    EXPECT_EQ(U(waveOutGetPosition)(h, NULL, sizeof mt), MMSYSERR_INVALPARAM);
    /* the waveIn handle itself still works */
    if (U(waveInGetPosition)) {
        mt.wType = TIME_BYTES;
        EXPECT_EQ(U(waveInGetPosition)(hi, &mt, sizeof mt), MMSYSERR_NOERROR);
    }
    EXPECT_EQ(U(waveInClose)(hi), MMSYSERR_NOERROR);
}

TEST(wp_contract_midiout_handle_rejected) {
    REQUIRE(U(midiOutOpen)); REQUIRE(U(midiOutClose)); REQUIRE(U(midiOutGetNumDevs));
    if (wp_nout() == 0 || U(midiOutGetNumDevs)() == 0) { t_skip("midiOut open needs a waveOut device"); return; }
    HMIDIOUT hm = NULL;
    MMRESULT r = U(midiOutOpen)(&hm, 0, 0, 0, CALLBACK_NULL);
    if (r != MMSYSERR_NOERROR) { t_skip("midiOutOpen(0) = %u", r); return; }
    HWAVEOUT h = (HWAVEOUT)hm;
    MMTIME mt; mt.wType = TIME_BYTES;
    EXPECT_EQ(U(waveOutPause)(h), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveOutRestart)(h), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveOutReset)(h), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveOutBreakLoop)(h), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveOutGetPosition)(h, &mt, sizeof mt), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(midiOutClose)(hm), MMSYSERR_NOERROR);
}

/* A live handle with pmmt NULL is still 11 (pointer check first), and the handle is unaffected. */
TEST(wp_contract_live_handle_null_mmtime) {
    REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutGetPosition));
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d) != MMSYSERR_NOERROR) { CHECKF(0, "open failed"); return; }
    static const UINT cbs[] = { 0, sizeof(MMTIME), 0x7FFFFFFF };
    for (int i = 0; i < 3; i++) EXPECT_EQ(U(waveOutGetPosition)(d.h, NULL, cbs[i]), MMSYSERR_INVALPARAM);
    EXPECT_EQ(wp_samples(d.h), 0);
    EXPECT_EQ(wp_close(&d), MMSYSERR_NOERROR);
}

/* Device ids are values <= 0xFFFF: never handles, whether or not a device with that id exists. */
TEST(wp_contract_device_ids_are_not_handles) {
    REQUIRE(U(waveOutGetPosition));
    UINT n = wp_nout();
    for (UINT id = 0; id <= n + 1; id++) {
        MMTIME mt; mt.wType = TIME_SAMPLES;
        EXPECT_EQ(U(waveOutGetPosition)((HWAVEOUT)(ULONG_PTR)id, &mt, sizeof mt), MMSYSERR_INVALHANDLE);
        EXPECT_EQ(U(waveOutBreakLoop)((HWAVEOUT)(ULONG_PTR)id), MMSYSERR_INVALHANDLE);
        EXPECT_EQ(U(waveOutRestart)((HWAVEOUT)(ULONG_PTR)id), MMSYSERR_INVALHANDLE);
    }
}
