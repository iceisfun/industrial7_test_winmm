#include "et.h"
/* Cross-export properties: Out/In identity (shared bodies), A vs W (LoadStringA is the ANSI conversion of the
 * same resource), wave vs midi on the shared MMSYSERR codes (same LoadString id), absent error-text exports of
 * other families, threads, and notes of the English text. VM_TIMEOUT: 120 */

static void sweep_pair_a(et_a_fn f1, et_a_fn f2, const char *n1, const char *n2) {
    static const UINT cchs[] = { 0, 1, 2, 3, 5, 9, 33, MAXERRORLENGTH };
    char b1[MAXERRORLENGTH + 16], b2[MAXERRORLENGTH + 16];
    int diffs = 0;
    for (DWORD c = 0; c <= 300; c++)
        for (size_t z = 0; z < ET_COUNT(cchs); z++) {
            memset(b1, ET_FILL, sizeof b1); memset(b2, ET_FILL, sizeof b2);
            MMRESULT r1 = f1(c, b1, cchs[z]), r2 = f2(c, b2, cchs[z]);
            if (r1 != r2 || memcmp(b1, b2, sizeof b1)) {
                diffs++;
                CHECKF(0, "%s vs %s differ at code %lu cch %u (%u/%u)", n1, n2, c, cchs[z], r1, r2);
            }
        }
    CHECKF(diffs == 0, "%d differences", diffs);
}
static void sweep_pair_w(et_w_fn f1, et_w_fn f2, const char *n1, const char *n2) {
    static const UINT cchs[] = { 0, 1, 2, 3, 5, 9, 33, MAXERRORLENGTH };
    WCHAR b1[MAXERRORLENGTH + 16], b2[MAXERRORLENGTH + 16];
    int diffs = 0;
    for (DWORD c = 0; c <= 300; c++)
        for (size_t z = 0; z < ET_COUNT(cchs); z++) {
            memset(b1, ET_FILL, sizeof b1); memset(b2, ET_FILL, sizeof b2);
            MMRESULT r1 = f1(c, b1, cchs[z]), r2 = f2(c, b2, cchs[z]);
            if (r1 != r2 || memcmp(b1, b2, sizeof b1)) {
                diffs++;
                CHECKF(0, "%s vs %s differ at code %lu cch %u (%u/%u)", n1, n2, c, cchs[z], r1, r2);
            }
        }
    CHECKF(diffs == 0, "%d differences", diffs);
}

/* Out and In variants share one body: identical result and identical buffer bytes for every code and size. */
TEST(mme_error_text_parity_waveout_wavein_a) {
    REQUIRE(U(waveOutGetErrorTextA)); REQUIRE(U(waveInGetErrorTextA));
    sweep_pair_a(U(waveOutGetErrorTextA), U(waveInGetErrorTextA), "waveOutA", "waveInA");
}
TEST(mme_error_text_parity_waveout_wavein_w) {
    REQUIRE(U(waveOutGetErrorTextW)); REQUIRE(U(waveInGetErrorTextW));
    sweep_pair_w(U(waveOutGetErrorTextW), U(waveInGetErrorTextW), "waveOutW", "waveInW");
}
TEST(mme_error_text_parity_midiout_midiin_a) {
    REQUIRE(U(midiOutGetErrorTextA)); REQUIRE(U(midiInGetErrorTextA));
    sweep_pair_a(U(midiOutGetErrorTextA), U(midiInGetErrorTextA), "midiOutA", "midiInA");
}
TEST(mme_error_text_parity_midiout_midiin_w) {
    REQUIRE(U(midiOutGetErrorTextW)); REQUIRE(U(midiInGetErrorTextW));
    sweep_pair_w(U(midiOutGetErrorTextW), U(midiInGetErrorTextW), "midiOutW", "midiInW");
}

