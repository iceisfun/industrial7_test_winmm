#include "et.h"
/* Per-export contract: the same battery runs against each of the 8 exports through its own U() lookup.
 * Argument gates, range check, cch < 2 shortcut, clearing of pszText[0], truncation and the exact text source
 * (winmmbase string table) all come from the four winmmbase bodies listed in et.h. VM_TIMEOUT: 120 */

/* ---- generic A/W helpers ----------------------------------------------- */
static MMRESULT g_call(const et_api *a, DWORD code, void *buf, UINT cch) {
    return a->wide ? et_call_w(a, code, (WCHAR *)buf, cch) : et_call_a(a, code, (char *)buf, cch);
}
static size_t g_cs(const et_api *a) { return a->wide ? sizeof(WCHAR) : 1; }
static size_t g_len(const et_api *a, const void *buf, size_t cap) {
    size_t n = 0;
    if (a->wide) { const WCHAR *w = buf; while (n < cap && w[n]) n++; }
    else { const char *s = buf; while (n < cap && s[n]) n++; }
    return n;
}
static int g_ch_zero(const et_api *a, const void *buf, size_t i) {
    return a->wide ? ((const WCHAR *)buf)[i] == 0 : ((const char *)buf)[i] == 0;
}
/* Reference text of the same charset, from winmmbase directly. */
static int g_ref(const et_api *a, DWORD code, void *buf, int cch) {
    return a->wide ? et_ref_w(code, buf, cch) : et_ref_a(code, buf, cch);
}
static const DWORD *g_codes(const et_api *a, size_t *n) {
    if (a->family == ET_WAVE) { *n = ET_COUNT(et_wave_text_codes); return et_wave_text_codes; }
    *n = ET_COUNT(et_midi_text_codes); return et_midi_text_codes;
}
#define API_REQ(a) do { if (!(a)->resolve()) { t_skip("missing export: %s", (a)->name); return; } } while (0)

enum { BUFCH = 512 };

/* cchText == 0 is checked before the pointer: 0 with a NULL buffer, whatever the code. */
static void t_cch0_null(int i) {
    const et_api *a = &et_apis[i]; API_REQ(a);
    static const DWORD codes[] = { 0, 1, 2, 11, 20, 21, 22, 31, 32, 35, 36, 63, 64, 71, 72, 256, 0xFFFF, 0x10000, 0xFFFFFFFF };
    for (size_t k = 0; k < ET_COUNT(codes); k++)
        CHECKF(g_call(a, codes[k], NULL, 0) == MMSYSERR_NOERROR, "%s(%lu, NULL, 0)", a->name, codes[k]);
}

/* cchText == 0 with a real buffer: 0, and not one byte written (the bodies return before pszText[0] = 0). */
static void t_cch0_untouched(int i) {
    const et_api *a = &et_apis[i]; API_REQ(a);
    static const DWORD codes[] = { 0, 2, 5, 11, 20, 21, 22, 32, 35, 64, 71, 72, 0xFFFF, 0xFFFFFFFF };
    unsigned char buf[64];
    for (size_t k = 0; k < ET_COUNT(codes); k++) {
        memset(buf, ET_FILL, sizeof buf);
        CHECKF(g_call(a, codes[k], buf, 0) == MMSYSERR_NOERROR, "%s(%lu, buf, 0)", a->name, codes[k]);
        CHECKF(et_bytes_are(buf, sizeof buf, ET_FILL), "%s(%lu, buf, 0) wrote to the buffer", a->name, codes[k]);
    }
}

