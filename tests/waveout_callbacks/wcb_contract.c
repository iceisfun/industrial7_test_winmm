/* waveout_callbacks: the contract half that holds on every audio configuration (0 or more devices).
 * Failed opens (parameter errors, bad ids, no devices) must deliver no callback of any type and leave
 * the out handle NULL. Validation order from winmmbase!waveOutOpen / waveInOpen:
 *   pwfx NULL -> 11; CALLBACK_WINDOW ((fdwOpen >> 16) & 7 == 1) with a non-NULL non-window -> 11;
 *   flags -> 10; phwo NULL (non-QUERY) -> 11, else *phwo = NULL; 0 devices -> 2; bad id -> 2.
 * VM_TIMEOUT: 240 */
#include "wcb.h"

static const DWORD k_types[] = { WCB_NULL, WCB_WINDOW, WCB_THREAD, WCB_FUNCTION, WCB_EVENT };
static const char *tname(DWORD t) {
    switch (t) { case WCB_NULL: return "NULL"; case WCB_WINDOW: return "WINDOW"; case WCB_THREAD: return "THREAD";
                 case WCB_FUNCTION: return "FUNCTION"; case WCB_EVENT: return "EVENT"; default: return "other"; }
}

/* One callback target of each type, all observable afterwards. */
typedef struct {
    HWND     win;
    wcb_thr  thr;
    wcb_log  fn;
    HANDLE   ev;
    wcb_log  winlog;
    int      ok;
} targets;

static void tg_init(targets *T) {
    memset(T, 0, sizeof *T);
    T->win = wcb_win_create();
    T->ok = wcb_thr_start(&T->thr) && T->win != NULL;
    wcb_log_init(&T->fn);
    wcb_log_init(&T->winlog);
    wcb_fn_register(&T->fn, (DWORD_PTR)&T->fn);
    T->ev = CreateEventA(NULL, FALSE, FALSE, NULL);
}
static void tg_target(targets *T, DWORD type, DWORD_PTR *cb, DWORD_PTR *inst) {
    *inst = 0;
    switch (type) {
    case WCB_WINDOW: *cb = (DWORD_PTR)T->win; break;
    case WCB_THREAD: *cb = T->thr.tid; break;
    case WCB_FUNCTION: *cb = (DWORD_PTR)wcb_fn_out; *inst = (DWORD_PTR)&T->fn; break;
    case WCB_EVENT: *cb = (DWORD_PTR)T->ev; break;
    default: *cb = 0; break;
    }
}
static void tg_target_in(targets *T, DWORD type, DWORD_PTR *cb, DWORD_PTR *inst) {
    tg_target(T, type, cb, inst);
    if (type == WCB_FUNCTION) *cb = (DWORD_PTR)wcb_fn_in;
}
/* Nothing at all may have been delivered to any target (short settle for queued types). */
static void tg_expect_nothing(targets *T, const char *what) {
    wcb_win_pump(T->win, &T->winlog, -1, 30);
    Sleep(20);
    CHECKF(T->fn.total == 0, "%s: function callback got %ld calls", what, T->fn.total);
    CHECKF(T->winlog.total == 0, "%s: window got %ld messages", what, T->winlog.total);
    int thrmm = wcb_count_range(&T->thr.log, MM_WOM_OPEN, MM_WIM_DATA);
    CHECKF(thrmm == 0, "%s: thread got %d MM messages", what, thrmm);
    CHECKF(WaitForSingleObject(T->ev, 0) == WAIT_TIMEOUT, "%s: event signalled", what);
}
static void tg_free(targets *T) {
    wcb_fn_unregister(&T->fn);
    wcb_thr_stop(&T->thr);
    wcb_win_destroy(T->win);
    wcb_log_free(&T->fn);
    wcb_log_free(&T->winlog);
    CloseHandle(T->ev);
}

TEST(wcb_env_record) {
    REQUIRE(U(waveOutGetNumDevs)); REQUIRE(U(waveInGetNumDevs));
    wcb_note_devices();
    UINT no = wcb_nout(), ni = wcb_nin();
    /* Reading the counts again must give the same values (no PnP change during the run). */
    EXPECT_EQ(U(waveOutGetNumDevs)(), no);
    EXPECT_EQ(U(waveInGetNumDevs)(), ni);
}

