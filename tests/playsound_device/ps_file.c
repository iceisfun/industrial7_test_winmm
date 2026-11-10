/* playsound_device: SND_FILENAME plays of silent WAVE files written to this process's scratch directory.
 *
 * Decomp (winmm sndPlaySoundI / sndMessage / soundLoadFile): the file is read whole into a LocalAlloc block and
 * parsed with soundInitWavHdr bounded by the real file size: RIFF length > file size -> fail, a chunk (including
 * data) that runs past the end of the file -> fail. An absolute name that does not exist gets ".WAV" appended.
 * Not found / unparsable + SND_NODEFAULT -> FALSE (synchronous) and nothing plays (asynchronous).
 * Every call passes SND_NODEFAULT: a failure must never fall back to the default system sound.
 */
#include "ps.h"

#define DEV (ps_ndev() > 0)
#define FSYNC   (PS_FILENAME | PS_SYNC | PS_NODEFAULT)
#define FASYNC  (PS_FILENAME | PS_ASYNC | PS_NODEFAULT)
#define FLOOP   (PS_FILENAME | PS_ASYNC | PS_LOOP | PS_NODEFAULT)

static int g_seq;
static int mkfile(wb_t *w, DWORD nbytes, WCHAR *path, const char *stem) {
    char leaf[64]; snprintf(leaf, sizeof leaf, "%s_%d.wav", stem, ++g_seq);
    return ps_write_file(w, nbytes, leaf, path, MAX_PATH);
}

TEST(psd_file_sync_plays) {
    PS_REQ();
    wb_t w; WCHAR p[MAX_PATH]; ps_silent(&w, WAVE_FORMAT_PCM, 1, 8000, 16, 300);
    REQUIRE(mkfile(&w, w.n, p, "ok"));
    double ms; BOOL r = ps_timed(psW, p, FSYNC, &ms);
    t_note("file sync: r=%d %.0f ms", r, ms);
    if (DEV) { CHECK(r == TRUE); CHECKF(ms >= 150.0 && ms < 3000.0, "%.0f ms", ms); }
    else CHECK(r == FALSE);
    ps_stop(); wb_free(&w);
}

TEST(psd_file_sync_ansi_and_snd) {
    PS_REQ();
    wb_t w; WCHAR p[MAX_PATH]; char pa[MAX_PATH]; ps_silent(&w, WAVE_FORMAT_PCM, 1, 11025, 8, 100);
    REQUIRE(mkfile(&w, w.n, p, "ansi"));
    WideCharToMultiByte(CP_ACP, 0, p, -1, pa, sizeof pa, NULL, NULL);
    BOOL e = DEV ? TRUE : FALSE;
    CHECK(psA(pa, FSYNC) == e);
    CHECK(psW(p, FSYNC) == e);
    /* sndPlaySound has no SND_FILENAME: the name goes through the alias lookup first, then the file search. */
    CHECK(spsW(p, PS_SYNC | PS_NODEFAULT) == e);
    CHECK(spsA(pa, PS_SYNC | PS_NODEFAULT) == e);
    ps_stop(); wb_free(&w);
}

TEST(psd_file_alias_flag_falls_back_to_file) {
    PS_REQ();
    wb_t w; WCHAR p[MAX_PATH]; ps_silent(&w, WAVE_FORMAT_PCM, 1, 8000, 16, 80);
    REQUIRE(mkfile(&w, w.n, p, "alias"));
    /* No registry alias has this name: the name is then tried as a file. */
    BOOL r = psW(p, PS_ALIAS | PS_SYNC | PS_NODEFAULT);
    t_note("SND_ALIAS with a file path: %d", r);
    CHECK(r == (DEV ? TRUE : FALSE));
    CHECK(psW(p, PS_SYNC | PS_NODEFAULT) == (DEV ? TRUE : FALSE));
    ps_stop(); wb_free(&w);
}

