#include "wp.h"
/* waveOutPause / waveOutRestart on a live device.
 * winmmbase forwards WODM_PAUSE (10) / WODM_RESTART (11) after the handle check (decomp); the state machine is the
 * driver's, so the playing-side expectations come from the VM.
 * VM_TIMEOUT: 240 */

#define SLACK_FR WP_MS2FR(10)

static void cleanup(wp_dev *d, WAVEHDR *hs, int n) {
    if (d->h) { U(waveOutRestart)(d->h); U(waveOutReset)(d->h); }
    for (int i = 0; i < n; i++) { if (d->h) wp_unprep(d, &hs[i]); wp_free_hdr(&hs[i]); }
}
static int queue(wp_dev *d, WAVEHDR *hs, int n, DWORD ms) {
    int ok = 1;
    for (int i = 0; i < n; i++) { wp_hdr_ms(&hs[i], d, ms); if (wp_prep(d, &hs[i])) ok = 0; }
    for (int i = 0; i < n; i++) if (wp_write(d, &hs[i])) ok = 0;
    CHECKF(ok, "prepare/write failed");
    return ok;
}
static DWORD absdiff(DWORD a, DWORD b) { return a > b ? a - b : b - a; }

TEST(wp_pause_idle_returns_0) {
    REQUIRE(U(waveOutPause)); REQUIRE(U(waveOutRestart));
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    EXPECT_EQ(U(waveOutPause)(d.h), MMSYSERR_NOERROR);
    EXPECT_EQ(wp_samples(d.h), 0);
    EXPECT_EQ(U(waveOutRestart)(d.h), MMSYSERR_NOERROR);
    EXPECT_EQ(wp_samples(d.h), 0);
    EXPECT_EQ(wp_close(&d), MMSYSERR_NOERROR);
}

TEST(wp_pause_twice_returns_0) {
    REQUIRE(U(waveOutPause));
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    EXPECT_EQ(U(waveOutPause)(d.h), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveOutPause)(d.h), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveOutPause)(d.h), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveOutRestart)(d.h), MMSYSERR_NOERROR);
    EXPECT_EQ(wp_close(&d), MMSYSERR_NOERROR);
}

TEST(wp_restart_without_pause_returns_0) {
    REQUIRE(U(waveOutRestart));
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    EXPECT_EQ(U(waveOutRestart)(d.h), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveOutRestart)(d.h), MMSYSERR_NOERROR);
    EXPECT_EQ(wp_samples(d.h), 0);
    EXPECT_EQ(wp_close(&d), MMSYSERR_NOERROR);
}

/* A single pause on a playing stream freezes the position; restart resumes it and everything completes. */
TEST(wp_pause_freezes_position) {
    REQUIRE(U(waveOutPause)); REQUIRE(U(waveOutRestart)); REQUIRE(U(waveOutGetPosition));
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    WAVEHDR hs[3];
    DWORD total = 3 * WP_MS2FR(200);
    if (queue(&d, hs, 3, 200)) {
        Sleep(120);
        EXPECT_EQ(U(waveOutPause)(d.h), MMSYSERR_NOERROR);
        Sleep(20);
        DWORD p1 = wp_samples(d.h);
        Sleep(200);
        DWORD p2 = wp_samples(d.h);
        t_note("paused at %lu, after 200 ms %lu (diff %ld frames)", p1, p2, (LONG)(p2 - p1));
        CHECKF(p1 != 0xFFFFFFFF && p1 < total, "p1 %lu", p1);
        CHECKF(absdiff(p1, p2) <= SLACK_FR, "position moved %lu -> %lu while paused", p1, p2);
        CHECKF(!(hs[2].dwFlags & WHDR_DONE), "last header completed while paused");
        CHECK(hs[2].dwFlags & WHDR_INQUEUE);
        /* while paused the counters are stable: bytes == samples * nBlockAlign exactly */
        DWORD s = wp_samples(d.h), b = wp_bytes(d.h);
        EXPECT_EQ(b, s * WP_ALIGN);
        EXPECT_EQ(U(waveOutRestart)(d.h), MMSYSERR_NOERROR);
        CHECK(wp_wait_all(&d, hs, 3, 600 + WP_DONE_SLACK));
        EXPECT_EQ(wp_samples(d.h), total);
        for (int i = 0; i < 3; i++) EXPECT_EQ(hs[i].dwFlags, WHDR_DONE | WHDR_PREPARED);
    }
    cleanup(&d, hs, 3);
    EXPECT_EQ(wp_close(&d), MMSYSERR_NOERROR);
}

