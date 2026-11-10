#include "t.h"
#include <mmsystem.h>
#include <math.h>
/* Chain proof for the VM runner (docs/VM_RUN.md): a real winmm call path, the audio environment record, and one short
 * quiet tone whose rendering tools/wav_capture.sh verifies on the host. Full coverage lives in the time_* and
 * waveout_* areas. VM_TIMEOUT: 60 */

TEST(smoke_time_gettime_monotonic) {
    REQUIRE(U(timeGetTime));
    DWORD prev = U(timeGetTime)(), bad = 0;
    for (int i = 0; i < 1000; i++) { DWORD t = U(timeGetTime)(); if ((LONG)(t - prev) < 0) bad++; prev = t; }
    EXPECT_EQ(bad, 0);
    DWORD a = U(timeGetTime)(); Sleep(50); DWORD d = U(timeGetTime)() - a;
    CHECKF(d >= 30 && d < 10000, "advanced %lu ms across Sleep(50)", d);
    t_note("timeGetTime - GetTickCount = %ld ms", (LONG)(U(timeGetTime)() - GetTickCount()));
}

TEST(smoke_time_devcaps) {
    REQUIRE(U(timeGetDevCaps));
    TIMECAPS tc;
/* UNVERIFIED: pin after the first Win10 run */
    EXPECT_EQ(U(timeGetDevCaps)(NULL, sizeof tc), TIMERR_NOCANDO);
/* UNVERIFIED: pin after the first Win10 run */
    EXPECT_EQ(U(timeGetDevCaps)(&tc, sizeof tc - 1), TIMERR_NOCANDO);
    memset(&tc, 0xCC, sizeof tc);
    EXPECT_EQ(U(timeGetDevCaps)(&tc, sizeof tc), MMSYSERR_NOERROR);
    CHECKF(tc.wPeriodMin >= 1 && tc.wPeriodMin <= 16, "wPeriodMin %u", tc.wPeriodMin);
/* UNVERIFIED: pin after the first Win10 run */
    EXPECT_EQ(tc.wPeriodMax, 1000000);
}

/* Records the device set the run saw (results file), and checks the parts of the contract that hold for any set:
 * an id equal to the device count is out of range; with no waveOut device even the mapper reports BADDEVICEID. */
TEST(smoke_audio_environment) {
    REQUIRE(U(waveOutGetNumDevs)); REQUIRE(U(waveOutGetDevCapsW)); REQUIRE(U(waveInGetNumDevs)); REQUIRE(U(waveInGetDevCapsW));
    REQUIRE(U(midiOutGetNumDevs)); REQUIRE(U(mixerGetNumDevs));
    UINT wo = U(waveOutGetNumDevs)(), wi = U(waveInGetNumDevs)(), mo = U(midiOutGetNumDevs)(), mx = U(mixerGetNumDevs)();
    t_note("AUDIOENV waveout=%u wavein=%u midiout=%u mixer=%u", wo, wi, mo, mx);
    WAVEOUTCAPSW oc; WAVEINCAPSW ic;
    EXPECT_EQ(U(waveOutGetDevCapsW)(wo, &oc, sizeof oc), MMSYSERR_BADDEVICEID);
    EXPECT_EQ(U(waveInGetDevCapsW)(wi, &ic, sizeof ic), MMSYSERR_BADDEVICEID);
    if (wo == 0) {
        EXPECT_EQ(U(waveOutGetDevCapsW)(WAVE_MAPPER, &oc, sizeof oc), MMSYSERR_BADDEVICEID);
    } else {
        memset(&oc, 0, sizeof oc);
        EXPECT_EQ(U(waveOutGetDevCapsW)(0, &oc, sizeof oc), MMSYSERR_NOERROR);
        CHECK(oc.szPname[0] != 0);
        EXPECT_EQ(U(waveOutGetDevCapsW)(WAVE_MAPPER, &oc, sizeof oc), MMSYSERR_NOERROR);
    }
}