/* A text == WideCharToMultiByte(CP_ACP) of the W text, for every code with text. */
static void a_from_w(et_a_fn fa, et_w_fn fw, const DWORD *codes, size_t n, const char *name) {
    for (size_t k = 0; k < n; k++) {
        WCHAR w[MAXERRORLENGTH]; char a[MAXERRORLENGTH], conv[MAXERRORLENGTH * 2];
        memset(a, 0, sizeof a); memset(w, 0, sizeof w);
        CHECK(fw(codes[k], w, MAXERRORLENGTH) == MMSYSERR_NOERROR);
        CHECK(fa(codes[k], a, MAXERRORLENGTH) == MMSYSERR_NOERROR);
        int cl = WideCharToMultiByte(CP_ACP, 0, w, -1, conv, sizeof conv, NULL, NULL);
        CHECKF(cl > 0 && !strcmp(conv, a), "%s(%lu): A \"%s\" vs converted W \"%s\"", name, codes[k], a, conv);
    }
}
TEST(mme_error_text_parity_waveout_a_is_acp_of_w) {
    REQUIRE(U(waveOutGetErrorTextA)); REQUIRE(U(waveOutGetErrorTextW));
    a_from_w(U(waveOutGetErrorTextA), U(waveOutGetErrorTextW), et_wave_text_codes, ET_COUNT(et_wave_text_codes), "waveOut");
}
TEST(mme_error_text_parity_wavein_a_is_acp_of_w) {
    REQUIRE(U(waveInGetErrorTextA)); REQUIRE(U(waveInGetErrorTextW));
    a_from_w(U(waveInGetErrorTextA), U(waveInGetErrorTextW), et_wave_text_codes, ET_COUNT(et_wave_text_codes), "waveIn");
}
TEST(mme_error_text_parity_midiout_a_is_acp_of_w) {
    REQUIRE(U(midiOutGetErrorTextA)); REQUIRE(U(midiOutGetErrorTextW));
    a_from_w(U(midiOutGetErrorTextA), U(midiOutGetErrorTextW), et_midi_text_codes, ET_COUNT(et_midi_text_codes), "midiOut");
}
TEST(mme_error_text_parity_midiin_a_is_acp_of_w) {
    REQUIRE(U(midiInGetErrorTextA)); REQUIRE(U(midiInGetErrorTextW));
    a_from_w(U(midiInGetErrorTextA), U(midiInGetErrorTextW), et_midi_text_codes, ET_COUNT(et_midi_text_codes), "midiIn");
}

/* A and W agree on the result code for every code and size (same range rule, same resource presence). */
TEST(mme_error_text_parity_a_w_result_codes) {
    ET_REQUIRE_ALL();
    static const UINT cchs[] = { 0, 1, 2, 4, MAXERRORLENGTH };
    for (int p = 0; p < ET_NAPI; p += 2) {
        const et_api *A = &et_apis[p], *W = &et_apis[p + 1];
        for (DWORD c = 0; c <= 300; c++)
            for (size_t z = 0; z < ET_COUNT(cchs); z++) {
                char a[MAXERRORLENGTH]; WCHAR w[MAXERRORLENGTH];
                MMRESULT ra = et_call_a(A, c, a, cchs[z]), rw = et_call_w(W, c, w, cchs[z]);
                CHECKF(ra == rw, "%s/%s(%lu, %u): %u vs %u", A->name, W->name, c, cchs[z], ra, rw);
            }
    }
}

/* Codes 0..20 come from the same string ids in both families: the wave and midi texts are equal. */
TEST(mme_error_text_parity_wave_midi_shared_w) {
    REQUIRE(U(waveOutGetErrorTextW)); REQUIRE(U(midiOutGetErrorTextW));
    for (DWORD c = 0; c <= 20; c++) {
        WCHAR a[MAXERRORLENGTH], b[MAXERRORLENGTH];
        CHECK(U(waveOutGetErrorTextW)(c, a, MAXERRORLENGTH) == MMSYSERR_NOERROR);
        CHECK(U(midiOutGetErrorTextW)(c, b, MAXERRORLENGTH) == MMSYSERR_NOERROR);
        CHECKF(!lstrcmpW(a, b), "code %lu: wave and midi text differ", c);
    }
}
TEST(mme_error_text_parity_wave_midi_shared_a) {
    REQUIRE(U(waveInGetErrorTextA)); REQUIRE(U(midiInGetErrorTextA));
    for (DWORD c = 0; c <= 20; c++) {
        char a[MAXERRORLENGTH], b[MAXERRORLENGTH];
        CHECK(U(waveInGetErrorTextA)(c, a, MAXERRORLENGTH) == MMSYSERR_NOERROR);
        CHECK(U(midiInGetErrorTextA)(c, b, MAXERRORLENGTH) == MMSYSERR_NOERROR);
        CHECKF(!strcmp(a, b), "code %lu: wave and midi text differ", c);
    }
}

