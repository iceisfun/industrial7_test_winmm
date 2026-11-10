#include "wf.h"
/* Non-PCM wFormatTag values with WAVE_FORMAT_QUERY: IEEE float, the ACM codec tags the wave mapper converts
 * (MS ADPCM, IMA ADPCM, GSM 6.10, mu-law, a-law, MPEG layer 3) and unknown tags. The mapper (msacm wave mapper)
 * may insert an ACM conversion; WAVE_FORMAT_DIRECT forbids it, and a specific device id never converts.
 * Targets per cell: mapper, mapper|DIRECT, id 0, id 0|DIRECT, id 0|WAVE_MAPPED.
 * VM_TIMEOUT: 240 */

#define N WF_NOTE
#define Z MMSYSERR_NOERROR
#define B WAVERR_BADFORMAT

typedef struct { UINT id; DWORD fl; const char *n; } target;
static const target k_tg[5] = {
    { WAVE_MAPPER, 0, "mapper" }, { WAVE_MAPPER, WAVE_FORMAT_DIRECT, "mapper|DIRECT" }, { 0, 0, "id0" },
    { 0, WAVE_FORMAT_DIRECT, "id0|DIRECT" }, { 0, WAVE_MAPPED, "id0|MAPPED" } };

static void five(int dir, const char *lab, const void *fmt, const int exp[5]) {
    if (!U(waveOutOpen) || !U(waveInOpen)) { t_skip("missing open"); return; }
    char l[96];
    for (int t = 0; t < 5; t++) {
        sprintf(l, "%s @%s", lab, k_tg[t].n);
        wf_cell(l, dir, k_tg[t].id, fmt, k_tg[t].fl, exp[t]);
    }
    EXPECT_EQ(wf_query(dir, wf_ndev(dir), fmt, 0), MMSYSERR_BADDEVICEID);
}

/* ---- IEEE float (tag 3, no extension) ------------------------------------------------------------------------- */

#define FLOAT_CASES(X) \
    X(stereo_48k_32,  2, 48000, 32, Z, N, N, N, N) \
    X(stereo_44k_32,  2, 44100, 32, Z, N, N, N, N) \
    X(mono_48k_32,    1, 48000, 32, N, N, N, N, N) \
    X(mono_22k_32,    1, 22050, 32, N, N, N, N, N) \
    X(stereo_96k_32,  2, 96000, 32, N, N, N, N, N) \
    X(stereo_8k_32,   2,  8000, 32, N, N, N, N, N) \
    X(stereo_48k_64,  2, 48000, 64, N, N, N, N, N) \
    X(stereo_48k_16,  2, 48000, 16, N, N, N, N, N) \
    X(stereo_48k_24,  2, 48000, 24, N, N, N, N, N) \
    X(six_48k_32,     6, 48000, 32, N, N, N, N, N) \
    X(ch0_48k_32,     0, 48000, 32, N, N, N, N, N) \
    X(stereo_0_32,    2,     0, 32, N, N, N, N, N)

