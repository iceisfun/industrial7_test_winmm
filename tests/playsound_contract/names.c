#include "ps.h"
/* Names: SND_ALIAS_ID ids, alias names, file search, name length limits, ANSI conversion.
 *
 * sndPlaySoundI name path (winmm): SND_ALIAS_ID -> TransAlias(id) (the eight SND_ALIAS_SYSTEM* ids map to string
 * ids 3000..3007, any other id is used as is) -> LoadStringW(winmm, id, buf, 256), no string -> FALSE. Otherwise the
 * name is copied into a 256-WCHAR buffer (StringCbCopyW 0x200: 256 chars or more -> empty -> NODEFAULT -> FALSE),
 * looked up as an alias, then as a file unless SND_SYSTEM:
 *   rooted path: GetFileAttributesW, missing -> append ".WAV" if it fits in the 256-WCHAR buffer and hand it on
 *   (sync: sndMessage/soundLoadFile fails -> FALSE; async: posted -> TRUE); if ".WAV" does not fit, fall through;
 *   otherwise: GetFileAttributesW(name) relative to the current directory, then SearchPathW(NULL, name, ".WAV"),
 *   then SearchPathW(MediaPath); nothing -> FALSE under SND_NODEFAULT.
 * PlaySoundA converts names (psz > 0xFFFF and not plain MEMORY) with MultiByteToWideChar(CP_ACP) into 256 WCHARs;
 * a name of 256 or more characters fails the conversion -> FALSE.
 * Files here are silent images this test writes into its own scratch directory.
 * VM_TIMEOUT: 120 */

static BYTE g_buf[65536];

/* ---- SND_ALIAS_ID ------------------------------------------------------------------------------------------------ */

static const WORD g_known_ids[] = { SND_ALIAS_SYSTEMASTERISK, SND_ALIAS_SYSTEMQUESTION, SND_ALIAS_SYSTEMHAND,
    SND_ALIAS_SYSTEMEXIT, SND_ALIAS_SYSTEMSTART, SND_ALIAS_SYSTEMWELCOME, SND_ALIAS_SYSTEMEXCLAMATION,
    SND_ALIAS_SYSTEMDEFAULT };

PTEST(alias_id_constants) {
    EXPECT_EQ(SND_ALIAS_SYSTEMASTERISK, 0x2A53); EXPECT_EQ(SND_ALIAS_SYSTEMQUESTION, 0x3F53);
    EXPECT_EQ(SND_ALIAS_SYSTEMHAND, 0x4853); EXPECT_EQ(SND_ALIAS_SYSTEMEXIT, 0x4553);
    EXPECT_EQ(SND_ALIAS_SYSTEMSTART, 0x5353); EXPECT_EQ(SND_ALIAS_SYSTEMWELCOME, 0x5753);
    EXPECT_EQ(SND_ALIAS_SYSTEMEXCLAMATION, 0x2153); EXPECT_EQ(SND_ALIAS_SYSTEMDEFAULT, 0x4453);
    EXPECT_EQ(SND_ALIAS_ID, SND_ALIAS | SND_RING);
}

/* The eight system ids name strings 3000..3007 in winmm's own string table (never played here). */
PTEST(alias_id_known_ids_have_winmm_strings) {
    HMODULE w = t_module("winmm.dll"); REQUIRE(w);
    static const UINT sid[] = { 3000, 3001, 3002, 3003, 3004, 3005, 3006, 3007 };
    for (int i = 0; i < 8; i++) {
        WCHAR s[256] = { 0 };
        int n = LoadStringW(w, sid[i], s, 256);
        CHECKF(n > 0, "string %u", sid[i]);
        t_note("winmm string %u = \"%ls\"", sid[i], s);
    }
}

static int id_is_free(WORD id) {
    for (int i = 0; i < 8; i++) if (g_known_ids[i] == id) return 0;
    if (id >= 3000 && id <= 3007) return 0;
    WCHAR s[256];
    return LoadStringW(t_module("winmm.dll"), id, s, 256) == 0;
}

