#include "ps.h"
/* SND_RESOURCE (0x40004: the MEMORY bit plus 0x40000).
 *
 * sndPlaySoundI: FindResourceW(hmod, psz, L"SOUND"), then L"WAVE"; neither -> FALSE; LoadResource failure -> FALSE;
 * then the resource bytes are a memory image (RESOURCE bit dropped): ASYNC checks RIFF/WAVE and copies, sync parses
 * them in place. hmod NULL searches the process image, so sndPlaySound (which always passes NULL) finds resources of
 * the calling exe. PlaySoundA converts a string name (psz > 0xFFFF) but passes MAKEINTRESOURCE ids unchanged.
 *
 * The resources live in a copy of this exe, written with UpdateResource into the scratch directory and loaded as a
 * data file (and, for hmod NULL, run as a child process). Every image is silent.
 * VM_TIMEOUT: 120 */

#define ID_SILENT 0x7A01
static BYTE g_buf[65536];

static WCHAR g_copy[MAX_PATH];
static int g_res_state;   /* 0 untried, 1 ready, -1 failed */

static BOOL add(HANDLE u, LPCWSTR type, LPCWSTR name, const void *p, DWORD n) {
    return UpdateResourceW(u, type, name, MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), (LPVOID)p, n);
}

static int build_copy(void) {
    if (g_res_state) return g_res_state > 0;
    g_res_state = -1;
    WCHAR self[MAX_PATH]; MultiByteToWideChar(CP_ACP, 0, t_self_path(), -1, self, MAX_PATH);
    ps_pathW(g_copy, MAX_PATH, L"i7psres.exe");
    if (!CopyFileW(self, g_copy, FALSE)) { t_note("CopyFile %lu", GetLastError()); return 0; }
    SetFileAttributesW(g_copy, FILE_ATTRIBUTE_NORMAL);   /* the payload is a read-only CD: the copy inherits READONLY */
    HANDLE u = BeginUpdateResourceW(g_copy, FALSE);
    if (!u) { t_note("BeginUpdateResource %lu", GetLastError()); return 0; }
    static BYTE silent[4096], badfmt[4096], junk[256], st[8192];
    DWORD ns = ps_make_silent(silent, sizeof silent);
    DWORD nb = ps_make_silent(badfmt, sizeof badfmt); badfmt[22] = badfmt[23] = 0;   /* channels 0 */
    memset(junk, 0x5A, sizeof junk); memcpy(junk, "JUNKJUNKJUNK", 12);
    DWORD nst = ps_make_pcm(st, sizeof st, 2, 11025, 8, 100);
    BOOL ok = add(u, L"WAVE", L"I7SILENT", silent, ns) && add(u, L"SOUND", L"I7SND", silent, ns) &&
              add(u, L"WAVE", MAKEINTRESOURCEW(ID_SILENT), silent, ns) && add(u, L"SOUND", MAKEINTRESOURCEW(0x7A02), silent, ns) &&
              add(u, L"WAVE", L"I7JUNK", junk, sizeof junk) && add(u, L"WAVE", L"I7BADFMT", badfmt, nb) &&
              add(u, (LPCWSTR)RT_RCDATA, L"I7RCDATA", silent, ns) && add(u, L"WAVEFILE", L"I7OTHERTYPE", silent, ns) &&
              add(u, L"SOUND", L"I7BOTH", junk, sizeof junk) && add(u, L"WAVE", L"I7BOTH", silent, ns) &&
              add(u, L"WAVE", L"I7STEREO8", st, nst) && add(u, L"wave", L"I7LOWERTYPE", silent, ns);
    if (!EndUpdateResourceW(u, !ok) || !ok) { t_note("UpdateResource failed %lu", GetLastError()); return 0; }
    g_res_state = 1;
    return 1;
}

static HMODULE load_copy(void) {
    if (!build_copy()) return NULL;
    return LoadLibraryExW(g_copy, NULL, LOAD_LIBRARY_AS_DATAFILE);
}

