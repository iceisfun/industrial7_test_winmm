#include "et.h"
/* Literal expectation table (no rule evaluation): one row per code, the result of the wave pair and of the midi pair
 * at cch 1 and at cch 256. Rows come from the winmmbase range checks; code 21 comes from the VM (no resource).
 * Every row is run against all 8 exports in both charsets. VM_TIMEOUT: 60 */

typedef struct { DWORD code; MMRESULT wave1, wave256, midi1, midi256; } row;
#define OK MMSYSERR_NOERROR
#define BE MMSYSERR_BADERRNUM
static const row rows[] = {
    /* MMSYSERR_* 0..20: text in both families */
    {  0, OK, OK, OK, OK }, {  1, OK, OK, OK, OK }, {  2, OK, OK, OK, OK }, {  3, OK, OK, OK, OK },
    {  4, OK, OK, OK, OK }, {  5, OK, OK, OK, OK }, {  6, OK, OK, OK, OK }, {  7, OK, OK, OK, OK },
    {  8, OK, OK, OK, OK }, {  9, OK, OK, OK, OK }, { 10, OK, OK, OK, OK }, { 11, OK, OK, OK, OK },
    { 12, OK, OK, OK, OK }, { 13, OK, OK, OK, OK }, { 14, OK, OK, OK, OK }, { 15, OK, OK, OK, OK },
    { 16, OK, OK, OK, OK }, { 17, OK, OK, OK, OK }, { 18, OK, OK, OK, OK }, { 19, OK, OK, OK, OK },
    { 20, OK, OK, OK, OK },
    /* MMSYSERR_MOREDATA: in range, no resource: success only while LoadString is skipped (cch 1) */
    { 21, OK, BE, OK, BE },
    /* gap below WAVERR_BASE */
    { 22, BE, BE, BE, BE }, { 23, BE, BE, BE, BE }, { 24, BE, BE, BE, BE }, { 25, BE, BE, BE, BE },
    { 26, BE, BE, BE, BE }, { 27, BE, BE, BE, BE }, { 28, BE, BE, BE, BE }, { 29, BE, BE, BE, BE },
    { 30, BE, BE, BE, BE }, { 31, BE, BE, BE, BE },
    /* WAVERR_BADFORMAT..WAVERR_SYNC: wave only */
    { 32, OK, OK, BE, BE }, { 33, OK, OK, BE, BE }, { 34, OK, OK, BE, BE }, { 35, OK, OK, BE, BE },
    { 36, BE, BE, BE, BE }, { 37, BE, BE, BE, BE }, { 47, BE, BE, BE, BE }, { 48, BE, BE, BE, BE },
    { 62, BE, BE, BE, BE }, { 63, BE, BE, BE, BE },
    /* MIDIERR_UNPREPARED..MIDIERR_LASTERROR: midi only */
    { 64, BE, BE, OK, OK }, { 65, BE, BE, OK, OK }, { 66, BE, BE, OK, OK }, { 67, BE, BE, OK, OK },
    { 68, BE, BE, OK, OK }, { 69, BE, BE, OK, OK }, { 70, BE, BE, OK, OK }, { 71, BE, BE, OK, OK },
    { 72, BE, BE, BE, BE }, { 73, BE, BE, BE, BE }, { 80, BE, BE, BE, BE },
    /* TIMERR / JOYERR / MIXERR / MCIERR / MMIOERR bases */
    { 96, BE, BE, BE, BE }, { 97, BE, BE, BE, BE }, { 128, BE, BE, BE, BE }, { 129, BE, BE, BE, BE },
    { 160, BE, BE, BE, BE }, { 165, BE, BE, BE, BE }, { 167, BE, BE, BE, BE }, { 256, BE, BE, BE, BE },
    { 257, BE, BE, BE, BE }, { 266, BE, BE, BE, BE }, { 512, BE, BE, BE, BE }, { 1024, BE, BE, BE, BE },
    { 1025, BE, BE, BE, BE }, { 1026, BE, BE, BE, BE },
    /* full 32-bit compare */
    { 0xFFFF, BE, BE, BE, BE }, { 0x10000, BE, BE, BE, BE }, { 0x10020, BE, BE, BE, BE }, { 0x10040, BE, BE, BE, BE },
    { 0x7FFFFFFF, BE, BE, BE, BE }, { 0x80000000, BE, BE, BE, BE }, { 0xFFFFFFE0, BE, BE, BE, BE },
    { 0xFFFFFFEA, BE, BE, BE, BE }, { 0xFFFFFFFF, BE, BE, BE, BE },
};
#undef OK
#undef BE

