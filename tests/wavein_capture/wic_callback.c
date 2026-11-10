/* wavein_capture: callback delivery for capture streams.
 * DriverCallback dispatches on the CALLBACK_* type [decomp winmmbase DriverCallback 0x5560]: WINDOW ->
 * PostMessage(hwnd, MM_WIM_*, hwi, dwParam1); THREAD -> PostThreadMessage; FUNCTION -> direct call with
 * (hwi, WIM_*, dwInstance, dwParam1, dwParam2); EVENT -> SetEvent. Messages: WIM_OPEN 0x3BE, WIM_CLOSE 0x3BF,
 * WIM_DATA 0x3C0 with dwParam1 = LPWAVEHDR. Device half only, except the failed-open checks.
 */
#include "wic.h"

#define FIRST_MS 3000

static MMRESULT open_fn(wic_s *s, UINT id, const WAVEFORMATEX *f, DWORD_PTR inst) {
    memset(s, 0, sizeof *s); wic_cb_reset();
    return wic_open_cb(s, id, f, (DWORD_PTR)wic_cbproc, inst, CALLBACK_FUNCTION);
}
static int wait_cb(int n, DWORD ms) {
    DWORD t0 = wic_now();
    while (wic_cbn < n) { if (wic_now() - t0 > ms) return 0; Sleep(5); }
    return 1;
}

TEST(wic_cb_fn_open_close_only) {
    REQUIRE(U(waveInOpen)); NEED_DEV();
    wic_s s; WAVEFORMATEX f; wic_pcm(&f, 44100, 16, 1);
    if (open_fn(&s, WAVE_MAPPER, &f, 0xC0FFEE)) { CHECK(!"open"); wic_teardown(&s); return; }
    /* WIM_OPEN is delivered before waveInOpen returns */
    EXPECT_EQ(wic_cbn, 1);
    EXPECT_EQ(wic_cblog[0].msg, WIM_OPEN);
    EXPECT_EQ(wic_cblog[0].h, s.h);
    EXPECT_EQ(wic_cblog[0].inst, 0xC0FFEE);
    EXPECT_EQ(wic_cblog[0].p1, 0);
    EXPECT_EQ(wic_cblog[0].p2, 0);
    t_note("WIM_OPEN on thread %lu (caller %lu)", wic_cblog[0].tid, GetCurrentThreadId());
    HWAVEIN h = s.h;
    EXPECT_EQ(U(waveInClose)(h), 0); s.h = NULL;
    /* WIM_CLOSE is delivered before waveInClose returns */
    EXPECT_EQ(wic_cbn, 2);
    EXPECT_EQ(wic_cblog[1].msg, WIM_CLOSE);
    EXPECT_EQ(wic_cblog[1].h, h);
    EXPECT_EQ(wic_cblog[1].inst, 0xC0FFEE);
    EXPECT_EQ(wic_cblog[1].p1, 0);
    wic_teardown(&s);
}

TEST(wic_cb_fn_full_sequence) {
    REQUIRE(U(waveInOpen)); NEED_DEV();
    wic_s s; WAVEFORMATEX f; wic_pcm(&f, 44100, 16, 1);
    if (open_fn(&s, WAVE_MAPPER, &f, 0x1234567)) { CHECK(!"open"); wic_teardown(&s); return; }
    DWORD len = wic_bytes_ms(&f, 40);
    for (int i = 0; i < 3; i++) { EXPECT_EQ(wic_buf(&s, i, len), 0); EXPECT_EQ(wic_add(&s, i), 0); }
    EXPECT_EQ(wic_cbn, 1);   /* prepare/add deliver nothing */
    EXPECT_EQ(U(waveInStart)(s.h), 0);
    CHECK(wait_cb(4, FIRST_MS + 1000));
    HWAVEIN h = s.h;
    EXPECT_EQ(U(waveInStop)(h), 0);
    EXPECT_EQ(U(waveInReset)(h), 0);
    for (int i = 0; i < 3; i++) EXPECT_EQ(U(waveInUnprepareHeader)(h, &s.hdr[i], sizeof(WAVEHDR)), 0);
    EXPECT_EQ(U(waveInClose)(h), 0); s.h = NULL;
    int n = wic_cbn;
    EXPECT_EQ(n, 5);
    static const UINT want[] = {WIM_OPEN, WIM_DATA, WIM_DATA, WIM_DATA, WIM_CLOSE};
    for (int i = 0; i < 5 && i < n; i++) {
        CHECKF(wic_cblog[i].msg == want[i], "event %d msg 0x%x want 0x%x", i, wic_cblog[i].msg, want[i]);
        EXPECT_EQ(wic_cblog[i].h, h);
        EXPECT_EQ(wic_cblog[i].inst, 0x1234567);
        EXPECT_EQ(wic_cblog[i].p2, 0);
    }
    for (int i = 1; i <= 3 && i < n; i++) {
        CHECKF(wic_cblog[i].p1 == (DWORD_PTR)&s.hdr[i - 1], "WIM_DATA %d carries header %p want %p", i, (void *)wic_cblog[i].p1, (void *)&s.hdr[i - 1]);
        /* inside the callback the header is already DONE and out of the queue */
        CHECKF(wic_cblog[i].flags == (WHDR_DONE | WHDR_PREPARED), "WIM_DATA %d flags 0x%lx", i, wic_cblog[i].flags);
        EXPECT_EQ(wic_cblog[i].rec, len);
    }
    wic_teardown(&s);
}

