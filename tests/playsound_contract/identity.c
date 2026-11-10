#include "ps.h"
/* Export identity, A/W parity across the six entry points, threads, and consumer-shaped calls.
 *
 * Export table (winmm 10.0.19041): ordinal 2 (no name) and PlaySound (10) share RVA 0x98B0, a 5-byte jmp to
 * PlaySoundA (11, 0x13200); PlaySoundW 12 (0x1950); sndPlaySoundA 135 = PlaySoundA(psz, NULL, f); sndPlaySoundW 136
 * = sndPlaySoundI(psz, NULL, f). All five live in winmm itself, not in winmmbase.
 * VM_TIMEOUT: 120 */

static BYTE g_img[8192];

PTEST(export_ordinal2_is_PlaySound) {
    REQUIRE(U(PlaySound));
    FARPROC o2 = t_proc_ord("winmm.dll", 2);
    CHECK(o2 != NULL);
    EXPECT_EQ(o2, t_proc_in("winmm.dll", "PlaySound"));
}

PTEST(export_ordinals) {
    REQUIRE(U(PlaySoundA)); REQUIRE(U(PlaySoundW)); REQUIRE(U(sndPlaySoundA)); REQUIRE(U(sndPlaySoundW));
    EXPECT_EQ(t_proc_ord("winmm.dll", 10), t_proc_in("winmm.dll", "PlaySound"));
    EXPECT_EQ(t_proc_ord("winmm.dll", 11), (FARPROC)U(PlaySoundA));
    EXPECT_EQ(t_proc_ord("winmm.dll", 12), (FARPROC)U(PlaySoundW));
    EXPECT_EQ(t_proc_ord("winmm.dll", 135), (FARPROC)U(sndPlaySoundA));
    EXPECT_EQ(t_proc_ord("winmm.dll", 136), (FARPROC)U(sndPlaySoundW));
    CHECK(t_proc_ord("winmm.dll", 1) == NULL);
}

/* PlaySound is an alias of PlaySoundA: either the same address, or a direct jmp to it. */
PTEST(export_PlaySound_aliases_PlaySoundA) {
    REQUIRE(U(PlaySound)); REQUIRE(U(PlaySoundA));
    BYTE *p = (BYTE *)U(PlaySound), *a = (BYTE *)U(PlaySoundA);
    BYTE *target = NULL;
    if (p == a) target = a;
    else if (p[0] == 0xE9) target = p + 5 + *(INT32 *)(p + 1);
    else if (p[0] == 0xEB) target = p + 2 + (INT8)p[1];
    CHECKF(target == a, "PlaySound %p first byte %02x, PlaySoundA %p", p, p[0], a);
    HMODULE w = t_module("winmm.dll");
    t_note("PlaySound rva 0x%llx, PlaySoundA rva 0x%llx, PlaySoundW rva 0x%llx", (unsigned long long)(p - (BYTE *)w),
           (unsigned long long)(a - (BYTE *)w), (unsigned long long)((BYTE *)U(PlaySoundW) - (BYTE *)w));
}

PTEST(export_distinct_entry_points) {
    REQUIRE(ps_have_all());
    FARPROC f[5] = { (FARPROC)U(PlaySoundA), (FARPROC)U(PlaySoundW), (FARPROC)U(sndPlaySoundA), (FARPROC)U(sndPlaySoundW),
                     t_proc_in("winmm.dll", "PlaySound") };
    for (int i = 0; i < 4; i++) for (int j = i + 1; j < 4; j++) CHECKF(f[i] != f[j], "%d == %d", i, j);
    HMODULE w = t_module("winmm.dll");
    for (int i = 0; i < 5; i++) {
        HMODULE owner = NULL;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)f[i], &owner);
        CHECKF(owner == w, "entry %d not inside winmm", i);
    }
}

/* Implemented in winmm, not exported by winmmbase. */
PTEST(export_not_in_winmmbase) {
    HMODULE b = t_module("winmmbase.dll");
    if (!b) { t_skip("winmmbase not loadable"); return; }
    static const char *names[] = { "PlaySound", "PlaySoundA", "PlaySoundW", "sndPlaySoundA", "sndPlaySoundW", "sndPlaySound" };
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) CHECKF(GetProcAddress(b, names[i]) == NULL, "%s", names[i]);
    CHECK(GetProcAddress(t_module("winmm.dll"), "sndPlaySound") == NULL);
    CHECK(GetProcAddress(t_module("winmm.dll"), "playsound") == NULL);
    CHECK(GetProcAddress(t_module("winmm.dll"), "PlaySoundEx") == NULL);
}

