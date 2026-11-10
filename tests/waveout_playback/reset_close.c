#include "wp.h"
/* waveOutReset (WODM_RESET 0x0C), waveOutClose with queued buffers, concurrent handles, open/close churn.
 * winmmbase: Reset validates the handle (5) and forwards; a driver 6 on a "device gone" handle becomes 0 (decomp).
 * Close forwards WODM_CLOSE and returns the driver's nonzero answer with the handle still open (decomp); with queued
 * buffers that answer is WAVERR_STILLPLAYING (33) (VM).
 * VM_TIMEOUT: 240 */

static int queue(wp_dev *d, WAVEHDR *hs, int n, DWORD ms) {
    int ok = 1;
    for (int i = 0; i < n; i++) { wp_hdr_ms(&hs[i], d, ms); if (wp_prep(d, &hs[i])) ok = 0; }
    for (int i = 0; i < n; i++) if (wp_write(d, &hs[i])) ok = 0;
    CHECKF(ok, "prepare/write failed");
    return ok;
}
static void cleanup(wp_dev *d, WAVEHDR *hs, int n) {
    if (d->h) { U(waveOutRestart)(d->h); U(waveOutReset)(d->h); }
    for (int i = 0; i < n; i++) { if (d->h) wp_unprep(d, &hs[i]); wp_free_hdr(&hs[i]); }
}

TEST(wp_reset_idle_returns_0) {
    REQUIRE(U(waveOutReset));
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    EXPECT_EQ(U(waveOutReset)(d.h), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveOutReset)(d.h), MMSYSERR_NOERROR);
    EXPECT_EQ(wp_samples(d.h), 0);
    EXPECT_EQ(wp_close(&d), MMSYSERR_NOERROR);
}

/* Reset while playing: every queued header comes back DONE (INQUEUE clear, PREPARED kept), synchronously. */
static void reset_queued(int n, DWORD ms, DWORD after) {
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    WAVEHDR hs[8];
    if (queue(&d, hs, n, ms)) {
        Sleep(after);
        EXPECT_EQ(U(waveOutReset)(d.h), MMSYSERR_NOERROR);
        for (int i = 0; i < n; i++)
            CHECKF(hs[i].dwFlags == (WHDR_DONE | WHDR_PREPARED), "hdr %d flags 0x%lx right after reset", i, hs[i].dwFlags);
        DWORD s = wp_samples(d.h);
        t_note("position right after reset: %lu samples (played about %lu ms of %lu)", s, after, n * ms);
        EXPECT_EQ(s, 0);
        EXPECT_EQ(wp_bytes(d.h), 0);
        for (int i = 0; i < n; i++) EXPECT_EQ(wp_unprep(&d, &hs[i]), MMSYSERR_NOERROR);
    }
    cleanup(&d, hs, n);
    EXPECT_EQ(wp_close(&d), MMSYSERR_NOERROR);
}
TEST(wp_reset_returns_all_3x100) { REQUIRE(U(waveOutReset)); reset_queued(3, 100, 50); }
TEST(wp_reset_returns_all_8x50) { REQUIRE(U(waveOutReset)); reset_queued(8, 50, 30); }
TEST(wp_reset_returns_all_immediately) { REQUIRE(U(waveOutReset)); reset_queued(4, 100, 0); }
TEST(wp_reset_returns_all_mid_second) { REQUIRE(U(waveOutReset)); reset_queued(3, 100, 150); }