/* Paused before anything is written: the buffer is accepted (INQUEUE) but not consumed until restart. */
static void pause_before_write(int n, DWORD ms) {
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    EXPECT_EQ(U(waveOutPause)(d.h), MMSYSERR_NOERROR);
    WAVEHDR hs[4];
    if (queue(&d, hs, n, ms)) {
        Sleep(n * ms + 200);
        for (int i = 0; i < n; i++) EXPECT_EQ(hs[i].dwFlags, WHDR_PREPARED | WHDR_INQUEUE);
        EXPECT_EQ(wp_samples(d.h), 0);
        EXPECT_EQ(wp_bytes(d.h), 0);
        EXPECT_EQ(U(waveOutRestart)(d.h), MMSYSERR_NOERROR);
        CHECK(wp_wait_all(&d, hs, n, n * ms + WP_DONE_SLACK));
        EXPECT_EQ(wp_samples(d.h), n * WP_MS2FR(ms));
    }
    cleanup(&d, hs, n);
    EXPECT_EQ(wp_close(&d), MMSYSERR_NOERROR);
}
TEST(wp_pause_before_write_one) { REQUIRE(U(waveOutPause)); pause_before_write(1, 100); }
TEST(wp_pause_before_write_three) { REQUIRE(U(waveOutPause)); pause_before_write(3, 100); }
TEST(wp_pause_before_write_four_short) { REQUIRE(U(waveOutPause)); pause_before_write(4, 20); }

/* The paused state survives a drained queue: pause after completion, then write -> held until restart. */
TEST(wp_pause_after_drain_holds_next_write) {
    REQUIRE(U(waveOutPause));
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    WAVEHDR hs[2];
    wp_hdr_ms(&hs[0], &d, 50); wp_hdr_ms(&hs[1], &d, 50);
    EXPECT_EQ(wp_prep(&d, &hs[0]), 0); EXPECT_EQ(wp_prep(&d, &hs[1]), 0);
    EXPECT_EQ(wp_write(&d, &hs[0]), 0);
    if (wp_wait_done(&d, &hs[0], 50 + WP_DONE_SLACK)) {
        EXPECT_EQ(U(waveOutPause)(d.h), MMSYSERR_NOERROR);
        EXPECT_EQ(wp_write(&d, &hs[1]), 0);
        Sleep(250);
        EXPECT_EQ(hs[1].dwFlags, WHDR_PREPARED | WHDR_INQUEUE);
        EXPECT_EQ(wp_samples(d.h), WP_MS2FR(50));
        EXPECT_EQ(U(waveOutRestart)(d.h), MMSYSERR_NOERROR);
        CHECK(wp_wait_done(&d, &hs[1], 50 + WP_DONE_SLACK));
        EXPECT_EQ(wp_samples(d.h), 2 * WP_MS2FR(50));
    } else CHECKF(0, "first buffer did not complete");
    cleanup(&d, hs, 2);
    EXPECT_EQ(wp_close(&d), MMSYSERR_NOERROR);
}

/* Several pause/restart cycles over one long buffer: the total is still exact. */
TEST(wp_pause_restart_cycles_exact_total) {
    REQUIRE(U(waveOutPause)); REQUIRE(U(waveOutRestart));
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    WAVEHDR hs[1];
    if (queue(&d, hs, 1, 500)) {
        DWORD last = 0, back = 0;
        for (int c = 0; c < 5 && !(hs[0].dwFlags & WHDR_DONE); c++) {
            Sleep(40);
            EXPECT_EQ(U(waveOutPause)(d.h), MMSYSERR_NOERROR);
            DWORD p = wp_samples(d.h);
            if (p < last) back++;
            last = p;
            Sleep(30);
            EXPECT_EQ(U(waveOutRestart)(d.h), MMSYSERR_NOERROR);
        }
        EXPECT_EQ(back, 0);
        CHECK(wp_wait_done(&d, &hs[0], 500 + WP_DONE_SLACK));
        EXPECT_EQ(wp_samples(d.h), WP_MS2FR(500));
    }
    cleanup(&d, hs, 1);
    EXPECT_EQ(wp_close(&d), MMSYSERR_NOERROR);
}

/* Restart on a stream that is playing (not paused) changes nothing. */
TEST(wp_restart_while_playing_no_effect) {
    REQUIRE(U(waveOutRestart));
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    WAVEHDR hs[2];
    if (queue(&d, hs, 2, 100)) {
        Sleep(30);
        EXPECT_EQ(U(waveOutRestart)(d.h), MMSYSERR_NOERROR);
        EXPECT_EQ(U(waveOutRestart)(d.h), MMSYSERR_NOERROR);
        CHECK(wp_wait_all(&d, hs, 2, 200 + WP_DONE_SLACK));
        EXPECT_EQ(wp_samples(d.h), 2 * WP_MS2FR(100));
    }
    cleanup(&d, hs, 2);
    EXPECT_EQ(wp_close(&d), MMSYSERR_NOERROR);
}