TEST(wic_cb_fn_data_thread) {
    REQUIRE(U(waveInOpen)); NEED_DEV();
    wic_s s; WAVEFORMATEX f; wic_pcm(&f, 44100, 16, 1);
    if (open_fn(&s, WAVE_MAPPER, &f, 0)) { CHECK(!"open"); wic_teardown(&s); return; }
    EXPECT_EQ(wic_buf(&s, 0, wic_bytes_ms(&f, 30)), 0); EXPECT_EQ(wic_add(&s, 0), 0);
    EXPECT_EQ(U(waveInStart)(s.h), 0);
    CHECK(wait_cb(2, FIRST_MS));
    if (wic_cbn >= 2) {
        /* WIM_DATA comes from the driver's worker thread, not the caller */
        CHECK(wic_cblog[1].tid != GetCurrentThreadId());
        t_note("WIM_OPEN tid %lu, WIM_DATA tid %lu, caller %lu", wic_cblog[0].tid, wic_cblog[1].tid, GetCurrentThreadId());
    }
    wic_teardown(&s);
}

TEST(wic_cb_fn_reset_returns_data_for_each) {
    REQUIRE(U(waveInOpen)); NEED_DEV();
    wic_s s; WAVEFORMATEX f; wic_pcm(&f, 44100, 16, 1);
    if (open_fn(&s, WAVE_MAPPER, &f, 0)) { CHECK(!"open"); wic_teardown(&s); return; }
    for (int i = 0; i < 4; i++) { EXPECT_EQ(wic_buf(&s, i, 4410), 0); EXPECT_EQ(wic_add(&s, i), 0); }
    /* never started: Reset hands every buffer back with a WIM_DATA, in queue order */
    EXPECT_EQ(U(waveInReset)(s.h), 0);
    CHECK(wait_cb(5, 1000));
    EXPECT_EQ(wic_cb_count(WIM_DATA), 4);
    for (int i = 1; i <= 4 && i < wic_cbn; i++) {
        EXPECT_EQ(wic_cblog[i].msg, WIM_DATA);
        EXPECT_EQ(wic_cblog[i].p1, (DWORD_PTR)&s.hdr[i - 1]);
        EXPECT_EQ(wic_cblog[i].rec, 0);
        EXPECT_EQ(wic_cblog[i].flags, 3);
    }
    wic_teardown(&s);
}

