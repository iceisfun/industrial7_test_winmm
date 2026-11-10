/* waveout_header_protocol: longer sequences - header arrays with mixed states, several handles, worker threads,
 * caller-forged flag states on a live handle (the header checks never ask the driver), and a concurrency check of
 * the headless gate. Silence only, bounded waits.
 */
#include "whp.h"

/* ---- caller-forged states on a live handle: decided by winmmbase from dwFlags alone ----------------------------- */

TEST(whp_seq_forged_inqueue_on_live_handle)
{
    HWAVEOUT h; HANDLE ev; if (!whp_need_dev(&h, &ev)) return;
    DWORD len; char *buf = whp_silence(50, &len);
    WAVEHDR w; whp_hdr_init(&w, 0, buf, len);
    EXPECT_EQ(U(waveOutPrepareHeader)(h, &w, sizeof w), MMSYSERR_NOERROR);
    w.dwFlags |= WHDR_INQUEUE;                     /* not really queued */
    EXPECT_EQ(U(waveOutWrite)(h, &w, sizeof w), WAVERR_STILLPLAYING);
    EXPECT_EQ(U(waveOutUnprepareHeader)(h, &w, sizeof w), WAVERR_STILLPLAYING);
    EXPECT_EQ(U(waveOutPrepareHeader)(h, &w, sizeof w), MMSYSERR_NOERROR);
    EXPECT_EQ(w.dwFlags, WHDR_PREPARED | WHDR_INQUEUE);
    w.dwFlags &= ~WHDR_INQUEUE;
    EXPECT_EQ(U(waveOutUnprepareHeader)(h, &w, sizeof w), MMSYSERR_NOERROR);
    EXPECT_EQ(w.dwFlags, 0);
    whp_close(h, ev);
}

TEST(whp_seq_forged_unprepared_on_live_handle)
{
    /* clearing PREPARED by hand makes Write answer 34 and Unprepare answer 0 without the driver */
    HWAVEOUT h; HANDLE ev; if (!whp_need_dev(&h, &ev)) return;
    DWORD len; char *buf = whp_silence(50, &len);
    WAVEHDR w; whp_hdr_init(&w, 0, buf, len);
    EXPECT_EQ(U(waveOutPrepareHeader)(h, &w, sizeof w), MMSYSERR_NOERROR);
    w.dwFlags &= ~WHDR_PREPARED;
    EXPECT_EQ(U(waveOutWrite)(h, &w, sizeof w), WAVERR_UNPREPARED);
    EXPECT_EQ(U(waveOutUnprepareHeader)(h, &w, sizeof w), MMSYSERR_NOERROR);
    EXPECT_EQ(w.dwFlags, 0);
    w.dwFlags |= WHDR_PREPARED;                    /* restore and unprepare properly */
    EXPECT_EQ(U(waveOutUnprepareHeader)(h, &w, sizeof w), MMSYSERR_NOERROR);
    EXPECT_EQ(w.dwFlags, 0);
    whp_close(h, ev);
}

TEST(whp_seq_unprepare_keeps_other_bits)
{
    /* Unprepare clears only PREPARED: DONE and the loop bits survive */
    HWAVEOUT h; HANDLE ev; if (!whp_need_dev(&h, &ev)) return;
    DWORD len; char *buf = whp_silence(50, &len);
    static const DWORD extra[] = { WHDR_DONE, WHDR_BEGINLOOP, WHDR_ENDLOOP, WHDR_DONE | WHDR_BEGINLOOP | WHDR_ENDLOOP };
    for (int i = 0; i < 4; i++) {
        WAVEHDR w; whp_hdr_init(&w, 0, buf, len);
        EXPECT_EQ(U(waveOutPrepareHeader)(h, &w, sizeof w), MMSYSERR_NOERROR);
        w.dwFlags |= extra[i];
        EXPECT_EQ(U(waveOutUnprepareHeader)(h, &w, sizeof w), MMSYSERR_NOERROR);
        CHECKF(w.dwFlags == extra[i], "unprepare kept 0x%lx want 0x%lx", w.dwFlags, extra[i]);
    }
    whp_close(h, ev);
}

/* ---- arrays with mixed states --------------------------------------------------------------------------------- */

