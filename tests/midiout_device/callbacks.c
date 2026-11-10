/* Notifications of a live midiOut handle: MOM_OPEN (0x3C7), MOM_DONE (0x3C9, dwParam1 = the MIDIHDR), MOM_CLOSE
 * (0x3C8), for every callback type, on the synth and through the mapper.
 *
 * winmm forwards them with DriverCallback(dwCallback, flags, handle, msg, dwInstance, p1, p2): CALLBACK_FUNCTION
 * gets (handle, msg, dwInstance, p1, p2); CALLBACK_WINDOW / CALLBACK_THREAD get MM_MOM_* with wParam = handle,
 * lParam = p1; CALLBACK_EVENT only sets the event. Callback functions here only record.
 */
#include "mod.h"

#define PH(h, m) U(midiOutPrepareHeader)((h), (m), sizeof(MIDIHDR))
#define UH(h, m) U(midiOutUnprepareHeader)((h), (m), sizeof(MIDIHDR))
#define LM(h, m) U(midiOutLongMsg)((h), (m), sizeof(MIDIHDR))
#define REQ_LONG() do { REQUIRE(U(midiOutLongMsg)); REQUIRE(U(midiOutPrepareHeader)); REQUIRE(U(midiOutUnprepareHeader)); } while (0)

static const UINT g_ids[] = { 0, MIDI_MAPPER };

static HMIDIOUT open_fn(UINT id, DWORD_PTR inst) {
    CHECKF(mod_open_count() == 0, "a handle is still open");
    mod_rec_reset();
    HMIDIOUT h = NULL;
    MMRESULT r = mod_open(&h, id, (DWORD_PTR)mod_cb, inst, CALLBACK_FUNCTION);
    CHECKF(r == MMSYSERR_NOERROR, "open 0x%x fn r=%u", id, r);
    return r ? NULL : h;
}

/* ================================================================ CALLBACK_FUNCTION */

TEST(midiout_device_cb_fn_open_close) {
    MOD_NEED_LIVE();
    for (int i = 0; i < NELEM(g_ids); i++) {
        HMIDIOUT h = open_fn(g_ids[i], 0xC0FFEE);
        if (!h) continue;
        CHECK(mod_wait_msg(MOM_OPEN, 1, 1000));
        EXPECT_EQ(mod_rec_count(), 1);
        const mod_rec_t *r = mod_rec(0);
        EXPECT_EQ(r->msg, MOM_OPEN);
        CHECK(r->h == h);
        EXPECT_EQ(r->inst, 0xC0FFEE);
        EXPECT_EQ(r->p1, 0);
        EXPECT_EQ(r->p2, 0);
        mod_close(h);
        CHECK(mod_wait_msg(MOM_CLOSE, 1, 1000));
        Sleep(20);
        EXPECT_EQ(mod_rec_count(), 2);
        r = mod_rec(1);
        EXPECT_EQ(r->msg, MOM_CLOSE);
        CHECK(r->h == h);
        EXPECT_EQ(r->inst, 0xC0FFEE);
        EXPECT_EQ(r->p1, 0);
        EXPECT_EQ(r->p2, 0);
    }
}

/* MOM_OPEN is delivered before midiOutOpen returns, MOM_CLOSE before midiOutClose returns; the thread is noted */
TEST(midiout_device_cb_fn_open_synchronous) {
    MOD_NEED_LIVE();
    for (int i = 0; i < NELEM(g_ids); i++) {
        HMIDIOUT h = open_fn(g_ids[i], 1);
        if (!h) continue;
        int at_return = mod_rec_count_msg(MOM_OPEN);
        DWORD tid = mod_rec_count() ? mod_rec(0)->tid : 0;
        t_note("id 0x%x MOM_OPEN delivered by return: %d same thread: %d", g_ids[i], at_return, tid == GetCurrentThreadId());
        EXPECT_EQ(at_return, 1);
        mod_close(h);
        int close_at_return = mod_rec_count_msg(MOM_CLOSE);
        t_note("id 0x%x MOM_CLOSE delivered by return: %d", g_ids[i], close_at_return);
        EXPECT_EQ(close_at_return, 1);
    }
}