TEST(wcb_callback_constants) {
    /* The CALLBACK_* values the area relies on (mmsystem.h); the driver dispatch uses (flags >> 16) & 7. */
    EXPECT_EQ(CALLBACK_TYPEMASK, 0x00070000);
    EXPECT_EQ(CALLBACK_NULL, 0);
    EXPECT_EQ(CALLBACK_WINDOW, 0x00010000);
    EXPECT_EQ(CALLBACK_TASK, 0x00020000);
    EXPECT_EQ(CALLBACK_THREAD, CALLBACK_TASK);
    EXPECT_EQ(CALLBACK_FUNCTION, 0x00030000);
    EXPECT_EQ(CALLBACK_EVENT, 0x00050000);
    EXPECT_EQ(MM_WOM_OPEN, 0x3BB); EXPECT_EQ(MM_WOM_CLOSE, 0x3BC); EXPECT_EQ(MM_WOM_DONE, 0x3BD);
    EXPECT_EQ(MM_WIM_OPEN, 0x3BE); EXPECT_EQ(MM_WIM_CLOSE, 0x3BF); EXPECT_EQ(MM_WIM_DATA, 0x3C0);
    EXPECT_EQ(WOM_OPEN, MM_WOM_OPEN); EXPECT_EQ(WIM_DATA, MM_WIM_DATA);
}

/* ---- waveOut: out-of-range id with every callback type ---- */
TEST(wcb_out_badid_each_type_no_delivery) {
    REQUIRE(U(waveOutOpen));
    UINT n = wcb_nout();
    targets T; tg_init(&T);
    if (!T.ok) { tg_free(&T); t_skip("callback targets unavailable"); return; }
    WAVEFORMATEX f; wcb_fmt_default(&f);
    UINT bad[] = { n, n + 1, 9999, 0x7FFFFFFF, 0xFFFE };
    for (int b = 0; b < 5; b++)
        for (int t = 0; t < 5; t++) {
            DWORD_PTR cb, inst; tg_target(&T, k_types[t], &cb, &inst);
            HWAVEOUT h = (HWAVEOUT)(DWORD_PTR)0x1234;
            MMRESULT r = U(waveOutOpen)(&h, bad[b], &f, cb, inst, k_types[t]);
            CHECKF(r == MMSYSERR_BADDEVICEID, "id %u type %s: %u", bad[b], tname(k_types[t]), r);
            CHECKF(h == NULL, "id %u type %s: handle %p not cleared", bad[b], tname(k_types[t]), h);
        }
    tg_expect_nothing(&T, "waveOutOpen bad id");
    tg_free(&T);
}

TEST(wcb_out_badid_query_each_type_no_delivery) {
    REQUIRE(U(waveOutOpen));
    UINT n = wcb_nout();
    targets T; tg_init(&T);
    if (!T.ok) { tg_free(&T); t_skip("callback targets unavailable"); return; }
    WAVEFORMATEX f; wcb_fmt_default(&f);
    for (int t = 0; t < 5; t++) {
        DWORD_PTR cb, inst; tg_target(&T, k_types[t], &cb, &inst);
        HWAVEOUT h = (HWAVEOUT)(DWORD_PTR)0x1234;
        EXPECT_EQ(U(waveOutOpen)(&h, n, &f, cb, inst, k_types[t] | WAVE_FORMAT_QUERY), MMSYSERR_BADDEVICEID);
        /* With WAVE_FORMAT_QUERY the out pointer is not touched. */
        EXPECT_EQ(h, (HWAVEOUT)(DWORD_PTR)0x1234);
        EXPECT_EQ(U(waveOutOpen)(NULL, n, &f, cb, inst, k_types[t] | WAVE_FORMAT_QUERY), MMSYSERR_BADDEVICEID);
    }
    tg_expect_nothing(&T, "waveOutOpen bad id QUERY");
    tg_free(&T);
}

