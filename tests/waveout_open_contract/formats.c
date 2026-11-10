#include "wo.h"
/* Formats at open time. winmmbase does not validate WAVEFORMATEX; the driver (and, through the mapper, ACM)
 * answers. Each row pins four answers observed on the Win10 VM (HDA codec, shared-mode engine 48 kHz float):
 * WAVE_FORMAT_QUERY through the mapper and on device 0, and a real open through both. A QUERY and a real open do
 * NOT always agree: the query path accepts formats the real open refuses (bits 64, nBlockAlign 0/3, rate
 * 1000000 on the device), and a refused real open on a device id answers 11 where the query answers 32.
 * A failed real open always leaves *phwo NULL. With no device every row is 2. */

#define B WAVERR_BADFORMAT
#define P MMSYSERR_INVALPARAM
typedef struct { const char *name; WAVEFORMATEX f; UINT qm, q0, om, o0; } frow;

static void build_rows(frow *r, int *n) {
    int k = 0;
#define ROW(nm, ch, rate, bits, a, b, c, d) do { r[k].name = nm; wo_pcm(&r[k].f, ch, rate, bits); \
    r[k].qm = a; r[k].q0 = b; r[k].om = c; r[k].o0 = d; k++; } while (0)
    ROW("pcm 8000/8/1", 1, 8000, 8,          0, 0, 0, 0);
    ROW("pcm 11025/8/1", 1, 11025, 8,        0, 0, 0, 0);
    ROW("pcm 22050/16/1", 1, 22050, 16,      0, 0, 0, 0);
    ROW("pcm 44100/16/1", 1, 44100, 16,      0, 0, 0, 0);
    ROW("pcm 44100/16/2", 2, 44100, 16,      0, 0, 0, 0);
    ROW("pcm 48000/16/2", 2, 48000, 16,      0, 0, 0, 0);
    ROW("pcm 96000/16/2", 2, 96000, 16,      0, 0, 0, 0);
    ROW("pcm 48000/24/2", 2, 48000, 24,      0, 0, 0, 0);
    ROW("pcm 48000/32/2", 2, 48000, 32,      0, 0, 0, 0);
    ROW("pcm 44100/8/2", 2, 44100, 8,        0, 0, 0, 0);
    ROW("pcm 6ch 44100/16", 6, 44100, 16,    B, B, B, P);
    ROW("pcm 8ch 48000/16", 8, 48000, 16,    B, B, B, P);
    ROW("pcm 4ch 44100/16", 4, 44100, 16,    B, B, B, P);
    ROW("pcm 3ch 44100/16", 3, 44100, 16,    B, B, B, P);
    ROW("pcm 0ch", 0, 44100, 16,             B, B, B, P);
    ROW("pcm rate 0", 2, 0, 16,              B, B, B, P);
    ROW("pcm bits 0", 2, 44100, 0,           B, B, B, P);
    ROW("pcm bits 12", 2, 44100, 12,         B, B, B, P);
    ROW("pcm bits 20", 2, 44100, 20,         B, B, B, P);
    ROW("pcm bits 64", 2, 44100, 64,         0, 0, B, P);
    ROW("pcm rate 1000", 2, 1000, 16,        0, 0, 0, 0);
    ROW("pcm rate 384000", 2, 384000, 16,    0, 0, 0, 0);
    ROW("pcm rate 1000000", 2, 1000000, 16,  0, 0, 0, B);
    ROW("pcm avgbytes 0", 2, 44100, 16,      0, 0, 0, 0); r[k - 1].f.nAvgBytesPerSec = 0;
    ROW("pcm avgbytes wrong", 2, 44100, 16,  0, 0, 0, 0); r[k - 1].f.nAvgBytesPerSec = 12345;
    ROW("pcm blockalign 3", 2, 44100, 16,    0, 0, B, P); r[k - 1].f.nBlockAlign = 3;
    ROW("pcm blockalign 0", 2, 44100, 16,    0, 0, B, P); r[k - 1].f.nBlockAlign = 0;
    ROW("pcm cbSize 0x7777", 2, 44100, 16,   0, 0, 0, 0); r[k - 1].f.cbSize = 0x7777;
    ROW("tag 0", 2, 44100, 16,               B, B, B, B); r[k - 1].f.wFormatTag = 0;
    ROW("tag 0x7777", 2, 44100, 16,          B, B, B, B); r[k - 1].f.wFormatTag = 0x7777;
    ROW("float 48000/32/2", 2, 48000, 32,    0, 0, 0, 0); r[k - 1].f.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    ROW("float 44100/32/1", 1, 44100, 32,    0, 0, 0, 0); r[k - 1].f.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    ROW("float 48000/64/2", 2, 48000, 64,    0, 0, B, B); r[k - 1].f.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    ROW("alaw 8000/8/1", 1, 8000, 8,         0, B, 0, B); r[k - 1].f.wFormatTag = WAVE_FORMAT_ALAW;
    ROW("mulaw 8000/8/1", 1, 8000, 8,        0, B, 0, B); r[k - 1].f.wFormatTag = WAVE_FORMAT_MULAW;
