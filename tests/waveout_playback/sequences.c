#include "wp.h"
/* State sequences across Pause / Restart / Reset / BreakLoop / GetPosition, and the cbmmt / wType table on a live
 * handle. Expectations pinned from the Win10 VM (driver behaviour; winmmbase only forwards, see contract.c).
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

/* ---- cbmmt x wType on a live idle handle: a short cbmmt is MMSYSERR_ERROR and nothing is written (VM) ---------- */

static void short_cb_type(UINT type) {
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    for (UINT cb = 0; cb < sizeof(MMTIME); cb++) {
        unsigned char buf[sizeof(MMTIME)];
        memset(buf, 0x5A, sizeof buf);
        ((MMTIME *)buf)->wType = type;
        MMRESULT r = U(waveOutGetPosition)(d.h, (MMTIME *)buf, cb);
        CHECKF(r == MMSYSERR_ERROR, "type 0x%x cb %u -> %u", type, cb, r);
        CHECKF(((MMTIME *)buf)->wType == type, "type 0x%x cb %u: wType now 0x%x", type, cb, ((MMTIME *)buf)->wType);
        CHECKF(((MMTIME *)buf)->u.cb == 0x5A5A5A5A, "type 0x%x cb %u: value written", type, cb);
    }
    /* the full size works right after */
    DWORD v = 1; UINT t = 0;
    EXPECT_EQ(wp_pos(d.h, type, &v, &t), MMSYSERR_NOERROR);
    EXPECT_EQ(v, 0);
    EXPECT_EQ(t, type == TIME_SAMPLES ? TIME_SAMPLES : TIME_BYTES);
    EXPECT_EQ(wp_close(&d), MMSYSERR_NOERROR);
}
TEST(wp_seq_short_cb_ms) { REQUIRE(U(waveOutGetPosition)); short_cb_type(TIME_MS); }
TEST(wp_seq_short_cb_samples) { REQUIRE(U(waveOutGetPosition)); short_cb_type(TIME_SAMPLES); }
TEST(wp_seq_short_cb_bytes) { REQUIRE(U(waveOutGetPosition)); short_cb_type(TIME_BYTES); }
TEST(wp_seq_short_cb_smpte) { REQUIRE(U(waveOutGetPosition)); short_cb_type(TIME_SMPTE); }
TEST(wp_seq_short_cb_midi) { REQUIRE(U(waveOutGetPosition)); short_cb_type(TIME_MIDI); }
TEST(wp_seq_short_cb_ticks) { REQUIRE(U(waveOutGetPosition)); short_cb_type(TIME_TICKS); }

/* Oversized cbmmt is accepted and only sizeof(MMTIME) bytes are written. */
TEST(wp_seq_large_cb_writes_12_bytes) {
    REQUIRE(U(waveOutGetPosition));
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    static const UINT cbs[] = { 12, 13, 16, 24, 100, 4096 };
    for (int i = 0; i < 6; i++) {
        unsigned char buf[4096];
        memset(buf, 0x3C, sizeof buf);
        ((MMTIME *)buf)->wType = TIME_SAMPLES;
        EXPECT_EQ(U(waveOutGetPosition)(d.h, (MMTIME *)buf, cbs[i]), MMSYSERR_NOERROR);
        EXPECT_EQ(((MMTIME *)buf)->wType, TIME_SAMPLES);
        EXPECT_EQ(((MMTIME *)buf)->u.sample, 0);
        int dirty = 0;
        for (int k = 12; k < (int)sizeof buf; k++) if (buf[k] != 0x3C) dirty++;
        CHECKF(dirty == 0, "cb %u: %d bytes past the MMTIME written", cbs[i], dirty);
    }
    EXPECT_EQ(wp_close(&d), MMSYSERR_NOERROR);
}

/* The rewrite only touches wType and the first DWORD of the union: SMPTE bytes 8..11 stay as they were. */
TEST(wp_seq_smpte_rewrite_leaves_tail) {
    REQUIRE(U(waveOutGetPosition));
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    MMTIME mt; memset(&mt, 0x77, sizeof mt); mt.wType = TIME_SMPTE;
    EXPECT_EQ(U(waveOutGetPosition)(d.h, &mt, sizeof mt), MMSYSERR_NOERROR);
    EXPECT_EQ(mt.wType, TIME_BYTES);
    EXPECT_EQ(mt.u.cb, 0);
    t_note("SMPTE tail after rewrite: fps 0x%02x dummy 0x%02x pad 0x%02x%02x", mt.u.smpte.fps, mt.u.smpte.dummy,
           mt.u.smpte.pad[0], mt.u.smpte.pad[1]);
    EXPECT_EQ(mt.u.smpte.fps, 0x77);
    EXPECT_EQ(mt.u.smpte.pad[1], 0x77);
    EXPECT_EQ(wp_close(&d), MMSYSERR_NOERROR);
}

/* ---- sequences --------------------------------------------------------------------------------------------------- */

