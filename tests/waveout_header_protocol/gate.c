/* waveout_header_protocol: the common front gate of waveOutPrepareHeader / waveOutUnprepareHeader / waveOutWrite.
 *
 * winmmbase (0x10910 / 0x109b0 / 0x10a70) starts every one of the three with
 *     if (pwh == NULL || cbwh < 0x30 || (pwh->dwFlags & 0xFFFFCFE0)) return MMSYSERR_INVALPARAM;
 * before it looks at the handle, so all of this runs without an audio device and with handles that never name a
 * live waveOut handle. A rejected call never writes the header.
 */
#include "whp.h"

static const UINT g_small_cb[] = { 0, 1, 2, 4, 8, 16, 24, 32, 36, 40, 44, 46, 47 };
#define N_SMALL_CB (sizeof g_small_cb / sizeof g_small_cb[0])
static const UINT g_big_cb[] = { 48, 49, 52, 56, 64, 96, 128, 0x100, 0x1000, 0x7FFFFFFF, 0x80000000u, 0xFFFFFFFFu };
#define N_BIG_CB (sizeof g_big_cb / sizeof g_big_cb[0])

/* header states that are interesting on the far side of the gate */
static const DWORD g_states[] = {
    0, WHDR_DONE, WHDR_PREPARED, WHDR_INQUEUE, WHDR_PREPARED | WHDR_INQUEUE, WHDR_PREPARED | WHDR_DONE,
    WHDR_BEGINLOOP | WHDR_ENDLOOP, 0x1000, 0x2000, 0x301F,
};
#define N_STATES (sizeof g_states / sizeof g_states[0])

/* Expected result for a header that passes the gate and a handle that is not a live waveOut handle. */
static MMRESULT model_bad_handle(int fn, DWORD fl)
{
    switch (fn) {
    case WHP_PREP:   return (fl & WHDR_PREPARED) ? MMSYSERR_NOERROR : MMSYSERR_INVALHANDLE;
    case WHP_UNPREP: return (fl & WHDR_INQUEUE) ? WAVERR_STILLPLAYING
                          : !(fl & WHDR_PREPARED) ? MMSYSERR_NOERROR : MMSYSERR_INVALHANDLE;
    default:         return !(fl & WHDR_PREPARED) ? WAVERR_UNPREPARED
                          : (fl & WHDR_INQUEUE) ? WAVERR_STILLPLAYING : MMSYSERR_INVALHANDLE;
    }
}

/* ---- NULL header ---------------------------------------------------------------------------------------------- */

static void null_header_case(int fn)
{
    whp_hdr_fn f = whp_fn(fn);
    REQUIRE(f);
    static const UINT cbs[] = { 0, 47, 48, 64, 0xFFFFFFFFu };
    for (int i = 0; i < WHP_NBAD; i++)
        for (unsigned c = 0; c < sizeof cbs / sizeof cbs[0]; c++)
            CHECKF(f((HWAVEOUT)whp_bad_handles[i], NULL, cbs[c]) == MMSYSERR_INVALPARAM,
                   "%s(h=%p, NULL, cb=%u)", whp_fn_name[fn], (void *)whp_bad_handles[i], cbs[c]);
}
TEST(whp_gate_null_header_prepare)   { null_header_case(WHP_PREP); }
TEST(whp_gate_null_header_unprepare) { null_header_case(WHP_UNPREP); }
TEST(whp_gate_null_header_write)     { null_header_case(WHP_WRITE); }

