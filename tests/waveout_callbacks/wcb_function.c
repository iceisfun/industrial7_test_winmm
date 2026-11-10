/* waveout_callbacks: CALLBACK_FUNCTION delivery for waveOut on real devices (device half; skipped with 0 devices).
 * DriverCallback (winmmbase!0x5560) type 3 calls cb(hdev, msg, dwInstance, dwParam1, dwParam2) directly.
 * The callback here only records (no waveOut* call from inside a callback). Audio is silence only.
 * VM_TIMEOUT: 240 */
#include "wcb.h"

#define NEED_OUT() do { REQUIRE(U(waveOutOpen)); if (wcb_nout() == 0) { t_skip("no waveOut device (count 0): contract half only"); return; } wcb_note_devices(); } while (0)

typedef struct {
    wcb_log  L;
    HWAVEOUT h;
    volatile DWORD_PTR hwatch;
} fnctx;

static MMRESULT fn_open(fnctx *c, UINT id, const WAVEFORMATEX *f, DWORD_PTR inst, DWORD extra) {
    wcb_log_init(&c->L);
    c->hwatch = 0;
    c->L.watch = &c->hwatch;
    wcb_fn_register(&c->L, inst);
    c->h = NULL;
    MMRESULT r = U(waveOutOpen)((HWAVEOUT *)&c->hwatch, id, f, (DWORD_PTR)wcb_fn_out, inst, CALLBACK_FUNCTION | extra);
    c->h = (HWAVEOUT)c->hwatch;
    return r;
}
static void fn_done(fnctx *c) {
    wcb_fn_unregister(&c->L);
    wcb_log_free(&c->L);
}

/* WOM_OPEN is delivered before waveOutOpen returns, exactly once, with the new handle. */
TEST(wcb_fn_open_synchronous_each_id) {
    NEED_OUT();
    UINT ids[6]; int k = wcb_out_ids(ids, 6);
    WAVEFORMATEX f; wcb_fmt_default(&f);
    for (int i = 0; i < k; i++) {
        fnctx c; DWORD_PTR inst = 0x1000 + i;
        MMRESULT r = fn_open(&c, ids[i], &f, inst, 0);
        CHECKF(r == MMSYSERR_NOERROR, "%s open %u", wcb_idname(ids[i]), r);
        if (r) { fn_done(&c); continue; }
        LONG at_return = c.L.total;
        CHECKF(at_return == 1, "%s: %ld callbacks when waveOutOpen returned", wcb_idname(ids[i]), at_return);
        CHECK(c.L.r[0].msg == WOM_OPEN);
        CHECK(c.L.r[0].h == (DWORD_PTR)c.h);
        CHECK(c.L.r[0].inst == inst);
        EXPECT_EQ(c.L.r[0].p1, 0);
        EXPECT_EQ(c.L.r[0].p2, 0);
        EXPECT_EQ(U(waveOutClose)(c.h), MMSYSERR_NOERROR);
        fn_done(&c);
    }
}

/* The out handle is stored only after the driver open (and its WOM_OPEN) returns. */
TEST(wcb_fn_open_handle_stored_after_callback) {
    NEED_OUT();
    UINT ids[6]; int k = wcb_out_ids(ids, 6);
    WAVEFORMATEX f; wcb_fmt_default(&f);
    for (int i = 0; i < k; i++) {
        fnctx c;
        if (fn_open(&c, ids[i], &f, 0x2000 + i, 0)) { fn_done(&c); CHECK(0); continue; }
        if (c.L.total >= 1) {
            CHECKF(c.L.r[0].phandle == 0, "%s: *phwo was %p during WOM_OPEN", wcb_idname(ids[i]), (void *)c.L.r[0].phandle);
            CHECK(c.L.r[0].h != 0);
        }
        U(waveOutClose)(c.h);
        fn_done(&c);
    }
}

TEST(wcb_fn_open_close_thread_ids) {
    NEED_OUT();
    UINT ids[6]; int k = wcb_out_ids(ids, 6);
    WAVEFORMATEX f; wcb_fmt_default(&f);
    DWORD me = GetCurrentThreadId();
    for (int i = 0; i < k; i++) {
        fnctx c;
        if (fn_open(&c, ids[i], &f, 0x3000 + i, 0)) { fn_done(&c); CHECK(0); continue; }
        EXPECT_EQ(U(waveOutClose)(c.h), MMSYSERR_NOERROR);
        if (c.L.total >= 2) {
            t_note("%s: WOM_OPEN on %s thread, WOM_CLOSE on %s thread", wcb_idname(ids[i]),
                   c.L.r[0].tid == me ? "caller" : "other", c.L.r[1].tid == me ? "caller" : "other");
            CHECK(c.L.r[0].tid == me);
            CHECK(c.L.r[1].tid == me);
        }
        fn_done(&c);
    }
}

