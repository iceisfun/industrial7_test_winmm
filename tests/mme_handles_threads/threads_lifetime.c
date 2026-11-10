#include "mh.h"
/* Handles across threads, handle lifetime / leak checks, and the first-use device-count race in a fresh child.
 * MME handles are process objects on one global list (no thread affinity): a handle opened by one thread is used
 * and closed by another, also after the opening thread has exited. VM_TIMEOUT: 300 */

#define NTHREADS 4

typedef struct {
    HANDLE go;            /* manual-reset start gate */
    int idx;
    DWORD res[8];         /* per-step results */
    ULONG_PTR h;
} worker_t;

static HANDLE start_thread(LPTHREAD_START_ROUTINE fn, void *arg) { return CreateThread(NULL, 0, fn, arg, 0, NULL); }
static int join(HANDLE t, DWORD ms) {
    DWORD w = WaitForSingleObject(t, ms);
    CloseHandle(t);
    return w == WAIT_OBJECT_0;
}

/* ---------------- mmio (always available) ---------------- */

static DWORD WINAPI mmio_open_thread(void *p) { worker_t *w = (worker_t *)p; w->h = (ULONG_PTR)mh_mmio_open(); return 0; }

/* Open in thread A (which exits), use and close in the main thread. */
TEST(mh_thread_mmio_open_a_close_b) {
    REQUIRE(U(mmioOpenA)); REQUIRE(U(mmioClose)); REQUIRE(U(mmioWrite)); REQUIRE(U(mmioSeek));
    worker_t w; memset(&w, 0, sizeof w);
    HANDLE t = start_thread(mmio_open_thread, &w);
    CHECK(t != NULL);
    if (!t) return;
    CHECK(join(t, 5000));
    CHECK(w.h != 0);
    if (!w.h) return;
    HMMIO h = (HMMIO)w.h;
    EXPECT_EQ(U(mmioWrite)(h, "xyz", 3), 3);
    EXPECT_EQ(U(mmioSeek)(h, 0, SEEK_CUR), 3);
    EXPECT_EQ(U(mmioClose)(h, 0), 0);
    EXPECT_EQ(U(mmioClose)(h, 0), MMSYSERR_INVALHANDLE);
}

static DWORD WINAPI mmio_close_thread(void *p) { worker_t *w = (worker_t *)p; w->res[0] = U(mmioClose)((HMMIO)w->h, 0);
    w->res[1] = U(mmioClose)((HMMIO)w->h, 0); return 0; }

/* Open in the main thread, close in a worker; the main thread then sees the handle as invalid. */
TEST(mh_thread_mmio_close_in_other_thread) {
    REQUIRE(U(mmioOpenA)); REQUIRE(U(mmioClose)); REQUIRE(U(mmioGetInfo));
    worker_t w; memset(&w, 0, sizeof w);
    w.h = (ULONG_PTR)mh_mmio_open();
    if (!w.h) { t_skip("mmio open failed"); return; }
    HANDLE t = start_thread(mmio_close_thread, &w);
    CHECK(t != NULL);
    if (!t) { U(mmioClose)((HMMIO)w.h, 0); return; }
    CHECK(join(t, 5000));
    EXPECT_EQ(w.res[0], 0);
    EXPECT_EQ(w.res[1], MMSYSERR_INVALHANDLE);
    MMIOINFO mi;
    EXPECT_EQ(U(mmioGetInfo)((HMMIO)w.h, &mi, 0), MMSYSERR_INVALHANDLE);
}