TEST(whp_gate_null_header_exact_codes)
{
    REQUIRE(U(waveOutPrepareHeader)); REQUIRE(U(waveOutUnprepareHeader)); REQUIRE(U(waveOutWrite));
    EXPECT_EQ(U(waveOutPrepareHeader)(NULL, NULL, sizeof(WAVEHDR)), MMSYSERR_INVALPARAM);
    EXPECT_EQ(U(waveOutUnprepareHeader)(NULL, NULL, sizeof(WAVEHDR)), MMSYSERR_INVALPARAM);
    EXPECT_EQ(U(waveOutWrite)(NULL, NULL, sizeof(WAVEHDR)), MMSYSERR_INVALPARAM);
    EXPECT_EQ(U(waveOutPrepareHeader)(NULL, NULL, 0), MMSYSERR_INVALPARAM);
    EXPECT_EQ(U(waveOutUnprepareHeader)(NULL, NULL, 0), MMSYSERR_INVALPARAM);
    EXPECT_EQ(U(waveOutWrite)(NULL, NULL, 0), MMSYSERR_INVALPARAM);
}

/* ---- cbwh below sizeof(WAVEHDR) ------------------------------------------------------------------------------- */

static void small_cb_case(int fn)
{
    whp_hdr_fn f = whp_fn(fn);
    REQUIRE(f);
    char buf[64] = {0};
    for (unsigned s = 0; s < N_STATES; s++)
        for (unsigned c = 0; c < N_SMALL_CB; c++)
            for (int i = 0; i < WHP_NBAD; i++) {
                WAVEHDR h, ref; whp_hdr_init(&h, g_states[s], buf, sizeof buf); ref = h;
                MMRESULT r = f((HWAVEOUT)whp_bad_handles[i], &h, g_small_cb[c]);
                CHECKF(r == MMSYSERR_INVALPARAM, "%s flags=0x%lx cb=%u h=%p -> %u", whp_fn_name[fn],
                       g_states[s], g_small_cb[c], (void *)whp_bad_handles[i], r);
                CHECKF(!memcmp(&h, &ref, sizeof h), "%s cb=%u wrote the header", whp_fn_name[fn], g_small_cb[c]);
            }
}
TEST(whp_gate_small_cb_prepare)   { small_cb_case(WHP_PREP); }
TEST(whp_gate_small_cb_unprepare) { small_cb_case(WHP_UNPREP); }
TEST(whp_gate_small_cb_write)     { small_cb_case(WHP_WRITE); }

/* 47 is the exact boundary; 48 (sizeof WAVEHDR on x64) passes. */
static void cb_boundary_case(int fn)
{
    whp_hdr_fn f = whp_fn(fn);
    REQUIRE(f);
    char buf[16] = {0};
    WAVEHDR h;
    whp_hdr_init(&h, WHDR_PREPARED | WHDR_INQUEUE, buf, sizeof buf);
    EXPECT_EQ(f(NULL, &h, 47), MMSYSERR_INVALPARAM);
    whp_hdr_init(&h, WHDR_PREPARED | WHDR_INQUEUE, buf, sizeof buf);
    MMRESULT want = model_bad_handle(fn, WHDR_PREPARED | WHDR_INQUEUE);
    EXPECT_EQ(f(NULL, &h, 48), want);
    EXPECT_EQ(h.dwFlags, WHDR_PREPARED | WHDR_INQUEUE);
}
TEST(whp_gate_cb_boundary_prepare)   { cb_boundary_case(WHP_PREP); }
TEST(whp_gate_cb_boundary_unprepare) { cb_boundary_case(WHP_UNPREP); }
TEST(whp_gate_cb_boundary_write)     { cb_boundary_case(WHP_WRITE); }

TEST(whp_gate_sizeof_wavehdr_is_48)
{
    /* The gate constant is the x64 structure size; a 32-bit sized header (32) is too small. */
    EXPECT_EQ(sizeof(WAVEHDR), 48);
    EXPECT_EQ(offsetof(WAVEHDR, dwFlags), 0x18);
    REQUIRE(U(waveOutWrite));
    char buf[8] = {0}; WAVEHDR h; whp_hdr_init(&h, WHDR_PREPARED, buf, sizeof buf);
    EXPECT_EQ(U(waveOutWrite)(NULL, &h, 32), MMSYSERR_INVALPARAM);
    EXPECT_EQ(U(waveOutWrite)(NULL, &h, 48), MMSYSERR_INVALHANDLE);
}

