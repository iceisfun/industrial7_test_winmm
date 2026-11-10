#include "wf.h"
/* WAVE_FORMAT_EXTENSIBLE (0xFFFE) negotiation with WAVE_FORMAT_QUERY. One TEST per (direction, case); each case is
 * queried on four targets: mapper, mapper|DIRECT, device 0, device 0|DIRECT. Every expectation is the verdict the
 * Win10 VM gave (HDA duplex and HDA output-only guests, identical for waveOut and waveIn); with no device in the
 * direction every cell is 2 (wf_cell's gate). Shape of the verdicts:
 *   - the shared-mode engine converts: any PCM 8/16/24/32 container (valid bits <= container, > 0), float
 *     16/32/64, any channel mask (0, mismatched, all bits), 1..8 channels, inconsistent nBlockAlign and (PCM)
 *     nAvgBytesPerSec -> 0 on every target;
 *   - cbSize < 22: the endpoint says 11 (MMSYSERR_INVALPARAM); the mapper without DIRECT then tries ACM and reports
 *     32, with DIRECT it passes the device's 11 through;
 *   - nChannels 0, nSamplesPerSec 0, wValidBitsPerSample 0 or > container, float with nAvgBytesPerSec 0, and every
 *     SubFormat other than PCM / IEEE_FLOAT (a-law, mu-law, ADPCM, DRM, unknown GUID) -> 32.
 * VM_TIMEOUT: 240 */

enum { TW_NONE, TW_BA_PLUS1, TW_BA_ZERO, TW_AVG_ZERO, TW_AVG_PLUS1 };

