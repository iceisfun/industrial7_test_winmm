/* playsound_device: WAVE image parsing (winmm soundInitWavHdr) through synchronous and looping async plays.
 *
 * soundInitWavHdr(hdr, image, size) as reached from SND_MEMORY (size = 0xFFFFFFFF, i.e. no length bound):
 *  - 'RIFF' at 0 and 'WAVE' at 8, else fail.
 *  - Walk chunks from offset 12 for "fmt "; after skipping a chunk, next offset >= RIFF length -> fail.
 *  - fmt cb < 14 -> fail. cb 14/15 are used as is, cb 16/17 as 16. cb >= 18: cbSize <= 0x3FF and cb == 18 + cbSize
 *    exactly; for tag 1 (PCM) / 3 (IEEE float) also cbSize == 0, wBitsPerSample % 8 == 0, nChannels <= 2 and
 *    nAvgBytesPerSec == nSamplesPerSec * nChannels * wBitsPerSample / 8.
 *  - Any cb: nChannels, nSamplesPerSec, nAvgBytesPerSec, nBlockAlign all nonzero.
 *  - Tag 0xFFFE: cb >= 40, wValidBitsPerSample != 0 and <= wBitsPerSample; PCM/float subformat -> the avg rule.
 *  - Then walk on from the end of fmt for "data" (same skip rule; a data chunk right after fmt needs no skip);
 *    the data length is NOT checked against the RIFF length (memory images).
 *  - The fmt body is moved down in place to a 4-byte boundary (memmove) when it is misaligned.
 * The device then decides (waveOutOpen(WAVE_MAPPER)) for formats the parser lets through.
 */
#include "ps.h"

#define DEV (ps_ndev() > 0)

enum { SUB_PCM, SUB_FLOAT, SUB_BOGUS };
#define AUTO 0xFFFFFFFFu
typedef struct {
    const char *name;
    DWORD cb; WORD tag, ch; DWORD rate, avg; DWORD align; WORD bits, cbsize;
    int ext; WORD valid; DWORD mask; int sub;
    BYTE fill;
    int exp;      /* 1: plays with a device; 0: rejected (parser or device) */
} fcase;