TEST(psd_file_wav_extension_appended) {
    PS_REQ();
    wb_t w; WCHAR p[MAX_PATH]; ps_silent(&w, WAVE_FORMAT_PCM, 1, 8000, 16, 80);
    REQUIRE(mkfile(&w, w.n, p, "noext"));
    size_t n = wcslen(p); REQUIRE(n > 4);
    p[n - 4] = 0;                       /* strip ".wav": the absolute name does not exist, "<name>.WAV" does */
    BOOL r = psW(p, FSYNC);
    t_note("absolute path without extension: %d", r);
    CHECK(r == (DEV ? TRUE : FALSE));
    ps_stop(); wb_free(&w);
}

TEST(psd_file_missing_false) {
    PS_REQ();
    WCHAR p[MAX_PATH]; char a[MAX_PATH]; t_tmpfile(a, sizeof a, "does_not_exist_ps.wav");
    MultiByteToWideChar(CP_ACP, 0, a, -1, p, MAX_PATH);
    double ms; BOOL r = ps_timed(psW, p, FSYNC, &ms);
    CHECK(r == FALSE);
    CHECKF(ms < 1000.0, "%.0f ms", ms);
    CHECK(psA(a, FSYNC) == FALSE);
    CHECK(ps_playing() == 0);
}

TEST(psd_file_async_missing_posts_true) {
    PS_REQ();
    WCHAR p[MAX_PATH]; char a[MAX_PATH]; t_tmpfile(a, sizeof a, "does_not_exist_async.wav");
    MultiByteToWideChar(CP_ACP, 0, a, -1, p, MAX_PATH);
    /* ASYNC: only the post happens on the calling thread; the lookup fails later on the notify thread. */
    CHECK(psW(p, FASYNC) == TRUE);
    Sleep(300);
    CHECK(ps_playing() == 0);
    ps_stop();
}

typedef struct { const char *name; int kind; int exp; } filecase;
enum { F_TRUNC_DATA, F_RIFF_GT_FILE, F_HEADER_ONLY, F_EMPTY, F_12BYTES, F_RIFF_LT_FILE, F_JUNK_TAIL, F_NOT_RIFF,
       F_FLOAT, F_EXT, F_8BIT, F_RIFF_SLACK, F_N };
