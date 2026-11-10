/* waveout_callbacks: waveIn callback delivery (WIM_OPEN 0x3BE, WIM_DATA 0x3C0, WIM_CLOSE 0x3BF) on a capture
 * device (skipped with 0 waveIn devices). WIM_DATA carries the LPWAVEHDR with dwBytesRecorded > 0 for a buffer
 * filled while recording. Capture content is not inspected (the wav backend records silence).
 * VM_TIMEOUT: 240 */
#include "wcb.h"

#define NEED_IN() do { REQUIRE(U(waveInOpen)); if (wcb_nin() == 0) { t_skip("no waveIn device (count 0): contract half only"); return; } wcb_note_devices(); } while (0)

typedef struct { wcb_log L; volatile DWORD_PTR hwatch; HWAVEIN h; } inctx;

static MMRESULT in_open_fn(inctx *c, UINT id, const WAVEFORMATEX *f, DWORD_PTR inst, DWORD extra) {
    wcb_log_init(&c->L);
    c->hwatch = 0;
    c->L.watch = &c->hwatch;
    wcb_fn_register(&c->L, inst);
    MMRESULT r = U(waveInOpen)((HWAVEIN *)&c->hwatch, id, f, (DWORD_PTR)wcb_fn_in, inst, CALLBACK_FUNCTION | extra);
    c->h = (HWAVEIN)c->hwatch;
    return r;
}
static void in_done(inctx *c) { wcb_fn_unregister(&c->L); wcb_log_free(&c->L); }

TEST(wcb_in_fn_open_close_each_id) {
    NEED_IN();
    UINT ids[6]; int k = wcb_in_ids(ids, 6);
    WAVEFORMATEX f; wcb_fmt(&f, 44100, 16, 1);
    DWORD me = GetCurrentThreadId();
    for (int i = 0; i < k; i++) {
        inctx c; DWORD_PTR inst = 0xA0000000F0000000ull + i;
        MMRESULT r = in_open_fn(&c, ids[i], &f, inst, 0);
        CHECKF(r == 0, "%s waveIn function open %u", wcb_idname(ids[i]), r);
        if (r) { EXPECT_EQ(c.L.total, 0); in_done(&c); continue; }
        EXPECT_EQ(c.L.total, 1);
        if (c.L.total >= 1) {
            EXPECT_EQ(c.L.r[0].msg, WIM_OPEN);
            EXPECT_EQ(c.L.r[0].h, (DWORD_PTR)c.h);
            CHECK(c.L.r[0].inst == inst);
            EXPECT_EQ(c.L.r[0].p1, 0);
            EXPECT_EQ(c.L.r[0].p2, 0);
            EXPECT_EQ(c.L.r[0].phandle, 0);   /* *phwi stored after the driver open returns */
            t_note("%s WIM_OPEN on %s thread", wcb_idname(ids[i]), c.L.r[0].tid == me ? "caller" : "other");
        }
        EXPECT_EQ(U(waveInClose)(c.h), 0);
        EXPECT_EQ(c.L.total, 2);
        if (c.L.total >= 2) {
            EXPECT_EQ(c.L.r[1].msg, WIM_CLOSE);
            EXPECT_EQ(c.L.r[1].h, (DWORD_PTR)c.h);
            CHECK(c.L.r[1].inst == inst);
            EXPECT_EQ(c.L.r[1].p1, 0);
        }
        Sleep(30);
        EXPECT_EQ(c.L.total, 2);
        in_done(&c);
    }
}