typedef struct { LPCWSTR name; LPCSTR aname; int async_ok; int sync; } rrow;   /* sync: 0 FALSE, 1 device-defined */
static const rrow g_rows[] = {
    { L"I7SILENT", "I7SILENT", 1, 1 },
    { L"i7silent", "i7silent", 1, 1 },                       /* resource names are case-insensitive */
    { L"I7SND", "I7SND", 1, 1 },                             /* "SOUND" type */
    { L"#31233", "#31233", 1, 1 },                           /* "#n" names an integer id (0x7A01) */
    { L"#31234", "#31234", 1, 1 },                           /* integer id under "SOUND" */
    { L"I7BOTH", "I7BOTH", 0, 0 },                           /* "SOUND" is searched first: the junk one wins */
    { L"I7JUNK", "I7JUNK", 0, 0 },                           /* not RIFF/WAVE */
    { L"I7BADFMT", "I7BADFMT", 1, 0 },                       /* header fine, parse fails later */
    { L"I7STEREO8", "I7STEREO8", 1, 1 },
    { L"I7LOWERTYPE", "I7LOWERTYPE", 1, 1 },                 /* type names are case-insensitive too */
    { L"I7RCDATA", "I7RCDATA", 0, 0 },                       /* wrong type */
    { L"I7OTHERTYPE", "I7OTHERTYPE", 0, 0 },
    { L"I7MISSING", "I7MISSING", 0, 0 },
    { L"#31235", "#31235", 0, 0 },
};

PTEST(resource_async_table) {
    REQUIRE(U(PlaySoundW)); REQUIRE(U(PlaySoundA)); REQUIRE(U(PlaySound)); ps_quiesce();
    HMODULE h = load_copy();
    if (!h) { t_skip("resource copy unavailable"); return; }
    ps_sweep_begin();
    for (size_t i = 0; i < sizeof g_rows / sizeof g_rows[0]; i++) {
        BOOL w = U(PlaySoundW)(g_rows[i].name, h, SND_RESOURCE | SND_ASYNC | SND_NODEFAULT); ps_stop();
        BOOL a = U(PlaySoundA)(g_rows[i].aname, h, SND_RESOURCE | SND_ASYNC | SND_NODEFAULT); ps_stop();
        BOOL p = U(PlaySound)(g_rows[i].aname, h, SND_RESOURCE | SND_ASYNC | SND_NODEFAULT); ps_stop();
        PS_EXPECT(!!w == g_rows[i].async_ok && !!a == g_rows[i].async_ok && !!p == g_rows[i].async_ok,
                  "%ls: W %d A %d PlaySound %d want %d", g_rows[i].name, w, a, p, g_rows[i].async_ok);
    }
    PS_SWEEP_END(); ps_quiesce();
    FreeLibrary(h);
}

PTEST(resource_sync_table) {
    REQUIRE(U(PlaySoundW)); REQUIRE(U(PlaySoundA)); ps_quiesce();
    HMODULE h = load_copy();
    if (!h) { t_skip("resource copy unavailable"); return; }
    ps_sweep_begin();
    for (size_t i = 0; i < sizeof g_rows / sizeof g_rows[0]; i++) {
        BOOL want = g_rows[i].sync ? PS_SYNC_VALID_EXPECT() : FALSE;
        BOOL w = U(PlaySoundW)(g_rows[i].name, h, SND_RESOURCE | SND_NODEFAULT);
        BOOL a = U(PlaySoundA)(g_rows[i].aname, h, SND_RESOURCE | SND_NODEFAULT);
        PS_EXPECT(!!w == !!want && !!a == !!want, "%ls: W %d A %d want %d", g_rows[i].name, w, a, want);
    }
    PS_SWEEP_END(); ps_stop();
    FreeLibrary(h);
}

/* MAKEINTRESOURCE ids reach FindResourceW unchanged through every form. */
PTEST(resource_integer_ids) {
    REQUIRE(ps_have_all()); ps_quiesce();
    HMODULE h = load_copy();
    if (!h) { t_skip("resource copy unavailable"); return; }
    for (int ep = 0; ep <= EP_ORD2; ep++) {
        CHECKF(ps_call(ep, PSZ_RAW, MAKEINTRESOURCEW(ID_SILENT), h, SND_RESOURCE | SND_ASYNC | SND_NODEFAULT), "%s WAVE id", ps_ep_name[ep]);
        ps_stop();
        CHECKF(ps_call(ep, PSZ_RAW, MAKEINTRESOURCEW(0x7A02), h, SND_RESOURCE | SND_ASYNC | SND_NODEFAULT), "%s SOUND id", ps_ep_name[ep]);
        ps_stop();
        CHECKF(!ps_call(ep, PSZ_RAW, MAKEINTRESOURCEW(0x7A03), h, SND_RESOURCE | SND_ASYNC | SND_NODEFAULT), "%s missing id", ps_ep_name[ep]);
        CHECKF(!ps_call(ep, PSZ_RAW, MAKEINTRESOURCEW(1), h, SND_RESOURCE | SND_NODEFAULT), "%s id 1", ps_ep_name[ep]);
        CHECKF(!!ps_call(ep, PSZ_RAW, MAKEINTRESOURCEW(ID_SILENT), h, SND_RESOURCE | SND_NODEFAULT) == PS_SYNC_VALID_EXPECT(), "%s sync id", ps_ep_name[ep]);
    }
    ps_quiesce();
    FreeLibrary(h);
}

