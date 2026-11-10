/* wavein_capture: capture formats. WAVE_FORMAT_QUERY tables (mapper, device 0, direct) and a short real
 * capture per format. Format validation is the driver's / mapper's (WAVERR_BADFORMAT 32); winmmbase only
 * checks pwfx != NULL [decomp]. Device half only.
 */
#include "wic.h"

#define FIRST_MS 3000

typedef struct { const char *n; WORD tag; DWORD rate; WORD bits, ch; int ext; } frow;
#define PCM(r, b, c) {#r "_" #b "_" #c, WAVE_FORMAT_PCM, r, b, c, 0}
static const frow k_ok[] = {
    PCM(8000, 8, 1), PCM(8000, 8, 2), PCM(8000, 16, 1), PCM(8000, 16, 2),
    PCM(11025, 8, 1), PCM(11025, 16, 2), PCM(16000, 16, 1), PCM(16000, 16, 2),
    PCM(22050, 8, 2), PCM(22050, 16, 1), PCM(32000, 16, 2), PCM(44100, 8, 1),
    PCM(44100, 16, 1), PCM(44100, 16, 2), PCM(48000, 8, 2), PCM(48000, 16, 1),
    PCM(48000, 16, 2), PCM(88200, 16, 2), PCM(96000, 16, 1), PCM(96000, 16, 2),
    {"float44100_1", WAVE_FORMAT_IEEE_FLOAT, 44100, 32, 1, 0},
    {"float48000_2", WAVE_FORMAT_IEEE_FLOAT, 48000, 32, 2, 0},
    {"ext_pcm44100_16_2", WAVE_FORMAT_EXTENSIBLE, 44100, 16, 2, 1},
    {"ext_pcm48000_16_1", WAVE_FORMAT_EXTENSIBLE, 48000, 16, 1, 1},
    {"ext_float48000_2", WAVE_FORMAT_EXTENSIBLE, 48000, 32, 2, 2},
};
#define NOK (int)(sizeof k_ok / sizeof k_ok[0])

static void mk(const frow *r, WAVEFORMATEXTENSIBLE *f) {
    memset(f, 0, sizeof *f);
    if (r->ext) wic_ext(f, r->rate, r->bits, r->ch, r->ext == 2);
    else if (r->tag == WAVE_FORMAT_IEEE_FLOAT) wic_float(&f->Format, r->rate, r->ch);
    else wic_pcm(&f->Format, r->rate, r->bits, r->ch);
}

TEST(wic_fmt_query_mapper_table) {
    REQUIRE(U(waveInOpen)); NEED_DEV();
    for (int i = 0; i < NOK; i++) {
        WAVEFORMATEXTENSIBLE f; mk(&k_ok[i], &f);
        MMRESULT r = U(waveInOpen)(NULL, WAVE_MAPPER, &f.Format, 0, 0, WAVE_FORMAT_QUERY);
        CHECKF(r == 0, "mapper query %s -> %u", k_ok[i].n, r);
    }
}

TEST(wic_fmt_query_device0_table) {
    REQUIRE(U(waveInOpen)); NEED_DEV();
    for (int i = 0; i < NOK; i++) {
        WAVEFORMATEXTENSIBLE f; mk(&k_ok[i], &f);
        MMRESULT r = U(waveInOpen)(NULL, 0, &f.Format, 0, 0, WAVE_FORMAT_QUERY);
        CHECKF(r == 0, "device 0 query %s -> %u", k_ok[i].n, r);
    }
}

TEST(wic_fmt_query_direct_table) {
    REQUIRE(U(waveInOpen)); NEED_DEV();
    int ok = 0;
    for (int i = 0; i < NOK; i++) {
        WAVEFORMATEXTENSIBLE f; mk(&k_ok[i], &f);
        MMRESULT r = U(waveInOpen)(NULL, 0, &f.Format, 0, 0, WAVE_FORMAT_DIRECT_QUERY);
        CHECKF(r == 0 || r == WAVERR_BADFORMAT, "direct query %s -> %u", k_ok[i].n, r);
        if (r == 0) ok++; else t_note("direct query %s -> %u", k_ok[i].n, r);
    }
    t_note("WAVE_FORMAT_DIRECT_QUERY on device 0: %d of %d accepted", ok, NOK);
}

TEST(wic_fmt_query_mapper_direct_table) {
    REQUIRE(U(waveInOpen)); NEED_DEV();
    for (int i = 0; i < NOK; i++) {
        WAVEFORMATEXTENSIBLE f; mk(&k_ok[i], &f);
        MMRESULT r = U(waveInOpen)(NULL, WAVE_MAPPER, &f.Format, 0, 0, WAVE_FORMAT_DIRECT_QUERY);
        CHECKF(r == 0 || r == WAVERR_BADFORMAT, "mapper direct query %s -> %u", k_ok[i].n, r);
        if (r) t_note("mapper direct query %s -> %u", k_ok[i].n, r);
    }
}