/* ---- waveOut: no devices -> mapper and id 0 fail with 2, nothing delivered ---- */
TEST(wcb_out_nodev_mapper_each_type) {
    REQUIRE(U(waveOutOpen));
    if (wcb_nout() != 0) { t_skip("waveOut devices present (%u): covered by the device half", wcb_nout()); return; }
    targets T; tg_init(&T);
    if (!T.ok) { tg_free(&T); t_skip("callback targets unavailable"); return; }
    WAVEFORMATEX f; wcb_fmt_default(&f);
    UINT ids[] = { WAVE_MAPPER, 0xFFFF, 0, 1 };
    for (int i = 0; i < 4; i++)
        for (int t = 0; t < 5; t++)
            for (int q = 0; q < 2; q++) {
                DWORD_PTR cb, inst; tg_target(&T, k_types[t], &cb, &inst);
                HWAVEOUT h = (HWAVEOUT)(DWORD_PTR)0x5555;
                MMRESULT r = U(waveOutOpen)(&h, ids[i], &f, cb, inst, k_types[t] | (q ? WAVE_FORMAT_QUERY : 0));
                CHECKF(r == MMSYSERR_BADDEVICEID, "nodev %s type %s q %d: %u", wcb_idname(ids[i]), tname(k_types[t]), q, r);
                if (!q) CHECK(h == NULL); else CHECK(h == (HWAVEOUT)(DWORD_PTR)0x5555);
            }
    tg_expect_nothing(&T, "waveOutOpen no devices");
    tg_free(&T);
}

/* ---- parameter errors win over device errors, and deliver nothing ---- */
TEST(wcb_out_pwfx_null_each_type) {
    REQUIRE(U(waveOutOpen));
    targets T; tg_init(&T);
    if (!T.ok) { tg_free(&T); t_skip("callback targets unavailable"); return; }
    for (int t = 0; t < 5; t++) {
        DWORD_PTR cb, inst; tg_target(&T, k_types[t], &cb, &inst);
        HWAVEOUT h = (HWAVEOUT)(DWORD_PTR)0x77;
        EXPECT_EQ(U(waveOutOpen)(&h, WAVE_MAPPER, NULL, cb, inst, k_types[t]), MMSYSERR_INVALPARAM);
        /* pwfx is checked before the handle is cleared */
        EXPECT_EQ(h, (HWAVEOUT)(DWORD_PTR)0x77);
        EXPECT_EQ(U(waveOutOpen)(&h, 0, NULL, cb, inst, k_types[t] | WAVE_FORMAT_QUERY), MMSYSERR_INVALPARAM);
        EXPECT_EQ(U(waveOutOpen)(NULL, 9999, NULL, cb, inst, k_types[t]), MMSYSERR_INVALPARAM);
    }
    tg_expect_nothing(&T, "pwfx NULL");
    tg_free(&T);
}

TEST(wcb_out_phwo_null_each_type) {
    REQUIRE(U(waveOutOpen));
    targets T; tg_init(&T);
    if (!T.ok) { tg_free(&T); t_skip("callback targets unavailable"); return; }
    WAVEFORMATEX f; wcb_fmt_default(&f);
    for (int t = 0; t < 5; t++) {
        DWORD_PTR cb, inst; tg_target(&T, k_types[t], &cb, &inst);
        EXPECT_EQ(U(waveOutOpen)(NULL, WAVE_MAPPER, &f, cb, inst, k_types[t]), MMSYSERR_INVALPARAM);
        EXPECT_EQ(U(waveOutOpen)(NULL, 0, &f, cb, inst, k_types[t]), MMSYSERR_INVALPARAM);
        EXPECT_EQ(U(waveOutOpen)(NULL, 9999, &f, cb, inst, k_types[t]), MMSYSERR_INVALPARAM);
    }
    tg_expect_nothing(&T, "phwo NULL");
    tg_free(&T);
}

TEST(wcb_out_badflags_each_type) {
    REQUIRE(U(waveOutOpen));
    targets T; tg_init(&T);
    if (!T.ok) { tg_free(&T); t_skip("callback targets unavailable"); return; }
    WAVEFORMATEX f; wcb_fmt_default(&f);
    /* mapper mask 0x7E04, device mask 0x7E10 */
    struct { UINT id; DWORD fl; } c[] = {
        { WAVE_MAPPER, WAVE_MAPPED }, { 0xFFFF, WAVE_MAPPED }, { 0, 0x10 }, { 1, 0x10 },
        { WAVE_MAPPER, 0x200 }, { WAVE_MAPPER, 0x400 }, { WAVE_MAPPER, 0x800 }, { WAVE_MAPPER, 0x1000 },
        { WAVE_MAPPER, 0x2000 }, { WAVE_MAPPER, 0x4000 }, { 0, 0x200 }, { 0, 0x4000 }, { 9999, 0x10 },
    };
    for (unsigned i = 0; i < sizeof c / sizeof c[0]; i++)
        for (int t = 0; t < 5; t++) {
            DWORD_PTR cb, inst; tg_target(&T, k_types[t], &cb, &inst);
            HWAVEOUT h = (HWAVEOUT)(DWORD_PTR)0x99;
            MMRESULT r = U(waveOutOpen)(&h, c[i].id, &f, cb, inst, k_types[t] | c[i].fl);
            CHECKF(r == MMSYSERR_INVALFLAG, "%s flags 0x%lx type %s: %u", wcb_idname(c[i].id), c[i].fl, tname(k_types[t]), r);
            /* the flag check comes before *phwo = NULL */
            CHECKF(h == (HWAVEOUT)(DWORD_PTR)0x99, "handle changed to %p", h);
        }
    tg_expect_nothing(&T, "bad flags");
    tg_free(&T);
}

