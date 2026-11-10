/* Shared tables for the mme_error_text area.
 *
 * Eight exports, four winmmbase bodies (winmm exports are thin jumps into winmmbase):
 *   waveOutGetErrorTextW == waveInGetErrorTextW  -> winmmbase!0x10320 -> waveGetErrorTextW (0x102c4)
 *   waveOutGetErrorTextA == waveInGetErrorTextA  -> winmmbase!0x10340 (inline LoadStringA)
 *   midiOutGetErrorTextW == midiInGetErrorTextW  -> winmmbase!0x7d40  -> midiGetErrorTextW (0x7cec)
 *   midiOutGetErrorTextA == midiInGetErrorTextA  -> winmmbase!0x7d60  (inline LoadStringA)
 * Every body:
 *   cchText == 0               -> 0 (MMSYSERR_NOERROR), nothing written, NULL pointer allowed
 *   pszText == NULL            -> 11 (MMSYSERR_INVALPARAM)
 *   pszText[0] = 0, then the range check on the full 32-bit code:
 *     wave: code < 22 || code - 32 < 4         midi: code < 22 || code - 64 < 8
 *   outside the range          -> 9 (MMSYSERR_BADERRNUM), only pszText[0] written
 *   inside, cchText < 2        -> 0 with the empty string (LoadString skipped)
 *   inside, LoadString(winmmbase, code, pszText, cchText) == 0 -> 9 (code 21 has no string: VM)
 *   else                       -> 0
 */
#ifndef ET_H
#define ET_H
#include "t.h"
#include <mmsystem.h>

typedef MMRESULT (WINAPI *et_a_fn)(MMRESULT, LPSTR, UINT);
typedef MMRESULT (WINAPI *et_w_fn)(MMRESULT, LPWSTR, UINT);

enum { ET_WAVE = 0, ET_MIDI = 1 };

/* One row per export. resolve() performs the real U() lookup of that export. */
typedef struct {
    const char *name;
    int family;      /* ET_WAVE / ET_MIDI */
    int wide;        /* 1 = W */
    int out;         /* 1 = Out variant, 0 = In variant */
    unsigned ord;    /* winmm.dll ordinal */
    FARPROC (*resolve)(void);
} et_api;

static FARPROC et_r_woa(void) { return (FARPROC)U(waveOutGetErrorTextA); }
static FARPROC et_r_wow(void) { return (FARPROC)U(waveOutGetErrorTextW); }
static FARPROC et_r_wia(void) { return (FARPROC)U(waveInGetErrorTextA); }
static FARPROC et_r_wiw(void) { return (FARPROC)U(waveInGetErrorTextW); }
static FARPROC et_r_moa(void) { return (FARPROC)U(midiOutGetErrorTextA); }
static FARPROC et_r_mow(void) { return (FARPROC)U(midiOutGetErrorTextW); }
static FARPROC et_r_mia(void) { return (FARPROC)U(midiInGetErrorTextA); }
static FARPROC et_r_miw(void) { return (FARPROC)U(midiInGetErrorTextW); }

static const et_api et_apis[8] = {
    { "waveOutGetErrorTextA", ET_WAVE, 0, 1, 164, et_r_woa },
    { "waveOutGetErrorTextW", ET_WAVE, 1, 1, 165, et_r_wow },
    { "waveInGetErrorTextA",  ET_WAVE, 0, 0, 148, et_r_wia },
    { "waveInGetErrorTextW",  ET_WAVE, 1, 0, 149, et_r_wiw },
    { "midiOutGetErrorTextA", ET_MIDI, 0, 1,  72, et_r_moa },
    { "midiOutGetErrorTextW", ET_MIDI, 1, 1,  73, et_r_mow },
    { "midiInGetErrorTextA",  ET_MIDI, 0, 0,  56, et_r_mia },
    { "midiInGetErrorTextW",  ET_MIDI, 1, 0,  57, et_r_miw },
};
#define ET_NAPI 8

