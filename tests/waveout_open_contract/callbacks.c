#include "wo.h"
/* The notifications tied to open and close: WOM_OPEN (0x3BB) when waveOutOpen succeeds, WOM_CLOSE (0x3BC) when
 * waveOutClose succeeds, delivered through the CALLBACK_* type chosen at open (winmmbase DriverCallback:
 * 1 PostMessage to the window, 2 PostThreadMessage, 3 direct call, 5 SetEvent). A QUERY open or a failed open
 * delivers nothing. Waits are bounded (<= 2 s). */

TEST(wo_cb_function_open_close) {
    REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutClose));
    if (wo_nodev_contract(CALLBACK_FUNCTION)) return;
    static wo_rec rec; memset(&rec, 0, sizeof rec);
    WAVEFORMATEX wf; wo_std(&wf);
    HWAVEOUT h = NULL;
    EXPECT_EQ(wo_open(&h, 0, &wf, (DWORD_PTR)wo_rec_cb, (DWORD_PTR)&rec, CALLBACK_FUNCTION), MMSYSERR_NOERROR);
    if (!h) return;
    LONG at_return = rec.n;
    CHECK(wo_rec_wait(&rec, 1, 2000));
    EXPECT_EQ(at_return, 1);                            /* WOM_OPEN is delivered before waveOutOpen returns (VM) */
    if (rec.n >= 1) {
        EXPECT_EQ(rec.msg[0], WOM_OPEN);
        EXPECT_EQ(rec.h[0], h);
        EXPECT_EQ(rec.inst[0], (DWORD_PTR)&rec);
        EXPECT_EQ(rec.p1[0], 0);
    }
    EXPECT_EQ(wo_close(h), MMSYSERR_NOERROR);
    CHECK(wo_rec_wait(&rec, 2, 2000));
    if (rec.n >= 2) {
        EXPECT_EQ(rec.msg[1], WOM_CLOSE);
        EXPECT_EQ(rec.h[1], h);
        EXPECT_EQ(rec.inst[1], (DWORD_PTR)&rec);
    }
    Sleep(50);
    EXPECT_EQ(rec.n, 2);
}

TEST(wo_cb_function_mapper_open_close) {
    REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutClose));
    if (wo_nodev_contract(CALLBACK_FUNCTION)) return;
    static wo_rec rec; memset(&rec, 0, sizeof rec);
    WAVEFORMATEX wf; wo_std(&wf);
    HWAVEOUT h = NULL;
    EXPECT_EQ(wo_open(&h, WAVE_MAPPER, &wf, (DWORD_PTR)wo_rec_cb, (DWORD_PTR)&rec, CALLBACK_FUNCTION), MMSYSERR_NOERROR);
    if (!h) return;
    CHECK(wo_rec_wait(&rec, 1, 2000));
    if (rec.n >= 1) { EXPECT_EQ(rec.msg[0], WOM_OPEN); EXPECT_EQ(rec.h[0], h); }
    EXPECT_EQ(wo_close(h), MMSYSERR_NOERROR);
    CHECK(wo_rec_wait(&rec, 2, 2000));
    if (rec.n >= 2) { EXPECT_EQ(rec.msg[1], WOM_CLOSE); EXPECT_EQ(rec.h[1], h); }
    Sleep(50);
    EXPECT_EQ(rec.n, 2);
}

/* dwInstance is passed through verbatim, all 64 bits. */
static volatile DWORD_PTR g_inst_seen[4]; static volatile LONG g_inst_n;
static void CALLBACK inst_cb(HWAVEOUT h, UINT msg, DWORD_PTR inst, DWORD_PTR p1, DWORD_PTR p2) {
    LONG i = InterlockedIncrement(&g_inst_n) - 1;
    if (i < 4) g_inst_seen[i] = inst;
}
TEST(wo_cb_function_instance_passthrough) {
    REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutClose));
    if (wo_nodev_contract(CALLBACK_FUNCTION)) return;
    static const DWORD_PTR vals[] = { 0, 1, 0xFFFFFFFF, 0x123456789ABCDEF0ull, (DWORD_PTR)-1 };
    WAVEFORMATEX wf; wo_std(&wf);
    for (unsigned v = 0; v < sizeof vals / sizeof vals[0]; v++) {
        g_inst_n = 0; g_inst_seen[0] = g_inst_seen[1] = 0xBAD;
        HWAVEOUT h = NULL;
        MMRESULT r = wo_open(&h, 0, &wf, (DWORD_PTR)inst_cb, vals[v], CALLBACK_FUNCTION);
        EXPECT_EQ(r, MMSYSERR_NOERROR);
        if (r) continue;
        EXPECT_EQ(wo_close(h), MMSYSERR_NOERROR);
        DWORD t0 = GetTickCount();
        while (g_inst_n < 2 && GetTickCount() - t0 < 2000) Sleep(5);
        EXPECT_EQ(g_inst_n, 2);
        EXPECT_EQ(g_inst_seen[0], vals[v]);
        EXPECT_EQ(g_inst_seen[1], vals[v]);
    }
}