/* Pause, Reset (paused, empty), Restart, write: plays. */
TEST(wp_seq_pause_reset_restart_write) {
    REQUIRE(U(waveOutPause));
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    EXPECT_EQ(U(waveOutPause)(d.h), 0);
    EXPECT_EQ(U(waveOutReset)(d.h), 0);
    EXPECT_EQ(U(waveOutRestart)(d.h), 0);
    WAVEHDR hs[1];
    if (queue(&d, hs, 1, 50)) {
        CHECK(wp_wait_done(&d, &hs[0], 50 + WP_DONE_SLACK));
        EXPECT_EQ(wp_samples(d.h), WP_MS2FR(50));
    }
    cleanup(&d, hs, 1);
    EXPECT_EQ(wp_close(&d), 0);
}

/* Restart on a paused handle with an empty queue, then write: plays without another restart. */
TEST(wp_seq_pause_restart_empty_then_write) {
    REQUIRE(U(waveOutPause));
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    EXPECT_EQ(U(waveOutPause)(d.h), 0);
    EXPECT_EQ(U(waveOutRestart)(d.h), 0);
    WAVEHDR hs[1];
    if (queue(&d, hs, 1, 50)) {
        CHECK(wp_wait_done(&d, &hs[0], 50 + WP_DONE_SLACK));
        EXPECT_EQ(wp_samples(d.h), WP_MS2FR(50));
    }
    cleanup(&d, hs, 1);
    EXPECT_EQ(wp_close(&d), 0);
}

/* Reset twice in a row while playing: the second is a no-op success. */
TEST(wp_seq_reset_twice_while_playing) {
    REQUIRE(U(waveOutReset));
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    WAVEHDR hs[2];
    if (queue(&d, hs, 2, 100)) {
        Sleep(20);
        EXPECT_EQ(U(waveOutReset)(d.h), 0);
        EXPECT_EQ(U(waveOutReset)(d.h), 0);
        EXPECT_EQ(hs[0].dwFlags, WHDR_DONE | WHDR_PREPARED);
        EXPECT_EQ(hs[1].dwFlags, WHDR_DONE | WHDR_PREPARED);
        EXPECT_EQ(wp_samples(d.h), 0);
    }
    cleanup(&d, hs, 2);
    EXPECT_EQ(wp_close(&d), 0);
}

/* Write, Reset, Write, Reset, ... ten rounds on one handle: every round returns its header DONE. */
TEST(wp_seq_write_reset_rounds) {
    REQUIRE(U(waveOutReset));
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    WAVEHDR wh; wp_hdr_ms(&wh, &d, 100);
    EXPECT_EQ(wp_prep(&d, &wh), 0);
    int bad = 0;
    for (int i = 0; i < 10; i++) {
        if (wp_write(&d, &wh)) bad++;
        if (!(wh.dwFlags & WHDR_INQUEUE)) bad++;
        if (U(waveOutReset)(d.h)) bad++;
        if (wh.dwFlags != (WHDR_DONE | WHDR_PREPARED)) bad++;
        if (wp_samples(d.h) != 0) bad++;
    }
    EXPECT_EQ(bad, 0);
    /* and it still plays to the end afterwards */
    EXPECT_EQ(wp_write(&d, &wh), 0);
    CHECK(wp_wait_done(&d, &wh, 100 + WP_DONE_SLACK));
    EXPECT_EQ(wp_samples(d.h), WP_MS2FR(100));
    cleanup(&d, &wh, 1);
    EXPECT_EQ(wp_close(&d), 0);
}

/* Reset while paused keeps nothing queued: unprepare of every header succeeds right away. */
TEST(wp_seq_paused_reset_unprepare_all) {
    REQUIRE(U(waveOutReset));
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    EXPECT_EQ(U(waveOutPause)(d.h), 0);
    WAVEHDR hs[5];
    if (queue(&d, hs, 5, 30)) {
        for (int i = 0; i < 5; i++) EXPECT_EQ(wp_unprep(&d, &hs[i]), WAVERR_STILLPLAYING);
        EXPECT_EQ(U(waveOutReset)(d.h), 0);
        for (int i = 0; i < 5; i++) {
            EXPECT_EQ(wp_unprep(&d, &hs[i]), 0);
            EXPECT_EQ(hs[i].dwFlags, WHDR_DONE);
        }
    }
    cleanup(&d, hs, 5);
    EXPECT_EQ(wp_close(&d), 0);
}

/* After close-refused (33), Pause/Restart/BreakLoop/GetPosition still work on the handle. */
TEST(wp_seq_after_close_refused_handle_alive) {
    REQUIRE(U(waveOutClose));
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    WAVEHDR hs[2];
    if (queue(&d, hs, 2, 100)) {
        MMRESULT r = U(waveOutClose)(d.h);
        EXPECT_EQ(r, WAVERR_STILLPLAYING);
        if (r == 0) { d.h = NULL; for (int i = 0; i < 2; i++) wp_free_hdr(&hs[i]); wp_close(&d); return; }
        EXPECT_EQ(U(waveOutPause)(d.h), 0);
        EXPECT_EQ(U(waveOutBreakLoop)(d.h), 0);
        CHECK(wp_samples(d.h) != 0xFFFFFFFF);
        EXPECT_EQ(U(waveOutRestart)(d.h), 0);
        CHECK(wp_wait_all(&d, hs, 2, 200 + WP_DONE_SLACK));
        EXPECT_EQ(wp_samples(d.h), 2 * WP_MS2FR(100));
    }
    cleanup(&d, hs, 2);
    EXPECT_EQ(wp_close(&d), 0);
}

