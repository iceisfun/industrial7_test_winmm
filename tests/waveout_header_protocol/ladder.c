/* waveout_header_protocol: the per-function state ladder after the gate, without a live handle.
 *
 * Decomp (winmmbase):
 *   waveOutPrepareHeader   PREPARED -> 0 (no handle check, header untouched); otherwise ValidateHandle -> 5
 *   waveOutUnprepareHeader INQUEUE -> 33 (checked first); !PREPARED -> 0 (no handle check); otherwise -> 5
 *   waveOutWrite           !PREPARED -> 34 (checked first); INQUEUE -> 33; otherwise ValidateHandle -> 5
 * Every one of these returns before the header is written (Prepare masks dwFlags and Write clears DONE only after the
 * handle passed), so the header must come back byte-identical.
 */
#include "whp.h"

static const DWORD g_allowed_bits[] = { WHDR_DONE, WHDR_PREPARED, WHDR_BEGINLOOP, WHDR_ENDLOOP, WHDR_INQUEUE, 0x1000, 0x2000 };

static DWORD combo_flags(int combo)
{
    DWORD fl = 0;
    for (int k = 0; k < 7; k++) if (combo & (1 << k)) fl |= g_allowed_bits[k];
    return fl;
}

static MMRESULT model(int fn, DWORD fl)
{
    switch (fn) {
    case WHP_PREP:   return (fl & WHDR_PREPARED) ? MMSYSERR_NOERROR : MMSYSERR_INVALHANDLE;
    case WHP_UNPREP: return (fl & WHDR_INQUEUE) ? WAVERR_STILLPLAYING
                          : !(fl & WHDR_PREPARED) ? MMSYSERR_NOERROR : MMSYSERR_INVALHANDLE;
    default:         return !(fl & WHDR_PREPARED) ? WAVERR_UNPREPARED
                          : (fl & WHDR_INQUEUE) ? WAVERR_STILLPLAYING : MMSYSERR_INVALHANDLE;
    }
}

/* ---- exhaustive: 128 allowed-bit combinations x every non-live handle ---------------------------------------- */

static void exhaustive_case(int fn)
{
    whp_hdr_fn f = whp_fn(fn);
    REQUIRE(f);
    char buf[32] = {0};
    int bad = 0;
    for (int combo = 0; combo < 128; combo++)
        for (int i = 0; i < WHP_NBAD; i++) {
            DWORD fl = combo_flags(combo);
            WAVEHDR h, ref; whp_hdr_init(&h, fl, buf, sizeof buf); ref = h;
            MMRESULT r = f((HWAVEOUT)whp_bad_handles[i], &h, sizeof h);
            MMRESULT want = model(fn, fl);
            CHECKF(r == want, "%s flags=0x%lx h=%p -> %u want %u", whp_fn_name[fn], fl, (void *)whp_bad_handles[i], r, want);
            if (memcmp(&h, &ref, sizeof h)) bad++;
        }
    CHECKF(bad == 0, "%s changed %d headers on headless paths", whp_fn_name[fn], bad);
}
TEST(whp_ladder_exhaustive_prepare)   { exhaustive_case(WHP_PREP); }
TEST(whp_ladder_exhaustive_unprepare) { exhaustive_case(WHP_UNPREP); }
TEST(whp_ladder_exhaustive_write)     { exhaustive_case(WHP_WRITE); }

/* ---- waveOutPrepareHeader ------------------------------------------------------------------------------------- */

TEST(whp_prepare_prepared_null_handle_ok)
{
    REQUIRE(U(waveOutPrepareHeader));
    char buf[16] = {0}; WAVEHDR h; whp_hdr_init(&h, WHDR_PREPARED, buf, sizeof buf);
    EXPECT_EQ(U(waveOutPrepareHeader)(NULL, &h, sizeof h), MMSYSERR_NOERROR);
    EXPECT_EQ(h.dwFlags, WHDR_PREPARED);
}