/* 4 threads x 200 open/write/seek/read/close cycles on private memory files at the same time. */
static DWORD WINAPI mmio_churn_thread(void *p) {
    worker_t *w = (worker_t *)p;
    char mem[64], buf[8];
    WaitForSingleObject(w->go, 5000);
    for (int i = 0; i < 200; i++) {
        MMIOINFO mi; memset(&mi, 0, sizeof mi);
        mi.fccIOProc = FOURCC_MEM; mi.pchBuffer = mem; mi.cchBuffer = sizeof mem;
        HMMIO h = U(mmioOpenA)(NULL, &mi, MMIO_READWRITE);
        if (!h) { w->res[0]++; continue; }
        char tag[4] = { 'T', (char)('0' + w->idx), (char)i, 0 };
        if (U(mmioWrite)(h, tag, 4) != 4) w->res[1]++;
        if (U(mmioSeek)(h, 0, SEEK_SET) != 0) w->res[2]++;
        if (U(mmioRead)(h, buf, 4) != 4 || memcmp(buf, tag, 4)) w->res[3]++;
        if (U(mmioClose)(h, 0) != 0) w->res[4]++;
        if (U(mmioClose)(h, 0) != MMSYSERR_INVALHANDLE) w->res[5]++;
    }
    return 0;
}
static DWORD WINAPI bad_validate_thread(void *p) {
    worker_t *w = (worker_t *)p;
    WaitForSingleObject(w->go, 5000);
    for (int i = 0; i < 2000; i++) {
        ULONG_PTR v = MH_BAD[i % MH_NBAD].v;
        if (U(mmioClose)((HMMIO)v, 0) != MMSYSERR_INVALHANDLE) w->res[0]++;
        if (U(waveOutClose)((HWAVEOUT)v) != MMSYSERR_INVALHANDLE) w->res[1]++;
        if (U(midiOutClose)((HMIDIOUT)v) != MMSYSERR_INVALHANDLE) w->res[2]++;
        if (U(mixerClose)((HMIXER)v) != MMSYSERR_INVALHANDLE) w->res[3]++;
    }
    return 0;
}
TEST(mh_thread_mmio_concurrent_churn) {
    REQUIRE(U(mmioOpenA)); REQUIRE(U(mmioClose)); REQUIRE(U(mmioWrite)); REQUIRE(U(mmioRead)); REQUIRE(U(mmioSeek));
    REQUIRE(U(waveOutClose)); REQUIRE(U(midiOutClose)); REQUIRE(U(mixerClose));
    worker_t w[NTHREADS + 1]; HANDLE t[NTHREADS + 1];
    HANDLE go = CreateEventA(NULL, TRUE, FALSE, NULL);
    for (int i = 0; i <= NTHREADS; i++) {
        memset(&w[i], 0, sizeof w[i]); w[i].go = go; w[i].idx = i;
        t[i] = start_thread(i < NTHREADS ? mmio_churn_thread : bad_validate_thread, &w[i]);
        CHECK(t[i] != NULL);
    }
    SetEvent(go);
    for (int i = 0; i <= NTHREADS; i++) if (t[i]) CHECKF(join(t[i], 30000), "thread %d did not finish", i);
    for (int i = 0; i < NTHREADS; i++)
        for (int k = 0; k < 6; k++) CHECKF(w[i].res[k] == 0, "churn thread %d step %d failed %lu times", i, k, w[i].res[k]);
    for (int k = 0; k < 4; k++) CHECKF(w[NTHREADS].res[k] == 0, "bad-value validator family %d: %lu non-5 results", k, w[NTHREADS].res[k]);
    CloseHandle(go);
}

/* Leak check: 200 mmio memory open/close cycles do not grow the process handle count. */
TEST(mh_leak_mmio_200_cycles) {
    REQUIRE(U(mmioOpenA)); REQUIRE(U(mmioClose));
    for (int i = 0; i < 5; i++) { HMMIO h = mh_mmio_open(); if (h) U(mmioClose)(h, 0); }
    DWORD before = 0, after = 0;
    CHECK(GetProcessHandleCount(GetCurrentProcess(), &before));
    int fails = 0;
    for (int i = 0; i < 200; i++) {
        HMMIO h = mh_mmio_open();
        if (!h) { fails++; continue; }
        if (U(mmioClose)(h, 0) != 0) fails++;
    }
    CHECK(GetProcessHandleCount(GetCurrentProcess(), &after));
    EXPECT_EQ(fails, 0);
    CHECKF(after <= before + 2, "handle count %lu -> %lu", before, after);
    t_note("mmio 200 cycles: process handles %lu -> %lu", before, after);
}