/* Every text in a family is distinct (each code has its own message). */
static void distinct_w(et_w_fn f, const DWORD *codes, size_t n, const char *name) {
    static WCHAR t[40][MAXERRORLENGTH];
    for (size_t k = 0; k < n; k++) { t[k][0] = 0; f(codes[k], t[k], MAXERRORLENGTH); }
    int dup = 0;
    for (size_t x = 0; x < n; x++) for (size_t y = x + 1; y < n; y++) if (!lstrcmpW(t[x], t[y])) { dup++; t_note("%s: codes %lu and %lu share text", name, codes[x], codes[y]); }
    if (dup) t_note("%s: %d duplicate pairs", name, dup);
    CHECK(n <= 40);
}
TEST(mme_error_text_distinct_texts_note) {
    REQUIRE(U(waveOutGetErrorTextW)); REQUIRE(U(midiOutGetErrorTextW));
    distinct_w(U(waveOutGetErrorTextW), et_wave_text_codes, ET_COUNT(et_wave_text_codes), "wave");
    distinct_w(U(midiOutGetErrorTextW), et_midi_text_codes, ET_COUNT(et_midi_text_codes), "midi");
}

/* The family-specific ranges: WAVERR_BADFORMAT..WAVERR_SYNC (32..35) and MIDIERR_UNPREPARED..MIDIERR_LASTERROR
 * (64..71) from mmsystem.h match the bodies' ranges exactly. */
TEST(mme_error_text_header_constants) {
    CHECK(WAVERR_BASE == 32); CHECK(WAVERR_BADFORMAT == 32); CHECK(WAVERR_SYNC == 35); CHECK(WAVERR_LASTERROR == 35);
    CHECK(MIDIERR_BASE == 64); CHECK(MIDIERR_UNPREPARED == 64); CHECK(MIDIERR_LASTERROR == 71);
    CHECK(MMSYSERR_BADERRNUM == 9); CHECK(MMSYSERR_INVALPARAM == 11); CHECK(MMSYSERR_MOREDATA == 21);
    CHECK(MAXERRORLENGTH == 256);
}

/* Specific named codes, W via waveOut and midiOut. */
TEST(mme_error_text_named_wave_codes) {
    REQUIRE(U(waveOutGetErrorTextW));
    static const DWORD ok[] = { MMSYSERR_NOERROR, MMSYSERR_ERROR, MMSYSERR_BADDEVICEID, MMSYSERR_NOTENABLED,
        MMSYSERR_ALLOCATED, MMSYSERR_INVALHANDLE, MMSYSERR_NODRIVER, MMSYSERR_NOMEM, MMSYSERR_NOTSUPPORTED,
        MMSYSERR_BADERRNUM, MMSYSERR_INVALFLAG, MMSYSERR_INVALPARAM, MMSYSERR_HANDLEBUSY, MMSYSERR_INVALIDALIAS,
        MMSYSERR_BADDB, MMSYSERR_KEYNOTFOUND, MMSYSERR_READERROR, MMSYSERR_WRITEERROR, MMSYSERR_DELETEERROR,
        MMSYSERR_VALNOTFOUND, MMSYSERR_NODRIVERCB, WAVERR_BADFORMAT, WAVERR_STILLPLAYING, WAVERR_UNPREPARED, WAVERR_SYNC };
    WCHAR b[MAXERRORLENGTH];
    for (size_t k = 0; k < ET_COUNT(ok); k++) CHECKF(U(waveOutGetErrorTextW)(ok[k], b, MAXERRORLENGTH) == 0 && b[0], "code %lu", ok[k]);
    static const DWORD bad[] = { MMSYSERR_MOREDATA, MIDIERR_UNPREPARED, MIDIERR_LASTERROR, MIXERR_INVALLINE, MIXERR_INVALCONTROL,
        MIXERR_INVALVALUE, TIMERR_NOCANDO, TIMERR_STRUCT, JOYERR_PARMS, JOYERR_NOCANDO, JOYERR_UNPLUGGED, MCIERR_INVALID_DEVICE_ID,
        MMIOERR_FILENOTFOUND };
    for (size_t k = 0; k < ET_COUNT(bad); k++) {
        b[0] = 'x';
        CHECKF(U(waveOutGetErrorTextW)(bad[k], b, MAXERRORLENGTH) == MMSYSERR_BADERRNUM && !b[0], "code %lu", bad[k]);
    }
}
TEST(mme_error_text_named_midi_codes) {
    REQUIRE(U(midiOutGetErrorTextW));
    static const DWORD ok[] = { MMSYSERR_NOERROR, MMSYSERR_BADDEVICEID, MMSYSERR_ALLOCATED, MMSYSERR_INVALHANDLE,
        MMSYSERR_NODRIVERCB, MIDIERR_UNPREPARED, MIDIERR_STILLPLAYING, MIDIERR_NOMAP, MIDIERR_NOTREADY,
        MIDIERR_NODEVICE, MIDIERR_INVALIDSETUP, MIDIERR_BADOPENMODE, MIDIERR_DONT_CONTINUE };
    WCHAR b[MAXERRORLENGTH];
    for (size_t k = 0; k < ET_COUNT(ok); k++) CHECKF(U(midiOutGetErrorTextW)(ok[k], b, MAXERRORLENGTH) == 0 && b[0], "code %lu", ok[k]);
    static const DWORD bad[] = { MMSYSERR_MOREDATA, WAVERR_BADFORMAT, WAVERR_SYNC, MIXERR_INVALLINE, MIXERR_INVALVALUE,
        TIMERR_NOCANDO, JOYERR_PARMS, MCIERR_INVALID_DEVICE_ID, MMIOERR_FILENOTFOUND };
    for (size_t k = 0; k < ET_COUNT(bad); k++) {
        b[0] = 'x';
        CHECKF(U(midiOutGetErrorTextW)(bad[k], b, MAXERRORLENGTH) == MMSYSERR_BADERRNUM && !b[0], "code %lu", bad[k]);
    }
}

