#include "ee.h"
/* Device counts: the seven *GetNumDevs exports.
 *
 * winmm!waveOutGetNumDevs and friends are jmp stubs into winmmbase; winmmbase runs ClientUpdatePnpInfo (first call in
 * the process waits for AudioSrv and builds the device lists, racing callers block on an init event) and then returns
 * the cached counter under NumDevsCritSec. joyGetNumDevs is a local winmm body that returns the constant 16.
 * Invariants checked here hold for every audio configuration of the VM (noaudio / hda / hda-out): the GS synth makes
 * midiOut >= 1, one mixer exists per endpoint (= waveOut + waveIn), joy is 16, and each count equals the number of
 * ids for which <family>GetDevCaps succeeds. Exact numbers are only recorded with t_note.
 * VM_TIMEOUT: 120 */

/* Records the configuration in the results file (first test in this file). */
TEST(enum_counts_record_environment) {
    REQUIRE(U(waveOutGetNumDevs)); REQUIRE(U(waveInGetNumDevs)); REQUIRE(U(midiOutGetNumDevs));
    REQUIRE(U(midiInGetNumDevs)); REQUIRE(U(mixerGetNumDevs)); REQUIRE(U(auxGetNumDevs)); REQUIRE(U(joyGetNumDevs));
    UINT wo = U(waveOutGetNumDevs)(), wi = U(waveInGetNumDevs)(), mo = U(midiOutGetNumDevs)(), mi = U(midiInGetNumDevs)();
    UINT mx = U(mixerGetNumDevs)(), ax = U(auxGetNumDevs)(), jy = U(joyGetNumDevs)();
    t_note("COUNTS waveout=%u wavein=%u midiout=%u midiin=%u mixer=%u aux=%u joy=%u", wo, wi, mo, mi, mx, ax, jy);
    /* Sanity: counts are small (device lists, not garbage). */
    CHECKF(wo < 64 && wi < 64 && mo < 64 && mi < 64 && mx < 128 && ax < 64, "%u %u %u %u %u %u", wo, wi, mo, mi, mx, ax);
}

/* ---- joyGetNumDevs: constant 16 [decomp: winmm!0x6330 mov eax,0x10] ---------------------------------------- */
TEST(enum_joy_numdevs_is_16) {
    REQUIRE(U(joyGetNumDevs));
    EXPECT_EQ(U(joyGetNumDevs)(), 16);
}
TEST(enum_joy_numdevs_constant_1000_calls) {
    REQUIRE(U(joyGetNumDevs));
    int bad = 0;
    for (int i = 0; i < 1000; i++) if (U(joyGetNumDevs)() != 16) bad++;
    EXPECT_EQ(bad, 0);
}
TEST(enum_joy_numdevs_preserves_last_error) {
    REQUIRE(U(joyGetNumDevs));
    /* A constant-return body touches nothing: the thread's last error survives. */
    SetLastError(0x13572468);
    EXPECT_EQ(U(joyGetNumDevs)(), 16);
    EXPECT_EQ(GetLastError(), 0x13572468);
}
TEST(enum_joy_numdevs_only_in_winmm) {
    REQUIRE(U(joyGetNumDevs));
    /* The joystick family is not part of winmmbase's export table. */
    EXPECT_EQ(t_proc_in("winmmbase.dll", "joyGetNumDevs"), NULL);
    EXPECT_NE(t_proc_in("winmm.dll", "joyGetNumDevs"), NULL);
}
TEST(enum_joy_numdevs_independent_of_audio) {
    REQUIRE(U(joyGetNumDevs)); REQUIRE(U(waveOutGetNumDevs));
    UINT before = U(joyGetNumDevs)();
    (void)U(waveOutGetNumDevs)();
    EXPECT_EQ(U(joyGetNumDevs)(), before);
    EXPECT_EQ(before, 16);
}