/* QUERY never touches the handle slot and never creates a handle */
TEST(wic_fmt_query_leaves_handle) {
    REQUIRE(U(waveInOpen)); NEED_DEV();
    for (int i = 0; i < NOK; i += 4) {
        WAVEFORMATEXTENSIBLE f; mk(&k_ok[i], &f);
        HWAVEIN h = (HWAVEIN)(ULONG_PTR)0x5555;
        EXPECT_EQ(U(waveInOpen)(&h, WAVE_MAPPER, &f.Format, 0, 0, WAVE_FORMAT_QUERY), 0);
        EXPECT_EQ(h, (HWAVEIN)(ULONG_PTR)0x5555);
    }
}

typedef struct { const char *n; WAVEFORMATEXTENSIBLE f; } badrow;

static int bad_rows(badrow *b) {
    int k = 0;
    wic_pcm(&b[k].f.Format, 44100, 16, 1); b[k].f.Format.wFormatTag = 0x9999; b[k++].n = "unknown tag 0x9999";
    wic_pcm(&b[k].f.Format, 44100, 16, 6); b[k++].n = "pcm 6 channels";
    wic_pcm(&b[k].f.Format, 44100, 16, 1); b[k].f.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE; b[k].f.Format.cbSize = 0; b[k++].n = "extensible cbSize 0";
    return k;
}

TEST(wic_fmt_bad_format_query_and_open) {
    REQUIRE(U(waveInOpen)); NEED_DEV();
    badrow b[8]; memset(b, 0, sizeof b); int k = bad_rows(b);
    UINT ids[] = {WAVE_MAPPER, 0};
    for (int i = 0; i < k; i++)
        for (int j = 0; j < 2; j++) {
            MMRESULT q = U(waveInOpen)(NULL, ids[j], &b[i].f.Format, 0, 0, WAVE_FORMAT_QUERY);
            CHECKF(q == WAVERR_BADFORMAT, "query %s id %d -> %u", b[i].n, (int)ids[j], q);
            HWAVEIN h = (HWAVEIN)(ULONG_PTR)0x5555;
            HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
            MMRESULT r = U(waveInOpen)(&h, ids[j], &b[i].f.Format, (DWORD_PTR)ev, 0, CALLBACK_EVENT);
            CHECKF(r == WAVERR_BADFORMAT, "open %s id %d -> %u", b[i].n, (int)ids[j], r);
            CHECK(h == NULL);
            EXPECT_EQ(WaitForSingleObject(ev, 0), WAIT_TIMEOUT);
            if (r == 0 && h) U(waveInClose)(h);
            CloseHandle(ev);
        }
}

TEST(wic_fmt_odd_fields_observed) {
    REQUIRE(U(waveInOpen)); NEED_DEV();
    struct { const char *n; WAVEFORMATEX f; } t[6]; int k = 0;
    wic_pcm(&t[k].f, 44100, 16, 1); t[k].f.nChannels = 0; t[k++].n = "channels 0";
    wic_pcm(&t[k].f, 44100, 16, 1); t[k].f.nSamplesPerSec = 0; t[k++].n = "rate 0";
    wic_pcm(&t[k].f, 44100, 16, 1); t[k].f.wBitsPerSample = 12; t[k++].n = "bits 12";
    wic_pcm(&t[k].f, 44100, 16, 1); t[k].f.wBitsPerSample = 0; t[k++].n = "bits 0";
    wic_pcm(&t[k].f, 44100, 16, 2); t[k].f.nBlockAlign = 3; t[k++].n = "blockalign 3";
    wic_pcm(&t[k].f, 1000, 16, 1); t[k++].n = "rate 1000";
    for (int i = 0; i < k; i++) {
        MMRESULT m = U(waveInOpen)(NULL, WAVE_MAPPER, &t[i].f, 0, 0, WAVE_FORMAT_QUERY);
        MMRESULT d = U(waveInOpen)(NULL, 0, &t[i].f, 0, 0, WAVE_FORMAT_QUERY);
        t_note("query %s: mapper %u device0 %u", t[i].n, m, d);
        CHECK(m == 0 || m == WAVERR_BADFORMAT || m == MMSYSERR_INVALPARAM || m == MMSYSERR_NOTSUPPORTED || m == MMSYSERR_ERROR);
    }
}

/* ---- real capture per format: 2 x 40 ms, full buffers, aligned position ---- */