/* pszText == NULL with cchText > 0 -> MMSYSERR_INVALPARAM, before the range check (bad codes too). */
static void t_null_invalparam(int i) {
    const et_api *a = &et_apis[i]; API_REQ(a);
    static const DWORD codes[] = { 0, 1, 2, 20, 21, 22, 32, 35, 36, 64, 71, 72, 256, 0xFFFF, 0xFFFFFFFF };
    static const UINT cchs[] = { 1, 2, 3, 5, 16, 128, 256, 4096, 0x10000, 0x7FFFFFFF, 0x80000000u, 0xFFFFFFFFu };
    for (size_t k = 0; k < ET_COUNT(codes); k++)
        for (size_t c = 0; c < ET_COUNT(cchs); c++)
            CHECKF(g_call(a, codes[k], NULL, cchs[c]) == MMSYSERR_INVALPARAM, "%s(%lu, NULL, %u)", a->name, codes[k], cchs[c]);
}

/* The gate paths and the out-of-range path call nothing, so the thread's last error is left alone. */
static void t_lasterror_gates(int i) {
    const et_api *a = &et_apis[i]; API_REQ(a);
    unsigned char buf[BUFCH * 2];
    static const DWORD seeds[] = { 0, 0x12345678, ERROR_INVALID_PARAMETER };
    for (size_t s = 0; s < ET_COUNT(seeds); s++) {
        SetLastError(seeds[s]); g_call(a, 0, NULL, 0);           CHECKF(GetLastError() == seeds[s], "%s cch 0", a->name);
        SetLastError(seeds[s]); g_call(a, 0, buf, 0);            CHECKF(GetLastError() == seeds[s], "%s cch 0 buf", a->name);
        SetLastError(seeds[s]); g_call(a, 0, NULL, 16);          CHECKF(GetLastError() == seeds[s], "%s NULL", a->name);
        SetLastError(seeds[s]); g_call(a, 22, buf, 16);          CHECKF(GetLastError() == seeds[s], "%s code 22", a->name);
        SetLastError(seeds[s]); g_call(a, 0xFFFFFFFF, buf, 16);  CHECKF(GetLastError() == seeds[s], "%s code -1", a->name);
        SetLastError(seeds[s]); g_call(a, 2, buf, 1);            CHECKF(GetLastError() == seeds[s], "%s cch 1", a->name);
    }
}

/* Out-of-range code: 9, pszText[0] = 0, nothing else written. */
static void t_bad_codes(int i) {
    const et_api *a = &et_apis[i]; API_REQ(a);
    size_t cs = g_cs(a);
    unsigned char buf[64 * 2];
    static const UINT cchs[] = { 1, 2, 5, 64 };
    for (size_t k = 0; k < ET_COUNT(et_bad_codes); k++)
        for (size_t c = 0; c < ET_COUNT(cchs); c++) {
            memset(buf, ET_FILL, sizeof buf);
            MMRESULT r = g_call(a, et_bad_codes[k], buf, cchs[c]);
            CHECKF(r == MMSYSERR_BADERRNUM, "%s(0x%lx, cch %u) = %u", a->name, et_bad_codes[k], cchs[c], r);
            CHECKF(g_ch_zero(a, buf, 0), "%s(0x%lx) did not clear pszText[0]", a->name, et_bad_codes[k]);
            CHECKF(et_bytes_are(buf + cs, sizeof buf - cs, ET_FILL), "%s(0x%lx) wrote past pszText[0]", a->name, et_bad_codes[k]);
        }
}

/* Codes of the other family: wave has no 64..71, midi has no 32..35. */
static void t_other_family(int i) {
    const et_api *a = &et_apis[i]; API_REQ(a);
    unsigned char buf[BUFCH * 2];
    DWORD lo = a->family == ET_WAVE ? 64 : 32, hi = a->family == ET_WAVE ? 71 : 35;
    for (DWORD c = lo; c <= hi; c++) {
        memset(buf, ET_FILL, sizeof buf);
        CHECKF(g_call(a, c, buf, BUFCH) == MMSYSERR_BADERRNUM, "%s(%lu)", a->name, c);
        CHECK(g_ch_zero(a, buf, 0));
        memset(buf, ET_FILL, sizeof buf);
        CHECKF(g_call(a, c, buf, 1) == MMSYSERR_BADERRNUM, "%s(%lu, cch 1)", a->name, c);
        CHECK(g_ch_zero(a, buf, 0));
    }
}