static const fcase g_f[] = {
    /* name                         cb  tag  ch rate   avg   align bits cbs ext valid mask sub fill exp */
    { "pcm_cb16",                   16, 1, 1, 8000,  AUTO, AUTO, 16, 0, 0, 0, 0, 0, 0x00, 1 },
    { "pcm_cb18_cbsize0",           18, 1, 1, 8000,  AUTO, AUTO, 16, 0, 0, 0, 0, 0, 0x00, 1 },
    { "pcm_cb17",                   17, 1, 1, 8000,  AUTO, AUTO, 16, 0, 0, 0, 0, 0, 0x00, 1 },
    { "pcm_cb15_bits_lowbyte",      15, 1, 1, 8000,  AUTO, AUTO, 16, 0, 0, 0, 0, 0, 0x00, 1 },
    { "pcm_cb14_no_bits",           14, 1, 1, 8000,  AUTO, AUTO, 16, 0, 0, 0, 0, 0, 0x00, 0 },
    { "pcm_cb13",                   13, 1, 1, 8000,  AUTO, AUTO, 16, 0, 0, 0, 0, 0, 0x00, 0 },
    { "pcm_cb0",                     0, 1, 1, 8000,  AUTO, AUTO, 16, 0, 0, 0, 0, 0, 0x00, 0 },
    { "pcm_cb18_cbsize2",           18, 1, 1, 8000,  AUTO, AUTO, 16, 2, 0, 0, 0, 0, 0x00, 0 },
    { "pcm_cb20_cbsize2",           20, 1, 1, 8000,  AUTO, AUTO, 16, 2, 0, 0, 0, 0, 0x00, 0 },
    { "pcm_cb18_bits12",            18, 1, 1, 8000,  12000, 2,  12, 0, 0, 0, 0, 0, 0x00, 0 },
    { "pcm_cb16_bits12",            16, 1, 1, 8000,  12000, 2,  12, 0, 0, 0, 0, 0, 0x00, 0 },
    { "pcm_cb18_ch3",               18, 1, 3, 8000,  AUTO, AUTO, 16, 0, 0, 0, 0, 0, 0x00, 0 },
    { "pcm_cb16_ch4",               16, 1, 4, 8000,  AUTO, AUTO, 16, 0, 0, 0, 0, 0, 0x00, 0 },
    { "pcm_cb16_ch6",               16, 1, 6, 48000, AUTO, AUTO, 16, 0, 0, 0, 0, 0, 0x00, 0 },
    { "pcm_cb18_avg_wrong",         18, 1, 1, 8000,  16001, 2,  16, 0, 0, 0, 0, 0, 0x00, 0 },
    { "pcm_cb16_avg_wrong",         16, 1, 1, 8000,  16001, 2,  16, 0, 0, 0, 0, 0, 0x00, 1 },
    { "pcm_cb16_ch0",               16, 1, 0, 8000,  16000, 2,  16, 0, 0, 0, 0, 0, 0x00, 0 },
    { "pcm_cb16_rate0",             16, 1, 1, 0,     16000, 2,  16, 0, 0, 0, 0, 0, 0x00, 0 },
    { "pcm_cb16_avg0",              16, 1, 1, 8000,  0,     2,  16, 0, 0, 0, 0, 0, 0x00, 0 },
    { "pcm_cb16_align0",            16, 1, 1, 8000,  16000, 0,  16, 0, 0, 0, 0, 0, 0x00, 0 },
    { "pcm_cb16_bits0",             16, 1, 1, 8000,  16000, 2,   0, 0, 0, 0, 0, 0, 0x00, 0 },
    { "pcm8_stereo_44100_cb18",     18, 1, 2, 44100, AUTO, AUTO,  8, 0, 0, 0, 0, 0, 0x80, 1 },
    { "pcm8_mono_11025_cb16",       16, 1, 1, 11025, AUTO, AUTO,  8, 0, 0, 0, 0, 0, 0x80, 1 },
    { "pcm16_stereo_44100_cb16",    16, 1, 2, 44100, AUTO, AUTO, 16, 0, 0, 0, 0, 0, 0x00, 1 },
    { "float32_stereo_cb18",        18, 3, 2, 44100, AUTO, AUTO, 32, 0, 0, 0, 0, 0, 0x00, 1 },
    { "float32_mono_cb16",          16, 3, 1, 48000, AUTO, AUTO, 32, 0, 0, 0, 0, 0, 0x00, 1 },
    { "float32_cb18_avg_wrong",     18, 3, 2, 44100, 1000,  8,  32, 0, 0, 0, 0, 0, 0x00, 0 },
    { "float64_cb18",               18, 3, 1, 48000, AUTO, AUTO, 64, 0, 0, 0, 0, 0, 0x00, 0 },
    { "float24_cb18",               18, 3, 1, 48000, AUTO, AUTO, 24, 0, 0, 0, 0, 0, 0x00, 0 },
    { "float32_cb18_ch3",           18, 3, 3, 48000, AUTO, AUTO, 32, 0, 0, 0, 0, 0, 0x00, 0 },
    { "ext_pcm16_stereo",           40, 0xFFFE, 2, 44100, AUTO, AUTO, 16, 22, 1, 16, 3, SUB_PCM, 0x00, 1 },
    { "ext_pcm16_mono_center",      40, 0xFFFE, 1, 48000, AUTO, AUTO, 16, 22, 1, 16, 4, SUB_PCM, 0x00, 1 },
    { "ext_pcm24_in32",             40, 0xFFFE, 2, 48000, AUTO, AUTO, 32, 22, 1, 24, 3, SUB_PCM, 0x00, 1 },
    { "ext_float32_stereo",         40, 0xFFFE, 2, 48000, AUTO, AUTO, 32, 22, 1, 32, 3, SUB_FLOAT, 0x00, 1 },
    { "ext_cb18_cbsize0",           18, 0xFFFE, 2, 44100, AUTO, AUTO, 16, 0, 0, 0, 0, 0, 0x00, 0 },
    { "ext_valid0",                 40, 0xFFFE, 2, 44100, AUTO, AUTO, 16, 22, 1, 0, 3, SUB_PCM, 0x00, 0 },
    { "ext_valid_gt_bits",          40, 0xFFFE, 2, 44100, AUTO, AUTO, 16, 22, 1, 24, 3, SUB_PCM, 0x00, 0 },
    { "ext_pcm_avg_wrong",          40, 0xFFFE, 2, 44100, 1234, AUTO, 16, 22, 1, 16, 3, SUB_PCM, 0x00, 0 },
    { "ext_float_avg_wrong",        40, 0xFFFE, 2, 44100, 1234, AUTO, 32, 22, 1, 32, 3, SUB_FLOAT, 0x00, 0 },
    { "ext_bogus_subformat",        40, 0xFFFE, 2, 44100, AUTO, AUTO, 16, 22, 1, 16, 3, SUB_BOGUS, 0x00, 0 },
    { "ext_cb40_cbsize0",           40, 0xFFFE, 2, 44100, AUTO, AUTO, 16, 0, 1, 16, 3, SUB_PCM, 0x00, 0 },
    { "ext_6ch",                    40, 0xFFFE, 6, 48000, AUTO, AUTO, 16, 22, 1, 16, 0x3F, SUB_PCM, 0x00, 0 },
    { "unknown_tag_cb18",           18, 0x1234, 1, 8000, 16000, 2,  16, 0, 0, 0, 0, 0, 0x00, 0 },
    { "cbsize_0x400",            0x412, 0x1234, 1, 8000, 16000, 2,  16, 0x400, 0, 0, 0, 0, 0x00, 0 },
    { "cbsize_0x3ff_unknown",    0x411, 0x1234, 1, 8000, 16000, 2,  16, 0x3FF, 0, 0, 0, 0, 0x00, 0 },
    { "alaw_cb18",                  18, 6, 1, 8000,  8000,  1,   8, 0, 0, 0, 0, 0, 0xD5, 1 },
    { "mulaw_cb18",                 18, 7, 1, 8000,  8000,  1,   8, 0, 0, 0, 0, 0, 0xFF, 1 },
    { "alaw_cb16",                  16, 6, 1, 8000,  8000,  1,   8, 0, 0, 0, 0, 0, 0xD5, 1 },
};
#define NF ((int)(sizeof g_f / sizeof g_f[0]))

