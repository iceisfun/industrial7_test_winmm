#include "wp.h"
/* Header loops (WHDR_BEGINLOOP / WHDR_ENDLOOP, dwLoops) and waveOutBreakLoop (WODM_BREAKLOOP 0x14).
 * winmmbase only validates the handle (5) and keeps BEGINLOOP/ENDLOOP through PrepareHeader (dwFlags &= 0xC) (decomp);
 * how loops play is the driver's: the counts below are VM observations.
 * VM_TIMEOUT: 240 */

static void cleanup(wp_dev *d, WAVEHDR *hs, int n) {
    if (d->h) { U(waveOutRestart)(d->h); U(waveOutReset)(d->h); }
    for (int i = 0; i < n; i++) { if (d->h) wp_unprep(d, &hs[i]); wp_free_hdr(&hs[i]); }
}

/* One header that is its own loop: BEGINLOOP|ENDLOOP, dwLoops = n. Expected position: n x frames (VM). */
static void single_loop(DWORD loops, DWORD ms, DWORD expect_mult) {
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    WAVEHDR wh; wp_hdr_ms(&wh, &d, ms);
    wh.dwFlags = WHDR_BEGINLOOP | WHDR_ENDLOOP;
    wh.dwLoops = loops;
    EXPECT_EQ(wp_prep(&d, &wh), MMSYSERR_NOERROR);
    EXPECT_EQ(wh.dwFlags, WHDR_BEGINLOOP | WHDR_ENDLOOP | WHDR_PREPARED);
    EXPECT_EQ(wp_write(&d, &wh), MMSYSERR_NOERROR);
    CHECK(wh.dwFlags & WHDR_INQUEUE);
    DWORD t0 = GetTickCount();
    int ok = wp_wait_done(&d, &wh, (loops ? loops : 1) * ms + WP_DONE_SLACK);
    DWORD el = GetTickCount() - t0;
    CHECKF(ok, "loop x%lu did not complete", loops);
    if (ok) {
        DWORD s = wp_samples(d.h), fr = WP_MS2FR(ms);
        t_note("dwLoops %lu over %lu ms: %lu samples = %.2f x buffer, %lu ms", loops, ms, s, (double)s / fr, el);
        EXPECT_EQ(s, expect_mult * fr);
        EXPECT_EQ(wh.dwFlags, WHDR_BEGINLOOP | WHDR_ENDLOOP | WHDR_PREPARED | WHDR_DONE);
        EXPECT_EQ(wh.dwLoops, loops);
    }
    cleanup(&d, &wh, 1);
    EXPECT_EQ(wp_close(&d), MMSYSERR_NOERROR);
}
TEST(wp_loop_single_x3) { REQUIRE(U(waveOutBreakLoop)); single_loop(3, 100, 3); }
TEST(wp_loop_single_x1) { REQUIRE(U(waveOutBreakLoop)); single_loop(1, 100, 1); }
TEST(wp_loop_single_x2) { REQUIRE(U(waveOutBreakLoop)); single_loop(2, 80, 2); }
TEST(wp_loop_single_x5_short) { REQUIRE(U(waveOutBreakLoop)); single_loop(5, 30, 5); }
TEST(wp_loop_single_x0) { REQUIRE(U(waveOutBreakLoop)); single_loop(0, 100, 1); }

/* Only one of the two loop flags: no loop. */
static void half_loop(DWORD flag) {
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    WAVEHDR wh; wp_hdr_ms(&wh, &d, 60);
    wh.dwFlags = flag; wh.dwLoops = 4;
    EXPECT_EQ(wp_prep(&d, &wh), MMSYSERR_NOERROR);
    EXPECT_EQ(wp_write(&d, &wh), MMSYSERR_NOERROR);
    int ok = wp_wait_done(&d, &wh, 4 * 60 + WP_DONE_SLACK);
    CHECK(ok);
    if (ok) {
        DWORD s = wp_samples(d.h);
        t_note("flag 0x%lx alone, dwLoops 4: %lu samples (%.2f x)", flag, s, (double)s / WP_MS2FR(60));
        EXPECT_EQ(s, WP_MS2FR(60));
    }
    cleanup(&d, &wh, 1);
    EXPECT_EQ(wp_close(&d), MMSYSERR_NOERROR);
}
/* A BEGINLOOP header with no ENDLOOP anywhere in the queue is never returned on its own (VM: still INQUEUE well after
 * its play time); Reset returns it. */
