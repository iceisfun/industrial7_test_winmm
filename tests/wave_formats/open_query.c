#include "wf.h"
/* A real open (CALLBACK_NULL, no buffers written, closed at once) against WAVE_FORMAT_QUERY for representative
 * formats on the mapper, device 0 and device 0|DIRECT, waveOut and waveIn. Nothing is played or recorded. They agree
 * except where the real open validates header fields the query ignores (pinned per cell below).
 * VM_TIMEOUT: 240 */

/* q[]: query verdict on {mapper, id 0, id 0|DIRECT}; o[]: real-open verdict on the same targets. WF_NOTE_IF_NOT0 for
 * the one non-core cell (24-bit 96 kHz, endpoint engine dependent). All values from the Win10 VM (HDA duplex and
 * output-only, waveOut and waveIn alike). */
#define NZ WF_NOTE_IF_NOT0
typedef struct { const char *n; int kind; WORD tag, ch; DWORD rate; WORD bits; int q[3], o[3]; } rep;
enum { K_PCM, K_EXT_PCM, K_EXT_FLOAT, K_ADPCM, K_IMA, K_MULAW };
static const rep k_rep[] = {
    { "pcm16/2/44100",   K_PCM, WAVE_FORMAT_PCM_, 2, 44100, 16,       { 0, 0, 0 },    { 0, 0, 0 } },
    { "pcm8/1/8000",     K_PCM, WAVE_FORMAT_PCM_, 1, 8000, 8,         { 0, 0, 0 },    { 0, 0, 0 } },
    { "pcm16/1/22050",   K_PCM, WAVE_FORMAT_PCM_, 1, 22050, 16,       { 0, 0, 0 },    { 0, 0, 0 } },
    /* The one disagreement: plain 6-ch PCM is 32 for the query everywhere, but a real open on a device id is 11. */
    { "pcm16/6/48000",   K_PCM, WAVE_FORMAT_PCM_, 6, 48000, 16,       { 32, 32, 32 }, { 32, 11, 11 } },
    { "float32/2/48000", K_PCM, WAVE_FORMAT_IEEE_FLOAT_, 2, 48000, 32, { 0, 0, 0 },    { 0, 0, 0 } },
    { "ext pcm16/2/48000", K_EXT_PCM, 0, 2, 48000, 16,                { 0, 0, 0 },    { 0, 0, 0 } },
    { "ext float32/2/48000", K_EXT_FLOAT, 0, 2, 48000, 32,            { 0, 0, 0 },    { 0, 0, 0 } },
    { "msadpcm/1/22050", K_ADPCM, 0, 1, 22050, 4,                     { 0, 32, 32 },  { 0, 32, 32 } },
    { "imaadpcm/1/22050", K_IMA, 0, 1, 22050, 4,                      { 0, 32, 32 },  { 0, 32, 32 } },
    { "mulaw/1/8000",    K_MULAW, 0, 1, 8000, 8,                      { 0, 32, 32 },  { 0, 32, 32 } },
    { "tag0x1234",       K_PCM, 0x1234, 2, 44100, 16,                 { 32, 32, 32 }, { 32, 32, 32 } },
    { "pcm24/2/96000",   K_PCM, WAVE_FORMAT_PCM_, 2, 96000, 24,       { NZ, NZ, NZ }, { NZ, NZ, NZ } },
};
#define NREP ((int)(sizeof k_rep / sizeof *k_rep))

static void build(const rep *r, void *buf) {
    switch (r->kind) {
    case K_PCM:       wf_pcm((wf_fmt *)buf, r->tag, r->ch, r->rate, r->bits); break;
    case K_EXT_PCM:   wf_extfmt((wf_ext *)buf, r->ch, r->rate, r->bits, r->bits, 3, &WF_SUB_PCM); break;
    case K_EXT_FLOAT: wf_extfmt((wf_ext *)buf, r->ch, r->rate, r->bits, r->bits, 3, &WF_SUB_FLOAT); break;
    case K_ADPCM:     wf_msadpcm((wf_fmt *)buf, r->ch, r->rate); break;
    case K_IMA:       wf_imaadpcm((wf_fmt *)buf, r->ch, r->rate); break;
    case K_MULAW:     wf_g711((wf_fmt *)buf, WAVE_FORMAT_MULAW_, r->ch, r->rate); break;
    }
}