/* Leak check for file-backed mmio (a kernel file handle per open). */
TEST(mh_leak_mmio_file_100_cycles) {
    REQUIRE(U(mmioOpenA)); REQUIRE(U(mmioClose)); REQUIRE(U(mmioWrite));
    char path[MAX_PATH]; t_tmpfile(path, sizeof path, "leak.bin");
    HMMIO h = U(mmioOpenA)(path, NULL, MMIO_CREATE | MMIO_READWRITE);
    CHECK(h != NULL);
    if (!h) return;
    EXPECT_EQ(U(mmioWrite)(h, "data", 4), 4);
    EXPECT_EQ(U(mmioClose)(h, 0), 0);
    DWORD before = 0, after = 0;
    CHECK(GetProcessHandleCount(GetCurrentProcess(), &before));
    int fails = 0;
    for (int i = 0; i < 100; i++) {
        h = U(mmioOpenA)(path, NULL, MMIO_READ);
        if (!h) { fails++; continue; }
        if (U(mmioClose)(h, 0) != 0) fails++;
    }
    CHECK(GetProcessHandleCount(GetCurrentProcess(), &after));
    EXPECT_EQ(fails, 0);
    CHECKF(after <= before + 2, "handle count %lu -> %lu", before, after);
    DeleteFileA(path);
}

/* ---------------- device half: waveOut across threads ---------------- */

static DWORD WINAPI wo_open_thread(void *p) {
    worker_t *w = (worker_t *)p; HWAVEOUT h = NULL;
    w->res[0] = mh_waveout_open(&h, NULL);
    w->h = (ULONG_PTR)h;
    return 0;
}

TEST(mh_thread_waveout_open_a_use_close_b) {
    REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutClose)); REQUIRE(U(waveOutGetID)); REQUIRE(U(waveOutPrepareHeader));
    if (!mh_devs()->wo) {
        /* zero devices: the open fails in any thread, and the handle stays NULL */
        worker_t w; memset(&w, 0, sizeof w); w.h = 1;
        HANDLE t = start_thread(wo_open_thread, &w);
        if (t) { CHECK(join(t, 10000)); EXPECT_EQ(w.res[0], MMSYSERR_BADDEVICEID); EXPECT_EQ(w.h, 0); }
        return;
    }
    worker_t w; memset(&w, 0, sizeof w);
    HANDLE t = start_thread(wo_open_thread, &w);
    CHECK(t != NULL);
    if (!t) return;
    CHECK(join(t, 10000));                      /* the opening thread has exited */
    EXPECT_EQ(w.res[0], 0);
    if (!w.h) return;
    HWAVEOUT h = (HWAVEOUT)w.h;
    UINT id = 0;
    EXPECT_EQ(U(waveOutGetID)(h, &id), 0);
    EXPECT_EQ(id, WAVE_MAPPER);
    EXPECT_EQ(mh_play_silence(h, 50, 3000), 0);
    EXPECT_EQ(U(waveOutClose)(h), 0);
    EXPECT_EQ(U(waveOutClose)(h), MMSYSERR_INVALHANDLE);
}

static DWORD WINAPI wo_use_close_thread(void *p) {
    worker_t *w = (worker_t *)p; HWAVEOUT h = (HWAVEOUT)w->h; UINT id = 0;
    w->res[0] = U(waveOutGetID)(h, &id);
    w->res[1] = mh_play_silence(h, 50, 3000);
    w->res[2] = U(waveOutClose)(h);
    w->res[3] = U(waveOutClose)(h);
    return 0;
}
TEST(mh_thread_waveout_open_main_close_worker) {
    REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutClose)); REQUIRE(U(waveOutGetID));
    if (!mh_devs()->wo) { t_note("no waveOut device"); return; }
    worker_t w; memset(&w, 0, sizeof w);
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    HWAVEOUT h = NULL;
    EXPECT_EQ(mh_waveout_open(&h, ev), 0);     /* CALLBACK_EVENT owned by the main thread */
    if (!h) { CloseHandle(ev); return; }
    w.h = (ULONG_PTR)h;
    HANDLE t = start_thread(wo_use_close_thread, &w);
    CHECK(t != NULL);
    if (!t) { U(waveOutClose)(h); CloseHandle(ev); return; }
    CHECK(join(t, 10000));
    EXPECT_EQ(w.res[0], 0);
    EXPECT_EQ(w.res[1], 0);
    EXPECT_EQ(w.res[2], 0);
    EXPECT_EQ(w.res[3], MMSYSERR_INVALHANDLE);
    UINT id;
    EXPECT_EQ(U(waveOutGetID)(h, &id), MMSYSERR_INVALHANDLE);
    CloseHandle(ev);
}