TEST(whp_seq_mixed_array_write)
{
    HWAVEOUT h; HANDLE ev; if (!whp_need_dev(&h, &ev)) return;
    DWORD len; char *buf = whp_silence(600, &len);
    WAVEHDR w[4];
    for (int i = 0; i < 4; i++) whp_hdr_init(&w[i], 0, buf, len);
    EXPECT_EQ(U(waveOutPrepareHeader)(h, &w[0], sizeof w[0]), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveOutPrepareHeader)(h, &w[1], sizeof w[1]), MMSYSERR_NOERROR);
    /* w[2] stays unprepared, w[3] gets a bad bit */
    w[3].dwFlags = 0x40;
    EXPECT_EQ(U(waveOutWrite)(h, &w[0], sizeof w[0]), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveOutWrite)(h, &w[0], sizeof w[0]), WAVERR_STILLPLAYING);
    EXPECT_EQ(U(waveOutWrite)(h, &w[2], sizeof w[2]), WAVERR_UNPREPARED);
    EXPECT_EQ(U(waveOutWrite)(h, &w[3], sizeof w[3]), MMSYSERR_INVALPARAM);
    EXPECT_EQ(U(waveOutWrite)(h, &w[1], sizeof w[1]), MMSYSERR_NOERROR);
    EXPECT_EQ(w[0].dwFlags, WHDR_PREPARED | WHDR_INQUEUE);
    EXPECT_EQ(w[1].dwFlags, WHDR_PREPARED | WHDR_INQUEUE);
    EXPECT_EQ(w[2].dwFlags, 0);
    EXPECT_EQ(w[3].dwFlags, 0x40);
    EXPECT_EQ(U(waveOutReset)(h), MMSYSERR_NOERROR);
    CHECK(whp_wait_all_done(w, 2, ev, WHP_WAIT_MS));
    EXPECT_EQ(w[0].dwFlags, WHDR_DONE | WHDR_PREPARED);
    EXPECT_EQ(w[1].dwFlags, WHDR_DONE | WHDR_PREPARED);
    EXPECT_EQ(U(waveOutUnprepareHeader)(h, &w[0], sizeof w[0]), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveOutUnprepareHeader)(h, &w[1], sizeof w[1]), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveOutUnprepareHeader)(h, &w[2], sizeof w[2]), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveOutUnprepareHeader)(h, &w[3], sizeof w[3]), MMSYSERR_INVALPARAM);
    whp_close(h, ev);
}

TEST(whp_seq_completion_order_by_polling)
{
    /* a later header is never DONE while an earlier one of the same handle is still queued */
    HWAVEOUT h; HANDLE ev; if (!whp_need_dev(&h, &ev)) return;
    DWORD len; char *buf = whp_silence(40, &len);
    WAVEHDR w[5];
    for (int i = 0; i < 5; i++) {
        whp_hdr_init(&w[i], 0, buf, len);
        EXPECT_EQ(U(waveOutPrepareHeader)(h, &w[i], sizeof w[i]), MMSYSERR_NOERROR);
    }
    for (int i = 0; i < 5; i++) EXPECT_EQ(U(waveOutWrite)(h, &w[i], sizeof w[i]), MMSYSERR_NOERROR);
    int violations = 0;
    DWORD t0 = GetTickCount();
    while (GetTickCount() - t0 < WHP_WAIT_MS) {
        DWORD f[5]; int all = 1;
        for (int i = 0; i < 5; i++) f[i] = ((volatile WAVEHDR *)&w[i])->dwFlags;
        for (int i = 1; i < 5; i++) if ((f[i] & WHDR_DONE) && !(f[i - 1] & WHDR_DONE)) violations++;
        for (int i = 0; i < 5; i++) if (!(f[i] & WHDR_DONE)) all = 0;
        if (all) break;
        Sleep(1);
    }
    CHECK(whp_wait_all_done(w, 5, ev, 10));
    EXPECT_EQ(violations, 0);
    for (int i = 0; i < 5; i++) EXPECT_EQ(U(waveOutUnprepareHeader)(h, &w[i], sizeof w[i]), MMSYSERR_NOERROR);
    whp_close(h, ev);
}