/* WOM_CLOSE is delivered during waveOutClose: present when it returns, nothing after it. */
TEST(wcb_fn_close_synchronous_each_id) {
    NEED_OUT();
    UINT ids[6]; int k = wcb_out_ids(ids, 6);
    WAVEFORMATEX f; wcb_fmt_default(&f);
    for (int i = 0; i < k; i++) {
        fnctx c; DWORD_PTR inst = 0x4000 + i;
        if (fn_open(&c, ids[i], &f, inst, 0)) { fn_done(&c); CHECK(0); continue; }
        HWAVEOUT h = c.h;
        EXPECT_EQ(U(waveOutClose)(h), MMSYSERR_NOERROR);
        LONG at_return = c.L.total;
        EXPECT_EQ(at_return, 2);
        if (at_return >= 2) {
            EXPECT_EQ(c.L.r[1].msg, WOM_CLOSE);
            EXPECT_EQ(c.L.r[1].h, (DWORD_PTR)h);
            EXPECT_EQ(c.L.r[1].inst, inst);
            EXPECT_EQ(c.L.r[1].p1, 0);
            EXPECT_EQ(c.L.r[1].p2, 0);
        }
        Sleep(50);
        EXPECT_EQ(c.L.total, at_return);
        /* the handle is gone: no further callback, INVALHANDLE */
        EXPECT_EQ(U(waveOutClose)(h), MMSYSERR_INVALHANDLE);
        EXPECT_EQ(c.L.total, at_return);
        fn_done(&c);
    }
}

/* dwInstance is passed through untouched, all 64 bits. */
TEST(wcb_fn_instance_64bit_values) {
    NEED_OUT();
    WAVEFORMATEX f; wcb_fmt_default(&f);
    DWORD_PTR v[] = { 0, 1, 0xFFFFFFFF, 0x100000000ull, 0xA5A5A5A55A5A5A5Aull, 0xFFFFFFFFFFFFFFFFull, 0x8000000000000000ull, 0x00000001DEADBEEFull };
    for (int i = 0; i < 8; i++) {
        fnctx c;
        if (fn_open(&c, WAVE_MAPPER, &f, v[i], 0)) { fn_done(&c); CHECK(0); continue; }
        U(waveOutClose)(c.h);
        CHECKF(c.L.total == 2, "inst 0x%llx: %ld callbacks", (unsigned long long)v[i], c.L.total);
        for (int j = 0; j < c.L.total && j < 2; j++)
            CHECKF(c.L.r[j].inst == v[i], "inst 0x%llx got 0x%llx", (unsigned long long)v[i], (unsigned long long)c.L.r[j].inst);
        fn_done(&c);
    }
}

/* One WOM_DONE per buffer, in submission order, dwParam1 == LPWAVEHDR, dwParam2 0, DONE set / INQUEUE clear. */
static void play_and_check(UINT id, const WAVEFORMATEX *f, int nbuf, DWORD ms, DWORD_PTR inst) {
    fnctx c;
    MMRESULT r = fn_open(&c, id, f, inst, 0);
    CHECKF(r == 0, "%s %lu/%u/%u open %u", wcb_idname(id), f->nSamplesPerSec, f->wBitsPerSample, f->nChannels, r);
    if (r) { fn_done(&c); return; }
    WAVEHDR hd[16]; char *d[16]; DWORD len;
    for (int i = 0; i < nbuf; i++) {
        d[i] = wcb_silence(f, ms, &len);
        wcb_hdr(&hd[i], d[i], len);
        hd[i].dwUser = 0x5000 + i;
        EXPECT_EQ(U(waveOutPrepareHeader)(c.h, &hd[i], sizeof hd[i]), 0);
    }
    for (int i = 0; i < nbuf; i++) EXPECT_EQ(U(waveOutWrite)(c.h, &hd[i], sizeof hd[i]), 0);
    int got = wcb_wait_total(&c.L, 1 + nbuf, WCB_WAIT_MS);
    CHECKF(got, "%s: %ld callbacks after %d buffers", wcb_idname(id), c.L.total, nbuf);
    if (!got) U(waveOutReset)(c.h);
    EXPECT_EQ(wcb_count(&c.L, WOM_DONE), nbuf);
    CHECK(wcb_check_done_order(&c.L, WOM_DONE, hd, nbuf));
    DWORD me = GetCurrentThreadId();
    int n = c.L.total < WCB_MAXREC ? c.L.total : WCB_MAXREC;
    for (int i = 0; i < n; i++) {
        wcb_rec *e = &c.L.r[i];
        if (e->msg != WOM_DONE) continue;
        CHECK(e->h == (DWORD_PTR)c.h);
        CHECK(e->inst == inst);
        CHECK(e->p2 == 0);
        CHECKF((e->hdr_flags & (WHDR_DONE | WHDR_INQUEUE | WHDR_PREPARED)) == (WHDR_DONE | WHDR_PREPARED),
               "flags at WOM_DONE 0x%lx", e->hdr_flags);
        CHECKF(e->tid != me, "WOM_DONE delivered on the caller thread");
    }
    for (int i = 0; i < nbuf; i++) {
        EXPECT_EQ(hd[i].dwFlags, WHDR_DONE | WHDR_PREPARED);
        EXPECT_EQ(hd[i].dwUser, 0x5000 + i);
        EXPECT_EQ(U(waveOutUnprepareHeader)(c.h, &hd[i], sizeof hd[i]), 0);
        HeapFree(GetProcessHeap(), 0, d[i]);
    }
    EXPECT_EQ(U(waveOutClose)(c.h), 0);
    EXPECT_EQ(wcb_count(&c.L, WOM_OPEN), 1);
    EXPECT_EQ(wcb_count(&c.L, WOM_CLOSE), 1);
    if (c.L.total >= 2) {
        EXPECT_EQ(c.L.r[0].msg, WOM_OPEN);
        EXPECT_EQ(c.L.r[c.L.total - 1].msg, WOM_CLOSE);
    }
    fn_done(&c);
}

