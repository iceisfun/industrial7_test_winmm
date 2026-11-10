#include "ps.h"
/* Flag gate and the device-independent outcome of every valid flag word, for four name classes that never play
 * anything: NULL, "", a relative name that exists nowhere, and a rooted path in a directory that does not exist.
 *
 * sndPlaySoundI (winmm), in order:
 *   1. f & 0xFF80DF20 -> FALSE.
 *   2. SND_NOSTOP with a current wave header -> FALSE (nothing plays in this file, so never taken here).
 *   3. SND_ASYNC creates the notify window; NULL psz with ASYNC|MEMORY drops ASYNC.
 *   4. MEMORY bit or psz == NULL: (f & 0x40004) == 0x40004 -> FindResourceW(hmod, psz, "SOUND"/"WAVE"), miss ->
 *      FALSE; ASYNC|MEMORY needs 'RIFF' at 0 and 'WAVE' at 8; then ASYNC posts (TRUE) or sync sndMessage:
 *      NULL stops (TRUE), a non-RIFF image fails soundInitWavHdr (FALSE).
 *   5. Name: SND_ALIAS_ID loads a winmm string; an empty name returns TRUE at once; alias lookup, then the file
 *      search unless SND_SYSTEM. A rooted path that does not exist gets ".WAV" appended and is handed on (async
 *      posts -> TRUE); a relative name that SearchPath and MediaPath cannot find -> FALSE under SND_NODEFAULT.
 * VM_TIMEOUT: 180 */

enum { C_NULL, C_EMPTY, C_MISSREL, C_MISSROOT, C_N };
static const char *const cls_name[C_N] = { "NULL", "empty", "missing-relative", "missing-rooted" };
/* zero-padded so a flag word with SND_MEMORY reads the name bytes as a (non-RIFF) image without running off */
static WCHAR g_names[C_N][64] = { L"", L"", L"i7ps_no_such_sound_q7", L"C:\\i7ps_no_such_dir_q7\\nothing.wav" };

static int cls_kind(int c) { return c == C_NULL ? PSZ_NULL : PSZ_WIDE; }

/* SND_ALIAS_ID with a string pointer would hand the pointer bits to LoadStringW: never sent with a name. */
static int skip_row(int c, DWORD f) {
    return c != C_NULL && !(f & PS_BAD_MASK) && (f & SND_ALIAS_ID) == SND_ALIAS_ID;
}

static BOOL expect_row(int c, DWORD f) {
    if (f & PS_BAD_MASK) return FALSE;
    switch (c) {
    case C_NULL:     return (f & SND_RESOURCE) != SND_RESOURCE;  /* FindResourceW(hmod, NULL, ..) finds nothing */
    case C_EMPTY:    return !(f & SND_MEMORY);
    case C_MISSREL:  return !(f & SND_MEMORY) && (f & SND_SYSTEM) && (f & SND_ASYNC);
    case C_MISSROOT: return !(f & SND_MEMORY) && (f & SND_ASYNC);
    }
    return -1;
}

static void run_row(int c, DWORD f, int ep) {
    if (skip_row(c, f)) return;
    if (c != C_NULL) f |= SND_NODEFAULT;
    BOOL got = ps_call(ep, cls_kind(c), g_names[c], NULL, f);
    BOOL want = expect_row(c, f);
    PS_EXPECT(!!got == !!want, "%s %s f=0x%08lx got %d want %d", ps_ep_name[ep], cls_name[c], f, got, want);
}

/* ---- the gate itself ---------------------------------------------------------------------------------------- */

static const DWORD g_bad_bits[] = { 0x20, 0x100, 0x200, 0x400, 0x800, 0x1000, 0x4000, 0x8000, 0x800000, 0x1000000,
    0x2000000, 0x4000000, 0x8000000, 0x10000000, 0x20000000, 0x40000000, 0x80000000 };