#define N WF_NOTE
#define Z MMSYSERR_NOERROR
#define B WAVERR_BADFORMAT
#define I MMSYSERR_INVALPARAM
/*     name                  ch rate   bits valid mask         sub           cb  tweak        mapper mapD id0 id0D */
#define EXT_CASES(X) \
    X(pcm16_stereo_44k,       2, 44100, 16, 16, 0x3,        WF_SUB_PCM,   22, TW_NONE,      Z, Z, Z, Z) \
    X(pcm16_stereo_48k,       2, 48000, 16, 16, 0x3,        WF_SUB_PCM,   22, TW_NONE,      Z, Z, Z, Z) \
    X(pcm16_stereo_22k,       2, 22050, 16, 16, 0x3,        WF_SUB_PCM,   22, TW_NONE,      Z, Z, Z, Z) \
    X(pcm16_mono_44k,         1, 44100, 16, 16, 0x4,        WF_SUB_PCM,   22, TW_NONE,      Z, Z, Z, Z) \
    X(pcm8_stereo_44k,        2, 44100,  8,  8, 0x3,        WF_SUB_PCM,   22, TW_NONE,      Z, Z, Z, Z) \
    X(pcm8_mono_8k,           1,  8000,  8,  8, 0x4,        WF_SUB_PCM,   22, TW_NONE,      Z, Z, Z, Z) \
    X(pcm24_stereo_48k,       2, 48000, 24, 24, 0x3,        WF_SUB_PCM,   22, TW_NONE,      Z, Z, Z, Z) \
    X(pcm32_stereo_48k,       2, 48000, 32, 32, 0x3,        WF_SUB_PCM,   22, TW_NONE,      Z, Z, Z, Z) \
    X(pcm24in32_stereo_48k,   2, 48000, 32, 24, 0x3,        WF_SUB_PCM,   22, TW_NONE,      Z, Z, Z, Z) \
    X(pcm20in24_stereo_48k,   2, 48000, 24, 20, 0x3,        WF_SUB_PCM,   22, TW_NONE,      Z, Z, Z, Z) \
    X(pcm12in16_stereo_44k,   2, 44100, 16, 12, 0x3,        WF_SUB_PCM,   22, TW_NONE,      Z, Z, Z, Z) \
    X(pcm16_valid0,           2, 44100, 16,  0, 0x3,        WF_SUB_PCM,   22, TW_NONE,      B, B, B, B) \
    X(pcm16_valid24,          2, 44100, 16, 24, 0x3,        WF_SUB_PCM,   22, TW_NONE,      B, B, B, B) \
    X(pcm16_mask0,            2, 44100, 16, 16, 0x0,        WF_SUB_PCM,   22, TW_NONE,      Z, Z, Z, Z) \
    X(pcm16_mask7,            2, 44100, 16, 16, 0x7,        WF_SUB_PCM,   22, TW_NONE,      Z, Z, Z, Z) \
    X(pcm16_mask_fl_only,     2, 44100, 16, 16, 0x1,        WF_SUB_PCM,   22, TW_NONE,      Z, Z, Z, Z) \
    X(pcm16_mask_all,         2, 44100, 16, 16, 0xFFFFFFFF, WF_SUB_PCM,   22, TW_NONE,      Z, Z, Z, Z) \
    X(pcm16_quad,             4, 48000, 16, 16, 0x33,       WF_SUB_PCM,   22, TW_NONE,      Z, Z, Z, Z) \
    X(pcm16_5point1,          6, 48000, 16, 16, 0x3F,       WF_SUB_PCM,   22, TW_NONE,      Z, Z, Z, Z) \
    X(pcm16_5point1_side,     6, 48000, 16, 16, 0x60F,      WF_SUB_PCM,   22, TW_NONE,      Z, Z, Z, Z) \
    X(pcm16_7point1,          8, 48000, 16, 16, 0x63F,      WF_SUB_PCM,   22, TW_NONE,      Z, Z, Z, Z) \
    X(pcm16_ch0,              0, 48000, 16, 16, 0x0,        WF_SUB_PCM,   22, TW_NONE,      B, B, B, B) \
    X(pcm16_rate0,            2,     0, 16, 16, 0x3,        WF_SUB_PCM,   22, TW_NONE,      B, B, B, B) \
    X(float32_stereo_48k,     2, 48000, 32, 32, 0x3,        WF_SUB_FLOAT, 22, TW_NONE,      Z, Z, Z, Z) \
    X(float32_stereo_44k,     2, 44100, 32, 32, 0x3,        WF_SUB_FLOAT, 22, TW_NONE,      Z, Z, Z, Z) \
    X(float32_mono_48k,       1, 48000, 32, 32, 0x4,        WF_SUB_FLOAT, 22, TW_NONE,      Z, Z, Z, Z) \
    X(float32_5point1,        6, 48000, 32, 32, 0x3F,       WF_SUB_FLOAT, 22, TW_NONE,      Z, Z, Z, Z) \
    X(float64_stereo_48k,     2, 48000, 64, 64, 0x3,        WF_SUB_FLOAT, 22, TW_NONE,      Z, Z, Z, Z) \
    X(float32_valid24,        2, 48000, 32, 24, 0x3,        WF_SUB_FLOAT, 22, TW_NONE,      Z, Z, Z, Z) \
    X(float16_stereo,         2, 48000, 16, 16, 0x3,        WF_SUB_FLOAT, 22, TW_NONE,      Z, Z, Z, Z) \
    X(cb0,                    2, 44100, 16, 16, 0x3,        WF_SUB_PCM,    0, TW_NONE,      B, I, I, I) \
    X(cb10,                   2, 44100, 16, 16, 0x3,        WF_SUB_PCM,   10, TW_NONE,      B, I, I, I) \
    X(cb21,                   2, 44100, 16, 16, 0x3,        WF_SUB_PCM,   21, TW_NONE,      B, I, I, I) \
    X(cb23,                   2, 44100, 16, 16, 0x3,        WF_SUB_PCM,   23, TW_NONE,      Z, Z, Z, Z) \
    X(cb40,                   2, 44100, 16, 16, 0x3,        WF_SUB_PCM,   40, TW_NONE,      Z, Z, Z, Z) \
    X(float_cb21,             2, 48000, 32, 32, 0x3,        WF_SUB_FLOAT, 21, TW_NONE,      B, I, I, I) \
    X(sub_bogus,              2, 44100, 16, 16, 0x3,        WF_SUB_BOGUS, 22, TW_NONE,      B, B, B, B) \
    X(sub_bogus_float,        2, 48000, 32, 32, 0x3,        WF_SUB_BOGUS, 22, TW_NONE,      B, B, B, B) \
    X(sub_alaw,               1,  8000,  8,  8, 0x4,        WF_SUB_ALAW,  22, TW_NONE,      B, B, B, B) \
    X(sub_mulaw,              1,  8000,  8,  8, 0x4,        WF_SUB_MULAW, 22, TW_NONE,      B, B, B, B) \
    X(sub_adpcm,              1, 22050,  4,  4, 0x4,        WF_SUB_ADPCM, 22, TW_NONE,      B, B, B, B) \
    X(sub_drm,                2, 44100, 16, 16, 0x3,        WF_SUB_DRM,   22, TW_NONE,      B, B, B, B) \
    X(blockalign_plus1,       2, 44100, 16, 16, 0x3,        WF_SUB_PCM,   22, TW_BA_PLUS1,  Z, Z, Z, Z) \
    X(blockalign_zero,        2, 44100, 16, 16, 0x3,        WF_SUB_PCM,   22, TW_BA_ZERO,   Z, Z, Z, Z) \
    X(avgbytes_zero,          2, 44100, 16, 16, 0x3,        WF_SUB_PCM,   22, TW_AVG_ZERO,  Z, Z, Z, Z) \
    X(avgbytes_plus1,         2, 44100, 16, 16, 0x3,        WF_SUB_PCM,   22, TW_AVG_PLUS1, Z, Z, Z, Z) \
    X(float_avgbytes_zero,    2, 48000, 32, 32, 0x3,        WF_SUB_FLOAT, 22, TW_AVG_ZERO,  B, B, B, B)

typedef struct {
    const char *name; WORD ch; DWORD rate; WORD bits, valid; DWORD mask; const GUID *sub; WORD cb; int tweak; int exp[4];
} ext_case;