TEST(wo_cb_function_query_no_call) {
    REQUIRE(U(waveOutOpen));
    if (wo_nodev_contract(CALLBACK_FUNCTION | WAVE_FORMAT_QUERY)) return;
    static wo_rec rec; memset(&rec, 0, sizeof rec);
    WAVEFORMATEX wf; wo_std(&wf);
    EXPECT_EQ(wo_open(NULL, 0, &wf, (DWORD_PTR)wo_rec_cb, (DWORD_PTR)&rec, CALLBACK_FUNCTION | WAVE_FORMAT_QUERY), MMSYSERR_NOERROR);
    EXPECT_EQ(wo_open(NULL, WAVE_MAPPER, &wf, (DWORD_PTR)wo_rec_cb, (DWORD_PTR)&rec, CALLBACK_FUNCTION | WAVE_FORMAT_QUERY), MMSYSERR_NOERROR);
    Sleep(150);
    EXPECT_EQ(rec.n, 0);
}

TEST(wo_cb_failed_open_no_call) {
    REQUIRE(U(waveOutOpen));
    static wo_rec rec; memset(&rec, 0, sizeof rec);
    WAVEFORMATEX wf; wo_std(&wf);
    HWAVEOUT h = WO_SENTINEL;
    EXPECT_EQ(wo_open(&h, WO_BADID, &wf, (DWORD_PTR)wo_rec_cb, (DWORD_PTR)&rec, CALLBACK_FUNCTION), MMSYSERR_BADDEVICEID);
    EXPECT_EQ(wo_open(&h, 0, &wf, (DWORD_PTR)wo_rec_cb, (DWORD_PTR)&rec, CALLBACK_FUNCTION | 0x200), MMSYSERR_INVALFLAG);
    EXPECT_EQ(wo_open(&h, WAVE_MAPPER, NULL, (DWORD_PTR)wo_rec_cb, (DWORD_PTR)&rec, CALLBACK_FUNCTION), MMSYSERR_INVALPARAM);
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    EXPECT_EQ(wo_open(&h, WO_BADID, &wf, (DWORD_PTR)ev, 0, CALLBACK_EVENT), MMSYSERR_BADDEVICEID);
    Sleep(100);
    EXPECT_EQ(rec.n, 0);
    EXPECT_EQ(WaitForSingleObject(ev, 0), WAIT_TIMEOUT);
    CloseHandle(ev);
}

TEST(wo_cb_event_open_close) {
    REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutClose));
    if (wo_nodev_contract(CALLBACK_EVENT)) return;
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    WAVEFORMATEX wf; wo_std(&wf);
    HWAVEOUT h = NULL;
    EXPECT_EQ(wo_open(&h, WAVE_MAPPER, &wf, (DWORD_PTR)ev, 0xABC, CALLBACK_EVENT), MMSYSERR_NOERROR);
    if (!h) { CloseHandle(ev); return; }
    EXPECT_EQ(WaitForSingleObject(ev, 2000), WAIT_OBJECT_0);      /* WOM_OPEN */
    EXPECT_EQ(WaitForSingleObject(ev, 50), WAIT_TIMEOUT);         /* only one signal for the open */
    EXPECT_EQ(wo_close(h), MMSYSERR_NOERROR);
    EXPECT_EQ(WaitForSingleObject(ev, 2000), WAIT_OBJECT_0);      /* WOM_CLOSE */
    CloseHandle(ev);
}