/* cchText == 1 with an in-range code: LoadString is skipped -> 0 and the empty string; rest untouched. */
static void t_cch1(int i) {
    const et_api *a = &et_apis[i]; API_REQ(a);
    size_t n, cs = g_cs(a); const DWORD *codes = g_codes(a, &n);
    unsigned char buf[32];
    for (size_t k = 0; k < n; k++) {
        memset(buf, ET_FILL, sizeof buf);
        CHECKF(g_call(a, codes[k], buf, 1) == MMSYSERR_NOERROR, "%s(%lu, cch 1)", a->name, codes[k]);
        CHECK(g_ch_zero(a, buf, 0));
        CHECKF(et_bytes_are(buf + cs, sizeof buf - cs, ET_FILL), "%s(%lu, cch 1) wrote past [0]", a->name, codes[k]);
    }
}

/* Code 21 (MMSYSERR_MOREDATA) is inside the range but has no string: 9 for cch >= 2, yet 0 for cch 1 because the
 * LoadString call is skipped there (decomp: cch < 2 short-circuits to success). */
static void t_code21(int i) {
    const et_api *a = &et_apis[i]; API_REQ(a);
    unsigned char buf[BUFCH * 2];
    static const UINT cchs[] = { 2, 3, 5, 16, 128, 256, BUFCH };
    for (size_t c = 0; c < ET_COUNT(cchs); c++) {
        memset(buf, ET_FILL, sizeof buf);
        CHECKF(g_call(a, MMSYSERR_MOREDATA, buf, cchs[c]) == MMSYSERR_BADERRNUM, "%s(21, cch %u)", a->name, cchs[c]);
        CHECK(g_ch_zero(a, buf, 0));
    }
    memset(buf, ET_FILL, sizeof buf);
    CHECK(g_call(a, MMSYSERR_MOREDATA, buf, 1) == MMSYSERR_NOERROR);
    CHECK(g_ch_zero(a, buf, 0));
    SetLastError(0);
    g_call(a, MMSYSERR_MOREDATA, buf, 64);
    t_note("%s(21) last error after the failed string load: %lu", a->name, GetLastError());
}

/* Every code with text: 0, non-empty, NUL-terminated within cch. */
static void t_success(int i) {
    const et_api *a = &et_apis[i]; API_REQ(a);
    size_t n; const DWORD *codes = g_codes(a, &n);
    static const UINT cchs[] = { MAXERRORLENGTH, BUFCH };
    unsigned char buf[BUFCH * 2];
    for (size_t k = 0; k < n; k++)
        for (size_t c = 0; c < ET_COUNT(cchs); c++) {
            memset(buf, ET_FILL, sizeof buf);
            CHECKF(g_call(a, codes[k], buf, cchs[c]) == MMSYSERR_NOERROR, "%s(%lu, %u)", a->name, codes[k], cchs[c]);
            size_t len = g_len(a, buf, cchs[c]);
            CHECKF(len > 0, "%s(%lu) empty text", a->name, codes[k]);
            CHECKF(len < cchs[c], "%s(%lu) not NUL-terminated within cch", a->name, codes[k]);
        }
}

/* Nothing is written at or past pszText[cchText]. */
static void t_tail_beyond_cch(int i) {
    const et_api *a = &et_apis[i]; API_REQ(a);
    size_t n, cs = g_cs(a); const DWORD *codes = g_codes(a, &n);
    static const UINT cchs[] = { 1, 2, 3, 4, 8, 17, 33, 64, MAXERRORLENGTH };
    unsigned char buf[(MAXERRORLENGTH + 32) * 2];
    for (size_t k = 0; k < n; k++)
        for (size_t c = 0; c < ET_COUNT(cchs); c++) {
            memset(buf, ET_FILL, sizeof buf);
            g_call(a, codes[k], buf, cchs[c]);
            CHECKF(et_bytes_are(buf + cchs[c] * cs, sizeof buf - cchs[c] * cs, ET_FILL),
                   "%s(%lu, cch %u) wrote past the buffer", a->name, codes[k], cchs[c]);
        }
}

