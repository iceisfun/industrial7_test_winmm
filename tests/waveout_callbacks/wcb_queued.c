/* waveout_callbacks: CALLBACK_EVENT, CALLBACK_WINDOW and CALLBACK_THREAD delivery for waveOut (device half).
 * DriverCallback: type 1 PostMessageA(hwnd, msg, hdev, dwParam1); type 2 PostThreadMessageA(tid, msg, hdev,
 * dwParam1) followed by mmTaskSignal(tid) = PostThreadMessageA(tid, WM_USER, 0, 0); type 5 SetEvent(handle).
 * Window and thread messages are queued: WOM_OPEN is in the queue when waveOutOpen returns. Silence only.
 * VM_TIMEOUT: 240 */
#include "wcb.h"

#define NEED_OUT() do { REQUIRE(U(waveOutOpen)); if (wcb_nout() == 0) { t_skip("no waveOut device (count 0): contract half only"); return; } wcb_note_devices(); } while (0)

/* ======================= CALLBACK_EVENT ======================= */

TEST(wcb_ev_open_signals) {
    NEED_OUT();
    UINT ids[6]; int k = wcb_out_ids(ids, 6);
    WAVEFORMATEX f; wcb_fmt_default(&f);
    for (int i = 0; i < k; i++) {
        HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
        HWAVEOUT h = NULL;
        MMRESULT r = U(waveOutOpen)(&h, ids[i], &f, (DWORD_PTR)ev, 0, CALLBACK_EVENT);
        CHECKF(r == 0, "%s event open %u", wcb_idname(ids[i]), r);
        if (!r) {
            /* signalled synchronously during the open */
            CHECKF(WaitForSingleObject(ev, 0) == WAIT_OBJECT_0, "%s: event not signalled when waveOutOpen returned", wcb_idname(ids[i]));
            EXPECT_EQ(WaitForSingleObject(ev, 50), WAIT_TIMEOUT);
            EXPECT_EQ(U(waveOutClose)(h), 0);
            CHECKF(WaitForSingleObject(ev, 0) == WAIT_OBJECT_0, "%s: event not signalled by close", wcb_idname(ids[i]));
            EXPECT_EQ(WaitForSingleObject(ev, 50), WAIT_TIMEOUT);
        }
        CloseHandle(ev);
    }
}

TEST(wcb_ev_done_per_buffer_sequential) {
    NEED_OUT();
    UINT ids[6]; int k = wcb_out_ids(ids, 6);
    WAVEFORMATEX f; wcb_fmt_default(&f);
    for (int i = 0; i < k; i++) {
        HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
        HWAVEOUT h = NULL;
        if (U(waveOutOpen)(&h, ids[i], &f, (DWORD_PTR)ev, 0, CALLBACK_EVENT)) { CHECK(0); CloseHandle(ev); continue; }
        CHECK(wcb_ev_wait(ev, 0));
        int signals = 0;
        for (int b = 0; b < 4; b++) {
            WAVEHDR wh; DWORD len; char *d = wcb_silence(&f, 30, &len); wcb_hdr(&wh, d, len);
            U(waveOutPrepareHeader)(h, &wh, sizeof wh);
            EXPECT_EQ(U(waveOutWrite)(h, &wh, sizeof wh), 0);
            if (wcb_ev_wait(ev, WCB_WAIT_MS)) signals++;
            /* the event is set after the header is marked done */
            CHECKF(wh.dwFlags & WHDR_DONE, "buffer %d: flags 0x%lx after the signal", b, wh.dwFlags);
            if (!(wh.dwFlags & WHDR_DONE)) U(waveOutReset)(h);
            U(waveOutUnprepareHeader)(h, &wh, sizeof wh);
            HeapFree(GetProcessHeap(), 0, d);
        }
        EXPECT_EQ(signals, 4);
        EXPECT_EQ(U(waveOutClose)(h), 0);
        CHECK(wcb_ev_wait(ev, 0));
        CloseHandle(ev);
    }
}