PTEST(gate_mask_is_complement_of_valid) {
    /* The documented flags (SND_RESOURCE counted as its 0x40000 bit) plus the undocumented 0x400000, which shell32's
     * SHPlaySound passes, are exactly the complement of the mask. */
    DWORD doc = SND_ASYNC | SND_NODEFAULT | SND_MEMORY | SND_LOOP | SND_NOSTOP | SND_PURGE | SND_APPLICATION |
                SND_NOWAIT | SND_ALIAS | SND_FILENAME | SND_RESOURCE | SND_SENTRY | SND_RING | SND_SYSTEM;
    EXPECT_EQ(doc, 0x003F20DF);
    EXPECT_EQ(doc | 0x400000, PS_VALID_MASK);
    EXPECT_EQ(PS_VALID_MASK & PS_BAD_MASK, 0);
    EXPECT_EQ(PS_VALID_MASK | PS_BAD_MASK, 0xFFFFFFFF);
    int n = 0; for (int i = 0; i < 32; i++) if (PS_BAD_MASK & (1u << i)) n++;
    EXPECT_EQ(n, 17);
    EXPECT_EQ(sizeof g_bad_bits / sizeof g_bad_bits[0], 17);
}

/* 0x400000 is accepted like any valid bit. */
PTEST(gate_undocumented_0x400000_accepted) {
    REQUIRE(ps_have_all()); ps_quiesce();
    for (int ep = 0; ep < EP_N; ep++) {
        CHECKF(ps_call(ep, PSZ_NULL, NULL, NULL, 0x400000), "%s NULL", ps_ep_name[ep]);
        CHECKF(ps_call(ep, PSZ_WIDE, L"", NULL, 0x400000 | SND_NODEFAULT), "%s empty", ps_ep_name[ep]);
        CHECKF(!ps_call(ep, PSZ_WIDE, L"i7ps_no_such_sound_q7", NULL, 0x400000 | SND_NODEFAULT), "%s missing", ps_ep_name[ep]);
        CHECKF(ps_call(ep, PSZ_WIDE, L"C:\\i7ps_nodir\\x.wav", NULL, 0x400000 | SND_ASYNC | SND_NODEFAULT), "%s rooted async", ps_ep_name[ep]);
    }
    ps_stop();
}

#define GATE_TEST(nm, fn, call) PTEST(gate_##nm) { REQUIRE(fn); ps_quiesce(); \
    for (size_t i = 0; i < sizeof g_bad_bits / sizeof g_bad_bits[0]; i++) { DWORD f = g_bad_bits[i]; \
        CHECKF(!(call), "bit 0x%lx", f); \
        f = g_bad_bits[i] | SND_NODEFAULT; CHECKF(!(call), "0x%lx", f); \
        f = g_bad_bits[i] | SND_ASYNC | SND_NODEFAULT; CHECKF(!(call), "0x%lx", f); \
        f = g_bad_bits[i] | SND_MEMORY | SND_NODEFAULT; CHECKF(!(call), "0x%lx", f); } \
    ps_stop(); }

/* every invalid bit fails even for the stop request (psz NULL), which is otherwise always TRUE */
GATE_TEST(null_PlaySoundW,  U(PlaySoundW),  U(PlaySoundW)(NULL, NULL, f))
GATE_TEST(null_PlaySoundA,  U(PlaySoundA),  U(PlaySoundA)(NULL, NULL, f))
GATE_TEST(null_PlaySound,   U(PlaySound),   U(PlaySound)(NULL, NULL, f))
GATE_TEST(null_sndPlaySoundA, U(sndPlaySoundA), U(sndPlaySoundA)(NULL, f))
GATE_TEST(null_sndPlaySoundW, U(sndPlaySoundW), U(sndPlaySoundW)(NULL, f))
/* "" returns TRUE right after the gate for any valid word; an invalid word still fails */
GATE_TEST(empty_PlaySoundW, U(PlaySoundW),  U(PlaySoundW)(L"", NULL, f))
GATE_TEST(empty_PlaySoundA, U(PlaySoundA),  U(PlaySoundA)("", NULL, f))
GATE_TEST(empty_sndPlaySoundW, U(sndPlaySoundW), U(sndPlaySoundW)(L"", f))
GATE_TEST(empty_sndPlaySoundA, U(sndPlaySoundA), U(sndPlaySoundA)("", f))