static void agree(int dir, int i, int t) {
    static const UINT ids[3] = { WAVE_MAPPER, 0, 0 };
    static const DWORD fls[3] = { 0, 0, WAVE_FORMAT_DIRECT };
    if (!U(waveOutOpen) || !U(waveInOpen) || !U(waveOutClose) || !U(waveInClose)) { t_skip("missing export"); return; }
    union { wf_fmt f; wf_ext e; } u;
    build(&k_rep[i], &u);
    char lab[64];
    sprintf(lab, "%s fl0x%lx", k_rep[i].n, fls[t]);
    MMRESULT q = wf_cell(lab, dir, ids[t], &u, fls[t], k_rep[i].q[t]);
    MMRESULT o = wf_cell_open(lab, dir, ids[t], &u, fls[t], k_rep[i].o[t]);
    /* Where both are pinned to the same value, or both unpinned, they must agree. */
    if (k_rep[i].q[t] == k_rep[i].o[t])
        CHECKF(q == o, "%s %s id %d fl 0x%lx: query %u open %u", wf_dirname(dir), k_rep[i].n, (int)ids[t], fls[t], q, o);
}

#define AG(D, DIR, I) \
    TEST(wf_##D##_open_matches_query_##I##_mapper) { agree(DIR, I, 0); } \
    TEST(wf_##D##_open_matches_query_##I##_id0)    { agree(DIR, I, 1); } \
    TEST(wf_##D##_open_matches_query_##I##_id0_direct) { agree(DIR, I, 2); }
#define AGALL(D, DIR) AG(D, DIR, 0) AG(D, DIR, 1) AG(D, DIR, 2) AG(D, DIR, 3) AG(D, DIR, 4) AG(D, DIR, 5) \
    AG(D, DIR, 6) AG(D, DIR, 7) AG(D, DIR, 8) AG(D, DIR, 9) AG(D, DIR, 10) AG(D, DIR, 11)
AGALL(out, WF_OUT)
AGALL(in, WF_IN)

TEST(wf_open_rep_table_size) { EXPECT_EQ(NREP, 12); }

/* Core real opens: pinned 0 where a device exists (the open path must accept what the query accepts). */
static void core_open(int dir, UINT id) {
    static const DWORD rates[] = { 8000, 11025, 22050, 44100, 48000 };
    char lab[64];
    for (unsigned r = 0; r < 5; r++)
        for (WORD b = 8; b <= 16; b += 8)
            for (WORD c = 1; c <= 2; c++) {
                wf_fmt f; wf_pcm(&f, WAVE_FORMAT_PCM_, c, rates[r], b);
                sprintf(lab, "pcm %lu/%u/%u", rates[r], b, c);
                wf_cell_open(lab, dir, id, &f, 0, 0);
            }
}
TEST(wf_out_open_core_mapper) { REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutClose)); core_open(WF_OUT, WAVE_MAPPER); }
TEST(wf_out_open_core_id0)    { REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutClose)); core_open(WF_OUT, 0); }
TEST(wf_in_open_core_mapper)  { REQUIRE(U(waveInOpen)); REQUIRE(U(waveInClose)); core_open(WF_IN, WAVE_MAPPER); }
TEST(wf_in_open_core_id0)     { REQUIRE(U(waveInOpen)); REQUIRE(U(waveInClose)); core_open(WF_IN, 0); }

/* Real opens with inconsistent header fields. Unlike the query path, the open validates the header: on a device id
 * the endpoint rejects nBlockAlign 3 (2ch/16), nSamplesPerSec 0, nChannels 0 and wBitsPerSample 12 / 0 with
 * MMSYSERR_INVALPARAM (11); through the mapper the same formats are WAVERR_BADFORMAT (32, the mapper's ACM fallback
 * finds nothing). nAvgBytesPerSec 0 is accepted on both [VM]. */
static void odd_open(int dir, UINT id) {
    wf_fmt f;
    const int bad = id == WAVE_MAPPER ? WAVERR_BADFORMAT : MMSYSERR_INVALPARAM;
    wf_pcm(&f, WAVE_FORMAT_PCM_, 2, 44100, 16); f.nBlockAlign = 3;      wf_cell_open("pcm blockalign3", dir, id, &f, 0, bad);
    wf_pcm(&f, WAVE_FORMAT_PCM_, 2, 44100, 16); f.nAvgBytesPerSec = 0;  wf_cell_open("pcm avg0", dir, id, &f, 0, 0);
    wf_pcm(&f, WAVE_FORMAT_PCM_, 1, 0, 8);                              wf_cell_open("pcm rate0", dir, id, &f, 0, bad);
    wf_pcm(&f, WAVE_FORMAT_PCM_, 0, 44100, 16);                         wf_cell_open("pcm ch0", dir, id, &f, 0, bad);
    wf_pcm(&f, WAVE_FORMAT_PCM_, 2, 44100, 12);                         wf_cell_open("pcm bits12", dir, id, &f, 0, bad);
    wf_pcm(&f, WAVE_FORMAT_PCM_, 2, 44100, 0);                          wf_cell_open("pcm bits0", dir, id, &f, 0, bad);
}
TEST(wf_out_open_odd_mapper) { REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutClose)); odd_open(WF_OUT, WAVE_MAPPER); }
TEST(wf_out_open_odd_id0)    { REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutClose)); odd_open(WF_OUT, 0); }
TEST(wf_in_open_odd_mapper)  { REQUIRE(U(waveInOpen)); REQUIRE(U(waveInClose)); odd_open(WF_IN, WAVE_MAPPER); }
TEST(wf_in_open_odd_id0)     { REQUIRE(U(waveInOpen)); REQUIRE(U(waveInClose)); odd_open(WF_IN, 0); }