/* The logon-sound shape (integer id, SND_RESOURCE|SND_SYSTEM, sync) against this area's own silent resource. */
PTEST(resource_system_flag_shape) {
    REQUIRE(U(PlaySoundW)); ps_quiesce();
    HMODULE h = load_copy();
    if (!h) { t_skip("resource copy unavailable"); return; }
    CHECK(!!U(PlaySoundW)(MAKEINTRESOURCEW(ID_SILENT), h, SND_RESOURCE | SND_SYSTEM | SND_NODEFAULT) == PS_SYNC_VALID_EXPECT());
    CHECK(U(PlaySoundW)(MAKEINTRESOURCEW(ID_SILENT), h, SND_RESOURCE | SND_SYSTEM | SND_ASYNC | SND_NODEFAULT));
    ps_quiesce();
    CHECK(!U(PlaySoundW)(MAKEINTRESOURCEW(0x7A03), h, SND_RESOURCE | SND_SYSTEM | SND_NODEFAULT));
    ps_stop();
    FreeLibrary(h);
}

/* A resource is looked up only with the RESOURCE combination: the same name as a file or alias is not found, and
 * the MEMORY bit alone treats the name pointer as an image. */
PTEST(resource_name_without_resource_flag) {
    REQUIRE(U(PlaySoundW)); ps_quiesce();
    HMODULE h = load_copy();
    if (!h) { t_skip("resource copy unavailable"); return; }
    /* Win10 22H2 oracle, no audio device: an ASYNC request with a name that cannot resolve is ACCEPTED (TRUE: the name is looked up
     * on the worker thread), so these are records, not expectations; only the synchronous calls decide at the call. */
    t_note("async bare name: %d", !!U(PlaySoundW)(L"I7SILENT", h, SND_ASYNC | SND_NODEFAULT));
    t_note("async FILENAME: %d", !!U(PlaySoundW)(L"I7SILENT", h, SND_FILENAME | SND_ASYNC | SND_NODEFAULT));
    t_note("async ALIAS: %d", !!U(PlaySoundW)(L"I7SILENT", h, SND_ALIAS | SND_ASYNC | SND_NODEFAULT));
    t_note("async 0x40000: %d", !!U(PlaySoundW)(L"I7SILENT", h, 0x40000 | SND_ASYNC | SND_NODEFAULT));
    static WCHAR padded[64] = L"I7SILENT";
    t_note("async MEMORY: %d", !!U(PlaySoundW)(padded, h, SND_MEMORY | SND_ASYNC | SND_NODEFAULT));
    CHECK(!U(PlaySoundW)(padded, h, SND_MEMORY | SND_NODEFAULT));
    ps_stop();
    FreeLibrary(h);
}

/* Other modules: none of this area's names exist in winmm, kernel32 or the test exe itself (hmod NULL). */
PTEST(resource_missing_in_other_modules) {
    REQUIRE(ps_have_all()); ps_quiesce();
    HMODULE mods[] = { NULL, GetModuleHandleW(NULL), t_module("winmm.dll"), t_module("kernel32.dll"), t_module("ntdll.dll") };
    for (size_t m = 0; m < sizeof mods / sizeof mods[0]; m++) {
        CHECKF(!U(PlaySoundW)(L"I7SILENT", mods[m], SND_RESOURCE | SND_NODEFAULT), "mod %zu", m);
        CHECKF(!U(PlaySoundW)(L"I7SILENT", mods[m], SND_RESOURCE | SND_ASYNC | SND_NODEFAULT), "mod %zu async", m);
        CHECKF(!U(PlaySoundA)("I7SILENT", mods[m], SND_RESOURCE | SND_ASYNC | SND_NODEFAULT), "mod %zu A", m);
        CHECKF(!U(PlaySoundW)(MAKEINTRESOURCEW(ID_SILENT), mods[m], SND_RESOURCE | SND_ASYNC | SND_NODEFAULT), "mod %zu id", m);
    }
    /* sndPlaySound passes hmod NULL: the test exe has no such resource */
    for (int ep = EP_SNDA; ep <= EP_SNDW; ep++) {
        CHECKF(!ps_call(ep, PSZ_WIDE, L"I7SILENT", NULL, SND_RESOURCE | SND_ASYNC | SND_NODEFAULT), "%s", ps_ep_name[ep]);
        CHECKF(!ps_call(ep, PSZ_RAW, MAKEINTRESOURCEW(ID_SILENT), NULL, SND_RESOURCE | SND_NODEFAULT), "%s id", ps_ep_name[ep]);
    }
    ps_stop();
}