/* WOM_DONE for every reset header, in submission order, before Reset returns. */
static volatile LONG g_n;
static DWORD_PTR g_msg[32], g_p1[32];
static void CALLBACK cb(HWAVEOUT h, UINT msg, DWORD_PTR inst, DWORD_PTR p1, DWORD_PTR p2) {
    LONG i = InterlockedIncrement(&g_n) - 1;
    if (i < 32) { g_msg[i] = msg; g_p1[i] = p1; }
}
TEST(wp_reset_done_callbacks_in_order) {
    REQUIRE(U(waveOutReset)); REQUIRE(U(waveOutOpen));
    WP_NEED_DEV();
    WAVEFORMATEX wf; wp_fmt_std(&wf);
    wp_dev d; memset(&d, 0, sizeof d); d.wf = wf;
    g_n = 0;
    EXPECT_EQ(U(waveOutOpen)(&d.h, WAVE_MAPPER, &wf, (DWORD_PTR)cb, 0, CALLBACK_FUNCTION), MMSYSERR_NOERROR);
    if (!d.h) return;
    WAVEHDR hs[4];
    if (queue(&d, hs, 4, 100)) {
        Sleep(30);
        EXPECT_EQ(U(waveOutReset)(d.h), MMSYSERR_NOERROR);
        LONG n = g_n;
        t_note("callbacks seen when Reset returned: %ld", n);
        EXPECT_EQ(n, 5);   /* VM: every WOM_DONE is delivered before waveOutReset returns */
        DWORD t0 = GetTickCount();
        while (g_n < 5 && GetTickCount() - t0 < 1000) Sleep(5);
        EXPECT_EQ(g_n, 5);
        EXPECT_EQ(g_msg[0], WOM_OPEN);
        for (int i = 0; i < 4 && i + 1 < g_n; i++) {
            EXPECT_EQ(g_msg[i + 1], WOM_DONE);
            EXPECT_EQ(g_p1[i + 1], (DWORD_PTR)&hs[i]);
        }
    }
    cleanup(&d, hs, 4);
    EXPECT_EQ(U(waveOutClose)(d.h), MMSYSERR_NOERROR);
}

/* After reset the handle plays again from position 0. */
TEST(wp_reset_then_write_works) {
    REQUIRE(U(waveOutReset));
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    WAVEHDR hs[3];
    if (queue(&d, hs, 3, 100)) {
        Sleep(60);
        EXPECT_EQ(U(waveOutReset)(d.h), MMSYSERR_NOERROR);
        /* reuse the same (still prepared) headers */
        for (int i = 0; i < 3; i++) EXPECT_EQ(wp_write(&d, &hs[i]), MMSYSERR_NOERROR);
        CHECK(wp_wait_all(&d, hs, 3, 300 + WP_DONE_SLACK));
        EXPECT_EQ(wp_samples(d.h), 3 * WP_MS2FR(100));
    }
    cleanup(&d, hs, 3);
    EXPECT_EQ(wp_close(&d), MMSYSERR_NOERROR);
}

/* Reset after a full drain also zeroes the position. */
TEST(wp_reset_after_drain_zeroes_position) {
    REQUIRE(U(waveOutReset));
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    WAVEHDR hs[2];
    if (queue(&d, hs, 2, 50) && wp_wait_all(&d, hs, 2, 100 + WP_DONE_SLACK)) {
        EXPECT_EQ(wp_samples(d.h), 2 * WP_MS2FR(50));
        EXPECT_EQ(U(waveOutReset)(d.h), MMSYSERR_NOERROR);
        EXPECT_EQ(wp_samples(d.h), 0);
        EXPECT_EQ(hs[0].dwFlags, WHDR_DONE | WHDR_PREPARED);
    }
    cleanup(&d, hs, 2);
    EXPECT_EQ(wp_close(&d), MMSYSERR_NOERROR);
}

/* Reset while paused: the queue is returned; whether the handle stays paused is recorded, then pinned. */
TEST(wp_reset_while_paused) {
    REQUIRE(U(waveOutReset)); REQUIRE(U(waveOutPause));
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    WAVEHDR hs[2];
    if (queue(&d, hs, 2, 100)) {
        Sleep(50);
        EXPECT_EQ(U(waveOutPause)(d.h), MMSYSERR_NOERROR);
        EXPECT_EQ(U(waveOutReset)(d.h), MMSYSERR_NOERROR);
        EXPECT_EQ(hs[0].dwFlags, WHDR_DONE | WHDR_PREPARED);
        EXPECT_EQ(hs[1].dwFlags, WHDR_DONE | WHDR_PREPARED);
        EXPECT_EQ(wp_samples(d.h), 0);
        EXPECT_EQ(wp_write(&d, &hs[0]), MMSYSERR_NOERROR);
        int played = wp_wait_done(&d, &hs[0], 600);
        t_note("after reset of a paused handle a new write %s without restart", played ? "plays" : "is held");
        EXPECT_EQ(played, 1);
        if (!played) {
            EXPECT_EQ(U(waveOutRestart)(d.h), MMSYSERR_NOERROR);
            CHECK(wp_wait_done(&d, &hs[0], 100 + WP_DONE_SLACK));
        }
        EXPECT_EQ(wp_samples(d.h), WP_MS2FR(100));
    }
    cleanup(&d, hs, 2);
    EXPECT_EQ(wp_close(&d), MMSYSERR_NOERROR);
}