/* ---- parity: the same inputs give the same BOOL through every entry point ------------------------------------ */

typedef struct { int kind; const void *psz; DWORD f; const char *what; } pcase;

PTEST(parity_table) {
    REQUIRE(ps_have_all()); ps_quiesce();
    static WCHAR missrel[64] = L"i7ps_parity_missing", missroot[64] = L"C:\\i7ps_parity\\x.wav", empty[8] = L"";
    static BYTE junk[64] = "JUNKJUNKJUNK";
    static BYTE notwave[64] = "RIFF\x20\0\0\0XXXXfmt ";
    ps_make_silent(g_img, sizeof g_img);
    const pcase cases[] = {
        { PSZ_NULL, NULL, 0, "stop" }, { PSZ_NULL, NULL, SND_ASYNC, "stop async" }, { PSZ_NULL, NULL, 0x20, "stop bad flag" },
        { PSZ_NULL, NULL, SND_RESOURCE, "NULL resource" }, { PSZ_NULL, NULL, 0x800000, "bad 0x800000" },
        { PSZ_WIDE, empty, SND_NODEFAULT, "empty" }, { PSZ_WIDE, empty, SND_ASYNC | SND_NODEFAULT, "empty async" },
        { PSZ_WIDE, empty, 0x100 | SND_NODEFAULT, "empty bad 0x100" },
        { PSZ_WIDE, missrel, SND_NODEFAULT, "missing" }, { PSZ_WIDE, missrel, SND_ASYNC | SND_NODEFAULT, "missing async" },
        { PSZ_WIDE, missrel, SND_FILENAME | SND_NODEFAULT, "missing file" }, { PSZ_WIDE, missrel, SND_ALIAS | SND_NODEFAULT, "missing alias" },
        { PSZ_WIDE, missrel, SND_SYSTEM | SND_ASYNC | SND_NODEFAULT, "missing system async" },
        { PSZ_WIDE, missroot, SND_FILENAME | SND_ASYNC | SND_NODEFAULT, "rooted async" },
        { PSZ_WIDE, missroot, SND_FILENAME | SND_NODEFAULT, "rooted sync" },
        { PSZ_WIDE, missrel, SND_RESOURCE | SND_NODEFAULT, "missing resource" },
        { PSZ_RAW, (void *)(ULONG_PTR)0x7A7A, SND_ALIAS_ID | SND_NODEFAULT, "alias id 0x7A7A" },
        { PSZ_RAW, (void *)(ULONG_PTR)0x7A7A, SND_ALIAS_ID | SND_ASYNC | SND_NODEFAULT, "alias id async" },
        { PSZ_RAW, junk, SND_MEMORY | SND_NODEFAULT, "junk sync" }, { PSZ_RAW, junk, SND_MEMORY | SND_ASYNC | SND_NODEFAULT, "junk async" },
        { PSZ_RAW, notwave, SND_MEMORY | SND_ASYNC | SND_NODEFAULT, "RIFF no WAVE async" },
        { PSZ_RAW, g_img, SND_MEMORY | SND_ASYNC | SND_NODEFAULT, "image async" },
        { PSZ_RAW, g_img, SND_MEMORY | SND_NODEFAULT, "image sync" },
        { PSZ_RAW, g_img, SND_MEMORY | 0x1000 | SND_NODEFAULT, "image bad 0x1000" },
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        BOOL r[EP_N];
        for (int ep = 0; ep < EP_N; ep++) { r[ep] = ps_call(ep, cases[i].kind, cases[i].psz, NULL, cases[i].f); ps_stop(); }
        for (int ep = 1; ep < EP_N; ep++)
            CHECKF(!!r[ep] == !!r[0], "%s: %s %d vs PlaySoundA %d", cases[i].what, ps_ep_name[ep], r[ep], r[0]);
    }
    ps_quiesce();
}