TEST(wcb_ev_done_batch_all_flags) {
    NEED_OUT();
    WAVEFORMATEX f; wcb_fmt_default(&f);
    int counts[] = { 2, 5, 10 };
    for (int ci = 0; ci < 3; ci++) {
        int nb = counts[ci];
        HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
        HWAVEOUT h = NULL;
        if (U(waveOutOpen)(&h, WAVE_MAPPER, &f, (DWORD_PTR)ev, 0, CALLBACK_EVENT)) { CHECK(0); CloseHandle(ev); continue; }
        WaitForSingleObject(ev, 0);
        WAVEHDR hd[10]; char *d[10]; DWORD len;
        for (int i = 0; i < nb; i++) { d[i] = wcb_silence(&f, 20, &len); wcb_hdr(&hd[i], d[i], len); U(waveOutPrepareHeader)(h, &hd[i], sizeof hd[i]); U(waveOutWrite)(h, &hd[i], sizeof hd[i]); }
        /* Signals may coalesce on an auto-reset event: wait until the last header is DONE. */
        DWORD t0 = GetTickCount(); int sig = 0;
        while (!(hd[nb - 1].dwFlags & WHDR_DONE) && GetTickCount() - t0 < WCB_WAIT_MS) if (wcb_ev_wait(ev, 100)) sig++;
        CHECKF(sig >= 1, "no signal for %d buffers", nb);
        CHECKF(sig <= nb, "%d signals for %d buffers", sig, nb);
        for (int i = 0; i < nb; i++) EXPECT_EQ(hd[i].dwFlags, WHDR_DONE | WHDR_PREPARED);
        U(waveOutReset)(h);
        for (int i = 0; i < nb; i++) { U(waveOutUnprepareHeader)(h, &hd[i], sizeof hd[i]); HeapFree(GetProcessHeap(), 0, d[i]); }
        EXPECT_EQ(U(waveOutClose)(h), 0);
        CloseHandle(ev);
    }
}

TEST(wcb_ev_manual_reset_event) {
    NEED_OUT();
    WAVEFORMATEX f; wcb_fmt_default(&f);
    HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    HWAVEOUT h = NULL;
    if (U(waveOutOpen)(&h, WAVE_MAPPER, &f, (DWORD_PTR)ev, 0, CALLBACK_EVENT)) { CHECK(0); CloseHandle(ev); return; }
    EXPECT_EQ(WaitForSingleObject(ev, 0), WAIT_OBJECT_0);
    EXPECT_EQ(WaitForSingleObject(ev, 0), WAIT_OBJECT_0);   /* manual reset: stays signalled */
    ResetEvent(ev);
    WAVEHDR wh; DWORD len; char *d = wcb_silence(&f, 30, &len); wcb_hdr(&wh, d, len);
    U(waveOutPrepareHeader)(h, &wh, sizeof wh); U(waveOutWrite)(h, &wh, sizeof wh);
    EXPECT_EQ(WaitForSingleObject(ev, WCB_WAIT_MS), WAIT_OBJECT_0);
    CHECK(wh.dwFlags & WHDR_DONE);
    ResetEvent(ev);
    U(waveOutUnprepareHeader)(h, &wh, sizeof wh);
    EXPECT_EQ(U(waveOutClose)(h), 0);
    EXPECT_EQ(WaitForSingleObject(ev, 0), WAIT_OBJECT_0);
    HeapFree(GetProcessHeap(), 0, d);
    CloseHandle(ev);
}

TEST(wcb_ev_reset_signals) {
    NEED_OUT();
    WAVEFORMATEX f; wcb_fmt_default(&f);
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    HWAVEOUT h = NULL;
    if (U(waveOutOpen)(&h, WAVE_MAPPER, &f, (DWORD_PTR)ev, 0, CALLBACK_EVENT)) { CHECK(0); CloseHandle(ev); return; }
    WaitForSingleObject(ev, 0);
    WAVEHDR wh; DWORD len; char *d = wcb_silence(&f, 1000, &len); wcb_hdr(&wh, d, len);
    U(waveOutPrepareHeader)(h, &wh, sizeof wh); U(waveOutWrite)(h, &wh, sizeof wh);
    EXPECT_EQ(U(waveOutClose)(h), WAVERR_STILLPLAYING);
    EXPECT_EQ(WaitForSingleObject(ev, 0), WAIT_TIMEOUT);    /* a refused close signals nothing */
    EXPECT_EQ(U(waveOutReset)(h), 0);
    EXPECT_EQ(WaitForSingleObject(ev, WCB_WAIT_MS), WAIT_OBJECT_0);
    EXPECT_EQ(wh.dwFlags, WHDR_DONE | WHDR_PREPARED);
    U(waveOutUnprepareHeader)(h, &wh, sizeof wh);
    EXPECT_EQ(U(waveOutClose)(h), 0);
    EXPECT_EQ(WaitForSingleObject(ev, 0), WAIT_OBJECT_0);
    HeapFree(GetProcessHeap(), 0, d);
    CloseHandle(ev);
}