#undef ROW
    *n = k;
}

static void run_rows(int real_open) {
    frow rows[48]; int n = 0;
    build_rows(rows, &n);
    for (int i = 0; i < n; i++) {
        for (int which = 0; which < 2; which++) {
            UINT id = which ? 0 : WAVE_MAPPER;
            const char *nm = which ? "id0" : "mapper";
            UINT wq = which ? rows[i].q0 : rows[i].qm, wo = which ? rows[i].o0 : rows[i].om;
            if (wo_ndev() == 0) wq = wo = MMSYSERR_BADDEVICEID;
            MMRESULT q = wo_open(NULL, id, &rows[i].f, 0, 0, WAVE_FORMAT_QUERY);
            CHECKF(q == wq, "%s %s query -> %u want %u", rows[i].name, nm, q, wq);
            if (!real_open) continue;
            HWAVEOUT h = WO_SENTINEL;
            MMRESULT r = wo_open(&h, id, &rows[i].f, 0, 0, 0);
            HWAVEOUT got = h;
            CHECKF(r == wo, "%s %s open -> %u want %u", rows[i].name, nm, r, wo);
            if (r) CHECKF(got == NULL, "%s: failed open left *phwo %p", rows[i].name, got);
            else CHECKF(got != NULL && got != WO_SENTINEL, "%s: open ok but *phwo %p", rows[i].name, got);
            wo_release(r, 0, got);
        }
    }
}

TEST(wo_fmt_query_table) { REQUIRE(U(waveOutOpen)); run_rows(0); }
TEST(wo_fmt_open_table) { REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutClose)); run_rows(1); }

/* WAVE_FORMAT_DIRECT switches ACM off: through the mapper the answer becomes the device's own (A-law / mu-law
 * are converted by the mapper only without DIRECT). */
TEST(wo_fmt_direct_vs_mapper) {
    REQUIRE(U(waveOutOpen));
    if (wo_nodev_contract(WAVE_FORMAT_QUERY | WAVE_FORMAT_DIRECT)) return;
    frow rows[48]; int n = 0;
    build_rows(rows, &n);
    for (int i = 0; i < n; i++) {
        MMRESULT m = wo_open(NULL, WAVE_MAPPER, &rows[i].f, 0, 0, WAVE_FORMAT_QUERY);
        MMRESULT md = wo_open(NULL, WAVE_MAPPER, &rows[i].f, 0, 0, WAVE_FORMAT_DIRECT_QUERY);
        MMRESULT d = wo_open(NULL, 0, &rows[i].f, 0, 0, WAVE_FORMAT_DIRECT_QUERY);
        CHECKF(m == rows[i].qm, "%s: mapper %u want %u", rows[i].name, m, rows[i].qm);
        CHECKF(md == rows[i].q0, "%s: mapper+DIRECT %u want %u", rows[i].name, md, rows[i].q0);
        CHECKF(d == rows[i].q0, "%s: id0+DIRECT %u want %u", rows[i].name, d, rows[i].q0);
        if (d == MMSYSERR_NOERROR) CHECKF(m == MMSYSERR_NOERROR, "%s: id0 direct ok but mapper %u", rows[i].name, m);
    }
}

TEST(wo_fmt_extensible) {
    REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutClose));
    if (wo_nodev_contract(WAVE_FORMAT_QUERY)) return;
    WAVEFORMATEXTENSIBLE x;
    struct { const char *nm; WORD ch, bits, valid; DWORD mask; int fl; } rows[] = {
        { "ext pcm 2ch 16", 2, 16, 16, SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT, 0 },
        { "ext pcm 1ch 16", 1, 16, 16, SPEAKER_FRONT_CENTER, 0 },
        { "ext pcm 2ch 24in32", 2, 32, 24, SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT, 0 },
        { "ext float 2ch 32", 2, 32, 32, SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT, 1 },
        { "ext pcm 6ch 16 5.1", 6, 16, 16, 0x3F /* 5.1 */, 0 },
        { "ext pcm 2ch mask 0", 2, 16, 16, 0, 0 },
    };
    for (unsigned i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        wo_ext(&x, rows[i].ch, 48000, rows[i].bits, rows[i].valid, rows[i].mask, rows[i].fl);
        MMRESULT qm = wo_open(NULL, WAVE_MAPPER, &x.Format, 0, 0, WAVE_FORMAT_QUERY);
        MMRESULT q0 = wo_open(NULL, 0, &x.Format, 0, 0, WAVE_FORMAT_QUERY);
        EXPECT_EQ(qm, MMSYSERR_NOERROR);                    /* every row below is taken (VM) */
        EXPECT_EQ(q0, MMSYSERR_NOERROR);
        HWAVEOUT h = WO_SENTINEL;
        MMRESULT r = wo_open(&h, WAVE_MAPPER, &x.Format, 0, 0, 0);
        HWAVEOUT got = h;
        CHECKF(r == qm, "%s: mapper open %u query %u", rows[i].nm, r, qm);
        if (r) CHECK(got == NULL);
        wo_release(r, 0, got);
    }
    /* stereo 16-bit extensible is the plain format in another dress */
    wo_ext(&x, 2, 48000, 16, 16, SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT, 0);
    EXPECT_EQ(wo_open(NULL, WAVE_MAPPER, &x.Format, 0, 0, WAVE_FORMAT_QUERY), MMSYSERR_NOERROR);
    EXPECT_EQ(wo_open(NULL, 0, &x.Format, 0, 0, WAVE_FORMAT_QUERY), MMSYSERR_NOERROR);
}