static void build_case(wb_t *w, const fcase *c) {
    wb_init(w, 0x4000 + 0x500);
    DWORD align = c->align == AUTO ? (DWORD)(c->ch * c->bits / 8) : c->align;
    DWORD avg = c->avg == AUTO ? c->rate * align : c->avg;
    if (c->ext) {
        BYTE ex[22]; const GUID *g = c->sub == SUB_PCM ? &ps_sub_pcm : c->sub == SUB_FLOAT ? &ps_sub_float : &ps_sub_bogus;
        memcpy(ex, &c->valid, 2); memcpy(ex + 2, &c->mask, 4); memcpy(ex + 6, g, 16);
        wb_fmt_raw(w, c->cb, c->tag, c->ch, c->rate, avg, (WORD)align, c->bits, c->cbsize, ex, 22);
    } else {
        wb_fmt_raw(w, c->cb, c->tag, c->ch, c->rate, avg, (WORD)align, c->bits, c->cbsize, NULL, 0);
    }
    wb_data(w, 0x0C00, c->fill);   /* 3072 bytes: 192 ms at 8000 Hz 16-bit mono, less for wider formats */
    wb_finish(w);
}

/* Rows whose outcome depends on what the wave mapper / driver accepts, not on the winmm parser: note only. */
static int soft_row(const fcase *c) {
    return !strcmp(c->name, "pcm_cb16_ch4") || !strcmp(c->name, "pcm_cb16_ch6") || !strcmp(c->name, "ext_6ch")
        || !strcmp(c->name, "float64_cb18") || !strcmp(c->name, "float24_cb18");
}