TEST(whp_seq_event_signalled_per_done)
{
    HWAVEOUT h; HANDLE ev; if (!whp_need_dev(&h, &ev)) return;
    DWORD len; char *buf = whp_silence(60, &len);
    WAVEHDR w; whp_hdr_init(&w, 0, buf, len);
    EXPECT_EQ(U(waveOutPrepareHeader)(h, &w, sizeof w), MMSYSERR_NOERROR);
    for (int round = 0; round < 3; round++) {
        ResetEvent(ev);
        EXPECT_EQ(U(waveOutWrite)(h, &w, sizeof w), MMSYSERR_NOERROR);
        DWORD wr = WaitForSingleObject(ev, WHP_WAIT_MS);
        EXPECT_EQ(wr, WAIT_OBJECT_0);
        CHECK(whp_wait_done(&w, NULL, WHP_WAIT_MS));
        EXPECT_EQ(w.dwFlags, WHDR_DONE | WHDR_PREPARED);
    }
    EXPECT_EQ(U(waveOutUnprepareHeader)(h, &w, sizeof w), MMSYSERR_NOERROR);
    whp_close(h, ev);
}

/* ---- several handles ------------------------------------------------------------------------------------------ */

TEST(whp_seq_two_handles_independent)
{
    HWAVEOUT h; HANDLE ev; if (!whp_need_dev(&h, &ev)) return;
    HWAVEOUT h2 = NULL; HANDLE ev2 = NULL;
    MMRESULT r = whp_open(WAVE_MAPPER, &h2, &ev2);
    EXPECT_EQ(r, MMSYSERR_NOERROR);
    if (r) { whp_close(h, ev); if (ev2) CloseHandle(ev2); return; }
    DWORD len; char *buf = whp_silence(80, &len);
    WAVEHDR a, b; whp_hdr_init(&a, 0, buf, len); whp_hdr_init(&b, 0, buf, len);
    EXPECT_EQ(U(waveOutPrepareHeader)(h, &a, sizeof a), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveOutPrepareHeader)(h2, &b, sizeof b), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveOutWrite)(h, &a, sizeof a), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveOutWrite)(h2, &b, sizeof b), MMSYSERR_NOERROR);
    CHECK(whp_wait_done(&a, ev, WHP_WAIT_MS));
    CHECK(whp_wait_done(&b, ev2, WHP_WAIT_MS));
    EXPECT_EQ(a.dwFlags, WHDR_DONE | WHDR_PREPARED);
    EXPECT_EQ(b.dwFlags, WHDR_DONE | WHDR_PREPARED);
    EXPECT_EQ(U(waveOutUnprepareHeader)(h, &a, sizeof a), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveOutUnprepareHeader)(h2, &b, sizeof b), MMSYSERR_NOERROR);
    whp_close(h2, ev2);
    whp_close(h, ev);
}

TEST(whp_seq_reset_one_handle_only)
{
    HWAVEOUT h; HANDLE ev; if (!whp_need_dev(&h, &ev)) return;
    HWAVEOUT h2 = NULL; HANDLE ev2 = NULL;
    MMRESULT r = whp_open(WAVE_MAPPER, &h2, &ev2);
    EXPECT_EQ(r, MMSYSERR_NOERROR);
    if (r) { whp_close(h, ev); if (ev2) CloseHandle(ev2); return; }
    DWORD len; char *buf = whp_silence(1200, &len);
    WAVEHDR a, b; whp_hdr_init(&a, 0, buf, len); whp_hdr_init(&b, 0, buf, len);
    EXPECT_EQ(U(waveOutPrepareHeader)(h, &a, sizeof a), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveOutPrepareHeader)(h2, &b, sizeof b), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveOutWrite)(h, &a, sizeof a), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveOutWrite)(h2, &b, sizeof b), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveOutReset)(h), MMSYSERR_NOERROR);
    EXPECT_EQ(a.dwFlags, WHDR_DONE | WHDR_PREPARED);
    EXPECT_EQ(b.dwFlags, WHDR_PREPARED | WHDR_INQUEUE);     /* 1.2 s buffer of the other handle still queued */
    EXPECT_EQ(U(waveOutUnprepareHeader)(h2, &b, sizeof b), WAVERR_STILLPLAYING);
    EXPECT_EQ(U(waveOutReset)(h2), MMSYSERR_NOERROR);
    CHECK(whp_wait_done(&b, ev2, WHP_WAIT_MS));
    EXPECT_EQ(U(waveOutUnprepareHeader)(h, &a, sizeof a), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveOutUnprepareHeader)(h2, &b, sizeof b), MMSYSERR_NOERROR);
    whp_close(h2, ev2);
    whp_close(h, ev);
}