/* Paused before any write, reset, then write: recorded alongside the case above. */
TEST(wp_reset_clears_pause_before_write) {
    REQUIRE(U(waveOutReset)); REQUIRE(U(waveOutPause));
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    EXPECT_EQ(U(waveOutPause)(d.h), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveOutReset)(d.h), MMSYSERR_NOERROR);
    WAVEHDR hs[1];
    if (queue(&d, hs, 1, 60)) {
        int played = wp_wait_done(&d, &hs[0], 600);
        t_note("pause, reset (idle), write: %s", played ? "plays" : "held");
        EXPECT_EQ(played, 1);
        if (!played) { U(waveOutRestart)(d.h); wp_wait_done(&d, &hs[0], 60 + WP_DONE_SLACK); }
    }
    cleanup(&d, hs, 1);
    EXPECT_EQ(wp_close(&d), MMSYSERR_NOERROR);
}

/* ---- close ------------------------------------------------------------------------------------------------------ */

TEST(wp_close_while_playing_is_33) {
    REQUIRE(U(waveOutClose));
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    WAVEHDR hs[3];
    if (queue(&d, hs, 3, 100)) {
        MMRESULT r = U(waveOutClose)(d.h);
        EXPECT_EQ(r, WAVERR_STILLPLAYING);
        if (r == MMSYSERR_NOERROR) { d.h = NULL; for (int i = 0; i < 3; i++) wp_free_hdr(&hs[i]); wp_close(&d); return; }
        EXPECT_EQ(U(waveOutClose)(d.h), WAVERR_STILLPLAYING);
        /* still open, still playing: position keeps working and the buffers still complete */
        CHECK(wp_samples(d.h) != 0xFFFFFFFF);
        CHECK(wp_wait_all(&d, hs, 3, 300 + WP_DONE_SLACK));
        EXPECT_EQ(wp_samples(d.h), 3 * WP_MS2FR(100));
        /* drained but still prepared: close now succeeds */
        for (int i = 0; i < 3; i++) EXPECT_EQ(wp_unprep(&d, &hs[i]), MMSYSERR_NOERROR);
    }
    for (int i = 0; i < 3; i++) wp_free_hdr(&hs[i]);
    EXPECT_EQ(U(waveOutClose)(d.h), MMSYSERR_NOERROR);
    d.h = NULL;
    wp_close(&d);
}

TEST(wp_close_after_reset_is_0) {
    REQUIRE(U(waveOutClose)); REQUIRE(U(waveOutReset));
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    WAVEHDR hs[3];
    if (queue(&d, hs, 3, 100)) {
        MMRESULT r = U(waveOutClose)(d.h);
        EXPECT_EQ(r, WAVERR_STILLPLAYING);
        if (r == MMSYSERR_NOERROR) { d.h = NULL; for (int i = 0; i < 3; i++) wp_free_hdr(&hs[i]); wp_close(&d); return; }
        EXPECT_EQ(U(waveOutReset)(d.h), MMSYSERR_NOERROR);
        for (int i = 0; i < 3; i++) EXPECT_EQ(wp_unprep(&d, &hs[i]), MMSYSERR_NOERROR);
    }
    for (int i = 0; i < 3; i++) wp_free_hdr(&hs[i]);
    EXPECT_EQ(U(waveOutClose)(d.h), MMSYSERR_NOERROR);
    d.h = NULL;
    wp_close(&d);
}

/* Done-but-still-prepared headers do not block close (recorded), then pinned. */
TEST(wp_close_with_prepared_done_headers) {
    REQUIRE(U(waveOutClose));
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    WAVEHDR hs[2];
    if (queue(&d, hs, 2, 40) && wp_wait_all(&d, hs, 2, 80 + WP_DONE_SLACK)) {
        MMRESULT r = U(waveOutClose)(d.h);
        t_note("close with 2 done+prepared headers -> %u", r);
        EXPECT_EQ(r, MMSYSERR_NOERROR);
        if (r == MMSYSERR_NOERROR) d.h = NULL;
    }
    cleanup(&d, hs, 2);
    wp_close(&d);
}

