/* waveout_callbacks: CALLBACK_NULL and the callback type field on real opens (device half), plus the
 * winmmbase handle protocol around a callback-bearing handle. Unknown types (4, 6, 7) on a real open are the
 * driver's decision: recorded with t_note, and every handle that opens is closed.
 * VM_TIMEOUT: 240 */
#include "wcb.h"

#define NEED_OUT() do { REQUIRE(U(waveOutOpen)); if (wcb_nout() == 0) { t_skip("no waveOut device (count 0): contract half only"); return; } wcb_note_devices(); } while (0)

TEST(wcb_null_open_play_close_each_id) {
    NEED_OUT();
    UINT ids[6]; int k = wcb_out_ids(ids, 6);
    WAVEFORMATEX f; wcb_fmt_default(&f);
    for (int i = 0; i < k; i++) {
        HWAVEOUT h = NULL;
        MMRESULT r = U(waveOutOpen)(&h, ids[i], &f, 0, 0, CALLBACK_NULL);
        CHECKF(r == 0, "%s CALLBACK_NULL open %u", wcb_idname(ids[i]), r);
        if (r) continue;
        CHECK(h != NULL);
        EXPECT_EQ(wcb_play_silence_poll(h, &f, 3, 20), 3);
        EXPECT_EQ(U(waveOutClose)(h), 0);
    }
}

TEST(wcb_null_ignores_callback_value) {
    NEED_OUT();
    /* CALLBACK_NULL with a non-zero dwCallback/dwInstance: nothing is called, nothing is posted. */
    WAVEFORMATEX f; wcb_fmt_default(&f);
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    wcb_log L; wcb_log_init(&L); wcb_fn_register(&L, 0xC000);
    DWORD_PTR vals[] = { (DWORD_PTR)ev, (DWORD_PTR)wcb_fn_out, GetCurrentThreadId() };
    for (int i = 0; i < 3; i++) {
        HWAVEOUT h = NULL;
        MMRESULT r = U(waveOutOpen)(&h, WAVE_MAPPER, &f, vals[i], 0xC000, CALLBACK_NULL);
        EXPECT_EQ(r, 0);
        if (r) continue;
        EXPECT_EQ(wcb_play_silence_poll(h, &f, 1, 20), 1);
        EXPECT_EQ(U(waveOutClose)(h), 0);
    }
    Sleep(30);
    EXPECT_EQ(WaitForSingleObject(ev, 0), WAIT_TIMEOUT);
    EXPECT_EQ(L.total, 0);
    MSG m;
    EXPECT_EQ(PeekMessageA(&m, (HWND)-1, MM_WOM_OPEN, MM_WOM_DONE, PM_REMOVE), FALSE);
    wcb_fn_unregister(&L); wcb_log_free(&L);
    CloseHandle(ev);
}

TEST(wcb_null_header_flag_progression) {
    NEED_OUT();
    /* Without callbacks the header bits are the only completion signal: PREPARED -> PREPARED|INQUEUE -> DONE|PREPARED. */
    WAVEFORMATEX f; wcb_fmt_default(&f);
    HWAVEOUT h = NULL;
    if (U(waveOutOpen)(&h, WAVE_MAPPER, &f, 0, 0, CALLBACK_NULL)) { CHECK(0); return; }
    WAVEHDR wh; DWORD len; char *d = wcb_silence(&f, 200, &len); wcb_hdr(&wh, d, len);
    EXPECT_EQ(U(waveOutPrepareHeader)(h, &wh, sizeof wh), 0);
    EXPECT_EQ(wh.dwFlags, WHDR_PREPARED);
    EXPECT_EQ(U(waveOutWrite)(h, &wh, sizeof wh), 0);
    DWORD f1 = wh.dwFlags;
    CHECKF(f1 == (WHDR_PREPARED | WHDR_INQUEUE) || f1 == (WHDR_PREPARED | WHDR_DONE), "flags right after write 0x%lx", f1);
    DWORD t0 = GetTickCount();
    while (!(wh.dwFlags & WHDR_DONE) && GetTickCount() - t0 < WCB_WAIT_MS) Sleep(5);
    EXPECT_EQ(wh.dwFlags, WHDR_DONE | WHDR_PREPARED);
    EXPECT_EQ(U(waveOutUnprepareHeader)(h, &wh, sizeof wh), 0);
    EXPECT_EQ(wh.dwFlags, WHDR_DONE);
    EXPECT_EQ(U(waveOutClose)(h), 0);
    HeapFree(GetProcessHeap(), 0, d);
}

