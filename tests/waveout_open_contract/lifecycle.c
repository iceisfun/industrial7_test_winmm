#include "wo.h"
/* waveOutClose and waveOutGetID: handle validation (no device needed) and the open/close/GetID lifecycle
 * (device half). ValidateHandle: a handle must be > 0xFFFF, not -1 / 0xFFFFFFFF, on the global handle list and
 * of type waveOut; anything else is 5. waveOutGetID tests the out pointer (11) BEFORE the handle and writes it
 * only on success. */

static const ULONG_PTR g_bad[] = { 0, 1, 2, 0x10, 0xFF, 0x1000, 0xFFFE, 0xFFFF, 0xFFFFFFFF, (ULONG_PTR)-1, (ULONG_PTR)-2 };
#define NBAD (sizeof g_bad / sizeof g_bad[0])

TEST(wo_close_bad_handles) {
    REQUIRE(U(waveOutClose));
    for (unsigned i = 0; i < NBAD; i++) {
        MMRESULT r = wo_close((HWAVEOUT)g_bad[i]);
        CHECKF(r == MMSYSERR_INVALHANDLE, "waveOutClose(0x%llx) -> %u", (unsigned long long)g_bad[i], r);
    }
}

TEST(wo_close_bad_handles_repeat) {
    REQUIRE(U(waveOutClose));
    for (int k = 0; k < 100; k++) {
        EXPECT_EQ(wo_close(NULL), MMSYSERR_INVALHANDLE);
        EXPECT_EQ(wo_close((HWAVEOUT)1), MMSYSERR_INVALHANDLE);
        EXPECT_EQ(wo_close((HWAVEOUT)0xFFFF), MMSYSERR_INVALHANDLE);
    }
}

TEST(wo_getid_null_out_first) {
    REQUIRE(U(waveOutGetID));
    for (unsigned i = 0; i < NBAD; i++) {
        MMRESULT r = wo_getid((HWAVEOUT)g_bad[i], NULL);
        CHECKF(r == MMSYSERR_INVALPARAM, "waveOutGetID(0x%llx, NULL) -> %u", (unsigned long long)g_bad[i], r);
    }
}

TEST(wo_getid_bad_handles_untouched) {
    REQUIRE(U(waveOutGetID));
    for (unsigned i = 0; i < NBAD; i++) {
        UINT id = 0xC0FFEE;
        MMRESULT r = wo_getid((HWAVEOUT)g_bad[i], &id);
        CHECKF(r == MMSYSERR_INVALHANDLE, "waveOutGetID(0x%llx) -> %u", (unsigned long long)g_bad[i], r);
        CHECKF(id == 0xC0FFEE, "out written on failure: 0x%x", id);
    }
}

/* An HMMIO lives on the same winmm handle list with another type: rejected as a waveOut handle. */
TEST(wo_crosstype_hmmio) {
    REQUIRE(U(waveOutClose)); REQUIRE(U(waveOutGetID)); REQUIRE(U(mmioOpenA)); REQUIRE(U(mmioClose));
    static char buf[256];
    MMIOINFO mi; memset(&mi, 0, sizeof mi);
    mi.fccIOProc = FOURCC_MEM; mi.pchBuffer = buf; mi.cchBuffer = sizeof buf;
    HMMIO m = U(mmioOpenA)(NULL, &mi, MMIO_READWRITE);
    CHECK(m != NULL);
    if (!m) return;
    UINT id = 0x77;
    EXPECT_EQ(wo_getid((HWAVEOUT)m, &id), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(id, 0x77);
    EXPECT_EQ(wo_getid((HWAVEOUT)m, NULL), MMSYSERR_INVALPARAM);
    EXPECT_EQ(wo_close((HWAVEOUT)m), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(mmioClose)(m, 0), MMSYSERR_NOERROR);    /* still alive after the rejected close */
}

/* ---- device half ---- */

TEST(wo_life_open_id0_close) {
    REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutClose)); REQUIRE(U(waveOutGetID));
    if (wo_nodev_contract(0)) return;
    HWAVEOUT h = WO_SENTINEL;
    EXPECT_EQ(wo_open_std(&h, 0, 0), MMSYSERR_NOERROR);
    HWAVEOUT got = h;
    CHECK(got != NULL && got != WO_SENTINEL);
    CHECKF((ULONG_PTR)got > 0xFFFF, "handle %p", got);
    if (!got || got == WO_SENTINEL) return;
    UINT id = 0x1234;
    EXPECT_EQ(wo_getid(got, &id), MMSYSERR_NOERROR);
    EXPECT_EQ(id, 0);
    EXPECT_EQ(wo_getid(got, NULL), MMSYSERR_INVALPARAM);
    EXPECT_EQ(wo_close(got), MMSYSERR_NOERROR);
    EXPECT_EQ(wo_close(got), MMSYSERR_INVALHANDLE);
    id = 0x1234;
    EXPECT_EQ(wo_getid(got, &id), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(id, 0x1234);
}