TEST(wic_cb_fn_stop_delivers_partial) {
    REQUIRE(U(waveInOpen)); NEED_DEV();
    wic_s s; WAVEFORMATEX f; wic_pcm(&f, 44100, 16, 1);
    if (open_fn(&s, WAVE_MAPPER, &f, 0)) { CHECK(!"open"); wic_teardown(&s); return; }
    DWORD big = wic_bytes_ms(&f, 1000);
    EXPECT_EQ(wic_buf(&s, 0, wic_bytes_ms(&f, 20)), 0); EXPECT_EQ(wic_add(&s, 0), 0);
    EXPECT_EQ(wic_buf(&s, 1, big), 0); EXPECT_EQ(wic_add(&s, 1), 0);
    EXPECT_EQ(U(waveInStart)(s.h), 0);
    CHECK(wait_cb(2, FIRST_MS));
    Sleep(120);
    EXPECT_EQ(U(waveInStop)(s.h), 0);
    CHECK(wait_cb(3, 1000));
    if (wic_cbn >= 3) {
        EXPECT_EQ(wic_cblog[2].msg, WIM_DATA);
        EXPECT_EQ(wic_cblog[2].p1, (DWORD_PTR)&s.hdr[1]);
        CHECKF(wic_cblog[2].rec > 0 && wic_cblog[2].rec < big, "partial %lu", wic_cblog[2].rec);
    }
    wic_teardown(&s);
}

TEST(wic_cb_fn_close_refused_no_close_msg) {
    REQUIRE(U(waveInOpen)); NEED_DEV();
    wic_s s; WAVEFORMATEX f; wic_pcm(&f, 44100, 16, 1);
    if (open_fn(&s, WAVE_MAPPER, &f, 0)) { CHECK(!"open"); wic_teardown(&s); return; }
    EXPECT_EQ(wic_buf(&s, 0, 4410), 0); EXPECT_EQ(wic_add(&s, 0), 0);
    EXPECT_EQ(U(waveInClose)(s.h), WAVERR_STILLPLAYING);
    EXPECT_EQ(wic_cbn, 1);
    EXPECT_EQ(wic_cb_count(WIM_CLOSE), 0);
    wic_teardown(&s);
    EXPECT_EQ(wic_cb_count(WIM_CLOSE), 1);
}

TEST(wic_cb_fn_query_no_callback) {
    REQUIRE(U(waveInOpen)); NEED_DEV();
    wic_cb_reset();
    WAVEFORMATEX f; wic_pcm(&f, 44100, 16, 1);
    EXPECT_EQ(U(waveInOpen)(NULL, WAVE_MAPPER, &f, (DWORD_PTR)wic_cbproc, 0, CALLBACK_FUNCTION | WAVE_FORMAT_QUERY), 0);
    EXPECT_EQ(U(waveInOpen)(NULL, 0, &f, (DWORD_PTR)wic_cbproc, 0, CALLBACK_FUNCTION | WAVE_FORMAT_QUERY), 0);
    Sleep(50);
    EXPECT_EQ(wic_cbn, 0);
}

TEST(wic_cb_fn_badformat_no_callback) {
    REQUIRE(U(waveInOpen)); NEED_DEV();
    wic_cb_reset();
    WAVEFORMATEX f; wic_pcm(&f, 44100, 16, 1); f.wFormatTag = 0x9999;
    HWAVEIN h = (HWAVEIN)(ULONG_PTR)0x5555;
    EXPECT_EQ(U(waveInOpen)(&h, WAVE_MAPPER, &f, (DWORD_PTR)wic_cbproc, 0, CALLBACK_FUNCTION), WAVERR_BADFORMAT);
    EXPECT_EQ(h, NULL);
    Sleep(50);
    EXPECT_EQ(wic_cbn, 0);
}

/* two streams, two instances: each callback carries its own handle and dwInstance */
TEST(wic_cb_fn_two_streams_instances) {
    REQUIRE(U(waveInOpen)); NEED_DEV();
    wic_cb_reset();
    WAVEFORMATEX f; wic_pcm(&f, 44100, 16, 1);
    wic_s a, b; memset(&a, 0, sizeof a); memset(&b, 0, sizeof b);
    EXPECT_EQ(wic_open_cb(&a, WAVE_MAPPER, &f, (DWORD_PTR)wic_cbproc, 0xA, CALLBACK_FUNCTION), 0);
    EXPECT_EQ(wic_open_cb(&b, WAVE_MAPPER, &f, (DWORD_PTR)wic_cbproc, 0xB, CALLBACK_FUNCTION), 0);
    if (a.h && b.h) {
        EXPECT_EQ(wic_buf(&a, 0, wic_bytes_ms(&f, 30)), 0); EXPECT_EQ(wic_add(&a, 0), 0);
        EXPECT_EQ(wic_buf(&b, 0, wic_bytes_ms(&f, 30)), 0); EXPECT_EQ(wic_add(&b, 0), 0);
        EXPECT_EQ(U(waveInStart)(a.h), 0); EXPECT_EQ(U(waveInStart)(b.h), 0);
        CHECK(wait_cb(4, FIRST_MS + 1000));
        int n = wic_cbn > 64 ? 64 : wic_cbn, ok = 0;
        for (int i = 0; i < n; i++) {
            wic_cbrec *r = &wic_cblog[i];
            if (r->h == a.h && r->inst == 0xA) ok++;
            else if (r->h == b.h && r->inst == 0xB) ok++;
            if (r->msg == WIM_DATA) CHECK(r->p1 == (DWORD_PTR)(r->h == a.h ? &a.hdr[0] : &b.hdr[0]));
        }
        EXPECT_EQ(ok, n);
    }
    wic_teardown(&b); wic_teardown(&a);
}