/* ---- CALLBACK_WINDOW validation: IsWindow only when the type field is 1 and dwCallback != 0 ---- */
static HWND dead_window(void) {
    HWND w = wcb_win_create();
    DestroyWindow(w);
    return w;
}

TEST(wcb_out_window_dead_hwnd_11) {
    REQUIRE(U(waveOutOpen));
    HWND dead = dead_window();
    CHECK(!IsWindow(dead));
    WAVEFORMATEX f; wcb_fmt_default(&f);
    UINT ids[] = { WAVE_MAPPER, 0xFFFF, 0, 1, 9999, wcb_nout() };
    for (int i = 0; i < 6; i++) {
        HWAVEOUT h = (HWAVEOUT)(DWORD_PTR)0x42;
        EXPECT_EQ(U(waveOutOpen)(&h, ids[i], &f, (DWORD_PTR)dead, 0, CALLBACK_WINDOW), MMSYSERR_INVALPARAM);
        EXPECT_EQ(h, (HWAVEOUT)(DWORD_PTR)0x42);
        EXPECT_EQ(U(waveOutOpen)(&h, ids[i], &f, (DWORD_PTR)dead, 0, CALLBACK_WINDOW | WAVE_FORMAT_QUERY), MMSYSERR_INVALPARAM);
        EXPECT_EQ(U(waveOutOpen)(NULL, ids[i], &f, (DWORD_PTR)dead, 0, CALLBACK_WINDOW), MMSYSERR_INVALPARAM);
    }
}

TEST(wcb_out_window_check_before_flags) {
    REQUIRE(U(waveOutOpen));
    HWND dead = dead_window();
    WAVEFORMATEX f; wcb_fmt_default(&f);
    /* Window check (11) precedes the flag check (10). */
    EXPECT_EQ(U(waveOutOpen)(NULL, WAVE_MAPPER, &f, (DWORD_PTR)dead, 0, CALLBACK_WINDOW | WAVE_MAPPED), MMSYSERR_INVALPARAM);
    EXPECT_EQ(U(waveOutOpen)(NULL, 0, &f, (DWORD_PTR)dead, 0, CALLBACK_WINDOW | 0x10), MMSYSERR_INVALPARAM);
    EXPECT_EQ(U(waveOutOpen)(NULL, 0, &f, (DWORD_PTR)dead, 0, CALLBACK_WINDOW | 0x4000), MMSYSERR_INVALPARAM);
    /* pwfx NULL precedes everything */
    EXPECT_EQ(U(waveOutOpen)(NULL, 0, NULL, (DWORD_PTR)dead, 0, CALLBACK_WINDOW | 0x4000), MMSYSERR_INVALPARAM);
    /* With a valid window the flag error surfaces. */
    HWND w = wcb_win_create();
    EXPECT_EQ(U(waveOutOpen)(NULL, WAVE_MAPPER, &f, (DWORD_PTR)w, 0, CALLBACK_WINDOW | WAVE_MAPPED), MMSYSERR_INVALFLAG);
    EXPECT_EQ(U(waveOutOpen)(NULL, 0, &f, (DWORD_PTR)w, 0, CALLBACK_WINDOW | 0x10), MMSYSERR_INVALFLAG);
    wcb_win_destroy(w);
}

TEST(wcb_out_window_typefield_aliases) {
    REQUIRE(U(waveOutOpen));
    /* (fdwOpen >> 16) & 7 == 1: 0x90000 and 0xF90000 also select the window check. */
    HWND dead = dead_window();
    WAVEFORMATEX f; wcb_fmt_default(&f);
    DWORD al[] = { 0x00010000, 0x00090000, 0x00190000, 0x00F90000, 0x7FF90000 };
    for (int i = 0; i < 5; i++) {
        HWAVEOUT h = (HWAVEOUT)(DWORD_PTR)0x42;
        MMRESULT r = U(waveOutOpen)(&h, WAVE_MAPPER, &f, (DWORD_PTR)dead, 0, al[i]);
        CHECKF(r == MMSYSERR_INVALPARAM, "flags 0x%lx: %u", al[i], r);
        CHECK(h == (HWAVEOUT)(DWORD_PTR)0x42);
    }
}