TEST(wcb_ev_query_and_badid_no_signal) {
    NEED_OUT();
    WAVEFORMATEX f; wcb_fmt_default(&f);
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    EXPECT_EQ(U(waveOutOpen)(NULL, WAVE_MAPPER, &f, (DWORD_PTR)ev, 0, CALLBACK_EVENT | WAVE_FORMAT_QUERY), 0);
    EXPECT_EQ(U(waveOutOpen)(NULL, 0, &f, (DWORD_PTR)ev, 0, CALLBACK_EVENT | WAVE_FORMAT_QUERY), 0);
    HWAVEOUT h = (HWAVEOUT)(DWORD_PTR)1;
    EXPECT_EQ(U(waveOutOpen)(&h, wcb_nout(), &f, (DWORD_PTR)ev, 0, CALLBACK_EVENT), MMSYSERR_BADDEVICEID);
    EXPECT_EQ(WaitForSingleObject(ev, 30), WAIT_TIMEOUT);
    CloseHandle(ev);
}

TEST(wcb_ev_null_event_handle) {
    NEED_OUT();
    /* CALLBACK_EVENT with a NULL handle: DriverCallback skips a NULL target. */
    WAVEFORMATEX f; wcb_fmt_default(&f);
    HWAVEOUT h = NULL;
    MMRESULT r = U(waveOutOpen)(&h, WAVE_MAPPER, &f, 0, 0, CALLBACK_EVENT);
    EXPECT_EQ(r, 0);
    if (r) return;
    EXPECT_EQ(wcb_play_silence_poll(h, &f, 2, 20), 2);
    EXPECT_EQ(U(waveOutClose)(h), 0);
}

TEST(wcb_ev_shared_by_two_handles) {
    NEED_OUT();
    WAVEFORMATEX f; wcb_fmt_default(&f);
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    HWAVEOUT a = NULL, b = NULL;
    if (U(waveOutOpen)(&a, WAVE_MAPPER, &f, (DWORD_PTR)ev, 0, CALLBACK_EVENT)) { CHECK(0); CloseHandle(ev); return; }
    EXPECT_EQ(WaitForSingleObject(ev, 0), WAIT_OBJECT_0);
    MMRESULT rb = U(waveOutOpen)(&b, 0, &f, (DWORD_PTR)ev, 0, CALLBACK_EVENT);
    EXPECT_EQ(rb, 0);
    if (!rb) {
        EXPECT_EQ(WaitForSingleObject(ev, 0), WAIT_OBJECT_0);
        EXPECT_EQ(U(waveOutClose)(b), 0);
        EXPECT_EQ(WaitForSingleObject(ev, 0), WAIT_OBJECT_0);
    }
    EXPECT_EQ(U(waveOutClose)(a), 0);
    EXPECT_EQ(WaitForSingleObject(ev, 0), WAIT_OBJECT_0);
    CloseHandle(ev);
}

/* ======================= CALLBACK_WINDOW ======================= */

TEST(wcb_win_open_queued_before_return) {
    NEED_OUT();
    UINT ids[6]; int k = wcb_out_ids(ids, 6);
    WAVEFORMATEX f; wcb_fmt_default(&f);
    for (int i = 0; i < k; i++) {
        HWND w = wcb_win_create();
        if (!w) { CHECK(0); continue; }
        HWAVEOUT h = NULL;
        MMRESULT r = U(waveOutOpen)(&h, ids[i], &f, (DWORD_PTR)w, 0, CALLBACK_WINDOW);
        CHECKF(r == 0, "%s window open %u", wcb_idname(ids[i]), r);
        if (!r) {
            /* posted during the open: already in this thread's queue */
            MSG m;
            BOOL have = PeekMessageA(&m, w, MM_WOM_OPEN, MM_WOM_OPEN, PM_REMOVE);
            CHECKF(have, "%s: WOM_OPEN not queued when waveOutOpen returned", wcb_idname(ids[i]));
            if (have) {
                EXPECT_EQ(m.message, MM_WOM_OPEN);
                EXPECT_EQ(m.wParam, (WPARAM)h);
                EXPECT_EQ(m.lParam, 0);
                EXPECT_EQ(m.hwnd, w);
            }
            EXPECT_EQ(PeekMessageA(&m, w, MM_WOM_OPEN, MM_WOM_DONE, PM_NOREMOVE), FALSE);
            EXPECT_EQ(U(waveOutClose)(h), 0);
            have = PeekMessageA(&m, w, MM_WOM_CLOSE, MM_WOM_CLOSE, PM_REMOVE);
            CHECKF(have, "%s: WOM_CLOSE not queued when waveOutClose returned", wcb_idname(ids[i]));
            if (have) { EXPECT_EQ(m.wParam, (WPARAM)h); EXPECT_EQ(m.lParam, 0); }
        }
        wcb_win_destroy(w);
    }
}