TEST(wcb_fn_done_one_buffer_each_id) {
    NEED_OUT();
    UINT ids[6]; int k = wcb_out_ids(ids, 6);
    WAVEFORMATEX f; wcb_fmt_default(&f);
    for (int i = 0; i < k; i++) play_and_check(ids[i], &f, 1, 40, 0x6000 + i);
}

TEST(wcb_fn_done_order_counts) {
    NEED_OUT();
    WAVEFORMATEX f; wcb_fmt_default(&f);
    int counts[] = { 2, 3, 4, 8, 12 };
    for (int i = 0; i < 5; i++) play_and_check(WAVE_MAPPER, &f, counts[i], 20, 0x6100 + i);
}

TEST(wcb_fn_done_order_device0) {
    NEED_OUT();
    WAVEFORMATEX f; wcb_fmt_default(&f);
    play_and_check(0, &f, 6, 20, 0x6200);
}

TEST(wcb_fn_done_formats_mapper) {
    NEED_OUT();
    struct { DWORD rate; WORD bits, ch; } fm[] = {
        { 8000, 8, 1 }, { 11025, 8, 2 }, { 22050, 16, 1 }, { 44100, 16, 2 }, { 48000, 16, 2 }, { 96000, 16, 2 }, { 16000, 16, 1 },
    };
    for (int i = 0; i < 7; i++) {
        WAVEFORMATEX f; wcb_fmt(&f, fm[i].rate, fm[i].bits, fm[i].ch);
        play_and_check(WAVE_MAPPER, &f, 3, 30, 0x6300 + i);
    }
}

TEST(wcb_fn_done_formats_device0) {
    NEED_OUT();
    struct { DWORD rate; WORD bits, ch; } fm[] = { { 44100, 16, 2 }, { 48000, 16, 2 }, { 22050, 16, 1 }, { 8000, 8, 1 } };
    for (int i = 0; i < 4; i++) {
        WAVEFORMATEX f; wcb_fmt(&f, fm[i].rate, fm[i].bits, fm[i].ch);
        play_and_check(0, &f, 2, 30, 0x6400 + i);
    }
}

TEST(wcb_fn_done_buffer_sizes) {
    NEED_OUT();
    WAVEFORMATEX f; wcb_fmt_default(&f);
    DWORD ms[] = { 1, 5, 10, 100, 250 };
    for (int i = 0; i < 5; i++) play_and_check(WAVE_MAPPER, &f, 2, ms[i], 0x6500 + i);
}

TEST(wcb_fn_done_odd_length_buffer) {
    NEED_OUT();
    /* A buffer whose length is not a multiple of nBlockAlign still completes with one WOM_DONE. */
    WAVEFORMATEX f; wcb_fmt_default(&f);
    fnctx c;
    if (fn_open(&c, WAVE_MAPPER, &f, 0x6600, 0)) { fn_done(&c); CHECK(0); return; }
    DWORD len; char *d = wcb_silence(&f, 30, &len);
    WAVEHDR wh; wcb_hdr(&wh, d, len - 3);
    EXPECT_EQ(U(waveOutPrepareHeader)(c.h, &wh, sizeof wh), 0);
    MMRESULT w = U(waveOutWrite)(c.h, &wh, sizeof wh);
    t_note("write of a %lu-byte (not block aligned) buffer: %u", len - 3, w);
    if (w == 0) {
        CHECK(wcb_wait_total(&c.L, 2, WCB_WAIT_MS));
        EXPECT_EQ(wcb_count(&c.L, WOM_DONE), 1);
    }
    U(waveOutReset)(c.h);
    U(waveOutUnprepareHeader)(c.h, &wh, sizeof wh);
    EXPECT_EQ(U(waveOutClose)(c.h), 0);
    HeapFree(GetProcessHeap(), 0, d);
    fn_done(&c);
}

