#include "wf.h"
/* Plain WAVE_FORMAT_PCM negotiation with WAVE_FORMAT_QUERY: one TEST per (direction, target, flags, rate); one cell
 * per (bits, channels). Targets: WAVE_MAPPER, device 0, the 0xFFFF mapper alias, and id 0 with WAVE_MAPPED.
 * The core (8/16-bit, 1-2 ch, 8000..48000 Hz) is pinned to 0 wherever a device exists; the 24/32-bit and
 * 96k/192k cells come from the shared-mode engine of the endpoint and are only noted when not 0 (on the Win10 VM
 * every cell of the 7 x 4 x 2 grid is 0 on every target, DIRECT included, out and in).
 * VM_TIMEOUT: 240 */

static const WORD k_bits[] = { 8, 16, 24, 32 };
static const WORD k_ch[] = { 1, 2 };

static int is_core(DWORD rate, WORD bits) { return (bits == 8 || bits == 16) && rate >= 8000 && rate <= 48000; }

static void row(int dir, UINT id, DWORD flags, DWORD rate) {
    if (!U(waveOutOpen) || !U(waveInOpen)) { t_skip("missing open"); return; }
    char lab[80];
    for (unsigned b = 0; b < 4; b++)
        for (unsigned c = 0; c < 2; c++) {
            wf_fmt f; wf_pcm(&f, WAVE_FORMAT_PCM_, k_ch[c], rate, k_bits[b]);
            sprintf(lab, "pcm %lu/%u/%u fl0x%lx", rate, k_bits[b], k_ch[c], flags);
            wf_cell(lab, dir, id, &f, flags, is_core(rate, k_bits[b]) ? 0 : WF_NOTE_IF_NOT0);
        }
}

#define R7(X, D, DIR, T, ID, V, FL) \
    X(D, DIR, T, ID, V, FL, 8000) X(D, DIR, T, ID, V, FL, 11025) X(D, DIR, T, ID, V, FL, 22050) \
    X(D, DIR, T, ID, V, FL, 44100) X(D, DIR, T, ID, V, FL, 48000) X(D, DIR, T, ID, V, FL, 96000) \
    X(D, DIR, T, ID, V, FL, 192000)
#define MK(D, DIR, T, ID, V, FL, RATE) TEST(wf_##D##_pcm_##T##_##V##_##RATE) { row(DIR, ID, FL, RATE); }

R7(MK, out, WF_OUT, mapper, WAVE_MAPPER, plain, 0)
R7(MK, out, WF_OUT, mapper, WAVE_MAPPER, direct, WAVE_FORMAT_DIRECT)
R7(MK, out, WF_OUT, id0, 0, plain, 0)
R7(MK, out, WF_OUT, id0, 0, direct, WAVE_FORMAT_DIRECT)
R7(MK, in, WF_IN, mapper, WAVE_MAPPER, plain, 0)
R7(MK, in, WF_IN, mapper, WAVE_MAPPER, direct, WAVE_FORMAT_DIRECT)
R7(MK, in, WF_IN, id0, 0, plain, 0)
R7(MK, in, WF_IN, id0, 0, direct, WAVE_FORMAT_DIRECT)