TEST(wp_loop_beginloop_only_is_held) {
    REQUIRE(U(waveOutBreakLoop)); REQUIRE(U(waveOutReset));
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    WAVEHDR wh; wp_hdr_ms(&wh, &d, 60);
    wh.dwFlags = WHDR_BEGINLOOP; wh.dwLoops = 4;
    EXPECT_EQ(wp_prep(&d, &wh), MMSYSERR_NOERROR);
    EXPECT_EQ(wp_write(&d, &wh), MMSYSERR_NOERROR);
    Sleep(400);
    DWORD p1 = wp_samples(d.h);
    Sleep(200);
    DWORD p2 = wp_samples(d.h);
    t_note("lone BEGINLOOP (60 ms, dwLoops 4) after 400/600 ms: flags 0x%lx, samples %lu / %lu", wh.dwFlags, p1, p2);
    EXPECT_EQ(wh.dwFlags, WHDR_BEGINLOOP | WHDR_PREPARED | WHDR_INQUEUE);
    CHECKF(p2 <= WP_MS2FR(60), "position %lu beyond the buffer", p2);
    /* BreakLoop does not release it either (recorded) */
    EXPECT_EQ(U(waveOutBreakLoop)(d.h), MMSYSERR_NOERROR);
    Sleep(200);
    t_note("after BreakLoop: flags 0x%lx samples %lu", wh.dwFlags, wp_samples(d.h));
    EXPECT_EQ(U(waveOutReset)(d.h), MMSYSERR_NOERROR);
    EXPECT_EQ(wh.dwFlags, WHDR_BEGINLOOP | WHDR_PREPARED | WHDR_DONE);
    EXPECT_EQ(wp_samples(d.h), 0);
    EXPECT_EQ(wp_unprep(&d, &wh), MMSYSERR_NOERROR);
    wp_free_hdr(&wh);
    EXPECT_EQ(wp_close(&d), MMSYSERR_NOERROR);
}

/* The held BEGINLOOP header is completed by a later ENDLOOP header: the span then loops dwLoops times. */
TEST(wp_loop_endloop_written_later) {
    REQUIRE(U(waveOutBreakLoop));
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    WAVEHDR hs[2];
    wp_hdr_ms(&hs[0], &d, 50); wp_hdr_ms(&hs[1], &d, 50);
    hs[0].dwFlags = WHDR_BEGINLOOP; hs[0].dwLoops = 2;
    hs[1].dwFlags = WHDR_ENDLOOP;
    EXPECT_EQ(wp_prep(&d, &hs[0]), 0); EXPECT_EQ(wp_prep(&d, &hs[1]), 0);
    EXPECT_EQ(wp_write(&d, &hs[0]), 0);
    Sleep(250);
    t_note("BEGINLOOP alone after 250 ms: flags 0x%lx samples %lu", hs[0].dwFlags, wp_samples(d.h));
    EXPECT_EQ(wp_write(&d, &hs[1]), 0);
    int ok = wp_wait_all(&d, hs, 2, 200 + WP_DONE_SLACK);
    CHECK(ok);
    if (ok) {
        DWORD s = wp_samples(d.h);
        t_note("then ENDLOOP: %lu samples (%.2f x 50 ms)", s, (double)s / WP_MS2FR(50));
        EXPECT_EQ(s, 4 * WP_MS2FR(50));
    }
    U(waveOutReset)(d.h);
    for (int i = 0; i < 2; i++) { wp_unprep(&d, &hs[i]); wp_free_hdr(&hs[i]); }
    EXPECT_EQ(wp_close(&d), MMSYSERR_NOERROR);
}