/* Unknown callback types on a real open: record the result and that nothing reaches any target. */
TEST(wcb_unknown_types_real_open) {
    NEED_OUT();
    WAVEFORMATEX f; wcb_fmt_default(&f);
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    DWORD types[] = { 0x40000, 0x60000, 0x70000 };
    UINT ids[] = { WAVE_MAPPER, 0 };
    for (int t = 0; t < 3; t++)
        for (int i = 0; i < 2; i++) {
            HWAVEOUT h = NULL;
            MMRESULT r = U(waveOutOpen)(&h, ids[i], &f, (DWORD_PTR)ev, 0, types[t]);
            t_note("callback type 0x%lx %s real open: %u", types[t], wcb_idname(ids[i]), r);
            CHECKF(r == MMSYSERR_NOERROR || r == MMSYSERR_INVALFLAG || r == MMSYSERR_INVALPARAM, "type 0x%lx: %u", types[t], r);
            if (r == 0) {
                CHECK(h != NULL);
                EXPECT_EQ(wcb_play_silence_poll(h, &f, 1, 20), 1);
                EXPECT_EQ(U(waveOutClose)(h), 0);
            } else CHECK(h == NULL);
        }
    /* DriverCallback dispatches only on 1/2/3/5: an event handle under type 4/6/7 is never signalled. */
    EXPECT_EQ(WaitForSingleObject(ev, 30), WAIT_TIMEOUT);
    CloseHandle(ev);
}

TEST(wcb_unknown_types_query_ok) {
    NEED_OUT();
    WAVEFORMATEX f; wcb_fmt_default(&f);
    DWORD types[] = { 0x40000, 0x60000, 0x70000 };
    for (int t = 0; t < 3; t++) {
        EXPECT_EQ(U(waveOutOpen)(NULL, WAVE_MAPPER, &f, 0x1234, 0, types[t] | WAVE_FORMAT_QUERY), 0);
        EXPECT_EQ(U(waveOutOpen)(NULL, 0, &f, 0x1234, 0, types[t] | WAVE_FORMAT_QUERY), 0);
    }
}

TEST(wcb_type_high_bits_real_open) {
    NEED_OUT();
    /* Bits above the CALLBACK_TYPEMASK field (0x80000..) are outside both the winmmbase flag masks and the
     * type field: record what the driver does with EVENT|0x80000 (field value 0xD). */
    WAVEFORMATEX f; wcb_fmt_default(&f);
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    DWORD fl[] = { CALLBACK_EVENT | 0x80000, CALLBACK_EVENT | 0x100000, CALLBACK_EVENT | 0x01000000 };
    for (int i = 0; i < 3; i++) {
        HWAVEOUT h = NULL;
        MMRESULT r = U(waveOutOpen)(&h, WAVE_MAPPER, &f, (DWORD_PTR)ev, 0, fl[i]);
        int sig = r == 0 && WaitForSingleObject(ev, 0) == WAIT_OBJECT_0;
        t_note("flags 0x%lx real open: %u, open signalled the event: %d", fl[i], r, sig);
        if (r == 0) { EXPECT_EQ(U(waveOutClose)(h), 0); WaitForSingleObject(ev, 0); }
    }
    CloseHandle(ev);
}

/* ---- handle protocol around a callback-bearing handle ---- */

TEST(wcb_handle_getid_on_callback_handles) {
    NEED_OUT();
    WAVEFORMATEX f; wcb_fmt_default(&f);
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    UINT ids[6]; int k = wcb_out_ids(ids, 6);
    for (int i = 0; i < k; i++) {
        HWAVEOUT h = NULL;
        if (U(waveOutOpen)(&h, ids[i], &f, (DWORD_PTR)ev, 0, CALLBACK_EVENT)) { CHECK(0); continue; }
        UINT got = 0xABCD;
        EXPECT_EQ(U(waveOutGetID)(h, &got), 0);
        EXPECT_EQ(got, ids[i]);   /* the id stored at open: WAVE_MAPPER stays 0xFFFFFFFF */
        EXPECT_EQ(U(waveOutGetID)(h, NULL), MMSYSERR_INVALPARAM);
        EXPECT_EQ(U(waveOutClose)(h), 0);
        EXPECT_EQ(U(waveOutGetID)(h, &got), MMSYSERR_INVALHANDLE);
    }
    CloseHandle(ev);
}