TEST(wcb_fn_done_zero_length_buffer) {
    NEED_OUT();
    WAVEFORMATEX f; wcb_fmt_default(&f);
    fnctx c;
    if (fn_open(&c, WAVE_MAPPER, &f, 0x6700, 0)) { fn_done(&c); CHECK(0); return; }
    char d[4] = {0};
    WAVEHDR wh; wcb_hdr(&wh, d, 0);
    MMRESULT p = U(waveOutPrepareHeader)(c.h, &wh, sizeof wh);
    MMRESULT w = p == 0 ? U(waveOutWrite)(c.h, &wh, sizeof wh) : 0xFFFF;
    int got = (p == 0 && w == 0) ? wcb_wait_total(&c.L, 2, 1000) : 0;
    t_note("zero-length buffer: prepare %u write %u WOM_DONE %s flags 0x%lx", p, w, got ? "yes" : "no", wh.dwFlags);
    U(waveOutReset)(c.h);
    if (p == 0) U(waveOutUnprepareHeader)(c.h, &wh, sizeof wh);
    EXPECT_EQ(U(waveOutClose)(c.h), 0);
    fn_done(&c);
}

/* Reset returns every queued buffer: one WOM_DONE each, in order, flags DONE|PREPARED. */
TEST(wcb_fn_reset_returns_all) {
    NEED_OUT();
    WAVEFORMATEX f; wcb_fmt_default(&f);
    int counts[] = { 1, 3, 6 };
    for (int ci = 0; ci < 3; ci++) {
        int nb = counts[ci];
        fnctx c;
        if (fn_open(&c, WAVE_MAPPER, &f, 0x6800 + ci, 0)) { fn_done(&c); CHECK(0); continue; }
        WAVEHDR hd[8]; char *d[8]; DWORD len;
        for (int i = 0; i < nb; i++) {
            d[i] = wcb_silence(&f, 500, &len);
            wcb_hdr(&hd[i], d[i], len);
            U(waveOutPrepareHeader)(c.h, &hd[i], sizeof hd[i]);
            EXPECT_EQ(U(waveOutWrite)(c.h, &hd[i], sizeof hd[i]), 0);
        }
        EXPECT_EQ(U(waveOutReset)(c.h), 0);
        CHECK(wcb_wait_total(&c.L, 1 + nb, WCB_WAIT_MS));
        EXPECT_EQ(wcb_count(&c.L, WOM_DONE), nb);
        CHECK(wcb_check_done_order(&c.L, WOM_DONE, hd, nb));
        for (int i = 0; i < nb; i++) {
            EXPECT_EQ(hd[i].dwFlags, WHDR_DONE | WHDR_PREPARED);
            EXPECT_EQ(U(waveOutUnprepareHeader)(c.h, &hd[i], sizeof hd[i]), 0);
            HeapFree(GetProcessHeap(), 0, d[i]);
        }
        EXPECT_EQ(U(waveOutClose)(c.h), 0);
        EXPECT_EQ(wcb_count(&c.L, WOM_CLOSE), 1);
        fn_done(&c);
    }
}

TEST(wcb_fn_reset_done_before_return) {
    NEED_OUT();
    /* Reset completes the queued buffers before it returns (their WOM_DONE has already been delivered). */
    WAVEFORMATEX f; wcb_fmt_default(&f);
    fnctx c;
    if (fn_open(&c, WAVE_MAPPER, &f, 0x6900, 0)) { fn_done(&c); CHECK(0); return; }
    WAVEHDR hd[4]; char *d[4]; DWORD len;
    for (int i = 0; i < 4; i++) {
        d[i] = wcb_silence(&f, 400, &len); wcb_hdr(&hd[i], d[i], len);
        U(waveOutPrepareHeader)(c.h, &hd[i], sizeof hd[i]); U(waveOutWrite)(c.h, &hd[i], sizeof hd[i]);
    }
    EXPECT_EQ(U(waveOutReset)(c.h), 0);
    int at_return = wcb_count(&c.L, WOM_DONE);
    t_note("WOM_DONE count when waveOutReset returned: %d of 4", at_return);
    int flags_done = 0;
    for (int i = 0; i < 4; i++) if (hd[i].dwFlags & WHDR_DONE) flags_done++;
    EXPECT_EQ(flags_done, 4);
    wcb_wait_total(&c.L, 5, WCB_WAIT_MS);
    for (int i = 0; i < 4; i++) { U(waveOutUnprepareHeader)(c.h, &hd[i], sizeof hd[i]); HeapFree(GetProcessHeap(), 0, d[i]); }
    U(waveOutClose)(c.h);
    fn_done(&c);
}

