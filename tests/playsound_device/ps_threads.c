/* playsound_device: PlaySound across threads.
 *
 * Decomp: sndMessage holds the process-wide SoundCritSec for the whole synchronous play (soundPlay -> soundWait),
 * so synchronous plays from several threads run one after the other, and a synchronous stop
 * (PlaySound(NULL, NULL, 0) -> sndMessage(NULL)) from another thread waits until the playing one is done.
 * Async plays run on winmm's notify window thread, which outlives the thread that requested them.
 * The SND_NOSTOP check reads the current header without the lock, so it answers immediately.
 */
#include "ps.h"

#define DEV (ps_ndev() > 0)

typedef struct { const BYTE *img; DWORD f; BOOL r; double t0, t1; HANDLE go; } job_t;
static DWORD WINAPI worker(void *p) {
    job_t *j = p;
    if (j->go) WaitForSingleObject(j->go, 5000);
    j->t0 = ps_now(); j->r = psW(j->img, j->f); j->t1 = ps_now();
    return 0;
}
static HANDLE start(job_t *j) { return CreateThread(NULL, 0, worker, j, 0, NULL); }
static int join(HANDLE h, DWORD ms) { DWORD w = WaitForSingleObject(h, ms); CloseHandle(h); return w == WAIT_OBJECT_0; }

TEST(psd_thread_worker_sync_play) {
    PS_REQ();
    wb_t w; ps_silent(&w, WAVE_FORMAT_PCM, 1, 8000, 16, 300);
    job_t j = { w.b, PS_MEMSYNC };
    HANDLE h = start(&j); REQUIRE(h);
    CHECKF(join(h, 5000), "worker did not finish");
    t_note("worker sync play: r=%d %.0f ms", j.r, j.t1 - j.t0);
    if (DEV) { CHECK(j.r == TRUE); CHECKF(j.t1 - j.t0 >= 150.0 && j.t1 - j.t0 < 3000.0, "%.0f ms", j.t1 - j.t0); }
    else CHECK(j.r == FALSE);
    ps_stop(); wb_free(&w);
}

TEST(psd_thread_worker_async_outlives_thread) {
    PS_REQ();
    wb_t w; ps_silent(&w, WAVE_FORMAT_PCM, 1, 8000, 16, 100);
    job_t j = { w.b, PS_MEMLOOP };
    HANDLE h = start(&j); REQUIRE(h);
    CHECK(join(h, 5000));
    CHECK(j.r == TRUE);
    if (DEV) { CHECKF(ps_wait_playing(1, 2000), "loop requested by an exited thread did not play"); Sleep(200); CHECK(ps_playing() == 1); }
    else { Sleep(200); CHECK(ps_playing() == 0); }
    ps_stop();                                     /* the main thread can stop it */
    CHECK(ps_playing() == 0);
    wb_free(&w);
}

TEST(psd_thread_nostop_sees_other_threads_sync_play) {
    PS_REQ();
    if (!DEV) { t_skip("needs a wave device"); return; }
    wb_t w; ps_silent(&w, WAVE_FORMAT_PCM, 1, 8000, 16, 700);
    job_t j = { w.b, PS_MEMSYNC };
    HANDLE h = start(&j); REQUIRE(h);
    int seen = ps_wait_playing(1, 2000);
    CHECKF(seen, "the worker's sync play never showed as playing");
    if (seen) {
        double t0 = ps_now();
        CHECK(psW(L"", PS_NOSTOP | PS_NODEFAULT) == FALSE);
        CHECKF(ps_now() - t0 < 100.0, "NOSTOP check blocked %.0f ms", ps_now() - t0);
    }
    CHECK(join(h, 5000));
    CHECK(j.r == TRUE);
    CHECK(ps_playing() == 0);
    ps_stop(); wb_free(&w);
}

TEST(psd_thread_stop_waits_for_sync_play) {
    PS_REQ();
    if (!DEV) { t_skip("needs a wave device"); return; }
    wb_t w; ps_silent(&w, WAVE_FORMAT_PCM, 1, 8000, 16, 600);
    job_t j = { w.b, PS_MEMSYNC };
    HANDLE h = start(&j); REQUIRE(h);
    CHECK(ps_wait_playing(1, 2000));
    double t0 = ps_now();
    BOOL r = psW(NULL, 0);
    double t1 = ps_now();
    CHECK(join(h, 5000));
    t_note("stop from main during a worker sync play: r=%d stop returned %.0f ms after the call, worker play %.0f ms, "
           "worker ended %.0f ms before the stop returned", r, t1 - t0, j.t1 - j.t0, t1 - j.t1);
    CHECK(r == TRUE);
    CHECK(j.r == TRUE);
    CHECKF(j.t1 - j.t0 >= 300.0, "worker play was cut short: %.0f ms", j.t1 - j.t0);
    CHECKF(t1 >= j.t1 - 5.0, "stop returned before the sync play finished");
    ps_stop(); wb_free(&w);
}