/* hmod NULL in a process whose exe carries the resources: every form, sndPlaySound included, finds them. */
#define CR_PSW   0x001
#define CR_PSA   0x002
#define CR_SNDW  0x004
#define CR_SNDA  0x008
#define CR_ID    0x010
#define CR_MISS  0x020
#define CR_JUNK  0x040
#define CR_SYNC  0x080
#define CR_LOWER 0x100
#define CR_DONE  0x40000000
CHILD(psres) {
    if (!U(PlaySoundW) || !U(PlaySoundA) || !U(sndPlaySoundW) || !U(sndPlaySoundA)) return 0x7F;
    int r = CR_DONE;
    const DWORD f = SND_RESOURCE | SND_ASYNC | SND_NODEFAULT;
    if (U(PlaySoundW)(L"I7SILENT", NULL, f)) r |= CR_PSW;
    U(PlaySoundW)(NULL, NULL, 0);
    if (U(PlaySoundA)("I7SILENT", NULL, f)) r |= CR_PSA;
    U(PlaySoundW)(NULL, NULL, 0);
    if (U(sndPlaySoundW)(L"I7SND", f)) r |= CR_SNDW;
    U(PlaySoundW)(NULL, NULL, 0);
    if (U(sndPlaySoundA)("I7SILENT", f)) r |= CR_SNDA;
    U(PlaySoundW)(NULL, NULL, 0);
    if (U(sndPlaySoundW)((LPCWSTR)MAKEINTRESOURCEW(ID_SILENT), f)) r |= CR_ID;
    U(PlaySoundW)(NULL, NULL, 0);
    if (U(PlaySoundW)(L"I7MISSING", NULL, f)) r |= CR_MISS;
    if (U(sndPlaySoundW)(L"I7JUNK", f)) r |= CR_JUNK;
    if (U(sndPlaySoundW)(L"I7SILENT", SND_RESOURCE | SND_NODEFAULT)) r |= CR_SYNC;
    if (U(PlaySoundW)(L"i7silent", GetModuleHandleW(NULL), f)) r |= CR_LOWER;
    U(PlaySoundW)(NULL, NULL, 0);
    Sleep(150);
    U(PlaySoundW)(NULL, NULL, 0);
    return r;
}

PTEST(resource_hmod_null_in_child) {
    REQUIRE(U(PlaySoundW)); ps_quiesce();
    if (!build_copy()) { t_skip("resource copy unavailable"); return; }
    WCHAR cmd[MAX_PATH + 64]; _snwprintf(cmd, MAX_PATH + 64, L"\"%ls\" --child psres", g_copy); cmd[MAX_PATH + 63] = 0;
    STARTUPINFOW si; PROCESS_INFORMATION pi; memset(&si, 0, sizeof si); si.cb = sizeof si;
    if (!CreateProcessW(g_copy, cmd, NULL, NULL, FALSE, 0, NULL, ps_tmpdirW(), &si, &pi)) { t_skip("CreateProcess %lu", GetLastError()); return; }
    DWORD code = 0xDEAD0000;
    if (WaitForSingleObject(pi.hProcess, 30000) != WAIT_OBJECT_0) { TerminateProcess(pi.hProcess, 1); WaitForSingleObject(pi.hProcess, 5000); code = 0xDEAD0001; }
    else GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    t_note("child result 0x%lx", code);
    CHECKF(code & CR_DONE, "child exit 0x%lx", code);
    if (!(code & CR_DONE)) return;
    CHECK(code & CR_PSW); CHECK(code & CR_PSA); CHECK(code & CR_SNDW); CHECK(code & CR_SNDA); CHECK(code & CR_ID);
    CHECK(!(code & CR_MISS)); CHECK(!(code & CR_JUNK)); CHECK(code & CR_LOWER);
    EXPECT_EQ(!!(code & CR_SYNC), PS_SYNC_VALID_EXPECT());
    ps_stop();
}
