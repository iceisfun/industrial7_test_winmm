/* time_basic: export identity. On Windows 10 x64 winmm's five basic time exports are
 * 7-byte "rex.W jmp qword ptr [rip+disp32]" (48 FF 25 disp32) thunks through winmm's own
 * import slot for api-ms-win-mm-time-l1-1-0, which the loader binds to kernel32's exports.
 * Ordinals: timeBeginPeriod 137, timeEndPeriod 138, timeGetDevCaps 139, timeGetSystemTime 140,
 * timeGetTime 141. winmmbase does not export them.
 */
#include "tb.h"

static const struct { const char *name; unsigned ord; } k_fns[] = {
    { "timeBeginPeriod", 137 }, { "timeEndPeriod", 138 }, { "timeGetDevCaps", 139 },
    { "timeGetSystemTime", 140 }, { "timeGetTime", 141 },
};
#define NFN (sizeof k_fns / sizeof k_fns[0])

static int image_range(HMODULE m, ULONG_PTR *lo, ULONG_PTR *hi) {
    if (!m) return 0;
    const IMAGE_DOS_HEADER *d = (const IMAGE_DOS_HEADER *)m;
    if (d->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)((const BYTE *)m + d->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
    *lo = (ULONG_PTR)m; *hi = (ULONG_PTR)m + nt->OptionalHeader.SizeOfImage;
    return 1;
}

/* Decode an indirect jmp thunk; returns the slot address or NULL. */
static void **thunk_slot(const BYTE *p, int *len) {
    if (p[0] == 0x48 && p[1] == 0xFF && p[2] == 0x25) { LONG d; memcpy(&d, p + 3, 4); *len = 7; return (void **)(p + 7 + d); }
    if (p[0] == 0xFF && p[1] == 0x25)                 { LONG d; memcpy(&d, p + 2, 4); *len = 6; return (void **)(p + 6 + d); }
    return NULL;
}

TEST(time_basic_identity_exports_present) {
    REQUIRE(U(timeGetTime));
    HMODULE w = t_module("winmm.dll");
    CHECK(w != NULL);
    for (size_t i = 0; i < NFN; i++) {
        CHECKF(GetProcAddress(w, k_fns[i].name) != NULL, "winmm!%s", k_fns[i].name);
        CHECKF(t_proc_in("kernel32.dll", k_fns[i].name) != NULL, "kernel32!%s", k_fns[i].name);
    }
}

TEST(time_basic_identity_ordinals) {
    REQUIRE(U(timeGetTime));
    for (size_t i = 0; i < NFN; i++) {
        FARPROC byname = t_proc_in("winmm.dll", k_fns[i].name);
        FARPROC byord = t_proc_ord("winmm.dll", k_fns[i].ord);
        CHECKF(byname && byname == byord, "%s ord %u: %p vs %p", k_fns[i].name, k_fns[i].ord, byname, byord);
    }
}

TEST(time_basic_identity_winmm_exports_in_winmm_image) {
    REQUIRE(U(timeGetTime));
    ULONG_PTR lo, hi;
    CHECK(image_range(t_module("winmm.dll"), &lo, &hi));
    for (size_t i = 0; i < NFN; i++) {
        ULONG_PTR a = (ULONG_PTR)t_proc_in("winmm.dll", k_fns[i].name);
        CHECKF(a >= lo && a < hi, "%s at %p not inside winmm", k_fns[i].name, (void *)a);
    }
}

TEST(time_basic_identity_winmm_not_forwarded_address) {
    REQUIRE(U(timeGetTime));
    /* winmm keeps its own entry point (a thunk), distinct from kernel32's */
    for (size_t i = 0; i < NFN; i++) {
        FARPROC w = t_proc_in("winmm.dll", k_fns[i].name), k = t_proc_in("kernel32.dll", k_fns[i].name);
        CHECKF(w && k && w != k, "%s", k_fns[i].name);
    }
}

TEST(time_basic_identity_thunk_shape) {
    REQUIRE(U(timeGetTime));
    for (size_t i = 0; i < NFN; i++) {
        const BYTE *p = (const BYTE *)t_proc_in("winmm.dll", k_fns[i].name);
        if (!p) { CHECKF(0, "%s", k_fns[i].name); continue; }
        CHECKF(p[0] == 0x48 && p[1] == 0xFF && p[2] == 0x25, "%s starts %02x %02x %02x", k_fns[i].name, p[0], p[1], p[2]);
    }
}

TEST(time_basic_identity_thunk_slot_in_winmm) {
    REQUIRE(U(timeGetTime));
    ULONG_PTR lo, hi;
    CHECK(image_range(t_module("winmm.dll"), &lo, &hi));
    for (size_t i = 0; i < NFN; i++) {
        int len = 0;
        const BYTE *p = (const BYTE *)t_proc_in("winmm.dll", k_fns[i].name);
        void **slot = p ? thunk_slot(p, &len) : NULL;
        CHECKF(slot != NULL, "%s not a jmp thunk", k_fns[i].name);
        if (!slot) continue;
        CHECKF((ULONG_PTR)slot >= lo && (ULONG_PTR)slot + sizeof(void *) <= hi, "%s slot %p outside winmm", k_fns[i].name, (void *)slot);
    }
}

TEST(time_basic_identity_thunk_target_is_kernel32) {
    REQUIRE(U(timeGetTime));
    for (size_t i = 0; i < NFN; i++) {
        int len = 0;
        const BYTE *p = (const BYTE *)t_proc_in("winmm.dll", k_fns[i].name);
        void **slot = p ? thunk_slot(p, &len) : NULL;
        if (!slot) { CHECKF(0, "%s", k_fns[i].name); continue; }
        FARPROC k = t_proc_in("kernel32.dll", k_fns[i].name);
        CHECKF(*slot == (void *)k, "%s: slot -> %p, kernel32 export %p", k_fns[i].name, *slot, (void *)k);
    }
}

TEST(time_basic_identity_timegettime_thunk) {
    REQUIRE(U(timeGetTime)); REQUIRE(UIN("kernel32.dll", timeGetTime));
    int len = 0;
    const BYTE *p = (const BYTE *)UIN("winmm.dll", timeGetTime);
    void **slot = thunk_slot(p, &len);
    CHECK(slot != NULL);
    EXPECT_EQ(len, 7);
    if (slot) EXPECT_EQ((ULONG_PTR)*slot, (ULONG_PTR)UIN("kernel32.dll", timeGetTime));
}

TEST(time_basic_identity_kernel32_target_in_kernel32_image) {
    REQUIRE(U(timeGetTime));
    ULONG_PTR lo, hi;
    CHECK(image_range(t_module("kernel32.dll"), &lo, &hi));
    for (size_t i = 0; i < NFN; i++) {
        ULONG_PTR a = (ULONG_PTR)t_proc_in("kernel32.dll", k_fns[i].name);
        CHECKF(a >= lo && a < hi, "kernel32!%s at %p is not a body inside kernel32", k_fns[i].name, (void *)a);
    }
}

TEST(time_basic_identity_apiset_resolves_to_kernel32) {
    REQUIRE(U(timeGetTime));
    HMODULE a = LoadLibraryA("api-ms-win-mm-time-l1-1-0.dll");
    if (!a) { t_note("LoadLibrary(api-ms-win-mm-time-l1-1-0) failed %lu", GetLastError()); return; }
    for (size_t i = 0; i < NFN; i++) {
        FARPROC v = GetProcAddress(a, k_fns[i].name);
        CHECKF(v == t_proc_in("kernel32.dll", k_fns[i].name), "%s via api set %p", k_fns[i].name, (void *)v);
    }
    t_note("api-ms-win-mm-time-l1-1-0 -> module %p (kernel32 %p)", (void *)a, (void *)t_module("kernel32.dll"));
    FreeLibrary(a);
}

TEST(time_basic_identity_not_in_winmmbase) {
    REQUIRE(U(timeGetTime));
    HMODULE b = t_module("winmmbase.dll");
    if (!b) { t_note("winmmbase.dll not loadable"); return; }
    for (size_t i = 0; i < NFN; i++)
        CHECKF(GetProcAddress(b, k_fns[i].name) == NULL, "winmmbase exports %s", k_fns[i].name);
}

TEST(time_basic_identity_u_resolves_to_winmm) {
    REQUIRE(U(timeGetTime));
    /* the harness module order puts winmm first */
    EXPECT_EQ((ULONG_PTR)U(timeGetTime), (ULONG_PTR)t_proc_in("winmm.dll", "timeGetTime"));
    EXPECT_EQ((ULONG_PTR)U(timeGetSystemTime), (ULONG_PTR)t_proc_in("winmm.dll", "timeGetSystemTime"));
    EXPECT_EQ((ULONG_PTR)U(timeGetDevCaps), (ULONG_PTR)t_proc_in("winmm.dll", "timeGetDevCaps"));
    EXPECT_EQ((ULONG_PTR)U(timeBeginPeriod), (ULONG_PTR)t_proc_in("winmm.dll", "timeBeginPeriod"));
    EXPECT_EQ((ULONG_PTR)U(timeEndPeriod), (ULONG_PTR)t_proc_in("winmm.dll", "timeEndPeriod"));
}

TEST(time_basic_identity_ordinal_calls) {
    REQUIRE(U(timeGetTime));
    typedef DWORD (WINAPI *gt_t)(void);
    typedef MMRESULT (WINAPI *gc_t)(LPTIMECAPS, UINT);
    typedef MMRESULT (WINAPI *gs_t)(LPMMTIME, UINT);
    typedef MMRESULT (WINAPI *bp_t)(UINT);
    gt_t gt = (gt_t)t_proc_ord("winmm.dll", 141);
    gc_t gc = (gc_t)t_proc_ord("winmm.dll", 139);
    gs_t gs = (gs_t)t_proc_ord("winmm.dll", 140);
    bp_t bp = (bp_t)t_proc_ord("winmm.dll", 137), ep = (bp_t)t_proc_ord("winmm.dll", 138);
    if (!gt || !gc || !gs || !bp || !ep) { CHECK(0); return; }
    CHECK(tb_diff(gt(), U(timeGetTime)()) <= 0);
    TIMECAPS tc; EXPECT_EQ(gc(&tc, 8), 0); EXPECT_EQ(tc.wPeriodMax, 1000000); EXPECT_EQ(gc(&tc, 7), TB_NOCANDO);
    MMTIME mt; EXPECT_EQ(gs(&mt, 12), 0); EXPECT_EQ(mt.wType, TIME_MS); EXPECT_EQ(gs(&mt, 11), TB_STRUCT);
    EXPECT_EQ(bp(0), TB_NOCANDO);
    EXPECT_EQ(bp(tb_pmin()), 0);
    EXPECT_EQ(ep(tb_pmin()), 0);
    EXPECT_EQ(ep(tb_pmin()), TB_NOCANDO);
}

TEST(time_basic_identity_next_to_event_exports) {
    REQUIRE(U(timeGetTime));
    /* the event functions are local winmm bodies, not kernel32 thunks */
    FARPROC s = t_proc_in("winmm.dll", "timeSetEvent"), k = t_proc_in("winmm.dll", "timeKillEvent");
    CHECK(s != NULL); CHECK(k != NULL);
    CHECK(t_proc_in("kernel32.dll", "timeSetEvent") == NULL);
    CHECK(t_proc_in("kernel32.dll", "timeKillEvent") == NULL);
    EXPECT_EQ((ULONG_PTR)s, (ULONG_PTR)t_proc_ord("winmm.dll", 143));
    EXPECT_EQ((ULONG_PTR)k, (ULONG_PTR)t_proc_ord("winmm.dll", 142));
}