static void win_play(UINT id, const WAVEFORMATEX *f, int nb, DWORD ms) {
    HWND w = wcb_win_create();
    if (!w) { CHECK(0); return; }
    wcb_log L; wcb_log_init(&L);
    HWAVEOUT h = NULL;
    MMRESULT r = U(waveOutOpen)(&h, id, f, (DWORD_PTR)w, 0xDEAD, CALLBACK_WINDOW);
    CHECKF(r == 0, "%s window open %u", wcb_idname(id), r);
    if (r) { wcb_log_free(&L); wcb_win_destroy(w); return; }
    WAVEHDR hd[12]; char *d[12]; DWORD len;
    for (int i = 0; i < nb; i++) { d[i] = wcb_silence(f, ms, &len); wcb_hdr(&hd[i], d[i], len); U(waveOutPrepareHeader)(h, &hd[i], sizeof hd[i]); EXPECT_EQ(U(waveOutWrite)(h, &hd[i], sizeof hd[i]), 0); }
    wcb_win_pump(w, &L, 1 + nb, WCB_WAIT_MS);
    CHECKF(L.total == 1 + nb, "%s: %ld window messages for %d buffers", wcb_idname(id), L.total, nb);
    if (L.total < 1 + nb) U(waveOutReset)(h);
    for (int i = 0; i < nb; i++) { EXPECT_EQ(hd[i].dwFlags, WHDR_DONE | WHDR_PREPARED); EXPECT_EQ(U(waveOutUnprepareHeader)(h, &hd[i], sizeof hd[i]), 0); HeapFree(GetProcessHeap(), 0, d[i]); }
    EXPECT_EQ(U(waveOutClose)(h), 0);
    wcb_win_pump(w, &L, 2 + nb, 500);
    EXPECT_EQ(L.total, 2 + nb);
    EXPECT_EQ(wcb_count(&L, WOM_OPEN), 1);
    EXPECT_EQ(wcb_count(&L, WOM_DONE), nb);
    EXPECT_EQ(wcb_count(&L, WOM_CLOSE), 1);
    CHECK(wcb_check_done_order(&L, WOM_DONE, hd, nb));
    if (L.total >= 2) { EXPECT_EQ(L.r[0].msg, WOM_OPEN); EXPECT_EQ(L.r[L.total - 1].msg, WOM_CLOSE); }
    for (int i = 0; i < L.total && i < WCB_MAXREC; i++) {
        CHECK(L.r[i].h == (DWORD_PTR)h);     /* wParam = HWAVEOUT */
        if (L.r[i].msg != WOM_DONE) CHECK(L.r[i].p1 == 0);
    }
    wcb_log_free(&L);
    wcb_win_destroy(w);
}

TEST(wcb_win_done_each_id) {
    NEED_OUT();
    UINT ids[6]; int k = wcb_out_ids(ids, 6);
    WAVEFORMATEX f; wcb_fmt_default(&f);
    for (int i = 0; i < k; i++) win_play(ids[i], &f, 3, 30);
}

TEST(wcb_win_done_order_counts) {
    NEED_OUT();
    WAVEFORMATEX f; wcb_fmt_default(&f);
    int counts[] = { 1, 2, 5, 10 };
    for (int i = 0; i < 4; i++) win_play(WAVE_MAPPER, &f, counts[i], 20);
}

TEST(wcb_win_done_formats) {
    NEED_OUT();
    struct { DWORD rate; WORD bits, ch; } fm[] = { { 8000, 8, 1 }, { 22050, 16, 2 }, { 48000, 16, 2 }, { 32000, 16, 1 } };
    for (int i = 0; i < 4; i++) { WAVEFORMATEX f; wcb_fmt(&f, fm[i].rate, fm[i].bits, fm[i].ch); win_play(WAVE_MAPPER, &f, 2, 30); }
}