/* BreakLoop inside a loop that spans several headers: the current pass finishes, no more passes. */
TEST(wp_breakloop_multi_header_loop) {
    REQUIRE(U(waveOutBreakLoop));
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    WAVEHDR hs[2];
    wp_hdr_ms(&hs[0], &d, 40); wp_hdr_ms(&hs[1], &d, 40);
    hs[0].dwFlags = WHDR_BEGINLOOP; hs[0].dwLoops = 20;
    hs[1].dwFlags = WHDR_ENDLOOP;
    EXPECT_EQ(wp_prep(&d, &hs[0]), 0); EXPECT_EQ(wp_prep(&d, &hs[1]), 0);
    EXPECT_EQ(wp_write(&d, &hs[0]), 0); EXPECT_EQ(wp_write(&d, &hs[1]), 0);
    Sleep(200);
    EXPECT_EQ(U(waveOutBreakLoop)(d.h), MMSYSERR_NOERROR);
    int ok = wp_wait_all(&d, hs, 2, 1600 + WP_DONE_SLACK);
    CHECK(ok);
    if (ok) {
        DWORD s = wp_samples(d.h), span = 2 * WP_MS2FR(40);
        t_note("2-header loop x20, BreakLoop after 200 ms: %lu samples (%.2f passes)", s, (double)s / span);
        CHECKF(s < 20 * span, "position %lu not below full %lu", s, 20 * span);
        CHECKF(s % span == 0, "ended mid-pass: %lu", s);
    }
    U(waveOutReset)(d.h);
    for (int i = 0; i < 2; i++) { wp_unprep(&d, &hs[i]); wp_free_hdr(&hs[i]); }
    EXPECT_EQ(wp_close(&d), MMSYSERR_NOERROR);
}

/* BreakLoop twice in a row during one loop: same as once. */
TEST(wp_breakloop_twice) {
    REQUIRE(U(waveOutBreakLoop));
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    WAVEHDR wh; wp_hdr_ms(&wh, &d, 40);
    wh.dwFlags = WHDR_BEGINLOOP | WHDR_ENDLOOP; wh.dwLoops = 30;
    EXPECT_EQ(wp_prep(&d, &wh), 0);
    EXPECT_EQ(wp_write(&d, &wh), 0);
    Sleep(150);
    EXPECT_EQ(U(waveOutBreakLoop)(d.h), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveOutBreakLoop)(d.h), MMSYSERR_NOERROR);
    int ok = wp_wait_done(&d, &wh, 1200 + WP_DONE_SLACK);
    CHECK(ok);
    if (ok) {
        DWORD s = wp_samples(d.h);
        CHECKF(s < 30 * WP_MS2FR(40) && s % WP_MS2FR(40) == 0, "samples %lu", s);
    }
    U(waveOutReset)(d.h);
    wp_unprep(&d, &wh); wp_free_hdr(&wh);
    EXPECT_EQ(wp_close(&d), MMSYSERR_NOERROR);
}

/* Two consecutive self-loops each repeat their own count. */
TEST(wp_loop_two_consecutive_loops) {
    REQUIRE(U(waveOutBreakLoop));
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    WAVEHDR hs[2];
    wp_hdr_ms(&hs[0], &d, 30); wp_hdr_ms(&hs[1], &d, 30);
    hs[0].dwFlags = hs[1].dwFlags = WHDR_BEGINLOOP | WHDR_ENDLOOP;
    hs[0].dwLoops = 2; hs[1].dwLoops = 3;
    EXPECT_EQ(wp_prep(&d, &hs[0]), 0); EXPECT_EQ(wp_prep(&d, &hs[1]), 0);
    EXPECT_EQ(wp_write(&d, &hs[0]), 0); EXPECT_EQ(wp_write(&d, &hs[1]), 0);
    int ok = wp_wait_all(&d, hs, 2, 150 + WP_DONE_SLACK);
    CHECK(ok);
    if (ok) EXPECT_EQ(wp_samples(d.h), 5 * WP_MS2FR(30));
    U(waveOutReset)(d.h);
    for (int i = 0; i < 2; i++) { wp_unprep(&d, &hs[i]); wp_free_hdr(&hs[i]); }
    EXPECT_EQ(wp_close(&d), MMSYSERR_NOERROR);
}