TEST(wo_cb_event_query_no_signal) {
    REQUIRE(U(waveOutOpen));
    if (wo_nodev_contract(CALLBACK_EVENT | WAVE_FORMAT_QUERY)) return;
    HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    WAVEFORMATEX wf; wo_std(&wf);
    EXPECT_EQ(wo_open(NULL, WAVE_MAPPER, &wf, (DWORD_PTR)ev, 0, CALLBACK_EVENT | WAVE_FORMAT_QUERY), MMSYSERR_NOERROR);
    EXPECT_EQ(wo_open(NULL, 0, &wf, (DWORD_PTR)ev, 0, CALLBACK_EVENT | WAVE_FORMAT_QUERY), MMSYSERR_NOERROR);
    EXPECT_EQ(WaitForSingleObject(ev, 150), WAIT_TIMEOUT);
    CloseHandle(ev);
}

/* CALLBACK_NULL ignores dwCallback: an event handle given there is never signalled. */
TEST(wo_cb_null_type_ignores_value) {
    REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutClose));
    if (wo_nodev_contract(0)) return;
    HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    WAVEFORMATEX wf; wo_std(&wf);
    HWAVEOUT h = NULL;
    EXPECT_EQ(wo_open(&h, 0, &wf, (DWORD_PTR)ev, 0, CALLBACK_NULL), MMSYSERR_NOERROR);
    if (h) EXPECT_EQ(wo_close(h), MMSYSERR_NOERROR);
    EXPECT_EQ(WaitForSingleObject(ev, 150), WAIT_TIMEOUT);
    CloseHandle(ev);
}

TEST(wo_cb_thread_open_close) {
    REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutClose)); REQUIRE(U(PeekMessageA));
    if (wo_nodev_contract(CALLBACK_THREAD)) return;
    MSG m;
    U(PeekMessageA)(&m, NULL, 0, 0, PM_NOREMOVE);     /* make sure this thread has a queue */
    wo_drain();
    WAVEFORMATEX wf; wo_std(&wf);
    HWAVEOUT h = NULL;
    EXPECT_EQ(wo_open(&h, 0, &wf, (DWORD_PTR)GetCurrentThreadId(), 0x77, CALLBACK_THREAD), MMSYSERR_NOERROR);
    if (!h) return;
    memset(&m, 0, sizeof m);
    CHECK(wo_wait_msg(MM_WOM_OPEN, 2000, &m));
    if (m.message == MM_WOM_OPEN) {
        EXPECT_EQ(m.hwnd, NULL);
        EXPECT_EQ(m.wParam, (WPARAM)h);
        EXPECT_EQ(m.lParam, 0);
    }
    EXPECT_EQ(wo_close(h), MMSYSERR_NOERROR);
    memset(&m, 0, sizeof m);
    CHECK(wo_wait_msg(MM_WOM_CLOSE, 2000, &m));
    if (m.message == MM_WOM_CLOSE) EXPECT_EQ(m.wParam, (WPARAM)h);
    wo_drain();
}

TEST(wo_cb_window_open_close) {
    REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutClose)); REQUIRE(U(CreateWindowExA)); REQUIRE(U(PeekMessageA));
    if (wo_nodev_contract(CALLBACK_WINDOW)) return;
    HWND w = wo_msgwnd();
    CHECK(w != NULL);
    if (!w) return;
    wo_drain();
    WAVEFORMATEX wf; wo_std(&wf);
    HWAVEOUT h = NULL;
    EXPECT_EQ(wo_open(&h, WAVE_MAPPER, &wf, (DWORD_PTR)w, 0x99, CALLBACK_WINDOW), MMSYSERR_NOERROR);
    if (!h) { wo_destroy(w); return; }
    MSG m; memset(&m, 0, sizeof m);
    CHECK(wo_wait_msg(MM_WOM_OPEN, 2000, &m));
    if (m.message == MM_WOM_OPEN) {
        EXPECT_EQ(m.hwnd, w);
        EXPECT_EQ(m.wParam, (WPARAM)h);
        EXPECT_EQ(m.lParam, 0);
    }
    EXPECT_EQ(wo_close(h), MMSYSERR_NOERROR);
    memset(&m, 0, sizeof m);
    CHECK(wo_wait_msg(MM_WOM_CLOSE, 2000, &m));
    if (m.message == MM_WOM_CLOSE) { EXPECT_EQ(m.hwnd, w); EXPECT_EQ(m.wParam, (WPARAM)h); }
    wo_drain();
    wo_destroy(w);
}