/* Double pause while playing = single pause; a single restart resumes. */
TEST(wp_pause_double_single_restart) {
    REQUIRE(U(waveOutPause));
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    WAVEHDR hs[2];
    if (queue(&d, hs, 2, 100)) {
        Sleep(40);
        EXPECT_EQ(U(waveOutPause)(d.h), MMSYSERR_NOERROR);
        EXPECT_EQ(U(waveOutPause)(d.h), MMSYSERR_NOERROR);
        DWORD p1 = wp_samples(d.h);
        Sleep(150);
        CHECKF(absdiff(wp_samples(d.h), p1) <= SLACK_FR, "moved while paused");
        EXPECT_EQ(U(waveOutRestart)(d.h), MMSYSERR_NOERROR);
        CHECK(wp_wait_all(&d, hs, 2, 200 + WP_DONE_SLACK));
        EXPECT_EQ(wp_samples(d.h), 2 * WP_MS2FR(100));
    }
    cleanup(&d, hs, 2);
    EXPECT_EQ(wp_close(&d), MMSYSERR_NOERROR);
}

/* Every wType answers while paused; the rewritten types report bytes. */
TEST(wp_pause_position_types_while_paused) {
    REQUIRE(U(waveOutPause)); REQUIRE(U(waveOutGetPosition));
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    WAVEHDR hs[1];
    if (queue(&d, hs, 1, 300)) {
        Sleep(80);
        EXPECT_EQ(U(waveOutPause)(d.h), MMSYSERR_NOERROR);
        Sleep(20);
        DWORD s = wp_samples(d.h);
        static const UINT types[] = { TIME_MS, TIME_SAMPLES, TIME_BYTES, TIME_SMPTE, TIME_MIDI, TIME_TICKS };
        for (int i = 0; i < 6; i++) {
            DWORD v = 0; UINT t = 0;
            EXPECT_EQ(wp_pos(d.h, types[i], &v, &t), MMSYSERR_NOERROR);
            if (types[i] == TIME_SAMPLES) { EXPECT_EQ(t, TIME_SAMPLES); EXPECT_EQ(v, s); }
            else { EXPECT_EQ(t, TIME_BYTES); EXPECT_EQ(v, s * WP_ALIGN); }
        }
        EXPECT_EQ(U(waveOutRestart)(d.h), MMSYSERR_NOERROR);
        CHECK(wp_wait_done(&d, &hs[0], 300 + WP_DONE_SLACK));
    }
    cleanup(&d, hs, 1);
    EXPECT_EQ(wp_close(&d), MMSYSERR_NOERROR);
}

/* Close while paused with queued headers is refused like close while playing. */
TEST(wp_pause_close_with_queue_is_33) {
    REQUIRE(U(waveOutPause)); REQUIRE(U(waveOutClose));
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    EXPECT_EQ(U(waveOutPause)(d.h), MMSYSERR_NOERROR);
    WAVEHDR hs[2];
    if (queue(&d, hs, 2, 100)) {
        MMRESULT r = U(waveOutClose)(d.h);
        EXPECT_EQ(r, WAVERR_STILLPLAYING);
        if (r == MMSYSERR_NOERROR) { d.h = NULL; for (int i = 0; i < 2; i++) wp_free_hdr(&hs[i]); wp_close(&d); return; }
        /* the handle stays open and usable */
        EXPECT_EQ(wp_samples(d.h), 0);
        EXPECT_EQ(U(waveOutRestart)(d.h), MMSYSERR_NOERROR);
        CHECK(wp_wait_all(&d, hs, 2, 200 + WP_DONE_SLACK));
    }
    cleanup(&d, hs, 2);
    EXPECT_EQ(wp_close(&d), MMSYSERR_NOERROR);
}

/* Paused with nothing queued: close succeeds. */
TEST(wp_pause_close_idle_is_0) {
    REQUIRE(U(waveOutPause)); REQUIRE(U(waveOutClose));
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    EXPECT_EQ(U(waveOutPause)(d.h), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveOutClose)(d.h), MMSYSERR_NOERROR);
    d.h = NULL;
    wp_close(&d);
}