PTEST(gate_ordinal2_null) {
    BOOL (WINAPI *p)(LPCSTR, HMODULE, DWORD) = (void *)t_proc_ord("winmm.dll", 2);
    REQUIRE(p);
    for (size_t i = 0; i < sizeof g_bad_bits / sizeof g_bad_bits[0]; i++) {
        CHECKF(!p(NULL, NULL, g_bad_bits[i]), "bit 0x%lx", g_bad_bits[i]);
        CHECKF(!p("", NULL, g_bad_bits[i] | SND_NODEFAULT), "bit 0x%lx", g_bad_bits[i]);
    }
    CHECK(p(NULL, NULL, 0));
    CHECK(p("", NULL, SND_NODEFAULT));
    ps_stop();
}

/* Each invalid bit on top of every valid bit at once: the gate runs before any flag is interpreted. */
PTEST(gate_bad_bit_with_all_valid_bits) {
    REQUIRE(ps_have_all()); ps_quiesce(); ps_sweep_begin();
    for (size_t i = 0; i < sizeof g_bad_bits / sizeof g_bad_bits[0]; i++)
        for (int ep = 0; ep < EP_N; ep++) {
            DWORD f = PS_VALID_MASK | g_bad_bits[i];
            for (int c = 0; c < C_N; c++) {
                BOOL got = ps_call(ep, cls_kind(c), g_names[c], NULL, f);
                PS_EXPECT(!got, "%s %s f=0x%08lx got %d", ps_ep_name[ep], cls_name[c], f, got);
            }
        }
    PS_SWEEP_END(); ps_stop();
}

/* All 32 bits at once and the high byte alone. */
PTEST(gate_all_bits_and_high_byte) {
    REQUIRE(ps_have_all()); ps_quiesce(); ps_sweep_begin();
    static const DWORD fs[] = { 0xFFFFFFFF, 0xFF000000, 0x00800000, 0x0000DF20, 0xFF80DF20, 0x80000000, 0x40000000,
                                0x80000002, 0x40000004, 0x00800004 | SND_ASYNC };
    for (size_t i = 0; i < sizeof fs / sizeof fs[0]; i++)
        for (int ep = 0; ep < EP_N; ep++)
            for (int c = 0; c < C_N; c++) {
                BOOL got = ps_call(ep, cls_kind(c), g_names[c], NULL, fs[i]);
                PS_EXPECT(!got, "%s %s f=0x%08lx got %d", ps_ep_name[ep], cls_name[c], fs[i], got);
            }
    PS_SWEEP_END(); ps_stop();
}

/* ---- single bits: each class x each entry point -------------------------------------------------------------- */

static void single_bits(int c) {
    ps_quiesce(); ps_sweep_begin();
    for (int ep = 0; ep < EP_N; ep++) {
        run_row(c, 0, ep);
        for (int b = 0; b < 32; b++) run_row(c, 1u << b, ep);
        run_row(c, SND_RESOURCE, ep);
        run_row(c, SND_ALIAS_ID, ep);
    }
    PS_SWEEP_END(); ps_stop();
}
PTEST(single_bits_null)            { REQUIRE(ps_have_all()); single_bits(C_NULL); }
PTEST(single_bits_empty)           { REQUIRE(ps_have_all()); single_bits(C_EMPTY); }
PTEST(single_bits_missing_relative){ REQUIRE(ps_have_all()); single_bits(C_MISSREL); }
PTEST(single_bits_missing_rooted)  { REQUIRE(ps_have_all()); single_bits(C_MISSROOT); }

/* ---- every pair of bits ------------------------------------------------------------------------------------- */