/* Opening an ACM-converted stream through the mapper reports the mapper id from waveOutGetID (the id stored at
 * open), and the stream closes cleanly. */
TEST(wf_out_open_acm_stream_getid) {
    REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutClose)); REQUIRE(U(waveOutGetID));
    wf_fmt f; wf_imaadpcm(&f, 1, 22050);
    HWAVEOUT h = (HWAVEOUT)(ULONG_PTR)0x77;
    MMRESULT r = U(waveOutOpen)(&h, WAVE_MAPPER, (LPCWAVEFORMATEX)&f, 0, 0, CALLBACK_NULL);
    if (wf_ndev(WF_OUT) == 0) { EXPECT_EQ(r, MMSYSERR_BADDEVICEID); EXPECT_EQ(h, NULL); return; }
    EXPECT_EQ(r, MMSYSERR_NOERROR);   /* the mapper opens an ACM conversion stream [VM] */
    if (r) { EXPECT_EQ(h, NULL); return; }
    UINT id = 1234;
    EXPECT_EQ(U(waveOutGetID)(h, &id), MMSYSERR_NOERROR);
    EXPECT_EQ(id, WAVE_MAPPER);
    EXPECT_EQ(U(waveOutClose)(h), MMSYSERR_NOERROR);
}

TEST(wf_in_open_acm_stream_getid) {
    REQUIRE(U(waveInOpen)); REQUIRE(U(waveInClose)); REQUIRE(U(waveInGetID));
    wf_fmt f; wf_imaadpcm(&f, 1, 22050);
    HWAVEIN h = (HWAVEIN)(ULONG_PTR)0x77;
    MMRESULT r = U(waveInOpen)(&h, WAVE_MAPPER, (LPCWAVEFORMATEX)&f, 0, 0, CALLBACK_NULL);
    if (wf_ndev(WF_IN) == 0) { EXPECT_EQ(r, MMSYSERR_BADDEVICEID); EXPECT_EQ(h, NULL); return; }
    EXPECT_EQ(r, MMSYSERR_NOERROR);   /* capture through an ACM encoder [VM] */
    if (r) { EXPECT_EQ(h, NULL); return; }
    UINT id = 1234;
    EXPECT_EQ(U(waveInGetID)(h, &id), MMSYSERR_NOERROR);
    EXPECT_EQ(id, WAVE_MAPPER);
    EXPECT_EQ(U(waveInClose)(h), MMSYSERR_NOERROR);
}

/* Device-0 open of the core format reports id 0 from GetID. */
TEST(wf_out_open_id0_getid) {
    REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutClose)); REQUIRE(U(waveOutGetID));
    if (wf_need_dev(WF_OUT)) return;
    wf_fmt f; wf_pcm(&f, WAVE_FORMAT_PCM_, 2, 44100, 16);
    HWAVEOUT h = NULL;
    MMRESULT r = U(waveOutOpen)(&h, 0, (LPCWAVEFORMATEX)&f, 0, 0, CALLBACK_NULL);
    EXPECT_EQ(r, MMSYSERR_NOERROR);
    if (r) return;
    UINT id = 1234;
    EXPECT_EQ(U(waveOutGetID)(h, &id), MMSYSERR_NOERROR);
    EXPECT_EQ(id, 0);
    EXPECT_EQ(U(waveOutClose)(h), MMSYSERR_NOERROR);
}

/* Two streams in different formats can be open on device 0 at once (shared-mode engine mixes them). */
TEST(wf_out_open_two_formats_concurrently) {
    REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutClose));
    if (wf_need_dev(WF_OUT)) return;
    wf_fmt a, b; wf_pcm(&a, WAVE_FORMAT_PCM_, 2, 44100, 16); wf_pcm(&b, WAVE_FORMAT_PCM_, 1, 8000, 8);
    HWAVEOUT ha = NULL, hb = NULL;
    EXPECT_EQ(U(waveOutOpen)(&ha, 0, (LPCWAVEFORMATEX)&a, 0, 0, CALLBACK_NULL), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveOutOpen)(&hb, 0, (LPCWAVEFORMATEX)&b, 0, 0, CALLBACK_NULL), MMSYSERR_NOERROR);
    CHECK(ha != hb);
    if (ha) EXPECT_EQ(U(waveOutClose)(ha), MMSYSERR_NOERROR);
    if (hb) EXPECT_EQ(U(waveOutClose)(hb), MMSYSERR_NOERROR);
}