/* Mixer, aux, joystick and time have no GetErrorText export, in winmm or winmmbase. */
TEST(mme_error_text_no_mixer_error_text) {
    HMODULE w = t_module("winmm.dll");
    REQUIRE(w);
    SetLastError(0);
    CHECK(GetProcAddress(w, "mixerGetErrorTextA") == NULL);
    EXPECT_ERR(ERROR_PROC_NOT_FOUND);
    CHECK(GetProcAddress(w, "mixerGetErrorTextW") == NULL);
    CHECK(GetProcAddress(w, "mixerGetErrorText") == NULL);
    HMODULE b = t_module("winmmbase.dll");
    if (b) { CHECK(GetProcAddress(b, "mixerGetErrorTextA") == NULL); CHECK(GetProcAddress(b, "mixerGetErrorTextW") == NULL); }
}
TEST(mme_error_text_no_other_family_error_text) {
    HMODULE w = t_module("winmm.dll");
    REQUIRE(w);
    static const char *names[] = { "auxGetErrorTextA", "auxGetErrorTextW", "joyGetErrorTextA", "joyGetErrorTextW",
        "timeGetErrorTextA", "midiStreamGetErrorTextA", "waveOutGetErrorText", "waveInGetErrorText",
        "midiOutGetErrorText", "midiInGetErrorText", "waveGetErrorTextW", "midiGetErrorTextW" };
    for (size_t k = 0; k < ET_COUNT(names); k++) CHECKF(GetProcAddress(w, names[k]) == NULL, "winmm exports %s", names[k]);
}

/* All 8 exports exist in winmm.dll by name. */
TEST(mme_error_text_exports_present) {
    for (int i = 0; i < ET_NAPI; i++) {
        CHECKF(t_proc_in("winmm.dll", et_apis[i].name) != NULL, "winmm lacks %s", et_apis[i].name);
        CHECKF(et_apis[i].resolve() != NULL, "U(%s) did not resolve", et_apis[i].name);
    }
}

/* Export layout record (not a contract): winmm has one stub per export; winmmbase aliases Out/In onto one body. */
TEST(mme_error_text_export_layout_note) {
    for (int i = 0; i < ET_NAPI; i += 1) {
        FARPROC w = t_proc_in("winmm.dll", et_apis[i].name), b = t_proc_in("winmmbase.dll", et_apis[i].name);
        t_note("%s winmm=%p winmmbase=%p", et_apis[i].name, w, b);
    }
    static const int pairs[][2] = { {0, 2}, {1, 3}, {4, 6}, {5, 7} };
    for (int p = 0; p < 4; p++) {
        FARPROC x = t_proc_in("winmmbase.dll", et_apis[pairs[p][0]].name), y = t_proc_in("winmmbase.dll", et_apis[pairs[p][1]].name);
        t_note("winmmbase %s %s %s", et_apis[pairs[p][0]].name, x == y ? "==" : "!=", et_apis[pairs[p][1]].name);
    }
}