/* ---- threads -------------------------------------------------------------------------------------------------- */

typedef struct { HWAVEOUT h; WAVEHDR *w; MMRESULT r; } whp_thr_arg;
static DWORD WINAPI whp_write_thread(void *p)
{
    whp_thr_arg *a = p;
    a->r = U(waveOutWrite)(a->h, a->w, sizeof *a->w);
    return 0;
}

TEST(whp_seq_write_from_worker_thread)
{
    HWAVEOUT h; HANDLE ev; if (!whp_need_dev(&h, &ev)) return;
    DWORD len; char *buf = whp_silence(80, &len);
    WAVEHDR w; whp_hdr_init(&w, 0, buf, len);
    EXPECT_EQ(U(waveOutPrepareHeader)(h, &w, sizeof w), MMSYSERR_NOERROR);
    whp_thr_arg a = { h, &w, 0xFFFF };
    HANDLE t = CreateThread(NULL, 0, whp_write_thread, &a, 0, NULL);
    CHECK(t != NULL);
    if (t) { EXPECT_EQ(WaitForSingleObject(t, WHP_WAIT_MS), WAIT_OBJECT_0); CloseHandle(t); }
    EXPECT_EQ(a.r, MMSYSERR_NOERROR);
    CHECK(whp_wait_done(&w, ev, WHP_WAIT_MS));
    EXPECT_EQ(U(waveOutUnprepareHeader)(h, &w, sizeof w), MMSYSERR_NOERROR);
    whp_close(h, ev);
}

TEST(whp_seq_race_same_header_two_threads)
{
    /* two threads write the same prepared header: exactly one 0 and one 33 is the classic outcome; record it */
    HWAVEOUT h; HANDLE ev; if (!whp_need_dev(&h, &ev)) return;
    DWORD len; char *buf = whp_silence(800, &len);
    WAVEHDR w; whp_hdr_init(&w, 0, buf, len);
    EXPECT_EQ(U(waveOutPrepareHeader)(h, &w, sizeof w), MMSYSERR_NOERROR);
    whp_thr_arg a = { h, &w, 0xFFFF }, b = { h, &w, 0xFFFF };
    HANDLE t1 = CreateThread(NULL, 0, whp_write_thread, &a, 0, NULL);
    HANDLE t2 = CreateThread(NULL, 0, whp_write_thread, &b, 0, NULL);
    if (t1) { WaitForSingleObject(t1, WHP_WAIT_MS); CloseHandle(t1); }
    if (t2) { WaitForSingleObject(t2, WHP_WAIT_MS); CloseHandle(t2); }
    t_note("concurrent writes of one header: %u / %u", a.r, b.r);
    CHECK(a.r == 0 || b.r == 0);
    CHECK((a.r == 0 || a.r == WAVERR_STILLPLAYING) && (b.r == 0 || b.r == WAVERR_STILLPLAYING));
    EXPECT_EQ(U(waveOutReset)(h), MMSYSERR_NOERROR);
    CHECK(whp_wait_done(&w, ev, WHP_WAIT_MS));
    EXPECT_EQ(U(waveOutUnprepareHeader)(h, &w, sizeof w), MMSYSERR_NOERROR);
    whp_close(h, ev);
}

typedef struct { int fn; int bad; } whp_gate_thr;
static DWORD WINAPI whp_gate_thread(void *p)
{
    whp_gate_thr *g = p;
    whp_hdr_fn f = whp_fn(g->fn);
    char buf[8] = {0};
    for (int i = 0; i < 2000; i++) {
        WAVEHDR w; whp_hdr_init(&w, (i & 1) ? 0x80 : WHDR_INQUEUE | WHDR_PREPARED, buf, sizeof buf);
        MMRESULT r = f(NULL, &w, sizeof w);
        MMRESULT want = (i & 1) ? MMSYSERR_INVALPARAM : (g->fn == WHP_PREP ? MMSYSERR_NOERROR : WAVERR_STILLPLAYING);
        if (r != want) g->bad++;
    }
    return 0;
}