/* An id with no winmm string fails before anything is looked up or posted, sync or async. */
PTEST(alias_id_unknown_fails) {
    REQUIRE(ps_have_all()); ps_quiesce(); ps_sweep_begin();
    static const WORD ids[] = { 0x7A7A, 0x1234, 0x5A5A, 0x4141, 0x2020, 0x7FFF, 0xFFFF, 0xFFFE, 0xBEEF, 0x0101,
                                0x2A54, 0x5354, 0x4452, 0x00FF, 0x0002 };
    int ran = 0;
    for (size_t i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        if (!id_is_free(ids[i])) { t_note("id 0x%04x has a winmm string; not sent", ids[i]); continue; }
        ran++;
        for (int ep = 0; ep < EP_N; ep++) {
            const void *p = (const void *)(ULONG_PTR)ids[i];
            BOOL s = ps_call(ep, PSZ_RAW, p, NULL, SND_ALIAS_ID | SND_NODEFAULT);
            BOOL a = ps_call(ep, PSZ_RAW, p, NULL, SND_ALIAS_ID | SND_ASYNC | SND_NODEFAULT);
            BOOL ap = ps_call(ep, PSZ_RAW, p, NULL, SND_ALIAS_ID | SND_APPLICATION | SND_ASYNC | SND_NODEFAULT);
            BOOL sy = ps_call(ep, PSZ_RAW, p, NULL, SND_ALIAS_ID | SND_SYSTEM | SND_ASYNC | SND_NODEFAULT);
            PS_EXPECT(!s && !a && !ap && !sy, "%s id 0x%04x: sync %d async %d app %d system %d", ps_ep_name[ep], ids[i], s, a, ap, sy);
        }
    }
    CHECKF(ran >= 10, "only %d ids free", ran);
    PS_SWEEP_END(); ps_stop();
}

/* ---- alias names ------------------------------------------------------------------------------------------------ */

PTEST(alias_unknown_names_fail) {
    REQUIRE(ps_have_all()); ps_quiesce(); ps_sweep_begin();
    static const WCHAR *names[] = { L"i7ps_unknown_event", L"I7PS_UNKNOWN_EVENT_2", L"i7ps.unknown.event",
        L"i7ps unknown event", L"i7ps_unknown_\x00e9v\x00e9nement", L".i7psDefault", L"i7psSystemHand" };
    static const DWORD fl[] = { SND_ALIAS, SND_ALIAS | SND_ASYNC, SND_ALIAS | SND_APPLICATION, SND_ALIAS | SND_APPLICATION | SND_ASYNC,
        SND_ALIAS | SND_ASYNC | SND_NOSTOP, SND_ALIAS | SND_ASYNC | SND_NOWAIT, SND_ALIAS | SND_ASYNC | SND_SENTRY,
        SND_ALIAS | SND_LOOP | SND_ASYNC, SND_APPLICATION, SND_APPLICATION | SND_ASYNC, 0, SND_ASYNC };
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
        for (size_t j = 0; j < sizeof fl / sizeof fl[0]; j++)
            for (int ep = 0; ep < EP_N; ep++) {
                BOOL got = ps_call(ep, PSZ_WIDE, names[i], NULL, fl[j] | SND_NODEFAULT);
                PS_EXPECT(!got, "%s %ls f=0x%lx got %d", ps_ep_name[ep], names[i], fl[j] | SND_NODEFAULT, got);
            }
    PS_SWEEP_END(); ps_stop();
}

/* SND_ALIAS|SND_SYSTEM skips the file search: an unknown alias is posted (async TRUE), and fails when sync. */
PTEST(alias_system_skips_file_search) {
    REQUIRE(ps_have_all()); ps_quiesce();
    for (int ep = 0; ep < EP_N; ep++) {
        CHECKF(ps_call(ep, PSZ_WIDE, L"i7ps_unknown_sys", NULL, SND_ALIAS | SND_SYSTEM | SND_ASYNC | SND_NODEFAULT), "%s async", ps_ep_name[ep]);
        CHECKF(!ps_call(ep, PSZ_WIDE, L"i7ps_unknown_sys", NULL, SND_ALIAS | SND_SYSTEM | SND_NODEFAULT), "%s sync", ps_ep_name[ep]);
    }
    ps_stop();
}

/* ---- name length ------------------------------------------------------------------------------------------------ */

static void rooted_name(WCHAR *out, int len) {
    /* "C:\i7ps_nodir\" + 'a'... + ".wav", exactly len characters */
    wcscpy(out, L"C:\\i7ps_nodir\\");
    int pre = (int)wcslen(out);
    for (int i = pre; i < len - 4; i++) out[i] = L'a';
    wcscpy(out + len - 4, L".wav");
}