/* 300 ms, 1234 Hz, -12 dBFS peak with 10 ms ramps, through the wave mapper. */
TEST(smoke_waveout_tone) {
    REQUIRE(U(waveOutGetNumDevs)); REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutPrepareHeader)); REQUIRE(U(waveOutWrite));
    REQUIRE(U(waveOutUnprepareHeader)); REQUIRE(U(waveOutGetPosition)); REQUIRE(U(waveOutClose)); REQUIRE(U(waveOutReset));
    enum { RATE = 44100, MS = 300, HZ = 1234, FRAMES = RATE * MS / 1000 };
    WAVEFORMATEX wf = { WAVE_FORMAT_PCM, 2, RATE, RATE * 4, 4, 16, 0 };
    HWAVEOUT h = (HWAVEOUT)(ULONG_PTR)1;
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    if (U(waveOutGetNumDevs)() == 0) {
        EXPECT_EQ(U(waveOutOpen)(&h, WAVE_MAPPER, &wf, (DWORD_PTR)ev, 0, CALLBACK_EVENT), MMSYSERR_BADDEVICEID);
        EXPECT_EQ(h, NULL);   /* waveout_open_contract EVIDENCE: *phwo is NULLed on a failed open */
        CloseHandle(ev);
        return;
    }
    static short pcm[FRAMES * 2];
    const double tau = 6.283185307179586;
    for (int i = 0; i < FRAMES; i++) {
        double env = 1.0, r = RATE / 100.0;
        if (i < r) env = i / r; else if (i > FRAMES - r) env = (FRAMES - i) / r;
        pcm[2 * i] = pcm[2 * i + 1] = (short)(0.25 * 32767 * env * sin(tau * HZ * i / RATE));
    }
    MMRESULT r = U(waveOutOpen)(&h, WAVE_MAPPER, &wf, (DWORD_PTR)ev, 0, CALLBACK_EVENT);
    EXPECT_EQ(r, MMSYSERR_NOERROR);
    if (r) { CloseHandle(ev); return; }
    WaitForSingleObject(ev, 1000);   /* WOM_OPEN */
    WAVEHDR wh; memset(&wh, 0, sizeof wh); wh.lpData = (LPSTR)pcm; wh.dwBufferLength = sizeof pcm;
    EXPECT_EQ(U(waveOutPrepareHeader)(h, &wh, sizeof wh), MMSYSERR_NOERROR);
    EXPECT_EQ(wh.dwFlags, WHDR_PREPARED);
    t_note("TONE hz=%d ms=%d", HZ, MS);
    EXPECT_EQ(U(waveOutWrite)(h, &wh, sizeof wh), MMSYSERR_NOERROR);
    CHECK(wh.dwFlags & WHDR_PREPARED);
    DWORD t0 = GetTickCount();
    while (!(wh.dwFlags & WHDR_DONE) && GetTickCount() - t0 < 5000) WaitForSingleObject(ev, 200);
    CHECKF(wh.dwFlags & WHDR_DONE, "flags 0x%lx after %lu ms", wh.dwFlags, GetTickCount() - t0);
    if (wh.dwFlags & WHDR_DONE) {
        EXPECT_EQ(wh.dwFlags, WHDR_DONE | WHDR_PREPARED);
        MMTIME mt; memset(&mt, 0, sizeof mt); mt.wType = TIME_SAMPLES;
        EXPECT_EQ(U(waveOutGetPosition)(h, &mt, sizeof mt), MMSYSERR_NOERROR);
        EXPECT_EQ(mt.wType, TIME_SAMPLES);
        EXPECT_EQ(mt.u.sample, FRAMES);
        t_note("tone completed after %lu ms", GetTickCount() - t0);
    } else {
        U(waveOutReset)(h);
    }
    EXPECT_EQ(U(waveOutUnprepareHeader)(h, &wh, sizeof wh), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveOutClose)(h), MMSYSERR_NOERROR);
    CloseHandle(ev);
}