/* sndPlaySoundA is PlaySoundA(psz, NULL, f): ANSI names convert the same way. */
PTEST(parity_snd_ansi_long_name) {
    REQUIRE(U(sndPlaySoundA)); REQUIRE(U(PlaySoundA)); ps_quiesce();
    static char name[600];
    for (int len = 240; len <= 260; len++) {
        memset(name, 0, sizeof name); memcpy(name, "C:\\i7ps_nodir\\", 14);
        for (int k = 14; k < len - 4; k++) name[k] = 'b';
        memcpy(name + len - 4, ".wav", 4);
        BOOL a = U(PlaySoundA)(name, NULL, SND_ASYNC | SND_NODEFAULT);
        BOOL s = U(sndPlaySoundA)(name, SND_ASYNC | SND_NODEFAULT);
        CHECKF(!!a == !!s, "len %d: PlaySoundA %d sndPlaySoundA %d", len, a, s);
        CHECKF(!!a == (len <= 251), "len %d: %d", len, a);
    }
    ps_stop();
}

/* ---- threads ------------------------------------------------------------------------------------------------- */

typedef struct { int iters; int bad; } wctx;
static DWORD WINAPI worker(void *p) {
    wctx *c = (wctx *)p;
    static BYTE img[4096];
    for (int i = 0; i < c->iters; i++) {
        if (!U(PlaySoundW)(NULL, NULL, 0)) c->bad++;
        if (U(PlaySoundW)(L"i7ps_thread_missing", NULL, SND_ASYNC | SND_NODEFAULT)) c->bad++;
        if (!U(PlaySoundW)(L"", NULL, SND_NODEFAULT)) c->bad++;
        if (U(PlaySoundW)(NULL, NULL, 0x8000)) c->bad++;
        if (!U(PlaySoundA)("C:\\i7ps_thread\\x.wav", NULL, SND_FILENAME | SND_ASYNC | SND_NODEFAULT)) c->bad++;
    }
    return 0;
}

PTEST(thread_calls_from_worker) {
    REQUIRE(U(PlaySoundW)); REQUIRE(U(PlaySoundA)); ps_quiesce();
    wctx c = { 50, 0 };
    HANDLE t = CreateThread(NULL, 0, worker, &c, 0, NULL);
    CHECK(t != NULL);
    if (!t) return;
    CHECK(WaitForSingleObject(t, 20000) == WAIT_OBJECT_0);
    CloseHandle(t);
    EXPECT_EQ(c.bad, 0);
    ps_quiesce();
}

PTEST(thread_concurrent_callers) {
    REQUIRE(U(PlaySoundW)); REQUIRE(U(PlaySoundA)); ps_quiesce();
    enum { N = 4 };
    wctx c[N]; HANDLE t[N];
    for (int i = 0; i < N; i++) { c[i].iters = 40; c[i].bad = 0; t[i] = CreateThread(NULL, 0, worker, &c[i], 0, NULL); }
    DWORD w = WaitForMultipleObjects(N, t, TRUE, 30000);
    CHECKF(w < WAIT_OBJECT_0 + N, "wait 0x%lx", w);
    for (int i = 0; i < N; i++) { if (t[i]) CloseHandle(t[i]); EXPECT_EQ(c[i].bad, 0); }
    ps_quiesce();
}

/* An async play started by a thread that has since exited is still stopped by the main thread. */
static DWORD WINAPI async_starter(void *p) {
    return U(PlaySoundW)((LPCWSTR)p, NULL, SND_ASYNC | SND_LOOP | SND_MEMORY | SND_NODEFAULT);
}
PTEST(thread_async_outlives_caller) {
    REQUIRE(U(PlaySoundW)); ps_quiesce();
    ps_make_silent(g_img, sizeof g_img);
    HANDLE t = CreateThread(NULL, 0, async_starter, g_img, 0, NULL);
    if (!t) { t_skip("CreateThread"); return; }
    WaitForSingleObject(t, 10000);
    DWORD code = 0; GetExitCodeThread(t, &code); CloseHandle(t);
    EXPECT_EQ(code, 1);
    if (ps_ndev() > 0) CHECK(ps_wait_playing(3000)); else { Sleep(300); CHECK(!ps_is_playing()); }
    CHECK(U(PlaySoundW)(NULL, NULL, 0));
    CHECK(!ps_is_playing());
    ps_quiesce();
}

/* ---- consumer-shaped calls ---------------------------------------------------------------------------------- */