/* Close with buffers still queued fails with WAVERR_STILLPLAYING, the handle stays open and no WOM_CLOSE is sent. */
TEST(wcb_fn_close_while_playing_33) {
    NEED_OUT();
    WAVEFORMATEX f; wcb_fmt_default(&f);
    fnctx c;
    if (fn_open(&c, WAVE_MAPPER, &f, 0x6A00, 0)) { fn_done(&c); CHECK(0); return; }
    WAVEHDR wh; DWORD len; char *d = wcb_silence(&f, 1000, &len); wcb_hdr(&wh, d, len);
    U(waveOutPrepareHeader)(c.h, &wh, sizeof wh);
    EXPECT_EQ(U(waveOutWrite)(c.h, &wh, sizeof wh), 0);
    EXPECT_EQ(U(waveOutClose)(c.h), WAVERR_STILLPLAYING);
    EXPECT_EQ(wcb_count(&c.L, WOM_CLOSE), 0);
    EXPECT_EQ(U(waveOutReset)(c.h), 0);
    CHECK(wcb_wait_total(&c.L, 2, WCB_WAIT_MS));
    EXPECT_EQ(wcb_count(&c.L, WOM_DONE), 1);
    EXPECT_EQ(U(waveOutUnprepareHeader)(c.h, &wh, sizeof wh), 0);
    EXPECT_EQ(U(waveOutClose)(c.h), 0);
    EXPECT_EQ(wcb_count(&c.L, WOM_CLOSE), 1);
    if (c.L.total >= 3) EXPECT_EQ(c.L.r[c.L.total - 1].msg, WOM_CLOSE);
    HeapFree(GetProcessHeap(), 0, d);
    fn_done(&c);
}

TEST(wcb_fn_pause_holds_done) {
    NEED_OUT();
    /* A paused device does not complete buffers: no WOM_DONE while paused (bounded 300 ms), then restart finishes. */
    WAVEFORMATEX f; wcb_fmt_default(&f);
    fnctx c;
    if (fn_open(&c, WAVE_MAPPER, &f, 0x6B00, 0)) { fn_done(&c); CHECK(0); return; }
    EXPECT_EQ(U(waveOutPause)(c.h), 0);
    WAVEHDR wh; DWORD len; char *d = wcb_silence(&f, 50, &len); wcb_hdr(&wh, d, len);
    U(waveOutPrepareHeader)(c.h, &wh, sizeof wh);
    EXPECT_EQ(U(waveOutWrite)(c.h, &wh, sizeof wh), 0);
    Sleep(300);
    EXPECT_EQ(wcb_count(&c.L, WOM_DONE), 0);
    CHECK(wh.dwFlags & WHDR_INQUEUE);
    EXPECT_EQ(U(waveOutRestart)(c.h), 0);
    CHECK(wcb_wait_total(&c.L, 2, WCB_WAIT_MS));
    EXPECT_EQ(wcb_count(&c.L, WOM_DONE), 1);
    U(waveOutReset)(c.h);
    U(waveOutUnprepareHeader)(c.h, &wh, sizeof wh);
    EXPECT_EQ(U(waveOutClose)(c.h), 0);
    HeapFree(GetProcessHeap(), 0, d);
    fn_done(&c);
}

TEST(wcb_fn_pause_reset_returns) {
    NEED_OUT();
    WAVEFORMATEX f; wcb_fmt_default(&f);
    fnctx c;
    if (fn_open(&c, WAVE_MAPPER, &f, 0x6C00, 0)) { fn_done(&c); CHECK(0); return; }
    EXPECT_EQ(U(waveOutPause)(c.h), 0);
    WAVEHDR hd[3]; char *d[3]; DWORD len;
    for (int i = 0; i < 3; i++) { d[i] = wcb_silence(&f, 50, &len); wcb_hdr(&hd[i], d[i], len); U(waveOutPrepareHeader)(c.h, &hd[i], sizeof hd[i]); U(waveOutWrite)(c.h, &hd[i], sizeof hd[i]); }
    Sleep(100);
    EXPECT_EQ(wcb_count(&c.L, WOM_DONE), 0);
    EXPECT_EQ(U(waveOutReset)(c.h), 0);
    CHECK(wcb_wait_total(&c.L, 4, WCB_WAIT_MS));
    EXPECT_EQ(wcb_count(&c.L, WOM_DONE), 3);
    CHECK(wcb_check_done_order(&c.L, WOM_DONE, hd, 3));
    for (int i = 0; i < 3; i++) { U(waveOutUnprepareHeader)(c.h, &hd[i], sizeof hd[i]); HeapFree(GetProcessHeap(), 0, d[i]); }
    EXPECT_EQ(U(waveOutClose)(c.h), 0);
    fn_done(&c);
}