TEST(whp_prepare_unprepared_null_handle_invalhandle)
{
    REQUIRE(U(waveOutPrepareHeader));
    char buf[16] = {0}; WAVEHDR h; whp_hdr_init(&h, 0, buf, sizeof buf);
    EXPECT_EQ(U(waveOutPrepareHeader)(NULL, &h, sizeof h), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(h.dwFlags, 0);
}

TEST(whp_prepare_0x1000_prepared_ok)
{
    REQUIRE(U(waveOutPrepareHeader));
    char buf[16] = {0}; WAVEHDR h; whp_hdr_init(&h, 0x1000 | WHDR_PREPARED, buf, sizeof buf);
    EXPECT_EQ(U(waveOutPrepareHeader)(NULL, &h, sizeof h), MMSYSERR_NOERROR);
    EXPECT_EQ(h.dwFlags, 0x1000 | WHDR_PREPARED);
}

TEST(whp_prepare_prepared_inqueue_ok_untouched)
{
    /* the early return does not look at INQUEUE or DONE at all */
    REQUIRE(U(waveOutPrepareHeader));
    char buf[16] = {0}; WAVEHDR h;
    static const DWORD st[] = { WHDR_PREPARED | WHDR_INQUEUE, WHDR_PREPARED | WHDR_DONE,
                                WHDR_PREPARED | WHDR_DONE | WHDR_INQUEUE | WHDR_BEGINLOOP | WHDR_ENDLOOP };
    for (int i = 0; i < 3; i++) {
        whp_hdr_init(&h, st[i], buf, sizeof buf);
        EXPECT_EQ(U(waveOutPrepareHeader)((HWAVEOUT)1, &h, sizeof h), MMSYSERR_NOERROR);
        EXPECT_EQ(h.dwFlags, st[i]);
    }
}

TEST(whp_prepare_unprepared_states_not_masked_on_bad_handle)
{
    /* dwFlags &= 0xC happens only after the handle passed: a failing prepare keeps DONE/INQUEUE/0x1000 */
    REQUIRE(U(waveOutPrepareHeader));
    char buf[16] = {0}; WAVEHDR h;
    static const DWORD st[] = { WHDR_DONE, WHDR_INQUEUE, WHDR_DONE | WHDR_INQUEUE, 0x1000, 0x2000,
                                WHDR_DONE | WHDR_BEGINLOOP | WHDR_ENDLOOP, 0x301D };
    for (unsigned i = 0; i < sizeof st / sizeof st[0]; i++)
        for (int k = 0; k < WHP_NBAD; k++) {
            whp_hdr_init(&h, st[i], buf, sizeof buf);
            EXPECT_EQ(U(waveOutPrepareHeader)((HWAVEOUT)whp_bad_handles[k], &h, sizeof h), MMSYSERR_INVALHANDLE);
            EXPECT_EQ(h.dwFlags, st[i]);
        }
}

TEST(whp_prepare_twice_null_handle)
{
    REQUIRE(U(waveOutPrepareHeader));
    char buf[16] = {0}; WAVEHDR h; whp_hdr_init(&h, WHDR_PREPARED, buf, sizeof buf);
    for (int i = 0; i < 5; i++) EXPECT_EQ(U(waveOutPrepareHeader)(NULL, &h, sizeof h), MMSYSERR_NOERROR);
    EXPECT_EQ(h.dwFlags, WHDR_PREPARED);
}

/* ---- waveOutUnprepareHeader ----------------------------------------------------------------------------------- */

TEST(whp_unprepare_inqueue_stillplaying)
{
    REQUIRE(U(waveOutUnprepareHeader));
    char buf[16] = {0}; WAVEHDR h;
    whp_hdr_init(&h, WHDR_PREPARED | WHDR_INQUEUE, buf, sizeof buf);
    EXPECT_EQ(U(waveOutUnprepareHeader)(NULL, &h, sizeof h), WAVERR_STILLPLAYING);
    EXPECT_EQ(h.dwFlags, WHDR_PREPARED | WHDR_INQUEUE);
}

TEST(whp_unprepare_inqueue_without_prepared_stillplaying)
{
    /* INQUEUE is tested before PREPARED */
    REQUIRE(U(waveOutUnprepareHeader));
    char buf[16] = {0}; WAVEHDR h;
    whp_hdr_init(&h, WHDR_INQUEUE, buf, sizeof buf);
    EXPECT_EQ(U(waveOutUnprepareHeader)(NULL, &h, sizeof h), WAVERR_STILLPLAYING);
    EXPECT_EQ(h.dwFlags, WHDR_INQUEUE);
}

TEST(whp_unprepare_not_prepared_ok_any_handle)
{
    REQUIRE(U(waveOutUnprepareHeader));
    char buf[16] = {0}; WAVEHDR h;
    static const DWORD st[] = { 0, WHDR_DONE, WHDR_BEGINLOOP, WHDR_ENDLOOP, 0x1000, 0x2000, WHDR_DONE | 0x300C };
    for (unsigned i = 0; i < sizeof st / sizeof st[0]; i++)
        for (int k = 0; k < WHP_NBAD; k++) {
            whp_hdr_init(&h, st[i], buf, sizeof buf);
            EXPECT_EQ(U(waveOutUnprepareHeader)((HWAVEOUT)whp_bad_handles[k], &h, sizeof h), MMSYSERR_NOERROR);
            EXPECT_EQ(h.dwFlags, st[i]);
        }
}

TEST(whp_unprepare_prepared_bad_handle_invalhandle)
{
    REQUIRE(U(waveOutUnprepareHeader));
    char buf[16] = {0}; WAVEHDR h;
    for (int k = 0; k < WHP_NBAD; k++) {
        whp_hdr_init(&h, WHDR_PREPARED | WHDR_DONE, buf, sizeof buf);
        EXPECT_EQ(U(waveOutUnprepareHeader)((HWAVEOUT)whp_bad_handles[k], &h, sizeof h), MMSYSERR_INVALHANDLE);
        EXPECT_EQ(h.dwFlags, WHDR_PREPARED | WHDR_DONE);
    }
}

/* ---- waveOutWrite --------------------------------------------------------------------------------------------- */

TEST(whp_write_unprepared_unprepared)
{
    REQUIRE(U(waveOutWrite));
    char buf[16] = {0}; WAVEHDR h; whp_hdr_init(&h, 0, buf, sizeof buf);
    EXPECT_EQ(U(waveOutWrite)(NULL, &h, sizeof h), WAVERR_UNPREPARED);
    EXPECT_EQ(h.dwFlags, 0);
}

TEST(whp_write_inqueue_without_prepared_unprepared)
{
    /* PREPARED is tested before INQUEUE */
    REQUIRE(U(waveOutWrite));
    char buf[16] = {0}; WAVEHDR h; whp_hdr_init(&h, WHDR_INQUEUE, buf, sizeof buf);
    EXPECT_EQ(U(waveOutWrite)(NULL, &h, sizeof h), WAVERR_UNPREPARED);
    EXPECT_EQ(h.dwFlags, WHDR_INQUEUE);
}

TEST(whp_write_inqueue_stillplaying)
{
    REQUIRE(U(waveOutWrite));
    char buf[16] = {0}; WAVEHDR h; whp_hdr_init(&h, WHDR_PREPARED | WHDR_INQUEUE, buf, sizeof buf);
    for (int k = 0; k < WHP_NBAD; k++) {
        EXPECT_EQ(U(waveOutWrite)((HWAVEOUT)whp_bad_handles[k], &h, sizeof h), WAVERR_STILLPLAYING);
        EXPECT_EQ(h.dwFlags, WHDR_PREPARED | WHDR_INQUEUE);
    }
}

TEST(whp_write_prepared_bad_handle_keeps_done)
{
    /* DONE is cleared only after the handle check */
    REQUIRE(U(waveOutWrite));
    char buf[16] = {0}; WAVEHDR h;
    for (int k = 0; k < WHP_NBAD; k++) {
        whp_hdr_init(&h, WHDR_PREPARED | WHDR_DONE, buf, sizeof buf);
        EXPECT_EQ(U(waveOutWrite)((HWAVEOUT)whp_bad_handles[k], &h, sizeof h), MMSYSERR_INVALHANDLE);
        EXPECT_EQ(h.dwFlags, WHDR_PREPARED | WHDR_DONE);
    }
}

TEST(whp_write_done_only_unprepared)
{
    REQUIRE(U(waveOutWrite));
    char buf[16] = {0}; WAVEHDR h; whp_hdr_init(&h, WHDR_DONE, buf, sizeof buf);
    EXPECT_EQ(U(waveOutWrite)(NULL, &h, sizeof h), WAVERR_UNPREPARED);
    EXPECT_EQ(h.dwFlags, WHDR_DONE);
}

TEST(whp_write_loop_flags_unprepared)
{
    REQUIRE(U(waveOutWrite));
    char buf[16] = {0}; WAVEHDR h; whp_hdr_init(&h, WHDR_BEGINLOOP | WHDR_ENDLOOP, buf, sizeof buf);
    h.dwLoops = 3;
    EXPECT_EQ(U(waveOutWrite)(NULL, &h, sizeof h), WAVERR_UNPREPARED);
    EXPECT_EQ(h.dwFlags, WHDR_BEGINLOOP | WHDR_ENDLOOP);
    EXPECT_EQ(h.dwLoops, 3);
}

/* ---- cross-function sequences on one header with no handle ---------------------------------------------------- */

TEST(whp_headless_sequence_unprepared)
{
    REQUIRE(whp_have_all());
    char buf[16] = {0}; WAVEHDR h; whp_hdr_init(&h, 0, buf, sizeof buf);
    EXPECT_EQ(U(waveOutWrite)(NULL, &h, sizeof h), WAVERR_UNPREPARED);
    EXPECT_EQ(U(waveOutUnprepareHeader)(NULL, &h, sizeof h), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveOutPrepareHeader)(NULL, &h, sizeof h), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveOutWrite)(NULL, &h, sizeof h), WAVERR_UNPREPARED);
    EXPECT_EQ(h.dwFlags, 0);
}