/* A window owned by another thread is still a window: accepted, and the messages go to that thread's queue. */
typedef struct { HWND w; HANDLE ready, quit; volatile LONG nopen, nclose; } wthr;
static DWORD WINAPI window_thread(void *p) {
    wthr *c = (wthr *)p;
    c->w = wo_msgwnd();
    SetEvent(c->ready);
    DWORD t0 = GetTickCount();
    while (GetTickCount() - t0 < 8000) {
        MSG m;
        while (U(PeekMessageA)(&m, NULL, 0, 0, PM_REMOVE)) {
            if (m.message == MM_WOM_OPEN) InterlockedIncrement(&c->nopen);
            if (m.message == MM_WOM_CLOSE) InterlockedIncrement(&c->nclose);
        }
        if (WaitForSingleObject(c->quit, 10) == WAIT_OBJECT_0) break;
    }
    wo_destroy(c->w);
    return 0;
}
TEST(wo_cb_window_other_thread) {
    REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutClose)); REQUIRE(U(CreateWindowExA)); REQUIRE(U(PeekMessageA));
    if (wo_nodev_contract(CALLBACK_WINDOW)) return;
    wthr c; memset(&c, 0, sizeof c);
    c.ready = CreateEventA(NULL, TRUE, FALSE, NULL); c.quit = CreateEventA(NULL, TRUE, FALSE, NULL);
    HANDLE th = CreateThread(NULL, 0, window_thread, &c, 0, NULL);
    WaitForSingleObject(c.ready, 3000);
    CHECK(c.w != NULL);
    if (c.w) {
        WAVEFORMATEX wf; wo_std(&wf);
        HWAVEOUT h = NULL;
        EXPECT_EQ(wo_open(&h, 0, &wf, (DWORD_PTR)c.w, 0, CALLBACK_WINDOW), MMSYSERR_NOERROR);
        if (h) EXPECT_EQ(wo_close(h), MMSYSERR_NOERROR);
        DWORD t0 = GetTickCount();
        while ((c.nopen < 1 || c.nclose < 1) && GetTickCount() - t0 < 2000) Sleep(10);
        EXPECT_EQ(c.nopen, 1);
        EXPECT_EQ(c.nclose, 1);
    }
    SetEvent(c.quit);
    WaitForSingleObject(th, 5000);
    CloseHandle(th); CloseHandle(c.ready); CloseHandle(c.quit);
}

TEST(wo_cb_window_query_no_message) {
    REQUIRE(U(waveOutOpen)); REQUIRE(U(CreateWindowExA)); REQUIRE(U(PeekMessageA));
    if (wo_nodev_contract(CALLBACK_WINDOW | WAVE_FORMAT_QUERY)) return;
    HWND w = wo_msgwnd();
    if (!w) { CHECK(0); return; }
    wo_drain();
    WAVEFORMATEX wf; wo_std(&wf);
    EXPECT_EQ(wo_open(NULL, WAVE_MAPPER, &wf, (DWORD_PTR)w, 0, CALLBACK_WINDOW | WAVE_FORMAT_QUERY), MMSYSERR_NOERROR);
    EXPECT_EQ(wo_open(NULL, WAVE_MAPPER, &wf, 0, 0, CALLBACK_WINDOW | WAVE_FORMAT_QUERY), MMSYSERR_NOERROR);
    CHECK(!wo_wait_msg(MM_WOM_OPEN, 150, NULL));
    wo_destroy(w);
}