/* Looping: BEGINLOOP|ENDLOOP on one header with dwLoops still yields exactly one WOM_DONE for that header. */
TEST(wcb_fn_loop_single_done) {
    NEED_OUT();
    WAVEFORMATEX f; wcb_fmt_default(&f);
    DWORD loops[] = { 1, 2, 3 };
    for (int li = 0; li < 3; li++) {
        fnctx c;
        if (fn_open(&c, WAVE_MAPPER, &f, 0x6D00 + li, 0)) { fn_done(&c); CHECK(0); continue; }
        WAVEHDR wh; DWORD len; char *d = wcb_silence(&f, 30, &len); wcb_hdr(&wh, d, len);
        wh.dwFlags = WHDR_BEGINLOOP | WHDR_ENDLOOP; wh.dwLoops = loops[li];
        EXPECT_EQ(U(waveOutPrepareHeader)(c.h, &wh, sizeof wh), 0);
        EXPECT_EQ(wh.dwFlags, WHDR_BEGINLOOP | WHDR_ENDLOOP | WHDR_PREPARED);
        EXPECT_EQ(U(waveOutWrite)(c.h, &wh, sizeof wh), 0);
        CHECK(wcb_wait_total(&c.L, 2, WCB_WAIT_MS));
        Sleep(60);
        EXPECT_EQ(wcb_count(&c.L, WOM_DONE), 1);
        U(waveOutReset)(c.h);
        U(waveOutUnprepareHeader)(c.h, &wh, sizeof wh);
        EXPECT_EQ(U(waveOutClose)(c.h), 0);
        HeapFree(GetProcessHeap(), 0, d);
        fn_done(&c);
    }
}

TEST(wcb_fn_loop_span_two_headers) {
    NEED_OUT();
    WAVEFORMATEX f; wcb_fmt_default(&f);
    fnctx c;
    if (fn_open(&c, WAVE_MAPPER, &f, 0x6E00, 0)) { fn_done(&c); CHECK(0); return; }
    WAVEHDR hd[3]; char *d[3]; DWORD len;
    for (int i = 0; i < 3; i++) { d[i] = wcb_silence(&f, 20, &len); wcb_hdr(&hd[i], d[i], len); }
    hd[0].dwFlags = WHDR_BEGINLOOP; hd[0].dwLoops = 2;
    hd[1].dwFlags = WHDR_ENDLOOP;
    for (int i = 0; i < 3; i++) { U(waveOutPrepareHeader)(c.h, &hd[i], sizeof hd[i]); EXPECT_EQ(U(waveOutWrite)(c.h, &hd[i], sizeof hd[i]), 0); }
    CHECK(wcb_wait_total(&c.L, 4, WCB_WAIT_MS));
    Sleep(60);
    EXPECT_EQ(wcb_count(&c.L, WOM_DONE), 3);
    CHECK(wcb_check_done_order(&c.L, WOM_DONE, hd, 3));
    U(waveOutReset)(c.h);
    for (int i = 0; i < 3; i++) { U(waveOutUnprepareHeader)(c.h, &hd[i], sizeof hd[i]); HeapFree(GetProcessHeap(), 0, d[i]); }
    EXPECT_EQ(U(waveOutClose)(c.h), 0);
    fn_done(&c);
}

TEST(wcb_fn_breakloop_completes) {
    NEED_OUT();
    WAVEFORMATEX f; wcb_fmt_default(&f);
    fnctx c;
    if (fn_open(&c, WAVE_MAPPER, &f, 0x6F00, 0)) { fn_done(&c); CHECK(0); return; }
    WAVEHDR wh; DWORD len; char *d = wcb_silence(&f, 20, &len); wcb_hdr(&wh, d, len);
    wh.dwFlags = WHDR_BEGINLOOP | WHDR_ENDLOOP; wh.dwLoops = 1000;   /* 20 s if never broken */
    U(waveOutPrepareHeader)(c.h, &wh, sizeof wh);
    EXPECT_EQ(U(waveOutWrite)(c.h, &wh, sizeof wh), 0);
    Sleep(60);
    EXPECT_EQ(U(waveOutBreakLoop)(c.h), 0);
    int got = wcb_wait_total(&c.L, 2, WCB_WAIT_MS);
    t_note("BreakLoop on a 1000-loop header: WOM_DONE %s within %d ms", got ? "delivered" : "not delivered", WCB_WAIT_MS);
    U(waveOutReset)(c.h);
    wcb_wait_total(&c.L, 2, 500);
    EXPECT_EQ(wcb_count(&c.L, WOM_DONE), 1);
    U(waveOutUnprepareHeader)(c.h, &wh, sizeof wh);
    EXPECT_EQ(U(waveOutClose)(c.h), 0);
    HeapFree(GetProcessHeap(), 0, d);
    fn_done(&c);
}