TEST(wo_life_open_mapper_close) {
    REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutClose)); REQUIRE(U(waveOutGetID));
    if (wo_nodev_contract(0)) return;
    HWAVEOUT h = WO_SENTINEL;
    EXPECT_EQ(wo_open_std(&h, WAVE_MAPPER, 0), MMSYSERR_NOERROR);
    HWAVEOUT got = h;
    if (!got || got == WO_SENTINEL) { CHECK(0); return; }
    UINT id = 0;
    EXPECT_EQ(wo_getid(got, &id), MMSYSERR_NOERROR);
    EXPECT_EQ(id, WAVE_MAPPER);                          /* the requested id, not the device the mapper chose */
    EXPECT_EQ(wo_close(got), MMSYSERR_NOERROR);
    EXPECT_EQ(wo_close(got), MMSYSERR_INVALHANDLE);
}

TEST(wo_life_open_ffff_reports_mapper) {
    REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutClose)); REQUIRE(U(waveOutGetID));
    if (wo_nodev_contract(0)) return;
    HWAVEOUT h = WO_SENTINEL;
    EXPECT_EQ(wo_open_std(&h, 0xFFFF, 0), MMSYSERR_NOERROR);
    HWAVEOUT got = h;
    if (!got || got == WO_SENTINEL) { CHECK(0); return; }
    UINT id = 0;
    EXPECT_EQ(wo_getid(got, &id), MMSYSERR_NOERROR);
    EXPECT_EQ(id, 0xFFFFFFFF);                           /* 0xFFFF was rewritten before it was stored */
    EXPECT_EQ(wo_close(got), MMSYSERR_NOERROR);
}

TEST(wo_life_open_mapped_id0) {
    REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutClose)); REQUIRE(U(waveOutGetID));
    if (wo_nodev_contract(WAVE_MAPPED)) return;
    HWAVEOUT h = WO_SENTINEL;
    EXPECT_EQ(wo_open_std(&h, 0, WAVE_MAPPED), MMSYSERR_NOERROR);
    HWAVEOUT got = h;
    if (!got || got == WO_SENTINEL) { CHECK(0); return; }
    UINT id = 0x55;
    EXPECT_EQ(wo_getid(got, &id), MMSYSERR_NOERROR);
    EXPECT_EQ(id, 0);
    EXPECT_EQ(wo_close(got), MMSYSERR_NOERROR);
}