/* dwLoops on the ENDLOOP header is ignored: the count comes from the BEGINLOOP header (recorded, then pinned). */
TEST(wp_loop_count_read_from_begin_header) {
    REQUIRE(U(waveOutBreakLoop));
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    WAVEHDR hs[2];
    wp_hdr_ms(&hs[0], &d, 30); wp_hdr_ms(&hs[1], &d, 30);
    hs[0].dwFlags = WHDR_BEGINLOOP; hs[0].dwLoops = 2;
    hs[1].dwFlags = WHDR_ENDLOOP; hs[1].dwLoops = 7;
    EXPECT_EQ(wp_prep(&d, &hs[0]), 0); EXPECT_EQ(wp_prep(&d, &hs[1]), 0);
    EXPECT_EQ(wp_write(&d, &hs[0]), 0); EXPECT_EQ(wp_write(&d, &hs[1]), 0);
    int ok = wp_wait_all(&d, hs, 2, 420 + WP_DONE_SLACK);
    CHECK(ok);
    if (ok) {
        DWORD s = wp_samples(d.h);
        t_note("begin dwLoops 2, end dwLoops 7: %lu samples (%.2f passes)", s, (double)s / (2 * WP_MS2FR(30)));
        EXPECT_EQ(s, 2 * 2 * WP_MS2FR(30));
    }
    U(waveOutReset)(d.h);
    for (int i = 0; i < 2; i++) { wp_unprep(&d, &hs[i]); wp_free_hdr(&hs[i]); }
    EXPECT_EQ(wp_close(&d), MMSYSERR_NOERROR);
}
TEST(wp_loop_endloop_only) { REQUIRE(U(waveOutBreakLoop)); half_loop(WHDR_ENDLOOP); }

/* dwLoops without any loop flag is ignored. */
TEST(wp_loop_count_without_flags_ignored) {
    REQUIRE(U(waveOutBreakLoop));
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    WAVEHDR wh; wp_hdr_ms(&wh, &d, 60);
    wh.dwLoops = 5;
    EXPECT_EQ(wp_prep(&d, &wh), MMSYSERR_NOERROR);
    EXPECT_EQ(wp_write(&d, &wh), MMSYSERR_NOERROR);
    if (wp_wait_done(&d, &wh, 60 + WP_DONE_SLACK)) EXPECT_EQ(wp_samples(d.h), WP_MS2FR(60));
    else CHECK(0);
    cleanup(&d, &wh, 1);
    EXPECT_EQ(wp_close(&d), MMSYSERR_NOERROR);
}

/* A loop spanning three headers: BEGINLOOP on the first (dwLoops there), ENDLOOP on the last. */
TEST(wp_loop_span_three_headers_x2) {
    REQUIRE(U(waveOutBreakLoop));
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    WAVEHDR hs[3];
    for (int i = 0; i < 3; i++) wp_hdr_ms(&hs[i], &d, 50);
    hs[0].dwFlags = WHDR_BEGINLOOP; hs[0].dwLoops = 2;
    hs[2].dwFlags = WHDR_ENDLOOP;
    for (int i = 0; i < 3; i++) EXPECT_EQ(wp_prep(&d, &hs[i]), MMSYSERR_NOERROR);
    for (int i = 0; i < 3; i++) EXPECT_EQ(wp_write(&d, &hs[i]), MMSYSERR_NOERROR);
    int ok = wp_wait_all(&d, hs, 3, 300 + WP_DONE_SLACK);
    CHECK(ok);
    if (ok) {
        DWORD s = wp_samples(d.h), fr = 3 * WP_MS2FR(50);
        t_note("3-header loop x2: %lu samples (%.2f x the span)", s, (double)s / fr);
        EXPECT_EQ(s, 2 * fr);
    }
    cleanup(&d, hs, 3);
    EXPECT_EQ(wp_close(&d), MMSYSERR_NOERROR);
}

/* A loop in the middle of the queue: plain, loop x3, plain. */
TEST(wp_loop_in_middle_of_queue) {
    REQUIRE(U(waveOutBreakLoop));
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    WAVEHDR hs[3];
    for (int i = 0; i < 3; i++) wp_hdr_ms(&hs[i], &d, 40);
    hs[1].dwFlags = WHDR_BEGINLOOP | WHDR_ENDLOOP; hs[1].dwLoops = 3;
    for (int i = 0; i < 3; i++) EXPECT_EQ(wp_prep(&d, &hs[i]), MMSYSERR_NOERROR);
    for (int i = 0; i < 3; i++) EXPECT_EQ(wp_write(&d, &hs[i]), MMSYSERR_NOERROR);
    int ok = wp_wait_all(&d, hs, 3, 200 + WP_DONE_SLACK);
    CHECK(ok);
    if (ok) {
        DWORD s = wp_samples(d.h), fr = WP_MS2FR(40);
        t_note("plain + loop x3 + plain: %lu samples (%.2f x 40 ms)", s, (double)s / fr);
        EXPECT_EQ(s, 5 * fr);
    }
    cleanup(&d, hs, 3);
    EXPECT_EQ(wp_close(&d), MMSYSERR_NOERROR);
}