/* ---- cbwh at or above sizeof(WAVEHDR): the gate passes, the ladder decides ------------------------------------ */

static void big_cb_case(int fn)
{
    whp_hdr_fn f = whp_fn(fn);
    REQUIRE(f);
    /* the header lives inside a larger zeroed block so a large cbwh never describes memory we do not own */
    static union { WAVEHDR h; char raw[256]; } blk;
    char buf[32] = {0};
    for (unsigned s = 0; s < N_STATES; s++)
        for (unsigned c = 0; c < N_BIG_CB; c++)
            for (int i = 0; i < WHP_NBAD; i++) {
                memset(&blk, 0, sizeof blk);
                whp_hdr_init(&blk.h, g_states[s], buf, sizeof buf);
                WAVEHDR ref = blk.h;
                MMRESULT r = f((HWAVEOUT)whp_bad_handles[i], &blk.h, g_big_cb[c]);
                MMRESULT want = model_bad_handle(fn, g_states[s]);
                CHECKF(r == want, "%s flags=0x%lx cb=0x%x h=%p -> %u want %u", whp_fn_name[fn], g_states[s],
                       g_big_cb[c], (void *)whp_bad_handles[i], r, want);
                CHECKF(!memcmp(&blk.h, &ref, sizeof ref), "%s cb=0x%x changed the header", whp_fn_name[fn], g_big_cb[c]);
                int tail_clean = 1;
                for (unsigned k = sizeof(WAVEHDR); k < sizeof blk.raw; k++) if (blk.raw[k]) tail_clean = 0;
                CHECKF(tail_clean, "%s cb=0x%x wrote past the header", whp_fn_name[fn], g_big_cb[c]);
            }
}
TEST(whp_gate_big_cb_prepare)   { big_cb_case(WHP_PREP); }
TEST(whp_gate_big_cb_unprepare) { big_cb_case(WHP_UNPREP); }
TEST(whp_gate_big_cb_write)     { big_cb_case(WHP_WRITE); }

TEST(whp_gate_cb64_accepted_headless)
{
    REQUIRE(U(waveOutPrepareHeader)); REQUIRE(U(waveOutUnprepareHeader)); REQUIRE(U(waveOutWrite));
    static union { WAVEHDR h; char raw[64]; } b;
    char buf[8] = {0};
    whp_hdr_init(&b.h, WHDR_PREPARED, buf, sizeof buf);
    EXPECT_EQ(U(waveOutPrepareHeader)(NULL, &b.h, 64), MMSYSERR_NOERROR);
    whp_hdr_init(&b.h, 0, buf, sizeof buf);
    EXPECT_EQ(U(waveOutUnprepareHeader)(NULL, &b.h, 64), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveOutWrite)(NULL, &b.h, 64), WAVERR_UNPREPARED);
    EXPECT_EQ(U(waveOutPrepareHeader)(NULL, &b.h, 64), MMSYSERR_INVALHANDLE);
}

/* ---- dwFlags bits ---------------------------------------------------------------------------------------------- */

/* Every single bit: those in 0xFFFFCFE0 -> 11; the others pass the gate. */
static void single_bit_case(int fn)
{
    whp_hdr_fn f = whp_fn(fn);
    REQUIRE(f);
    char buf[16] = {0};
    for (int bit = 0; bit < 32; bit++) {
        DWORD fl = 1u << bit;
        for (int i = 0; i < WHP_NBAD; i++) {
            WAVEHDR h, ref; whp_hdr_init(&h, fl, buf, sizeof buf); ref = h;
            MMRESULT r = f((HWAVEOUT)whp_bad_handles[i], &h, sizeof h);
            MMRESULT want = (fl & WHP_GATE_MASK) ? MMSYSERR_INVALPARAM : model_bad_handle(fn, fl);
            CHECKF(r == want, "%s bit %d (0x%lx) h=%p -> %u want %u", whp_fn_name[fn], bit, fl,
                   (void *)whp_bad_handles[i], r, want);
            CHECKF(!memcmp(&h, &ref, sizeof h), "%s bit %d changed the header", whp_fn_name[fn], bit);
        }
    }
}
TEST(whp_gate_single_bits_prepare)   { single_bit_case(WHP_PREP); }
TEST(whp_gate_single_bits_unprepare) { single_bit_case(WHP_UNPREP); }
TEST(whp_gate_single_bits_write)     { single_bit_case(WHP_WRITE); }