/* Between the terminator and pszText[cchText] nothing is written either (string copy, no padding). */
static void t_tail_after_nul(int i) {
    const et_api *a = &et_apis[i]; API_REQ(a);
    size_t n, cs = g_cs(a); const DWORD *codes = g_codes(a, &n);
    unsigned char buf[MAXERRORLENGTH * 2];
    for (size_t k = 0; k < n; k++) {
        memset(buf, ET_FILL, sizeof buf);
        CHECK(g_call(a, codes[k], buf, MAXERRORLENGTH) == MMSYSERR_NOERROR);
        size_t len = g_len(a, buf, MAXERRORLENGTH);
        if (len >= MAXERRORLENGTH) continue;
        CHECKF(et_bytes_are(buf + (len + 1) * cs, sizeof buf - (len + 1) * cs, ET_FILL),
               "%s(%lu) wrote after the terminator", a->name, codes[k]);
    }
}

/* The text is the winmmbase string resource with id == code, in the same charset. */
static void t_matches_resource(int i) {
    const et_api *a = &et_apis[i]; API_REQ(a);
    if (!UIN("user32.dll", LoadStringW) || !t_module("winmmbase.dll")) { t_skip("no winmmbase / LoadString"); return; }
    size_t n, cs = g_cs(a); const DWORD *codes = g_codes(a, &n);
    unsigned char got[BUFCH * 2], ref[BUFCH * 2];
    for (size_t k = 0; k < n; k++) {
        memset(got, 0, sizeof got); memset(ref, 0, sizeof ref);
        CHECK(g_call(a, codes[k], got, BUFCH) == MMSYSERR_NOERROR);
        int rl = g_ref(a, codes[k], ref, BUFCH);
        CHECKF(rl > 0, "winmmbase string %lu missing", codes[k]);
        CHECKF((size_t)rl == g_len(a, got, BUFCH), "%s(%lu) length %u vs resource %d", a->name, codes[k], (unsigned)g_len(a, got, BUFCH), rl);
        CHECKF(!memcmp(got, ref, (rl + 1) * cs), "%s(%lu) text differs from the winmmbase resource", a->name, codes[k]);
    }
    /* 21 and the codes between the ranges have no resource at all. */
    static const DWORD none[] = { 21, 22, 31, 36 };
    for (size_t k = 0; k < ET_COUNT(none); k++) CHECKF(g_ref(a, none[k], ref, BUFCH) == 0, "winmmbase has string %lu", none[k]);
}

/* MAXERRORLENGTH (256) holds every text: the result does not change with a much larger buffer. */
static void t_maxerrorlength(int i) {
    const et_api *a = &et_apis[i]; API_REQ(a);
    size_t n, cs = g_cs(a); const DWORD *codes = g_codes(a, &n);
    static unsigned char small[MAXERRORLENGTH * 2], big[8192 * 2];
    for (size_t k = 0; k < n; k++) {
        memset(small, 0, sizeof small); memset(big, 0, sizeof big);
        CHECK(g_call(a, codes[k], small, MAXERRORLENGTH) == MMSYSERR_NOERROR);
        CHECK(g_call(a, codes[k], big, 8192) == MMSYSERR_NOERROR);
        size_t ls = g_len(a, small, MAXERRORLENGTH), lb = g_len(a, big, 8192);
        CHECKF(lb < MAXERRORLENGTH - 1, "%s(%lu) full text %u chars does not fit", a->name, codes[k], (unsigned)lb);
        CHECKF(ls == lb && !memcmp(small, big, (lb + 1) * cs), "%s(%lu) 256 vs 8192 buffers differ", a->name, codes[k]);
    }
}