/* BreakLoop during a long loop: the loop ends early (position below the full count). */
TEST(wp_breakloop_ends_loop_early) {
    REQUIRE(U(waveOutBreakLoop));
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    enum { MS = 50, LOOPS = 30 };
    WAVEHDR wh; wp_hdr_ms(&wh, &d, MS);
    wh.dwFlags = WHDR_BEGINLOOP | WHDR_ENDLOOP; wh.dwLoops = LOOPS;
    EXPECT_EQ(wp_prep(&d, &wh), MMSYSERR_NOERROR);
    EXPECT_EQ(wp_write(&d, &wh), MMSYSERR_NOERROR);
    Sleep(220);
    DWORD t0 = GetTickCount();
    EXPECT_EQ(U(waveOutBreakLoop)(d.h), MMSYSERR_NOERROR);
    int ok = wp_wait_done(&d, &wh, LOOPS * MS + WP_DONE_SLACK);
    DWORD el = GetTickCount() - t0;
    CHECK(ok);
    if (ok) {
        DWORD s = wp_samples(d.h), full = LOOPS * WP_MS2FR(MS);
        t_note("breakloop after 220 ms: done %lu ms later, %lu samples (%.2f x buffer, full %lu)", el, s,
               (double)s / WP_MS2FR(MS), full);
        CHECKF(s < full, "position %lu not below full loop %lu", s, full);
        CHECKF(s % WP_MS2FR(MS) == 0, "loop ended mid-buffer: %lu", s);
        CHECKF(el < 1000, "done %lu ms after BreakLoop", el);
    }
    cleanup(&d, &wh, 1);
    EXPECT_EQ(wp_close(&d), MMSYSERR_NOERROR);
}

/* BreakLoop with no loop playing: 0, and nothing changes. */
TEST(wp_breakloop_idle_returns_0) {
    REQUIRE(U(waveOutBreakLoop));
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    EXPECT_EQ(U(waveOutBreakLoop)(d.h), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveOutBreakLoop)(d.h), MMSYSERR_NOERROR);
    EXPECT_EQ(wp_samples(d.h), 0);
    EXPECT_EQ(wp_close(&d), MMSYSERR_NOERROR);
}

TEST(wp_breakloop_plain_playback_unaffected) {
    REQUIRE(U(waveOutBreakLoop));
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    WAVEHDR hs[2];
    for (int i = 0; i < 2; i++) { wp_hdr_ms(&hs[i], &d, 100); EXPECT_EQ(wp_prep(&d, &hs[i]), 0); }
    for (int i = 0; i < 2; i++) EXPECT_EQ(wp_write(&d, &hs[i]), 0);
    Sleep(40);
    EXPECT_EQ(U(waveOutBreakLoop)(d.h), MMSYSERR_NOERROR);
    CHECK(wp_wait_all(&d, hs, 2, 200 + WP_DONE_SLACK));
    EXPECT_EQ(wp_samples(d.h), 2 * WP_MS2FR(100));
    cleanup(&d, hs, 2);
    EXPECT_EQ(wp_close(&d), MMSYSERR_NOERROR);
}

/* BreakLoop before the loop header starts (queued behind a plain buffer): recorded, then pinned. */
TEST(wp_breakloop_before_loop_starts) {
    REQUIRE(U(waveOutBreakLoop));
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    WAVEHDR hs[2];
    wp_hdr_ms(&hs[0], &d, 150);
    wp_hdr_ms(&hs[1], &d, 40);
    hs[1].dwFlags = WHDR_BEGINLOOP | WHDR_ENDLOOP; hs[1].dwLoops = 4;
    for (int i = 0; i < 2; i++) EXPECT_EQ(wp_prep(&d, &hs[i]), 0);
    for (int i = 0; i < 2; i++) EXPECT_EQ(wp_write(&d, &hs[i]), 0);
    Sleep(30);
    EXPECT_EQ(U(waveOutBreakLoop)(d.h), MMSYSERR_NOERROR);
    int ok = wp_wait_all(&d, hs, 2, 310 + WP_DONE_SLACK);
    CHECK(ok);
    if (ok) {
        DWORD s = wp_samples(d.h), loops = (s - WP_MS2FR(150)) / WP_MS2FR(40);
        t_note("breakloop before the loop started: %lu samples = 150 ms + %lu x 40 ms", s, loops);
        EXPECT_EQ(s, WP_MS2FR(150) + 4 * WP_MS2FR(40));
    }
    cleanup(&d, hs, 2);
    EXPECT_EQ(wp_close(&d), MMSYSERR_NOERROR);
}