/* A bad bit wins over every state bit: 25 bad bits x 128 allowed combinations. */
static void bad_bit_with_states_case(int fn)
{
    whp_hdr_fn f = whp_fn(fn);
    REQUIRE(f);
    static const DWORD allowed[] = { WHDR_DONE, WHDR_PREPARED, WHDR_BEGINLOOP, WHDR_ENDLOOP, WHDR_INQUEUE, 0x1000, 0x2000 };
    char buf[16] = {0};
    int bad = 0, n = 0;
    for (int bit = 0; bit < 32; bit++) {
        DWORD b = 1u << bit;
        if (!(b & WHP_GATE_MASK)) continue;
        for (int combo = 0; combo < 128; combo++) {
            DWORD fl = b;
            for (int k = 0; k < 7; k++) if (combo & (1 << k)) fl |= allowed[k];
            WAVEHDR h; whp_hdr_init(&h, fl, buf, sizeof buf);
            MMRESULT r = f(NULL, &h, sizeof h);
            n++;
            if (r != MMSYSERR_INVALPARAM || h.dwFlags != fl) {
                if (bad++ < 8) t_note("%s flags=0x%lx -> %u flags after 0x%lx", whp_fn_name[fn], fl, r, h.dwFlags);
            }
        }
    }
    CHECKF(bad == 0, "%s: %d of %d bad-bit combinations not rejected with 11", whp_fn_name[fn], bad, n);
    EXPECT_EQ(n, 25 * 128);
}
TEST(whp_gate_bad_bit_dominates_prepare)   { bad_bit_with_states_case(WHP_PREP); }
TEST(whp_gate_bad_bit_dominates_unprepare) { bad_bit_with_states_case(WHP_UNPREP); }
TEST(whp_gate_bad_bit_dominates_write)     { bad_bit_with_states_case(WHP_WRITE); }

/* Named examples from the task list: 0x20, 0x100, 0x80000000 and the full masks. */
static void named_bad_flags_case(int fn)
{
    whp_hdr_fn f = whp_fn(fn);
    REQUIRE(f);
    static const DWORD bad[] = { 0x20, 0x40, 0x80, 0x100, 0x200, 0x400, 0x800, 0x4000, 0x8000, 0x10000,
                                 0x80000000u, 0xFFFFFFFFu, 0xFFFFCFE0u, 0x22, 0x120, 0x80000002u, 0x3020, 0x4002 };
    char buf[16] = {0};
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        WAVEHDR h; whp_hdr_init(&h, bad[i], buf, sizeof buf);
        MMRESULT r = f(NULL, &h, sizeof h);
        CHECKF(r == MMSYSERR_INVALPARAM, "%s flags=0x%lx -> %u", whp_fn_name[fn], bad[i], r);
        EXPECT_EQ(h.dwFlags, bad[i]);
    }
}
TEST(whp_gate_named_bad_flags_prepare)   { named_bad_flags_case(WHP_PREP); }
TEST(whp_gate_named_bad_flags_unprepare) { named_bad_flags_case(WHP_UNPREP); }
TEST(whp_gate_named_bad_flags_write)     { named_bad_flags_case(WHP_WRITE); }