/* cchText 5: 0, at most 4 characters, a prefix of the full text. */
static void t_trunc5(int i) {
    const et_api *a = &et_apis[i]; API_REQ(a);
    size_t n, cs = g_cs(a); const DWORD *codes = g_codes(a, &n);
    int prefix_ok = a->wide || et_acp_sbcs();
    unsigned char full[BUFCH * 2], cut[32];
    for (size_t k = 0; k < n; k++) {
        memset(full, 0, sizeof full); memset(cut, ET_FILL, sizeof cut);
        CHECK(g_call(a, codes[k], full, BUFCH) == MMSYSERR_NOERROR);
        CHECKF(g_call(a, codes[k], cut, 5) == MMSYSERR_NOERROR, "%s(%lu, 5)", a->name, codes[k]);
        size_t lc = g_len(a, cut, 5), lf = g_len(a, full, BUFCH);
        CHECKF(lc <= 4, "%s(%lu, 5) length %u", a->name, codes[k], (unsigned)lc);
        if (prefix_ok) {
            CHECKF(lc == (lf < 4 ? lf : 4), "%s(%lu, 5) length %u of %u", a->name, codes[k], (unsigned)lc, (unsigned)lf);
            CHECKF(!memcmp(cut, full, lc * cs), "%s(%lu, 5) not a prefix", a->name, codes[k]);
        }
    }
}

/* Every cch from 2 to len + 2: 0, length min(len, cch - 1), prefix of the full text (W always; A in a SBCS code page). */
static void t_trunc_sweep(int i) {
    const et_api *a = &et_apis[i]; API_REQ(a);
    size_t n, cs = g_cs(a); const DWORD *codes = g_codes(a, &n);
    int exact = a->wide || et_acp_sbcs();
    if (!exact) t_note("ANSI code page is multi-byte: A truncation checked for length bound only");
    unsigned char full[BUFCH * 2], cut[BUFCH * 2];
    for (size_t k = 0; k < n; k++) {
        memset(full, 0, sizeof full);
        CHECK(g_call(a, codes[k], full, BUFCH) == MMSYSERR_NOERROR);
        size_t lf = g_len(a, full, BUFCH);
        for (UINT cch = 2; cch <= lf + 2 && cch < BUFCH; cch++) {
            memset(cut, ET_FILL, sizeof cut);
            MMRESULT r = g_call(a, codes[k], cut, cch);
            CHECKF(r == MMSYSERR_NOERROR, "%s(%lu, %u) = %u", a->name, codes[k], cch, r);
            size_t lc = g_len(a, cut, cch);
            CHECKF(lc < cch, "%s(%lu, %u) not terminated", a->name, codes[k], cch);
            if (exact) {
                size_t want = lf < cch - 1 ? lf : cch - 1;
                CHECKF(lc == want && !memcmp(cut, full, lc * cs), "%s(%lu, %u) length %u want %u", a->name, codes[k], cch, (unsigned)lc, (unsigned)want);
            }
        }
    }
}

/* The table of every code 0..300 against the range rule, with a full-size buffer. */
static void t_sweep(int i) {
    const et_api *a = &et_apis[i]; API_REQ(a);
    unsigned char buf[MAXERRORLENGTH * 2];
    for (DWORD c = 0; c <= 300; c++) {
        memset(buf, ET_FILL, sizeof buf);
        MMRESULT r = g_call(a, c, buf, MAXERRORLENGTH), want = et_expect(a->family, c, MAXERRORLENGTH);
        CHECKF(r == want, "%s(%lu) = %u want %u", a->name, c, r, want);
        if (want == MMSYSERR_NOERROR) CHECKF(!g_ch_zero(a, buf, 0), "%s(%lu) empty", a->name, c);
        else CHECKF(g_ch_zero(a, buf, 0), "%s(%lu) failure left text", a->name, c);
    }
}