TEST(wcb_handle_cross_type_rejected) {
    NEED_OUT();
    /* An HWAVEOUT given to waveIn* is INVALHANDLE and triggers no callback on the waveOut handle. */
    REQUIRE(U(waveInClose)); REQUIRE(U(waveInReset)); REQUIRE(U(waveInStart));
    WAVEFORMATEX f; wcb_fmt_default(&f);
    wcb_log L; wcb_log_init(&L); wcb_fn_register(&L, 0xC100);
    HWAVEOUT h = NULL;
    if (U(waveOutOpen)(&h, WAVE_MAPPER, &f, (DWORD_PTR)wcb_fn_out, 0xC100, CALLBACK_FUNCTION)) { CHECK(0); wcb_fn_unregister(&L); wcb_log_free(&L); return; }
    EXPECT_EQ(L.total, 1);
    EXPECT_EQ(U(waveInClose)((HWAVEIN)h), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveInReset)((HWAVEIN)h), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveInStart)((HWAVEIN)h), MMSYSERR_INVALHANDLE);
    Sleep(20);
    EXPECT_EQ(L.total, 1);
    EXPECT_EQ(U(waveOutClose)(h), 0);
    EXPECT_EQ(L.total, 2);
    wcb_fn_unregister(&L); wcb_log_free(&L);
}

TEST(wcb_handle_reset_idle_no_callback) {
    NEED_OUT();
    /* Reset/Pause/Restart on an idle handle deliver nothing. */
    WAVEFORMATEX f; wcb_fmt_default(&f);
    wcb_log L; wcb_log_init(&L); wcb_fn_register(&L, 0xC200);
    HWAVEOUT h = NULL;
    if (U(waveOutOpen)(&h, WAVE_MAPPER, &f, (DWORD_PTR)wcb_fn_out, 0xC200, CALLBACK_FUNCTION)) { CHECK(0); wcb_fn_unregister(&L); wcb_log_free(&L); return; }
    EXPECT_EQ(U(waveOutReset)(h), 0);
    EXPECT_EQ(U(waveOutPause)(h), 0);
    EXPECT_EQ(U(waveOutRestart)(h), 0);
    EXPECT_EQ(U(waveOutReset)(h), 0);
    EXPECT_EQ(U(waveOutBreakLoop)(h), 0);
    Sleep(30);
    EXPECT_EQ(L.total, 1);
    EXPECT_EQ(U(waveOutClose)(h), 0);
    EXPECT_EQ(L.total, 2);
    wcb_fn_unregister(&L); wcb_log_free(&L);
}

TEST(wcb_handle_write_errors_no_callback) {
    NEED_OUT();
    /* Rejected writes (unprepared 34, already queued 33, bad size 11) never produce WOM_DONE. */
    WAVEFORMATEX f; wcb_fmt_default(&f);
    wcb_log L; wcb_log_init(&L); wcb_fn_register(&L, 0xC300);
    HWAVEOUT h = NULL;
    if (U(waveOutOpen)(&h, WAVE_MAPPER, &f, (DWORD_PTR)wcb_fn_out, 0xC300, CALLBACK_FUNCTION)) { CHECK(0); wcb_fn_unregister(&L); wcb_log_free(&L); return; }
    WAVEHDR wh; DWORD len; char *d = wcb_silence(&f, 300, &len); wcb_hdr(&wh, d, len);
    EXPECT_EQ(U(waveOutWrite)(h, &wh, sizeof wh), WAVERR_UNPREPARED);
    EXPECT_EQ(U(waveOutWrite)(h, &wh, sizeof wh - 1), MMSYSERR_INVALPARAM);
    EXPECT_EQ(U(waveOutPrepareHeader)(h, &wh, sizeof wh), 0);
    EXPECT_EQ(U(waveOutWrite)(h, &wh, sizeof wh), 0);
    EXPECT_EQ(U(waveOutWrite)(h, &wh, sizeof wh), WAVERR_STILLPLAYING);
    EXPECT_EQ(U(waveOutUnprepareHeader)(h, &wh, sizeof wh), WAVERR_STILLPLAYING);
    CHECK(wcb_wait_total(&L, 2, WCB_WAIT_MS));
    Sleep(50);
    EXPECT_EQ(wcb_count(&L, WOM_DONE), 1);
    EXPECT_EQ(U(waveOutUnprepareHeader)(h, &wh, sizeof wh), 0);
    EXPECT_EQ(U(waveOutClose)(h), 0);
    EXPECT_EQ(wcb_count(&L, WOM_CLOSE), 1);
    HeapFree(GetProcessHeap(), 0, d);
    wcb_fn_unregister(&L); wcb_log_free(&L);
}