/* ---- concurrency ------------------------------------------------------------------------------------------------- */

TEST(wp_two_handles_play_concurrently) {
    REQUIRE(U(waveOutGetPosition));
    WP_NEED_DEV();
    wp_dev a, b;
    if (wp_open(&a)) { CHECK(0); return; }
    if (wp_open(&b)) { CHECK(0); wp_close(&a); return; }
    CHECK(a.h != b.h);
    WAVEHDR ha[2], hb[3];
    queue(&a, ha, 2, 100);
    queue(&b, hb, 3, 50);
    CHECK(wp_wait_all(&a, ha, 2, 200 + WP_DONE_SLACK));
    CHECK(wp_wait_all(&b, hb, 3, 150 + WP_DONE_SLACK));
    EXPECT_EQ(wp_samples(a.h), 2 * WP_MS2FR(100));
    EXPECT_EQ(wp_samples(b.h), 3 * WP_MS2FR(50));
    cleanup(&a, ha, 2); cleanup(&b, hb, 3);
    EXPECT_EQ(wp_close(&a), MMSYSERR_NOERROR);
    EXPECT_EQ(wp_close(&b), MMSYSERR_NOERROR);
}

/* Two handles with different formats, and a reset on one does not disturb the other. */
TEST(wp_two_handles_reset_is_per_handle) {
    REQUIRE(U(waveOutReset));
    WP_NEED_DEV();
    WAVEFORMATEX wf; wp_fmt(&wf, 22050, 1, 16);
    wp_dev a, b;
    if (wp_open(&a)) { CHECK(0); return; }
    if (wp_open_ex(&b, WAVE_MAPPER, &wf)) { CHECK(0); wp_close(&a); return; }
    WAVEHDR ha[3], hb[2];
    queue(&a, ha, 3, 100);
    queue(&b, hb, 2, 100);
    Sleep(30);
    EXPECT_EQ(U(waveOutReset)(a.h), MMSYSERR_NOERROR);
    for (int i = 0; i < 3; i++) EXPECT_EQ(ha[i].dwFlags, WHDR_DONE | WHDR_PREPARED);
    CHECK(!(hb[1].dwFlags & WHDR_DONE));
    CHECK(wp_wait_all(&b, hb, 2, 200 + WP_DONE_SLACK));
    EXPECT_EQ(wp_samples(b.h), 2 * 2205);
    EXPECT_EQ(wp_bytes(b.h), 2 * 2205 * 2);
    EXPECT_EQ(wp_samples(a.h), 0);
    cleanup(&a, ha, 3); cleanup(&b, hb, 2);
    EXPECT_EQ(wp_close(&a), MMSYSERR_NOERROR);
    EXPECT_EQ(wp_close(&b), MMSYSERR_NOERROR);
}

/* Four handles at once, all complete with exact positions. */
TEST(wp_four_handles_play_concurrently) {
    WP_NEED_DEV();
    wp_dev d[4];
    WAVEHDR hs[4][2];
    int open = 0;
    for (int i = 0; i < 4; i++) {
        if (wp_open(&d[i])) { CHECKF(0, "open %d", i); break; }
        open++;
    }
    for (int i = 0; i < open; i++) queue(&d[i], hs[i], 2, 40 + 20 * i);
    for (int i = 0; i < open; i++) {
        CHECKF(wp_wait_all(&d[i], hs[i], 2, 2 * (40 + 20 * i) + WP_DONE_SLACK), "handle %d", i);
        EXPECT_EQ(wp_samples(d[i].h), 2 * WP_MS2FR(40 + 20 * i));
    }
    for (int i = 0; i < open; i++) { cleanup(&d[i], hs[i], 2); EXPECT_EQ(wp_close(&d[i]), MMSYSERR_NOERROR); }
}