static const filecase g_fc[F_N] = {
    [F_TRUNC_DATA]   = { "data_runs_past_eof",  F_TRUNC_DATA, 0 },
    [F_RIFF_GT_FILE] = { "riff_len_gt_file",    F_RIFF_GT_FILE, 0 },
    [F_HEADER_ONLY]  = { "header_only_44",      F_HEADER_ONLY, 0 },
    [F_EMPTY]        = { "empty_file",          F_EMPTY, 0 },
    [F_12BYTES]      = { "riff_wave_only",      F_12BYTES, 0 },
    [F_RIFF_LT_FILE] = { "riff_len_lt_file",    F_RIFF_LT_FILE, 1 },
    [F_JUNK_TAIL]    = { "trailing_garbage",    F_JUNK_TAIL, 1 },
    [F_NOT_RIFF]     = { "not_riff",            F_NOT_RIFF, 0 },
    [F_FLOAT]        = { "float32_stereo",      F_FLOAT, 1 },
    [F_EXT]          = { "extensible_pcm16",    F_EXT, 1 },
    [F_8BIT]         = { "pcm8_mono_11025",     F_8BIT, 1 },
    [F_RIFF_SLACK]   = { "riff_len_eq_file",    F_RIFF_SLACK, 1 },
};
static DWORD build_file(wb_t *w, int k) {
    switch (k) {
    case F_TRUNC_DATA: ps_silent(w, 1, 1, 8000, 16, 200); wb_set_riff(w, w->n - 100 - 8); return w->n - 100; /* data past EOF */
    case F_RIFF_GT_FILE: ps_silent(w, 1, 1, 8000, 16, 200); wb_set_riff(w, w->n + 1); return w->n;   /* RIFF > file size */
    case F_RIFF_SLACK: ps_silent(w, 1, 1, 8000, 16, 200); wb_set_riff(w, w->n); return w->n;   /* RIFF == file size: allowed */
    case F_HEADER_ONLY: ps_silent(w, 1, 1, 8000, 16, 200); wb_set_riff(w, 36); return 44 - 8 + 8; /* "data" hdr, no bytes */
    case F_EMPTY: wb_init(w, 64); return 0;
    case F_12BYTES: wb_init(w, 64); wb_set_riff(w, 4); return 12;
    case F_RIFF_LT_FILE: ps_silent(w, 1, 1, 8000, 16, 200); wb_set_riff(w, w->n - 8 - 400); return w->n;
    case F_JUNK_TAIL: ps_silent(w, 1, 1, 8000, 16, 200); return w->n;
    case F_NOT_RIFF: ps_silent(w, 1, 1, 8000, 16, 200); memcpy(w->b, "RIFX", 4); return w->n;
    case F_FLOAT: ps_silent(w, 3, 2, 44100, 32, 150); return w->n;
    case F_EXT: wb_init(w, 0x8000); wb_fmt_ext(w, 2, 44100, 16, 16, 3, &ps_sub_pcm, 0); wb_data(w, 17640, 0); wb_finish(w); return w->n;
    case F_8BIT: ps_silent(w, 1, 1, 11025, 8, 150); return w->n;
    }
    return 0;
}
static void run_file(int k) {
    wb_t w; WCHAR p[MAX_PATH]; DWORD n = build_file(&w, k);
    if (k == F_JUNK_TAIL) {        /* file = image + 37 bytes of 0x7F after the RIFF payload */
        BYTE *b = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, n + 64); memcpy(b, w.b, n); memset(b + n, 0x7F, 37);
        HeapFree(GetProcessHeap(), 0, w.b); w.b = b; w.cap = n + 64; n += 37;
    }
    if (!mkfile(&w, n, p, g_fc[k].name)) { t_skip("cannot write scratch file"); wb_free(&w); return; }
    double ms; BOOL r = ps_timed(psW, p, FSYNC, &ms);
    t_note("file %s (%lu bytes): r=%d %.0f ms", g_fc[k].name, n, r, ms);
    if (DEV) CHECKF(r == (BOOL)g_fc[k].exp, "%s: got %d want %d", g_fc[k].name, r, g_fc[k].exp);
    else CHECKF(r == FALSE, "%s: got %d with 0 devices", g_fc[k].name, r);
    CHECKF(ms < 3000.0, "%.0f ms", ms);
    /* The same file asynchronously: always posted; it plays only if the sync play could. */
    CHECK(psW(p, FLOOP) == TRUE);
    if (DEV) {
        int pl = ps_wait_playing(1, g_fc[k].exp ? 2000 : 400);
        CHECKF(pl == g_fc[k].exp, "%s: async loop playing=%d", g_fc[k].name, pl);
    }
    ps_stop();
    CHECK(ps_playing() == 0);
    wb_free(&w);
}
#define FC(k) TEST(psd_file_case_##k) { PS_REQ(); run_file(k); }
FC(F_TRUNC_DATA) FC(F_RIFF_GT_FILE) FC(F_HEADER_ONLY) FC(F_EMPTY) FC(F_12BYTES) FC(F_RIFF_LT_FILE) FC(F_JUNK_TAIL)
FC(F_NOT_RIFF) FC(F_FLOAT) FC(F_EXT) FC(F_8BIT) FC(F_RIFF_SLACK)

TEST(psd_file_same_file_twice) {
    PS_REQ();
    wb_t w; WCHAR p[MAX_PATH]; ps_silent(&w, WAVE_FORMAT_PCM, 1, 8000, 16, 100);
    REQUIRE(mkfile(&w, w.n, p, "twice"));
    BOOL e = DEV ? TRUE : FALSE;
    for (int i = 0; i < 3; i++) CHECK(psW(p, FSYNC) == e);
    ps_stop(); wb_free(&w);
}