/* 0x1000 and 0x2000 are not part of the gate mask (legacy internal bits) and pass. */
TEST(whp_gate_internal_bits_pass)
{
    REQUIRE(U(waveOutPrepareHeader)); REQUIRE(U(waveOutUnprepareHeader)); REQUIRE(U(waveOutWrite));
    char buf[8] = {0}; WAVEHDR h;
    static const DWORD in[] = { 0x1000, 0x2000, 0x3000 };
    for (int i = 0; i < 3; i++) {
        whp_hdr_init(&h, in[i] | WHDR_PREPARED, buf, sizeof buf);
        EXPECT_EQ(U(waveOutPrepareHeader)(NULL, &h, sizeof h), MMSYSERR_NOERROR);
        EXPECT_EQ(h.dwFlags, in[i] | WHDR_PREPARED);
        whp_hdr_init(&h, in[i], buf, sizeof buf);
        EXPECT_EQ(U(waveOutPrepareHeader)(NULL, &h, sizeof h), MMSYSERR_INVALHANDLE);
        EXPECT_EQ(U(waveOutUnprepareHeader)(NULL, &h, sizeof h), MMSYSERR_NOERROR);
        EXPECT_EQ(U(waveOutWrite)(NULL, &h, sizeof h), WAVERR_UNPREPARED);
        EXPECT_EQ(h.dwFlags, in[i]);
        whp_hdr_init(&h, in[i] | WHDR_PREPARED | WHDR_INQUEUE, buf, sizeof buf);
        EXPECT_EQ(U(waveOutUnprepareHeader)(NULL, &h, sizeof h), WAVERR_STILLPLAYING);
        EXPECT_EQ(U(waveOutWrite)(NULL, &h, sizeof h), WAVERR_STILLPLAYING);
    }
}

/* ---- header payload is never examined by the gate ------------------------------------------------------------- */

static void payload_ignored_case(int fn)
{
    whp_hdr_fn f = whp_fn(fn);
    REQUIRE(f);
    /* lpData NULL / zero length / odd lengths: the gate and the headless ladder only read dwFlags. */
    static const DWORD lens[] = { 0, 1, 3, 0x7FFFFFFF, 0xFFFFFFFFu };
    for (unsigned s = 0; s < N_STATES; s++)
        for (unsigned l = 0; l < sizeof lens / sizeof lens[0]; l++) {
            WAVEHDR h, ref; whp_hdr_init(&h, g_states[s], NULL, lens[l]);
            h.dwLoops = 0xFFFFFFFFu; h.lpNext = NULL; h.reserved = (DWORD_PTR)-1;
            ref = h;
            MMRESULT r = f(NULL, &h, sizeof h);
            MMRESULT want = model_bad_handle(fn, g_states[s]);
            CHECKF(r == want, "%s flags=0x%lx len=0x%lx -> %u want %u", whp_fn_name[fn], g_states[s], lens[l], r, want);
            CHECKF(!memcmp(&h, &ref, sizeof h), "%s modified a header it rejected/accepted headless", whp_fn_name[fn]);
        }
}
TEST(whp_gate_payload_ignored_prepare)   { payload_ignored_case(WHP_PREP); }
TEST(whp_gate_payload_ignored_unprepare) { payload_ignored_case(WHP_UNPREP); }
TEST(whp_gate_payload_ignored_write)     { payload_ignored_case(WHP_WRITE); }