TEST(wcb_win_messages_not_dispatched_unless_pumped) {
    NEED_OUT();
    /* Nothing reaches the window procedure on its own: the messages wait in the queue of the window's thread. */
    WAVEFORMATEX f; wcb_fmt_default(&f);
    HWND w = wcb_win_create();
    HWAVEOUT h = NULL;
    if (U(waveOutOpen)(&h, WAVE_MAPPER, &f, (DWORD_PTR)w, 0, CALLBACK_WINDOW)) { CHECK(0); wcb_win_destroy(w); return; }
    WAVEHDR hd[3]; char *d[3]; DWORD len;
    for (int i = 0; i < 3; i++) { d[i] = wcb_silence(&f, 20, &len); wcb_hdr(&hd[i], d[i], len); U(waveOutPrepareHeader)(h, &hd[i], sizeof hd[i]); U(waveOutWrite)(h, &hd[i], sizeof hd[i]); }
    DWORD t0 = GetTickCount();
    while (!(hd[2].dwFlags & WHDR_DONE) && GetTickCount() - t0 < WCB_WAIT_MS) Sleep(10);
    CHECK(hd[2].dwFlags & WHDR_DONE);
    Sleep(30);
    EXPECT_EQ(wcb_win_peek_count(w, MM_WOM_OPEN, MM_WOM_DONE), 4);   /* OPEN + 3 DONE */
    MSG m; int order_ok = 1, n = 0;
    while (PeekMessageA(&m, w, MM_WOM_OPEN, MM_WOM_DONE, PM_REMOVE)) {
        if (n == 0 && m.message != MM_WOM_OPEN) order_ok = 0;
        if (n >= 1 && (m.message != MM_WOM_DONE || m.lParam != (LPARAM)&hd[n - 1])) order_ok = 0;
        n++;
    }
    CHECK(order_ok);
    EXPECT_EQ(n, 4);
    for (int i = 0; i < 3; i++) { U(waveOutUnprepareHeader)(h, &hd[i], sizeof hd[i]); HeapFree(GetProcessHeap(), 0, d[i]); }
    EXPECT_EQ(U(waveOutClose)(h), 0);
    wcb_win_destroy(w);
}

TEST(wcb_win_null_hwnd_no_messages) {
    NEED_OUT();
    /* CALLBACK_WINDOW with dwCallback 0: open succeeds, playback works, nothing is posted anywhere. */
    WAVEFORMATEX f; wcb_fmt_default(&f);
    UINT ids[6]; int k = wcb_out_ids(ids, 6);
    for (int i = 0; i < k; i++) {
        HWAVEOUT h = NULL;
        MMRESULT r = U(waveOutOpen)(&h, ids[i], &f, 0, 0, CALLBACK_WINDOW);
        CHECKF(r == 0, "%s NULL hwnd: %u", wcb_idname(ids[i]), r);
        if (r) continue;
        CHECK(h != NULL);
        EXPECT_EQ(wcb_play_silence_poll(h, &f, 2, 20), 2);
        EXPECT_EQ(U(waveOutClose)(h), 0);
        MSG m;
        EXPECT_EQ(PeekMessageA(&m, NULL, MM_WOM_OPEN, MM_WIM_DATA, PM_REMOVE), FALSE);
    }
}

TEST(wcb_win_reset_and_stillplaying) {
    NEED_OUT();
    WAVEFORMATEX f; wcb_fmt_default(&f);
    HWND w = wcb_win_create();
    wcb_log L; wcb_log_init(&L);
    HWAVEOUT h = NULL;
    if (U(waveOutOpen)(&h, WAVE_MAPPER, &f, (DWORD_PTR)w, 0, CALLBACK_WINDOW)) { CHECK(0); wcb_log_free(&L); wcb_win_destroy(w); return; }
    WAVEHDR hd[2]; char *d[2]; DWORD len;
    for (int i = 0; i < 2; i++) { d[i] = wcb_silence(&f, 800, &len); wcb_hdr(&hd[i], d[i], len); U(waveOutPrepareHeader)(h, &hd[i], sizeof hd[i]); U(waveOutWrite)(h, &hd[i], sizeof hd[i]); }
    EXPECT_EQ(U(waveOutClose)(h), WAVERR_STILLPLAYING);
    EXPECT_EQ(U(waveOutReset)(h), 0);
    for (int i = 0; i < 2; i++) { U(waveOutUnprepareHeader)(h, &hd[i], sizeof hd[i]); HeapFree(GetProcessHeap(), 0, d[i]); }
    EXPECT_EQ(U(waveOutClose)(h), 0);
    wcb_win_pump(w, &L, 4, WCB_WAIT_MS);
    EXPECT_EQ(L.total, 4);
    EXPECT_EQ(wcb_count(&L, WOM_CLOSE), 1);
    EXPECT_EQ(wcb_count(&L, WOM_DONE), 2);
    CHECK(wcb_check_done_order(&L, WOM_DONE, hd, 2));
    if (L.total == 4) { EXPECT_EQ(L.r[0].msg, WOM_OPEN); EXPECT_EQ(L.r[3].msg, WOM_CLOSE); }
    wcb_log_free(&L);
    wcb_win_destroy(w);
}

static HWND g_xwin; static HANDLE g_xready, g_xquit;
static DWORD WINAPI xwin_thread(LPVOID p) {
    g_xwin = wcb_win_create();
    SetEvent(g_xready);
    WaitForSingleObject(g_xquit, 10000);
    /* drain and count before destroying */
    wcb_log *L = (wcb_log *)p;
    wcb_win_pump(g_xwin, L, -1, 50);
    wcb_win_destroy(g_xwin);
    return 0;
}