TEST(wo_fmt_extensible_short_cbsize) {   /* query only: no stream is created */
    REQUIRE(U(waveOutOpen));
    if (wo_nodev_contract(WAVE_FORMAT_QUERY)) return;
    WAVEFORMATEXTENSIBLE x;
    static const WORD cbs[] = { 0, 1, 21, 22, 23, 100 };
    for (unsigned i = 0; i < sizeof cbs / sizeof cbs[0]; i++) {
        wo_ext(&x, 2, 48000, 16, 16, SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT, 0);
        struct { WAVEFORMATEXTENSIBLE x; BYTE pad[128]; } big; memset(&big, 0, sizeof big); big.x = x;
        big.x.Format.cbSize = cbs[i];
        MMRESULT qm = wo_open(NULL, WAVE_MAPPER, &big.x.Format, 0, 0, WAVE_FORMAT_QUERY);
        MMRESULT q0 = wo_open(NULL, 0, &big.x.Format, 0, 0, WAVE_FORMAT_QUERY);
        /* cbSize < 22 for WAVE_FORMAT_EXTENSIBLE: the mapper answers 32, the device 11 (VM) */
        UINT wm = cbs[i] < 22 ? WAVERR_BADFORMAT : MMSYSERR_NOERROR, w0 = cbs[i] < 22 ? MMSYSERR_INVALPARAM : MMSYSERR_NOERROR;
        CHECKF(qm == wm, "cbSize %u: mapper %u want %u", cbs[i], qm, wm);
        CHECKF(q0 == w0, "cbSize %u: id0 %u want %u", cbs[i], q0, w0);
    }
}

/* The format buffer is only read: open must not write into the caller's WAVEFORMATEX. */
TEST(wo_fmt_not_modified) {
    REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutClose));
    if (wo_nodev_contract(0)) return;
    struct { WAVEFORMATEX f; BYTE tail[32]; } b, ref;
    memset(&b, 0xA5, sizeof b); wo_std(&b.f);
    ref = b;
    HWAVEOUT h = NULL;
    EXPECT_EQ(wo_open(&h, WAVE_MAPPER, &b.f, 0, 0, 0), MMSYSERR_NOERROR);
    CHECK(!memcmp(&b, &ref, sizeof b));
    if (h) EXPECT_EQ(wo_close(h), MMSYSERR_NOERROR);
    EXPECT_EQ(wo_open(NULL, 0, &b.f, 0, 0, WAVE_FORMAT_QUERY), MMSYSERR_NOERROR);
    CHECK(!memcmp(&b, &ref, sizeof b));
}

/* The format is captured at open: changing the caller's struct afterwards does not affect the open handle. */
TEST(wo_fmt_buffer_reusable_after_open) {
    REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutClose)); REQUIRE(U(waveOutGetID));
    if (wo_nodev_contract(0)) return;
    WAVEFORMATEX *wf = (WAVEFORMATEX *)HeapAlloc(GetProcessHeap(), 0, sizeof *wf);
    wo_std(wf);
    HWAVEOUT h = NULL;
    EXPECT_EQ(wo_open(&h, 0, wf, 0, 0, 0), MMSYSERR_NOERROR);
    memset(wf, 0xEE, sizeof *wf);
    HeapFree(GetProcessHeap(), 0, wf);
    if (!h) return;
    UINT id = 3;
    EXPECT_EQ(wo_getid(h, &id), MMSYSERR_NOERROR);
    EXPECT_EQ(id, 0);
    EXPECT_EQ(wo_close(h), MMSYSERR_NOERROR);
}

/* Many 2-channel 16-bit rates in a row: each query is 0 on both paths (shared-mode engine resamples). */
TEST(wo_fmt_rate_sweep_query) {
    REQUIRE(U(waveOutOpen));
    if (wo_nodev_contract(WAVE_FORMAT_QUERY)) return;
    static const DWORD rates[] = { 8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000, 88200, 96000, 176400, 192000 };
    for (unsigned i = 0; i < sizeof rates / sizeof rates[0]; i++)
        for (WORD ch = 1; ch <= 2; ch++)
            for (WORD bits = 8; bits <= 16; bits += 8) {
                WAVEFORMATEX wf; wo_pcm(&wf, ch, rates[i], bits);
                MMRESULT a = wo_open(NULL, WAVE_MAPPER, &wf, 0, 0, WAVE_FORMAT_QUERY);
                MMRESULT b = wo_open(NULL, 0, &wf, 0, 0, WAVE_FORMAT_QUERY);
                CHECKF(a == 0 && b == 0, "%lu/%u/%u: mapper %u id0 %u", rates[i], bits, ch, a, b);
            }
}