TEST(wo_life_open_flags_accepted) {
    REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutClose)); REQUIRE(U(waveOutGetID));
    if (wo_nodev_contract(WAVE_ALLOWSYNC)) return;
    struct { UINT id; DWORD fl; UINT gid; } rows[] = {
        { 0, WAVE_ALLOWSYNC, 0 }, { WAVE_MAPPER, WAVE_ALLOWSYNC, WAVE_MAPPER },
        { 0, WAVE_FORMAT_DIRECT, 0 }, { WAVE_MAPPER, WAVE_FORMAT_DIRECT, WAVE_MAPPER },
        { 0, WAVE_ALLOWSYNC | WAVE_FORMAT_DIRECT, 0 }, { 0, WAVE_MAPPED | WAVE_FORMAT_DIRECT, 0 },
        { 0, WAVE_MAPPED | WAVE_ALLOWSYNC, 0 },
        { WAVE_MAPPER, WAVE_MAPPED_DEFAULT_COMMUNICATION_DEVICE, WAVE_MAPPER },
        { 0xFFFF, WAVE_MAPPED_DEFAULT_COMMUNICATION_DEVICE, WAVE_MAPPER },
        { WAVE_MAPPER, WAVE_MAPPED_DEFAULT_COMMUNICATION_DEVICE | WAVE_ALLOWSYNC, WAVE_MAPPER },
        { WAVE_MAPPER, CALLBACK_NULL, WAVE_MAPPER }, { 0, CALLBACK_WINDOW, 0 },   /* NULL window: no callback */
        { WAVE_MAPPER, CALLBACK_EVENT, WAVE_MAPPER }, { 0, CALLBACK_FUNCTION, 0 }, { 0, CALLBACK_THREAD, 0 },
    };
    for (unsigned i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        HWAVEOUT h = WO_SENTINEL;
        MMRESULT r = wo_open_std(&h, rows[i].id, rows[i].fl);   /* dwCallback 0 for every type */
        CHECKF(r == MMSYSERR_NOERROR, "id 0x%x fl 0x%lx -> %u", rows[i].id, rows[i].fl, r);
        if (r) continue;
        UINT id = 0x99;
        EXPECT_EQ(wo_getid(h, &id), MMSYSERR_NOERROR);
        CHECKF(id == rows[i].gid, "id 0x%x fl 0x%lx: GetID 0x%x want 0x%x", rows[i].id, rows[i].fl, id, rows[i].gid);
        EXPECT_EQ(wo_close(h), MMSYSERR_NOERROR);
    }
}

TEST(wo_life_callback_type7_open) {
    REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutClose));
    if (wo_nodev_contract(0)) return;
    WAVEFORMATEX wf; wo_std(&wf);
    EXPECT_EQ(wo_open(NULL, WAVE_MAPPER, &wf, 0, 0, 0x70000 | WAVE_FORMAT_QUERY), MMSYSERR_NOERROR);
    static const DWORD types[] = { 0x40000, 0x60000, 0x70000 };
    for (int t = 0; t < 3; t++) {
        HWAVEOUT h = WO_SENTINEL;
        MMRESULT r = wo_open(&h, WAVE_MAPPER, &wf, 0, 0, types[t]);
        HWAVEOUT got = h;
        CHECKF(r == MMSYSERR_NOERROR, "callback type 0x%lx without QUERY, mapper -> %u", types[t], r);
        wo_release(r, 0, got);
        h = WO_SENTINEL;
        r = wo_open(&h, 0, &wf, 0, 0, types[t]);
        got = h;
        CHECKF(r == MMSYSERR_NOERROR, "callback type 0x%lx without QUERY, id 0 -> %u", types[t], r);
        wo_release(r, 0, got);
    }
}

TEST(wo_life_passthrough_bits_open) {
    REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutClose));
    if (wo_nodev_contract(0)) return;
    WAVEFORMATEX wf; wo_std(&wf);
    static const DWORD bits[] = { 0x20, 0x40, 0x80, 0x100, 0x8000, 0x80000, 0x01000000 };
    for (unsigned i = 0; i < sizeof bits / sizeof bits[0]; i++) {
        HWAVEOUT h = WO_SENTINEL;
        MMRESULT r = wo_open(&h, 0, &wf, 0, 0, bits[i]);
        HWAVEOUT got = h;
        CHECKF(r == MMSYSERR_NOERROR, "open id0 bit 0x%lx -> %u (driver ignores it, VM)", bits[i], r);
        if (r) CHECKF(got == NULL, "failed open left *phwo %p", got);
        wo_release(r, 0, got);
    }
}