/* LongMsg: exactly one MOM_DONE with the header pointer, DONE set and INQUEUE clear when it is delivered */
/* validated: Windows 10 22H2 19045.2965, no-audio and emulated-HDA runs */
TEST(midiout_device_cb_fn_done) {
    MOD_NEED_LIVE(); REQ_LONG();
    for (int i = 0; i < NELEM(g_ids); i++) {
        HMIDIOUT h = open_fn(g_ids[i], 0x77);
        if (!h) continue;
        mod_wait_msg(MOM_OPEN, 1, 1000);
        MIDIHDR m; mod_hdr(&m, mod_gs_reset, sizeof(mod_gs_reset));
        EXPECT_EQ(PH(h, &m), MMSYSERR_NOERROR);
        EXPECT_EQ(mod_rec_count_msg(MOM_DONE), 0);    /* prepare notifies nothing */
        double t0 = mod_now();
        EXPECT_EQ(LM(h, &m), MMSYSERR_NOERROR);
        int at_return = mod_rec_count_msg(MOM_DONE);
        CHECK(mod_wait_msg(MOM_DONE, 1, 2000));
        Sleep(20);
        EXPECT_EQ(mod_rec_count_msg(MOM_DONE), 1);
        for (int k = 0; k < mod_rec_count(); k++) {
            const mod_rec_t *r = mod_rec(k);
            if (r->msg != MOM_DONE) continue;
            CHECK(r->p1 == (DWORD_PTR)&m);
            EXPECT_EQ(r->p2, 0);
            CHECK(r->h == h);
            EXPECT_EQ(r->inst, 0x77);
            CHECKF(r->hflags & MHDR_DONE, "flags at MOM_DONE 0x%lx", r->hflags);
            CHECKF(!(r->hflags & MHDR_INQUEUE), "flags at MOM_DONE 0x%lx", r->hflags);
            t_note("id 0x%x MOM_DONE by return=%d after %.1f ms on %s thread, flags 0x%lx", g_ids[i], at_return,
                   r->t - t0, r->tid == GetCurrentThreadId() ? "the calling" : "another", r->hflags);
        }
        EXPECT_EQ(UH(h, &m), MMSYSERR_NOERROR);
        EXPECT_EQ(mod_rec_count_msg(MOM_DONE), 1);    /* unprepare notifies nothing */
        mod_close(h);
        CHECK(mod_wait_msg(MOM_CLOSE, 1, 1000));
        /* order: OPEN, DONE, CLOSE */
        int n = mod_rec_count();
        CHECK(n == 3);
        if (n == 3) {
            EXPECT_EQ(mod_rec(0)->msg, MOM_OPEN);
            EXPECT_EQ(mod_rec(1)->msg, MOM_DONE);
            EXPECT_EQ(mod_rec(2)->msg, MOM_CLOSE);
        }
    }
}

/* n headers -> n MOM_DONE in submission order */
TEST(midiout_device_cb_fn_done_order) {
    MOD_NEED_LIVE(); REQ_LONG();
    for (int i = 0; i < NELEM(g_ids); i++) {
        HMIDIOUT h = open_fn(g_ids[i], 0);
        if (!h) continue;
        mod_wait_msg(MOM_OPEN, 1, 1000);
        MIDIHDR m[6];
        for (int k = 0; k < 6; k++) {
            mod_hdr(&m[k], (k & 1) ? mod_gm_on : mod_gs_reset, (k & 1) ? sizeof(mod_gm_on) : sizeof(mod_gs_reset));
            EXPECT_EQ(PH(h, &m[k]), MMSYSERR_NOERROR);
        }
        for (int k = 0; k < 6; k++) CHECKF(LM(h, &m[k]) == MMSYSERR_NOERROR, "hdr %d", k);
        CHECK(mod_wait_msg(MOM_DONE, 6, 2000));
        Sleep(20);
        EXPECT_EQ(mod_rec_count_msg(MOM_DONE), 6);
        int k = 0;
        for (int j = 0; j < mod_rec_count(); j++) {
            const mod_rec_t *r = mod_rec(j);
            if (r->msg != MOM_DONE) continue;
            if (k < 6) CHECKF(r->p1 == (DWORD_PTR)&m[k], "done #%d is header %p", k, (void *)r->p1);
            k++;
        }
        for (int k2 = 0; k2 < 6; k2++) {
            mod_wait_done(&m[k2], 2000);
            CHECKF(UH(h, &m[k2]) == MMSYSERR_NOERROR, "hdr %d", k2);
        }
        mod_close(h);
    }
}