#define FX(nm, ch, rate, bits, a, b, c, d, e) \
    static void float_##nm(int dir) { wf_fmt f; wf_pcm(&f, WAVE_FORMAT_IEEE_FLOAT_, ch, rate, bits); \
        static const int x[5] = { a, b, c, d, e }; five(dir, "float " #nm, &f, x); } \
    TEST(wf_out_float_##nm) { float_##nm(WF_OUT); } TEST(wf_in_float_##nm) { float_##nm(WF_IN); }
FLOAT_CASES(FX)

/* ---- ACM codec tags -------------------------------------------------------------------------------------------- */

/* Through the plain mapper an ACM codec converts to PCM: expected 0. With DIRECT: no conversion -> 32. A specific
 * id is the endpoint driver alone. */
#define ACM_CASES(X) \
    X(msadpcm_mono_22k,   wf_msadpcm(&f, 1, 22050),                     Z, B, N, N, N) \
    X(msadpcm_mono_8k,    wf_msadpcm(&f, 1, 8000),                      Z, B, N, N, N) \
    X(msadpcm_mono_11k,   wf_msadpcm(&f, 1, 11025),                     Z, B, N, N, N) \
    X(msadpcm_mono_44k,   wf_msadpcm(&f, 1, 44100),                     Z, B, N, N, N) \
    X(msadpcm_stereo_22k, wf_msadpcm(&f, 2, 22050),                     Z, B, N, N, N) \
    X(msadpcm_stereo_44k, wf_msadpcm(&f, 2, 44100),                     Z, B, N, N, N) \
    X(imaadpcm_mono_22k,  wf_imaadpcm(&f, 1, 22050),                    Z, B, N, N, N) \
    X(imaadpcm_mono_8k,   wf_imaadpcm(&f, 1, 8000),                     Z, B, N, N, N) \
    X(imaadpcm_mono_11k,  wf_imaadpcm(&f, 1, 11025),                    Z, B, N, N, N) \
    X(imaadpcm_mono_44k,  wf_imaadpcm(&f, 1, 44100),                    Z, B, N, N, N) \
    X(imaadpcm_stereo_22k, wf_imaadpcm(&f, 2, 22050),                   Z, B, N, N, N) \
    X(imaadpcm_stereo_44k, wf_imaadpcm(&f, 2, 44100),                   Z, B, N, N, N) \
    X(gsm610_8k,          wf_gsm610(&f, 8000),                          Z, B, N, N, N) \
    X(gsm610_11k,         wf_gsm610(&f, 11025),                         N, B, N, N, N) \
    X(gsm610_22k,         wf_gsm610(&f, 22050),                         N, B, N, N, N) \
    X(gsm610_44k,         wf_gsm610(&f, 44100),                         N, B, N, N, N) \
    X(mulaw_mono_8k,      wf_g711(&f, WAVE_FORMAT_MULAW_, 1, 8000),     Z, B, N, N, N) \
    X(mulaw_stereo_8k,    wf_g711(&f, WAVE_FORMAT_MULAW_, 2, 8000),     Z, B, N, N, N) \
    X(mulaw_mono_11k,     wf_g711(&f, WAVE_FORMAT_MULAW_, 1, 11025),    Z, B, N, N, N) \
    X(mulaw_mono_22k,     wf_g711(&f, WAVE_FORMAT_MULAW_, 1, 22050),    Z, B, N, N, N) \
    X(mulaw_mono_44k,     wf_g711(&f, WAVE_FORMAT_MULAW_, 1, 44100),    Z, B, N, N, N) \
    X(alaw_mono_8k,       wf_g711(&f, WAVE_FORMAT_ALAW_, 1, 8000),      Z, B, N, N, N) \
    X(alaw_stereo_8k,     wf_g711(&f, WAVE_FORMAT_ALAW_, 2, 8000),      Z, B, N, N, N) \
    X(alaw_mono_11k,      wf_g711(&f, WAVE_FORMAT_ALAW_, 1, 11025),     Z, B, N, N, N) \
    X(alaw_mono_22k,      wf_g711(&f, WAVE_FORMAT_ALAW_, 1, 22050),     Z, B, N, N, N) \
    X(alaw_mono_44k,      wf_g711(&f, WAVE_FORMAT_ALAW_, 1, 44100),     Z, B, N, N, N) \
    X(mp3_stereo_44k_128, wf_mp3(&f, 2, 44100, 128),                    N, B, N, N, N) \
    X(mp3_mono_22k_32,    wf_mp3(&f, 1, 22050, 32),                     N, B, N, N, N) \
    X(mp3_stereo_48k_192, wf_mp3(&f, 2, 48000, 192),                    N, B, N, N, N)

#define AX(nm, build, a, b, c, d, e) \
    static void acm_##nm(int dir) { wf_fmt f; build; static const int x[5] = { a, b, c, d, e }; \
        five(dir, "acm " #nm, &f, x); } \
    TEST(wf_out_acm_##nm) { acm_##nm(WF_OUT); } TEST(wf_in_acm_##nm) { acm_##nm(WF_IN); }
ACM_CASES(AX)

/* ACM tags with damaged codec-specific bytes: recorded. */
static void acm_damaged(int dir) {
    static const int n5[5] = { N, N, N, N, N };
    wf_fmt f;
    wf_msadpcm(&f, 1, 22050); f.cbSize = 0;                       five(dir, "msadpcm cbSize0", &f, n5);
    wf_msadpcm(&f, 1, 22050); f.extra[2] = f.extra[3] = 0;        five(dir, "msadpcm numcoef0", &f, n5);
    wf_msadpcm(&f, 1, 22050); f.extra[0] = f.extra[1] = 0;        five(dir, "msadpcm spb0", &f, n5);
    wf_msadpcm(&f, 1, 22050); f.nBlockAlign = 0;                  five(dir, "msadpcm blockalign0", &f, n5);
    wf_msadpcm(&f, 1, 22050); f.wBitsPerSample = 8;               five(dir, "msadpcm bits8", &f, n5);
    wf_imaadpcm(&f, 1, 22050); f.cbSize = 0;                      five(dir, "imaadpcm cbSize0", &f, n5);
    wf_imaadpcm(&f, 1, 22050); f.extra[0] = f.extra[1] = 0;       five(dir, "imaadpcm spb0", &f, n5);
    wf_imaadpcm(&f, 1, 22050); f.wBitsPerSample = 3;              five(dir, "imaadpcm bits3", &f, n5);
    wf_gsm610(&f, 8000); f.extra[0] = f.extra[1] = 0;             five(dir, "gsm610 spb0", &f, n5);
    wf_gsm610(&f, 8000); f.nBlockAlign = 64;                      five(dir, "gsm610 block64", &f, n5);
    wf_g711(&f, WAVE_FORMAT_MULAW_, 1, 8000); f.wBitsPerSample = 16; five(dir, "mulaw bits16", &f, n5);
    wf_g711(&f, WAVE_FORMAT_ALAW_, 1, 8000); f.nChannels = 0;     five(dir, "alaw ch0", &f, n5);
}
TEST(wf_out_acm_damaged) { acm_damaged(WF_OUT); }
TEST(wf_in_acm_damaged)  { acm_damaged(WF_IN); }

/* ---- unknown / unsupported tags ------------------------------------------------------------------------------- */

/* 0x1234 is not a registered tag: the endpoint rejects it with DIRECT on id 0 (32); mapper verdicts recorded. */
#define TAG_CASES(X) \
    X(x1234,   0x1234, N, N, N, B, N) \
    X(x0000,   0x0000, N, N, N, N, N) \
    X(xffff,   0xFFFF, N, N, N, N, N) \
    X(x0004,   0x0004, N, N, N, N, N) \
    X(x0050,   0x0050, N, N, N, N, N) \
    X(x0092,   0x0092, N, N, N, N, N) \
    X(x0161,   0x0161, N, N, N, N, N) \
    X(x00ff,   0x00FF, N, N, N, N, N) \
    X(x1610,   0x1610, N, N, N, N, N) \
    X(x2000,   0x2000, N, N, N, N, N) \
    X(x8000,   0x8000, N, N, N, N, N)

#define TGX(nm, tag, a, b, c, d, e) \
    static void tag_##nm(int dir) { wf_fmt f; wf_pcm(&f, tag, 2, 44100, 16); static const int x[5] = { a, b, c, d, e }; \
        five(dir, "tag " #nm, &f, x); } \
    TEST(wf_out_tag_##nm) { tag_##nm(WF_OUT); } TEST(wf_in_tag_##nm) { tag_##nm(WF_IN); }
TAG_CASES(TGX)

/* The unknown tag with codec-style extra bytes (cbSize 2) gets the same id0|DIRECT verdict. */
static void tag_extra(int dir) {
    wf_fmt f; wf_pcm(&f, 0x1234, 2, 44100, 16); f.cbSize = 2; f.extra[0] = 1;
    static const int x[5] = { N, N, N, B, N };
    five(dir, "tag x1234 cb2", &f, x);
}
TEST(wf_out_tag_x1234_with_extra) { tag_extra(WF_OUT); }
TEST(wf_in_tag_x1234_with_extra)  { tag_extra(WF_IN); }