/* English text record of 0, 2, 32 and 64 (locale dependent, never checked), plus every text for the evidence log. */
static void note_w(const char *fam, et_w_fn f, DWORD c) {
    WCHAR w[MAXERRORLENGTH]; char a[MAXERRORLENGTH * 2];
    w[0] = 0;
    MMRESULT r = f(c, w, MAXERRORLENGTH);
    WideCharToMultiByte(CP_UTF8, 0, w, -1, a, sizeof a, NULL, NULL);
    t_note("%s %lu -> %u \"%s\"", fam, c, r, a);
}
TEST(mme_error_text_note_key_texts) {
    REQUIRE(U(waveOutGetErrorTextW)); REQUIRE(U(midiOutGetErrorTextW));
    t_note("user default UI language 0x%04x, ACP %u", GetUserDefaultUILanguage(), GetACP());
    note_w("wave", U(waveOutGetErrorTextW), 0);
    note_w("wave", U(waveOutGetErrorTextW), 2);
    note_w("wave", U(waveOutGetErrorTextW), 32);
    note_w("midi", U(midiOutGetErrorTextW), 64);
}
TEST(mme_error_text_note_all_texts) {
    REQUIRE(U(waveOutGetErrorTextW)); REQUIRE(U(midiOutGetErrorTextW));
    for (size_t k = 0; k < ET_COUNT(et_wave_text_codes); k++) note_w("wave", U(waveOutGetErrorTextW), et_wave_text_codes[k]);
    for (size_t k = 64; k <= 71; k++) note_w("midi", U(midiOutGetErrorTextW), (DWORD)k);
}

/* Concurrent callers get the same results as a single thread (the bodies keep no state). */
typedef struct { int idx; int mismatches; } thr_arg;
static WCHAR g_gold_w[2][300][MAXERRORLENGTH];
static char g_gold_a[2][300][MAXERRORLENGTH];
static MMRESULT g_gold_r[ET_NAPI][300];
static DWORD WINAPI thr_main(LPVOID p) {
    thr_arg *t = p;
    for (int rep = 0; rep < 5; rep++)
        for (int i = 0; i < ET_NAPI; i++) {
            const et_api *a = &et_apis[(i + t->idx) % ET_NAPI];
            int ai = (int)(a - et_apis);
            for (DWORD c = 0; c < 80; c++) {
                if (a->wide) {
                    WCHAR b[MAXERRORLENGTH]; b[0] = 0;
                    MMRESULT r = et_call_w(a, c, b, MAXERRORLENGTH);
                    if (r != g_gold_r[ai][c] || lstrcmpW(b, g_gold_w[a->family][c])) t->mismatches++;
                } else {
                    char b[MAXERRORLENGTH]; b[0] = 0;
                    MMRESULT r = et_call_a(a, c, b, MAXERRORLENGTH);
                    if (r != g_gold_r[ai][c] || strcmp(b, g_gold_a[a->family][c])) t->mismatches++;
                }
            }
        }
    return 0;
}
TEST(mme_error_text_threads_consistent) {
    ET_REQUIRE_ALL();
    for (int i = 0; i < ET_NAPI; i++) {
        const et_api *a = &et_apis[i];
        for (DWORD c = 0; c < 80; c++) {
            if (a->wide) { g_gold_w[a->family][c][0] = 0; g_gold_r[i][c] = et_call_w(a, c, g_gold_w[a->family][c], MAXERRORLENGTH); }
            else { g_gold_a[a->family][c][0] = 0; g_gold_r[i][c] = et_call_a(a, c, g_gold_a[a->family][c], MAXERRORLENGTH); }
            CHECKF(g_gold_r[i][c] == et_expect(a->family, c, MAXERRORLENGTH), "%s(%lu) = %u", a->name, c, g_gold_r[i][c]);
        }
    }
    enum { NT = 6 };
    thr_arg args[NT]; HANDLE h[NT];
    for (int t = 0; t < NT; t++) { args[t].idx = t; args[t].mismatches = 0; h[t] = CreateThread(NULL, 0, thr_main, &args[t], 0, NULL); CHECK(h[t] != NULL); }
    DWORD w = WaitForMultipleObjects(NT, h, TRUE, 30000);
    CHECK(w == WAIT_OBJECT_0);
    for (int t = 0; t < NT; t++) { if (h[t]) CloseHandle(h[t]); CHECKF(args[t].mismatches == 0, "thread %d: %d mismatches", t, args[t].mismatches); }
}

/* Results do not depend on whether any MME device exists: also run before and after device enumeration. */
TEST(mme_error_text_device_independent) {
    REQUIRE(U(waveOutGetErrorTextW)); REQUIRE(U(waveOutGetNumDevs)); REQUIRE(U(midiOutGetNumDevs));
    WCHAR before[MAXERRORLENGTH], after[MAXERRORLENGTH];
    CHECK(U(waveOutGetErrorTextW)(MMSYSERR_BADDEVICEID, before, MAXERRORLENGTH) == 0);
    UINT wo = U(waveOutGetNumDevs)(), mo = U(midiOutGetNumDevs)();
    t_note("devices: waveOut %u midiOut %u", wo, mo);
    CHECK(U(waveOutGetErrorTextW)(MMSYSERR_BADDEVICEID, after, MAXERRORLENGTH) == 0);
    CHECK(!lstrcmpW(before, after));
}