/* 4 threads open / prepare / write / wait / unprepare / close concurrently (shared mode: all succeed). */
static DWORD WINAPI wo_full_cycle_thread(void *p) {
    worker_t *w = (worker_t *)p; HWAVEOUT h = NULL;
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    WaitForSingleObject(w->go, 5000);
    w->res[0] = mh_waveout_open(&h, ev);
    if (!w->res[0]) {
        w->res[1] = mh_play_silence(h, 100, 4000);
        w->res[2] = U(waveOutClose)(h);
    }
    CloseHandle(ev);
    return 0;
}
TEST(mh_thread_waveout_four_concurrent) {
    REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutClose));
    if (!mh_devs()->wo) { t_note("no waveOut device"); return; }
    worker_t w[NTHREADS]; HANDLE t[NTHREADS];
    HANDLE go = CreateEventA(NULL, TRUE, FALSE, NULL);
    for (int i = 0; i < NTHREADS; i++) { memset(&w[i], 0, sizeof w[i]); w[i].go = go; w[i].idx = i;
        t[i] = start_thread(wo_full_cycle_thread, &w[i]); CHECK(t[i] != NULL); }
    SetEvent(go);
    for (int i = 0; i < NTHREADS; i++) if (t[i]) CHECKF(join(t[i], 20000), "thread %d stuck", i);
    for (int i = 0; i < NTHREADS; i++) {
        CHECKF(w[i].res[0] == 0, "thread %d open -> %lu", i, w[i].res[0]);
        CHECKF(w[i].res[1] == 0, "thread %d play -> 0x%lx", i, w[i].res[1]);
        CHECKF(w[i].res[2] == 0, "thread %d close -> %lu", i, w[i].res[2]);
    }
    CloseHandle(go);
}

/* One thread polls waveOutGetPosition while another closes the handle: only 0 (before), 5 (after) or 12
 * (HANDLEBUSY: the close has marked the handle busy and the poll got the per-handle lock in between) occur. */
typedef struct { HWAVEOUT h; volatile LONG stop; DWORD n0, n5, n12, nother, last_other, polls; } poll_t;
static DWORD WINAPI poll_pos_thread(void *p) {
    poll_t *s = (poll_t *)p;
    while (!s->stop && s->polls < 2000000) {
        MMTIME mt; mt.wType = TIME_BYTES;
        MMRESULT r = U(waveOutGetPosition)(s->h, &mt, sizeof mt);
        s->polls++;
        if (r == 0) s->n0++; else if (r == 5) s->n5++; else if (r == 12) s->n12++; else { s->nother++; s->last_other = r; }
        if (r == 5 && s->n5 > 200) break;   /* well past the close */
    }
    return 0;
}
TEST(mh_thread_waveout_close_while_polling) {
    REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutClose)); REQUIRE(U(waveOutGetPosition));
    if (!mh_devs()->wo) { t_note("no waveOut device"); return; }
    for (int round = 0; round < 3; round++) {
        poll_t s; memset(&s, 0, sizeof s);
        EXPECT_EQ(mh_waveout_open(&s.h, NULL), 0);
        if (!s.h) return;
        HANDLE t = start_thread(poll_pos_thread, &s);
        CHECK(t != NULL);
        if (!t) { U(waveOutClose)(s.h); return; }
        Sleep(30);
        EXPECT_EQ(U(waveOutClose)(s.h), 0);
        Sleep(30);
        s.stop = 1;
        CHECK(join(t, 5000));
        EXPECT_EQ(s.nother, 0);
        CHECK(s.n0 > 0);   /* polls before the close saw a live handle */
        CHECK(s.n5 > 0);   /* and after it an invalid one */
        t_note("round %d: polls=%lu ok=%lu invalhandle=%lu busy=%lu other=%lu(last %lu)", round, s.polls, s.n0, s.n5,
               s.n12, s.nother, s.last_other);
    }
}