/* refused LongMsg calls (unprepared, INQUEUE, length 0) notify nothing */
TEST(midiout_device_cb_fn_refused_long_no_done) {
    MOD_NEED_LIVE(); REQ_LONG();
    HMIDIOUT h = open_fn(0, 0);
    if (!h) return;
    mod_wait_msg(MOM_OPEN, 1, 1000);
    MIDIHDR m; mod_hdr(&m, mod_gs_reset, sizeof(mod_gs_reset));
    EXPECT_EQ(LM(h, &m), MIDIERR_UNPREPARED);
    EXPECT_EQ(PH(h, &m), MMSYSERR_NOERROR);
    m.dwFlags |= MHDR_INQUEUE;
    EXPECT_EQ(LM(h, &m), MIDIERR_STILLPLAYING);
    m.dwFlags &= ~MHDR_INQUEUE;
    m.dwBufferLength = 0;
    EXPECT_EQ(LM(h, &m), MMSYSERR_INVALPARAM);
    EXPECT_EQ(UH(h, &m), MMSYSERR_NOERROR);
    Sleep(50);
    EXPECT_EQ(mod_rec_count_msg(MOM_DONE), 0);
    mod_close(h);
}

/* short messages and a reset with nothing queued notify nothing */
TEST(midiout_device_cb_fn_reset_idle_no_callback) {
    MOD_NEED_LIVE();
    for (int i = 0; i < NELEM(g_ids); i++) {
        HMIDIOUT h = open_fn(g_ids[i], 0);
        if (!h) continue;
        mod_wait_msg(MOM_OPEN, 1, 1000);
        EXPECT_EQ(U(midiOutReset)(h), MMSYSERR_NOERROR);
        EXPECT_EQ(U(midiOutReset)(h), MMSYSERR_NOERROR);
        Sleep(50);
        EXPECT_EQ(mod_rec_count(), 1);
        mod_close(h);
    }
}

/* the instance value is passed through unchanged, including 0 and all-ones */
TEST(midiout_device_cb_fn_instance_values) {
    MOD_NEED_LIVE();
    static const DWORD_PTR inst[] = { 0, 1, 0xFFFFFFFF, (DWORD_PTR)-1, (DWORD_PTR)0x123456789ABCDEF0ull };
    for (int i = 0; i < NELEM(inst); i++) {
        HMIDIOUT h = open_fn(0, inst[i]);
        if (!h) continue;
        mod_wait_msg(MOM_OPEN, 1, 1000);
        mod_close(h);
        mod_wait_msg(MOM_CLOSE, 1, 1000);
        for (int k = 0; k < mod_rec_count(); k++)
            CHECKF(mod_rec(k)->inst == inst[i], "inst 0x%llx got 0x%llx", (unsigned long long)inst[i],
                   (unsigned long long)mod_rec(k)->inst);
    }
}

/* ================================================================ CALLBACK_EVENT */

TEST(midiout_device_cb_event_signals) {
    MOD_NEED_LIVE(); REQ_LONG();
    for (int i = 0; i < NELEM(g_ids); i++) {
        HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
        HMIDIOUT h = NULL;
        MMRESULT r = mod_open(&h, g_ids[i], (DWORD_PTR)ev, 0, CALLBACK_EVENT);
        EXPECT_EQ(r, MMSYSERR_NOERROR);
        if (r) { CloseHandle(ev); continue; }
        EXPECT_EQ(WaitForSingleObject(ev, 1000), WAIT_OBJECT_0);      /* MOM_OPEN */
        EXPECT_EQ(WaitForSingleObject(ev, 0), WAIT_TIMEOUT);
        /* short messages do not signal */
        U(midiOutShortMsg)(h, MOD_MSG(0xB0, 123, 0));
        EXPECT_EQ(WaitForSingleObject(ev, 30), WAIT_TIMEOUT);
        MIDIHDR m; mod_hdr(&m, mod_gs_reset, sizeof(mod_gs_reset));
        EXPECT_EQ(PH(h, &m), MMSYSERR_NOERROR);
        EXPECT_EQ(WaitForSingleObject(ev, 0), WAIT_TIMEOUT);
        EXPECT_EQ(LM(h, &m), MMSYSERR_NOERROR);
        EXPECT_EQ(WaitForSingleObject(ev, 2000), WAIT_OBJECT_0);      /* MOM_DONE */
        CHECK(m.dwFlags & MHDR_DONE);
        EXPECT_EQ(UH(h, &m), MMSYSERR_NOERROR);
        mod_close(h);
        EXPECT_EQ(WaitForSingleObject(ev, 1000), WAIT_OBJECT_0);      /* MOM_CLOSE */
        CloseHandle(ev);
    }
}