/* The mapper alias, WAVE_MAPPED through device 0, and ALLOWSYNC: core rows only (44100 and 8000). */
TEST(wf_out_pcm_ffff_plain_44100)     { row(WF_OUT, 0xFFFF, 0, 44100); }
TEST(wf_out_pcm_ffff_direct_8000)     { row(WF_OUT, 0xFFFF, WAVE_FORMAT_DIRECT, 8000); }
TEST(wf_in_pcm_ffff_plain_44100)      { row(WF_IN, 0xFFFF, 0, 44100); }
TEST(wf_in_pcm_ffff_direct_8000)      { row(WF_IN, 0xFFFF, WAVE_FORMAT_DIRECT, 8000); }
TEST(wf_out_pcm_id0mapped_plain_22050) { row(WF_OUT, 0, WAVE_MAPPED, 22050); }
TEST(wf_out_pcm_id0mapped_plain_48000) { row(WF_OUT, 0, WAVE_MAPPED, 48000); }
TEST(wf_in_pcm_id0mapped_plain_22050)  { row(WF_IN, 0, WAVE_MAPPED, 22050); }
TEST(wf_in_pcm_id0mapped_plain_48000)  { row(WF_IN, 0, WAVE_MAPPED, 48000); }
TEST(wf_out_pcm_mapper_allowsync_11025) { row(WF_OUT, WAVE_MAPPER, WAVE_ALLOWSYNC, 11025); }
TEST(wf_out_pcm_id0_allowsync_44100)    { row(WF_OUT, 0, WAVE_ALLOWSYNC, 44100); }
TEST(wf_in_pcm_mapper_allowsync_11025)  { row(WF_IN, WAVE_MAPPER, WAVE_ALLOWSYNC, 11025); }
TEST(wf_in_pcm_id0_allowsync_44100)     { row(WF_IN, 0, WAVE_ALLOWSYNC, 44100); }
TEST(wf_out_pcm_mapper_commdefault_44100) { row(WF_OUT, WAVE_MAPPER, WAVE_MAPPED_DEFAULT_COMMUNICATION_DEVICE, 44100); }
TEST(wf_in_pcm_mapper_commdefault_44100)  { row(WF_IN, WAVE_MAPPER, WAVE_MAPPED_DEFAULT_COMMUNICATION_DEVICE, 44100); }

/* ---- off-grid values: channels, rates, bit depths ------------------------------------------------------------ */

/* Plain PCM with more than 2 channels has no channel mask: every count 3..255 is WAVERR_BADFORMAT on the mapper and
 * on the device, with and without DIRECT [VM]. */
static void channels(int dir, DWORD flags) {
    static const WORD ch[] = { 3, 4, 5, 6, 7, 8, 16, 255 };
    char lab[64];
    UINT ids[2] = { WAVE_MAPPER, 0 };
    for (unsigned i = 0; i < sizeof ch / sizeof *ch; i++)
        for (int j = 0; j < 2; j++) {
            wf_fmt f; wf_pcm(&f, WAVE_FORMAT_PCM_, ch[i], 48000, 16);
            sprintf(lab, "pcm 48000/16/%u fl0x%lx", ch[i], flags);
            wf_cell(lab, dir, ids[j], &f, flags, WAVERR_BADFORMAT);
        }
}
TEST(wf_out_pcm_channels_plain)  { channels(WF_OUT, 0); }
TEST(wf_out_pcm_channels_direct) { channels(WF_OUT, WAVE_FORMAT_DIRECT); }
TEST(wf_in_pcm_channels_plain)   { channels(WF_IN, 0); }
TEST(wf_in_pcm_channels_direct)  { channels(WF_IN, WAVE_FORMAT_DIRECT); }

/* 6-channel plain PCM: 32 at every rate and depth on the core grid, every target. */
static void six(int dir, UINT id, DWORD flags) {
    static const DWORD rates[] = { 8000, 22050, 44100, 48000 };
    char lab[64];
    for (unsigned r = 0; r < 4; r++)
        for (unsigned b = 0; b < 2; b++) {
            wf_fmt f; wf_pcm(&f, WAVE_FORMAT_PCM_, 6, rates[r], k_bits[b]);
            sprintf(lab, "pcm %lu/%u/6 fl0x%lx", rates[r], k_bits[b], flags);
            wf_cell(lab, dir, id, &f, flags, WAVERR_BADFORMAT);
        }
}
TEST(wf_out_pcm_6ch_mapper) { six(WF_OUT, WAVE_MAPPER, 0); }
TEST(wf_out_pcm_6ch_id0)    { six(WF_OUT, 0, 0); }
TEST(wf_out_pcm_6ch_id0_direct)    { six(WF_OUT, 0, WAVE_FORMAT_DIRECT); }
TEST(wf_out_pcm_6ch_mapper_direct) { six(WF_OUT, WAVE_MAPPER, WAVE_FORMAT_DIRECT); }
TEST(wf_in_pcm_6ch_mapper)  { six(WF_IN, WAVE_MAPPER, 0); }
TEST(wf_in_pcm_6ch_id0)     { six(WF_IN, 0, 0); }
TEST(wf_in_pcm_6ch_id0_direct)    { six(WF_IN, 0, WAVE_FORMAT_DIRECT); }
TEST(wf_in_pcm_6ch_mapper_direct) { six(WF_IN, WAVE_MAPPER, WAVE_FORMAT_DIRECT); }