/* waveOutReset from another thread while a buffer plays: 0, and the header comes back DONE, not INQUEUE. */
typedef struct { HWAVEOUT h; DWORD r; } reset_t;
static DWORD WINAPI reset_thread(void *p) { reset_t *s = (reset_t *)p; s->r = U(waveOutReset)(s->h); return 0; }
TEST(mh_thread_waveout_reset_from_other_thread) {
    REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutClose)); REQUIRE(U(waveOutReset)); REQUIRE(U(waveOutPrepareHeader));
    REQUIRE(U(waveOutWrite)); REQUIRE(U(waveOutUnprepareHeader)); REQUIRE(U(waveOutGetPosition));
    if (!mh_devs()->wo) { t_note("no waveOut device"); return; }
    HWAVEOUT h = NULL;
    EXPECT_EQ(mh_waveout_open(&h, NULL), 0);
    if (!h) return;
    static char silence[44100 * 4 * 2];   /* 2 s */
    WAVEHDR wh; memset(&wh, 0, sizeof wh); wh.lpData = silence; wh.dwBufferLength = sizeof silence;
    EXPECT_EQ(U(waveOutPrepareHeader)(h, &wh, sizeof wh), 0);
    EXPECT_EQ(U(waveOutWrite)(h, &wh, sizeof wh), 0);
    Sleep(150);
    CHECKF(wh.dwFlags & WHDR_INQUEUE, "flags 0x%lx while playing", wh.dwFlags);
    reset_t s = { h, 0xFFFF };
    HANDLE t = start_thread(reset_thread, &s);
    CHECK(t != NULL);
    if (t) CHECK(join(t, 5000)); else s.r = U(waveOutReset)(h);
    EXPECT_EQ(s.r, 0);
    DWORD t0 = GetTickCount();
    while (!(wh.dwFlags & WHDR_DONE) && GetTickCount() - t0 < 1000) Sleep(5);
    EXPECT_EQ(wh.dwFlags, WHDR_DONE | WHDR_PREPARED);
    MMTIME mt; mt.wType = TIME_BYTES;
    EXPECT_EQ(U(waveOutGetPosition)(h, &mt, sizeof mt), 0);
    t_note("position after cross-thread reset: type %u bytes %lu", mt.wType, mt.u.cb);
    EXPECT_EQ(U(waveOutUnprepareHeader)(h, &wh, sizeof wh), 0);
    EXPECT_EQ(U(waveOutClose)(h), 0);
}

/* Pause in one thread, restart in another; the buffer completes. */
typedef struct { HWAVEOUT h; DWORD r; } pr_t;
static DWORD WINAPI pause_thread(void *p) { pr_t *s = (pr_t *)p; s->r = U(waveOutPause)(s->h); return 0; }
TEST(mh_thread_waveout_pause_restart_split) {
    REQUIRE(U(waveOutPause)); REQUIRE(U(waveOutRestart)); REQUIRE(U(waveOutWrite)); REQUIRE(U(waveOutPrepareHeader));
    if (!mh_devs()->wo) { t_note("no waveOut device"); return; }
    HWAVEOUT h = NULL;
    EXPECT_EQ(mh_waveout_open(&h, NULL), 0);
    if (!h) return;
    static char silence[44100 * 4 / 5];   /* 200 ms */
    WAVEHDR wh; memset(&wh, 0, sizeof wh); wh.lpData = silence; wh.dwBufferLength = sizeof silence;
    pr_t s = { h, 0xFFFF };
    HANDLE t = start_thread(pause_thread, &s);
    if (t) CHECK(join(t, 5000));
    EXPECT_EQ(s.r, 0);
    EXPECT_EQ(U(waveOutPrepareHeader)(h, &wh, sizeof wh), 0);
    EXPECT_EQ(U(waveOutWrite)(h, &wh, sizeof wh), 0);
    Sleep(400);
    CHECKF(!(wh.dwFlags & WHDR_DONE), "paused buffer completed: flags 0x%lx", wh.dwFlags);
    EXPECT_EQ(U(waveOutRestart)(h), 0);
    DWORD t0 = GetTickCount();
    while (!(wh.dwFlags & WHDR_DONE) && GetTickCount() - t0 < 3000) Sleep(5);
    EXPECT_EQ(wh.dwFlags, WHDR_DONE | WHDR_PREPARED);
    if (!(wh.dwFlags & WHDR_DONE)) U(waveOutReset)(h);
    EXPECT_EQ(U(waveOutUnprepareHeader)(h, &wh, sizeof wh), 0);
    EXPECT_EQ(U(waveOutClose)(h), 0);
}