/* Two handles at once: each callback carries its own handle and instance. */
TEST(wcb_fn_two_handles_separate) {
    NEED_OUT();
    WAVEFORMATEX f; wcb_fmt_default(&f);
    fnctx a, b;
    if (fn_open(&a, WAVE_MAPPER, &f, 0x7001, 0)) { fn_done(&a); CHECK(0); return; }
    MMRESULT rb = fn_open(&b, wcb_nout() > 0 ? 0 : WAVE_MAPPER, &f, 0x7002, 0);
    if (rb) { t_note("second simultaneous open: %u", rb); U(waveOutClose)(a.h); fn_done(&a); fn_done(&b); return; }
    CHECK(a.h != b.h);
    WAVEHDR ha, hb; DWORD len; char *da = wcb_silence(&f, 30, &len), *db = wcb_silence(&f, 30, &len);
    wcb_hdr(&ha, da, len); wcb_hdr(&hb, db, len);
    U(waveOutPrepareHeader)(a.h, &ha, sizeof ha); U(waveOutPrepareHeader)(b.h, &hb, sizeof hb);
    U(waveOutWrite)(a.h, &ha, sizeof ha); U(waveOutWrite)(b.h, &hb, sizeof hb);
    CHECK(wcb_wait_total(&a.L, 2, WCB_WAIT_MS));
    CHECK(wcb_wait_total(&b.L, 2, WCB_WAIT_MS));
    EXPECT_EQ(U(waveOutUnprepareHeader)(a.h, &ha, sizeof ha), 0);
    EXPECT_EQ(U(waveOutUnprepareHeader)(b.h, &hb, sizeof hb), 0);
    EXPECT_EQ(U(waveOutClose)(a.h), 0);
    EXPECT_EQ(U(waveOutClose)(b.h), 0);
    wcb_log *Ls[2] = { &a.L, &b.L }; HWAVEOUT hs[2] = { a.h, b.h }; WAVEHDR *hh[2] = { &ha, &hb }; DWORD_PTR in[2] = { 0x7001, 0x7002 };
    for (int k = 0; k < 2; k++) {
        EXPECT_EQ(Ls[k]->total, 3);
        int n = Ls[k]->total < 3 ? Ls[k]->total : 3;
        for (int i = 0; i < n; i++) { CHECK(Ls[k]->r[i].h == (DWORD_PTR)hs[k]); CHECK(Ls[k]->r[i].inst == in[k]); }
        if (n == 3) { EXPECT_EQ(Ls[k]->r[0].msg, WOM_OPEN); EXPECT_EQ(Ls[k]->r[1].msg, WOM_DONE); EXPECT_EQ(Ls[k]->r[1].p1, (DWORD_PTR)hh[k]); EXPECT_EQ(Ls[k]->r[2].msg, WOM_CLOSE); }
    }
    HeapFree(GetProcessHeap(), 0, da); HeapFree(GetProcessHeap(), 0, db);
    fn_done(&a); fn_done(&b);
}

/* Several open/close cycles: exactly OPEN, CLOSE each time. */
TEST(wcb_fn_open_close_cycles) {
    NEED_OUT();
    WAVEFORMATEX f; wcb_fmt_default(&f);
    for (int i = 0; i < 10; i++) {
        fnctx c;
        if (fn_open(&c, (i & 1) ? 0 : WAVE_MAPPER, &f, 0x7100 + i, 0)) { fn_done(&c); CHECK(0); continue; }
        EXPECT_EQ(U(waveOutClose)(c.h), 0);
        EXPECT_EQ(c.L.total, 2);
        if (c.L.total == 2) { EXPECT_EQ(c.L.r[0].msg, WOM_OPEN); EXPECT_EQ(c.L.r[1].msg, WOM_CLOSE); EXPECT_EQ(c.L.r[0].h, c.L.r[1].h); }
        fn_done(&c);
    }
}

/* Flags that pass the winmmbase mask combine with CALLBACK_FUNCTION without changing the sequence. */
TEST(wcb_fn_allowsync_and_direct) {
    NEED_OUT();
    WAVEFORMATEX f; wcb_fmt_default(&f);
    struct { UINT id; DWORD fl; } c[] = { { WAVE_MAPPER, WAVE_ALLOWSYNC }, { 0, WAVE_ALLOWSYNC }, { 0, WAVE_FORMAT_DIRECT }, { WAVE_MAPPER, WAVE_FORMAT_DIRECT }, { 0, WAVE_MAPPED } };
    for (int i = 0; i < 5; i++) {
        fnctx x;
        MMRESULT r = fn_open(&x, c[i].id, &f, 0x7200 + i, c[i].fl);
        CHECKF(r == 0, "%s flags 0x%lx open %u", wcb_idname(c[i].id), c[i].fl, r);
        if (r) { EXPECT_EQ(x.L.total, 0); fn_done(&x); continue; }
        EXPECT_EQ(x.L.total, 1);
        WAVEHDR wh; DWORD len; char *d = wcb_silence(&f, 20, &len); wcb_hdr(&wh, d, len);
        U(waveOutPrepareHeader)(x.h, &wh, sizeof wh); U(waveOutWrite)(x.h, &wh, sizeof wh);
        CHECK(wcb_wait_total(&x.L, 2, WCB_WAIT_MS));
        U(waveOutUnprepareHeader)(x.h, &wh, sizeof wh);
        EXPECT_EQ(U(waveOutClose)(x.h), 0);
        EXPECT_EQ(x.L.total, 3);
        HeapFree(GetProcessHeap(), 0, d);
        fn_done(&x);
    }
}