static void run_sync_case(int i) {
    const fcase *c = &g_f[i]; wb_t w; double ms;
    build_case(&w, c);
    BOOL r = ps_timed(psW, w.b, PS_MEMSYNC, &ms);
    t_note("fmt %s sync: r=%d %.0f ms", c->name, r, ms);
    if (DEV && soft_row(c)) { if (r != (BOOL)c->exp) t_note("%s: sync got %d, expected %d (mapper-dependent, not enforced)", c->name, r, c->exp); }
    else if (DEV) CHECKF(r == (BOOL)c->exp, "%s: sync got %d want %d", c->name, r, c->exp);
    else CHECKF(r == FALSE, "%s: sync got %d with 0 devices", c->name, r);
    CHECKF(ms < 3000.0, "%s: %.0f ms", c->name, ms);
    CHECK(ps_playing() == 0);
    wb_free(&w);
}
static void run_async_case(int i) {
    const fcase *c = &g_f[i]; wb_t w;
    build_case(&w, c);
    BOOL r = psW(w.b, PS_MEMLOOP);
    CHECKF(r == TRUE, "%s: async post got %d", c->name, r);     /* posted before any parsing */
    if (DEV) {
        int p = ps_wait_playing(1, c->exp ? 2000 : 400);
        t_note("fmt %s async loop: playing=%d", c->name, p);
        if (soft_row(c)) { if (p != c->exp) t_note("%s: async playing=%d, expected %d (mapper-dependent, not enforced)", c->name, p, c->exp); }
        else CHECKF(p == c->exp, "%s: async playing=%d want %d", c->name, p, c->exp);
    } else { Sleep(100); CHECK(ps_playing() == 0); }
    ps_stop();
    CHECK(ps_playing() == 0);
    wb_free(&w);
}