static void in_record(UINT id, const WAVEFORMATEX *f, int nb, DWORD ms, DWORD_PTR inst) {
    inctx c;
    MMRESULT r = in_open_fn(&c, id, f, inst, 0);
    CHECKF(r == 0, "%s %lu/%u/%u waveIn open %u", wcb_idname(id), f->nSamplesPerSec, f->wBitsPerSample, f->nChannels, r);
    if (r) { in_done(&c); return; }
    WAVEHDR hd[8]; char *d[8]; DWORD len;
    for (int i = 0; i < nb; i++) {
        d[i] = wcb_silence(f, ms, &len); wcb_hdr(&hd[i], d[i], len);
        EXPECT_EQ(U(waveInPrepareHeader)(c.h, &hd[i], sizeof hd[i]), 0);
        EXPECT_EQ(U(waveInAddBuffer)(c.h, &hd[i], sizeof hd[i]), 0);
    }
    Sleep(20);
    EXPECT_EQ(wcb_count(&c.L, WIM_DATA), 0);   /* nothing is recorded before waveInStart */
    EXPECT_EQ(U(waveInStart)(c.h), 0);
    int got = wcb_wait_total(&c.L, 1 + nb, WCB_WAIT_MS);
    CHECKF(got, "%s: %d of %d WIM_DATA", wcb_idname(id), wcb_count(&c.L, WIM_DATA), nb);
    EXPECT_EQ(U(waveInStop)(c.h), 0);
    EXPECT_EQ(U(waveInReset)(c.h), 0);
    wcb_wait_total(&c.L, 1 + nb, 300);
    EXPECT_EQ(wcb_count(&c.L, WIM_DATA), nb);
    CHECK(wcb_check_done_order(&c.L, WIM_DATA, hd, nb));
    int n = c.L.total < WCB_MAXREC ? c.L.total : WCB_MAXREC;
    for (int i = 0; i < n; i++) {
        wcb_rec *e = &c.L.r[i];
        if (e->msg != WIM_DATA) continue;
        CHECK(e->h == (DWORD_PTR)c.h);
        CHECK(e->inst == inst);
        EXPECT_EQ(e->p2, 0);
        CHECKF(e->hdr_flags & WHDR_DONE, "flags at WIM_DATA 0x%lx", e->hdr_flags);
        CHECKF(!(e->hdr_flags & WHDR_INQUEUE), "INQUEUE still set at WIM_DATA 0x%lx", e->hdr_flags);
        WAVEHDR *wh = (WAVEHDR *)e->p1;
        CHECKF(e->hdr_bytes > 0 && e->hdr_bytes <= wh->dwBufferLength, "dwBytesRecorded %lu of %lu", e->hdr_bytes, wh->dwBufferLength);
    }
    for (int i = 0; i < nb; i++) {
        EXPECT_EQ(hd[i].dwFlags, WHDR_DONE | WHDR_PREPARED);
        EXPECT_EQ(hd[i].dwBytesRecorded, hd[i].dwBufferLength);   /* filled while recording */
        EXPECT_EQ(U(waveInUnprepareHeader)(c.h, &hd[i], sizeof hd[i]), 0);
        HeapFree(GetProcessHeap(), 0, d[i]);
    }
    EXPECT_EQ(U(waveInClose)(c.h), 0);
    EXPECT_EQ(wcb_count(&c.L, WIM_OPEN), 1);
    EXPECT_EQ(wcb_count(&c.L, WIM_CLOSE), 1);
    if (c.L.total >= 2) { EXPECT_EQ(c.L.r[0].msg, WIM_OPEN); EXPECT_EQ(c.L.r[c.L.total - 1].msg, WIM_CLOSE); }
    in_done(&c);
}

TEST(wcb_in_fn_data_each_id) {
    NEED_IN();
    UINT ids[6]; int k = wcb_in_ids(ids, 6);
    WAVEFORMATEX f; wcb_fmt(&f, 44100, 16, 1);
    for (int i = 0; i < k; i++) in_record(ids[i], &f, 2, 50, 0xB000 + i);
}

TEST(wcb_in_fn_data_counts) {
    NEED_IN();
    WAVEFORMATEX f; wcb_fmt(&f, 44100, 16, 2);
    int counts[] = { 1, 3, 6 };
    for (int i = 0; i < 3; i++) in_record(WAVE_MAPPER, &f, counts[i], 40, 0xB100 + i);
}

TEST(wcb_in_fn_data_formats) {
    NEED_IN();
    struct { DWORD rate; WORD bits, ch; } fm[] = { { 8000, 8, 1 }, { 22050, 16, 1 }, { 44100, 16, 2 }, { 48000, 16, 2 } };
    for (int i = 0; i < 4; i++) { WAVEFORMATEX f; wcb_fmt(&f, fm[i].rate, fm[i].bits, fm[i].ch); in_record(WAVE_MAPPER, &f, 2, 40, 0xB200 + i); }
}