/* ---- CALLBACK_EVENT ---- */

TEST(wic_cb_event_signals) {
    REQUIRE(U(waveInOpen)); NEED_DEV();
    wic_s s; if (wic_open_std(&s, WAVE_MAPPER)) { wic_teardown(&s); return; }
    EXPECT_EQ(WaitForSingleObject(s.ev, 0), WAIT_OBJECT_0);   /* open */
    EXPECT_EQ(WaitForSingleObject(s.ev, 0), WAIT_TIMEOUT);    /* auto-reset, nothing else yet */
    DWORD len = wic_bytes_ms(wic_wfx(&s), 30);
    EXPECT_EQ(wic_buf(&s, 0, len), 0); EXPECT_EQ(wic_add(&s, 0), 0);
    EXPECT_EQ(WaitForSingleObject(s.ev, 50), WAIT_TIMEOUT);   /* add does not signal */
    EXPECT_EQ(U(waveInStart)(s.h), 0);
    EXPECT_EQ(WaitForSingleObject(s.ev, FIRST_MS), WAIT_OBJECT_0);   /* WIM_DATA */
    EXPECT_EQ(s.hdr[0].dwFlags, 3);
    EXPECT_EQ(U(waveInReset)(s.h), 0);
    EXPECT_EQ(U(waveInUnprepareHeader)(s.h, &s.hdr[0], sizeof(WAVEHDR)), 0);
    WaitForSingleObject(s.ev, 0);
    EXPECT_EQ(U(waveInClose)(s.h), 0); s.h = NULL;
    EXPECT_EQ(WaitForSingleObject(s.ev, 0), WAIT_OBJECT_0);   /* close */
    wic_teardown(&s);
}

TEST(wic_cb_event_manual_reset_event) {
    REQUIRE(U(waveInOpen)); NEED_DEV();
    HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    WAVEFORMATEX f; wic_pcm(&f, 44100, 16, 1);
    wic_s s; memset(&s, 0, sizeof s);
    if (wic_open_cb(&s, WAVE_MAPPER, &f, (DWORD_PTR)ev, 0, CALLBACK_EVENT)) { CloseHandle(ev); return; }
    EXPECT_EQ(WaitForSingleObject(ev, 0), WAIT_OBJECT_0);
    ResetEvent(ev);
    EXPECT_EQ(wic_buf(&s, 0, wic_bytes_ms(&f, 30)), 0); EXPECT_EQ(wic_add(&s, 0), 0);
    EXPECT_EQ(U(waveInStart)(s.h), 0);
    EXPECT_EQ(WaitForSingleObject(ev, FIRST_MS), WAIT_OBJECT_0);
    CHECK(wic_wait_done(&s, 0, 500));
    EXPECT_EQ(wic_teardown(&s), 0);
    CloseHandle(ev);
}

TEST(wic_cb_event_reset_signals) {
    REQUIRE(U(waveInOpen)); NEED_DEV();
    wic_s s; if (wic_open_std(&s, WAVE_MAPPER)) { wic_teardown(&s); return; }
    WaitForSingleObject(s.ev, 0);
    EXPECT_EQ(wic_buf(&s, 0, 4410), 0); EXPECT_EQ(wic_add(&s, 0), 0);
    EXPECT_EQ(U(waveInReset)(s.h), 0);
    EXPECT_EQ(WaitForSingleObject(s.ev, 500), WAIT_OBJECT_0);
    EXPECT_EQ(s.hdr[0].dwFlags, 3);
    EXPECT_EQ(wic_teardown(&s), 0);
}