TEST(wo_life_same_device_twice) {
    REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutClose)); REQUIRE(U(waveOutGetID));
    if (wo_nodev_contract(0)) return;
    HWAVEOUT a = NULL, b = NULL;
    EXPECT_EQ(wo_open_std(&a, 0, 0), MMSYSERR_NOERROR);
    EXPECT_EQ(wo_open_std(&b, 0, 0), MMSYSERR_NOERROR);
    CHECK(a && b && a != b);
    UINT ia = 9, ib = 9;
    if (a) { EXPECT_EQ(wo_getid(a, &ia), MMSYSERR_NOERROR); EXPECT_EQ(ia, 0); }
    if (b) { EXPECT_EQ(wo_getid(b, &ib), MMSYSERR_NOERROR); EXPECT_EQ(ib, 0); }
    if (a) EXPECT_EQ(wo_close(a), MMSYSERR_NOERROR);
    if (b) {   /* the other handle is unaffected by closing the first */
        ib = 9; EXPECT_EQ(wo_getid(b, &ib), MMSYSERR_NOERROR); EXPECT_EQ(ib, 0);
        EXPECT_EQ(wo_close(b), MMSYSERR_NOERROR);
    }
}

TEST(wo_life_mapper_and_device_concurrently) {
    REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutClose)); REQUIRE(U(waveOutGetID));
    if (wo_nodev_contract(0)) return;
    HWAVEOUT m = NULL, d = NULL;
    EXPECT_EQ(wo_open_std(&m, WAVE_MAPPER, 0), MMSYSERR_NOERROR);
    EXPECT_EQ(wo_open_std(&d, 0, 0), MMSYSERR_NOERROR);
    UINT im = 0, id = 7;
    if (m) { EXPECT_EQ(wo_getid(m, &im), MMSYSERR_NOERROR); EXPECT_EQ(im, WAVE_MAPPER); }
    if (d) { EXPECT_EQ(wo_getid(d, &id), MMSYSERR_NOERROR); EXPECT_EQ(id, 0); }
    if (d) EXPECT_EQ(wo_close(d), MMSYSERR_NOERROR);
    if (m) EXPECT_EQ(wo_close(m), MMSYSERR_NOERROR);
}

TEST(wo_life_many_concurrent_handles) {
    REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutClose)); REQUIRE(U(waveOutGetID));
    if (wo_nodev_contract(0)) return;
    enum { N = 8 };
    HWAVEOUT h[N]; MMRESULT r[N];
    for (int i = 0; i < N; i++) { h[i] = NULL; r[i] = wo_open_std(&h[i], (i & 1) ? WAVE_MAPPER : 0, 0); }
    for (int i = 0; i < N; i++) CHECKF(r[i] == MMSYSERR_NOERROR, "open %d -> %u", i, r[i]);
    for (int i = 0; i < N; i++) for (int j = i + 1; j < N; j++) if (!r[i] && !r[j]) CHECK(h[i] != h[j]);
    for (int i = 0; i < N; i++) if (!r[i]) {
        UINT id = 0x42;
        EXPECT_EQ(wo_getid(h[i], &id), MMSYSERR_NOERROR);
        EXPECT_EQ(id, (i & 1) ? WAVE_MAPPER : 0);
    }
    /* close in reverse order of odd, then even */
    for (int i = N - 1; i >= 0; i -= 2) if (!r[i]) EXPECT_EQ(wo_close(h[i]), MMSYSERR_NOERROR);
    for (int i = N - 2; i >= 0; i -= 2) if (!r[i]) EXPECT_EQ(wo_close(h[i]), MMSYSERR_NOERROR);
    for (int i = 0; i < N; i++) if (!r[i]) EXPECT_EQ(wo_close(h[i]), MMSYSERR_INVALHANDLE);
}