/* ---- per-family invariants ------------------------------------------------------------------------------- */
TEST(enum_midiout_at_least_one_gs_synth) {
    REQUIRE(U(midiOutGetNumDevs));
    /* Microsoft GS Wavetable Synth is a software device: enumerated with and without audio hardware [VM]. */
    UINT n = U(midiOutGetNumDevs)();
    CHECKF(n >= 1, "midiOut %u", n);
}
TEST(enum_aux_count_recorded) {
    REQUIRE(U(auxGetNumDevs));
    UINT n = U(auxGetNumDevs)();
    if (n != 0) t_note("aux devices present: %u (0 on every VM configuration observed)", n);
    else t_note("aux=0");
    AUXCAPSW c;
    EXPECT_EQ(U(auxGetDevCapsW)(n, &c, sizeof c), MMSYSERR_BADDEVICEID);
}
TEST(enum_midiin_count_recorded) {
    REQUIRE(U(midiInGetNumDevs));
    UINT n = U(midiInGetNumDevs)();
    t_note("midiIn=%u", n);
    MIDIINCAPSW c;
    EXPECT_EQ(U(midiInGetDevCapsW)(n, &c, sizeof c), MMSYSERR_BADDEVICEID);
}
TEST(enum_mixer_count_is_wave_sum) {
    REQUIRE(U(mixerGetNumDevs)); REQUIRE(U(waveOutGetNumDevs)); REQUIRE(U(waveInGetNumDevs));
    /* One mixer per endpoint, one wave device per endpoint: mixer == waveOut + waveIn [VM: 0=0+0, 2=1+1, 1=1+0]. */
    UINT mx = U(mixerGetNumDevs)(), wo = U(waveOutGetNumDevs)(), wi = U(waveInGetNumDevs)();
    EXPECT_EQ(mx, wo + wi);
}
TEST(enum_mixer_zero_iff_no_wave) {
    REQUIRE(U(mixerGetNumDevs)); REQUIRE(U(waveOutGetNumDevs)); REQUIRE(U(waveInGetNumDevs));
    UINT mx = U(mixerGetNumDevs)(), w = U(waveOutGetNumDevs)() + U(waveInGetNumDevs)();
    EXPECT_EQ(mx == 0, w == 0);
}

/* ---- stability: the cached counter does not change between calls (no PnP event during the test) ------------ */
#define STABLE_TEST(fam, fn)                                                                                     \
    TEST(enum_##fn##_stable_100_calls) {                                                                          \
        REQUIRE(U(fn));                                                                                           \
        UINT first = U(fn)(), bad = 0;                                                                            \
        for (int i = 0; i < 100; i++) if (U(fn)() != first) bad++;                                               \
        EXPECT_EQ(bad, 0);                                                                                        \
        EXPECT_EQ(ee_numdevs(fam), first);                                                                        \
    }
STABLE_TEST(F_WAVEOUT, waveOutGetNumDevs)
STABLE_TEST(F_WAVEIN, waveInGetNumDevs)
STABLE_TEST(F_MIDIOUT, midiOutGetNumDevs)
STABLE_TEST(F_MIDIIN, midiInGetNumDevs)
STABLE_TEST(F_MIXER, mixerGetNumDevs)
STABLE_TEST(F_AUX, auxGetNumDevs)

/* Same value through winmm's stub and winmmbase's body. */
#define BASE_TEST(fam, fn)                                                                                       \
    TEST(enum_##fn##_winmm_eq_winmmbase) {                                                                        \
        REQUIRE(U(fn));                                                                                           \
        __typeof__(&fn) b = UIN("winmmbase.dll", fn);                                                             \
        REQUIRE(b);                                                                                               \
        EXPECT_NE((void *)b, (void *)U(fn));                                                                      \
        EXPECT_EQ(b(), U(fn)());                                                                                  \
        EXPECT_EQ(ee_numdevs_base(fam), ee_numdevs(fam));                                                         \
    }
BASE_TEST(F_WAVEOUT, waveOutGetNumDevs)
BASE_TEST(F_WAVEIN, waveInGetNumDevs)
BASE_TEST(F_MIDIOUT, midiOutGetNumDevs)
BASE_TEST(F_MIDIIN, midiInGetNumDevs)
BASE_TEST(F_MIXER, mixerGetNumDevs)
BASE_TEST(F_AUX, auxGetNumDevs)