/* Same table with cch 1 and cch 2: the range check alone decides (21 succeeds at cch 1). */
static void t_sweep_small(int i) {
    const et_api *a = &et_apis[i]; API_REQ(a);
    unsigned char buf[16];
    for (UINT cch = 1; cch <= 2; cch++)
        for (DWORD c = 0; c <= 300; c++) {
            memset(buf, ET_FILL, sizeof buf);
            MMRESULT r = g_call(a, c, buf, cch), want = et_expect(a->family, c, cch);
            CHECKF(r == want, "%s(%lu, cch %u) = %u want %u", a->name, c, cch, r, want);
            CHECKF(g_ch_zero(a, buf, cch - 1), "%s(%lu, cch %u) not terminated", a->name, c, cch);
        }
}

/* Stable across repeated calls (no state between calls). */
static void t_repeat(int i) {
    const et_api *a = &et_apis[i]; API_REQ(a);
    size_t n, cs = g_cs(a); const DWORD *codes = g_codes(a, &n);
    unsigned char first[MAXERRORLENGTH * 2], again[MAXERRORLENGTH * 2];
    for (size_t k = 0; k < n; k++) {
        memset(first, 0, sizeof first);
        g_call(a, codes[k], first, MAXERRORLENGTH);
        int same = 1;
        for (int rep = 0; rep < 20; rep++) {
            memset(again, 0, sizeof again);
            if (g_call(a, codes[k], again, MAXERRORLENGTH) != MMSYSERR_NOERROR || memcmp(first, again, sizeof first)) same = 0;
        }
        CHECKF(same, "%s(%lu) not stable", a->name, codes[k]);
    }
}

/* Ordinal export resolves to the named export (winmm.dll). */
static void t_ordinal(int i) {
    const et_api *a = &et_apis[i]; API_REQ(a);
    FARPROC byname = t_proc_in("winmm.dll", a->name), byord = t_proc_ord("winmm.dll", a->ord);
    CHECK(byname != NULL);
    CHECKF(byord == byname, "%s ordinal %u -> %p, name -> %p", a->name, a->ord, byord, byname);
}

/* The winmmbase export of the same name gives the same answers as the winmm export. */
static void t_winmmbase_same(int i) {
    const et_api *a = &et_apis[i]; API_REQ(a);
    FARPROC base = t_proc_in("winmmbase.dll", a->name);
    if (!base) { t_skip("winmmbase.dll does not export %s", a->name); return; }
    unsigned char b1[MAXERRORLENGTH * 2], b2[MAXERRORLENGTH * 2];
    static const UINT cchs[] = { 0, 1, 2, 5, MAXERRORLENGTH };
    for (DWORD c = 0; c <= 80; c++)
        for (size_t z = 0; z < ET_COUNT(cchs); z++) {
            memset(b1, ET_FILL, sizeof b1); memset(b2, ET_FILL, sizeof b2);
            MMRESULT r1 = g_call(a, c, b1, cchs[z]);
            MMRESULT r2 = a->wide ? ((et_w_fn)base)(c, (WCHAR *)b2, cchs[z]) : ((et_a_fn)base)(c, (char *)b2, cchs[z]);
            CHECKF(r1 == r2 && !memcmp(b1, b2, sizeof b1), "%s(%lu, %u): winmm %u, winmmbase %u", a->name, c, cchs[z], r1, r2);
        }
}

/* The low word alone does not select the text: 0x10000 + valid code is out of range. */
static void t_high_word(int i) {
    const et_api *a = &et_apis[i]; API_REQ(a);
    size_t n; const DWORD *codes = g_codes(a, &n);
    static const DWORD hi[] = { 0x10000, 0x20000, 0x80000000, 0xFFFF0000 };
    unsigned char buf[MAXERRORLENGTH * 2];
    for (size_t k = 0; k < n; k++)
        for (size_t h = 0; h < ET_COUNT(hi); h++) {
            memset(buf, ET_FILL, sizeof buf);
            CHECKF(g_call(a, codes[k] | hi[h], buf, MAXERRORLENGTH) == MMSYSERR_BADERRNUM, "%s(0x%lx)", a->name, codes[k] | hi[h]);
            CHECK(g_ch_zero(a, buf, 0));
        }
}