/* Reset during a loop returns the header DONE immediately and zeroes the position. */
TEST(wp_loop_reset_during_loop) {
    REQUIRE(U(waveOutReset));
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    WAVEHDR wh; wp_hdr_ms(&wh, &d, 50);
    wh.dwFlags = WHDR_BEGINLOOP | WHDR_ENDLOOP; wh.dwLoops = 40;
    EXPECT_EQ(wp_prep(&d, &wh), 0);
    EXPECT_EQ(wp_write(&d, &wh), 0);
    Sleep(150);
    EXPECT_EQ(U(waveOutReset)(d.h), MMSYSERR_NOERROR);
    EXPECT_EQ(wh.dwFlags & (WHDR_DONE | WHDR_INQUEUE), WHDR_DONE);
    EXPECT_EQ(wp_samples(d.h), 0);
    cleanup(&d, &wh, 1);
    EXPECT_EQ(wp_close(&d), MMSYSERR_NOERROR);
}

/* Pause during a loop holds it; restart finishes the remaining iterations. */
TEST(wp_loop_pause_during_loop) {
    REQUIRE(U(waveOutPause));
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    WAVEHDR wh; wp_hdr_ms(&wh, &d, 50);
    wh.dwFlags = WHDR_BEGINLOOP | WHDR_ENDLOOP; wh.dwLoops = 4;
    EXPECT_EQ(wp_prep(&d, &wh), 0);
    EXPECT_EQ(wp_write(&d, &wh), 0);
    Sleep(70);
    EXPECT_EQ(U(waveOutPause)(d.h), MMSYSERR_NOERROR);
    Sleep(250);
    CHECK(!(wh.dwFlags & WHDR_DONE));
    EXPECT_EQ(U(waveOutRestart)(d.h), MMSYSERR_NOERROR);
    int ok = wp_wait_done(&d, &wh, 200 + WP_DONE_SLACK);
    CHECK(ok);
    if (ok) EXPECT_EQ(wp_samples(d.h), 4 * WP_MS2FR(50));
    cleanup(&d, &wh, 1);
    EXPECT_EQ(wp_close(&d), MMSYSERR_NOERROR);
}

/* Loop flags survive prepare/unprepare; the loop header can be replayed. */
TEST(wp_loop_replay_same_header) {
    REQUIRE(U(waveOutBreakLoop));
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    WAVEHDR wh; wp_hdr_ms(&wh, &d, 40);
    wh.dwFlags = WHDR_BEGINLOOP | WHDR_ENDLOOP; wh.dwLoops = 2;
    EXPECT_EQ(wp_prep(&d, &wh), 0);
    for (int round = 1; round <= 2; round++) {
        EXPECT_EQ(wp_write(&d, &wh), 0);
        if (!wp_wait_done(&d, &wh, 80 + WP_DONE_SLACK)) { CHECKF(0, "round %d", round); break; }
        EXPECT_EQ(wp_samples(d.h), round * 2 * WP_MS2FR(40));
        EXPECT_EQ(wh.dwFlags & (WHDR_BEGINLOOP | WHDR_ENDLOOP), WHDR_BEGINLOOP | WHDR_ENDLOOP);
    }
    EXPECT_EQ(wp_unprep(&d, &wh), 0);
    EXPECT_EQ(wh.dwFlags & (WHDR_BEGINLOOP | WHDR_ENDLOOP | WHDR_PREPARED), WHDR_BEGINLOOP | WHDR_ENDLOOP);
    wp_free_hdr(&wh);
    EXPECT_EQ(wp_close(&d), MMSYSERR_NOERROR);
}