static void capture_fmt(const frow *r, UINT id) {
    WAVEFORMATEXTENSIBLE f; mk(r, &f);
    wic_s s;
    MMRESULT ro = wic_open(&s, id, &f.Format);
    CHECKF(ro == 0, "%s open id %d -> %u", r->n, (int)id, ro);
    if (ro) { wic_teardown(&s); return; }
    DWORD len = wic_bytes_ms(&f.Format, 40);
    for (int i = 0; i < 2; i++) { EXPECT_EQ(wic_buf(&s, i, len), 0); EXPECT_EQ(wic_add(&s, i), 0); }
    EXPECT_EQ(U(waveInStart)(s.h), 0);
    int ok = wic_wait_all(&s, 0, 2, FIRST_MS + 500);
    CHECKF(ok, "%s: buffers not done", r->n);
    EXPECT_EQ(U(waveInStop)(s.h), 0);
    for (int i = 0; i < 2 && ok; i++) {
        CHECKF(s.hdr[i].dwFlags == 3, "%s buffer %d flags 0x%lx", r->n, i, s.hdr[i].dwFlags);
        CHECKF(s.hdr[i].dwBytesRecorded == len, "%s buffer %d recorded %lu of %lu", r->n, i, s.hdr[i].dwBytesRecorded, len);
    }
    DWORD b = wic_pos(s.h, TIME_BYTES, NULL), sm = wic_pos(s.h, TIME_SAMPLES, NULL);
    CHECKF(b >= 2 * len && b % f.Format.nBlockAlign == 0, "%s pos %lu", r->n, b);
    CHECKF(sm * f.Format.nBlockAlign == b, "%s samples %lu bytes %lu", r->n, sm, b);
    if (ok && r->tag == WAVE_FORMAT_PCM && r->bits == 8) {
        unsigned char lo = 255, hi = 0;
        for (DWORD i = 0; i < len; i++) { unsigned char v = (unsigned char)s.mem[0][i]; if (v < lo) lo = v; if (v > hi) hi = v; }
        t_note("%s 8-bit capture range [%u, %u] (silence is 128)", r->n, lo, hi);
    }
    EXPECT_EQ(wic_teardown(&s), 0);
}

#define CTEST(i) TEST(wic_fmt_capture_mapper_##i) { REQUIRE(U(waveInOpen)); NEED_DEV(); capture_fmt(&k_ok[i], WAVE_MAPPER); }
CTEST(0) CTEST(1) CTEST(2) CTEST(3) CTEST(4) CTEST(5) CTEST(6) CTEST(7) CTEST(8) CTEST(9)
CTEST(10) CTEST(11) CTEST(12) CTEST(13) CTEST(14) CTEST(15) CTEST(16) CTEST(17) CTEST(18) CTEST(19)
CTEST(20) CTEST(21) CTEST(22) CTEST(23) CTEST(24)

#define DTEST(i) TEST(wic_fmt_capture_dev0_##i) { REQUIRE(U(waveInOpen)); NEED_DEV(); capture_fmt(&k_ok[i], 0); }
DTEST(0) DTEST(12) DTEST(13) DTEST(16) DTEST(21) DTEST(24)

/* the spec'd trio, named */
TEST(wic_fmt_capture_8000_8_1_named) {
    REQUIRE(U(waveInOpen)); NEED_DEV();
    WAVEFORMATEX f; wic_pcm(&f, 8000, 8, 1);
    EXPECT_EQ(f.nBlockAlign, 1); EXPECT_EQ(f.nAvgBytesPerSec, 8000);
    capture_fmt(&k_ok[0], WAVE_MAPPER);
}
TEST(wic_fmt_capture_48000_16_2_named) {
    REQUIRE(U(waveInOpen)); NEED_DEV();
    capture_fmt(&k_ok[16], WAVE_MAPPER);
}
TEST(wic_fmt_capture_float_mapper_named) {
    REQUIRE(U(waveInOpen)); NEED_DEV();
    WAVEFORMATEX f; wic_float(&f, 48000, 2);
    EXPECT_EQ(f.nBlockAlign, 8); EXPECT_EQ(f.nAvgBytesPerSec, 384000);
    capture_fmt(&k_ok[21], WAVE_MAPPER);
}

TEST(wic_fmt_float_samples_in_range) {
    REQUIRE(U(waveInOpen)); NEED_DEV();
    WAVEFORMATEX f; wic_float(&f, 48000, 2);
    wic_s s; if (wic_open(&s, WAVE_MAPPER, &f)) { CHECK(!"open"); wic_teardown(&s); return; }
    DWORD len = wic_bytes_ms(&f, 40);
    EXPECT_EQ(wic_buf(&s, 0, len), 0); EXPECT_EQ(wic_add(&s, 0), 0);
    EXPECT_EQ(U(waveInStart)(s.h), 0);
    if (wic_wait_done(&s, 0, FIRST_MS)) {
        int bad = 0; float *p = (float *)s.mem[0];
        for (DWORD i = 0; i < len / 4; i++) if (!(p[i] >= -1.01f && p[i] <= 1.01f)) bad++;
        t_note("float capture: %d of %lu samples outside [-1,1] or NaN", bad, len / 4);
        EXPECT_EQ(bad, 0);   /* the engine writes real float samples, never the 0xCD fill */
    } else CHECK(!"not done");
    EXPECT_EQ(wic_teardown(&s), 0);
}

/* one stream per format reopened back to back: open/close cycles leave no state behind */
TEST(wic_fmt_reopen_cycle) {
    REQUIRE(U(waveInOpen)); NEED_DEV();
    for (int c = 0; c < 6; c++) {
        const frow *r = &k_ok[(c * 5) % NOK];
        WAVEFORMATEXTENSIBLE f; mk(r, &f);
        wic_s s;
        CHECKF(wic_open(&s, WAVE_MAPPER, &f.Format) == 0, "cycle %d %s", c, r->n);
        EXPECT_EQ(wic_teardown(&s), 0);
    }
}