/* A failed real open (unsupported format) delivers no WOM_OPEN. */
TEST(wcb_fn_badformat_open_no_callback) {
    NEED_OUT();
    WAVEFORMATEX f; wcb_fmt(&f, 44100, 16, 6);
    fnctx c;
    MMRESULT r = fn_open(&c, 0, &f, 0x7300, WAVE_FORMAT_DIRECT);
    t_note("6-ch PCM real open on id 0 (DIRECT): %u", r);
    if (r == 0) U(waveOutClose)(c.h);
    else { CHECK(c.h == NULL); Sleep(30); EXPECT_EQ(c.L.total, 0); }
    fn_done(&c);
    WAVEFORMATEX g; memset(&g, 0, sizeof g); g.wFormatTag = 0x7777; g.nChannels = 2; g.nSamplesPerSec = 44100; g.wBitsPerSample = 16; g.nBlockAlign = 4; g.nAvgBytesPerSec = 176400;
    r = fn_open(&c, WAVE_MAPPER, &g, 0x7301, 0);
    CHECKF(r == WAVERR_BADFORMAT, "unknown format tag through the mapper: %u", r);
    if (r == 0) U(waveOutClose)(c.h);
    else { CHECK(c.h == NULL); Sleep(30); EXPECT_EQ(c.L.total, 0); }
    fn_done(&c);
}

/* NULL function pointer: DriverCallback skips a NULL target, so open/play/close work with no delivery. */
TEST(wcb_fn_null_pointer_target) {
    NEED_OUT();
    WAVEFORMATEX f; wcb_fmt_default(&f);
    UINT ids[6]; int k = wcb_out_ids(ids, 6);
    for (int i = 0; i < k; i++) {
        HWAVEOUT h = NULL;
        MMRESULT r = U(waveOutOpen)(&h, ids[i], &f, 0, 0x7400, CALLBACK_FUNCTION);
        CHECKF(r == 0, "%s CALLBACK_FUNCTION NULL: %u", wcb_idname(ids[i]), r);
        if (r) continue;
        EXPECT_EQ(wcb_play_silence_poll(h, &f, 2, 20), 2);
        EXPECT_EQ(U(waveOutClose)(h), 0);
    }
}

/* All WOM_DONE of one handle arrive on one thread (the driver's completion thread), never the caller's. */
TEST(wcb_fn_done_single_worker_thread) {
    NEED_OUT();
    WAVEFORMATEX f; wcb_fmt_default(&f);
    UINT ids[6]; int k = wcb_out_ids(ids, 6);
    for (int x = 0; x < k; x++) {
        fnctx c;
        if (fn_open(&c, ids[x], &f, 0x7500 + x, 0)) { fn_done(&c); CHECK(0); continue; }
        WAVEHDR hd[5]; char *d[5]; DWORD len;
        for (int i = 0; i < 5; i++) { d[i] = wcb_silence(&f, 20, &len); wcb_hdr(&hd[i], d[i], len); U(waveOutPrepareHeader)(c.h, &hd[i], sizeof hd[i]); U(waveOutWrite)(c.h, &hd[i], sizeof hd[i]); }
        CHECK(wcb_wait_total(&c.L, 6, WCB_WAIT_MS));
        DWORD t = 0; int same = 1, n = c.L.total < WCB_MAXREC ? c.L.total : WCB_MAXREC;
        for (int i = 0; i < n; i++) if (c.L.r[i].msg == WOM_DONE) { if (!t) t = c.L.r[i].tid; else if (c.L.r[i].tid != t) same = 0; }
        CHECKF(same, "%s: WOM_DONE on more than one thread", wcb_idname(ids[x]));
        CHECK(t != 0 && t != GetCurrentThreadId());
        U(waveOutReset)(c.h);
        for (int i = 0; i < 5; i++) { U(waveOutUnprepareHeader)(c.h, &hd[i], sizeof hd[i]); HeapFree(GetProcessHeap(), 0, d[i]); }
        EXPECT_EQ(U(waveOutClose)(c.h), 0);
        fn_done(&c);
    }
}

/* WOM_DONE from waveOutReset: record which thread delivers it (the resetting caller or the worker). */
TEST(wcb_fn_reset_done_thread) {
    NEED_OUT();
    WAVEFORMATEX f; wcb_fmt_default(&f);
    fnctx c;
    if (fn_open(&c, WAVE_MAPPER, &f, 0x7600, 0)) { fn_done(&c); CHECK(0); return; }
    WAVEHDR wh; DWORD len; char *d = wcb_silence(&f, 1000, &len); wcb_hdr(&wh, d, len);
    U(waveOutPrepareHeader)(c.h, &wh, sizeof wh); U(waveOutWrite)(c.h, &wh, sizeof wh);
    EXPECT_EQ(U(waveOutReset)(c.h), 0);
    CHECK(wcb_wait_total(&c.L, 2, WCB_WAIT_MS));
    if (c.L.total >= 2) t_note("WOM_DONE from waveOutReset delivered on the %s thread", c.L.r[1].tid == GetCurrentThreadId() ? "caller" : "worker");
    U(waveOutUnprepareHeader)(c.h, &wh, sizeof wh);
    EXPECT_EQ(U(waveOutClose)(c.h), 0);
    HeapFree(GetProcessHeap(), 0, d);
    fn_done(&c);
}