TEST(wcb_in_fn_reset_returns_unfilled) {
    NEED_IN();
    /* Reset without Start returns every queued buffer through WIM_DATA with dwBytesRecorded 0. */
    WAVEFORMATEX f; wcb_fmt(&f, 44100, 16, 1);
    inctx c;
    if (in_open_fn(&c, WAVE_MAPPER, &f, 0xB300, 0)) { CHECK(0); in_done(&c); return; }
    WAVEHDR hd[3]; char *d[3]; DWORD len;
    for (int i = 0; i < 3; i++) { d[i] = wcb_silence(&f, 500, &len); wcb_hdr(&hd[i], d[i], len); U(waveInPrepareHeader)(c.h, &hd[i], sizeof hd[i]); EXPECT_EQ(U(waveInAddBuffer)(c.h, &hd[i], sizeof hd[i]), 0); }
    for (int i = 0; i < 3; i++) CHECKF(hd[i].dwFlags & WHDR_INQUEUE, "buffer %d flags 0x%lx after AddBuffer", i, hd[i].dwFlags);
    EXPECT_EQ(U(waveInClose)(c.h), WAVERR_STILLPLAYING);
    EXPECT_EQ(wcb_count(&c.L, WIM_CLOSE), 0);
    EXPECT_EQ(U(waveInReset)(c.h), 0);
    CHECK(wcb_wait_total(&c.L, 4, WCB_WAIT_MS));
    EXPECT_EQ(wcb_count(&c.L, WIM_DATA), 3);
    CHECK(wcb_check_done_order(&c.L, WIM_DATA, hd, 3));
    for (int i = 0; i < 3; i++) {
        EXPECT_EQ(hd[i].dwFlags, WHDR_DONE | WHDR_PREPARED);
        EXPECT_EQ(hd[i].dwBytesRecorded, 0);
        U(waveInUnprepareHeader)(c.h, &hd[i], sizeof hd[i]); HeapFree(GetProcessHeap(), 0, d[i]);
    }
    EXPECT_EQ(U(waveInClose)(c.h), 0);
    EXPECT_EQ(wcb_count(&c.L, WIM_CLOSE), 1);
    in_done(&c);
}

TEST(wcb_in_fn_stop_partial) {
    NEED_IN();
    /* Stop with a long buffer in progress: the partially filled buffer is returned (dwBytesRecorded < length). */
    WAVEFORMATEX f; wcb_fmt(&f, 44100, 16, 1);
    inctx c;
    if (in_open_fn(&c, WAVE_MAPPER, &f, 0xB400, 0)) { CHECK(0); in_done(&c); return; }
    WAVEHDR wh; DWORD len; char *d = wcb_silence(&f, 2000, &len); wcb_hdr(&wh, d, len);
    U(waveInPrepareHeader)(c.h, &wh, sizeof wh); U(waveInAddBuffer)(c.h, &wh, sizeof wh);
    EXPECT_EQ(U(waveInStart)(c.h), 0);
    Sleep(300);
    EXPECT_EQ(U(waveInStop)(c.h), 0);
    int got = wcb_wait_total(&c.L, 2, 1000);
    t_note("waveInStop after 300 ms of a 2 s buffer: WIM_DATA %s, dwBytesRecorded %lu of %lu, flags 0x%lx",
           got ? "delivered" : "not delivered", wh.dwBytesRecorded, wh.dwBufferLength, wh.dwFlags);
    EXPECT_EQ(U(waveInReset)(c.h), 0);
    CHECK(wcb_wait_total(&c.L, 2, WCB_WAIT_MS));
    EXPECT_EQ(wcb_count(&c.L, WIM_DATA), 1);
    CHECK(wh.dwFlags & WHDR_DONE);
    CHECK(wh.dwBytesRecorded < wh.dwBufferLength);
    U(waveInUnprepareHeader)(c.h, &wh, sizeof wh);
    EXPECT_EQ(U(waveInClose)(c.h), 0);
    HeapFree(GetProcessHeap(), 0, d);
    in_done(&c);
}

TEST(wcb_in_event) {
    NEED_IN();
    WAVEFORMATEX f; wcb_fmt(&f, 44100, 16, 1);
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    HWAVEIN h = NULL;
    if (U(waveInOpen)(&h, WAVE_MAPPER, &f, (DWORD_PTR)ev, 0, CALLBACK_EVENT)) { CHECK(0); CloseHandle(ev); return; }
    EXPECT_EQ(WaitForSingleObject(ev, 0), WAIT_OBJECT_0);
    int sig = 0;
    for (int b = 0; b < 3; b++) {
        WAVEHDR wh; DWORD len; char *d = wcb_silence(&f, 40, &len); wcb_hdr(&wh, d, len);
        U(waveInPrepareHeader)(h, &wh, sizeof wh); U(waveInAddBuffer)(h, &wh, sizeof wh);
        if (b == 0) EXPECT_EQ(U(waveInStart)(h), 0);
        if (wcb_ev_wait(ev, WCB_WAIT_MS)) sig++;
        CHECKF(wh.dwFlags & WHDR_DONE, "flags 0x%lx", wh.dwFlags);
        CHECK(wh.dwBytesRecorded > 0);
        if (!(wh.dwFlags & WHDR_DONE)) U(waveInReset)(h);
        U(waveInUnprepareHeader)(h, &wh, sizeof wh);
        HeapFree(GetProcessHeap(), 0, d);
    }
    EXPECT_EQ(sig, 3);
    U(waveInReset)(h);
    WaitForSingleObject(ev, 0);
    EXPECT_EQ(U(waveInClose)(h), 0);
    EXPECT_EQ(WaitForSingleObject(ev, 0), WAIT_OBJECT_0);
    CloseHandle(ev);
}