/* shell SHPlaySound: alias name, ASYNC|NODEFAULT|NOSTOP|NOWAIT|ALIAS|SENTRY|SYSTEM (+0x400000). SND_SYSTEM skips the
 * file search, so an unknown event name is posted (TRUE) and dies quietly on the play thread under NODEFAULT. */
PTEST(consumer_shell_event_shape) {
    REQUIRE(U(PlaySoundW)); ps_quiesce();
    CHECK(U(PlaySoundW)(L"i7ps_unknown_shell_event", NULL, 0x292013));
    CHECK(U(PlaySoundW)(L"i7ps_unknown_shell_event", NULL, 0x692013));
    ps_quiesce();
    /* the same call without SND_SYSTEM searches files first and fails */
    CHECK(!U(PlaySoundW)(L"i7ps_unknown_shell_event", NULL, 0x292013 & ~SND_SYSTEM));
    ps_stop();
}

/* comctl32 CCPlaySound: a registry-resolved path with FILENAME|SYSTEM|ASYNC|NODEFAULT. */
PTEST(consumer_comctl_path_shape) {
    REQUIRE(U(PlaySoundW)); ps_quiesce();
    CHECK(U(PlaySoundW)(L"C:\\i7ps_nodir\\menu.wav", NULL, 0x220003));
    CHECK(U(PlaySoundW)(L"i7ps_relative_menu.wav", NULL, 0x220003));
    CHECK(U(PlaySoundW)(L"", NULL, 0x220003));
    ps_quiesce();
}

/* An alarm loop: start a looping async alarm from a file, stop it, start again, stop. */
PTEST(consumer_alarm_loop_file) {
    REQUIRE(U(PlaySoundW)); ps_quiesce();
    WCHAR p[MAX_PATH]; ps_pathW(p, MAX_PATH, L"i7ps_alarm.wav");
    DWORD n = ps_make_silent(g_img, sizeof g_img);
    if (!ps_write_fileW(p, g_img, n)) { t_skip("write"); return; }
    for (int i = 0; i < 3; i++) {
        CHECK(U(PlaySoundW)(p, NULL, SND_FILENAME | SND_ASYNC | SND_LOOP | SND_NODEFAULT));
        Sleep(200);
        CHECK(U(PlaySoundW)(NULL, NULL, 0));
        CHECK(!ps_is_playing());
    }
    /* acknowledge-by-replacement: a second alarm replaces the first, one stop ends both */
    CHECK(U(PlaySoundW)(p, NULL, SND_FILENAME | SND_ASYNC | SND_LOOP | SND_NODEFAULT));
    CHECK(U(PlaySoundW)(p, NULL, SND_FILENAME | SND_ASYNC | SND_LOOP | SND_NODEFAULT));
    Sleep(200);
    CHECK(U(PlaySoundW)(NULL, NULL, 0));
    CHECK(!ps_is_playing());
    ps_quiesce();
}

/* A UI that plays "no sound if busy": NOSTOP with nothing playing behaves like a normal call. */
PTEST(consumer_nostop_idle) {
    REQUIRE(U(PlaySoundW)); REQUIRE(U(sndPlaySoundW)); ps_quiesce();
    ps_make_silent(g_img, sizeof g_img);
    CHECK(U(PlaySoundW)((LPCWSTR)g_img, NULL, SND_MEMORY | SND_ASYNC | SND_NOSTOP | SND_NODEFAULT));
    ps_quiesce();
    CHECK(!!U(sndPlaySoundW)((LPCWSTR)g_img, SND_MEMORY | SND_NOSTOP | SND_NODEFAULT) == PS_SYNC_VALID_EXPECT());
    CHECK(!U(PlaySoundW)(L"i7ps_busy_missing", NULL, SND_ASYNC | SND_NOSTOP | SND_NODEFAULT));
    ps_stop();
}

/* The webcam shape: an explicit stop, then an integer resource id from a module without it. */
PTEST(consumer_stop_then_resource_id) {
    REQUIRE(U(PlaySoundW)); ps_quiesce();
    CHECK(U(PlaySoundW)(NULL, NULL, 0));
    CHECK(!U(PlaySoundW)(MAKEINTRESOURCEW(0x7A11), GetModuleHandleW(NULL), SND_RESOURCE | SND_ASYNC | SND_NODEFAULT));
    ps_stop();
}