/* ---- CALLBACK_NULL: buffers still complete, poll the header ---- */

TEST(wic_cb_null_polling) {
    REQUIRE(U(waveInOpen)); NEED_DEV();
    wic_s s; memset(&s, 0, sizeof s);
    WAVEFORMATEX f; wic_pcm(&f, 44100, 16, 1);
    if (wic_open_cb(&s, WAVE_MAPPER, &f, 0, 0, CALLBACK_NULL)) { CHECK(!"open"); return; }
    DWORD len = wic_bytes_ms(&f, 30);
    for (int i = 0; i < 2; i++) { EXPECT_EQ(wic_buf(&s, i, len), 0); EXPECT_EQ(wic_add(&s, i), 0); }
    EXPECT_EQ(U(waveInStart)(s.h), 0);
    CHECK(wic_wait_all(&s, 0, 2, FIRST_MS + 500));
    EXPECT_EQ(s.hdr[1].dwBytesRecorded, len);
    EXPECT_EQ(wic_teardown(&s), 0);
}

/* ---- CALLBACK_THREAD ---- */

typedef struct { UINT msg; WPARAM w; LPARAM l; } qmsg;

static int drain_thread(qmsg *out, int cap, int want, DWORD ms) {
    int n = 0; DWORD t0 = wic_now(); MSG m;
    while (n < want && wic_now() - t0 <= ms) {
        while (n < cap && U(PeekMessageA)(&m, (HWND)-1, MM_WIM_OPEN, MM_WIM_DATA, PM_REMOVE)) {
            out[n].msg = m.message; out[n].w = m.wParam; out[n].l = m.lParam; n++;
        }
        if (n < want) Sleep(5);
    }
    return n;
}

TEST(wic_cb_thread_sequence) {
    REQUIRE(U(waveInOpen)); REQUIRE(U(PeekMessageA)); NEED_DEV();
    MSG m; U(PeekMessageA)(&m, NULL, 0, 0, PM_NOREMOVE);
    while (U(PeekMessageA)(&m, (HWND)-1, MM_WIM_OPEN, MM_WIM_DATA, PM_REMOVE)) {}
    wic_s s; memset(&s, 0, sizeof s);
    WAVEFORMATEX f; wic_pcm(&f, 44100, 16, 1);
    if (wic_open_cb(&s, WAVE_MAPPER, &f, GetCurrentThreadId(), 0x99, CALLBACK_THREAD)) { CHECK(!"open"); return; }
    DWORD len = wic_bytes_ms(&f, 30);
    for (int i = 0; i < 2; i++) { EXPECT_EQ(wic_buf(&s, i, len), 0); EXPECT_EQ(wic_add(&s, i), 0); }
    EXPECT_EQ(U(waveInStart)(s.h), 0);
    CHECK(wic_wait_all(&s, 0, 2, FIRST_MS + 500));
    HWAVEIN h = s.h;
    EXPECT_EQ(wic_teardown(&s), 0);
    qmsg q[16]; int n = drain_thread(q, 16, 4, 1000);
    EXPECT_EQ(n, 4);
    static const UINT want[] = {MM_WIM_OPEN, MM_WIM_DATA, MM_WIM_DATA, MM_WIM_CLOSE};
    for (int i = 0; i < n && i < 4; i++) {
        CHECKF(q[i].msg == want[i], "msg %d 0x%x want 0x%x", i, q[i].msg, want[i]);
        EXPECT_EQ(q[i].w, (WPARAM)h);
    }
    if (n >= 3) {
        EXPECT_EQ(q[0].l, 0);
        EXPECT_EQ(q[1].l, (LPARAM)&s.hdr[0]);
        EXPECT_EQ(q[2].l, (LPARAM)&s.hdr[1]);
    }
}

/* ---- CALLBACK_WINDOW (message-only window, messages read without dispatch) ---- */