/* a manual-reset event stays signalled; DONE of several headers coalesce on it */
TEST(midiout_device_cb_event_manual_reset) {
    MOD_NEED_LIVE(); REQ_LONG();
    HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    HMIDIOUT h = NULL;
    MMRESULT r = mod_open(&h, 0, (DWORD_PTR)ev, 0, CALLBACK_EVENT);
    EXPECT_EQ(r, MMSYSERR_NOERROR);
    if (!r) {
        EXPECT_EQ(WaitForSingleObject(ev, 1000), WAIT_OBJECT_0);
        EXPECT_EQ(WaitForSingleObject(ev, 0), WAIT_OBJECT_0);
        ResetEvent(ev);
        MIDIHDR m[3];
        for (int k = 0; k < 3; k++) { mod_hdr(&m[k], mod_gm_on, sizeof(mod_gm_on)); EXPECT_EQ(PH(h, &m[k]), MMSYSERR_NOERROR); }
        for (int k = 0; k < 3; k++) EXPECT_EQ(LM(h, &m[k]), MMSYSERR_NOERROR);
        EXPECT_EQ(WaitForSingleObject(ev, 2000), WAIT_OBJECT_0);
        for (int k = 0; k < 3; k++) { CHECK(mod_wait_done(&m[k], 2000)); EXPECT_EQ(UH(h, &m[k]), MMSYSERR_NOERROR); }
        mod_close(h);
    }
    CloseHandle(ev);
}

/* ================================================================ CALLBACK_WINDOW / CALLBACK_THREAD */

TEST(midiout_device_cb_window_messages) {
    MOD_NEED_LIVE(); REQ_LONG(); REQUIRE(U(PeekMessageA)); REQUIRE(U(DestroyWindow));
    for (int i = 0; i < NELEM(g_ids); i++) {
        HWND w = mod_window();
        if (!w) { t_skip("no window"); return; }
        HMIDIOUT h = NULL;
        MMRESULT r = mod_open(&h, g_ids[i], (DWORD_PTR)w, 0x99, CALLBACK_WINDOW);
        EXPECT_EQ(r, MMSYSERR_NOERROR);
        if (r) { U(DestroyWindow)(w); continue; }
        WPARAM wp = 0; LPARAM lp = 0x55;
        EXPECT_EQ(mod_pump_count(w, MM_MOM_OPEN, 1000, 1, &wp, &lp), 1);
        CHECK((HMIDIOUT)wp == h);
        EXPECT_EQ(lp, 0);
        MIDIHDR m; mod_hdr(&m, mod_gs_reset, sizeof(mod_gs_reset));
        EXPECT_EQ(PH(h, &m), MMSYSERR_NOERROR);
        EXPECT_EQ(LM(h, &m), MMSYSERR_NOERROR);
        wp = 0; lp = 0;
        EXPECT_EQ(mod_pump_count(w, MM_MOM_DONE, 2000, 1, &wp, &lp), 1);
        CHECK((HMIDIOUT)wp == h);
        CHECK(lp == (LPARAM)&m);
        EXPECT_EQ(UH(h, &m), MMSYSERR_NOERROR);
        mod_close(h);
        wp = 0; lp = 0x55;
        EXPECT_EQ(mod_pump_count(w, MM_MOM_CLOSE, 1000, 1, &wp, &lp), 1);
        CHECK((HMIDIOUT)wp == h);
        EXPECT_EQ(lp, 0);
        /* nothing else arrives */
        EXPECT_EQ(mod_pump_count(w, MM_MOM_DONE, 30, 1, NULL, NULL), 0);
        U(DestroyWindow)(w);
    }
}

TEST(midiout_device_cb_thread_messages) {
    MOD_NEED_LIVE(); REQ_LONG(); REQUIRE(U(PeekMessageA));
    MSG msg;
    U(PeekMessageA)(&msg, NULL, 0, 0, PM_NOREMOVE);   /* this thread has a queue */
    for (int i = 0; i < NELEM(g_ids); i++) {
        mod_pump_drain((HWND)-1);
        HMIDIOUT h = NULL;
        MMRESULT r = mod_open(&h, g_ids[i], GetCurrentThreadId(), 0, CALLBACK_THREAD);
        EXPECT_EQ(r, MMSYSERR_NOERROR);
        if (r) continue;
        WPARAM wp = 0; LPARAM lp = 0;
        EXPECT_EQ(mod_pump_count((HWND)-1, MM_MOM_OPEN, 1000, 1, &wp, &lp), 1);
        CHECK((HMIDIOUT)wp == h);
        MIDIHDR m[2];
        for (int k = 0; k < 2; k++) { mod_hdr(&m[k], mod_gs_reset, sizeof(mod_gs_reset)); EXPECT_EQ(PH(h, &m[k]), MMSYSERR_NOERROR); }
        for (int k = 0; k < 2; k++) EXPECT_EQ(LM(h, &m[k]), MMSYSERR_NOERROR);
        EXPECT_EQ(mod_pump_count((HWND)-1, MM_MOM_DONE, 2000, 2, &wp, &lp), 2);
        CHECK(lp == (LPARAM)&m[0]);
        for (int k = 0; k < 2; k++) EXPECT_EQ(UH(h, &m[k]), MMSYSERR_NOERROR);
        mod_close(h);
        EXPECT_EQ(mod_pump_count((HWND)-1, MM_MOM_CLOSE, 1000, 1, &wp, &lp), 1);
        CHECK((HMIDIOUT)wp == h);
    }
}