/* Async rooted missing paths are posted while path + ".WAV" fits in the 256-WCHAR buffer (len <= 251); from 252
 * the extension no longer fits, the file search runs and fails; from 256 the copy (and the ANSI conversion) fails. */
PTEST(rooted_length_boundary_async) {
    REQUIRE(ps_have_all()); ps_quiesce(); ps_sweep_begin();
    static const int lens[] = { 30, 100, 200, 240, 248, 249, 250, 251, 252, 253, 254, 255, 256, 257, 259, 260, 261, 300, 511, 1000 };
    static WCHAR name[1100];
    for (size_t i = 0; i < sizeof lens / sizeof lens[0]; i++)
        for (int ep = 0; ep < EP_N; ep++) {
            rooted_name(name, lens[i]);
            if ((int)wcslen(name) != lens[i]) { t_skip("name build"); return; }
            BOOL got = ps_call(ep, PSZ_WIDE, name, NULL, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
            PS_EXPECT(!!got == (lens[i] <= 251), "%s len %d got %d want %d", ps_ep_name[ep], lens[i], got, lens[i] <= 251);
            got = ps_call(ep, PSZ_WIDE, name, NULL, SND_FILENAME | SND_NODEFAULT);
            PS_EXPECT(!got, "%s len %d sync got %d", ps_ep_name[ep], lens[i], got);
        }
    PS_SWEEP_END(); ps_stop();
}

/* Same boundary without SND_FILENAME (alias lookup first, then the same file path). */
PTEST(rooted_length_boundary_alias_async) {
    REQUIRE(ps_have_all()); ps_quiesce(); ps_sweep_begin();
    static const int lens[] = { 200, 250, 251, 252, 255, 256, 300 };
    static WCHAR name[400];
    for (size_t i = 0; i < sizeof lens / sizeof lens[0]; i++)
        for (int ep = 0; ep < EP_N; ep++) {
            rooted_name(name, lens[i]);
            BOOL got = ps_call(ep, PSZ_WIDE, name, NULL, SND_ASYNC | SND_NODEFAULT);
            PS_EXPECT(!!got == (lens[i] <= 251), "%s len %d got %d", ps_ep_name[ep], lens[i], got);
            got = ps_call(ep, PSZ_WIDE, name, NULL, SND_ALIAS | SND_ASYNC | SND_NODEFAULT);
            PS_EXPECT(!!got == (lens[i] <= 251), "%s alias len %d got %d", ps_ep_name[ep], lens[i], got);
        }
    PS_SWEEP_END(); ps_stop();
}

/* Long relative names never resolve. */
PTEST(relative_long_names_fail) {
    REQUIRE(ps_have_all()); ps_quiesce(); ps_sweep_begin();
    static const int lens[] = { 200, 255, 256, 260, 300, 1000 };
    static WCHAR name[1100];
    for (size_t i = 0; i < sizeof lens / sizeof lens[0]; i++)
        for (int ep = 0; ep < EP_N; ep++) {
            for (int k = 0; k < lens[i]; k++) name[k] = L'q';
            memcpy(name, L"i7ps", 8); name[lens[i]] = 0;
            BOOL got = ps_call(ep, PSZ_WIDE, name, NULL, SND_ASYNC | SND_NODEFAULT);
            PS_EXPECT(!got, "%s len %d got %d", ps_ep_name[ep], lens[i], got);
            got = ps_call(ep, PSZ_WIDE, name, NULL, SND_ALIAS | SND_ASYNC | SND_NODEFAULT);
            PS_EXPECT(!got, "%s alias len %d got %d", ps_ep_name[ep], lens[i], got);
        }
    PS_SWEEP_END(); ps_stop();
}

/* ---- scratch files ----------------------------------------------------------------------------------------------- */

typedef struct { const WCHAR *leaf; int kind; } sfile;
enum { F_OK, F_JUNK, F_EMPTY, F_BADFMT, F_TRUNC, F_RIFFBIG, F_RIFFPLUS8, F_SHORT, F_NODATA, F_STEREO8 };
static const sfile g_files[] = {
    { L"i7ps_ok.wav", F_OK }, { L"i7ps_noext", F_OK }, { L"i7ps_junk.wav", F_JUNK }, { L"i7ps_empty.wav", F_EMPTY },
    { L"i7ps_badfmt.wav", F_BADFMT }, { L"i7ps_trunc.wav", F_TRUNC }, { L"i7ps_riffbig.wav", F_RIFFBIG },
    { L"i7ps_riffplus8.wav", F_RIFFPLUS8 }, { L"i7ps_short.wav", F_SHORT }, { L"i7ps_nodata.wav", F_NODATA },
    { L"i7ps_st8.wav", F_STEREO8 }, { L"i7ps_other.WAV", F_OK },
};
#define NFILES (sizeof g_files / sizeof g_files[0])

static int file_parses(int kind) { return kind == F_OK || kind == F_RIFFPLUS8 || kind == F_STEREO8; }

static DWORD make_file_image(int kind) {
    ps_img m; DWORD n;
    switch (kind) {
    case F_OK: return ps_make_silent(g_buf, sizeof g_buf);
    case F_STEREO8: return ps_make_pcm(g_buf, sizeof g_buf, 2, 11025, 8, 100);
    case F_JUNK: memset(g_buf, 0x5A, 64); memcpy(g_buf, "JUNKJUNKJUNK", 12); return 64;
    case F_EMPTY: return 0;
    case F_BADFMT: n = ps_make_silent(g_buf, sizeof g_buf); g_buf[22] = g_buf[23] = 0; return n;  /* channels 0 */
    case F_TRUNC: n = ps_make_silent(g_buf, sizeof g_buf) - 100; m.b = g_buf; img_set_riff_len(&m, n - 8); return n; /* data runs past EOF */
    case F_RIFFBIG: n = ps_make_silent(g_buf, sizeof g_buf); m.b = g_buf; img_set_riff_len(&m, n); img_set_riff_len(&m, n + 1); return n;
    case F_RIFFPLUS8: n = ps_make_silent(g_buf, sizeof g_buf); m.b = g_buf; img_set_riff_len(&m, n); return n; /* == file size */
    case F_SHORT: memcpy(g_buf, "RIFF\x04\0\0\0WAV", 11); return 11;
    case F_NODATA: { ps_fmt f; fmt_pcm(&f, 1, 8000, 16); img_begin(&m, g_buf, sizeof g_buf); img_chunk(&m, "fmt ", &f, 16); return img_end(&m); }
    }
    return 0;
}

static int g_files_ready;
static int make_files(void) {
    if (g_files_ready) return g_files_ready > 0;
    g_files_ready = -1;
    for (size_t i = 0; i < NFILES; i++) {
        WCHAR p[MAX_PATH]; ps_pathW(p, MAX_PATH, g_files[i].leaf);
        DWORD n = make_file_image(g_files[i].kind);
        if (!ps_write_fileW(p, g_buf, n)) { t_note("cannot write %ls (%lu)", p, GetLastError()); return 0; }
    }
    WCHAR d[MAX_PATH]; ps_pathW(d, MAX_PATH, L"i7ps_dir.wav"); CreateDirectoryW(d, NULL);
    g_files_ready = 1;
    return 1;
}

static WCHAR g_oldcwd[MAX_PATH];
static int enter_tmp(void) {
    if (!make_files()) return 0;
    GetCurrentDirectoryW(MAX_PATH, g_oldcwd);
    return SetCurrentDirectoryW(ps_tmpdirW());
}
static void leave_tmp(void) { if (g_oldcwd[0]) SetCurrentDirectoryW(g_oldcwd); g_oldcwd[0] = 0; }

/* Relative names resolve against the current directory before any search path: existence decides an async call. */
PTEST(relative_async_existence_table) {
    REQUIRE(ps_have_all()); ps_quiesce();
    if (!enter_tmp()) { t_skip("scratch files"); return; }
    ps_sweep_begin();
    static const struct { const WCHAR *n; BOOL ok; } rows[] = {
        { L"i7ps_ok.wav", TRUE }, { L"i7ps_ok", TRUE }, { L"I7PS_OK.WAV", TRUE }, { L"i7ps_noext", TRUE },
        { L"i7ps_other", TRUE }, { L"i7ps_other.wav", TRUE }, { L".\\i7ps_ok.wav", TRUE }, { L"i7ps_junk.wav", TRUE },
        { L"i7ps_empty.wav", TRUE }, { L"i7ps_badfmt", TRUE }, { L"i7ps_dir.wav", TRUE },
        { L"i7ps_missing.wav", FALSE }, { L"i7ps_missing", FALSE }, { L"i7ps_noext.wav", FALSE }, { L"i7ps_ok.wav.wav", FALSE },
        { L"i7ps_o", FALSE }, { L"i7ps_ok.wa", FALSE },
    };
    static const DWORD fl[] = { SND_FILENAME | SND_ASYNC, SND_ASYNC, SND_ALIAS | SND_ASYNC, SND_ALIAS | SND_APPLICATION | SND_ASYNC };
    for (size_t r = 0; r < sizeof rows / sizeof rows[0]; r++)
        for (size_t j = 0; j < sizeof fl / sizeof fl[0]; j++)
            for (int ep = 0; ep < EP_N; ep++) {
                BOOL got = ps_call(ep, PSZ_WIDE, rows[r].n, NULL, fl[j] | SND_NODEFAULT);
                PS_EXPECT(!!got == rows[r].ok, "%s %ls f=0x%lx got %d want %d", ps_ep_name[ep], rows[r].n, fl[j], got, rows[r].ok);
                ps_stop();
            }
    PS_SWEEP_END(); ps_quiesce(); leave_tmp();
}

/* Sync plays of scratch files: a file the parser rejects (or that cannot be opened) fails whatever the devices; a
 * well-formed one succeeds only when there is a wave device. soundLoadFile bounds the parse by the file size. */
PTEST(relative_sync_file_table) {
    REQUIRE(ps_have_all()); ps_quiesce();
    if (!enter_tmp()) { t_skip("scratch files"); return; }
    ps_sweep_begin();
    for (size_t i = 0; i < NFILES; i++)
        for (int ep = 0; ep < EP_N; ep++) {
            if (file_parses(g_files[i].kind) && ep != EP_A && ep != EP_W) continue;
            BOOL got = ps_call(ep, PSZ_WIDE, g_files[i].leaf, NULL, SND_FILENAME | SND_NODEFAULT);
            BOOL want = file_parses(g_files[i].kind) ? PS_SYNC_VALID_EXPECT() : FALSE;
            PS_EXPECT(!!got == !!want, "%s %ls got %d want %d", ps_ep_name[ep], g_files[i].leaf, got, want);
        }
    /* a directory is found by GetFileAttributes but cannot be read */
    for (int ep = 0; ep < EP_N; ep++)
        PS_EXPECT(!ps_call(ep, PSZ_WIDE, L"i7ps_dir.wav", NULL, SND_FILENAME | SND_NODEFAULT), "%s dir", ps_ep_name[ep]);
    PS_SWEEP_END(); ps_stop(); leave_tmp();
}

/* Rooted paths to scratch files: full name, no extension (".WAV" appended), and a trailing dot (fails). */
PTEST(rooted_sync_file_forms) {
    REQUIRE(ps_have_all()); ps_quiesce();
    if (!make_files()) { t_skip("scratch files"); return; }
    WCHAR full[MAX_PATH], noext[MAX_PATH], dot[MAX_PATH], junk[MAX_PATH], miss[MAX_PATH];
    ps_pathW(full, MAX_PATH, L"i7ps_ok.wav"); ps_pathW(noext, MAX_PATH, L"i7ps_ok"); ps_pathW(dot, MAX_PATH, L"i7ps_ok.");
    ps_pathW(junk, MAX_PATH, L"i7ps_junk.wav"); ps_pathW(miss, MAX_PATH, L"i7ps_missing.wav");
    for (int ep = 0; ep < EP_N; ep++) {
        if (ep == EP_A || ep == EP_W) {
            CHECKF(!!ps_call(ep, PSZ_WIDE, full, NULL, SND_FILENAME | SND_NODEFAULT) == PS_SYNC_VALID_EXPECT(), "%s full", ps_ep_name[ep]);
            CHECKF(!!ps_call(ep, PSZ_WIDE, noext, NULL, SND_FILENAME | SND_NODEFAULT) == PS_SYNC_VALID_EXPECT(), "%s noext", ps_ep_name[ep]);
            CHECKF(!!ps_call(ep, PSZ_WIDE, full, NULL, SND_ALIAS | SND_NODEFAULT) == PS_SYNC_VALID_EXPECT(), "%s full via alias", ps_ep_name[ep]);
        }
        CHECKF(!ps_call(ep, PSZ_WIDE, dot, NULL, SND_FILENAME | SND_NODEFAULT), "%s trailing dot", ps_ep_name[ep]);
        CHECKF(!ps_call(ep, PSZ_WIDE, junk, NULL, SND_FILENAME | SND_NODEFAULT), "%s junk", ps_ep_name[ep]);
        CHECKF(!ps_call(ep, PSZ_WIDE, miss, NULL, SND_FILENAME | SND_NODEFAULT), "%s missing", ps_ep_name[ep]);
        /* async: rooted names are posted without an existence check */
        CHECKF(ps_call(ep, PSZ_WIDE, full, NULL, SND_FILENAME | SND_ASYNC | SND_NODEFAULT), "%s full async", ps_ep_name[ep]);
        CHECKF(ps_call(ep, PSZ_WIDE, dot, NULL, SND_FILENAME | SND_ASYNC | SND_NODEFAULT), "%s dot async", ps_ep_name[ep]);
        CHECKF(ps_call(ep, PSZ_WIDE, miss, NULL, SND_FILENAME | SND_ASYNC | SND_NODEFAULT), "%s missing async", ps_ep_name[ep]);
        CHECKF(ps_call(ep, PSZ_WIDE, junk, NULL, SND_FILENAME | SND_ASYNC | SND_NODEFAULT), "%s junk async", ps_ep_name[ep]);
        ps_quiesce();
    }
    ps_stop();
}

/* soundLoadFile opens with FILE_SHARE_READ|WRITE: a file held without sharing cannot be played (sync FALSE). */
PTEST(rooted_locked_file) {
    REQUIRE(U(PlaySoundW)); REQUIRE(U(PlaySoundA)); ps_quiesce();
    if (!make_files()) { t_skip("scratch files"); return; }
    WCHAR p[MAX_PATH]; ps_pathW(p, MAX_PATH, L"i7ps_locked.wav");
    DWORD n = ps_make_silent(g_buf, sizeof g_buf);
    if (!ps_write_fileW(p, g_buf, n)) { t_skip("write"); return; }
    HANDLE h = CreateFileW(p, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) { t_skip("lock"); return; }
    CHECK(!U(PlaySoundW)(p, NULL, SND_FILENAME | SND_NODEFAULT));
    CHECK(U(PlaySoundW)(p, NULL, SND_FILENAME | SND_ASYNC | SND_NODEFAULT));
    ps_quiesce();
    CloseHandle(h);
    /* shared readers are fine */
    h = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    CHECK(!!U(PlaySoundW)(p, NULL, SND_FILENAME | SND_NODEFAULT) == PS_SYNC_VALID_EXPECT());
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    ps_stop();
}

/* SND_SYSTEM hands a relative name straight to soundLoadFile, which opens it relative to the current directory. */
PTEST(relative_system_flag) {
    REQUIRE(ps_have_all()); ps_quiesce();
    if (!enter_tmp()) { t_skip("scratch files"); return; }
    for (int ep = 0; ep < EP_N; ep++) {
        CHECKF(ps_call(ep, PSZ_WIDE, L"i7ps_ok.wav", NULL, SND_SYSTEM | SND_ASYNC | SND_NODEFAULT), "%s async", ps_ep_name[ep]);
        CHECKF(ps_call(ep, PSZ_WIDE, L"i7ps_missing.wav", NULL, SND_SYSTEM | SND_ASYNC | SND_NODEFAULT), "%s missing async", ps_ep_name[ep]);
        CHECKF(!ps_call(ep, PSZ_WIDE, L"i7ps_missing.wav", NULL, SND_SYSTEM | SND_NODEFAULT), "%s missing sync", ps_ep_name[ep]);
        /* no ".WAV" search under SND_SYSTEM: the bare stem is not a file */
        CHECKF(!ps_call(ep, PSZ_WIDE, L"i7ps_ok", NULL, SND_SYSTEM | SND_NODEFAULT), "%s stem sync", ps_ep_name[ep]);
        CHECKF(!ps_call(ep, PSZ_WIDE, L"i7ps_junk.wav", NULL, SND_SYSTEM | SND_NODEFAULT), "%s junk sync", ps_ep_name[ep]);
        ps_quiesce();
    }
    CHECK(!!U(PlaySoundW)(L"i7ps_ok.wav", NULL, SND_SYSTEM | SND_NODEFAULT) == PS_SYNC_VALID_EXPECT());
    ps_stop(); leave_tmp();
}

/* PlaySoundA converts with the ANSI code page; a name outside it reaches the file system as '?' and is not found. */
PTEST(ansi_conversion_cp1252) {
    REQUIRE(U(PlaySoundW)); REQUIRE(U(PlaySoundA)); REQUIRE(U(sndPlaySoundA)); ps_quiesce();
    if (GetACP() != 1252) { t_skip("ACP %u", GetACP()); return; }
    if (!enter_tmp()) { t_skip("scratch files"); return; }
    DWORD n = ps_make_silent(g_buf, sizeof g_buf);
    WCHAR p[MAX_PATH]; ps_pathW(p, MAX_PATH, L"i7ps_\x00e9t\x00e9.wav");
    WCHAR q[MAX_PATH]; ps_pathW(q, MAX_PATH, L"i7ps_\x4e2d\x6587.wav");
    if (!ps_write_fileW(p, g_buf, n) || !ps_write_fileW(q, g_buf, n)) { t_skip("write"); leave_tmp(); return; }
    CHECK(U(PlaySoundW)(L"i7ps_\x00e9t\x00e9.wav", NULL, SND_FILENAME | SND_ASYNC | SND_NODEFAULT));
    CHECK(U(PlaySoundA)("i7ps_\xe9t\xe9.wav", NULL, SND_FILENAME | SND_ASYNC | SND_NODEFAULT));
    CHECK(U(sndPlaySoundA)("i7ps_\xe9t\xe9", SND_ASYNC | SND_NODEFAULT));
    CHECK(!U(PlaySoundA)("i7ps_\xe8t\xe8.wav", NULL, SND_FILENAME | SND_ASYNC | SND_NODEFAULT));
    CHECK(U(PlaySoundW)(L"i7ps_\x4e2d\x6587.wav", NULL, SND_FILENAME | SND_ASYNC | SND_NODEFAULT));
    CHECK(!U(PlaySoundA)("i7ps_??.wav", NULL, SND_FILENAME | SND_ASYNC | SND_NODEFAULT));
    ps_quiesce();
    CHECK(!!U(PlaySoundA)("i7ps_\xe9t\xe9.wav", NULL, SND_FILENAME | SND_NODEFAULT) == PS_SYNC_VALID_EXPECT());
    ps_stop(); leave_tmp();
}

/* Upper-case and lower-case spellings of the same rooted file behave identically. */
PTEST(rooted_case_insensitive) {
    REQUIRE(U(PlaySoundW)); ps_quiesce();
    if (!make_files()) { t_skip("scratch files"); return; }
    WCHAR p[MAX_PATH]; ps_pathW(p, MAX_PATH, L"I7PS_OK.WAV");
    CHECK(!!U(PlaySoundW)(p, NULL, SND_FILENAME | SND_NODEFAULT) == PS_SYNC_VALID_EXPECT());
    CharLowerW(p);
    CHECK(!!U(PlaySoundW)(p, NULL, SND_FILENAME | SND_NODEFAULT) == PS_SYNC_VALID_EXPECT());
    ps_stop();
}

/* Repeating the same file uses the cached sound when unchanged; the result does not change between repeats. */
PTEST(rooted_same_file_repeated) {
    REQUIRE(U(PlaySoundW)); ps_quiesce();
    if (!make_files()) { t_skip("scratch files"); return; }
    WCHAR p[MAX_PATH]; ps_pathW(p, MAX_PATH, L"i7ps_ok.wav");
    for (int i = 0; i < 4; i++) CHECKF(!!U(PlaySoundW)(p, NULL, SND_FILENAME | SND_NODEFAULT) == PS_SYNC_VALID_EXPECT(), "round %d", i);
    /* rewrite the file as junk: the next play reloads it and fails */
    WCHAR j[MAX_PATH]; ps_pathW(j, MAX_PATH, L"i7ps_rewrite.wav");
    DWORD n = ps_make_silent(g_buf, sizeof g_buf); ps_write_fileW(j, g_buf, n);
    CHECK(!!U(PlaySoundW)(j, NULL, SND_FILENAME | SND_NODEFAULT) == PS_SYNC_VALID_EXPECT());
    ps_stop();
    memset(g_buf, 0x33, 64); ps_write_fileW(j, g_buf, 64);
    CHECK(!U(PlaySoundW)(j, NULL, SND_FILENAME | SND_NODEFAULT));
    ps_stop();
}