TEST(wcb_out_nonwindow_types_not_validated) {
    REQUIRE(U(waveOutOpen));
    /* A dead window handle given as THREAD/FUNCTION/EVENT/NULL type is not checked by winmmbase:
     * the result is the device result (2 for an out-of-range id). */
    HWND dead = dead_window();
    WAVEFORMATEX f; wcb_fmt_default(&f);
    UINT n = wcb_nout();
    DWORD ty[] = { WCB_NULL, WCB_THREAD, WCB_EVENT, 0x40000, 0x60000, 0x70000 };
    for (int i = 0; i < 6; i++) {
        HWAVEOUT h = (HWAVEOUT)(DWORD_PTR)0x42;
        MMRESULT r = U(waveOutOpen)(&h, n, &f, (DWORD_PTR)dead, 0, ty[i]);
        CHECKF(r == MMSYSERR_BADDEVICEID, "type 0x%lx: %u", ty[i], r);
        CHECK(h == NULL);
    }
}

TEST(wcb_out_window_null_hwnd_accepted_contract) {
    REQUIRE(U(waveOutOpen));
    /* CALLBACK_WINDOW with dwCallback 0 skips IsWindow: the result is the device result. */
    WAVEFORMATEX f; wcb_fmt_default(&f);
    UINT n = wcb_nout();
    EXPECT_EQ(U(waveOutOpen)(NULL, n, &f, 0, 0, CALLBACK_WINDOW | WAVE_FORMAT_QUERY), MMSYSERR_BADDEVICEID);
    EXPECT_EQ(U(waveOutOpen)(NULL, WAVE_MAPPER, &f, 0, 0, CALLBACK_WINDOW | WAVE_FORMAT_QUERY), n ? MMSYSERR_NOERROR : MMSYSERR_BADDEVICEID);
    HWAVEOUT h = (HWAVEOUT)(DWORD_PTR)0x42;
    EXPECT_EQ(U(waveOutOpen)(&h, n, &f, 0, 0, CALLBACK_WINDOW), MMSYSERR_BADDEVICEID);
    EXPECT_EQ(h, NULL);
}

TEST(wcb_out_window_other_thread_window_valid) {
    REQUIRE(U(waveOutOpen));
    /* IsWindow is process-wide: the desktop window passes the check (QUERY only, no messages sent). */
    WAVEFORMATEX f; wcb_fmt_default(&f);
    UINT n = wcb_nout();
    HWND desk = GetDesktopWindow();
    EXPECT_EQ(U(waveOutOpen)(NULL, n, &f, (DWORD_PTR)desk, 0, CALLBACK_WINDOW | WAVE_FORMAT_QUERY), MMSYSERR_BADDEVICEID);
    EXPECT_EQ(U(waveOutOpen)(NULL, WAVE_MAPPER, &f, (DWORD_PTR)desk, 0, CALLBACK_WINDOW | WAVE_FORMAT_QUERY), n ? MMSYSERR_NOERROR : MMSYSERR_BADDEVICEID);
}

/* ---- WAVE_FORMAT_QUERY never delivers a callback ---- */
TEST(wcb_out_query_no_callback_any_type) {
    REQUIRE(U(waveOutOpen));
    UINT n = wcb_nout();
    targets T; tg_init(&T);
    if (!T.ok) { tg_free(&T); t_skip("callback targets unavailable"); return; }
    WAVEFORMATEX f; wcb_fmt_default(&f);
    UINT ids[6]; int k = wcb_out_ids(ids, 6);
    if (k == 0) { ids[0] = WAVE_MAPPER; ids[1] = 0; k = 2; }
    for (int i = 0; i < k; i++)
        for (int t = 0; t < 5; t++) {
            DWORD_PTR cb, inst; tg_target(&T, k_types[t], &cb, &inst);
            HWAVEOUT h = (HWAVEOUT)(DWORD_PTR)0xABCD;
            MMRESULT r = U(waveOutOpen)(&h, ids[i], &f, cb, inst, k_types[t] | WAVE_FORMAT_QUERY);
            CHECKF(r == (n ? MMSYSERR_NOERROR : MMSYSERR_BADDEVICEID), "%s %s: %u", wcb_idname(ids[i]), tname(k_types[t]), r);
            CHECK(h == (HWAVEOUT)(DWORD_PTR)0xABCD);
        }
    tg_expect_nothing(&T, "WAVE_FORMAT_QUERY");
    tg_free(&T);
}