TEST(wcb_handle_rewrite_after_done) {
    NEED_OUT();
    /* A DONE header can be written again without re-preparing: one more WOM_DONE with the same LPWAVEHDR. */
    WAVEFORMATEX f; wcb_fmt_default(&f);
    wcb_log L; wcb_log_init(&L); wcb_fn_register(&L, 0xC400);
    HWAVEOUT h = NULL;
    if (U(waveOutOpen)(&h, WAVE_MAPPER, &f, (DWORD_PTR)wcb_fn_out, 0xC400, CALLBACK_FUNCTION)) { CHECK(0); wcb_fn_unregister(&L); wcb_log_free(&L); return; }
    WAVEHDR wh; DWORD len; char *d = wcb_silence(&f, 20, &len); wcb_hdr(&wh, d, len);
    U(waveOutPrepareHeader)(h, &wh, sizeof wh);
    for (int i = 0; i < 4; i++) {
        EXPECT_EQ(U(waveOutWrite)(h, &wh, sizeof wh), 0);
        CHECK(wcb_wait_total(&L, 2 + i, WCB_WAIT_MS));
        EXPECT_EQ(wh.dwFlags, WHDR_DONE | WHDR_PREPARED);
    }
    EXPECT_EQ(wcb_count(&L, WOM_DONE), 4);
    int n = L.total < WCB_MAXREC ? L.total : WCB_MAXREC;
    for (int i = 0; i < n; i++) if (L.r[i].msg == WOM_DONE) CHECK(L.r[i].p1 == (DWORD_PTR)&wh);
    U(waveOutUnprepareHeader)(h, &wh, sizeof wh);
    EXPECT_EQ(U(waveOutClose)(h), 0);
    HeapFree(GetProcessHeap(), 0, d);
    wcb_fn_unregister(&L); wcb_log_free(&L);
}

static HWAVEOUT s_oth_h; static WAVEHDR s_oth_wh; static MMRESULT s_oth_r;
static DWORD WINAPI oth_writer(LPVOID p) { s_oth_r = U(waveOutWrite)(s_oth_h, &s_oth_wh, sizeof s_oth_wh); return 0; }

TEST(wcb_handle_write_from_other_thread) {
    NEED_OUT();
    /* A buffer written from a second thread completes through the handle's callback; WOM_DONE is not
     * delivered on the writer thread's behalf after it exits (the handle owns the delivery). */
    WAVEFORMATEX f; wcb_fmt_default(&f);
    wcb_log L; wcb_log_init(&L); wcb_fn_register(&L, 0xC500);
    if (U(waveOutOpen)(&s_oth_h, WAVE_MAPPER, &f, (DWORD_PTR)wcb_fn_out, 0xC500, CALLBACK_FUNCTION)) { CHECK(0); wcb_fn_unregister(&L); wcb_log_free(&L); return; }
    DWORD len; char *d = wcb_silence(&f, 30, &len); wcb_hdr(&s_oth_wh, d, len);
    EXPECT_EQ(U(waveOutPrepareHeader)(s_oth_h, &s_oth_wh, sizeof s_oth_wh), 0);
    s_oth_r = 0xFFFF;
    DWORD wtid = 0;
    HANDLE th = CreateThread(NULL, 0, oth_writer, NULL, 0, &wtid);
    CHECK(th != NULL);
    if (th) { EXPECT_EQ(WaitForSingleObject(th, WCB_WAIT_MS), WAIT_OBJECT_0); CloseHandle(th); }
    EXPECT_EQ(s_oth_r, 0);
    CHECK(wcb_wait_total(&L, 2, WCB_WAIT_MS));
    EXPECT_EQ(wcb_count(&L, WOM_DONE), 1);
    if (L.total >= 2) { EXPECT_EQ(L.r[1].p1, (DWORD_PTR)&s_oth_wh); CHECK(L.r[1].tid != wtid); }
    U(waveOutUnprepareHeader)(s_oth_h, &s_oth_wh, sizeof s_oth_wh);
    EXPECT_EQ(U(waveOutClose)(s_oth_h), 0);
    HeapFree(GetProcessHeap(), 0, d);
    wcb_fn_unregister(&L); wcb_log_free(&L);
}