/* A buffer that already holds text is overwritten (pszText[0] cleared first, then the new text). */
static void t_overwrite(int i) {
    const et_api *a = &et_apis[i]; API_REQ(a);
    size_t cs = g_cs(a);
    unsigned char buf[MAXERRORLENGTH * 2], ref[MAXERRORLENGTH * 2];
    DWORD second = a->family == ET_WAVE ? WAVERR_BADFORMAT : MIDIERR_UNPREPARED;
    CHECK(g_call(a, MMSYSERR_NOERROR, buf, MAXERRORLENGTH) == MMSYSERR_NOERROR);
    CHECK(g_call(a, second, buf, MAXERRORLENGTH) == MMSYSERR_NOERROR);
    memset(ref, 0, sizeof ref);
    CHECK(g_call(a, second, ref, MAXERRORLENGTH) == MMSYSERR_NOERROR);
    size_t l = g_len(a, ref, MAXERRORLENGTH);
    CHECK(!memcmp(buf, ref, (l + 1) * cs));
    CHECK(g_call(a, 0xFFFF, buf, MAXERRORLENGTH) == MMSYSERR_BADERRNUM);
    CHECK(g_ch_zero(a, buf, 0));
}

#define ET_PER_API(tag, i) \
    TEST(mme_error_text_##tag##_cch0_null)            { t_cch0_null(i); } \
    TEST(mme_error_text_##tag##_cch0_untouched)       { t_cch0_untouched(i); } \
    TEST(mme_error_text_##tag##_null_invalparam)      { t_null_invalparam(i); } \
    TEST(mme_error_text_##tag##_lasterror_gates)      { t_lasterror_gates(i); } \
    TEST(mme_error_text_##tag##_bad_codes)            { t_bad_codes(i); } \
    TEST(mme_error_text_##tag##_other_family)         { t_other_family(i); } \
    TEST(mme_error_text_##tag##_cch1)                 { t_cch1(i); } \
    TEST(mme_error_text_##tag##_code21)               { t_code21(i); } \
    TEST(mme_error_text_##tag##_success)              { t_success(i); } \
    TEST(mme_error_text_##tag##_tail_beyond_cch)      { t_tail_beyond_cch(i); } \
    TEST(mme_error_text_##tag##_tail_after_nul)       { t_tail_after_nul(i); } \
    TEST(mme_error_text_##tag##_matches_resource)     { t_matches_resource(i); } \
    TEST(mme_error_text_##tag##_maxerrorlength)       { t_maxerrorlength(i); } \
    TEST(mme_error_text_##tag##_trunc5)               { t_trunc5(i); } \
    TEST(mme_error_text_##tag##_trunc_sweep)          { t_trunc_sweep(i); } \
    TEST(mme_error_text_##tag##_sweep_0_300)          { t_sweep(i); } \
    TEST(mme_error_text_##tag##_sweep_cch1_cch2)      { t_sweep_small(i); } \
    TEST(mme_error_text_##tag##_repeat)               { t_repeat(i); } \
    TEST(mme_error_text_##tag##_ordinal)              { t_ordinal(i); } \
    TEST(mme_error_text_##tag##_winmmbase_same)       { t_winmmbase_same(i); } \
    TEST(mme_error_text_##tag##_high_word)            { t_high_word(i); } \
    TEST(mme_error_text_##tag##_overwrite)            { t_overwrite(i); }

ET_PER_API(waveouta, 0)
ET_PER_API(waveoutw, 1)
ET_PER_API(waveina, 2)
ET_PER_API(waveinw, 3)
ET_PER_API(midiouta, 4)
ET_PER_API(midioutw, 5)
ET_PER_API(midiina, 6)
ET_PER_API(midiinw, 7)