TEST(wo_life_open_close_cycles) {
    REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutClose));
    if (wo_nodev_contract(0)) return;
    int bad = 0;
    for (int k = 0; k < 25; k++) {
        HWAVEOUT h = NULL;
        MMRESULT r = wo_open_std(&h, (k % 3 == 0) ? WAVE_MAPPER : 0, 0);
        if (r) { bad++; continue; }
        if (wo_close(h) != MMSYSERR_NOERROR) bad++;
    }
    EXPECT_EQ(bad, 0);
}

static DWORD WINAPI thr_close(void *p) { return wo_close((HWAVEOUT)p); }
static DWORD WINAPI thr_getid(void *p) { UINT id = 0x31; MMRESULT r = wo_getid((HWAVEOUT)p, &id); return r ? 0x10000 | r : id; }
static DWORD WINAPI thr_open(void *p) {
    HWAVEOUT h = NULL; MMRESULT r = wo_open_std(&h, 0, 0);
    *(HWAVEOUT *)p = h;
    return r;
}

TEST(wo_life_close_on_other_thread) {
    REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutClose));
    if (wo_nodev_contract(0)) return;
    HWAVEOUT h = NULL;
    EXPECT_EQ(wo_open_std(&h, 0, 0), MMSYSERR_NOERROR);
    if (!h) return;
    EXPECT_EQ(wo_on_thread(thr_getid, h, 5000), 0);
    EXPECT_EQ(wo_on_thread(thr_close, h, 5000), MMSYSERR_NOERROR);
    EXPECT_EQ(wo_close(h), MMSYSERR_INVALHANDLE);
}

TEST(wo_life_open_on_thread_that_exits) {
    REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutClose)); REQUIRE(U(waveOutGetID));
    if (wo_nodev_contract(0)) return;
    HWAVEOUT h = NULL;
    EXPECT_EQ(wo_on_thread(thr_open, &h, 5000), MMSYSERR_NOERROR);
    if (!h) return;
    UINT id = 3;
    EXPECT_EQ(wo_getid(h, &id), MMSYSERR_NOERROR);   /* handles are per process, not per thread */
    EXPECT_EQ(id, 0);
    EXPECT_EQ(wo_close(h), MMSYSERR_NOERROR);
}

typedef struct { volatile LONG go; int fails; } race_ctx;
static DWORD WINAPI thr_cycle(void *p) {
    race_ctx *c = (race_ctx *)p;
    while (!c->go) Sleep(0);
    int fails = 0;
    for (int k = 0; k < 10; k++) {
        HWAVEOUT h = NULL;
        if (wo_open_std(&h, k & 1 ? WAVE_MAPPER : 0, 0) != MMSYSERR_NOERROR) { fails++; continue; }
        UINT id = 5;
        if (wo_getid(h, &id) != MMSYSERR_NOERROR || id != (k & 1 ? WAVE_MAPPER : 0u)) fails++;
        if (wo_close(h) != MMSYSERR_NOERROR) fails++;
    }
    return fails;
}

TEST(wo_life_parallel_open_close) {
    REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutClose)); REQUIRE(U(waveOutGetID));
    if (wo_nodev_contract(0)) return;
    race_ctx c = { 0, 0 };
    HANDLE th[3];
    for (int i = 0; i < 3; i++) th[i] = CreateThread(NULL, 0, thr_cycle, &c, 0, NULL);
    InterlockedExchange(&c.go, 1);
    DWORD w = WaitForMultipleObjects(3, th, TRUE, 20000);
    EXPECT_EQ(w, WAIT_OBJECT_0);
    for (int i = 0; i < 3; i++) {
        DWORD code = 0xDEAD;
        GetExitCodeThread(th[i], &code);
        CHECKF(code == 0, "thread %d failures %lu", i, code);
        CloseHandle(th[i]);
    }
}