/* Leak check: 50 waveOut open/close cycles (after warm-up). */
TEST(mh_leak_waveout_50_cycles) {
    REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutClose));
    if (!mh_devs()->wo) {
        HWAVEOUT h;
        for (int i = 0; i < 50; i++) { h = (HWAVEOUT)1; EXPECT_EQ(mh_waveout_open(&h, NULL), MMSYSERR_BADDEVICEID); }
        return;
    }
    for (int i = 0; i < 3; i++) { HWAVEOUT h = NULL; if (!mh_waveout_open(&h, NULL)) U(waveOutClose)(h); }
    DWORD before = 0, after = 0; int fails = 0;
    CHECK(GetProcessHandleCount(GetCurrentProcess(), &before));
    for (int i = 0; i < 50; i++) {
        HWAVEOUT h = NULL;
        if (mh_waveout_open(&h, NULL)) { fails++; continue; }
        if (U(waveOutClose)(h)) fails++;
    }
    Sleep(200);
    CHECK(GetProcessHandleCount(GetCurrentProcess(), &after));
    EXPECT_EQ(fails, 0);
    CHECKF(after <= before + 16, "handle count %lu -> %lu", before, after);
    t_note("waveOut 50 cycles: process handles %lu -> %lu", before, after);
}

TEST(mh_leak_mixer_wavein_midiout_cycles) {
    REQUIRE(U(mixerOpen)); REQUIRE(U(mixerClose)); REQUIRE(U(waveInOpen)); REQUIRE(U(waveInClose));
    REQUIRE(U(midiOutOpen)); REQUIRE(U(midiOutClose));
    const mh_counts *c = mh_devs();
    struct { const char *name; int avail, n; } fam[3] = { { "mixer", c->mx > 0, 50 }, { "waveIn", c->wi > 0, 20 },
                                                          { "midiOut", c->wo > 0 && c->mo > 0, 20 } };
    for (int f = 0; f < 3; f++) {
        if (!fam[f].avail) { t_note("%s: no device", fam[f].name); continue; }
        DWORD before = 0, after = 0; int fails = 0;
        for (int i = -3; i < fam[f].n; i++) {
            if (i == 0) GetProcessHandleCount(GetCurrentProcess(), &before);
            MMRESULT r1 = 1, r2 = 1;
            if (f == 0) { HMIXER h = NULL; r1 = U(mixerOpen)(&h, 0, 0, 0, 0); if (!r1) r2 = U(mixerClose)(h); }
            if (f == 1) { HWAVEIN h = NULL; r1 = mh_wavein_open(&h); if (!r1) r2 = U(waveInClose)(h); }
            if (f == 2) { HMIDIOUT h = NULL; r1 = U(midiOutOpen)(&h, 0, 0, 0, CALLBACK_NULL); if (!r1) r2 = U(midiOutClose)(h); }
            if (i >= 0 && (r1 || r2)) fails++;
        }
        Sleep(100);
        GetProcessHandleCount(GetCurrentProcess(), &after);
        CHECKF(fails == 0, "%s: %d failed cycles", fam[f].name, fails);
        CHECKF(after <= before + 16, "%s: handle count %lu -> %lu", fam[f].name, before, after);
        t_note("%s %d cycles: process handles %lu -> %lu", fam[f].name, fam[f].n, before, after);
    }
}

/* ---------------- first-use race in a fresh process ---------------- */