/* Each handle gets its own notification pair, tagged with its own handle value. */
TEST(wo_cb_two_handles_distinct_notifications) {
    REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutClose));
    if (wo_nodev_contract(CALLBACK_FUNCTION)) return;
    static wo_rec ra, rb; memset(&ra, 0, sizeof ra); memset(&rb, 0, sizeof rb);
    WAVEFORMATEX wf; wo_std(&wf);
    HWAVEOUT a = NULL, b = NULL;
    EXPECT_EQ(wo_open(&a, 0, &wf, (DWORD_PTR)wo_rec_cb, (DWORD_PTR)&ra, CALLBACK_FUNCTION), MMSYSERR_NOERROR);
    EXPECT_EQ(wo_open(&b, WAVE_MAPPER, &wf, (DWORD_PTR)wo_rec_cb, (DWORD_PTR)&rb, CALLBACK_FUNCTION), MMSYSERR_NOERROR);
    if (a) EXPECT_EQ(wo_close(a), MMSYSERR_NOERROR);
    if (b) EXPECT_EQ(wo_close(b), MMSYSERR_NOERROR);
    CHECK(wo_rec_wait(&ra, 2, 2000)); CHECK(wo_rec_wait(&rb, 2, 2000));
    Sleep(30);
    EXPECT_EQ(ra.n, 2); EXPECT_EQ(rb.n, 2);
    if (ra.n >= 2) { EXPECT_EQ(ra.h[0], a); EXPECT_EQ(ra.h[1], a); EXPECT_EQ(ra.msg[0], WOM_OPEN); EXPECT_EQ(ra.msg[1], WOM_CLOSE); }
    if (rb.n >= 2) { EXPECT_EQ(rb.h[0], b); EXPECT_EQ(rb.h[1], b); EXPECT_EQ(rb.msg[0], WOM_OPEN); EXPECT_EQ(rb.msg[1], WOM_CLOSE); }
}

/* A refused close (buffers still queued) sends no WOM_CLOSE. */
static short g_quiet[44100 * 2];
TEST(wo_cb_refused_close_no_notification) {
    REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutClose)); REQUIRE(U(waveOutPrepareHeader)); REQUIRE(U(waveOutWrite));
    REQUIRE(U(waveOutReset)); REQUIRE(U(waveOutUnprepareHeader));
    if (wo_nodev_contract(CALLBACK_FUNCTION)) return;
    static wo_rec rec; memset(&rec, 0, sizeof rec);
    WAVEFORMATEX wf; wo_std(&wf);
    HWAVEOUT h = NULL;
    EXPECT_EQ(wo_open(&h, 0, &wf, (DWORD_PTR)wo_rec_cb, (DWORD_PTR)&rec, CALLBACK_FUNCTION), MMSYSERR_NOERROR);
    if (!h) return;
    CHECK(wo_rec_wait(&rec, 1, 2000));
    WAVEHDR wh; memset(&wh, 0, sizeof wh); wh.lpData = (LPSTR)g_quiet; wh.dwBufferLength = sizeof g_quiet;
    EXPECT_EQ(U(waveOutPrepareHeader)(h, &wh, sizeof wh), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveOutWrite)(h, &wh, sizeof wh), MMSYSERR_NOERROR);
    MMRESULT r = wo_close(h);
    EXPECT_EQ(r, WAVERR_STILLPLAYING);
    if (r == MMSYSERR_NOERROR) return;
    Sleep(50);
    int closes = 0;
    for (LONG i = 0; i < rec.n && i < 16; i++) if (rec.msg[i] == WOM_CLOSE) closes++;
    EXPECT_EQ(closes, 0);
    EXPECT_EQ(U(waveOutReset)(h), MMSYSERR_NOERROR);
    DWORD t0 = GetTickCount();
    while ((wh.dwFlags & WHDR_INQUEUE) && GetTickCount() - t0 < 2000) Sleep(10);
    EXPECT_EQ(U(waveOutUnprepareHeader)(h, &wh, sizeof wh), MMSYSERR_NOERROR);
    EXPECT_EQ(wo_close(h), MMSYSERR_NOERROR);
    t0 = GetTickCount();
    for (;;) {
        closes = 0;
        for (LONG i = 0; i < rec.n && i < 16; i++) if (rec.msg[i] == WOM_CLOSE) closes++;
        if (closes || GetTickCount() - t0 > 2000) break;
        Sleep(5);
    }
    EXPECT_EQ(closes, 1);
    /* order: OPEN first, then one DONE for the reset buffer, CLOSE last */
    if (rec.n >= 3) {
        EXPECT_EQ(rec.msg[0], WOM_OPEN);
        EXPECT_EQ(rec.msg[rec.n - 1], WOM_CLOSE);
        int dones = 0;
        for (LONG i = 0; i < rec.n && i < 16; i++) if (rec.msg[i] == WOM_DONE) { dones++; EXPECT_EQ(rec.p1[i], (DWORD_PTR)&wh); }
        EXPECT_EQ(dones, 1);
    }
}