/* Range check of the bodies (decomp), on the full 32-bit value. */
static int et_in_range(int family, DWORD code) {
    if (code < 22) return 1;
    return family == ET_WAVE ? (code - 32u < 4u) : (code - 64u < 8u);
}
/* Codes that have a string resource (in range, minus 21 = MMSYSERR_MOREDATA which has none: VM). */
static int et_has_text(int family, DWORD code) { return et_in_range(family, code) && code != 21; }

/* Expected result for a valid buffer of cch >= 1 characters. */
static MMRESULT et_expect(int family, DWORD code, UINT cch) {
    if (!et_in_range(family, code)) return MMSYSERR_BADERRNUM;
    if (cch < 2) return MMSYSERR_NOERROR;
    return code == 21 ? MMSYSERR_BADERRNUM : MMSYSERR_NOERROR;
}

/* Every code with text, per family. */
static const DWORD et_wave_text_codes[] = { 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20, 32,33,34,35 };
static const DWORD et_midi_text_codes[] = { 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20, 64,65,66,67,68,69,70,71 };

/* Codes outside both ranges, plus high-word values whose low word is a valid code (the check is on 32 bits). */
static const DWORD et_bad_codes[] = {
    22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 36, 37, 40, 47, 48, 63, 72, 73, 79, 80, 95, 96, 100, 127, 128, 255, 256,
    257, 511, 512, 1024, 0x7FFF, 0x8000, 0xFFFF, 0x10000, 0x10002, 0x10020, 0x10040, 0x20000, 0x7FFFFFFF, 0x80000000,
    0x80000002, 0xFFFFFF00, 0xFFFFFFE0, 0xFFFFFFFE, 0xFFFFFFFF
};
#define ET_COUNT(a) (sizeof(a) / sizeof((a)[0]))

static MMRESULT et_call_a(const et_api *api, DWORD code, char *buf, UINT cch) {
    return ((et_a_fn)api->resolve())((MMRESULT)code, buf, cch);
}
static MMRESULT et_call_w(const et_api *api, DWORD code, WCHAR *buf, UINT cch) {
    return ((et_w_fn)api->resolve())((MMRESULT)code, buf, cch);
}

/* Reference text straight from the winmmbase string table (the bodies call LoadString(ghInst = winmmbase)). */
static int et_ref_w(DWORD code, WCHAR *buf, int cch) {
    __typeof__(&LoadStringW) ls = UIN("user32.dll", LoadStringW);
    HMODULE m = t_module("winmmbase.dll");
    buf[0] = 0;
    if (!ls || !m) return -1;
    return ls(m, code, buf, cch);
}
static int et_ref_a(DWORD code, char *buf, int cch) {
    __typeof__(&LoadStringA) ls = UIN("user32.dll", LoadStringA);
    HMODULE m = t_module("winmmbase.dll");
    buf[0] = 0;
    if (!ls || !m) return -1;
    return ls(m, code, buf, cch);
}

static int et_acp_sbcs(void) { CPINFO ci; return GetCPInfo(CP_ACP, &ci) && ci.MaxCharSize == 1; }

#define ET_FILL 0xCC
static int et_bytes_are(const void *p, size_t n, unsigned char v) {
    const unsigned char *b = (const unsigned char *)p;
    for (size_t i = 0; i < n; i++) if (b[i] != v) return 0;
    return 1;
}

/* REQUIRE every one of the 8 exports. */
#define ET_REQUIRE_ALL() do { \
    REQUIRE(U(waveOutGetErrorTextA)); REQUIRE(U(waveOutGetErrorTextW)); REQUIRE(U(waveInGetErrorTextA)); \
    REQUIRE(U(waveInGetErrorTextW)); REQUIRE(U(midiOutGetErrorTextA)); REQUIRE(U(midiOutGetErrorTextW)); \
    REQUIRE(U(midiInGetErrorTextA)); REQUIRE(U(midiInGetErrorTextW)); } while (0)
#endif