TEST(wcb_win_window_of_other_thread) {
    NEED_OUT();
    /* Messages go to the window's own thread queue, not the opener's. */
    WAVEFORMATEX f; wcb_fmt_default(&f);
    wcb_log L; wcb_log_init(&L);
    g_xready = CreateEventA(NULL, TRUE, FALSE, NULL); g_xquit = CreateEventA(NULL, TRUE, FALSE, NULL);
    HANDLE th = CreateThread(NULL, 0, xwin_thread, &L, 0, NULL);
    if (WaitForSingleObject(g_xready, WCB_WAIT_MS) != WAIT_OBJECT_0 || !g_xwin) { CHECK(0); SetEvent(g_xquit); WaitForSingleObject(th, WCB_WAIT_MS); CloseHandle(th); return; }
    HWAVEOUT h = NULL;
    MMRESULT r = U(waveOutOpen)(&h, WAVE_MAPPER, &f, (DWORD_PTR)g_xwin, 0, CALLBACK_WINDOW);
    EXPECT_EQ(r, 0);
    if (!r) {
        EXPECT_EQ(wcb_play_silence_poll(h, &f, 1, 20), 1);
        EXPECT_EQ(U(waveOutClose)(h), 0);
        MSG m;
        EXPECT_EQ(PeekMessageA(&m, NULL, MM_WOM_OPEN, MM_WOM_DONE, PM_NOREMOVE), FALSE);
    }
    SetEvent(g_xquit);
    EXPECT_EQ(WaitForSingleObject(th, WCB_WAIT_MS), WAIT_OBJECT_0);
    CloseHandle(th);
    if (!r) {
        EXPECT_EQ(L.total, 3);
        EXPECT_EQ(wcb_count(&L, WOM_OPEN), 1); EXPECT_EQ(wcb_count(&L, WOM_DONE), 1); EXPECT_EQ(wcb_count(&L, WOM_CLOSE), 1);
    }
    CloseHandle(g_xready); CloseHandle(g_xquit);
    wcb_log_free(&L);
}

TEST(wcb_win_destroyed_after_open) {
    NEED_OUT();
    /* The window is checked only at open: destroying it later is harmless (PostMessageA just fails). */
    WAVEFORMATEX f; wcb_fmt_default(&f);
    HWND w = wcb_win_create();
    HWAVEOUT h = NULL;
    if (U(waveOutOpen)(&h, WAVE_MAPPER, &f, (DWORD_PTR)w, 0, CALLBACK_WINDOW)) { CHECK(0); wcb_win_destroy(w); return; }
    wcb_win_destroy(w);
    EXPECT_EQ(wcb_play_silence_poll(h, &f, 2, 20), 2);
    EXPECT_EQ(U(waveOutClose)(h), 0);
    MSG m;
    EXPECT_EQ(PeekMessageA(&m, NULL, MM_WOM_OPEN, MM_WOM_DONE, PM_REMOVE), FALSE);
}

/* ======================= CALLBACK_THREAD ======================= */

static void thr_seq_note(wcb_thr *T, const char *what) {
    char b[512]; int o = 0;
    int n = T->log.total < 24 ? T->log.total : 24;
    for (int i = 0; i < n && o < 480; i++) o += sprintf(b + o, "%s%s", i ? "," : "", T->log.r[i].msg == WM_USER ? "U" : wcb_msgname(T->log.r[i].msg));
    b[o] = 0;
    t_note("%s thread sequence: %s", what, b);
}

/* Remove the WM_USER entries (mmTaskSignal) and return the MM sequence. */
static int thr_mm(wcb_thr *T, wcb_rec *out, int cap) {
    int k = 0, n = T->log.total < WCB_MAXREC ? T->log.total : WCB_MAXREC;
    for (int i = 0; i < n && k < cap; i++) if (T->log.r[i].msg >= MM_WOM_OPEN && T->log.r[i].msg <= MM_WIM_DATA) out[k++] = T->log.r[i];
    return k;
}