#define ROWX(nm, ch, rate, bits, valid, mask, sub, cb, tw, e0, e1, e2, e3) \
    { #nm, ch, rate, bits, valid, mask, &sub, cb, tw, { e0, e1, e2, e3 } },
static const ext_case k_cases[] = { EXT_CASES(ROWX) };

static void build(wf_ext *e, const ext_case *c) {
    wf_extfmt(e, c->ch, c->rate, c->bits, c->valid, c->mask, c->sub);
    e->cbSize = c->cb;
    switch (c->tweak) {
    case TW_BA_PLUS1:  e->nBlockAlign++; break;
    case TW_BA_ZERO:   e->nBlockAlign = 0; break;
    case TW_AVG_ZERO:  e->nAvgBytesPerSec = 0; break;
    case TW_AVG_PLUS1: e->nAvgBytesPerSec++; break;
    }
}

static void run_case(int dir, const char *name) {
    if (!U(waveOutOpen) || !U(waveInOpen)) { t_skip("missing open"); return; }
    const ext_case *c = NULL;
    for (unsigned i = 0; i < sizeof k_cases / sizeof *k_cases; i++) if (!strcmp(k_cases[i].name, name)) c = &k_cases[i];
    if (!c) { CHECKF(0, "no case %s", name); return; }
    wf_ext e; build(&e, c);
    char lab[96];
    static const struct { UINT id; DWORD fl; } tg[4] = {
        { WAVE_MAPPER, 0 }, { WAVE_MAPPER, WAVE_FORMAT_DIRECT }, { 0, 0 }, { 0, WAVE_FORMAT_DIRECT } };
    for (int t = 0; t < 4; t++) {
        sprintf(lab, "ext %s fl0x%lx", c->name, tg[t].fl);
        wf_cell(lab, dir, tg[t].id, &e, tg[t].fl, c->exp[t]);
    }
    /* Out-of-range id: always 2. */
    EXPECT_EQ(wf_query(dir, wf_ndev(dir), &e, 0), MMSYSERR_BADDEVICEID);
}

#define TX(nm, ...) TEST(wf_out_ext_##nm) { run_case(WF_OUT, #nm); } TEST(wf_in_ext_##nm) { run_case(WF_IN, #nm); }
EXT_CASES(TX)

/* The struct the corpus sends is exactly the SDK layout (cbSize 22 = 40 - 18). */
TEST(wf_ext_layout) {
    EXPECT_EQ(sizeof(WAVEFORMATEX), 18);
    EXPECT_EQ(offsetof(wf_ext, wValidBitsPerSample), 18);
    EXPECT_EQ(offsetof(wf_ext, dwChannelMask), 20);
    EXPECT_EQ(offsetof(wf_ext, SubFormat), 24);
    EXPECT_EQ(offsetof(wf_ext, pad), 40);
}

/* EXTENSIBLE PCM 16-bit stereo and the plain PCM tag get the same verdict at every core rate. */
static void ext_vs_plain(int dir, UINT id) {
    static const DWORD rates[] = { 8000, 11025, 22050, 32000, 44100, 48000 };
    for (unsigned i = 0; i < sizeof rates / sizeof *rates; i++) {
        wf_ext e; wf_extfmt(&e, 2, rates[i], 16, 16, 3, &WF_SUB_PCM);
        wf_fmt f; wf_pcm(&f, WAVE_FORMAT_PCM_, 2, rates[i], 16);
        MMRESULT a = wf_query(dir, id, &e, 0), b = wf_query(dir, id, &f, 0);
        CHECKF(a == b, "%s id %d rate %lu: ext %u plain %u", wf_dirname(dir), (int)id, rates[i], a, b);
        if (wf_pre_driver(dir, id) >= 0) EXPECT_EQ(a, MMSYSERR_BADDEVICEID);
    }
}
TEST(wf_out_ext_matches_plain_mapper) { REQUIRE(U(waveOutOpen)); ext_vs_plain(WF_OUT, WAVE_MAPPER); }
TEST(wf_out_ext_matches_plain_id0)    { REQUIRE(U(waveOutOpen)); ext_vs_plain(WF_OUT, 0); }
TEST(wf_in_ext_matches_plain_mapper)  { REQUIRE(U(waveInOpen)); ext_vs_plain(WF_IN, WAVE_MAPPER); }
TEST(wf_in_ext_matches_plain_id0)     { REQUIRE(U(waveInOpen)); ext_vs_plain(WF_IN, 0); }

/* The engine mix format of the HDA render endpoint (EXTENSIBLE float32 stereo 48 kHz, mask 3) is accepted
 * everywhere it can be: mapper and device 0, out and in. */
static void mixfmt(int dir) {
    wf_ext e; wf_extfmt(&e, 2, 48000, 32, 32, 3, &WF_SUB_FLOAT);
    wf_cell("ext mixformat", dir, WAVE_MAPPER, &e, 0, 0);
    wf_cell("ext mixformat", dir, 0, &e, 0, 0);
    wf_cell("ext mixformat", dir, 0xFFFF, &e, 0, 0);
}
TEST(wf_out_ext_mixformat) { REQUIRE(U(waveOutOpen)); mixfmt(WF_OUT); }
TEST(wf_in_ext_mixformat)  { REQUIRE(U(waveInOpen)); mixfmt(WF_IN); }