/* GetNumDevs ignores the caller's last-error value: on success paths nothing should be left behind that a caller
 * could misread. Recorded only (ClientUpdatePnpInfo may call APIs that set it). */
#define LASTERR_TEST(fn)                                                                                         \
    TEST(enum_##fn##_last_error_recorded) {                                                                       \
        REQUIRE(U(fn));                                                                                           \
        SetLastError(0x2468ACE0);                                                                                 \
        (void)U(fn)();                                                                                            \
        DWORD e = GetLastError();                                                                                 \
        if (e != 0x2468ACE0) t_note(#fn " changed the last error to %lu", e);                                    \
    }
LASTERR_TEST(waveOutGetNumDevs)
LASTERR_TEST(waveInGetNumDevs)
LASTERR_TEST(midiOutGetNumDevs)
LASTERR_TEST(mixerGetNumDevs)

/* ---- count == number of ids for which GetDevCaps succeeds, both A and W, every family ----------------------- */
static void ids_match_caps(int fam, int wide) {
    UINT n = ee_numdevs(fam);
    if (n == ~0u) { t_skip("missing %sGetNumDevs", ee_family_name[fam]); return; }
    union { WAVEOUTCAPS2W w; MIDIOUTCAPS2W m; MIXERCAPS2W x; AUXCAPS2W a; unsigned char raw[512]; } u;
    UINT cb = ee_caps_size(fam, wide);
    for (UINT i = 0; i < n; i++) {
        memset(&u, 0, sizeof u);
        MMRESULT r = ee_caps(fam, wide, i, &u, cb);
        CHECKF(r == MMSYSERR_NOERROR, "%sGetDevCaps%c(%u) of %u -> %u", ee_family_name[fam], wide ? 'W' : 'A', i, n, r);
        /* Every enumerated device has a non-empty name. */
        if (!r) CHECKF(wide ? ((WCHAR *)(u.raw + EE_PNAME_OFF))[0] != 0 : ((char *)(u.raw + EE_PNAME_OFF))[0] != 0,
                       "%s %u empty name", ee_family_name[fam], i);
    }
    static const UINT past[] = { 0, 1, 2, 7, 100 };
    for (size_t k = 0; k < sizeof past / sizeof past[0]; k++) {
        memset(&u, 0x5A, sizeof u);
        MMRESULT r = ee_caps(fam, wide, n + past[k], &u, cb);
        CHECKF(r == MMSYSERR_BADDEVICEID, "%sGetDevCaps%c(%u) with %u devices -> %u", ee_family_name[fam], wide ? 'W' : 'A',
               n + past[k], n, r);
        CHECKF(ee_all(&u, sizeof u, 0x5A), "%s out-of-range caps wrote the buffer", ee_family_name[fam]);
    }
}
#define IDS_TEST(fam, tag, wide) TEST(enum_ids_match_caps_##tag) { ids_match_caps(fam, wide); }
IDS_TEST(F_WAVEOUT, waveout_a, 0)
IDS_TEST(F_WAVEOUT, waveout_w, 1)
IDS_TEST(F_WAVEIN, wavein_a, 0)
IDS_TEST(F_WAVEIN, wavein_w, 1)
IDS_TEST(F_MIDIOUT, midiout_a, 0)
IDS_TEST(F_MIDIOUT, midiout_w, 1)
IDS_TEST(F_MIDIIN, midiin_a, 0)
IDS_TEST(F_MIDIIN, midiin_w, 1)
IDS_TEST(F_MIXER, mixer_a, 0)
IDS_TEST(F_MIXER, mixer_w, 1)
IDS_TEST(F_AUX, aux_a, 0)
IDS_TEST(F_AUX, aux_w, 1)

/* The direct exports agree with the table-driven helper for the primary waveOut functions. */
TEST(enum_ids_match_caps_waveout_direct) {
    REQUIRE(U(waveOutGetNumDevs)); REQUIRE(U(waveOutGetDevCapsA)); REQUIRE(U(waveOutGetDevCapsW));
    UINT n = U(waveOutGetNumDevs)();
    WAVEOUTCAPSA a; WAVEOUTCAPSW w;
    for (UINT i = 0; i < n; i++) {
        EXPECT_EQ(U(waveOutGetDevCapsA)(i, &a, sizeof a), MMSYSERR_NOERROR);
        EXPECT_EQ(U(waveOutGetDevCapsW)(i, &w, sizeof w), MMSYSERR_NOERROR);
    }
    EXPECT_EQ(U(waveOutGetDevCapsA)(n, &a, sizeof a), MMSYSERR_BADDEVICEID);
    EXPECT_EQ(U(waveOutGetDevCapsW)(n, &w, sizeof w), MMSYSERR_BADDEVICEID);
}

/* Each family's highest valid id and the first invalid one through the message interface (WODM/WIDM/MODM/MIDM
 * GETDEVCAPS go through the same waveIDMessage/midiIDMessage range check). */
TEST(enum_message_range_matches_count) {
    static const struct { int fam; UINT msg; } t[] = {
        { F_WAVEOUT, WODM_GETDEVCAPS }, { F_WAVEIN, WIDM_GETDEVCAPS }, { F_MIDIOUT, MODM_GETDEVCAPS }, { F_MIDIIN, MIDM_GETDEVCAPS },
    };
    for (size_t k = 0; k < sizeof t / sizeof t[0]; k++) {
        UINT n = ee_numdevs(t[k].fam);
        unsigned char buf[512];
        MMRESULT r = ee_msg(t[k].fam, n, t[k].msg, (DWORD_PTR)buf, sizeof buf);
        CHECKF(r == MMSYSERR_BADDEVICEID, "%sMessage(%u, GETDEVCAPS) -> %u", ee_family_name[t[k].fam], n, r);
        r = ee_msg(t[k].fam, n + 1000, t[k].msg, (DWORD_PTR)buf, sizeof buf);
        CHECKF(r == MMSYSERR_BADDEVICEID, "%sMessage(%u+1000) -> %u", ee_family_name[t[k].fam], n, r);
    }
}

/* ---- threads: concurrent GetNumDevs from 4 threads return the same value --------------------------------- */
typedef struct { HANDLE go; UINT expect[F_COUNT]; UINT joy; volatile LONG bad; int iters; } thr_ctx;
static DWORD WINAPI count_thread(LPVOID p) {
    thr_ctx *c = (thr_ctx *)p;
    WaitForSingleObject(c->go, 10000);
    for (int i = 0; i < c->iters; i++) {
        for (int f = 0; f < F_COUNT; f++) if (ee_numdevs(f) != c->expect[f]) InterlockedIncrement(&c->bad);
        UINT (WINAPI *j)(void) = (UINT (WINAPI *)(void))t_proc_in("winmm.dll", "joyGetNumDevs");
        if (j && j() != c->joy) InterlockedIncrement(&c->bad);
    }
    return 0;
}
static void run_threads(int nthreads, int iters) {
    thr_ctx c; memset(&c, 0, sizeof c);
    ee_counts s; ee_snapshot(&s);
    memcpy(c.expect, s.n, sizeof c.expect); c.joy = s.joy; c.iters = iters;
    c.go = CreateEventA(NULL, TRUE, FALSE, NULL);
    HANDLE th[16];
    for (int i = 0; i < nthreads; i++) th[i] = CreateThread(NULL, 0, count_thread, &c, 0, NULL);
    SetEvent(c.go);
    DWORD w = WaitForMultipleObjects(nthreads, th, TRUE, 30000);
    EXPECT_EQ(w, WAIT_OBJECT_0);
    for (int i = 0; i < nthreads; i++) CloseHandle(th[i]);
    CloseHandle(c.go);
    EXPECT_EQ(c.bad, 0);
}
TEST(enum_counts_4_threads_concurrent) {
    REQUIRE(U(waveOutGetNumDevs)); REQUIRE(U(mixerGetNumDevs));
    run_threads(4, 200);
}
TEST(enum_counts_8_threads_concurrent) {
    REQUIRE(U(waveOutGetNumDevs));
    run_threads(8, 50);
}

/* ---- fresh process: the very first winmm call happens on 4 threads at once ------------------------------- */
/* Child: LoadLibrary winmm (no winmm call yet), release 4 threads at once, each makes GetNumDevs its first call.
 * Exit code: 0 if all threads agree on every family, 0x100 | family on disagreement, 0x200 on wait failure.
 * argv[1] (optional): the parent's counts "wo,wi,mo,mi,mx,ax" to compare with (0x300 | family on mismatch). */
typedef struct { HANDLE go; UINT got[F_COUNT]; } first_ctx;
static DWORD WINAPI first_thread(LPVOID p) {
    first_ctx *c = (first_ctx *)p;
    WaitForSingleObject(c->go, 10000);
    /* Families in a thread-dependent order so different initial exports race. */
    int start = (int)(GetCurrentThreadId() % F_COUNT);
    for (int k = 0; k < F_COUNT; k++) { int f = (start + k) % F_COUNT; c->got[f] = ee_numdevs(f); }
    return 0;
}
CHILD(first_call_race) {
    if (!LoadLibraryA("winmm.dll")) return 0x400;
    first_ctx c[4]; HANDLE th[4];
    HANDLE go = CreateEventA(NULL, TRUE, FALSE, NULL);
    for (int i = 0; i < 4; i++) { memset(&c[i], 0, sizeof c[i]); c[i].go = go; th[i] = CreateThread(NULL, 0, first_thread, &c[i], 0, NULL); }
    Sleep(50);
    SetEvent(go);
    if (WaitForMultipleObjects(4, th, TRUE, 60000) != WAIT_OBJECT_0) return 0x200;
    for (int i = 1; i < 4; i++)
        for (int f = 0; f < F_COUNT; f++) if (c[i].got[f] != c[0].got[f]) return 0x100 | f;
    if (argc > 1) {
        UINT p[F_COUNT] = { 0 };
        if (sscanf(argv[1], "%u,%u,%u,%u,%u,%u", &p[0], &p[1], &p[2], &p[3], &p[4], &p[5]) == 6)
            for (int f = 0; f < F_COUNT; f++) if (p[f] != c[0].got[f]) return 0x300 | f;
    }
    return 0;
}
TEST(enum_counts_first_call_race_in_fresh_process) {
    REQUIRE(U(waveOutGetNumDevs));
    ee_counts s; ee_snapshot(&s);
    char args[128];
    snprintf(args, sizeof args, "%u,%u,%u,%u,%u,%u", s.n[0], s.n[1], s.n[2], s.n[3], s.n[4], s.n[5]);
    DWORD code = t_run_child("first_call_race", args, 90000);
    EXPECT_EQ(code, 0);
}
TEST(enum_counts_first_call_race_repeated) {
    REQUIRE(U(waveOutGetNumDevs));
    ee_counts s; ee_snapshot(&s);
    char args[128];
    snprintf(args, sizeof args, "%u,%u,%u,%u,%u,%u", s.n[0], s.n[1], s.n[2], s.n[3], s.n[4], s.n[5]);
    for (int i = 0; i < 3; i++) {
        DWORD code = t_run_child("first_call_race", args, 90000);
        CHECKF(code == 0, "run %d exit 0x%lx", i, code);
    }
}

/* Child: each family's count in a process whose first winmm call is that family (exit code = count, or 0xFFFF). */
CHILD(count_one) {
    int fam = argc > 1 ? atoi(argv[1]) : 0;
    if (fam < 0 || fam >= F_COUNT) return 0xFFFF;
    UINT n = ee_numdevs(fam);
    return n == ~0u ? 0xFFFF : (int)n;
}
static void child_count(int fam) {
    UINT parent = ee_numdevs(fam);
    char a[16]; snprintf(a, sizeof a, "%d", fam);
    DWORD code = t_run_child("count_one", a, 90000);
    CHECKF(code == parent, "%s child %lu parent %u", ee_family_name[fam], code, parent);
}
#define CHILD_TEST(fam, tag) TEST(enum_counts_child_first_call_##tag) { REQUIRE(U(waveOutGetNumDevs)); child_count(fam); }
CHILD_TEST(F_WAVEOUT, waveout)
CHILD_TEST(F_WAVEIN, wavein)
CHILD_TEST(F_MIDIOUT, midiout)
CHILD_TEST(F_MIDIIN, midiin)
CHILD_TEST(F_MIXER, mixer)
CHILD_TEST(F_AUX, aux)

/* Child: joyGetNumDevs as the only winmm call (no audio init needed). */
CHILD(joy_only) {
    UINT (WINAPI *j)(void) = (UINT (WINAPI *)(void))t_proc_in("winmm.dll", "joyGetNumDevs");
    return j ? (int)j() : 0xFFFF;
}
TEST(enum_joy_numdevs_in_fresh_process) {
    REQUIRE(U(joyGetNumDevs));
    EXPECT_EQ(t_run_child("joy_only", NULL, 30000), 16);
}

/* ---- counts unaffected by opening/closing handles ---------------------------------------------------------- */
TEST(enum_counts_unchanged_by_open_close) {
    REQUIRE(U(waveOutGetNumDevs)); REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutClose)); REQUIRE(U(mixerOpen)); REQUIRE(U(mixerClose));
    ee_counts a, b; ee_snapshot(&a);
    if (a.n[F_WAVEOUT]) {
        WAVEFORMATEX wf = { WAVE_FORMAT_PCM, 2, 44100, 44100 * 4, 4, 16, 0 };
        HWAVEOUT h = NULL;
        MMRESULT r = U(waveOutOpen)(&h, 0, &wf, 0, 0, CALLBACK_NULL);
        EXPECT_EQ(r, MMSYSERR_NOERROR);
        ee_snapshot(&b);
        CHECK(!memcmp(&a, &b, sizeof a));
        if (!r) EXPECT_EQ(U(waveOutClose)(h), MMSYSERR_NOERROR);
    }
    if (a.n[F_MIXER]) {
        HMIXER m = NULL;
        MMRESULT r = U(mixerOpen)(&m, 0, 0, 0, MIXER_OBJECTF_MIXER);
        EXPECT_EQ(r, MMSYSERR_NOERROR);
        ee_snapshot(&b);
        CHECK(!memcmp(&a, &b, sizeof a));
        if (!r) EXPECT_EQ(U(mixerClose)(m), MMSYSERR_NOERROR);
    }
    ee_snapshot(&b);
    CHECK(!memcmp(&a, &b, sizeof a));
}

/* GetNumDevs through the driver message interface: WODM_GETNUMDEVS / WIDM_GETNUMDEVS sent by id to device 0 are
 * answered by that device's driver (wdmaud), which reports its own per-driver count (one endpoint per driver). */
TEST(enum_driver_getnumdevs_message) {
    REQUIRE(U(waveOutMessage)); REQUIRE(U(waveOutGetNumDevs));
    UINT n = U(waveOutGetNumDevs)();
    if (!n) {
        EXPECT_EQ(U(waveOutMessage)((HWAVEOUT)0, WODM_GETNUMDEVS, 0, 0), MMSYSERR_BADDEVICEID);
        return;
    }
    for (UINT i = 0; i < n; i++) {
        MMRESULT r = U(waveOutMessage)((HWAVEOUT)(UINT_PTR)i, WODM_GETNUMDEVS, 0, 0);
        t_note("waveOutMessage(%u, WODM_GETNUMDEVS) -> %u", i, r);
        CHECKF(r >= 1 && r <= n, "driver count %u (total %u)", r, n);
    }
}