TEST(wcb_mixed_types_simultaneous) {
    NEED_OUT();
    /* Four handles with four delivery types open at once; each sees exactly its own OPEN/DONE/CLOSE. */
    WAVEFORMATEX f; wcb_fmt_default(&f);
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    HWND w = wcb_win_create();
    wcb_thr T; int thr_ok = wcb_thr_start(&T);
    wcb_log FL; wcb_log_init(&FL); wcb_fn_register(&FL, 0xC600);
    wcb_log WL; wcb_log_init(&WL);
    HWAVEOUT h[4] = {0};
    MMRESULT r[4];
    r[0] = U(waveOutOpen)(&h[0], WAVE_MAPPER, &f, (DWORD_PTR)wcb_fn_out, 0xC600, CALLBACK_FUNCTION);
    r[1] = U(waveOutOpen)(&h[1], WAVE_MAPPER, &f, (DWORD_PTR)ev, 0, CALLBACK_EVENT);
    r[2] = U(waveOutOpen)(&h[2], WAVE_MAPPER, &f, (DWORD_PTR)w, 0, CALLBACK_WINDOW);
    r[3] = thr_ok ? U(waveOutOpen)(&h[3], WAVE_MAPPER, &f, T.tid, 0, CALLBACK_THREAD) : 99;
    for (int i = 0; i < 4; i++) EXPECT_EQ(r[i], 0);
    WAVEHDR hd[4]; char *d[4]; DWORD len;
    for (int i = 0; i < 4; i++) {
        d[i] = wcb_silence(&f, 30, &len); wcb_hdr(&hd[i], d[i], len);
        if (r[i]) continue;
        U(waveOutPrepareHeader)(h[i], &hd[i], sizeof hd[i]);
        EXPECT_EQ(U(waveOutWrite)(h[i], &hd[i], sizeof hd[i]), 0);
    }
    DWORD t0 = GetTickCount();
    for (;;) {
        int all = 1;
        for (int i = 0; i < 4; i++) if (!r[i] && !(hd[i].dwFlags & WHDR_DONE)) all = 0;
        if (all || GetTickCount() - t0 > WCB_WAIT_MS) break;
        Sleep(10);
    }
    for (int i = 0; i < 4; i++) if (!r[i]) { EXPECT_EQ(hd[i].dwFlags, WHDR_DONE | WHDR_PREPARED); U(waveOutUnprepareHeader)(h[i], &hd[i], sizeof hd[i]); EXPECT_EQ(U(waveOutClose)(h[i]), 0); }
    if (!r[0]) {
        EXPECT_EQ(FL.total, 3);
        for (int i = 0; i < FL.total && i < 3; i++) CHECK(FL.r[i].h == (DWORD_PTR)h[0]);
    }
    if (!r[2]) {
        wcb_win_pump(w, &WL, 3, WCB_WAIT_MS);
        EXPECT_EQ(WL.total, 3);
        for (int i = 0; i < WL.total && i < 3; i++) CHECK(WL.r[i].h == (DWORD_PTR)h[2]);
    }
    if (!r[3]) {
        CHECK(wcb_thr_wait(&T, MM_WOM_CLOSE, MM_WOM_CLOSE, 1, WCB_WAIT_MS));
        EXPECT_EQ(wcb_count_range(&T.log, MM_WOM_OPEN, MM_WOM_DONE), 3);
        int n = T.log.total < WCB_MAXREC ? T.log.total : WCB_MAXREC;
        for (int i = 0; i < n; i++) if (T.log.r[i].msg != WM_USER) CHECK(T.log.r[i].h == (DWORD_PTR)h[3]);
    }
    for (int i = 0; i < 4; i++) HeapFree(GetProcessHeap(), 0, d[i]);
    wcb_fn_unregister(&FL); wcb_log_free(&FL); wcb_log_free(&WL);
    if (thr_ok) wcb_thr_stop(&T);
    wcb_win_destroy(w);
    CloseHandle(ev);
}