/* Pausing one handle does not hold another. */
TEST(wp_pause_is_per_handle) {
    REQUIRE(U(waveOutPause));
    WP_NEED_DEV();
    wp_dev a, b;
    if (wp_open(&a)) { CHECK(0); return; }
    if (wp_open(&b)) { CHECK(0); wp_close(&a); return; }
    EXPECT_EQ(U(waveOutPause)(a.h), MMSYSERR_NOERROR);
    WAVEHDR ha[1], hb[1];
    queue(&a, ha, 1, 100);
    queue(&b, hb, 1, 100);
    CHECK(wp_wait_done(&b, &hb[0], 100 + WP_DONE_SLACK));
    EXPECT_EQ(ha[0].dwFlags, WHDR_PREPARED | WHDR_INQUEUE);
    EXPECT_EQ(wp_samples(a.h), 0);
    EXPECT_EQ(wp_samples(b.h), WP_MS2FR(100));
    EXPECT_EQ(U(waveOutRestart)(a.h), MMSYSERR_NOERROR);
    CHECK(wp_wait_done(&a, &ha[0], 100 + WP_DONE_SLACK));
    EXPECT_EQ(wp_samples(a.h), WP_MS2FR(100));
    cleanup(&a, ha, 1); cleanup(&b, hb, 1);
    EXPECT_EQ(wp_close(&a), MMSYSERR_NOERROR);
    EXPECT_EQ(wp_close(&b), MMSYSERR_NOERROR);
}

/* BreakLoop while paused (no loop) is a no-op success, and does not unpause. */
TEST(wp_pause_breakloop_while_paused) {
    REQUIRE(U(waveOutPause)); REQUIRE(U(waveOutBreakLoop));
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    EXPECT_EQ(U(waveOutPause)(d.h), MMSYSERR_NOERROR);
    WAVEHDR hs[1];
    if (queue(&d, hs, 1, 60)) {
        EXPECT_EQ(U(waveOutBreakLoop)(d.h), MMSYSERR_NOERROR);
        Sleep(200);
        EXPECT_EQ(hs[0].dwFlags, WHDR_PREPARED | WHDR_INQUEUE);
        EXPECT_EQ(U(waveOutRestart)(d.h), MMSYSERR_NOERROR);
        CHECK(wp_wait_done(&d, &hs[0], 60 + WP_DONE_SLACK));
    }
    cleanup(&d, hs, 1);
    EXPECT_EQ(wp_close(&d), MMSYSERR_NOERROR);
}

/* Pause and Restart through the mapper and through each device id behave the same. */
TEST(wp_pause_every_device) {
    REQUIRE(U(waveOutPause));
    WP_NEED_DEV();
    for (UINT id = 0; id < wp_nout(); id++) {
        WAVEFORMATEX wf; wp_fmt_std(&wf);
        wp_dev d;
        if (wp_open_ex(&d, id, &wf)) { CHECKF(0, "open %u", id); continue; }
        EXPECT_EQ(U(waveOutPause)(d.h), MMSYSERR_NOERROR);
        WAVEHDR hs[1];
        if (queue(&d, hs, 1, 50)) {
            Sleep(150);
            EXPECT_EQ(wp_samples(d.h), 0);
            EXPECT_EQ(U(waveOutRestart)(d.h), MMSYSERR_NOERROR);
            CHECK(wp_wait_done(&d, &hs[0], 50 + WP_DONE_SLACK));
            EXPECT_EQ(wp_samples(d.h), WP_MS2FR(50));
        }
        cleanup(&d, hs, 1);
        EXPECT_EQ(wp_close(&d), MMSYSERR_NOERROR);
    }
}

/* A quiet tone interrupted by a pause (host capture sees one tone; the total length is unchanged). */
TEST(wp_pause_tone_resumes) {
    REQUIRE(U(waveOutPause));
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    WAVEHDR wh; wp_hdr_ms(&wh, &d, 300);
    wp_fill_tone(&wh, &d, 990);
    EXPECT_EQ(U(waveOutPause)(d.h), MMSYSERR_NOERROR);
    EXPECT_EQ(wp_prep(&d, &wh), MMSYSERR_NOERROR);
    EXPECT_EQ(wp_write(&d, &wh), MMSYSERR_NOERROR);
    Sleep(100);
    t_note("TONE hz=%d ms=%d", 990, 300);
    EXPECT_EQ(U(waveOutRestart)(d.h), MMSYSERR_NOERROR);
    CHECK(wp_wait_done(&d, &wh, 300 + WP_DONE_SLACK));
    EXPECT_EQ(wp_samples(d.h), WP_MS2FR(300));
    cleanup(&d, &wh, 1);
    EXPECT_EQ(wp_close(&d), MMSYSERR_NOERROR);
}