/* CALLBACK_WINDOW with no window: the open succeeds; where the notifications go is noted */
TEST(midiout_device_cb_window_null) {
    MOD_NEED_LIVE(); REQUIRE(U(PeekMessageA));
    MSG msg;
    U(PeekMessageA)(&msg, NULL, 0, 0, PM_NOREMOVE);
    mod_pump_drain((HWND)-1);
    HMIDIOUT h = NULL;
    MMRESULT r = mod_open(&h, 0, 0, 0, CALLBACK_WINDOW);
    EXPECT_EQ(r, MMSYSERR_NOERROR);
    if (r) return;
    int open_q = mod_pump_count((HWND)-1, MM_MOM_OPEN, 100, 1, NULL, NULL);
    mod_close(h);
    int close_q = mod_pump_count((HWND)-1, MM_MOM_CLOSE, 100, 1, NULL, NULL);
    t_note("CALLBACK_WINDOW NULL: MM_MOM_OPEN in thread queue %d, MM_MOM_CLOSE %d", open_q, close_q);
}

/* CALLBACK_THREAD aimed at another thread with a queue: the messages land there */
typedef struct { HANDLE ready, quit; DWORD tid; int open, done, close; HMIDIOUT h_open; } qthr_t;
static DWORD WINAPI qthread(LPVOID p) {
    qthr_t *q = (qthr_t *)p;
    MSG m;
    U(PeekMessageA)(&m, NULL, 0, 0, PM_NOREMOVE);
    SetEvent(q->ready);
    double end = mod_now() + 5000;
    while (mod_now() < end && WaitForSingleObject(q->quit, 0) != WAIT_OBJECT_0) {
        while (U(PeekMessageA)(&m, (HWND)-1, MM_MOM_OPEN, MM_MOM_DONE, PM_REMOVE)) {
            if (m.message == MM_MOM_OPEN) { q->open++; q->h_open = (HMIDIOUT)m.wParam; }
            if (m.message == MM_MOM_DONE) q->done++;
            if (m.message == MM_MOM_CLOSE) q->close++;
        }
        Sleep(5);
    }
    return 0;
}

TEST(midiout_device_cb_thread_other) {
    MOD_NEED_LIVE(); REQ_LONG(); REQUIRE(U(PeekMessageA));
    qthr_t q; memset(&q, 0, sizeof(q));
    q.ready = CreateEventA(NULL, TRUE, FALSE, NULL);
    q.quit = CreateEventA(NULL, TRUE, FALSE, NULL);
    HANDLE th = CreateThread(NULL, 0, qthread, &q, 0, &q.tid);
    WaitForSingleObject(q.ready, 2000);
    HMIDIOUT h = NULL;
    MMRESULT r = mod_open(&h, 0, q.tid, 0, CALLBACK_THREAD);
    EXPECT_EQ(r, MMSYSERR_NOERROR);
    if (!r) {
        MIDIHDR m; mod_hdr(&m, mod_gs_reset, sizeof(mod_gs_reset));
        EXPECT_EQ(PH(h, &m), MMSYSERR_NOERROR);
        EXPECT_EQ(LM(h, &m), MMSYSERR_NOERROR);
        CHECK(mod_wait_done(&m, 2000));
        EXPECT_EQ(UH(h, &m), MMSYSERR_NOERROR);
        mod_close(h);
        for (int i = 0; i < 200 && q.close == 0; i++) Sleep(5);
    }
    SetEvent(q.quit);
    WaitForSingleObject(th, 6000);
    CloseHandle(th); CloseHandle(q.ready); CloseHandle(q.quit);
    EXPECT_EQ(q.open, 1);
    EXPECT_EQ(q.done, 1);
    EXPECT_EQ(q.close, 1);
    CHECK(q.h_open == h);
}