TEST(whp_seq_headless_gate_concurrent)
{
    REQUIRE(whp_have_all());
    whp_gate_thr g[6]; HANDLE t[6];
    for (int i = 0; i < 6; i++) { g[i].fn = i % 3; g[i].bad = 0; t[i] = CreateThread(NULL, 0, whp_gate_thread, &g[i], 0, NULL); }
    for (int i = 0; i < 6; i++) if (t[i]) { EXPECT_EQ(WaitForSingleObject(t[i], 10000), WAIT_OBJECT_0); CloseHandle(t[i]); }
    for (int i = 0; i < 6; i++) CHECKF(g[i].bad == 0, "thread %d (%s): %d mismatches", i, whp_fn_name[g[i].fn], g[i].bad);
}

/* ---- pause / reset interplay ---------------------------------------------------------------------------------- */

TEST(whp_seq_reset_while_paused_returns_buffers)
{
    REQUIRE(U(waveOutPause));
    HWAVEOUT h; HANDLE ev; if (!whp_need_dev(&h, &ev)) return;
    DWORD len; char *buf = whp_silence(100, &len);
    WAVEHDR w[3];
    EXPECT_EQ(U(waveOutPause)(h), MMSYSERR_NOERROR);
    for (int i = 0; i < 3; i++) {
        whp_hdr_init(&w[i], 0, buf, len);
        EXPECT_EQ(U(waveOutPrepareHeader)(h, &w[i], sizeof w[i]), MMSYSERR_NOERROR);
        EXPECT_EQ(U(waveOutWrite)(h, &w[i], sizeof w[i]), MMSYSERR_NOERROR);
    }
    Sleep(200);
    for (int i = 0; i < 3; i++) EXPECT_EQ(w[i].dwFlags, WHDR_PREPARED | WHDR_INQUEUE);
    EXPECT_EQ(U(waveOutReset)(h), MMSYSERR_NOERROR);
    CHECK(whp_wait_all_done(w, 3, ev, WHP_WAIT_MS));
    for (int i = 0; i < 3; i++) {
        EXPECT_EQ(w[i].dwFlags, WHDR_DONE | WHDR_PREPARED);
        EXPECT_EQ(U(waveOutUnprepareHeader)(h, &w[i], sizeof w[i]), MMSYSERR_NOERROR);
    }
    whp_close(h, ev);
}

TEST(whp_seq_write_after_reset_plays)
{
    HWAVEOUT h; HANDLE ev; if (!whp_need_dev(&h, &ev)) return;
    DWORD len; char *buf = whp_silence(60, &len);
    WAVEHDR w; whp_hdr_init(&w, 0, buf, len);
    EXPECT_EQ(U(waveOutPrepareHeader)(h, &w, sizeof w), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveOutWrite)(h, &w, sizeof w), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveOutReset)(h), MMSYSERR_NOERROR);
    CHECK(whp_wait_done(&w, ev, WHP_WAIT_MS));
    EXPECT_EQ(U(waveOutWrite)(h, &w, sizeof w), MMSYSERR_NOERROR);
    CHECK(whp_wait_done(&w, ev, WHP_WAIT_MS));
    EXPECT_EQ(w.dwFlags, WHDR_DONE | WHDR_PREPARED);
    EXPECT_EQ(U(waveOutUnprepareHeader)(h, &w, sizeof w), MMSYSERR_NOERROR);
    whp_close(h, ev);
}

TEST(whp_seq_close_after_stillplaying_then_more_writes)
{
    /* a refused close leaves a fully working handle: another header can still be written */
    HWAVEOUT h; HANDLE ev; if (!whp_need_dev(&h, &ev)) return;
    DWORD len; char *buf = whp_silence(1000, &len);
    WAVEHDR a, b; whp_hdr_init(&a, 0, buf, len); whp_hdr_init(&b, 0, buf, len / 10);
    EXPECT_EQ(U(waveOutPrepareHeader)(h, &a, sizeof a), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveOutPrepareHeader)(h, &b, sizeof b), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveOutWrite)(h, &a, sizeof a), MMSYSERR_NOERROR);
    MMRESULT rc = U(waveOutClose)(h);
    EXPECT_EQ(rc, WAVERR_STILLPLAYING);
    if (rc == 0) { CloseHandle(ev); return; }
    EXPECT_EQ(U(waveOutWrite)(h, &b, sizeof b), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveOutReset)(h), MMSYSERR_NOERROR);
    CHECK(whp_wait_done(&a, ev, WHP_WAIT_MS));
    CHECK(whp_wait_done(&b, ev, WHP_WAIT_MS));
    EXPECT_EQ(U(waveOutUnprepareHeader)(h, &a, sizeof a), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveOutUnprepareHeader)(h, &b, sizeof b), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveOutClose)(h), MMSYSERR_NOERROR);
    CloseHandle(ev);
}