/* A second thread calls Pause/Restart/GetPosition on the handle while the first waits for completion. */
static wp_dev *g_td;
static DWORD WINAPI ctl_thread(LPVOID p) {
    for (int i = 0; i < 3; i++) {
        Sleep(30);
        if (U(waveOutPause)(g_td->h)) return 1;
        if (wp_samples(g_td->h) == 0xFFFFFFFF) return 2;
        Sleep(20);
        if (U(waveOutRestart)(g_td->h)) return 3;
    }
    return 0;
}
TEST(wp_pause_restart_from_other_thread) {
    REQUIRE(U(waveOutPause));
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    WAVEHDR hs[2];
    if (queue(&d, hs, 2, 150)) {
        g_td = &d;
        HANDLE th = CreateThread(NULL, 0, ctl_thread, NULL, 0, NULL);
        CHECK(th != NULL);
        if (th) {
            EXPECT_EQ(WaitForSingleObject(th, 5000), WAIT_OBJECT_0);
            DWORD code = 99; GetExitCodeThread(th, &code); CloseHandle(th);
            EXPECT_EQ(code, 0);
        }
        CHECK(wp_wait_all(&d, hs, 2, 300 + WP_DONE_SLACK));
        EXPECT_EQ(wp_samples(d.h), 2 * WP_MS2FR(150));
    }
    cleanup(&d, hs, 2);
    EXPECT_EQ(wp_close(&d), MMSYSERR_NOERROR);
}

/* ---- churn ------------------------------------------------------------------------------------------------------- */

static DWORD handle_count(void) {
    DWORD n = 0;
    if (U(GetProcessHandleCount)) U(GetProcessHandleCount)(GetCurrentProcess(), &n);
    return n;
}

TEST(wp_open_close_20_no_leak) {
    REQUIRE(U(GetProcessHandleCount));
    WP_NEED_DEV();
    wp_dev d;
    /* warm-up: the first open of a process creates the long-lived plumbing */
    for (int i = 0; i < 2; i++) if (!wp_open(&d)) wp_close(&d);
    DWORD before = handle_count();
    int fails = 0;
    for (int i = 0; i < 20; i++) {
        if (wp_open(&d)) { fails++; continue; }
        if (wp_close(&d) != MMSYSERR_NOERROR) fails++;
    }
    DWORD after = handle_count();
    t_note("handles before %lu after %lu", before, after);
    EXPECT_EQ(fails, 0);
    CHECKF(after <= before + 4, "handle count grew %lu -> %lu over 20 open/close", before, after);
}

TEST(wp_open_play_reset_close_20_no_leak) {
    REQUIRE(U(GetProcessHandleCount));
    WP_NEED_DEV();
    wp_dev d;
    for (int i = 0; i < 2; i++) if (!wp_open(&d)) wp_close(&d);
    DWORD before = handle_count();
    int fails = 0;
    for (int i = 0; i < 20; i++) {
        if (wp_open(&d)) { fails++; continue; }
        WAVEHDR wh; wp_hdr_ms(&wh, &d, 20);
        if (wp_prep(&d, &wh) || wp_write(&d, &wh)) fails++;
        if (i & 1) { if (!wp_wait_done(&d, &wh, 20 + WP_DONE_SLACK)) fails++; }
        if (U(waveOutReset)(d.h)) fails++;
        if (!(wh.dwFlags & WHDR_DONE)) fails++;
        if (wp_unprep(&d, &wh)) fails++;
        wp_free_hdr(&wh);
        if (wp_close(&d) != MMSYSERR_NOERROR) fails++;
    }
    DWORD after = handle_count();
    t_note("handles before %lu after %lu", before, after);
    EXPECT_EQ(fails, 0);
    CHECKF(after <= before + 4, "handle count grew %lu -> %lu", before, after);
}

/* The CALLBACK_EVENT target is not closed or leaked by winmm: still a valid event after close. */
TEST(wp_close_leaves_callback_event_alone) {
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    HANDLE ev = d.ev;
    EXPECT_EQ(U(waveOutClose)(d.h), MMSYSERR_NOERROR);
    /* WOM_CLOSE signalled the event */
    EXPECT_EQ(WaitForSingleObject(ev, 1000), WAIT_OBJECT_0);
    CHECK(SetEvent(ev));
    CloseHandle(ev);
}