static HWND mkwnd(void) {
    if (!U(CreateWindowExA)) return NULL;
    return U(CreateWindowExA)(0, "STATIC", "wic", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, NULL, NULL);
}
static int drain_wnd(HWND w, qmsg *out, int cap, int want, DWORD ms) {
    int n = 0; DWORD t0 = wic_now(); MSG m;
    while (n < want && wic_now() - t0 <= ms) {
        while (n < cap && U(PeekMessageA)(&m, w, MM_WIM_OPEN, MM_WIM_DATA, PM_REMOVE)) {
            out[n].msg = m.message; out[n].w = m.wParam; out[n].l = m.lParam; n++;
        }
        if (n < want) Sleep(5);
    }
    return n;
}

TEST(wic_cb_window_sequence) {
    REQUIRE(U(waveInOpen)); REQUIRE(U(PeekMessageA)); REQUIRE(U(DestroyWindow)); NEED_DEV();
    HWND w = mkwnd();
    if (!w) { t_skip("no message-only window"); return; }
    wic_s s; memset(&s, 0, sizeof s);
    WAVEFORMATEX f; wic_pcm(&f, 44100, 16, 1);
    if (wic_open_cb(&s, WAVE_MAPPER, &f, (DWORD_PTR)w, 0, CALLBACK_WINDOW)) { CHECK(!"open"); U(DestroyWindow)(w); return; }
    DWORD len = wic_bytes_ms(&f, 30);
    for (int i = 0; i < 3; i++) { EXPECT_EQ(wic_buf(&s, i, len), 0); EXPECT_EQ(wic_add(&s, i), 0); }
    EXPECT_EQ(U(waveInStart)(s.h), 0);
    CHECK(wic_wait_all(&s, 0, 3, FIRST_MS + 500));
    HWAVEIN h = s.h;
    EXPECT_EQ(wic_teardown(&s), 0);
    qmsg q[16]; int n = drain_wnd(w, q, 16, 5, 1000);
    EXPECT_EQ(n, 5);
    static const UINT want[] = {MM_WIM_OPEN, MM_WIM_DATA, MM_WIM_DATA, MM_WIM_DATA, MM_WIM_CLOSE};
    for (int i = 0; i < n && i < 5; i++) {
        CHECKF(q[i].msg == want[i], "msg %d 0x%x want 0x%x", i, q[i].msg, want[i]);
        EXPECT_EQ(q[i].w, (WPARAM)h);
        if (q[i].msg == MM_WIM_DATA) EXPECT_EQ(q[i].l, (LPARAM)&s.hdr[i - 1]);
    }
    U(DestroyWindow)(w);
}

TEST(wic_cb_window_null_hwnd) {
    REQUIRE(U(waveInOpen)); NEED_DEV();
    wic_s s; memset(&s, 0, sizeof s);
    WAVEFORMATEX f; wic_pcm(&f, 44100, 16, 1);
    /* CALLBACK_WINDOW with a NULL window is accepted: no callback at all [decomp] */
    EXPECT_EQ(wic_open_cb(&s, WAVE_MAPPER, &f, 0, 0, CALLBACK_WINDOW), 0);
    if (s.h) {
        EXPECT_EQ(wic_buf(&s, 0, wic_bytes_ms(&f, 30)), 0); EXPECT_EQ(wic_add(&s, 0), 0);
        EXPECT_EQ(U(waveInStart)(s.h), 0);
        CHECK(wic_wait_done(&s, 0, FIRST_MS));
    }
    EXPECT_EQ(wic_teardown(&s), 0);
}

TEST(wic_cb_window_bad_hwnd) {
    REQUIRE(U(waveInOpen)); REQUIRE(U(DestroyWindow));
    HWND w = mkwnd();
    if (!w) { t_skip("no window"); return; }
    U(DestroyWindow)(w);   /* now a stale handle */
    WAVEFORMATEX f; wic_pcm(&f, 44100, 16, 1);
    HWAVEIN h = (HWAVEIN)(ULONG_PTR)0x5555;
    /* the window is validated before the device check: 11 on every guest */
    EXPECT_EQ(U(waveInOpen)(&h, WAVE_MAPPER, &f, (DWORD_PTR)w, 0, CALLBACK_WINDOW), MMSYSERR_INVALPARAM);
    EXPECT_EQ(h, (HWAVEIN)(ULONG_PTR)0x5555);
    EXPECT_EQ(U(waveInOpen)(NULL, WAVE_MAPPER, &f, (DWORD_PTR)w, 0, CALLBACK_WINDOW | WAVE_FORMAT_QUERY), MMSYSERR_INVALPARAM);
}