TEST(whp_seq_many_prepared_headers)
{
    /* prepare/unprepare 64 headers on one handle without writing */
    HWAVEOUT h; HANDLE ev; if (!whp_need_dev(&h, &ev)) return;
    DWORD len; char *buf = whp_silence(10, &len);
    static WAVEHDR w[64];
    int bad = 0;
    for (int i = 0; i < 64; i++) {
        whp_hdr_init(&w[i], 0, buf, len);
        if (U(waveOutPrepareHeader)(h, &w[i], sizeof w[i]) != 0 || w[i].dwFlags != WHDR_PREPARED) bad++;
    }
    for (int i = 0; i < 64; i++)
        if (U(waveOutUnprepareHeader)(h, &w[i], sizeof w[i]) != 0 || w[i].dwFlags != 0) bad++;
    EXPECT_EQ(bad, 0);
    whp_close(h, ev);
}

/* ---- observations: when dwBytesRecorded is rewritten, whether dwLoops repeats ---------------------------------- */

TEST(whp_seq_bytesrecorded_timeline)
{
    REQUIRE(U(waveOutPause)); REQUIRE(U(waveOutRestart));
    HWAVEOUT h; HANDLE ev; if (!whp_need_dev(&h, &ev)) return;
    DWORD len; char *buf = whp_silence(60, &len);
    WAVEHDR w; whp_hdr_init(&w, 0, buf, len);
    w.dwBytesRecorded = 0xC3C3C3C3u;
    EXPECT_EQ(U(waveOutPause)(h), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveOutPrepareHeader)(h, &w, sizeof w), MMSYSERR_NOERROR);
    DWORD after_prep = w.dwBytesRecorded;
    EXPECT_EQ(U(waveOutWrite)(h, &w, sizeof w), MMSYSERR_NOERROR);
    DWORD after_write = w.dwBytesRecorded;
    EXPECT_EQ(U(waveOutRestart)(h), MMSYSERR_NOERROR);
    CHECK(whp_wait_done(&w, ev, WHP_WAIT_MS));
    t_note("dwBytesRecorded: preset 0xC3C3C3C3, after prepare 0x%lx, after write (paused) 0x%lx, after done 0x%lx",
           after_prep, after_write, w.dwBytesRecorded);
    EXPECT_EQ(after_prep, 0xC3C3C3C3u);
    EXPECT_EQ(U(waveOutUnprepareHeader)(h, &w, sizeof w), MMSYSERR_NOERROR);
    whp_close(h, ev);
}

TEST(whp_seq_loop_count_timing_note)
{
    HWAVEOUT h; HANDLE ev; if (!whp_need_dev(&h, &ev)) return;
    DWORD len; char *buf = whp_silence(150, &len);
    static const DWORD loops[] = { 1, 4 };
    for (int i = 0; i < 2; i++) {
        WAVEHDR w; whp_hdr_init(&w, WHDR_BEGINLOOP | WHDR_ENDLOOP, buf, len);
        w.dwLoops = loops[i];
        EXPECT_EQ(U(waveOutPrepareHeader)(h, &w, sizeof w), MMSYSERR_NOERROR);
        DWORD t0 = GetTickCount();
        EXPECT_EQ(U(waveOutWrite)(h, &w, sizeof w), MMSYSERR_NOERROR);
        int done = whp_wait_done(&w, ev, WHP_WAIT_MS);
        CHECK(done);
        t_note("150 ms buffer dwLoops=%lu: done after ~%lu ms, dwLoops after 0x%lx flags 0x%lx", loops[i],
               GetTickCount() - t0, w.dwLoops, w.dwFlags);
        U(waveOutReset)(h);
        EXPECT_EQ(U(waveOutUnprepareHeader)(h, &w, sizeof w), MMSYSERR_NOERROR);
    }
    whp_close(h, ev);
}