TEST(wf_in_open_two_formats_concurrently) {
    REQUIRE(U(waveInOpen)); REQUIRE(U(waveInClose));
    if (wf_need_dev(WF_IN)) return;
    wf_fmt a, b; wf_pcm(&a, WAVE_FORMAT_PCM_, 2, 44100, 16); wf_pcm(&b, WAVE_FORMAT_PCM_, 1, 8000, 8);
    HWAVEIN ha = NULL, hb = NULL;
    MMRESULT ra = U(waveInOpen)(&ha, 0, (LPCWAVEFORMATEX)&a, 0, 0, CALLBACK_NULL);
    MMRESULT rb = U(waveInOpen)(&hb, 0, (LPCWAVEFORMATEX)&b, 0, 0, CALLBACK_NULL);
    EXPECT_EQ(ra, MMSYSERR_NOERROR);
    EXPECT_EQ(rb, MMSYSERR_NOERROR);  /* shared-mode capture: a second stream in another format opens too [VM] */
    if (ha) EXPECT_EQ(U(waveInClose)(ha), MMSYSERR_NOERROR);
    if (!rb && hb) EXPECT_EQ(U(waveInClose)(hb), MMSYSERR_NOERROR);
}

/* Real opens of the same inconsistent headers through mapper|DIRECT (no ACM fallback), and of EXTENSIBLE with a
 * short cbSize: recorded. */
static void odd_open_direct(int dir) {
    wf_fmt f; wf_ext e;
    wf_pcm(&f, WAVE_FORMAT_PCM_, 2, 44100, 16); f.nBlockAlign = 3;
    wf_cell_open("pcm blockalign3 DIRECT", dir, WAVE_MAPPER, &f, WAVE_FORMAT_DIRECT, WF_NOTE);
    wf_pcm(&f, WAVE_FORMAT_PCM_, 1, 0, 8);
    wf_cell_open("pcm rate0 DIRECT", dir, WAVE_MAPPER, &f, WAVE_FORMAT_DIRECT, WF_NOTE);
    wf_pcm(&f, WAVE_FORMAT_PCM_, 6, 48000, 16);
    wf_cell_open("pcm 6ch DIRECT", dir, WAVE_MAPPER, &f, WAVE_FORMAT_DIRECT, WF_NOTE);
    wf_extfmt(&e, 2, 44100, 16, 16, 3, &WF_SUB_PCM); e.cbSize = 0;
    wf_cell_open("ext cb0", dir, WAVE_MAPPER, &e, 0, WF_NOTE);
    wf_cell_open("ext cb0", dir, 0, &e, 0, WF_NOTE);
    wf_cell_open("ext cb0 DIRECT", dir, WAVE_MAPPER, &e, WAVE_FORMAT_DIRECT, WF_NOTE);
    wf_extfmt(&e, 2, 44100, 16, 16, 3, &WF_SUB_PCM); e.nBlockAlign = 5;
    wf_cell_open("ext blockalign5", dir, WAVE_MAPPER, &e, 0, WF_NOTE);
    wf_cell_open("ext blockalign5", dir, 0, &e, 0, WF_NOTE);
    wf_extfmt(&e, 2, 48000, 32, 32, 3, &WF_SUB_FLOAT); e.nAvgBytesPerSec = 0;
    wf_cell_open("ext float avg0", dir, 0, &e, 0, WF_NOTE);
    wf_extfmt(&e, 6, 48000, 16, 16, 0x3F, &WF_SUB_PCM);
    wf_cell_open("ext pcm16 5.1", dir, WAVE_MAPPER, &e, 0, WF_NOTE);
    wf_cell_open("ext pcm16 5.1", dir, 0, &e, 0, WF_NOTE);
    wf_pcm(&f, WAVE_FORMAT_IEEE_FLOAT_, 2, 48000, 64);
    wf_cell_open("float64", dir, 0, &f, 0, WF_NOTE);
    wf_pcm(&f, WAVE_FORMAT_PCM_, 2, 1000000, 16);
    wf_cell_open("pcm 1MHz", dir, 0, &f, 0, WF_NOTE);
}
TEST(wf_out_open_odd_direct_and_ext) { REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutClose)); odd_open_direct(WF_OUT); }
TEST(wf_in_open_odd_direct_and_ext)  { REQUIRE(U(waveInOpen)); REQUIRE(U(waveInClose)); odd_open_direct(WF_IN); }