static void run_table(int i, UINT cch) {
    const et_api *a = &et_apis[i];
    if (!a->resolve()) { t_skip("missing export: %s", a->name); return; }
    for (size_t k = 0; k < ET_COUNT(rows); k++) {
        const row *r = &rows[k];
        MMRESULT want = a->family == ET_WAVE ? (cch == 1 ? r->wave1 : r->wave256) : (cch == 1 ? r->midi1 : r->midi256);
        unsigned char buf[MAXERRORLENGTH * 2];
        memset(buf, ET_FILL, sizeof buf);
        MMRESULT got = a->wide ? et_call_w(a, r->code, (WCHAR *)buf, cch) : et_call_a(a, r->code, (char *)buf, cch);
        CHECKF(got == want, "%s(0x%lx, cch %u) = %u want %u", a->name, r->code, cch, got, want);
        int empty = a->wide ? ((WCHAR *)buf)[0] == 0 : buf[0] == 0;
        if (want != MMSYSERR_NOERROR || cch == 1) CHECKF(empty, "%s(0x%lx, cch %u) not empty", a->name, r->code, cch);
        else CHECKF(!empty, "%s(0x%lx, cch %u) empty", a->name, r->code, cch);
        /* the rule-based helper agrees with the literal row */
        CHECK(et_expect(a->family, r->code, cch) == want);
    }
}