static void pairs(int c, int ep) {
    ps_quiesce(); ps_sweep_begin();
    for (int i = 0; i < 32; i++)
        for (int j = i + 1; j < 32; j++) run_row(c, (1u << i) | (1u << j), ep);
    PS_SWEEP_END(); ps_stop();
}
#define PAIRS(cn, c) \
    PTEST(pairs_##cn##_PlaySoundA)    { REQUIRE(ps_have_all()); pairs(c, EP_A); } \
    PTEST(pairs_##cn##_PlaySoundW)    { REQUIRE(ps_have_all()); pairs(c, EP_W); } \
    PTEST(pairs_##cn##_PlaySound)     { REQUIRE(ps_have_all()); pairs(c, EP_PS); } \
    PTEST(pairs_##cn##_ordinal2)      { REQUIRE(ps_have_all()); pairs(c, EP_ORD2); } \
    PTEST(pairs_##cn##_sndPlaySoundA) { REQUIRE(ps_have_all()); pairs(c, EP_SNDA); } \
    PTEST(pairs_##cn##_sndPlaySoundW) { REQUIRE(ps_have_all()); pairs(c, EP_SNDW); }
PAIRS(null, C_NULL)
PAIRS(empty, C_EMPTY)
PAIRS(missrel, C_MISSREL)
PAIRS(missroot, C_MISSROOT)

/* ---- valid combinations that applications actually pass -------------------------------------------------------- */

static const DWORD g_common[] = {
    SND_SYNC, SND_ASYNC, SND_NODEFAULT, SND_ASYNC | SND_NODEFAULT, SND_FILENAME, SND_FILENAME | SND_ASYNC,
    SND_ALIAS, SND_ALIAS | SND_ASYNC, SND_ALIAS | SND_ASYNC | SND_NOSTOP, SND_ALIAS | SND_APPLICATION | SND_ASYNC,
    SND_ALIAS | SND_SYSTEM | SND_ASYNC, SND_FILENAME | SND_ASYNC | SND_LOOP, SND_FILENAME | SND_NOWAIT,
    SND_PURGE, SND_FILENAME | SND_SENTRY, SND_ALIAS | SND_SENTRY | SND_ASYNC, SND_RING | SND_ASYNC,
    SND_SYSTEM, SND_SYSTEM | SND_ASYNC, SND_MEMORY, SND_MEMORY | SND_ASYNC, SND_RESOURCE, SND_RESOURCE | SND_ASYNC,
    SND_ASYNC | SND_LOOP, SND_ASYNC | SND_LOOP | SND_NOSTOP, SND_FILENAME | SND_ALIAS, PS_VALID_MASK,
    PS_VALID_MASK & ~SND_ASYNC, PS_VALID_MASK & ~SND_MEMORY, PS_VALID_MASK & ~(SND_MEMORY | SND_RING),
};

static void common(int c) {
    ps_quiesce(); ps_sweep_begin();
    for (int ep = 0; ep < EP_N; ep++)
        for (size_t i = 0; i < sizeof g_common / sizeof g_common[0]; i++) run_row(c, g_common[i], ep);
    PS_SWEEP_END(); ps_stop();
}
PTEST(common_combos_null)             { REQUIRE(ps_have_all()); common(C_NULL); }
PTEST(common_combos_empty)            { REQUIRE(ps_have_all()); common(C_EMPTY); }
PTEST(common_combos_missing_relative) { REQUIRE(ps_have_all()); common(C_MISSREL); }
PTEST(common_combos_missing_rooted)   { REQUIRE(ps_have_all()); common(C_MISSROOT); }

/* ---- the stop request ------------------------------------------------------------------------------------------ */

PTEST(null_stop_forms) {
    REQUIRE(ps_have_all()); ps_quiesce();
    for (int ep = 0; ep < EP_N; ep++) {
        CHECKF(ps_call(ep, PSZ_NULL, NULL, NULL, 0), "%s(NULL, 0)", ps_ep_name[ep]);
        CHECKF(ps_call(ep, PSZ_NULL, NULL, NULL, SND_ASYNC), "%s(NULL, ASYNC)", ps_ep_name[ep]);
        CHECKF(ps_call(ep, PSZ_NULL, NULL, NULL, SND_MEMORY), "%s(NULL, MEMORY)", ps_ep_name[ep]);
        /* NULL with ASYNC|MEMORY drops ASYNC and stops synchronously */
        CHECKF(ps_call(ep, PSZ_NULL, NULL, NULL, SND_ASYNC | SND_MEMORY), "%s(NULL, ASYNC|MEMORY)", ps_ep_name[ep]);
        CHECKF(ps_call(ep, PSZ_NULL, NULL, NULL, SND_PURGE), "%s(NULL, PURGE)", ps_ep_name[ep]);
        CHECKF(ps_call(ep, PSZ_NULL, NULL, NULL, SND_NOSTOP), "%s(NULL, NOSTOP) with nothing playing", ps_ep_name[ep]);
        CHECKF(ps_call(ep, PSZ_NULL, NULL, NULL, SND_ALIAS_ID), "%s(NULL, ALIAS_ID)", ps_ep_name[ep]);
        CHECKF(ps_call(ep, PSZ_NULL, NULL, NULL, SND_ALIAS_ID | SND_ASYNC), "%s(NULL, ALIAS_ID|ASYNC)", ps_ep_name[ep]);
        CHECKF(ps_call(ep, PSZ_NULL, NULL, NULL, SND_FILENAME | SND_NODEFAULT), "%s(NULL, FILENAME)", ps_ep_name[ep]);
        CHECKF(ps_call(ep, PSZ_NULL, NULL, NULL, SND_SYSTEM | SND_ASYNC), "%s(NULL, SYSTEM|ASYNC)", ps_ep_name[ep]);
    }
    ps_stop();
}

/* SND_RESOURCE (which contains the MEMORY bit) with psz NULL looks up resource NULL and fails. */
PTEST(null_resource_fails) {
    REQUIRE(ps_have_all()); ps_quiesce();
    HMODULE self = GetModuleHandleW(NULL);
    for (int ep = 0; ep < EP_N; ep++) {
        CHECKF(!ps_call(ep, PSZ_NULL, NULL, NULL, SND_RESOURCE), "%s", ps_ep_name[ep]);
        CHECKF(!ps_call(ep, PSZ_NULL, NULL, NULL, SND_RESOURCE | SND_ASYNC), "%s async", ps_ep_name[ep]);
        CHECKF(!ps_call(ep, PSZ_NULL, NULL, NULL, SND_RESOURCE | SND_NODEFAULT), "%s nodefault", ps_ep_name[ep]);
    }
    CHECK(!U(PlaySoundW)(NULL, self, SND_RESOURCE));
    CHECK(!U(PlaySoundA)(NULL, self, SND_RESOURCE | SND_ASYNC));
    CHECK(!U(PlaySoundW)(NULL, t_module("winmm.dll"), SND_RESOURCE));
    ps_stop();
}

/* The stop request is idempotent and does not depend on whether anything was played before. */
PTEST(null_stop_repeated) {
    REQUIRE(U(PlaySoundW)); REQUIRE(U(sndPlaySoundW));
    for (int i = 0; i < 200; i++) {
        CHECK(U(PlaySoundW)(NULL, NULL, 0));
        if (i % 20 == 0) CHECK(U(PlaySoundW)(NULL, NULL, SND_ASYNC));
        if (i % 25 == 0) CHECK(U(sndPlaySoundW)(NULL, SND_ASYNC));
    }
    ps_stop();
}

/* An empty name returns TRUE for every valid flag word without a MEMORY bit, for every hmod. */
PTEST(empty_name_any_hmod) {
    REQUIRE(U(PlaySoundW)); REQUIRE(U(PlaySoundA)); ps_quiesce();
    HMODULE mods[] = { NULL, GetModuleHandleW(NULL), t_module("winmm.dll"), t_module("kernel32.dll") };
    for (size_t m = 0; m < 4; m++) {
        CHECKF(U(PlaySoundW)(L"", mods[m], SND_NODEFAULT), "mod %zu", m);
        CHECKF(U(PlaySoundA)("", mods[m], SND_NODEFAULT | SND_ASYNC), "mod %zu", m);
        CHECKF(U(PlaySoundW)(L"", mods[m], SND_NODEFAULT | SND_FILENAME), "mod %zu", m);
        CHECKF(U(PlaySoundW)(L"", mods[m], SND_NODEFAULT | SND_ALIAS), "mod %zu", m);
        CHECKF(!U(PlaySoundW)(L"", mods[m], SND_NODEFAULT | SND_RESOURCE), "mod %zu: resource \"\"", m);
    }
    ps_stop();
}

/* Missing relative names fail even when async: the alias/file search runs on the caller's thread. */
PTEST(missing_relative_async_fails_synchronously) {
    REQUIRE(ps_have_all()); ps_quiesce();
    static const WCHAR *names[] = { L"i7ps_nothing_a", L"i7ps_nothing_b.wav", L"i7ps nothing with spaces",
        L"i7ps_nothing.WAV", L".\\i7ps_nothing_c", L"i7ps_sub\\nothing", L"i7ps_nothing_d.", L"i7ps_nothing_e.mp3" };
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
        for (int ep = 0; ep < EP_N; ep++) {
            CHECKF(!ps_call(ep, PSZ_WIDE, names[i], NULL, SND_ASYNC | SND_NODEFAULT), "%s %ls", ps_ep_name[ep], names[i]);
            CHECKF(!ps_call(ep, PSZ_WIDE, names[i], NULL, SND_ASYNC | SND_FILENAME | SND_NODEFAULT), "%s %ls", ps_ep_name[ep], names[i]);
            CHECKF(!ps_call(ep, PSZ_WIDE, names[i], NULL, SND_ASYNC | SND_ALIAS | SND_NODEFAULT), "%s %ls", ps_ep_name[ep], names[i]);
            CHECKF(!ps_call(ep, PSZ_WIDE, names[i], NULL, SND_NODEFAULT), "%s %ls", ps_ep_name[ep], names[i]);
            /* SND_SYSTEM skips the file search: async posts the name and returns TRUE */
            CHECKF(ps_call(ep, PSZ_WIDE, names[i], NULL, SND_ASYNC | SND_SYSTEM | SND_NODEFAULT), "%s %ls", ps_ep_name[ep], names[i]);
            CHECKF(!ps_call(ep, PSZ_WIDE, names[i], NULL, SND_SYSTEM | SND_NODEFAULT), "%s %ls", ps_ep_name[ep], names[i]);
        }
    ps_stop();
}

/* A rooted path is not checked for existence before an async post. */
PTEST(missing_rooted_async_posts) {
    REQUIRE(ps_have_all()); ps_quiesce();
    static const WCHAR *names[] = { L"C:\\i7ps_nodir\\x.wav", L"C:\\i7ps_nodir\\x", L"C:\\i7ps_nofile.wav",
        L"C:\\i7ps_nodir\\a\\b\\c\\d.wav", L"Z:\\i7ps\\x.wav", L"\\\\?\\C:\\i7ps_nodir\\x.wav", L"c:/i7ps_nodir/x.wav" };
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
        for (int ep = 0; ep < EP_N; ep++) {
            BOOL a = ps_call(ep, PSZ_WIDE, names[i], NULL, SND_ASYNC | SND_FILENAME | SND_NODEFAULT);
            BOOL s = ps_call(ep, PSZ_WIDE, names[i], NULL, SND_FILENAME | SND_NODEFAULT);
            if (i < 5) { CHECKF(a, "%s %ls async", ps_ep_name[ep], names[i]); }
            else if (ep == 0) t_note("%ls async -> %d", names[i], a);
            CHECKF(!s, "%s %ls sync", ps_ep_name[ep], names[i]);
        }
    ps_stop();
}