TEST(wo_life_closed_handle_rejected_everywhere) {
    REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutClose)); REQUIRE(U(waveOutGetID));
    REQUIRE(U(waveOutReset)); REQUIRE(U(waveOutPause)); REQUIRE(U(waveOutRestart));
    if (wo_nodev_contract(0)) return;
    HWAVEOUT h = NULL;
    EXPECT_EQ(wo_open_std(&h, 0, 0), MMSYSERR_NOERROR);
    if (!h) return;
    EXPECT_EQ(wo_close(h), MMSYSERR_NOERROR);
    EXPECT_EQ(wo_close(h), MMSYSERR_INVALHANDLE);
    UINT id = 0xAB;
    EXPECT_EQ(wo_getid(h, &id), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(id, 0xAB);
    EXPECT_EQ(wo_getid(h, NULL), MMSYSERR_INVALPARAM);
    EXPECT_EQ(U(waveOutReset)(h), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveOutPause)(h), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveOutRestart)(h), MMSYSERR_INVALHANDLE);
}

/* An open handle passed to the other device families' entry points is the wrong type. */
TEST(wo_crosstype_waveout_to_other_families) {
    REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutClose)); REQUIRE(U(waveInClose)); REQUIRE(U(waveInGetID));
    REQUIRE(U(midiOutClose)); REQUIRE(U(midiInClose)); REQUIRE(U(mixerClose));
    if (wo_nodev_contract(0)) return;
    HWAVEOUT h = NULL;
    EXPECT_EQ(wo_open_std(&h, 0, 0), MMSYSERR_NOERROR);
    if (!h) return;
    UINT id = 0x66;
    EXPECT_EQ(U(waveInClose)((HWAVEIN)h), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveInGetID)((HWAVEIN)h, &id), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(id, 0x66);
    EXPECT_EQ(U(midiOutClose)((HMIDIOUT)h), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(midiInClose)((HMIDIIN)h), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(mixerClose)((HMIXER)h), MMSYSERR_INVALHANDLE);
    /* still a working waveOut handle */
    EXPECT_EQ(wo_getid(h, &id), MMSYSERR_NOERROR);
    EXPECT_EQ(id, 0);
    EXPECT_EQ(wo_close(h), MMSYSERR_NOERROR);
}