TEST(whp_headless_sequence_fake_prepared)
{
    REQUIRE(whp_have_all());
    char buf[16] = {0}; WAVEHDR h; whp_hdr_init(&h, WHDR_PREPARED, buf, sizeof buf);
    EXPECT_EQ(U(waveOutPrepareHeader)(NULL, &h, sizeof h), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveOutWrite)(NULL, &h, sizeof h), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveOutUnprepareHeader)(NULL, &h, sizeof h), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(h.dwFlags, WHDR_PREPARED);
    h.dwFlags |= WHDR_INQUEUE;
    EXPECT_EQ(U(waveOutWrite)(NULL, &h, sizeof h), WAVERR_STILLPLAYING);
    EXPECT_EQ(U(waveOutUnprepareHeader)(NULL, &h, sizeof h), WAVERR_STILLPLAYING);
    EXPECT_EQ(U(waveOutPrepareHeader)(NULL, &h, sizeof h), MMSYSERR_NOERROR);
    EXPECT_EQ(h.dwFlags, WHDR_PREPARED | WHDR_INQUEUE);
}

/* The same ladder answers with an array of headers (independent state per header). */
TEST(whp_headless_header_array)
{
    REQUIRE(whp_have_all());
    WAVEHDR hs[8]; char buf[8] = {0};
    for (int i = 0; i < 8; i++) whp_hdr_init(&hs[i], (DWORD)(((i & 1) ? WHDR_PREPARED : 0) | ((i & 2) ? WHDR_INQUEUE : 0) |
                                                     ((i & 4) ? WHDR_DONE : 0)), buf, sizeof buf);
    for (int i = 0; i < 8; i++) {
        DWORD fl = hs[i].dwFlags;
        for (int fn = 0; fn < WHP_NFN; fn++) {
            MMRESULT r = whp_fn(fn)(NULL, &hs[i], sizeof hs[i]);
            CHECKF(r == model(fn, fl), "%s hdr %d flags 0x%lx -> %u", whp_fn_name[fn], i, fl, r);
        }
        EXPECT_EQ(hs[i].dwFlags, fl);
    }
}

