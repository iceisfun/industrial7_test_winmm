#include "ps.h"
/* SND_MEMORY images.
 *
 * ASYNC|MEMORY (sndPlaySoundI): 'RIFF' at 0 and 'WAVE' at 8 or FALSE; size = RIFF length + 8, and a size that wraps
 * below 8 -> FALSE; a size in [1, 0x1FFFFF] is copied (LocalAlloc) before the post, larger sizes are posted with the
 * caller's buffer. Nothing else about the image is checked before the API returns TRUE.
 *
 * Sync MEMORY: sndMessage -> soundLoadMemory -> soundInitWavHdr(image, 0xFFFFFFFF). An image the parser rejects
 * -> FALSE whatever the devices; an image it accepts is opened on the wave mapper -> TRUE only with a device.
 * soundInitWavHdr: RIFF/WAVE; walk chunks (raw sizes, no pad byte) to 'fmt ' (next-chunk offset >= RIFF length ->
 * fail); fmt cb < 14 fail; cb >= 18: cbSize <= 0x3FF and cbSize + 18 == cb, and for tags 1/3: cbSize 0, bits % 8
 * == 0, channels <= 2, avg == rate*ch*bits/8; channels, rate, avg, align nonzero; EXTENSIBLE: cb >= 40, valid bits
 * nonzero and <= bits, PCM/FLOAT subformat -> avg rule; then the chunk after fmt onward to 'data'.
 * VM_TIMEOUT: 150 */

static BYTE g_img[65536];
static BYTE g_big[0x210000];   /* 2 MiB + slack, only for the large-size rows; never freed */