/* The shared-mode engine resamples anything: every non-zero rate from 1 Hz to 1 MHz is accepted (0) on the mapper
 * and on the device, DIRECT included [VM]. */
static void rates(int dir, DWORD flags) {
    static const DWORD r[] = { 1, 100, 1000, 4000, 6000, 12000, 16000, 24000, 32000, 37800, 64000, 88200, 176400,
                               384000, 768000, 1000000 };
    char lab[64];
    UINT ids[2] = { WAVE_MAPPER, 0 };
    for (unsigned i = 0; i < sizeof r / sizeof *r; i++)
        for (int j = 0; j < 2; j++) {
            wf_fmt f; wf_pcm(&f, WAVE_FORMAT_PCM_, 2, r[i], 16);
            sprintf(lab, "pcm %lu/16/2 fl0x%lx", r[i], flags);
            wf_cell(lab, dir, ids[j], &f, flags, MMSYSERR_NOERROR);
        }
}
TEST(wf_out_pcm_offgrid_rates_plain)  { rates(WF_OUT, 0); }
TEST(wf_out_pcm_offgrid_rates_direct) { rates(WF_OUT, WAVE_FORMAT_DIRECT); }
TEST(wf_in_pcm_offgrid_rates_plain)   { rates(WF_IN, 0); }
TEST(wf_in_pcm_offgrid_rates_direct)  { rates(WF_IN, WAVE_FORMAT_DIRECT); }

/* Plain PCM depths that are not a whole number of bytes (1, 4, 7, 12, 20) -> 32; whole-byte depths beyond 32
 * (40, 48, 64) -> 0 [VM]. */
static void bitdepths(int dir, DWORD flags) {
    static const WORD b[] = { 1, 4, 7, 12, 20, 40, 48, 64 };
    char lab[64];
    UINT ids[2] = { WAVE_MAPPER, 0 };
    for (unsigned i = 0; i < sizeof b / sizeof *b; i++)
        for (int j = 0; j < 2; j++) {
            wf_fmt f; wf_pcm(&f, WAVE_FORMAT_PCM_, 2, 44100, b[i]);
            sprintf(lab, "pcm 44100/%u/2 fl0x%lx", b[i], flags);
            wf_cell(lab, dir, ids[j], &f, flags, b[i] % 8 ? WAVERR_BADFORMAT : MMSYSERR_NOERROR);
        }
}
TEST(wf_out_pcm_offgrid_bits_plain)  { bitdepths(WF_OUT, 0); }
TEST(wf_out_pcm_offgrid_bits_direct) { bitdepths(WF_OUT, WAVE_FORMAT_DIRECT); }
TEST(wf_in_pcm_offgrid_bits_plain)   { bitdepths(WF_IN, 0); }
TEST(wf_in_pcm_offgrid_bits_direct)  { bitdepths(WF_IN, WAVE_FORMAT_DIRECT); }

/* ---- degenerate fields ------------------------------------------------------------------------------------------- */

/* nChannels 0, nSamplesPerSec 0, wBitsPerSample 0 or 12: WAVERR_BADFORMAT on the query path, every target [VM].
 * (A real open of the same formats on a device id is 11: open_query.c.) */