/* After a successful close every one of the five returns INVALHANDLE (and NULL pmmt still 11 first). */
TEST(wp_seq_after_close_all_invalid) {
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    WAVEHDR hs[1];
    if (queue(&d, hs, 1, 30)) wp_wait_done(&d, &hs[0], 30 + WP_DONE_SLACK);
    HWAVEOUT h = d.h;
    cleanup(&d, hs, 1);
    EXPECT_EQ(wp_close(&d), 0);
    MMTIME mt; mt.wType = TIME_SAMPLES;
    EXPECT_EQ(U(waveOutPause)(h), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveOutRestart)(h), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveOutReset)(h), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveOutBreakLoop)(h), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveOutGetPosition)(h, &mt, sizeof mt), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveOutGetPosition)(h, NULL, sizeof mt), MMSYSERR_INVALPARAM);
}

/* Position is per handle: a second handle opened mid-playback of the first starts at 0. */
TEST(wp_seq_position_per_handle) {
    WP_NEED_DEV();
    wp_dev a, b;
    if (wp_open(&a)) { CHECK(0); return; }
    WAVEHDR ha[1];
    if (queue(&a, ha, 1, 200)) {
        Sleep(80);
        if (wp_open(&b) == 0) {
            EXPECT_EQ(wp_samples(b.h), 0);
            EXPECT_EQ(wp_bytes(b.h), 0);
            CHECK(wp_samples(a.h) > 0);
            EXPECT_EQ(wp_close(&b), 0);
        } else CHECK(0);
        CHECK(wp_wait_done(&a, &ha[0], 200 + WP_DONE_SLACK));
        EXPECT_EQ(wp_samples(a.h), WP_MS2FR(200));
    }
    cleanup(&a, ha, 1);
    EXPECT_EQ(wp_close(&a), 0);
}

/* Position does not move between the last DONE and later reads (no drift on an idle handle). */
TEST(wp_seq_idle_position_stable) {
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    WAVEHDR hs[1];
    if (queue(&d, hs, 1, 50) && wp_wait_done(&d, &hs[0], 50 + WP_DONE_SLACK)) {
        DWORD p1 = wp_samples(d.h);
        Sleep(250);
        EXPECT_EQ(wp_samples(d.h), p1);
        EXPECT_EQ(p1, WP_MS2FR(50));
    }
    cleanup(&d, hs, 1);
    EXPECT_EQ(wp_close(&d), 0);
}

/* Pause on an idle drained handle: the position stays and Restart leaves it unchanged. */
TEST(wp_seq_pause_idle_after_drain_position) {
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    WAVEHDR hs[1];
    if (queue(&d, hs, 1, 40) && wp_wait_done(&d, &hs[0], 40 + WP_DONE_SLACK)) {
        EXPECT_EQ(U(waveOutPause)(d.h), 0);
        EXPECT_EQ(wp_samples(d.h), WP_MS2FR(40));
        EXPECT_EQ(U(waveOutRestart)(d.h), 0);
        EXPECT_EQ(wp_samples(d.h), WP_MS2FR(40));
    }
    cleanup(&d, hs, 1);
    EXPECT_EQ(wp_close(&d), 0);
}

/* Many tiny buffers (5 ms) complete in order with an exact total. */
TEST(wp_seq_many_tiny_buffers_in_order) {
    WP_NEED_DEV();
    wp_dev d;
    if (wp_open(&d)) { CHECK(0); return; }
    enum { N = 32 };
    WAVEHDR hs[N];
    if (queue(&d, hs, N, 5)) {
        int bad = 0;
        DWORD t0 = GetTickCount();
        while (!(hs[N - 1].dwFlags & WHDR_DONE) && GetTickCount() - t0 < 160 + WP_DONE_SLACK) {
            /* scan from the last header back: a later DONE seen first implies every earlier one is DONE */
            int later_done = 0;
            for (int i = N - 1; i >= 0; i--) {
                int dn = (hs[i].dwFlags & WHDR_DONE) != 0;
                if (!dn && later_done) bad++;
                later_done |= dn;
            }
            Sleep(1);
        }
        EXPECT_EQ(bad, 0);
        CHECK(wp_wait_all(&d, hs, N, 1000));
        EXPECT_EQ(wp_samples(d.h), N * WP_MS2FR(5));
    }
    cleanup(&d, hs, N);
    EXPECT_EQ(wp_close(&d), 0);
}