TEST(wcb_out_query_bad_format_no_callback) {
    REQUIRE(U(waveOutOpen));
    UINT n = wcb_nout();
    targets T; tg_init(&T);
    if (!T.ok) { tg_free(&T); t_skip("callback targets unavailable"); return; }
    WAVEFORMATEX f; wcb_fmt(&f, 44100, 16, 6);   /* 6-channel plain PCM: WAVERR_BADFORMAT on the HDA guest */
    for (int t = 0; t < 5; t++) {
        DWORD_PTR cb, inst; tg_target(&T, k_types[t], &cb, &inst);
        MMRESULT r = U(waveOutOpen)(NULL, WAVE_MAPPER, &f, cb, inst, k_types[t] | WAVE_FORMAT_QUERY);
        if (n == 0) EXPECT_EQ(r, MMSYSERR_BADDEVICEID);
        else CHECKF(r == WAVERR_BADFORMAT || r == MMSYSERR_NOERROR, "6ch query %u", r);
        if (t == 0 && n) t_note("6ch PCM query through the mapper: %u", r);
    }
    tg_expect_nothing(&T, "QUERY bad format");
    tg_free(&T);
}

TEST(wcb_out_query_any_type_value_accepted) {
    REQUIRE(U(waveOutOpen));
    /* The query path does not look at the callback type field at all: 0..7 << 16 all give the device result. */
    UINT n = wcb_nout();
    WAVEFORMATEX f; wcb_fmt_default(&f);
    for (DWORD t = 0; t < 8; t++) {
        if (t == 1) continue; /* window type is validated when dwCallback != 0 */
        MMRESULT r = U(waveOutOpen)(NULL, WAVE_MAPPER, &f, 0x1234, 0, (t << 16) | WAVE_FORMAT_QUERY);
        CHECKF(r == (n ? MMSYSERR_NOERROR : MMSYSERR_BADDEVICEID), "type %lu: %u", t, r);
        r = U(waveOutOpen)(NULL, n, &f, 0x1234, 0, (t << 16) | WAVE_FORMAT_QUERY);
        CHECKF(r == MMSYSERR_BADDEVICEID, "type %lu bad id: %u", t, r);
    }
}

/* ---- waveIn mirror ---- */
TEST(wcb_in_badid_each_type_no_delivery) {
    REQUIRE(U(waveInOpen));
    UINT n = wcb_nin();
    targets T; tg_init(&T);
    if (!T.ok) { tg_free(&T); t_skip("callback targets unavailable"); return; }
    WAVEFORMATEX f; wcb_fmt(&f, 44100, 16, 1);
    UINT bad[] = { n, n + 1, 9999, 0xFFFE };
    for (int b = 0; b < 4; b++)
        for (int t = 0; t < 5; t++) {
            DWORD_PTR cb, inst; tg_target_in(&T, k_types[t], &cb, &inst);
            HWAVEIN h = (HWAVEIN)(DWORD_PTR)0x1234;
            MMRESULT r = U(waveInOpen)(&h, bad[b], &f, cb, inst, k_types[t]);
            CHECKF(r == MMSYSERR_BADDEVICEID, "in id %u type %s: %u", bad[b], tname(k_types[t]), r);
            CHECK(h == NULL);
            h = (HWAVEIN)(DWORD_PTR)0x1234;
            r = U(waveInOpen)(&h, bad[b], &f, cb, inst, k_types[t] | WAVE_FORMAT_QUERY);
            CHECKF(r == MMSYSERR_BADDEVICEID, "in query id %u type %s: %u", bad[b], tname(k_types[t]), r);
            CHECK(h == (HWAVEIN)(DWORD_PTR)0x1234);
        }
    tg_expect_nothing(&T, "waveInOpen bad id");
    tg_free(&T);
}