typedef UINT (WINAPI *numdevs_fn)(void);
typedef struct { HANDLE go; numdevs_fn fn; UINT n; } race_t;
static DWORD WINAPI race_thread(void *p) { race_t *r = (race_t *)p; WaitForSingleObject(r->go, 20000); r->n = r->fn(); return 0; }

static numdevs_fn numdevs_by_name(const char *fam) {
    if (!strcmp(fam, "waveout")) return (numdevs_fn)t_proc_in("winmm.dll", "waveOutGetNumDevs");
    if (!strcmp(fam, "wavein")) return (numdevs_fn)t_proc_in("winmm.dll", "waveInGetNumDevs");
    if (!strcmp(fam, "midiout")) return (numdevs_fn)t_proc_in("winmm.dll", "midiOutGetNumDevs");
    if (!strcmp(fam, "mixer")) return (numdevs_fn)t_proc_in("winmm.dll", "mixerGetNumDevs");
    if (!strcmp(fam, "aux")) return (numdevs_fn)t_proc_in("winmm.dll", "auxGetNumDevs");
    return NULL;
}

/* exe --child numdevs_race <family>: 4 threads make the process's first device-count call at the same moment.
 * Exit code 100 + count when all agree, 1 when they disagree, 2 on setup errors. */
CHILD(numdevs_race) {
    if (argc < 2) return 2;
    numdevs_fn fn = numdevs_by_name(argv[1]);
    if (!fn) return 2;
    race_t r[NTHREADS]; HANDLE t[NTHREADS];
    HANDLE go = CreateEventA(NULL, TRUE, FALSE, NULL);
    for (int i = 0; i < NTHREADS; i++) { r[i].go = go; r[i].fn = fn; r[i].n = 0xFFFF; t[i] = CreateThread(NULL, 0, race_thread, &r[i], 0, NULL);
        if (!t[i]) return 2; }
    Sleep(50);
    SetEvent(go);
    for (int i = 0; i < NTHREADS; i++) { if (WaitForSingleObject(t[i], 150000) != WAIT_OBJECT_0) return 3; CloseHandle(t[i]); }
    for (int i = 1; i < NTHREADS; i++) if (r[i].n != r[0].n) {
        printf("race %s: thread0=%u thread%d=%u\n", argv[1], r[0].n, i, r[i].n); return 1;
    }
    if (fn() != r[0].n) return 1;
    return 100 + (int)r[0].n;
}

TEST(mh_race_first_numdevs_child) {
    REQUIRE(U(waveOutGetNumDevs)); REQUIRE(U(auxGetNumDevs));
    const mh_counts *c = mh_devs();
    UINT aux = U(auxGetNumDevs)();
    struct { const char *fam; UINT want; } cases[] = {
        { "waveout", c->wo }, { "wavein", c->wi }, { "midiout", c->mo }, { "mixer", c->mx }, { "aux", aux },
    };
    for (int i = 0; i < 5; i++) {
        DWORD code = t_run_child("numdevs_race", cases[i].fam, 170000);
        CHECKF(code == 100 + cases[i].want, "%s: child exit 0x%lx, want %u (100 + count %u)", cases[i].fam, code,
               100 + cases[i].want, cases[i].want);
    }
}

/* ---------------- handle values ---------------- */

/* Live handles of every kind are pointer-sized values above 0xFFFF and distinct from each other. */
TEST(mh_handle_values_distinct) {
    mh_zoo z; mh_zoo_open(&z);
    ULONG_PTR v[] = { (ULONG_PTR)z.mmio, (ULONG_PTR)z.wo, (ULONG_PTR)z.wi, (ULONG_PTR)z.mx, (ULONG_PTR)z.mo };
    for (int i = 0; i < 5; i++) {
        if (!v[i]) continue;
        CHECKF(v[i] > 0xFFFF, "handle %d = 0x%llx", i, (unsigned long long)v[i]);
        CHECKF((v[i] & 7) == 0, "handle %d = 0x%llx not 8-aligned", i, (unsigned long long)v[i]);
        for (int k = i + 1; k < 5; k++) if (v[k]) CHECK(v[i] != v[k]);
    }
    mh_zoo_close(&z);
}