/* The model is the same through the explicitly pinned winmm.dll export and through winmmbase directly. */
static void module_parity_case(const char *dll)
{
    __typeof__(&waveOutPrepareHeader) p = (__typeof__(&waveOutPrepareHeader))t_proc_in(dll, "waveOutPrepareHeader");
    __typeof__(&waveOutUnprepareHeader) u = (__typeof__(&waveOutUnprepareHeader))t_proc_in(dll, "waveOutUnprepareHeader");
    __typeof__(&waveOutWrite) w = (__typeof__(&waveOutWrite))t_proc_in(dll, "waveOutWrite");
    if (!p || !u || !w) { t_skip("%s does not export the header functions", dll); return; }
    char buf[8] = {0};
    for (int combo = 0; combo < 128; combo++) {
        DWORD fl = combo_flags(combo);
        WAVEHDR h; whp_hdr_init(&h, fl, buf, sizeof buf);
        CHECKF(p(NULL, &h, sizeof h) == model(WHP_PREP, fl), "%s prepare 0x%lx", dll, fl);
        CHECKF(u(NULL, &h, sizeof h) == model(WHP_UNPREP, fl), "%s unprepare 0x%lx", dll, fl);
        CHECKF(w(NULL, &h, sizeof h) == model(WHP_WRITE, fl), "%s write 0x%lx", dll, fl);
        CHECKF(h.dwFlags == fl, "%s changed 0x%lx", dll, fl);
        h.dwFlags = fl | 0x40;
        CHECKF(p(NULL, &h, sizeof h) == MMSYSERR_INVALPARAM, "%s prepare bad", dll);
        CHECKF(u(NULL, &h, sizeof h) == MMSYSERR_INVALPARAM, "%s unprepare bad", dll);
        CHECKF(w(NULL, &h, sizeof h) == MMSYSERR_INVALPARAM, "%s write bad", dll);
    }
}
TEST(whp_parity_winmm_export)     { REQUIRE(UIN("winmm.dll", waveOutWrite)); module_parity_case("winmm.dll"); }
TEST(whp_parity_winmmbase_export) { module_parity_case("winmmbase.dll"); }

TEST(whp_winmm_and_winmmbase_note)
{
    FARPROC a = t_proc_in("winmm.dll", "waveOutWrite");
    FARPROC b = t_proc_in("winmmbase.dll", "waveOutWrite");
    REQUIRE(a);
    t_note("winmm!waveOutWrite=%p winmmbase!waveOutWrite=%p same=%d", (void *)a, (void *)b, a == b);
}