TEST(wcb_in_nodev_mapper_each_type) {
    REQUIRE(U(waveInOpen));
    if (wcb_nin() != 0) { t_skip("waveIn devices present (%u)", wcb_nin()); return; }
    targets T; tg_init(&T);
    if (!T.ok) { tg_free(&T); t_skip("callback targets unavailable"); return; }
    WAVEFORMATEX f; wcb_fmt(&f, 44100, 16, 1);
    UINT ids[] = { WAVE_MAPPER, 0xFFFF, 0 };
    for (int i = 0; i < 3; i++)
        for (int t = 0; t < 5; t++) {
            DWORD_PTR cb, inst; tg_target_in(&T, k_types[t], &cb, &inst);
            HWAVEIN h = (HWAVEIN)(DWORD_PTR)0x5555;
            MMRESULT r = U(waveInOpen)(&h, ids[i], &f, cb, inst, k_types[t]);
            CHECKF(r == MMSYSERR_BADDEVICEID, "in nodev %s %s: %u", wcb_idname(ids[i]), tname(k_types[t]), r);
            CHECK(h == NULL);
            EXPECT_EQ(U(waveInOpen)(NULL, ids[i], &f, cb, inst, k_types[t] | WAVE_FORMAT_QUERY), MMSYSERR_BADDEVICEID);
        }
    tg_expect_nothing(&T, "waveInOpen no devices");
    tg_free(&T);
}

TEST(wcb_in_param_errors_each_type) {
    REQUIRE(U(waveInOpen));
    targets T; tg_init(&T);
    if (!T.ok) { tg_free(&T); t_skip("callback targets unavailable"); return; }
    WAVEFORMATEX f; wcb_fmt(&f, 44100, 16, 1);
    for (int t = 0; t < 5; t++) {
        DWORD_PTR cb, inst; tg_target_in(&T, k_types[t], &cb, &inst);
        HWAVEIN h = (HWAVEIN)(DWORD_PTR)0x77;
        EXPECT_EQ(U(waveInOpen)(&h, WAVE_MAPPER, NULL, cb, inst, k_types[t]), MMSYSERR_INVALPARAM);
        EXPECT_EQ(h, (HWAVEIN)(DWORD_PTR)0x77);
        EXPECT_EQ(U(waveInOpen)(NULL, WAVE_MAPPER, &f, cb, inst, k_types[t]), MMSYSERR_INVALPARAM);
        EXPECT_EQ(U(waveInOpen)(NULL, 0, &f, cb, inst, k_types[t]), MMSYSERR_INVALPARAM);
        EXPECT_EQ(U(waveInOpen)(&h, WAVE_MAPPER, &f, cb, inst, k_types[t] | WAVE_MAPPED), MMSYSERR_INVALFLAG);
        EXPECT_EQ(U(waveInOpen)(&h, 0, &f, cb, inst, k_types[t] | 0x4000), MMSYSERR_INVALFLAG);
        EXPECT_EQ(h, (HWAVEIN)(DWORD_PTR)0x77);
    }
    tg_expect_nothing(&T, "waveInOpen parameter errors");
    tg_free(&T);
}

TEST(wcb_in_window_dead_hwnd_11) {
    REQUIRE(U(waveInOpen));
    HWND dead = dead_window();
    WAVEFORMATEX f; wcb_fmt(&f, 44100, 16, 1);
    UINT ids[] = { WAVE_MAPPER, 0, 9999, wcb_nin() };
    for (int i = 0; i < 4; i++) {
        HWAVEIN h = (HWAVEIN)(DWORD_PTR)0x42;
        EXPECT_EQ(U(waveInOpen)(&h, ids[i], &f, (DWORD_PTR)dead, 0, CALLBACK_WINDOW), MMSYSERR_INVALPARAM);
        EXPECT_EQ(h, (HWAVEIN)(DWORD_PTR)0x42);
        EXPECT_EQ(U(waveInOpen)(NULL, ids[i], &f, (DWORD_PTR)dead, 0, CALLBACK_WINDOW | WAVE_FORMAT_QUERY), MMSYSERR_INVALPARAM);
        EXPECT_EQ(U(waveInOpen)(NULL, ids[i], &f, (DWORD_PTR)dead, 0, 0x00090000), MMSYSERR_INVALPARAM);
    }
}