#define FS(i) TEST(psd_fmt_sync_##i) { PS_REQ(); if (i >= NF) return; run_sync_case(i); ps_stop(); } \
              TEST(psd_fmt_async_##i) { PS_REQ(); if (i >= NF) return; run_async_case(i); ps_stop(); }
FS(0) FS(1) FS(2) FS(3) FS(4) FS(5) FS(6) FS(7) FS(8) FS(9)
FS(10) FS(11) FS(12) FS(13) FS(14) FS(15) FS(16) FS(17) FS(18) FS(19)
FS(20) FS(21) FS(22) FS(23) FS(24) FS(25) FS(26) FS(27) FS(28) FS(29)
FS(30) FS(31) FS(32) FS(33) FS(34) FS(35) FS(36) FS(37) FS(38) FS(39)
FS(40) FS(41) FS(42) FS(43) FS(44) FS(45) FS(46) FS(47) FS(48)

TEST(psd_fmt_table_ansi_parity) {
    PS_REQ();
    /* PlaySoundA and sndPlaySoundW give the same answer as PlaySoundW for every parse-rejected image. */
    for (int i = 0; i < NF; i++) {
        if (g_f[i].exp || soft_row(&g_f[i])) continue;
        wb_t w; build_case(&w, &g_f[i]);
        BOOL a = psA(w.b, PS_MEMSYNC), b = spsW(w.b, PS_MEMSYNC), c = spsA(w.b, PS_MEMSYNC);
        CHECKF(a == FALSE && b == FALSE && c == FALSE, "%s: A=%d sndW=%d sndA=%d", g_f[i].name, a, b, c);
        wb_free(&w);
    }
    ps_stop();
}

/* ---- chunk layout ----------------------------------------------------------- */
enum { L_NOFMT, L_NODATA, L_DATA_BEFORE_FMT, L_LIST_BEFORE_FMT, L_LIST_BETWEEN, L_FACT_FLOAT, L_TWO_FMT, L_TWO_DATA,
       L_RIFF4_CONTIG, L_RIFF4_LIST, L_EMPTY_DATA_AFTER_SKIP, L_EMPTY_DATA_CONTIG, L_HUGE_SKIP, L_FMT_CASE, L_JUNK1,
       L_JUNK2, L_JUNK3, L_ODD_DATA, L_WAVE_BAD, L_RIFX, L_RIFF_SHORT_BY_ONE_SKIP, L_RIFF_HUGE, L_DATA_LEN_PAD, L_N };
typedef struct { const char *name; int exp; int async_ok; } lcase;
static const lcase g_l[L_N] = {
    [L_NOFMT]               = { "no_fmt",                  0, 0 },
    [L_NODATA]              = { "no_data",                 0, 0 },
    [L_DATA_BEFORE_FMT]     = { "data_before_fmt",         0, 0 },
    [L_LIST_BEFORE_FMT]     = { "list_before_fmt",         1, 1 },
    [L_LIST_BETWEEN]        = { "list_between_fmt_data",   1, 1 },
    [L_FACT_FLOAT]          = { "fact_chunk_float",        1, 1 },
    [L_TWO_FMT]             = { "two_fmt_first_wins",      1, 1 },
    [L_TWO_DATA]            = { "two_data",                1, 1 },
    [L_RIFF4_CONTIG]        = { "riff_len4_contiguous",    1, 0 },
    [L_RIFF4_LIST]          = { "riff_len4_list_between",  0, 0 },
    [L_EMPTY_DATA_AFTER_SKIP] = { "empty_data_after_skip", 0, 0 },
    [L_EMPTY_DATA_CONTIG]   = { "empty_data_contiguous",   1, 0 },
    [L_HUGE_SKIP]           = { "huge_skipped_chunk",      0, 1 },
    [L_FMT_CASE]            = { "fmt_id_uppercase",        0, 0 },
    [L_JUNK1]               = { "junk1_misaligned_fmt",    1, 1 },
    [L_JUNK2]               = { "junk2_misaligned_fmt",    1, 1 },
    [L_JUNK3]               = { "junk3_misaligned_fmt",    1, 1 },
    [L_ODD_DATA]            = { "odd_data_len_8bit",       1, 1 },
    [L_WAVE_BAD]            = { "wave_magic_bad",          0, 0 },
    [L_RIFX]                = { "rifx_magic",              0, 0 },
    [L_RIFF_SHORT_BY_ONE_SKIP] = { "riff_ends_inside_skip", 0, 0 },
    [L_RIFF_HUGE]           = { "riff_len_beyond_image",   1, 0 },
    [L_DATA_LEN_PAD]        = { "data_len_beyond_riff",    1, 0 },
};

static void build_layout(wb_t *w, int k) {
    wb_init_magic(w, 0x8000, k == L_RIFX ? "RIFX" : "RIFF", k == L_WAVE_BAD ? "WAVX" : "WAVE");
    const DWORD D = 1600;   /* 100 ms at 8000 Hz 16-bit mono */
    switch (k) {
    case L_NOFMT: wb_data(w, D, 0); wb_finish(w); break;
    case L_NODATA: wb_fmt(w, 1, 1, 8000, 16); wb_finish(w); break;
    case L_DATA_BEFORE_FMT: wb_data(w, D, 0); wb_fmt(w, 1, 1, 8000, 16); wb_finish(w); break;
    case L_LIST_BEFORE_FMT: wb_chunk(w, "LIST", 12); wb_raw(w, "INFOISFT\0\0\0\0", 12); wb_fmt(w, 1, 1, 8000, 16); wb_data(w, D, 0); wb_finish(w); break;
    case L_LIST_BETWEEN: wb_fmt(w, 1, 1, 8000, 16); wb_chunk(w, "LIST", 4); wb_raw(w, "INFO", 4); wb_data(w, D, 0); wb_finish(w); break;
    case L_FACT_FLOAT: wb_fmt_raw(w, 18, 3, 1, 8000, 32000, 4, 32, 0, NULL, 0); wb_chunk(w, "fact", 4); wb_u32(w, 800); wb_data(w, 3200, 0); wb_finish(w); break;
    case L_TWO_FMT: wb_fmt(w, 1, 1, 8000, 16); wb_fmt_raw(w, 16, 0x1234, 0, 0, 0, 0, 0, 0, NULL, 0); wb_data(w, D, 0); wb_finish(w); break;
    case L_TWO_DATA: wb_fmt(w, 1, 1, 8000, 16); wb_data(w, D, 0); wb_data(w, D, 0); wb_finish(w); break;
    case L_RIFF4_CONTIG: wb_fmt(w, 1, 1, 8000, 16); wb_data(w, D, 0); wb_set_riff(w, 4); break;
    case L_RIFF4_LIST: wb_fmt(w, 1, 1, 8000, 16); wb_chunk(w, "LIST", 4); wb_raw(w, "INFO", 4); wb_data(w, D, 0); wb_set_riff(w, 4); break;
    case L_EMPTY_DATA_AFTER_SKIP: wb_fmt(w, 1, 1, 8000, 16); wb_chunk(w, "LIST", 4); wb_raw(w, "INFO", 4); wb_data(w, 0, 0); wb_finish(w); break;
    case L_EMPTY_DATA_CONTIG: wb_fmt(w, 1, 1, 8000, 16); wb_data(w, 0, 0); wb_finish(w); break;
    case L_HUGE_SKIP: wb_chunk(w, "LIST", 0xFFFFFF00); wb_fmt(w, 1, 1, 8000, 16); wb_data(w, D, 0); wb_finish(w); break;
    case L_FMT_CASE: wb_chunk(w, "FMT ", 16); wb_u16(w, 1); wb_u16(w, 1); wb_u32(w, 8000); wb_u32(w, 16000); wb_u16(w, 2); wb_u16(w, 16); wb_data(w, D, 0); wb_finish(w); break;
    case L_JUNK1: wb_chunk(w, "JUNK", 1); wb_fill(w, 0x11, 1); wb_fmt(w, 1, 1, 8000, 16); wb_data(w, D, 0); wb_finish(w); break;
    case L_JUNK2: wb_chunk(w, "JUNK", 2); wb_fill(w, 0x22, 2); wb_fmt(w, 1, 1, 8000, 16); wb_data(w, D, 0); wb_finish(w); break;
    case L_JUNK3: wb_chunk(w, "JUNK", 3); wb_fill(w, 0x33, 3); wb_fmt(w, 1, 1, 8000, 16); wb_data(w, D, 0); wb_finish(w); break;
    case L_ODD_DATA: wb_fmt(w, 1, 1, 8000, 8); wb_data(w, 801, 0x80); wb_finish(w); break;
    case L_WAVE_BAD: case L_RIFX: wb_fmt(w, 1, 1, 8000, 16); wb_data(w, D, 0); wb_finish(w); break;
    case L_RIFF_SHORT_BY_ONE_SKIP:
        /* fmt, then a 4-byte LIST that ends exactly at RIFF length: the data header starts at offset == RIFF length. */
        wb_fmt(w, 1, 1, 8000, 16); wb_chunk(w, "LIST", 4); wb_raw(w, "INFO", 4);
        { DWORD at = w->n; wb_data(w, D, 0); wb_set_riff(w, at); } break;
    case L_RIFF_HUGE: wb_fmt(w, 1, 1, 8000, 16); wb_data(w, D, 0); wb_set_riff(w, 0x7FFFFFF0); break;
    case L_DATA_LEN_PAD:
        /* data claims 4x the bytes the RIFF length covers; the buffer has zeros there (no clamp, no failure). */
        wb_fmt(w, 1, 1, 8000, 16); wb_chunk(w, "data", 4 * D); wb_fill(w, 0, D); wb_finish(w); break;
    }
}

static void run_layout(int k, int async) {
    wb_t w; build_layout(&w, k);
    const lcase *c = &g_l[k];
    if (!async) {
        double ms; BOOL r = ps_timed(psW, w.b, PS_MEMSYNC, &ms);
        t_note("layout %s sync: r=%d %.0f ms", c->name, r, ms);
        if (DEV) CHECKF(r == (BOOL)c->exp, "%s: got %d want %d", c->name, r, c->exp);
        else CHECKF(r == FALSE, "%s: got %d with 0 devices", c->name, r);
        CHECKF(ms < 3000.0, "%s: %.0f ms", c->name, ms);
    } else {
        BOOL gate = (k != L_WAVE_BAD && k != L_RIFX);
        BOOL r = psW(w.b, PS_MEMLOOP);
        CHECKF(r == gate, "%s: async post got %d want %d", c->name, r, gate);
        if (DEV && r) {
            int p = ps_wait_playing(1, c->exp ? 2000 : 400);
            t_note("layout %s async loop: playing=%d", c->name, p);
            CHECKF(p == c->exp, "%s: async playing=%d want %d", c->name, p, c->exp);
        }
    }
    ps_stop();
    CHECK(ps_playing() == 0);
    wb_free(&w);
}
#define LS(k) TEST(psd_layout_sync_##k) { PS_REQ(); run_layout(k, 0); }
#define LA(k) TEST(psd_layout_async_##k) { PS_REQ(); if (!g_l[k].async_ok && g_l[k].exp) return; run_layout(k, 1); }
LS(L_NOFMT) LS(L_NODATA) LS(L_DATA_BEFORE_FMT) LS(L_LIST_BEFORE_FMT) LS(L_LIST_BETWEEN) LS(L_FACT_FLOAT) LS(L_TWO_FMT)
LS(L_TWO_DATA) LS(L_RIFF4_CONTIG) LS(L_RIFF4_LIST) LS(L_EMPTY_DATA_AFTER_SKIP) LS(L_EMPTY_DATA_CONTIG) LS(L_HUGE_SKIP)
LS(L_FMT_CASE) LS(L_JUNK1) LS(L_JUNK2) LS(L_JUNK3) LS(L_ODD_DATA) LS(L_WAVE_BAD) LS(L_RIFX) LS(L_RIFF_SHORT_BY_ONE_SKIP)
LS(L_RIFF_HUGE) LS(L_DATA_LEN_PAD)
/* Async only where the copy (RIFF length + 8 bytes) holds every byte the parser reads. */
LA(L_LIST_BEFORE_FMT) LA(L_LIST_BETWEEN) LA(L_FACT_FLOAT) LA(L_TWO_FMT) LA(L_TWO_DATA) LA(L_HUGE_SKIP) LA(L_JUNK1)
LA(L_JUNK2) LA(L_JUNK3) LA(L_ODD_DATA) LA(L_WAVE_BAD) LA(L_RIFX)

TEST(psd_layout_data_len_beyond_riff_not_clamped) {
    PS_REQ();
    if (!DEV) { t_skip("needs a wave device"); return; }
    /* RIFF length covers 50 ms of data, the data header claims 500 ms: the sync play lasts the header's length. */
    wb_t w; wb_init(&w, 0x4000);
    wb_fmt(&w, 1, 1, 8000, 16); wb_chunk(&w, "data", 8000); wb_fill(&w, 0, 800); wb_finish(&w);
    double ms; BOOL r = ps_timed(psW, w.b, PS_MEMSYNC, &ms);
    t_note("data 8000 bytes claimed, RIFF covers 800: r=%d %.0f ms", r, ms);
    CHECK(r == TRUE);
    CHECKF(ms >= 350.0 && ms < 3000.0, "%.0f ms (clamped to RIFF length would be ~50 ms)", ms);
    ps_stop(); wb_free(&w);
}

TEST(psd_layout_misaligned_fmt_moved_in_place) {
    PS_REQ();
    /* JUNK(2) puts the fmt body at offset 30; the parser memmoves its 16 bytes to offset 28 in the caller's image.
       This happens while parsing, before the device open: with and without devices. */
    wb_t w; build_layout(&w, L_JUNK2);
    BYTE body[16]; memcpy(body, w.b + 30, 16);
    BYTE tail[2]; memcpy(tail, w.b + 44, 2);
    BOOL r = psW(w.b, PS_MEMSYNC);
    if (DEV) CHECK(r == TRUE); else CHECK(r == FALSE);
    CHECKF(memcmp(w.b + 28, body, 16) == 0, "fmt body not moved to offset 28");
    CHECKF(memcmp(w.b + 44, tail, 2) == 0, "bytes after the moved body changed");
    DWORD fmtlen; memcpy(&fmtlen, w.b + 26, 4);
    EXPECT_EQ(fmtlen, 0x00010010);   /* the old fmt length's high half now holds wFormatTag = 1 */
    ps_stop(); wb_free(&w);
}

TEST(psd_layout_misaligned_fmt_second_play) {
    PS_REQ();
    /* After the in-place move the fmt length reads 0x10010, so a second play of the same buffer parses differently. */
    wb_t w; build_layout(&w, L_JUNK2);
    BOOL r1 = psW(w.b, PS_MEMSYNC);
    BOOL r2 = psW(w.b, PS_MEMSYNC);
    t_note("misaligned image played twice: %d then %d", r1, r2);
    if (DEV) CHECK(r1 == TRUE); else CHECK(r1 == FALSE);
    CHECK(r2 == FALSE);
    ps_stop(); wb_free(&w);
}

TEST(psd_layout_misaligned_fmt_odd_offsets) {
    PS_REQ();
    for (int k = L_JUNK1; k <= L_JUNK3; k++) {
        wb_t w; build_layout(&w, k);
        DWORD off = 12 + 8 + (DWORD)(k - L_JUNK1 + 1) + 8;     /* fmt body offset */
        DWORD to = off & ~3u;
        BYTE body[16]; memcpy(body, w.b + off, 16);
        BOOL r = psW(w.b, PS_MEMSYNC);
        if (DEV) CHECK(r == TRUE); else CHECK(r == FALSE);
        CHECKF(memcmp(w.b + to, body, 16) == 0, "%s: body not at %lu", g_l[k].name, to);
        wb_free(&w);
    }
    ps_stop();
}