TEST(wcb_in_window) {
    NEED_IN();
    WAVEFORMATEX f; wcb_fmt(&f, 44100, 16, 1);
    HWND w = wcb_win_create();
    wcb_log L; wcb_log_init(&L);
    HWAVEIN h = NULL;
    if (U(waveInOpen)(&h, WAVE_MAPPER, &f, (DWORD_PTR)w, 0, CALLBACK_WINDOW)) { CHECK(0); wcb_log_free(&L); wcb_win_destroy(w); return; }
    MSG m;
    BOOL have = PeekMessageA(&m, w, MM_WIM_OPEN, MM_WIM_OPEN, PM_NOREMOVE);
    CHECKF(have, "WIM_OPEN not queued when waveInOpen returned");
    WAVEHDR hd[2]; char *d[2]; DWORD len;
    for (int i = 0; i < 2; i++) { d[i] = wcb_silence(&f, 40, &len); wcb_hdr(&hd[i], d[i], len); U(waveInPrepareHeader)(h, &hd[i], sizeof hd[i]); U(waveInAddBuffer)(h, &hd[i], sizeof hd[i]); }
    EXPECT_EQ(U(waveInStart)(h), 0);
    wcb_win_pump(w, &L, 3, WCB_WAIT_MS);
    EXPECT_EQ(U(waveInReset)(h), 0);
    for (int i = 0; i < 2; i++) { U(waveInUnprepareHeader)(h, &hd[i], sizeof hd[i]); }
    EXPECT_EQ(U(waveInClose)(h), 0);
    wcb_win_pump(w, &L, 4, 500);
    EXPECT_EQ(L.total, 4);
    EXPECT_EQ(wcb_count(&L, WIM_OPEN), 1);
    EXPECT_EQ(wcb_count(&L, WIM_DATA), 2);
    EXPECT_EQ(wcb_count(&L, WIM_CLOSE), 1);
    CHECK(wcb_check_done_order(&L, WIM_DATA, hd, 2));
    for (int i = 0; i < L.total && i < WCB_MAXREC; i++) {
        CHECK(L.r[i].h == (DWORD_PTR)h);
        if (L.r[i].msg != WIM_DATA) CHECK(L.r[i].p1 == 0);
    }
    if (L.total == 4) { EXPECT_EQ(L.r[0].msg, WIM_OPEN); EXPECT_EQ(L.r[3].msg, WIM_CLOSE); }
    for (int i = 0; i < 2; i++) HeapFree(GetProcessHeap(), 0, d[i]);
    wcb_log_free(&L);
    wcb_win_destroy(w);
}

TEST(wcb_in_thread) {
    NEED_IN();
    WAVEFORMATEX f; wcb_fmt(&f, 44100, 16, 1);
    wcb_thr T;
    if (!wcb_thr_start(&T)) { CHECK(0); wcb_thr_stop(&T); return; }
    HWAVEIN h = NULL;
    if (U(waveInOpen)(&h, WAVE_MAPPER, &f, T.tid, 0, CALLBACK_THREAD)) { CHECK(0); wcb_thr_stop(&T); return; }
    WAVEHDR hd[2]; char *d[2]; DWORD len;
    for (int i = 0; i < 2; i++) { d[i] = wcb_silence(&f, 40, &len); wcb_hdr(&hd[i], d[i], len); U(waveInPrepareHeader)(h, &hd[i], sizeof hd[i]); U(waveInAddBuffer)(h, &hd[i], sizeof hd[i]); }
    EXPECT_EQ(U(waveInStart)(h), 0);
    CHECK(wcb_thr_wait(&T, MM_WIM_DATA, MM_WIM_DATA, 2, WCB_WAIT_MS));
    EXPECT_EQ(U(waveInReset)(h), 0);
    for (int i = 0; i < 2; i++) U(waveInUnprepareHeader)(h, &hd[i], sizeof hd[i]);
    EXPECT_EQ(U(waveInClose)(h), 0);
    CHECK(wcb_thr_wait(&T, MM_WIM_CLOSE, MM_WIM_CLOSE, 1, WCB_WAIT_MS));
    Sleep(30);
    int n = T.log.total < WCB_MAXREC ? T.log.total : WCB_MAXREC, k = 0, users = 0, ok = 1;
    UINT want[4] = { WIM_OPEN, WIM_DATA, WIM_DATA, WIM_CLOSE };
    for (int i = 0; i < n; i++) {
        wcb_rec *e = &T.log.r[i];
        if (e->msg == WM_USER) { users++; continue; }
        if (k >= 4 || e->msg != want[k] || e->h != (DWORD_PTR)h) ok = 0;
        if (k >= 1 && k <= 2 && e->p1 != (DWORD_PTR)&hd[k - 1]) ok = 0;
        k++;
    }
    EXPECT_EQ(k, 4);
    CHECK(ok);
    EXPECT_EQ(users, 4);
    for (int i = 0; i < 2; i++) HeapFree(GetProcessHeap(), 0, d[i]);
    wcb_thr_stop(&T);
}