TEST(wo_crosstype_hwavein) {
    REQUIRE(U(waveOutClose)); REQUIRE(U(waveOutGetID)); REQUIRE(U(waveInOpen)); REQUIRE(U(waveInClose));
    if (wo_nwavein() == 0) {
        WAVEFORMATEX wf; wo_pcm(&wf, 1, 44100, 16);
        HWAVEIN hi = (HWAVEIN)WO_SENTINEL;
        EXPECT_EQ(U(waveInOpen)(&hi, WAVE_MAPPER, &wf, 0, 0, 0), MMSYSERR_BADDEVICEID);
        t_note("no waveIn device: HWAVEIN cross-type case not run");
        return;
    }
    WAVEFORMATEX wf; wo_pcm(&wf, 1, 44100, 16);
    HWAVEIN hi = NULL;
    MMRESULT r = U(waveInOpen)(&hi, WAVE_MAPPER, &wf, 0, 0, 0);
    EXPECT_EQ(r, MMSYSERR_NOERROR);
    if (r) return;
    UINT id = 0x21;
    EXPECT_EQ(wo_close((HWAVEOUT)hi), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(wo_getid((HWAVEOUT)hi, &id), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(id, 0x21);
    EXPECT_EQ(U(waveInClose)(hi), MMSYSERR_NOERROR);
}

TEST(wo_crosstype_hmidiout) {
    REQUIRE(U(waveOutClose)); REQUIRE(U(waveOutGetID)); REQUIRE(U(midiOutOpen)); REQUIRE(U(midiOutClose));
    REQUIRE(U(midiOutGetNumDevs)); REQUIRE(U(midiOutShortMsg));
    if (U(midiOutGetNumDevs)() == 0 || wo_ndev() == 0) { t_note("no usable midiOut device (needs a wave device)"); return; }
    HMIDIOUT hm = NULL;
    MMRESULT r = U(midiOutOpen)(&hm, 0, 0, 0, CALLBACK_NULL);
    if (r != MMSYSERR_NOERROR) { t_note("midiOutOpen(0) -> %u, case not run", r); return; }
    UINT id = 0x22;
    EXPECT_EQ(wo_close((HWAVEOUT)hm), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(wo_getid((HWAVEOUT)hm, &id), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(id, 0x22);
    for (DWORD ch = 0; ch < 16; ch++) U(midiOutShortMsg)(hm, 0x7BB0 | ch);   /* all notes off */
    EXPECT_EQ(U(midiOutClose)(hm), MMSYSERR_NOERROR);
}

TEST(wo_crosstype_hmixer) {
    REQUIRE(U(waveOutClose)); REQUIRE(U(waveOutGetID)); REQUIRE(U(mixerOpen)); REQUIRE(U(mixerClose)); REQUIRE(U(mixerGetNumDevs));
    if (U(mixerGetNumDevs)() == 0) { t_note("no mixer"); return; }
    HMIXER hx = NULL;
    MMRESULT r = U(mixerOpen)(&hx, 0, 0, 0, 0);
    EXPECT_EQ(r, MMSYSERR_NOERROR);
    if (r) return;
    UINT id = 0x23;
    EXPECT_EQ(wo_close((HWAVEOUT)hx), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(wo_getid((HWAVEOUT)hx, &id), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(id, 0x23);
    EXPECT_EQ(U(mixerClose)(hx), MMSYSERR_NOERROR);
}

/* ---- close semantics with buffers (silence only) ---- */

static short g_silence[44100 * 2];   /* 1 s of 44.1 kHz 16-bit stereo silence (zeroed) */

TEST(wo_close_with_queued_buffer) {
    REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutClose)); REQUIRE(U(waveOutPrepareHeader)); REQUIRE(U(waveOutWrite));
    REQUIRE(U(waveOutReset)); REQUIRE(U(waveOutUnprepareHeader)); REQUIRE(U(waveOutGetID));
    if (wo_nodev_contract(0)) return;
    HWAVEOUT h = NULL;
    EXPECT_EQ(wo_open_std(&h, 0, 0), MMSYSERR_NOERROR);
    if (!h) return;
    WAVEHDR wh; memset(&wh, 0, sizeof wh);
    wh.lpData = (LPSTR)g_silence; wh.dwBufferLength = sizeof g_silence;
    EXPECT_EQ(U(waveOutPrepareHeader)(h, &wh, sizeof wh), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveOutWrite)(h, &wh, sizeof wh), MMSYSERR_NOERROR);
    MMRESULT r = wo_close(h);
    EXPECT_EQ(r, WAVERR_STILLPLAYING);
    if (r == MMSYSERR_NOERROR) return;               /* handle gone; header memory is ours */
    UINT id = 9;
    EXPECT_EQ(wo_getid(h, &id), MMSYSERR_NOERROR);   /* still open after the refused close */
    EXPECT_EQ(id, 0);
    EXPECT_EQ(U(waveOutReset)(h), MMSYSERR_NOERROR);
    DWORD t0 = GetTickCount();
    while ((wh.dwFlags & WHDR_INQUEUE) && GetTickCount() - t0 < 2000) Sleep(10);
    CHECKF(!(wh.dwFlags & WHDR_INQUEUE), "flags 0x%lx after reset", wh.dwFlags);
    EXPECT_EQ(U(waveOutUnprepareHeader)(h, &wh, sizeof wh), MMSYSERR_NOERROR);
    EXPECT_EQ(wo_close(h), MMSYSERR_NOERROR);
}

TEST(wo_close_paused_empty) {
    REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutClose)); REQUIRE(U(waveOutPause));
    if (wo_nodev_contract(0)) return;
    HWAVEOUT h = NULL;
    EXPECT_EQ(wo_open_std(&h, WAVE_MAPPER, 0), MMSYSERR_NOERROR);
    if (!h) return;
    EXPECT_EQ(U(waveOutPause)(h), MMSYSERR_NOERROR);
    EXPECT_EQ(wo_close(h), MMSYSERR_NOERROR);
}

TEST(wo_close_after_buffer_done) {
    REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutClose)); REQUIRE(U(waveOutPrepareHeader)); REQUIRE(U(waveOutWrite));
    REQUIRE(U(waveOutReset)); REQUIRE(U(waveOutUnprepareHeader));
    if (wo_nodev_contract(0)) return;
    HWAVEOUT h = NULL;
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    WAVEFORMATEX wf; wo_std(&wf);
    EXPECT_EQ(wo_open(&h, 0, &wf, (DWORD_PTR)ev, 0, CALLBACK_EVENT), MMSYSERR_NOERROR);
    if (!h) { CloseHandle(ev); return; }
    WAVEHDR wh; memset(&wh, 0, sizeof wh);
    wh.lpData = (LPSTR)g_silence; wh.dwBufferLength = 44100 * 4 / 20;    /* 50 ms */
    EXPECT_EQ(U(waveOutPrepareHeader)(h, &wh, sizeof wh), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveOutWrite)(h, &wh, sizeof wh), MMSYSERR_NOERROR);
    DWORD t0 = GetTickCount();
    while (!(wh.dwFlags & WHDR_DONE) && GetTickCount() - t0 < 3000) WaitForSingleObject(ev, 100);
    CHECKF(wh.dwFlags & WHDR_DONE, "flags 0x%lx", wh.dwFlags);
    if (!(wh.dwFlags & WHDR_DONE)) U(waveOutReset)(h);
    EXPECT_EQ(U(waveOutUnprepareHeader)(h, &wh, sizeof wh), MMSYSERR_NOERROR);
    EXPECT_EQ(wo_close(h), MMSYSERR_NOERROR);
    CloseHandle(ev);
}