static const GUID SUB_PCM   = { 0x00000001, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
static const GUID SUB_FLOAT = { 0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };

/* ---- async header gate ------------------------------------------------------------------------------------------- */

PTEST(async_valid_image_all_entry_points) {
    REQUIRE(ps_have_all()); ps_quiesce();
    for (int ep = 0; ep < EP_N; ep++) {
        DWORD n = ps_make_silent(g_img, sizeof g_img);
        CHECK(n == 44 + 1600);
        /* the ANSI forms pass a MEMORY pointer through unconverted, or the RIFF check would fail */
        CHECKF(ps_call(ep, PSZ_RAW, g_img, NULL, SND_ASYNC | SND_MEMORY | SND_NODEFAULT), "%s", ps_ep_name[ep]);
        ps_stop();
    }
    ps_quiesce();
}

PTEST(async_valid_image_hmod_ignored) {
    REQUIRE(U(PlaySoundW)); REQUIRE(U(PlaySoundA)); ps_quiesce();
    ps_make_silent(g_img, sizeof g_img);
    CHECK(U(PlaySoundW)((LPCWSTR)g_img, t_module("winmm.dll"), SND_ASYNC | SND_MEMORY | SND_NODEFAULT)); ps_stop();
    CHECK(U(PlaySoundA)((LPCSTR)g_img, GetModuleHandleW(NULL), SND_ASYNC | SND_MEMORY | SND_NODEFAULT)); ps_stop();
    ps_quiesce();
}

typedef struct { const char *what; DWORD off, len; const char *bytes; BOOL ok; } hdr_row;
static const hdr_row g_hdr_rows[] = {
    { "RIFX", 0, 4, "RIFX", FALSE }, { "riff", 0, 4, "riff", FALSE }, { "RIF_", 0, 4, "RIF ", FALSE },
    { "FFIR", 0, 4, "FFIR", FALSE }, { "zero magic", 0, 4, "\0\0\0\0", FALSE }, { "RF64", 0, 4, "RF64", FALSE },
    { "LIST", 0, 4, "LIST", FALSE }, { "wave", 8, 4, "wave", FALSE }, { "WAV_", 8, 4, "WAV ", FALSE },
    { "AVI_", 8, 4, "AVI ", FALSE }, { "RMID", 8, 4, "RMID", FALSE }, { "EVAW", 8, 4, "EVAW", FALSE },
    { "fmt_ at 8", 8, 4, "fmt ", FALSE },
    /* only the two magics and the length matter: everything after offset 12 is parsed later by the play thread */
    { "fmt -> FMT_", 12, 4, "FMT ", TRUE }, { "fmt tag 0", 20, 2, "\0\0", TRUE }, { "channels 0", 22, 2, "\0\0", TRUE },
    { "data -> DATA", 36, 4, "DATA", TRUE }, { "rate 0", 24, 4, "\0\0\0\0", TRUE }, { "fmt cb 2", 16, 4, "\2\0\0\0", TRUE },
};

PTEST(async_header_magic_table) {
    REQUIRE(ps_have_all()); ps_quiesce(); ps_sweep_begin();
    for (size_t r = 0; r < sizeof g_hdr_rows / sizeof g_hdr_rows[0]; r++)
        for (int ep = 0; ep < EP_N; ep++) {
            ps_make_silent(g_img, sizeof g_img);
            memcpy(g_img + g_hdr_rows[r].off, g_hdr_rows[r].bytes, g_hdr_rows[r].len);
            BOOL got = ps_call(ep, PSZ_RAW, g_img, NULL, SND_ASYNC | SND_MEMORY | SND_NODEFAULT);
            PS_EXPECT(!!got == g_hdr_rows[r].ok, "%s %s got %d want %d", ps_ep_name[ep], g_hdr_rows[r].what, got, g_hdr_rows[r].ok);
            ps_stop();
        }
    PS_SWEEP_END(); ps_quiesce();
}

/* RIFF length + 8 computed in 32 bits: 0xFFFFFFF8..0xFFFFFFFF wrap to 0..7 -> FALSE. */
PTEST(async_riff_length_wrap) {
    REQUIRE(ps_have_all()); ps_quiesce(); ps_sweep_begin();
    for (DWORD len = 0xFFFFFFF8; len != 0; len++)
        for (int ep = 0; ep < EP_N; ep++) {
            ps_img m; ps_make_silent(g_img, sizeof g_img); m.b = g_img; img_set_riff_len(&m, len);
            BOOL got = ps_call(ep, PSZ_RAW, g_img, NULL, SND_ASYNC | SND_MEMORY | SND_NODEFAULT);
            PS_EXPECT(!got, "%s len 0x%08lx got %d", ps_ep_name[ep], len, got);
        }
    PS_SWEEP_END(); ps_stop();
}

/* Lengths that do not wrap and stay inside the caller's (zero-filled) buffer are copied and posted. */
PTEST(async_riff_length_inside_buffer) {
    REQUIRE(ps_have_all()); ps_quiesce(); ps_sweep_begin();
    static const DWORD extra[] = { 0, 1, 2, 3, 4, 8, 100, 1000, 4096, 30000 };
    for (size_t i = 0; i < sizeof extra / sizeof extra[0]; i++)
        for (int ep = 0; ep < EP_N; ep++) {
            ps_img m; DWORD n = ps_make_silent(g_img, sizeof g_img); m.b = g_img; img_set_riff_len(&m, n - 8 + extra[i]);
            BOOL got = ps_call(ep, PSZ_RAW, g_img, NULL, SND_ASYNC | SND_MEMORY | SND_NODEFAULT);
            PS_EXPECT(got, "%s len n-8+%lu got %d", ps_ep_name[ep], extra[i], got);
            ps_stop();
        }
    PS_SWEEP_END(); ps_quiesce();
}

/* Sizes around the 2 MiB copy limit (size - 1 < 0x1FFFFF is copied) and the largest non-wrapping length, which is
 * posted with the caller's buffer: all TRUE. The buffer stays alive for the life of the process. */
PTEST(async_large_sizes_posted) {
    REQUIRE(U(PlaySoundW)); REQUIRE(U(PlaySoundA)); ps_quiesce();
    static const DWORD lens[] = { 0x1FFFF6, 0x1FFFF7, 0x1FFFF8, 0x200000, 0xFFFFFFF7 };
    for (size_t i = 0; i < sizeof lens / sizeof lens[0]; i++) {
        ps_img m; ps_make_silent(g_big, sizeof g_big); m.b = g_big; img_set_riff_len(&m, lens[i]);
        CHECKF(U(PlaySoundW)((LPCWSTR)g_big, NULL, SND_ASYNC | SND_MEMORY | SND_NODEFAULT), "len 0x%lx", lens[i]);
        ps_quiesce();
        CHECKF(U(PlaySoundA)((LPCSTR)g_big, NULL, SND_ASYNC | SND_MEMORY | SND_NODEFAULT), "A len 0x%lx", lens[i]);
        ps_quiesce();
    }
}

/* A copied image belongs to winmm once the call returns: the caller may scribble over it at once. */
PTEST(async_copied_image_caller_may_reuse) {
    REQUIRE(U(PlaySoundW)); ps_quiesce();
    for (int i = 0; i < 8; i++) {
        BYTE *p = (BYTE *)HeapAlloc(GetProcessHeap(), 0, 4096);
        if (!p) { t_skip("HeapAlloc"); return; }
        DWORD n = ps_make_silent(p, 4096);
        CHECK(U(PlaySoundW)((LPCWSTR)p, NULL, SND_ASYNC | SND_MEMORY | SND_NODEFAULT));
        memset(p, 0xA5, n);   /* would be junk if the play thread still read the caller's buffer */
        HeapFree(GetProcessHeap(), 0, p);
        Sleep(20);
    }
    ps_quiesce();
}

/* ASYNC|MEMORY on non-RIFF data never reaches the play thread. */
PTEST(async_junk_images) {
    REQUIRE(ps_have_all()); ps_quiesce(); ps_sweep_begin();
    static const char *junk[] = { "JUNKxxxxWAVE", "RIFF\x10\0\0\0XXXX", "RIFF\x10\0\0\0wave", "\0\0\0\0\0\0\0\0\0\0\0\0",
        "MThd\0\0\0\x06\0\x01\0\x01", "ID3\x03\0\0\0\0\0\0\0\0", "OggS\0\x02\0\0\0\0\0\0", "fLaC\0\0\0\x22\0\0\0\0" };
    for (size_t i = 0; i < sizeof junk / sizeof junk[0]; i++)
        for (int ep = 0; ep < EP_N; ep++) {
            memset(g_img, 0, 256); memcpy(g_img, junk[i], 12);
            BOOL got = ps_call(ep, PSZ_RAW, g_img, NULL, SND_ASYNC | SND_MEMORY | SND_NODEFAULT);
            PS_EXPECT(!got, "%s junk %zu got %d", ps_ep_name[ep], i, got);
            got = ps_call(ep, PSZ_RAW, g_img, NULL, SND_MEMORY | SND_NODEFAULT);
            PS_EXPECT(!got, "%s junk %zu sync got %d", ps_ep_name[ep], i, got);
            got = ps_call(ep, PSZ_RAW, g_img, NULL, SND_ASYNC | SND_LOOP | SND_MEMORY | SND_NODEFAULT);
            PS_EXPECT(!got, "%s junk %zu loop got %d", ps_ep_name[ep], i, got);
        }
    PS_SWEEP_END(); ps_stop();
}

/* ---- sync parser table ------------------------------------------------------------------------------------------- */

typedef enum { P_BAD, P_PCM, P_NOTE } pexp;
typedef struct { const char *what; pexp want; } prow;

/* Builds row r into g_img; returns its size. Every image keeps 64+ zero bytes after it inside g_img. */
static DWORD build_row(int r, prow *out) {
    ps_img m; ps_fmt f; BYTE zero[64] = { 0 };
    img_begin(&m, g_img, sizeof g_img);
    fmt_pcm(&f, 1, 8000, 16);
    DWORD dn = 1600, fcb = 16;
    pexp want = P_BAD; const char *what = NULL;
#define ROW(w, e) do { what = w; want = e; } while (0)
    switch (r) {
    /* accepted, standard PCM */
    case 0: ROW("canonical 8k16m", P_PCM); break;
    case 1: ROW("8-bit mono 11025", P_PCM); fmt_pcm(&f, 1, 11025, 8); dn = 1102; break;
    case 2: ROW("16-bit stereo 44100", P_PCM); fmt_pcm(&f, 2, 44100, 16); dn = 17640; break;
    case 3: ROW("8-bit stereo 22050", P_PCM); fmt_pcm(&f, 2, 22050, 8); dn = 4410; break;
    case 4: ROW("PCM cb 18 cbSize 0", P_PCM); fcb = 18; break;
    case 5: ROW("LIST chunk before fmt", P_PCM); img_chunk(&m, "LIST", "INFOabcd", 8); break;
    case 6: ROW("fact chunk between fmt and data", P_PCM); fcb = 0x10000 | 16; break;
    case 7: ROW("RIFF length 4 (fmt first, data next: length not consulted)", P_PCM); fcb = 0x20000 | 16; break;
    case 8: ROW("RIFF length 0x7FFFFFFF", P_PCM); fcb = 0x30000 | 16; break;
    case 9: ROW("fmt cb 17 (data at an odd offset)", P_PCM); fcb = 17; break;
    case 10: ROW("two LIST chunks before fmt", P_PCM); img_chunk(&m, "LIST", "INFO", 4); img_chunk(&m, "junk", zero, 12); break;
    case 11: ROW("data length 2 (one frame)", P_PCM); dn = 2; break;
    /* rejected */
    case 20: ROW("no chunks (RIFF length 4)", P_BAD); fcb = 0x40000; break;
    case 21: ROW("fmt cb 0", P_BAD); fcb = 0; break;
    case 22: ROW("fmt cb 8", P_BAD); fcb = 8; break;
    case 23: ROW("fmt cb 12", P_BAD); fcb = 12; break;
    case 24: ROW("fmt cb 13", P_BAD); fcb = 13; break;
    case 25: ROW("cb 18 cbSize 2", P_BAD); fcb = 18; f.cb = 2; break;
    case 26: ROW("cb 20 cbSize 0", P_BAD); fcb = 20; break;
    case 27: ROW("PCM cb 18 bits 12", P_BAD); fcb = 18; f.bits = 12; f.align = 2; f.avg = 8000 * 12 / 8; break;
    case 28: ROW("PCM cb 18 channels 3", P_BAD); fcb = 18; fmt_pcm(&f, 3, 8000, 16); break;
    case 29: ROW("PCM cb 18 avg +1", P_BAD); fcb = 18; f.avg++; break;
    case 30: ROW("PCM cb 20 cbSize 2", P_BAD); fcb = 20; f.cb = 2; break;
    case 31: ROW("FLOAT cb 18 bits 20", P_BAD); fcb = 18; f.tag = WAVE_FORMAT_IEEE_FLOAT; f.bits = 20; break;
    case 32: ROW("FLOAT cb 18 avg wrong", P_BAD); fcb = 18; f.tag = WAVE_FORMAT_IEEE_FLOAT; f.bits = 32; f.align = 4; f.avg = 8000; break;
    case 33: ROW("channels 0", P_BAD); f.ch = 0; break;
    case 34: ROW("rate 0", P_BAD); f.rate = 0; break;
    case 35: ROW("avg 0", P_BAD); f.avg = 0; break;
    case 36: ROW("align 0", P_BAD); f.align = 0; break;
    case 37: ROW("EXTENSIBLE cb 18 cbSize 0", P_BAD); fcb = 18; f.tag = WAVE_FORMAT_EXTENSIBLE; break;
    case 38: ROW("EXTENSIBLE valid bits 0", P_BAD); fcb = 40; f.tag = WAVE_FORMAT_EXTENSIBLE; f.cb = 22; f.valid = 0; f.mask = 4; f.sub = SUB_PCM; break;
    case 39: ROW("EXTENSIBLE valid 24 > bits 16", P_BAD); fcb = 40; f.tag = WAVE_FORMAT_EXTENSIBLE; f.cb = 22; f.valid = 24; f.mask = 4; f.sub = SUB_PCM; break;
    case 40: ROW("EXTENSIBLE PCM avg wrong", P_BAD); fcb = 40; f.tag = WAVE_FORMAT_EXTENSIBLE; f.cb = 22; f.valid = 16; f.mask = 4; f.sub = SUB_PCM; f.avg = 1234; break;
    case 41: ROW("EXTENSIBLE FLOAT avg wrong", P_BAD); fcb = 40; f.tag = WAVE_FORMAT_EXTENSIBLE; f.cb = 22; f.valid = 32; f.bits = 32; f.align = 4; f.mask = 4; f.sub = SUB_FLOAT; f.avg = 16000; break;
    case 42: ROW("EXTENSIBLE cb 38 cbSize 20", P_BAD); fcb = 38; f.tag = WAVE_FORMAT_EXTENSIBLE; f.cb = 20; f.valid = 16; f.mask = 4; break;
    case 43: ROW("cbSize 0x400 cb 0x412 (tag 0x55)", P_BAD); fcb = 0x412; f.tag = 0x55; f.cb = 0x400; break;
    case 44: ROW("no data chunk (fmt, LIST, end)", P_BAD); dn = 0x50000; break;
    case 45: ROW("data only, no fmt", P_BAD); fcb = 0x60000; break;
    case 46: ROW("data before fmt, nothing after", P_BAD); fcb = 0x70000 | 16; break;
    case 47: ROW("'FMT ' upper case", P_BAD); fcb = 0x80000 | 16; break;
    case 48: ROW("'DATA' upper case", P_BAD); fcb = 0x90000 | 16; break;
    case 49: ROW("odd chunk with its RIFF pad byte before fmt", P_BAD); img_chunk(&m, "junk", "abc", 3); img_pad(&m); break;
    case 50: ROW("PCM cb 18 cbSize 0 bits 0", P_BAD); fcb = 18; f.bits = 0; f.avg = 0; break;
    case 51: ROW("fmt chunk size 0xFFFFFFF0 (runs past RIFF length)", P_BAD); fcb = 0xA0000; break;
    case 52: ROW("fmt cb 16 then data cb 8 then end, RIFF length short of data", P_PCM); fcb = 0xB0000 | 16; break;
    /* accepted by the parser; whether the mapper opens it is device-specific */
    case 60: ROW("fmt cb 14 (WAVEFORMAT, bits read as 0)", P_NOTE); fcb = 14; break;
    case 61: ROW("IEEE float mono 8k cb 18", P_NOTE); fcb = 18; f.tag = WAVE_FORMAT_IEEE_FLOAT; f.bits = 32; f.align = 4; f.avg = 32000; dn = 3200; break;
    case 62: ROW("EXTENSIBLE PCM 16 mono", P_NOTE); fcb = 40; f.tag = WAVE_FORMAT_EXTENSIBLE; f.cb = 22; f.valid = 16; f.mask = 4; f.sub = SUB_PCM; break;
    case 63: ROW("PCM cb 16 avg inconsistent (not checked below cb 18)", P_NOTE); f.avg = 12345; break;
    case 64: ROW("data length 0", P_NOTE); dn = 0; break;
    case 65: ROW("odd unpadded chunk before fmt (fmt realigned in the caller's buffer)", P_NOTE); img_chunk(&m, "junk", "abc", 3); break;
    case 66: ROW("PCM 24-bit mono cb 16", P_NOTE); fmt_pcm(&f, 1, 8000, 24); dn = 2400; break;
    default: return 0;
    }
#undef ROW
    DWORD mode = fcb >> 16; fcb &= 0xFFFF;
    switch (mode) {
    case 0:  if (fcb > sizeof f) { static BYTE wide[0x420]; memset(wide, 0, sizeof wide); memcpy(wide, &f, sizeof f); img_chunk(&m, "fmt ", wide, fcb); }
             else { img_chunk(&m, "fmt ", &f, fcb); } if (dn == 0x50000) img_chunk(&m, "LIST", "INFO", 4); else img_chunk(&m, "data", NULL, dn); img_end(&m); break;
    case 1:  img_chunk(&m, "fmt ", &f, fcb); img_chunk(&m, "fact", "\x40\x06\0\0", 4); img_chunk(&m, "data", NULL, dn); img_end(&m); break;
    case 2:  img_chunk(&m, "fmt ", &f, fcb); img_chunk(&m, "data", NULL, dn); img_end(&m); img_set_riff_len(&m, 4); break;
    case 3:  img_chunk(&m, "fmt ", &f, fcb); img_chunk(&m, "data", NULL, dn); img_end(&m); img_set_riff_len(&m, 0x7FFFFFFF); break;
    case 4:  img_end(&m); break;
    case 6:  img_chunk(&m, "data", NULL, dn); img_end(&m); break;
    case 7:  img_chunk(&m, "data", NULL, dn); img_chunk(&m, "fmt ", &f, fcb); img_end(&m); break;
    case 8:  img_chunk(&m, "FMT ", &f, fcb); img_chunk(&m, "data", NULL, dn); img_end(&m); break;
    case 9:  img_chunk(&m, "fmt ", &f, fcb); img_chunk(&m, "DATA", NULL, dn); img_end(&m); break;
    case 10: img_chunk_decl(&m, "fmt ", &f, 16, 0xFFFFFFF0); img_chunk(&m, "data", NULL, dn); img_end(&m); break;
    case 11: img_chunk(&m, "fmt ", &f, fcb); img_chunk(&m, "data", NULL, dn); img_end(&m); img_set_riff_len(&m, 4 + 24 + 4); break;
    }
    out->what = what; out->want = want;
    return m.n;
}

static void parser_rows(int lo, int hi, int all_eps) {
    REQUIRE(ps_have_all()); ps_quiesce(); ps_sweep_begin();
    for (int r = lo; r <= hi; r++) {
        prow pr; if (!build_row(r, &pr)) continue;
        for (int ep = 0; ep < EP_N; ep++) {
            if (!all_eps && ep != EP_A && ep != EP_W && ep != EP_SNDW) continue;
            build_row(r, &pr);
            DWORD t0 = GetTickCount();
            BOOL got = ps_call(ep, PSZ_RAW, g_img, NULL, SND_MEMORY | SND_NODEFAULT);
            DWORD ms = GetTickCount() - t0;
            if (pr.want == P_BAD) PS_EXPECT(!got, "%s [%d] %s: got %d want 0", ps_ep_name[ep], r, pr.what, got);
            else if (pr.want == P_PCM) PS_EXPECT(!!got == PS_SYNC_VALID_EXPECT(), "%s [%d] %s: got %d want %d (ndev %u)", ps_ep_name[ep], r, pr.what, got, PS_SYNC_VALID_EXPECT(), ps_ndev());
            else if (ep == EP_W) t_note("[%d] %s: sync -> %d in %lu ms (ndev %u)", r, pr.what, got, ms, ps_ndev());
            if (pr.want == P_BAD) PS_EXPECT(ms < 2000, "%s [%d] %s: rejection took %lu ms", ps_ep_name[ep], r, pr.what, ms);
            ps_stop();
        }
    }
    PS_SWEEP_END(); ps_stop();
}
PTEST(sync_parser_accepted_pcm)   { parser_rows(0, 19, 0); }
PTEST(sync_parser_rejected)       { parser_rows(20, 59, 1); }
PTEST(sync_parser_device_defined) { parser_rows(60, 79, 0); }

/* Rejections do not depend on SND_LOOP, NOSTOP (nothing playing), PURGE, NOWAIT or SENTRY. */
PTEST(sync_parser_rejected_with_flags) {
    REQUIRE(U(PlaySoundW)); ps_quiesce(); ps_sweep_begin();
    static const DWORD fl[] = { SND_LOOP, SND_NOSTOP, SND_PURGE, SND_NOWAIT, SND_SENTRY, SND_RING, SND_APPLICATION,
                                SND_SYSTEM, SND_FILENAME, SND_ALIAS };
    for (int r = 20; r < 60; r++) {
        prow pr; if (!build_row(r, &pr)) continue;
        for (size_t i = 0; i < sizeof fl / sizeof fl[0]; i++) {
            build_row(r, &pr);
            BOOL got = U(PlaySoundW)((LPCWSTR)g_img, NULL, SND_MEMORY | SND_NODEFAULT | fl[i]);
            PS_EXPECT(!got, "[%d] %s | 0x%lx got %d", r, pr.what, fl[i], got);
        }
    }
    PS_SWEEP_END(); ps_stop();
}

/* soundInitWavHdr moves a misaligned fmt block down to a 4-byte boundary inside the image itself: for a sync play
 * that is the caller's buffer. Observed and recorded, not required. */
PTEST(sync_misaligned_fmt_buffer_note) {
    REQUIRE(U(PlaySoundW)); ps_quiesce();
    prow pr; DWORD n = build_row(65, &pr);
    static BYTE before[256]; memcpy(before, g_img, sizeof before);
    BOOL got = U(PlaySoundW)((LPCWSTR)g_img, NULL, SND_MEMORY | SND_NODEFAULT);
    int diff = -1; for (int i = 0; i < 256; i++) if (before[i] != g_img[i]) { diff = i; break; }
    t_note("misaligned fmt: sync -> %d, first changed byte %d (fmt data at %d)", got, diff, 12 + 8 + 3 + 8);
    ps_stop();
}

/* ---- device-gated: looping, NOSTOP, replacement -------------------------------------------------------------- */

/* With a device an async loop keeps a wave header current: NOSTOP calls fail (even the stop request) until a plain
 * stop. With no device the loop never opens, so NOSTOP calls behave as if idle. */
PTEST(async_loop_and_nostop) {
    REQUIRE(U(PlaySoundW)); REQUIRE(U(PlaySoundA)); ps_quiesce();
    ps_make_silent(g_img, sizeof g_img);
    CHECK(U(PlaySoundW)((LPCWSTR)g_img, NULL, SND_ASYNC | SND_LOOP | SND_MEMORY | SND_NODEFAULT));
    BOOL playing = FALSE;
    if (ps_ndev() == 0) { Sleep(300); playing = ps_is_playing(); }
    else playing = ps_wait_playing(3000);
    if (ps_ndev() == 0) {
        CHECK(!playing);
        CHECK(U(PlaySoundW)(NULL, NULL, SND_NOSTOP));
        CHECK(U(PlaySoundW)(L"", NULL, SND_NOSTOP | SND_NODEFAULT));
    } else {
        CHECKF(playing, "loop not current after 3 s");
        if (playing) {
            static BYTE other[4096]; ps_make_silent(other, sizeof other);
            CHECK(!U(PlaySoundW)((LPCWSTR)other, NULL, SND_ASYNC | SND_MEMORY | SND_NOSTOP | SND_NODEFAULT));
            CHECK(!U(PlaySoundW)((LPCWSTR)other, NULL, SND_MEMORY | SND_NOSTOP | SND_NODEFAULT));
            CHECK(!U(PlaySoundW)(NULL, NULL, SND_NOSTOP));
            CHECK(!U(PlaySoundA)("", NULL, SND_NOSTOP | SND_NODEFAULT));
            CHECK(!U(PlaySoundW)(L"C:\\i7ps_nodir\\x.wav", NULL, SND_ASYNC | SND_NOSTOP | SND_NODEFAULT));
            /* bad flags still fail first; an empty name without NOSTOP returns TRUE and does not stop the loop */
            CHECK(!U(PlaySoundW)(NULL, NULL, SND_NOSTOP | 0x20));
            CHECK(U(PlaySoundW)(L"", NULL, SND_NODEFAULT));
            CHECK(ps_is_playing());
            CHECK(ps_is_playing());
        }
    }
    CHECK(U(PlaySoundW)(NULL, NULL, 0));
    CHECK(!ps_is_playing());
    CHECK(U(PlaySoundW)(NULL, NULL, SND_NOSTOP));
    ps_quiesce();
}

/* A sync play replaces whatever is current and leaves nothing current afterwards. */
PTEST(sync_play_replaces_async_loop) {
    REQUIRE(U(PlaySoundW)); ps_quiesce();
    ps_make_silent(g_img, sizeof g_img);
    static BYTE second[4096]; ps_make_silent(second, sizeof second);
    CHECK(U(PlaySoundW)((LPCWSTR)g_img, NULL, SND_ASYNC | SND_LOOP | SND_MEMORY | SND_NODEFAULT));
    Sleep(300);
    DWORD t0 = GetTickCount();
    BOOL got = U(PlaySoundW)((LPCWSTR)second, NULL, SND_MEMORY | SND_NODEFAULT);
    DWORD ms = GetTickCount() - t0;
    EXPECT_EQ(!!got, PS_SYNC_VALID_EXPECT());
    CHECKF(ms < 3000, "sync play took %lu ms", ms);
    CHECK(!ps_is_playing());
    ps_quiesce();
}

/* SND_LOOP without SND_ASYNC plays once (soundOpen loops only for ASYNC|LOOP) and returns. */
PTEST(sync_loop_plays_once) {
    REQUIRE(U(PlaySoundW)); ps_quiesce();
    ps_make_silent(g_img, sizeof g_img);
    DWORD t0 = GetTickCount();
    BOOL got = U(PlaySoundW)((LPCWSTR)g_img, NULL, SND_LOOP | SND_MEMORY | SND_NODEFAULT);
    DWORD ms = GetTickCount() - t0;
    EXPECT_EQ(!!got, PS_SYNC_VALID_EXPECT());
    CHECKF(ms < 3000, "sync loop returned after %lu ms", ms);
    CHECK(!ps_is_playing());
    t_note("sync LOOP of 100 ms image: %d after %lu ms", got, ms);
    ps_quiesce();
}

/* With no wave device the open fails at once; with one the sync call returns after about the image length. */
PTEST(sync_valid_image_device_contract) {
    REQUIRE(ps_have_all()); ps_quiesce();
    for (int ep = 0; ep < EP_N; ep++) {
        ps_make_silent(g_img, sizeof g_img);
        DWORD t0 = GetTickCount();
        BOOL got = ps_call(ep, PSZ_RAW, g_img, NULL, SND_MEMORY | SND_NODEFAULT);
        DWORD ms = GetTickCount() - t0;
        CHECKF(!!got == PS_SYNC_VALID_EXPECT(), "%s got %d ndev %u", ps_ep_name[ep], got, ps_ndev());
        CHECKF(ms < 3000, "%s took %lu ms", ps_ep_name[ep], ms);
        if (ps_ndev() == 0) CHECKF(ms < 1000, "%s no-device failure took %lu ms", ps_ep_name[ep], ms);
        if (ep == EP_W) t_note("sync 100 ms image: %d in %lu ms", got, ms);
    }
    ps_stop();
}