static void degenerate(int dir, DWORD flags) {
    struct { WORD ch; DWORD rate; WORD bits; const char *n; } d[] = {
        { 0, 44100, 16, "ch0" }, { 2, 0, 16, "rate0" }, { 2, 44100, 0, "bits0" }, { 2, 44100, 12, "bits12" },
        { 0, 0, 0, "all0" }, { 1, 0, 8, "rate0 mono8" }, { 0, 8000, 8, "ch0 8k8" },
    };
    char lab[64];
    UINT ids[2] = { WAVE_MAPPER, 0 };
    for (unsigned i = 0; i < sizeof d / sizeof *d; i++)
        for (int j = 0; j < 2; j++) {
            wf_fmt f; wf_pcm(&f, WAVE_FORMAT_PCM_, d[i].ch, d[i].rate, d[i].bits);
            sprintf(lab, "pcm %s fl0x%lx", d[i].n, flags);
            wf_cell(lab, dir, ids[j], &f, flags, WAVERR_BADFORMAT);
        }
}
TEST(wf_out_pcm_degenerate_plain)  { degenerate(WF_OUT, 0); }
TEST(wf_out_pcm_degenerate_direct) { degenerate(WF_OUT, WAVE_FORMAT_DIRECT); }
TEST(wf_in_pcm_degenerate_plain)   { degenerate(WF_IN, 0); }
TEST(wf_in_pcm_degenerate_direct)  { degenerate(WF_IN, WAVE_FORMAT_DIRECT); }

/* ---- nBlockAlign / nAvgBytesPerSec consistency --------------------------------------------------------------- */

/* The query path does not validate nBlockAlign against nChannels * bits / 8: 0, 1, 2, 3, 8 and 0xFFFF for 2ch/16
 * are all accepted, with and without DIRECT [VM]. (The real open does validate it: open_query.c.) */
static void blockalign(int dir, DWORD flags) {
    static const WORD ba[] = { 3, 0, 1, 2, 8, 0xFFFF };
    char lab[64];
    UINT ids[2] = { WAVE_MAPPER, 0 };
    for (unsigned i = 0; i < sizeof ba / sizeof *ba; i++)
        for (int j = 0; j < 2; j++) {
            wf_fmt f; wf_pcm(&f, WAVE_FORMAT_PCM_, 2, 44100, 16);
            f.nBlockAlign = ba[i];
            sprintf(lab, "pcm 44100/16/2 blockalign%u fl0x%lx", ba[i], flags);
            wf_cell(lab, dir, ids[j], &f, flags, MMSYSERR_NOERROR);
        }
}
TEST(wf_out_pcm_blockalign_plain)  { blockalign(WF_OUT, 0); }
TEST(wf_out_pcm_blockalign_direct) { blockalign(WF_OUT, WAVE_FORMAT_DIRECT); }
TEST(wf_in_pcm_blockalign_plain)   { blockalign(WF_IN, 0); }
TEST(wf_in_pcm_blockalign_direct)  { blockalign(WF_IN, WAVE_FORMAT_DIRECT); }

/* nAvgBytesPerSec is not checked for plain PCM, on the query path or on a real open (0, 1, too small, too large,
 * 0xFFFFFFFF -> 0) [VM]. */
static void avgbytes(int dir, DWORD flags) {
    static const DWORD ab[] = { 0, 1, 44100, 176401, 0xFFFFFFFF };
    char lab[64];
    UINT ids[2] = { WAVE_MAPPER, 0 };
    for (unsigned i = 0; i < sizeof ab / sizeof *ab; i++)
        for (int j = 0; j < 2; j++) {
            wf_fmt f; wf_pcm(&f, WAVE_FORMAT_PCM_, 2, 44100, 16);
            f.nAvgBytesPerSec = ab[i];
            sprintf(lab, "pcm 44100/16/2 avg%lu fl0x%lx", ab[i], flags);
            wf_cell(lab, dir, ids[j], &f, flags, MMSYSERR_NOERROR);
        }
}
TEST(wf_out_pcm_avgbytes_plain)  { avgbytes(WF_OUT, 0); }
TEST(wf_out_pcm_avgbytes_direct) { avgbytes(WF_OUT, WAVE_FORMAT_DIRECT); }
TEST(wf_in_pcm_avgbytes_plain)   { avgbytes(WF_IN, 0); }
TEST(wf_in_pcm_avgbytes_direct)  { avgbytes(WF_IN, WAVE_FORMAT_DIRECT); }