TEST(wo_close_with_prepared_unwritten_header) {
    REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutClose)); REQUIRE(U(waveOutPrepareHeader)); REQUIRE(U(waveOutUnprepareHeader));
    if (wo_nodev_contract(0)) return;
    HWAVEOUT h = NULL;
    EXPECT_EQ(wo_open_std(&h, 0, 0), MMSYSERR_NOERROR);
    if (!h) return;
    WAVEHDR wh; memset(&wh, 0, sizeof wh);
    wh.lpData = (LPSTR)g_silence; wh.dwBufferLength = 4096;
    EXPECT_EQ(U(waveOutPrepareHeader)(h, &wh, sizeof wh), MMSYSERR_NOERROR);
    MMRESULT r = wo_close(h);
    EXPECT_EQ(r, MMSYSERR_NOERROR);                     /* a prepared, never written header does not block close (VM) */
    if (r == MMSYSERR_NOERROR) {
        /* PREPARED is still set in our header; with the handle gone the unprepare fails the handle check */
        EXPECT_EQ(wh.dwFlags & WHDR_PREPARED, WHDR_PREPARED);
        EXPECT_EQ(U(waveOutUnprepareHeader)(h, &wh, sizeof wh), MMSYSERR_INVALHANDLE);
    } else {
        EXPECT_EQ(U(waveOutUnprepareHeader)(h, &wh, sizeof wh), MMSYSERR_NOERROR);
        EXPECT_EQ(wo_close(h), MMSYSERR_NOERROR);
    }
}