#define ET_TABLE(tag, i) \
    TEST(mme_error_text_table_##tag##_cch1)   { run_table(i, 1); } \
    TEST(mme_error_text_table_##tag##_cch256) { run_table(i, MAXERRORLENGTH); }
ET_TABLE(waveouta, 0)
ET_TABLE(waveoutw, 1)
ET_TABLE(waveina, 2)
ET_TABLE(waveinw, 3)
ET_TABLE(midiouta, 4)
ET_TABLE(midioutw, 5)
ET_TABLE(midiina, 6)
ET_TABLE(midiinw, 7)

/* Range boundaries one by one, direct U() calls (one test per boundary, both charsets, Out and In). */
#define BOUND_W(fn, code, want) do { WCHAR b_[MAXERRORLENGTH]; b_[0] = 'x'; \
    MMRESULT r_ = U(fn)(code, b_, MAXERRORLENGTH); CHECKF(r_ == (want), #fn "(%u) = %u", (unsigned)(code), r_); \
    CHECKF((r_ == 0) == (b_[0] != 0), #fn "(%u) text/result mismatch", (unsigned)(code)); } while (0)
#define BOUND_A(fn, code, want) do { char b_[MAXERRORLENGTH]; b_[0] = 'x'; \
    MMRESULT r_ = U(fn)(code, b_, MAXERRORLENGTH); CHECKF(r_ == (want), #fn "(%u) = %u", (unsigned)(code), r_); \
    CHECKF((r_ == 0) == (b_[0] != 0), #fn "(%u) text/result mismatch", (unsigned)(code)); } while (0)

TEST(mme_error_text_bound_wave_20_21_22) {
    REQUIRE(U(waveOutGetErrorTextW)); REQUIRE(U(waveOutGetErrorTextA)); REQUIRE(U(waveInGetErrorTextW)); REQUIRE(U(waveInGetErrorTextA));
    BOUND_W(waveOutGetErrorTextW, 20, 0); BOUND_W(waveOutGetErrorTextW, 21, 9); BOUND_W(waveOutGetErrorTextW, 22, 9);
    BOUND_A(waveOutGetErrorTextA, 20, 0); BOUND_A(waveOutGetErrorTextA, 21, 9); BOUND_A(waveOutGetErrorTextA, 22, 9);
    BOUND_W(waveInGetErrorTextW, 20, 0);  BOUND_W(waveInGetErrorTextW, 21, 9);  BOUND_W(waveInGetErrorTextW, 22, 9);
    BOUND_A(waveInGetErrorTextA, 20, 0);  BOUND_A(waveInGetErrorTextA, 21, 9);  BOUND_A(waveInGetErrorTextA, 22, 9);
}
TEST(mme_error_text_bound_wave_31_32) {
    REQUIRE(U(waveOutGetErrorTextW)); REQUIRE(U(waveOutGetErrorTextA)); REQUIRE(U(waveInGetErrorTextW)); REQUIRE(U(waveInGetErrorTextA));
    BOUND_W(waveOutGetErrorTextW, 31, 9); BOUND_W(waveOutGetErrorTextW, 32, 0);
    BOUND_A(waveOutGetErrorTextA, 31, 9); BOUND_A(waveOutGetErrorTextA, 32, 0);
    BOUND_W(waveInGetErrorTextW, 31, 9);  BOUND_W(waveInGetErrorTextW, 32, 0);
    BOUND_A(waveInGetErrorTextA, 31, 9);  BOUND_A(waveInGetErrorTextA, 32, 0);
}
TEST(mme_error_text_bound_wave_35_36) {
    REQUIRE(U(waveOutGetErrorTextW)); REQUIRE(U(waveOutGetErrorTextA)); REQUIRE(U(waveInGetErrorTextW)); REQUIRE(U(waveInGetErrorTextA));
    BOUND_W(waveOutGetErrorTextW, 35, 0); BOUND_W(waveOutGetErrorTextW, 36, 9);
    BOUND_A(waveOutGetErrorTextA, 35, 0); BOUND_A(waveOutGetErrorTextA, 36, 9);
    BOUND_W(waveInGetErrorTextW, 35, 0);  BOUND_W(waveInGetErrorTextW, 36, 9);
    BOUND_A(waveInGetErrorTextA, 35, 0);  BOUND_A(waveInGetErrorTextA, 36, 9);
}
TEST(mme_error_text_bound_wave_63_64_71_72) {
    REQUIRE(U(waveOutGetErrorTextW)); REQUIRE(U(waveOutGetErrorTextA)); REQUIRE(U(waveInGetErrorTextW)); REQUIRE(U(waveInGetErrorTextA));
    BOUND_W(waveOutGetErrorTextW, 63, 9); BOUND_W(waveOutGetErrorTextW, 64, 9); BOUND_W(waveOutGetErrorTextW, 71, 9); BOUND_W(waveOutGetErrorTextW, 72, 9);
    BOUND_A(waveOutGetErrorTextA, 63, 9); BOUND_A(waveOutGetErrorTextA, 64, 9); BOUND_A(waveOutGetErrorTextA, 71, 9); BOUND_A(waveOutGetErrorTextA, 72, 9);
    BOUND_W(waveInGetErrorTextW, 64, 9);  BOUND_A(waveInGetErrorTextA, 71, 9);
}
TEST(mme_error_text_bound_midi_20_21_22) {
    REQUIRE(U(midiOutGetErrorTextW)); REQUIRE(U(midiOutGetErrorTextA)); REQUIRE(U(midiInGetErrorTextW)); REQUIRE(U(midiInGetErrorTextA));
    BOUND_W(midiOutGetErrorTextW, 20, 0); BOUND_W(midiOutGetErrorTextW, 21, 9); BOUND_W(midiOutGetErrorTextW, 22, 9);
    BOUND_A(midiOutGetErrorTextA, 20, 0); BOUND_A(midiOutGetErrorTextA, 21, 9); BOUND_A(midiOutGetErrorTextA, 22, 9);
    BOUND_W(midiInGetErrorTextW, 20, 0);  BOUND_W(midiInGetErrorTextW, 21, 9);  BOUND_W(midiInGetErrorTextW, 22, 9);
    BOUND_A(midiInGetErrorTextA, 20, 0);  BOUND_A(midiInGetErrorTextA, 21, 9);  BOUND_A(midiInGetErrorTextA, 22, 9);
}
TEST(mme_error_text_bound_midi_32_35) {
    REQUIRE(U(midiOutGetErrorTextW)); REQUIRE(U(midiOutGetErrorTextA)); REQUIRE(U(midiInGetErrorTextW)); REQUIRE(U(midiInGetErrorTextA));
    BOUND_W(midiOutGetErrorTextW, 32, 9); BOUND_W(midiOutGetErrorTextW, 35, 9);
    BOUND_A(midiOutGetErrorTextA, 32, 9); BOUND_A(midiOutGetErrorTextA, 35, 9);
    BOUND_W(midiInGetErrorTextW, 33, 9);  BOUND_A(midiInGetErrorTextA, 34, 9);
}
TEST(mme_error_text_bound_midi_63_64) {
    REQUIRE(U(midiOutGetErrorTextW)); REQUIRE(U(midiOutGetErrorTextA)); REQUIRE(U(midiInGetErrorTextW)); REQUIRE(U(midiInGetErrorTextA));
    BOUND_W(midiOutGetErrorTextW, 63, 9); BOUND_W(midiOutGetErrorTextW, 64, 0);
    BOUND_A(midiOutGetErrorTextA, 63, 9); BOUND_A(midiOutGetErrorTextA, 64, 0);
    BOUND_W(midiInGetErrorTextW, 63, 9);  BOUND_W(midiInGetErrorTextW, 64, 0);
    BOUND_A(midiInGetErrorTextA, 63, 9);  BOUND_A(midiInGetErrorTextA, 64, 0);
}
TEST(mme_error_text_bound_midi_71_72) {
    REQUIRE(U(midiOutGetErrorTextW)); REQUIRE(U(midiOutGetErrorTextA)); REQUIRE(U(midiInGetErrorTextW)); REQUIRE(U(midiInGetErrorTextA));
    BOUND_W(midiOutGetErrorTextW, 71, 0); BOUND_W(midiOutGetErrorTextW, 72, 9);
    BOUND_A(midiOutGetErrorTextA, 71, 0); BOUND_A(midiOutGetErrorTextA, 72, 9);
    BOUND_W(midiInGetErrorTextW, 71, 0);  BOUND_W(midiInGetErrorTextW, 72, 9);
    BOUND_A(midiInGetErrorTextA, 71, 0);  BOUND_A(midiInGetErrorTextA, 72, 9);
}

/* Gate order on direct calls: cch 0 beats a NULL pointer, NULL beats a bad code. */
TEST(mme_error_text_gate_order_direct) {
    ET_REQUIRE_ALL();
    CHECK(U(waveOutGetErrorTextA)(0xFFFF, NULL, 0) == MMSYSERR_NOERROR);
    CHECK(U(waveOutGetErrorTextW)(0xFFFF, NULL, 0) == MMSYSERR_NOERROR);
    CHECK(U(waveInGetErrorTextA)(0xFFFF, NULL, 0) == MMSYSERR_NOERROR);
    CHECK(U(waveInGetErrorTextW)(0xFFFF, NULL, 0) == MMSYSERR_NOERROR);
    CHECK(U(midiOutGetErrorTextA)(0xFFFF, NULL, 0) == MMSYSERR_NOERROR);
    CHECK(U(midiOutGetErrorTextW)(0xFFFF, NULL, 0) == MMSYSERR_NOERROR);
    CHECK(U(midiInGetErrorTextA)(0xFFFF, NULL, 0) == MMSYSERR_NOERROR);
    CHECK(U(midiInGetErrorTextW)(0xFFFF, NULL, 0) == MMSYSERR_NOERROR);
    CHECK(U(waveOutGetErrorTextA)(0xFFFF, NULL, 1) == MMSYSERR_INVALPARAM);
    CHECK(U(waveOutGetErrorTextW)(0xFFFF, NULL, 1) == MMSYSERR_INVALPARAM);
    CHECK(U(waveInGetErrorTextA)(0xFFFF, NULL, 1) == MMSYSERR_INVALPARAM);
    CHECK(U(waveInGetErrorTextW)(0xFFFF, NULL, 1) == MMSYSERR_INVALPARAM);
    CHECK(U(midiOutGetErrorTextA)(0xFFFF, NULL, 1) == MMSYSERR_INVALPARAM);
    CHECK(U(midiOutGetErrorTextW)(0xFFFF, NULL, 1) == MMSYSERR_INVALPARAM);
    CHECK(U(midiInGetErrorTextA)(0xFFFF, NULL, 1) == MMSYSERR_INVALPARAM);
    CHECK(U(midiInGetErrorTextW)(0xFFFF, NULL, 1) == MMSYSERR_INVALPARAM);
}

/* Every W text is non-empty; control characters or a leading space are recorded (resource content, not a contract). */
TEST(mme_error_text_text_shape) {
    REQUIRE(U(waveOutGetErrorTextW)); REQUIRE(U(midiOutGetErrorTextW));
    for (int f = 0; f < 2; f++) {
        const DWORD *codes = f ? et_midi_text_codes : et_wave_text_codes;
        size_t n = f ? ET_COUNT(et_midi_text_codes) : ET_COUNT(et_wave_text_codes);
        for (size_t k = 0; k < n; k++) {
            WCHAR w[MAXERRORLENGTH]; w[0] = 0;
            (f ? U(midiOutGetErrorTextW) : U(waveOutGetErrorTextW))(codes[k], w, MAXERRORLENGTH);
            int ctl = 0, lead_space = w[0] == L' ';
            for (int j = 0; w[j]; j++) if (w[j] < 0x20) ctl++;
            if (ctl || lead_space) t_note("%s code %lu: %d control chars, leading space %d", f ? "midi" : "wave", codes[k], ctl, lead_space);
            CHECK(w[0] != 0);
        }
    }
}