TEST(wcb_thr_open_close_each_id) {
    NEED_OUT();
    UINT ids[6]; int k = wcb_out_ids(ids, 6);
    WAVEFORMATEX f; wcb_fmt_default(&f);
    for (int i = 0; i < k; i++) {
        wcb_thr T;
        if (!wcb_thr_start(&T)) { CHECK(0); wcb_thr_stop(&T); continue; }
        HWAVEOUT h = NULL;
        MMRESULT r = U(waveOutOpen)(&h, ids[i], &f, T.tid, 0x1111, CALLBACK_THREAD);
        CHECKF(r == 0, "%s thread open %u", wcb_idname(ids[i]), r);
        if (!r) {
            CHECK(wcb_thr_wait(&T, MM_WOM_OPEN, MM_WOM_OPEN, 1, WCB_WAIT_MS));
            EXPECT_EQ(U(waveOutClose)(h), 0);
            CHECK(wcb_thr_wait(&T, MM_WOM_CLOSE, MM_WOM_CLOSE, 1, WCB_WAIT_MS));
            Sleep(30);
            wcb_rec mm[8]; int n = thr_mm(&T, mm, 8);
            EXPECT_EQ(n, 2);
            if (n == 2) {
                EXPECT_EQ(mm[0].msg, WOM_OPEN); EXPECT_EQ(mm[0].h, (DWORD_PTR)h); EXPECT_EQ(mm[0].p1, 0);
                EXPECT_EQ(mm[1].msg, WOM_CLOSE); EXPECT_EQ(mm[1].h, (DWORD_PTR)h); EXPECT_EQ(mm[1].p1, 0);
                EXPECT_EQ(mm[0].tid, T.tid);
            }
            if (i == 0) thr_seq_note(&T, wcb_idname(ids[i]));
        }
        wcb_thr_stop(&T);
    }
}

TEST(wcb_thr_task_signal_follows_each_message) {
    NEED_OUT();
    /* DCB_TASK: every posted MM message is followed by WM_USER (0x400, wParam 0, lParam 0) from mmTaskSignal. */
    WAVEFORMATEX f; wcb_fmt_default(&f);
    wcb_thr T;
    if (!wcb_thr_start(&T)) { CHECK(0); wcb_thr_stop(&T); return; }
    HWAVEOUT h = NULL;
    if (U(waveOutOpen)(&h, WAVE_MAPPER, &f, T.tid, 0, CALLBACK_THREAD)) { CHECK(0); wcb_thr_stop(&T); return; }
    EXPECT_EQ(wcb_play_silence_poll(h, &f, 2, 20), 2);
    EXPECT_EQ(U(waveOutClose)(h), 0);
    CHECK(wcb_thr_wait(&T, MM_WOM_CLOSE, MM_WOM_CLOSE, 1, WCB_WAIT_MS));
    Sleep(50);
    thr_seq_note(&T, "mapper open/2 buffers/close");
    int n = T.log.total < WCB_MAXREC ? T.log.total : WCB_MAXREC;
    int mm = 0, user = 0, paired = 1;
    for (int i = 0; i < n; i++) {
        if (T.log.r[i].msg == WM_USER) { user++; if (T.log.r[i].h != 0 || T.log.r[i].p1 != 0) paired = 0; continue; }
        mm++;
        if (i + 1 >= n || T.log.r[i + 1].msg != WM_USER) paired = 0;
    }
    EXPECT_EQ(mm, 4);
    EXPECT_EQ(user, mm);
    CHECK(paired);
    wcb_thr_stop(&T);
}

static void thr_play(UINT id, const WAVEFORMATEX *f, int nb, DWORD ms) {
    wcb_thr T;
    if (!wcb_thr_start(&T)) { CHECK(0); wcb_thr_stop(&T); return; }
    HWAVEOUT h = NULL;
    MMRESULT r = U(waveOutOpen)(&h, id, f, T.tid, 0, CALLBACK_THREAD);
    CHECKF(r == 0, "%s thread open %u", wcb_idname(id), r);
    if (r) { wcb_thr_stop(&T); return; }
    WAVEHDR hd[12]; char *d[12]; DWORD len;
    for (int i = 0; i < nb; i++) { d[i] = wcb_silence(f, ms, &len); wcb_hdr(&hd[i], d[i], len); U(waveOutPrepareHeader)(h, &hd[i], sizeof hd[i]); EXPECT_EQ(U(waveOutWrite)(h, &hd[i], sizeof hd[i]), 0); }
    int got = wcb_thr_wait(&T, MM_WOM_DONE, MM_WOM_DONE, nb, WCB_WAIT_MS);
    CHECKF(got, "%s: %d of %d WOM_DONE", wcb_idname(id), wcb_count(&T.log, WOM_DONE), nb);
    if (!got) U(waveOutReset)(h);
    for (int i = 0; i < nb; i++) { U(waveOutUnprepareHeader)(h, &hd[i], sizeof hd[i]); HeapFree(GetProcessHeap(), 0, d[i]); }
    EXPECT_EQ(U(waveOutClose)(h), 0);
    CHECK(wcb_thr_wait(&T, MM_WOM_CLOSE, MM_WOM_CLOSE, 1, WCB_WAIT_MS));
    wcb_rec mm[32]; int n = thr_mm(&T, mm, 32);
    EXPECT_EQ(n, nb + 2);
    if (n == nb + 2) {
        EXPECT_EQ(mm[0].msg, WOM_OPEN);
        int ok = 1;
        for (int i = 0; i < nb; i++) if (mm[1 + i].msg != WOM_DONE || mm[1 + i].p1 != (DWORD_PTR)&hd[i] || mm[1 + i].h != (DWORD_PTR)h) ok = 0;
        CHECKF(ok, "%s: WOM_DONE sequence (lParam = LPWAVEHDR in submission order)", wcb_idname(id));
        EXPECT_EQ(mm[nb + 1].msg, WOM_CLOSE);
    }
    wcb_thr_stop(&T);
}