TEST(psd_thread_two_sync_concurrent) {
    PS_REQ();
    wb_t a, b; ps_silent(&a, WAVE_FORMAT_PCM, 1, 8000, 16, 300); ps_silent(&b, WAVE_FORMAT_PCM, 2, 22050, 16, 300);
    HANDLE go = CreateEventA(NULL, TRUE, FALSE, NULL); REQUIRE(go);
    job_t ja = { a.b, PS_MEMSYNC, 0, 0, 0, go }, jb = { b.b, PS_MEMSYNC, 0, 0, 0, go };
    HANDLE ha = start(&ja), hb = start(&jb); REQUIRE(ha && hb);
    Sleep(20); double t0 = ps_now(); SetEvent(go);
    CHECK(join(ha, 8000)); CHECK(join(hb, 8000));
    double end = ja.t1 > jb.t1 ? ja.t1 : jb.t1;
    t_note("two concurrent sync plays: A=%d %.0f ms, B=%d %.0f ms, total %.0f ms", ja.r, ja.t1 - ja.t0, jb.r, jb.t1 - jb.t0, end - t0);
    if (DEV) {
        CHECK(ja.r == TRUE); CHECK(jb.r == TRUE);
        /* SoundCritSec serializes them: the pair takes about two image lengths. */
        CHECKF(end - t0 >= 450.0, "total %.0f ms", end - t0);
        CHECKF(end - t0 < 6000.0, "total %.0f ms", end - t0);
    } else { CHECK(ja.r == FALSE); CHECK(jb.r == FALSE); }
    CloseHandle(go); ps_stop(); wb_free(&a); wb_free(&b);
}

TEST(psd_thread_sync_vs_async_concurrent) {
    PS_REQ();
    wb_t a, b; ps_silent(&a, WAVE_FORMAT_PCM, 1, 8000, 16, 300); ps_silent(&b, WAVE_FORMAT_PCM, 1, 8000, 16, 100);
    HANDLE go = CreateEventA(NULL, TRUE, FALSE, NULL); REQUIRE(go);
    job_t ja = { a.b, PS_MEMSYNC, 0, 0, 0, go }, jb = { b.b, PS_MEMLOOP, 0, 0, 0, go };
    HANDLE ha = start(&ja), hb = start(&jb); REQUIRE(ha && hb);
    Sleep(20); SetEvent(go);
    CHECK(join(ha, 8000)); CHECK(join(hb, 8000));
    CHECK(jb.r == TRUE);                      /* the async post always succeeds */
    if (!DEV) CHECK(ja.r == FALSE);
    Sleep(300);
    t_note("sync A=%d alongside async loop B: loop playing afterwards=%d", ja.r, ps_playing());
    ps_stop();
    CHECK(ps_playing() == 0);
    CloseHandle(go); wb_free(&a); wb_free(&b);
}

TEST(psd_thread_two_async_loops_last_wins) {
    PS_REQ();
    wb_t a, b; ps_silent(&a, WAVE_FORMAT_PCM, 1, 8000, 16, 100); ps_silent(&b, WAVE_FORMAT_PCM, 2, 44100, 16, 100);
    HANDLE go = CreateEventA(NULL, TRUE, FALSE, NULL); REQUIRE(go);
    job_t ja = { a.b, PS_MEMLOOP, 0, 0, 0, go }, jb = { b.b, PS_MEMLOOP, 0, 0, 0, go };
    HANDLE ha = start(&ja), hb = start(&jb); REQUIRE(ha && hb);
    SetEvent(go);
    CHECK(join(ha, 5000)); CHECK(join(hb, 5000));
    CHECK(ja.r == TRUE); CHECK(jb.r == TRUE);
    /* Both posts go to the single notify thread: one replaces the other, exactly one loop remains. */
    if (DEV) { CHECK(ps_wait_playing(1, 2000)); Sleep(200); CHECK(ps_playing() == 1); }
    ps_stop();
    CHECK(ps_playing() == 0);
    CloseHandle(go); wb_free(&a); wb_free(&b);
}

TEST(psd_thread_many_workers_sync) {
    PS_REQ();
    wb_t w; ps_silent(&w, WAVE_FORMAT_PCM, 1, 22050, 16, 60);
    job_t j[4]; HANDLE h[4];
    for (int i = 0; i < 4; i++) { memset(&j[i], 0, sizeof j[i]); j[i].img = w.b; j[i].f = PS_MEMSYNC; h[i] = start(&j[i]); }
    for (int i = 0; i < 4; i++) { if (h[i]) CHECK(join(h[i], 8000)); }
    /* All four use the same image concurrently; serialized by SoundCritSec, each one plays. */
    for (int i = 0; i < 4; i++) CHECKF(j[i].r == (DEV ? TRUE : FALSE), "worker %d: %d", i, j[i].r);
    ps_stop(); wb_free(&w);
}

TEST(psd_thread_stop_from_worker) {
    PS_REQ();
    wb_t w; ps_silent(&w, WAVE_FORMAT_PCM, 1, 8000, 16, 100);
    CHECK(psW(w.b, PS_MEMLOOP) == TRUE);
    if (DEV) CHECK(ps_wait_playing(1, 2000));
    job_t j = { NULL, 0 };
    HANDLE h = start(&j); REQUIRE(h);
    CHECK(join(h, 5000));
    CHECK(j.r == TRUE);
    CHECK(ps_playing() == 0);
    ps_stop(); wb_free(&w);
}

TEST(psd_thread_nostop_from_worker_during_loop) {
    PS_REQ();
    wb_t w; ps_silent(&w, WAVE_FORMAT_PCM, 1, 8000, 16, 100);
    CHECK(psW(w.b, PS_MEMLOOP) == TRUE);
    if (DEV) CHECK(ps_wait_playing(1, 2000)); else Sleep(200);
    job_t j = { w.b, PS_MEMSYNC | PS_NOSTOP };
    HANDLE h = start(&j); REQUIRE(h);
    CHECK(join(h, 5000));
    CHECK(j.r == FALSE);   /* playing: NOSTOP refuses; 0 devices: nothing opened, then the sync play itself fails */
    if (DEV) CHECK(ps_playing() == 1);
    ps_stop(); wb_free(&w);
}