TEST(wcb_in_window_null_hwnd_contract) {
    REQUIRE(U(waveInOpen));
    UINT n = wcb_nin();
    WAVEFORMATEX f; wcb_fmt(&f, 44100, 16, 1);
    EXPECT_EQ(U(waveInOpen)(NULL, n, &f, 0, 0, CALLBACK_WINDOW | WAVE_FORMAT_QUERY), MMSYSERR_BADDEVICEID);
    HWAVEIN h = (HWAVEIN)(DWORD_PTR)0x42;
    EXPECT_EQ(U(waveInOpen)(&h, n, &f, 0, 0, CALLBACK_WINDOW), MMSYSERR_BADDEVICEID);
    EXPECT_EQ(h, NULL);
}

TEST(wcb_in_query_no_callback_any_type) {
    REQUIRE(U(waveInOpen));
    UINT n = wcb_nin();
    targets T; tg_init(&T);
    if (!T.ok) { tg_free(&T); t_skip("callback targets unavailable"); return; }
    WAVEFORMATEX f; wcb_fmt(&f, 44100, 16, 1);
    UINT ids[6]; int k = wcb_in_ids(ids, 6);
    if (k == 0) { ids[0] = WAVE_MAPPER; ids[1] = 0; k = 2; }
    for (int i = 0; i < k; i++)
        for (int t = 0; t < 5; t++) {
            DWORD_PTR cb, inst; tg_target_in(&T, k_types[t], &cb, &inst);
            MMRESULT r = U(waveInOpen)(NULL, ids[i], &f, cb, inst, k_types[t] | WAVE_FORMAT_QUERY);
            CHECKF(r == (n ? MMSYSERR_NOERROR : MMSYSERR_BADDEVICEID), "in %s %s: %u", wcb_idname(ids[i]), tname(k_types[t]), r);
        }
    tg_expect_nothing(&T, "waveIn QUERY");
    tg_free(&T);
}

/* ---- handle misuse after/without an open does not produce callbacks ---- */
TEST(wcb_out_invalid_handle_calls_no_delivery) {
    REQUIRE(U(waveOutClose)); REQUIRE(U(waveOutReset)); REQUIRE(U(waveOutWrite)); REQUIRE(U(waveOutPrepareHeader));
    targets T; tg_init(&T);
    if (!T.ok) { tg_free(&T); t_skip("callback targets unavailable"); return; }
    HWAVEOUT bad[] = { NULL, (HWAVEOUT)(DWORD_PTR)1, (HWAVEOUT)(DWORD_PTR)0xFFFF, (HWAVEOUT)(DWORD_PTR)-1 };
    char buf[64] = {0};
    for (int i = 0; i < 4; i++) {
        WAVEHDR wh; wcb_hdr(&wh, buf, sizeof buf);
        EXPECT_EQ(U(waveOutClose)(bad[i]), MMSYSERR_INVALHANDLE);
        EXPECT_EQ(U(waveOutReset)(bad[i]), MMSYSERR_INVALHANDLE);
        EXPECT_EQ(U(waveOutPrepareHeader)(bad[i], &wh, sizeof wh), MMSYSERR_INVALHANDLE);
        wh.dwFlags = WHDR_PREPARED;
        EXPECT_EQ(U(waveOutWrite)(bad[i], &wh, sizeof wh), MMSYSERR_INVALHANDLE);
        EXPECT_EQ(wh.dwFlags, WHDR_PREPARED);
    }
    tg_expect_nothing(&T, "invalid handle calls");
    tg_free(&T);
}

TEST(wcb_in_invalid_handle_calls_no_delivery) {
    REQUIRE(U(waveInClose)); REQUIRE(U(waveInReset)); REQUIRE(U(waveInStart)); REQUIRE(U(waveInAddBuffer));
    HWAVEIN bad[] = { NULL, (HWAVEIN)(DWORD_PTR)1, (HWAVEIN)(DWORD_PTR)0xFFFF };
    char buf[64] = {0};
    for (int i = 0; i < 3; i++) {
        WAVEHDR wh; wcb_hdr(&wh, buf, sizeof buf);
        EXPECT_EQ(U(waveInClose)(bad[i]), MMSYSERR_INVALHANDLE);
        EXPECT_EQ(U(waveInReset)(bad[i]), MMSYSERR_INVALHANDLE);
        EXPECT_EQ(U(waveInStart)(bad[i]), MMSYSERR_INVALHANDLE);
        EXPECT_EQ(U(waveInAddBuffer)(bad[i], &wh, sizeof wh), WAVERR_UNPREPARED);
        wh.dwFlags = WHDR_PREPARED;
        EXPECT_EQ(U(waveInAddBuffer)(bad[i], &wh, sizeof wh), MMSYSERR_INVALHANDLE);
    }
}