/* An unaligned header (inside a byte buffer) is read the same way. */
static void unaligned_case(int fn)
{
    whp_hdr_fn f = whp_fn(fn);
    REQUIRE(f);
    static char raw[sizeof(WAVEHDR) + 16];
    for (int off = 1; off < 8; off++) {
        for (unsigned s = 0; s < N_STATES; s++) {
            memset(raw, 0, sizeof raw);
            WAVEHDR tmp; whp_hdr_init(&tmp, g_states[s], NULL, 0);
            memcpy(raw + off, &tmp, sizeof tmp);
            MMRESULT r = f(NULL, (WAVEHDR *)(raw + off), sizeof tmp);
            CHECKF(r == model_bad_handle(fn, g_states[s]), "%s off=%d flags=0x%lx -> %u", whp_fn_name[fn], off, g_states[s], r);
            CHECKF(!memcmp(raw + off, &tmp, sizeof tmp), "%s off=%d changed the header", whp_fn_name[fn], off);
        }
        memset(raw, 0, sizeof raw);
        WAVEHDR tmp; whp_hdr_init(&tmp, 0x40, NULL, 0);
        memcpy(raw + off, &tmp, sizeof tmp);
        EXPECT_EQ(f(NULL, (WAVEHDR *)(raw + off), sizeof tmp), MMSYSERR_INVALPARAM);
    }
}
TEST(whp_gate_unaligned_prepare)   { unaligned_case(WHP_PREP); }
TEST(whp_gate_unaligned_unprepare) { unaligned_case(WHP_UNPREP); }
TEST(whp_gate_unaligned_write)     { unaligned_case(WHP_WRITE); }

/* Order inside the gate does not matter for the result, but all three predicates reject independently. */
TEST(whp_gate_each_predicate_alone)
{
    REQUIRE(whp_have_all());
    char buf[8] = {0};
    for (int fn = 0; fn < WHP_NFN; fn++) {
        whp_hdr_fn f = whp_fn(fn);
        WAVEHDR h;
        /* only cb bad */
        whp_hdr_init(&h, WHDR_PREPARED, buf, sizeof buf);
        CHECKF(f(NULL, &h, 47) == MMSYSERR_INVALPARAM, "%s cb only", whp_fn_name[fn]);
        /* only flags bad */
        whp_hdr_init(&h, WHDR_PREPARED | 0x20, buf, sizeof buf);
        CHECKF(f(NULL, &h, 48) == MMSYSERR_INVALPARAM, "%s flags only", whp_fn_name[fn]);
        /* only pointer bad */
        CHECKF(f(NULL, NULL, 48) == MMSYSERR_INVALPARAM, "%s pointer only", whp_fn_name[fn]);
        /* all bad */
        CHECKF(f((HWAVEOUT)1, NULL, 0) == MMSYSERR_INVALPARAM, "%s all bad", whp_fn_name[fn]);
    }
}

/* Repeating a rejected call is stable (no hidden state). */
TEST(whp_gate_repeat_stable)
{
    REQUIRE(whp_have_all());
    char buf[8] = {0};
    for (int fn = 0; fn < WHP_NFN; fn++) {
        whp_hdr_fn f = whp_fn(fn);
        WAVEHDR h; whp_hdr_init(&h, 0x80000000u | WHDR_PREPARED, buf, sizeof buf);
        int ok = 1;
        for (int k = 0; k < 200; k++) if (f(NULL, &h, sizeof h) != MMSYSERR_INVALPARAM) ok = 0;
        CHECKF(ok, "%s repeated", whp_fn_name[fn]);
        EXPECT_EQ(h.dwFlags, 0x80000000u | WHDR_PREPARED);
    }
}

/* The gate does not clear or set GetLastError in any way the caller should depend on: just record it. */
TEST(whp_gate_last_error_observed)
{
    REQUIRE(whp_have_all());
    char buf[8] = {0}; WAVEHDR h; whp_hdr_init(&h, 0x20, buf, sizeof buf);
    SetLastError(0xBEEF);
    MMRESULT r = U(waveOutWrite)(NULL, &h, sizeof h);
    EXPECT_EQ(r, MMSYSERR_INVALPARAM);
    t_note("GetLastError after gate rejection = %lu (preset 0xBEEF)", GetLastError());
    whp_hdr_init(&h, 0, buf, sizeof buf);
    SetLastError(0xBEEF);
    r = U(waveOutPrepareHeader)(NULL, &h, sizeof h);
    EXPECT_EQ(r, MMSYSERR_INVALHANDLE);
    t_note("GetLastError after handle rejection = %lu (preset 0xBEEF)", GetLastError());
}