TEST(psd_file_loop_then_stop) {
    PS_REQ();
    wb_t w; WCHAR p[MAX_PATH]; ps_silent(&w, WAVE_FORMAT_PCM, 1, 8000, 16, 100);
    REQUIRE(mkfile(&w, w.n, p, "loop"));
    CHECK(psW(p, FLOOP) == TRUE);
    if (DEV) { CHECK(ps_wait_playing(1, 2000)); Sleep(300); CHECK(ps_playing() == 1); }
    ps_stop();
    CHECK(ps_playing() == 0);
    wb_free(&w);
}

TEST(psd_file_loop_survives_file_delete) {
    PS_REQ();
    if (!DEV) { t_skip("needs a wave device"); return; }
    wb_t w; WCHAR p[MAX_PATH]; ps_silent(&w, WAVE_FORMAT_PCM, 1, 8000, 16, 100);
    REQUIRE(mkfile(&w, w.n, p, "del"));
    CHECK(psW(p, FLOOP) == TRUE);
    CHECK(ps_wait_playing(1, 2000));
    /* The whole file was read into memory and the handle closed: deleting it does not disturb the loop. */
    CHECK(DeleteFileW(p));
    Sleep(250);
    CHECK(ps_playing() == 1);
    ps_stop(); wb_free(&w);
}

TEST(psd_file_nostop_while_file_loop) {
    PS_REQ();
    if (!DEV) { t_skip("needs a wave device"); return; }
    wb_t w; WCHAR p[MAX_PATH]; ps_silent(&w, WAVE_FORMAT_PCM, 1, 8000, 16, 100);
    REQUIRE(mkfile(&w, w.n, p, "nostop"));
    CHECK(psW(p, FLOOP) == TRUE);
    CHECK(ps_wait_playing(1, 2000));
    CHECK(psW(p, FSYNC | PS_NOSTOP) == FALSE);
    CHECK(psW(p, FASYNC | PS_NOSTOP) == FALSE);
    ps_stop();
    CHECK(psW(p, FSYNC | PS_NOSTOP) == TRUE);
    ps_stop(); wb_free(&w);
}

TEST(psd_file_relative_name_current_dir) {
    PS_REQ();
    wb_t w; WCHAR p[MAX_PATH], old[MAX_PATH]; ps_silent(&w, WAVE_FORMAT_PCM, 1, 8000, 16, 80);
    REQUIRE(ps_write_file(&w, w.n, "psrel_cd.wav", p, MAX_PATH));
    REQUIRE(GetCurrentDirectoryW(MAX_PATH, old));
    WCHAR dir[MAX_PATH]; MultiByteToWideChar(CP_ACP, 0, t_tmpdir(), -1, dir, MAX_PATH);
    REQUIRE(SetCurrentDirectoryW(dir));
    BOOL r1 = psW(L"psrel_cd.wav", FSYNC);
    BOOL r2 = psW(L"psrel_cd", FSYNC);            /* SearchPath default extension ".WAV" */
    SetCurrentDirectoryW(old);
    t_note("relative name in the current dir: %d, without extension: %d", r1, r2);
    CHECK(r1 == (DEV ? TRUE : FALSE));
    CHECK(r2 == (DEV ? TRUE : FALSE));
    ps_stop(); wb_free(&w);
}

TEST(psd_file_memory_parity) {
    PS_REQ();
    /* The same bytes as a file and as a memory image give the same result for well-formed images. */
    static const struct { WORD tag, ch; DWORD rate; WORD bits; } t[] = {
        { 1, 1, 11025, 8 }, { 1, 2, 44100, 16 }, { 3, 2, 48000, 32 }, { 1, 2, 48000, 24 } };
    for (int i = 0; i < 4; i++) {
        wb_t w; WCHAR p[MAX_PATH]; ps_silent(&w, t[i].tag, t[i].ch, t[i].rate, t[i].bits, 80);
        if (!mkfile(&w, w.n, p, "parity")) { wb_free(&w); continue; }
        BOOL a = psW(p, FSYNC), b = psW(w.b, PS_MEMSYNC);
        CHECKF(a == b, "case %d: file %d memory %d", i, a, b);
        wb_free(&w);
    }
    ps_stop();
}