TEST(wcb_in_window_null_hwnd) {
    NEED_IN();
    WAVEFORMATEX f; wcb_fmt(&f, 44100, 16, 1);
    HWAVEIN h = NULL;
    MMRESULT r = U(waveInOpen)(&h, WAVE_MAPPER, &f, 0, 0, CALLBACK_WINDOW);
    EXPECT_EQ(r, 0);
    if (r) return;
    WAVEHDR wh; DWORD len; char *d = wcb_silence(&f, 40, &len); wcb_hdr(&wh, d, len);
    U(waveInPrepareHeader)(h, &wh, sizeof wh); U(waveInAddBuffer)(h, &wh, sizeof wh);
    U(waveInStart)(h);
    DWORD t0 = GetTickCount();
    while (!(wh.dwFlags & WHDR_DONE) && GetTickCount() - t0 < WCB_WAIT_MS) Sleep(10);
    CHECK(wh.dwFlags & WHDR_DONE);
    U(waveInReset)(h);
    U(waveInUnprepareHeader)(h, &wh, sizeof wh);
    EXPECT_EQ(U(waveInClose)(h), 0);
    MSG m;
    EXPECT_EQ(PeekMessageA(&m, NULL, MM_WOM_OPEN, MM_WIM_DATA, PM_REMOVE), FALSE);
    HeapFree(GetProcessHeap(), 0, d);
}

TEST(wcb_in_null_callback_poll) {
    NEED_IN();
    WAVEFORMATEX f; wcb_fmt(&f, 44100, 16, 1);
    HWAVEIN h = NULL;
    if (U(waveInOpen)(&h, WAVE_MAPPER, &f, 0, 0, CALLBACK_NULL)) { CHECK(0); return; }
    WAVEHDR hd[2]; char *d[2]; DWORD len;
    for (int i = 0; i < 2; i++) { d[i] = wcb_silence(&f, 40, &len); wcb_hdr(&hd[i], d[i], len); U(waveInPrepareHeader)(h, &hd[i], sizeof hd[i]); U(waveInAddBuffer)(h, &hd[i], sizeof hd[i]); }
    U(waveInStart)(h);
    DWORD t0 = GetTickCount();
    while (!(hd[1].dwFlags & WHDR_DONE) && GetTickCount() - t0 < WCB_WAIT_MS) Sleep(10);
    for (int i = 0; i < 2; i++) { CHECK(hd[i].dwFlags & WHDR_DONE); CHECK(hd[i].dwBytesRecorded == hd[i].dwBufferLength); }
    U(waveInReset)(h);
    for (int i = 0; i < 2; i++) { U(waveInUnprepareHeader)(h, &hd[i], sizeof hd[i]); HeapFree(GetProcessHeap(), 0, d[i]); }
    EXPECT_EQ(U(waveInClose)(h), 0);
}

TEST(wcb_in_query_no_callback_device) {
    NEED_IN();
    WAVEFORMATEX f; wcb_fmt(&f, 44100, 16, 1);
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    wcb_log L; wcb_log_init(&L); wcb_fn_register(&L, 0xB500);
    EXPECT_EQ(U(waveInOpen)(NULL, WAVE_MAPPER, &f, (DWORD_PTR)ev, 0, CALLBACK_EVENT | WAVE_FORMAT_QUERY), 0);
    EXPECT_EQ(U(waveInOpen)(NULL, 0, &f, (DWORD_PTR)wcb_fn_in, 0xB500, CALLBACK_FUNCTION | WAVE_FORMAT_QUERY), 0);
    Sleep(30);
    EXPECT_EQ(WaitForSingleObject(ev, 0), WAIT_TIMEOUT);
    EXPECT_EQ(L.total, 0);
    wcb_fn_unregister(&L); wcb_log_free(&L);
    CloseHandle(ev);
}