TEST(wcb_thr_done_each_id) {
    NEED_OUT();
    UINT ids[6]; int k = wcb_out_ids(ids, 6);
    WAVEFORMATEX f; wcb_fmt_default(&f);
    for (int i = 0; i < k; i++) thr_play(ids[i], &f, 3, 30);
}

TEST(wcb_thr_done_order_counts) {
    NEED_OUT();
    WAVEFORMATEX f; wcb_fmt_default(&f);
    int counts[] = { 1, 4, 8 };
    for (int i = 0; i < 3; i++) thr_play(WAVE_MAPPER, &f, counts[i], 20);
}

TEST(wcb_thr_target_is_caller_thread) {
    NEED_OUT();
    /* CALLBACK_THREAD naming the opener's own thread: messages are in its queue after open. */
    WAVEFORMATEX f; wcb_fmt_default(&f);
    MSG m;
    PeekMessageA(&m, NULL, 0, 0, PM_NOREMOVE);
    while (PeekMessageA(&m, (HWND)-1, 0, 0, PM_REMOVE)) {}
    HWAVEOUT h = NULL;
    if (U(waveOutOpen)(&h, WAVE_MAPPER, &f, GetCurrentThreadId(), 0, CALLBACK_THREAD)) { CHECK(0); return; }
    BOOL have = PeekMessageA(&m, (HWND)-1, MM_WOM_OPEN, MM_WOM_OPEN, PM_REMOVE);
    CHECKF(have, "WOM_OPEN not in the caller's thread queue at return");
    if (have) { EXPECT_EQ(m.wParam, (WPARAM)h); EXPECT_EQ(m.lParam, 0); EXPECT_EQ(m.hwnd, NULL); }
    have = PeekMessageA(&m, (HWND)-1, WM_USER, WM_USER, PM_REMOVE);
    CHECKF(have, "no WM_USER task signal after WOM_OPEN");
    EXPECT_EQ(U(waveOutClose)(h), 0);
    have = PeekMessageA(&m, (HWND)-1, MM_WOM_CLOSE, MM_WOM_CLOSE, PM_REMOVE);
    CHECKF(have, "WOM_CLOSE not queued at return");
    while (PeekMessageA(&m, (HWND)-1, 0, 0, PM_REMOVE)) {}
}

static DWORD WINAPI noqueue_thread(LPVOID p) { WaitForSingleObject((HANDLE)p, 10000); return 0; }

TEST(wcb_thr_no_queue_thread_and_zero_tid) {
    NEED_OUT();
    /* Thread target 0 is a NULL target (no delivery); a thread id without a queue just loses the posts.
     * Neither affects the open or the buffer protocol. */
    WAVEFORMATEX f; wcb_fmt_default(&f);
    HWAVEOUT h = NULL;
    MMRESULT r = U(waveOutOpen)(&h, WAVE_MAPPER, &f, 0, 0, CALLBACK_THREAD);
    EXPECT_EQ(r, 0);
    if (!r) { EXPECT_EQ(wcb_play_silence_poll(h, &f, 1, 20), 1); EXPECT_EQ(U(waveOutClose)(h), 0); }
    HANDLE q = CreateEventA(NULL, TRUE, FALSE, NULL);
    DWORD tid; HANDLE th = CreateThread(NULL, 0, noqueue_thread, q, 0, &tid);
    /* a thread that never touches user32, so it has no message queue */
    r = U(waveOutOpen)(&h, WAVE_MAPPER, &f, tid, 0, CALLBACK_THREAD);
    t_note("CALLBACK_THREAD to a thread without a message queue: open %u", r);
    if (!r) { EXPECT_EQ(wcb_play_silence_poll(h, &f, 1, 20), 1); EXPECT_EQ(U(waveOutClose)(h), 0); }
    SetEvent(q);
    EXPECT_EQ(WaitForSingleObject(th, WCB_WAIT_MS), WAIT_OBJECT_0);
    CloseHandle(th); CloseHandle(q);
}
