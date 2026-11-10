#include "wo.h"
/* waveOutOpen parameter gates (steps 1..5 of the ladder in wo.h). Everything here is decided before
 * winmmbase looks at the device list, so the codes are the same with and without an audio device. Each
 * failing gate returns before *phwo is written: the sentinel must survive. */

static const UINT g_ids[] = { 0, 1, 2, 0xFFFF, WAVE_MAPPER, WO_BADID, 0xFFFE, 0x10000, 0x7FFFFFFF, 0xFFFFFFFE };
#define NIDS (sizeof g_ids / sizeof g_ids[0])

/* ---- step 1: pwfx NULL ---- */

TEST(wo_open_nullfmt_every_id_and_flag) {
    REQUIRE(U(waveOutOpen));
    static const DWORD fls[] = { 0, WAVE_FORMAT_QUERY, WAVE_ALLOWSYNC, WAVE_FORMAT_DIRECT, CALLBACK_EVENT, CALLBACK_FUNCTION,
                                 CALLBACK_THREAD, CALLBACK_WINDOW, WAVE_FORMAT_DIRECT_QUERY };
    for (unsigned i = 0; i < NIDS; i++)
        for (unsigned f = 0; f < sizeof fls / sizeof fls[0]; f++) {
            HWAVEOUT h = WO_SENTINEL;
            MMRESULT r = wo_open(&h, g_ids[i], NULL, 0, 0, fls[f]);
            CHECKF(r == MMSYSERR_INVALPARAM, "id 0x%x fl 0x%lx -> %u", g_ids[i], fls[f], r);
            HWAVEOUT after = h;
            CHECKF(after == WO_SENTINEL, "id 0x%x fl 0x%lx: *phwo written (%p)", g_ids[i], fls[f], after);
        }
}

TEST(wo_open_nullfmt_null_phwo) {
    REQUIRE(U(waveOutOpen));
    for (unsigned i = 0; i < NIDS; i++) {
        EXPECT_EQ(wo_open(NULL, g_ids[i], NULL, 0, 0, 0), MMSYSERR_INVALPARAM);
        EXPECT_EQ(wo_open(NULL, g_ids[i], NULL, 0, 0, WAVE_FORMAT_QUERY), MMSYSERR_INVALPARAM);
    }
}

TEST(wo_open_nullfmt_beats_bad_flags) {
    REQUIRE(U(waveOutOpen));
    static const DWORD bad[] = { 0x200, 0x400, 0x800, 0x1000, 0x2000, 0x4000, 0x7E00, WAVE_MAPPED | 0x200 };
    for (unsigned i = 0; i < NIDS; i++)
        for (unsigned b = 0; b < sizeof bad / sizeof bad[0]; b++) {
            HWAVEOUT h = WO_SENTINEL;
            EXPECT_EQ(wo_open(&h, g_ids[i], NULL, 0, 0, bad[b]), MMSYSERR_INVALPARAM);
            HWAVEOUT after = h; EXPECT_EQ(after, WO_SENTINEL);
        }
}

TEST(wo_open_nullfmt_beats_bad_window) {
    REQUIRE(U(waveOutOpen));
    for (unsigned i = 0; i < NIDS; i++) {
        HWAVEOUT h = WO_SENTINEL;
        EXPECT_EQ(wo_open(&h, g_ids[i], NULL, WO_NOWND, 0, CALLBACK_WINDOW), MMSYSERR_INVALPARAM);
        HWAVEOUT after = h; EXPECT_EQ(after, WO_SENTINEL);
        EXPECT_EQ(wo_open(NULL, g_ids[i], NULL, WO_NOWND, 0, CALLBACK_WINDOW | 0x4000), MMSYSERR_INVALPARAM);
    }
}

TEST(wo_open_nullfmt_mapped_with_mapper) {
    REQUIRE(U(waveOutOpen));
    /* pwfx is tested before the WAVE_MAPPER mask: 11, not 10. */
    EXPECT_EQ(wo_open(NULL, WAVE_MAPPER, NULL, 0, 0, WAVE_MAPPED), MMSYSERR_INVALPARAM);
    EXPECT_EQ(wo_open(NULL, 0xFFFF, NULL, 0, 0, WAVE_MAPPED), MMSYSERR_INVALPARAM);
    EXPECT_EQ(wo_open(NULL, 0, NULL, 0, 0, WAVE_MAPPED_DEFAULT_COMMUNICATION_DEVICE), MMSYSERR_INVALPARAM);
}

/* ---- step 2: CALLBACK_WINDOW with something that is not a window ---- */

TEST(wo_open_badwindow_every_id) {
    REQUIRE(U(waveOutOpen)); REQUIRE(U(IsWindow));
    CHECK(!wo_iswindow((HWND)WO_NOWND));
    WAVEFORMATEX wf; wo_std(&wf);
    static const DWORD fls[] = { CALLBACK_WINDOW, CALLBACK_WINDOW | WAVE_FORMAT_QUERY, CALLBACK_WINDOW | WAVE_ALLOWSYNC,
                                 CALLBACK_WINDOW | WAVE_FORMAT_DIRECT };
    for (unsigned i = 0; i < NIDS; i++)
        for (unsigned f = 0; f < sizeof fls / sizeof fls[0]; f++) {
            HWAVEOUT h = WO_SENTINEL;
            MMRESULT r = wo_open(&h, g_ids[i], &wf, WO_NOWND, 0, fls[f]);
            CHECKF(r == MMSYSERR_INVALPARAM, "id 0x%x fl 0x%lx -> %u", g_ids[i], fls[f], r);
            HWAVEOUT after = h; CHECK(after == WO_SENTINEL);
        }
}

TEST(wo_open_badwindow_null_phwo) {
    REQUIRE(U(waveOutOpen));
    WAVEFORMATEX wf; wo_std(&wf);
    for (unsigned i = 0; i < NIDS; i++)
        EXPECT_EQ(wo_open(NULL, g_ids[i], &wf, WO_NOWND, 0, CALLBACK_WINDOW), MMSYSERR_INVALPARAM);
}

TEST(wo_open_badwindow_beats_bad_flags) {
    REQUIRE(U(waveOutOpen));
    WAVEFORMATEX wf; wo_std(&wf);
    static const DWORD bad[] = { 0x200, 0x4000, 0x7E00, WAVE_MAPPED, WAVE_MAPPED_DEFAULT_COMMUNICATION_DEVICE };
    for (unsigned i = 0; i < NIDS; i++)
        for (unsigned b = 0; b < sizeof bad / sizeof bad[0]; b++) {
            HWAVEOUT h = WO_SENTINEL;
            MMRESULT r = wo_open(&h, g_ids[i], &wf, WO_NOWND, 0, CALLBACK_WINDOW | bad[b]);
            CHECKF(r == MMSYSERR_INVALPARAM, "id 0x%x fl 0x%lx -> %u (window is checked before the flag mask)",
                   g_ids[i], CALLBACK_WINDOW | bad[b], r);
            HWAVEOUT after = h; CHECK(after == WO_SENTINEL);
        }
}

TEST(wo_open_badwindow_small_values) {
    REQUIRE(U(waveOutOpen)); REQUIRE(U(IsWindow));
    WAVEFORMATEX wf; wo_std(&wf);
    static const DWORD_PTR vals[] = { 1, 2, 0x10, 0x1234, 0xFFFF, 0xFFFFFFFF, (DWORD_PTR)-1 };
    for (unsigned v = 0; v < sizeof vals / sizeof vals[0]; v++) {
        if (wo_iswindow((HWND)vals[v])) { t_note("0x%llx is a window here; skipped", (unsigned long long)vals[v]); continue; }
        HWAVEOUT h = WO_SENTINEL;
        MMRESULT r = wo_open(&h, WO_BADID, &wf, vals[v], 0, CALLBACK_WINDOW);
        CHECKF(r == MMSYSERR_INVALPARAM, "window 0x%llx -> %u", (unsigned long long)vals[v], r);
        r = wo_open(NULL, WAVE_MAPPER, &wf, vals[v], 0, CALLBACK_WINDOW | WAVE_FORMAT_QUERY);
        CHECKF(r == MMSYSERR_INVALPARAM, "query window 0x%llx -> %u", (unsigned long long)vals[v], r);
    }
}

TEST(wo_open_destroyed_window) {
    REQUIRE(U(waveOutOpen)); REQUIRE(U(CreateWindowExA)); REQUIRE(U(DestroyWindow));
    HWND w = wo_msgwnd();
    CHECK(w != NULL);
    if (!w) return;
    wo_destroy(w);
    if (wo_iswindow(w)) { t_note("destroyed hwnd value reused"); return; }
    WAVEFORMATEX wf; wo_std(&wf);
    HWAVEOUT h = WO_SENTINEL;
    EXPECT_EQ(wo_open(&h, 0, &wf, (DWORD_PTR)w, 0, CALLBACK_WINDOW), MMSYSERR_INVALPARAM);
    HWAVEOUT after = h; EXPECT_EQ(after, WO_SENTINEL);
    EXPECT_EQ(wo_open(NULL, WAVE_MAPPER, &wf, (DWORD_PTR)w, 0, CALLBACK_WINDOW | WAVE_FORMAT_QUERY), MMSYSERR_INVALPARAM);
}

/* Only callback type 1 (bits 16..18 == 1) is validated. Bit 19 (0x80000) is outside the type field, so
 * 0x90000 is still a window callback; every other type with a non-window value passes this gate and is
 * stopped here by an invalid flag bit instead (10). */
TEST(wo_open_callback_type_field_gate) {
    REQUIRE(U(waveOutOpen));
    WAVEFORMATEX wf; wo_std(&wf);
    for (DWORD hi = 0; hi < 16; hi++) {
        DWORD fl = (hi << 16) | 0x200;
        int is_window = (hi & 7) == 1;
        HWAVEOUT h = WO_SENTINEL;
        MMRESULT r = wo_open(&h, 0, &wf, WO_NOWND, 0, fl);
        UINT want = is_window ? MMSYSERR_INVALPARAM : MMSYSERR_INVALFLAG;
        CHECKF(r == want, "fl 0x%lx with callback 0x1234 -> %u want %u", fl, r, want);
        HWAVEOUT after = h; CHECK(after == WO_SENTINEL);
        r = wo_open(&h, WAVE_MAPPER, &wf, WO_NOWND, 0, fl | WAVE_MAPPED);
        CHECKF(r == want, "mapper fl 0x%lx -> %u want %u", fl | WAVE_MAPPED, r, want);
    }
}

TEST(wo_open_callback_type_window_high_bits) {
    REQUIRE(U(waveOutOpen));
    WAVEFORMATEX wf; wo_std(&wf);
    static const DWORD hi[] = { 0x00090000, 0x00190000, 0x00F90000, 0x10010000, 0x80010000, 0xFFF90000 };
    for (unsigned i = 0; i < sizeof hi / sizeof hi[0]; i++) {
        HWAVEOUT h = WO_SENTINEL;
        EXPECT_EQ(wo_open(&h, WO_BADID, &wf, WO_NOWND, 0, hi[i]), MMSYSERR_INVALPARAM);
        EXPECT_EQ(wo_open(NULL, WAVE_MAPPER, &wf, WO_NOWND, 0, hi[i] | WAVE_FORMAT_QUERY), MMSYSERR_INVALPARAM);
    }
}

TEST(wo_open_null_window_passes_gate) {
    REQUIRE(U(waveOutOpen));
    WAVEFORMATEX wf; wo_std(&wf);
    /* dwCallback 0 with CALLBACK_WINDOW is "no callback": the next gate decides. */
    for (unsigned i = 0; i < NIDS; i++) {
        UINT id = g_ids[i];
        UINT m = (id == 0xFFFF || id == WAVE_MAPPER) ? WAVE_MAPPED : WAVE_MAPPED_DEFAULT_COMMUNICATION_DEVICE;
        EXPECT_EQ(wo_open(NULL, id, &wf, 0, 0, CALLBACK_WINDOW | m), MMSYSERR_INVALFLAG);
        EXPECT_EQ(wo_open(NULL, id, &wf, 0, 0, CALLBACK_WINDOW), MMSYSERR_INVALPARAM);   /* phwo NULL */
    }
}

TEST(wo_open_valid_window_passes_gate) {
    REQUIRE(U(waveOutOpen)); REQUIRE(U(CreateWindowExA));
    HWND w = wo_msgwnd();
    CHECK(w != NULL);
    if (!w) return;
    WAVEFORMATEX wf; wo_std(&wf);
    for (unsigned i = 0; i < NIDS; i++) {
        UINT id = g_ids[i];
        UINT m = (id == 0xFFFF || id == WAVE_MAPPER) ? WAVE_MAPPED : WAVE_MAPPED_DEFAULT_COMMUNICATION_DEVICE;
        EXPECT_EQ(wo_open(NULL, id, &wf, (DWORD_PTR)w, 0, CALLBACK_WINDOW | m), MMSYSERR_INVALFLAG);
        EXPECT_EQ(wo_open(NULL, id, &wf, (DWORD_PTR)w, 0, CALLBACK_WINDOW), MMSYSERR_INVALPARAM);
    }
    wo_destroy(w);
}

TEST(wo_open_other_callback_types_not_validated) {
    REQUIRE(U(waveOutOpen));
    WAVEFORMATEX wf; wo_std(&wf);
    /* A non-window value with THREAD / FUNCTION / EVENT / type 4/6/7 is not looked at by the gates: phwo NULL
     * is what fails (11), and the value is never used. */
    static const DWORD types[] = { CALLBACK_NULL, CALLBACK_THREAD, CALLBACK_FUNCTION, 0x40000, CALLBACK_EVENT, 0x60000, 0x70000 };
    for (unsigned t = 0; t < sizeof types / sizeof types[0]; t++)
        for (unsigned i = 0; i < NIDS; i++) {
            MMRESULT r = wo_open(NULL, g_ids[i], &wf, WO_NOWND, 0, types[t]);
            CHECKF(r == MMSYSERR_INVALPARAM, "type 0x%lx id 0x%x -> %u", types[t], g_ids[i], r);
        }
}

/* ---- step 4: flag masks ---- */

/* Every single low-word bit, with phwo NULL and no QUERY: a masked bit gives 10, an accepted bit falls through
 * to the phwo gate (11). Nothing here depends on devices. */
TEST(wo_open_flag_bits_mapper) {
    REQUIRE(U(waveOutOpen));
    WAVEFORMATEX wf; wo_std(&wf);
    for (int b = 1; b < 16; b++) {
        DWORD bit = 1u << b;
        UINT want = (bit & WO_MASK_MAPPER) ? MMSYSERR_INVALFLAG : MMSYSERR_INVALPARAM;
        MMRESULT r = wo_open(NULL, WAVE_MAPPER, &wf, 0, 0, bit);
        CHECKF(r == want, "mapper bit 0x%lx -> %u want %u", bit, r, want);
        r = wo_open(NULL, 0xFFFF, &wf, 0, 0, bit);
        CHECKF(r == want, "0xFFFF bit 0x%lx -> %u want %u", bit, r, want);
    }
}

TEST(wo_open_flag_bits_device_ids) {
    REQUIRE(U(waveOutOpen));
    WAVEFORMATEX wf; wo_std(&wf);
    static const UINT ids[] = { 0, 1, WO_BADID, 0xFFFE, 0x10000, 0x1FFFF, 0x7FFFFFFF, 0xFFFFFFFE };
    for (unsigned i = 0; i < sizeof ids / sizeof ids[0]; i++)
        for (int b = 1; b < 16; b++) {
            DWORD bit = 1u << b;
            UINT want = (bit & WO_MASK_DEVICE) ? MMSYSERR_INVALFLAG : MMSYSERR_INVALPARAM;
            MMRESULT r = wo_open(NULL, ids[i], &wf, 0, 0, bit);
            CHECKF(r == want, "id 0x%x bit 0x%lx -> %u want %u", ids[i], bit, r, want);
        }
}

TEST(wo_open_flag_bits_sentinel_kept) {
    REQUIRE(U(waveOutOpen));
    WAVEFORMATEX wf; wo_std(&wf);
    for (int b = 2; b < 16; b++) {
        DWORD bit = 1u << b;
        if (bit & WO_MASK_DEVICE) {
            HWAVEOUT h = WO_SENTINEL;
            EXPECT_EQ(wo_open(&h, 0, &wf, 0, 0, bit), MMSYSERR_INVALFLAG);
            HWAVEOUT after = h; EXPECT_EQ(after, WO_SENTINEL);
        }
        if (bit & WO_MASK_MAPPER) {
            HWAVEOUT h = WO_SENTINEL;
            EXPECT_EQ(wo_open(&h, WAVE_MAPPER, &wf, 0, 0, bit), MMSYSERR_INVALFLAG);
            HWAVEOUT after = h; EXPECT_EQ(after, WO_SENTINEL);
        }
    }
}

/* Combinations: the masked part wins over everything after it; QUERY does not exempt a flag. */
TEST(wo_open_flag_combinations_table) {
    REQUIRE(U(waveOutOpen));
    WAVEFORMATEX wf; wo_std(&wf);
    static const DWORD fls[] = {
        0x0001, 0x0003, 0x0005, 0x0007, 0x000F, 0x0011, 0x0013, 0x0015, 0x001F, 0x0021, 0x00E1, 0x01E1, 0x0201,
        0x0401, 0x0801, 0x1001, 0x2001, 0x4001, 0x7E01, 0x8001, 0x81E1, 0xFFFF, 0xFFFE, 0x7E10, 0x7E04, 0x0014,
        0x8000, 0x81E0, 0x01EB, 0x01FB, 0x00010001, 0x00030001, 0x00050001, 0x80000001, 0x7FFF0000, 0xFFFF0000,
    };
    static const UINT ids[] = { 0, WAVE_MAPPER, 0xFFFF, WO_BADID };
    for (unsigned i = 0; i < sizeof ids / sizeof ids[0]; i++)
        for (unsigned f = 0; f < sizeof fls / sizeof fls[0]; f++) {
            DWORD fl = fls[f];
            UINT want = wo_gate(ids[i], 0, 0, fl, 1, 0);
            if (want == WO_UNSET) continue;            /* QUERY with valid flags reaches the device part */
            MMRESULT r = wo_open(NULL, ids[i], &wf, 0, 0, fl);
            CHECKF(r == want, "id 0x%x fl 0x%lx -> %u want %u", ids[i], fl, r, want);
        }
}

TEST(wo_open_mapped_flag_with_mapper_ids) {
    REQUIRE(U(waveOutOpen));
    WAVEFORMATEX wf; wo_std(&wf);
    static const DWORD extra[] = { 0, WAVE_FORMAT_QUERY, WAVE_ALLOWSYNC, WAVE_FORMAT_DIRECT, WAVE_FORMAT_DIRECT_QUERY,
                                   CALLBACK_EVENT, CALLBACK_FUNCTION, WAVE_MAPPED_DEFAULT_COMMUNICATION_DEVICE };
    for (unsigned e = 0; e < sizeof extra / sizeof extra[0]; e++) {
        HWAVEOUT h = WO_SENTINEL;
        EXPECT_EQ(wo_open(&h, WAVE_MAPPER, &wf, 0, 0, WAVE_MAPPED | extra[e]), MMSYSERR_INVALFLAG);
        HWAVEOUT after = h; EXPECT_EQ(after, WO_SENTINEL);
        h = WO_SENTINEL;
        EXPECT_EQ(wo_open(&h, 0xFFFF, &wf, 0, 0, WAVE_MAPPED | extra[e]), MMSYSERR_INVALFLAG);
        after = h; EXPECT_EQ(after, WO_SENTINEL);
        EXPECT_EQ(wo_open(NULL, WAVE_MAPPER, &wf, 0, 0, WAVE_MAPPED | extra[e]), MMSYSERR_INVALFLAG);
    }
}

TEST(wo_open_defcomm_flag_with_device_ids) {
    REQUIRE(U(waveOutOpen));
    WAVEFORMATEX wf; wo_std(&wf);
    static const UINT ids[] = { 0, 1, WO_BADID, 0xFFFE, 0x7FFFFFFF, 0xFFFFFFFE };
    static const DWORD extra[] = { 0, WAVE_FORMAT_QUERY, WAVE_MAPPED, WAVE_FORMAT_DIRECT_QUERY, CALLBACK_EVENT };
    for (unsigned i = 0; i < sizeof ids / sizeof ids[0]; i++)
        for (unsigned e = 0; e < sizeof extra / sizeof extra[0]; e++) {
            HWAVEOUT h = WO_SENTINEL;
            MMRESULT r = wo_open(&h, ids[i], &wf, 0, 0, WAVE_MAPPED_DEFAULT_COMMUNICATION_DEVICE | extra[e]);
            CHECKF(r == MMSYSERR_INVALFLAG, "id 0x%x fl 0x%lx -> %u", ids[i], WAVE_MAPPED_DEFAULT_COMMUNICATION_DEVICE | extra[e], r);
            HWAVEOUT after = h; CHECK(after == WO_SENTINEL);
        }
}

TEST(wo_open_defcomm_flag_with_mapper_passes_mask) {
    REQUIRE(U(waveOutOpen));
    WAVEFORMATEX wf; wo_std(&wf);
    /* 0x10 is legal with the mapper: with phwo NULL the phwo gate answers. */
    EXPECT_EQ(wo_open(NULL, WAVE_MAPPER, &wf, 0, 0, WAVE_MAPPED_DEFAULT_COMMUNICATION_DEVICE), MMSYSERR_INVALPARAM);
    EXPECT_EQ(wo_open(NULL, 0xFFFF, &wf, 0, 0, WAVE_MAPPED_DEFAULT_COMMUNICATION_DEVICE), MMSYSERR_INVALPARAM);
    EXPECT_EQ(wo_open(NULL, WAVE_MAPPER, &wf, 0, 0, WAVE_MAPPED_DEFAULT_COMMUNICATION_DEVICE | WAVE_ALLOWSYNC), MMSYSERR_INVALPARAM);
}

TEST(wo_open_mapped_flag_with_device_ids_passes_mask) {
    REQUIRE(U(waveOutOpen));
    WAVEFORMATEX wf; wo_std(&wf);
    static const UINT ids[] = { 0, 1, WO_BADID, 0xFFFE, 0xFFFFFFFE };
    for (unsigned i = 0; i < sizeof ids / sizeof ids[0]; i++)
        EXPECT_EQ(wo_open(NULL, ids[i], &wf, 0, 0, WAVE_MAPPED), MMSYSERR_INVALPARAM);
}

/* 0xFFFF is the mapper only as the exact value; neighbouring ids are ordinary device ids (0x7E10 mask). */
TEST(wo_open_ffff_alias_exact_value_only) {
    REQUIRE(U(waveOutOpen));
    WAVEFORMATEX wf; wo_std(&wf);
    EXPECT_EQ(wo_open(NULL, 0xFFFF, &wf, 0, 0, WAVE_MAPPED), MMSYSERR_INVALFLAG);
    EXPECT_EQ(wo_open(NULL, 0xFFFE, &wf, 0, 0, WAVE_MAPPED), MMSYSERR_INVALPARAM);
    EXPECT_EQ(wo_open(NULL, 0x1FFFF, &wf, 0, 0, WAVE_MAPPED), MMSYSERR_INVALPARAM);
    EXPECT_EQ(wo_open(NULL, 0xFFFFFFFE, &wf, 0, 0, WAVE_MAPPED), MMSYSERR_INVALPARAM);
    EXPECT_EQ(wo_open(NULL, 0xFFFF, &wf, 0, 0, WAVE_MAPPED_DEFAULT_COMMUNICATION_DEVICE), MMSYSERR_INVALPARAM);
    EXPECT_EQ(wo_open(NULL, 0xFFFE, &wf, 0, 0, WAVE_MAPPED_DEFAULT_COMMUNICATION_DEVICE), MMSYSERR_INVALFLAG);
    EXPECT_EQ(wo_open(NULL, 0x1FFFF, &wf, 0, 0, WAVE_MAPPED_DEFAULT_COMMUNICATION_DEVICE), MMSYSERR_INVALFLAG);
}

TEST(wo_open_high_word_not_masked) {
    REQUIRE(U(waveOutOpen));
    WAVEFORMATEX wf; wo_std(&wf);
    /* Bits above 0xFFFF are not part of either mask (callback 0 so the window gate is off). */
    for (int b = 16; b < 32; b++) {
        DWORD bit = 1u << b;
        EXPECT_EQ(wo_open(NULL, 0, &wf, 0, 0, bit), MMSYSERR_INVALPARAM);
        EXPECT_EQ(wo_open(NULL, WAVE_MAPPER, &wf, 0, 0, bit), MMSYSERR_INVALPARAM);
        EXPECT_EQ(wo_open(NULL, 0, &wf, 0, 0, bit | 0x200), MMSYSERR_INVALFLAG);
    }
}

/* ---- step 5: phwo NULL ---- */

TEST(wo_open_null_phwo_every_id) {
    REQUIRE(U(waveOutOpen));
    WAVEFORMATEX wf; wo_std(&wf);
    static const DWORD fls[] = { 0, WAVE_ALLOWSYNC, WAVE_FORMAT_DIRECT, WAVE_ALLOWSYNC | WAVE_FORMAT_DIRECT, CALLBACK_EVENT,
                                 CALLBACK_FUNCTION, CALLBACK_THREAD, CALLBACK_WINDOW, 0x00E0, 0x8000 };
    for (unsigned i = 0; i < NIDS; i++)
        for (unsigned f = 0; f < sizeof fls / sizeof fls[0]; f++) {
            MMRESULT r = wo_open(NULL, g_ids[i], &wf, 0, 0, fls[f]);
            CHECKF(r == MMSYSERR_INVALPARAM, "id 0x%x fl 0x%lx -> %u", g_ids[i], fls[f], r);
        }
}

/* With WAVE_FORMAT_QUERY phwo is never read: an unknown id reaches the device check and gives 2. */
TEST(wo_open_query_null_phwo_badid) {
    REQUIRE(U(waveOutOpen));
    WAVEFORMATEX wf; wo_std(&wf);
    static const UINT ids[] = { WO_BADID, 0xFFFE, 0x10000, 0x7FFFFFFF, 0xFFFFFFFE };
    for (unsigned i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        EXPECT_EQ(wo_open(NULL, ids[i], &wf, 0, 0, WAVE_FORMAT_QUERY), MMSYSERR_BADDEVICEID);
        EXPECT_EQ(wo_open(NULL, ids[i], &wf, 0, 0, WAVE_FORMAT_DIRECT_QUERY), MMSYSERR_BADDEVICEID);
        EXPECT_EQ(wo_open(NULL, ids[i], &wf, 0, 0, WAVE_FORMAT_QUERY | WAVE_ALLOWSYNC), MMSYSERR_BADDEVICEID);
    }
}

TEST(wo_open_query_does_not_write_phwo) {
    REQUIRE(U(waveOutOpen));
    WAVEFORMATEX wf; wo_std(&wf);
    static const UINT ids[] = { 0, WAVE_MAPPER, 0xFFFF, WO_BADID, 0xFFFFFFFE };
    for (unsigned i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        HWAVEOUT h = WO_SENTINEL;
        MMRESULT r = wo_open(&h, ids[i], &wf, 0, 0, WAVE_FORMAT_QUERY);
        HWAVEOUT after = h;
        CHECKF(after == WO_SENTINEL, "id 0x%x: QUERY (r=%u) wrote *phwo = %p", ids[i], r, after);
    }
}

/* ---- *phwo = NULL happens before the device check ---- */

TEST(wo_open_failed_open_nulls_handle) {
    REQUIRE(U(waveOutOpen));
    WAVEFORMATEX wf; wo_std(&wf);
    static const UINT ids[] = { WO_BADID, 0xFFFE, 0x10000, 0x7FFFFFFF, 0xFFFFFFFE };
    static const DWORD fls[] = { 0, WAVE_ALLOWSYNC, WAVE_FORMAT_DIRECT, CALLBACK_EVENT, CALLBACK_FUNCTION };
    for (unsigned i = 0; i < sizeof ids / sizeof ids[0]; i++)
        for (unsigned f = 0; f < sizeof fls / sizeof fls[0]; f++) {
            HWAVEOUT h = WO_SENTINEL;
            MMRESULT r = wo_open(&h, ids[i], &wf, 0, 0, fls[f]);
            CHECKF(r == MMSYSERR_BADDEVICEID, "id 0x%x fl 0x%lx -> %u", ids[i], fls[f], r);
            HWAVEOUT after = h;
            CHECKF(after == NULL, "id 0x%x fl 0x%lx: *phwo %p after failed open", ids[i], fls[f], after);
        }
}

TEST(wo_open_failed_open_nulls_handle_any_sentinel) {
    REQUIRE(U(waveOutOpen));
    WAVEFORMATEX wf; wo_std(&wf);
    static const ULONG_PTR sents[] = { 1, 0xFFFF, 0x10000, 0xFFFFFFFF, (ULONG_PTR)-1, 0x123456789ABCull };
    for (unsigned s = 0; s < sizeof sents / sizeof sents[0]; s++) {
        HWAVEOUT h = (HWAVEOUT)sents[s];
        EXPECT_EQ(wo_open(&h, WO_BADID, &wf, 0, 0, 0), MMSYSERR_BADDEVICEID);
        HWAVEOUT after = h; EXPECT_EQ(after, NULL);
    }
}

/* Gate order on one call: each row has several defects; the earliest one in the ladder names the code. */
TEST(wo_open_gate_order_table) {
    REQUIRE(U(waveOutOpen));
    WAVEFORMATEX wf; wo_std(&wf);
    struct row { int nullfmt; DWORD_PTR cb; DWORD fl; int nullph; UINT id; UINT want; } rows[] = {
        { 1, WO_NOWND, CALLBACK_WINDOW | 0x200, 1, WO_BADID, 11 },
        { 0, WO_NOWND, CALLBACK_WINDOW | 0x200, 1, WO_BADID, 11 },
        { 0, 0,        CALLBACK_WINDOW | 0x200, 1, WO_BADID, 10 },
        { 0, 0,        0x200,                   1, WO_BADID, 10 },
        { 0, 0,        0,                       1, WO_BADID, 11 },
        { 0, 0,        WAVE_FORMAT_QUERY,       1, WO_BADID, 2 },
        { 0, 0,        0,                       0, WO_BADID, 2 },
        { 1, 0,        WAVE_MAPPED,             1, WAVE_MAPPER, 11 },
        { 0, 0,        WAVE_MAPPED,             1, WAVE_MAPPER, 10 },
        { 0, 0,        WAVE_MAPPED,             1, 0xFFFF, 10 },
        { 0, 0,        WAVE_MAPPED_DEFAULT_COMMUNICATION_DEVICE, 1, 0, 10 },
        { 0, 0,        WAVE_MAPPED_DEFAULT_COMMUNICATION_DEVICE, 1, WAVE_MAPPER, 11 },
        { 0, WO_NOWND, CALLBACK_FUNCTION | 0x400, 1, 0, 10 },
        { 0, WO_NOWND, CALLBACK_EVENT,          1, 0, 11 },
        { 0, WO_NOWND, CALLBACK_WINDOW | WAVE_FORMAT_QUERY, 1, WO_BADID, 11 },
        { 0, WO_NOWND, CALLBACK_THREAD | WAVE_FORMAT_QUERY, 1, WO_BADID, 2 },
        { 0, WO_NOWND, CALLBACK_EVENT | WAVE_FORMAT_QUERY,  1, 0xFFFFFFFE, 2 },
    };
    for (unsigned i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        struct row *w = &rows[i];
        HWAVEOUT h = WO_SENTINEL;
        UINT model = wo_expect_nodriver(w->id, w->nullfmt, w->cb, w->fl, w->nullph, 0);
        CHECKF(model == w->want, "row %u: model %u table %u", i, model, w->want);
        MMRESULT r = wo_open(w->nullph ? NULL : &h, w->id, w->nullfmt ? NULL : &wf, w->cb, 0, w->fl);
        CHECKF(r == w->want, "row %u -> %u want %u", i, r, w->want);
        wo_release(r, w->fl, h);
    }
}

/* Generated cartesian sweep: every combination whose outcome the model decides without the driver. */
TEST(wo_open_ladder_cartesian_sweep) {
    REQUIRE(U(waveOutOpen));
    WAVEFORMATEX wf; wo_std(&wf);
    static const DWORD_PTR cbs[] = { 0, WO_NOWND };
    static const DWORD types[] = { CALLBACK_NULL, CALLBACK_WINDOW, CALLBACK_THREAD, CALLBACK_FUNCTION, CALLBACK_EVENT };
    static const DWORD fl_low[] = { 0, WAVE_FORMAT_QUERY, WAVE_ALLOWSYNC, WAVE_MAPPED, WAVE_FORMAT_DIRECT,
                                    WAVE_MAPPED_DEFAULT_COMMUNICATION_DEVICE, 0x20, 0x200, 0x4000, 0x8000,
                                    WAVE_FORMAT_QUERY | WAVE_MAPPED, WAVE_FORMAT_QUERY | 0x10, WAVE_FORMAT_QUERY | 0x400 };
    unsigned run = 0, skipped = 0;
    for (unsigned i = 0; i < NIDS; i++)
      for (int nf = 0; nf < 2; nf++)
        for (unsigned c = 0; c < 2; c++)
          for (unsigned t = 0; t < 5; t++)
            for (unsigned f = 0; f < sizeof fl_low / sizeof fl_low[0]; f++)
              for (int np = 0; np < 2; np++) {
                DWORD fl = types[t] | fl_low[f];
                UINT want = wo_expect_nodriver(g_ids[i], nf, cbs[c], fl, np, 0);
                if (want == WO_UNSET) { skipped++; continue; }
                /* never hand a non-function value to a call that could reach the driver as CALLBACK_FUNCTION */
                if (types[t] == CALLBACK_FUNCTION && cbs[c] && wo_gate(g_ids[i], nf, cbs[c], fl, np, 0) == WO_UNSET) { skipped++; continue; }
                HWAVEOUT h = WO_SENTINEL;
                MMRESULT r = wo_open(np ? NULL : &h, g_ids[i], nf ? NULL : &wf, cbs[c], 0, fl);
                run++;
                CHECKF(r == want, "id 0x%x nullfmt %d cb 0x%llx fl 0x%lx nullph %d -> %u want %u",
                       g_ids[i], nf, (unsigned long long)cbs[c], fl, np, r, want);
                if (!np) {
                    HWAVEOUT after = h;
                    int gate = wo_gate(g_ids[i], nf, cbs[c], fl, np, 0) != WO_UNSET;
                    if (gate || (fl & WAVE_FORMAT_QUERY)) CHECKF(after == WO_SENTINEL, "row wrote *phwo %p", after);
                    else CHECKF(after == NULL, "row left *phwo %p", after);
                }
                wo_release(r, fl, h);
              }
    t_note("cartesian sweep: %u calls checked, %u reach the driver (skipped here)", run, skipped);
    CHECK(run > 1000);
}

/* A format pointer to a non-PCM tag with cbSize != 0 passes the gates exactly like PCM. */
TEST(wo_open_nonpcm_format_passes_gates) {
    REQUIRE(U(waveOutOpen));
    struct { WAVEFORMATEX f; BYTE extra[64]; } big;
    memset(&big, 0, sizeof big);
    big.f.wFormatTag = 0x1234; big.f.nChannels = 2; big.f.nSamplesPerSec = 44100; big.f.wBitsPerSample = 16;
    big.f.nBlockAlign = 4; big.f.nAvgBytesPerSec = 176400; big.f.cbSize = 64;
    EXPECT_EQ(wo_open(NULL, 0, &big.f, 0, 0, 0), MMSYSERR_INVALPARAM);
    EXPECT_EQ(wo_open(NULL, 0, &big.f, 0, 0, 0x200), MMSYSERR_INVALFLAG);
    EXPECT_EQ(wo_open(NULL, WO_BADID, &big.f, 0, 0, WAVE_FORMAT_QUERY), MMSYSERR_BADDEVICEID);
    HWAVEOUT h = WO_SENTINEL;
    EXPECT_EQ(wo_open(&h, WO_BADID, &big.f, 0, 0, 0), MMSYSERR_BADDEVICEID);
    HWAVEOUT after = h; EXPECT_EQ(after, NULL);
}

/* Formats are not validated by the gates: garbage field values still give the gate codes. */
TEST(wo_open_format_fields_not_gated) {
    REQUIRE(U(waveOutOpen));
    WAVEFORMATEX fs[6];
    wo_pcm(&fs[0], 0, 44100, 16);
    wo_pcm(&fs[1], 2, 0, 16);
    wo_pcm(&fs[2], 2, 44100, 0);
    wo_pcm(&fs[3], 2, 44100, 16); fs[3].wFormatTag = 0;
    wo_pcm(&fs[4], 2, 44100, 16); fs[4].nBlockAlign = 0; fs[4].nAvgBytesPerSec = 0;
    wo_pcm(&fs[5], 64, 1000000, 64);
    for (int i = 0; i < 6; i++) {
        EXPECT_EQ(wo_open(NULL, 0, &fs[i], 0, 0, 0), MMSYSERR_INVALPARAM);
        EXPECT_EQ(wo_open(NULL, WAVE_MAPPER, &fs[i], 0, 0, WAVE_MAPPED), MMSYSERR_INVALFLAG);
        EXPECT_EQ(wo_open(NULL, WO_BADID, &fs[i], 0, 0, WAVE_FORMAT_QUERY), MMSYSERR_BADDEVICEID);
        HWAVEOUT h = WO_SENTINEL;
        EXPECT_EQ(wo_open(&h, WO_BADID, &fs[i], 0, 0, 0), MMSYSERR_BADDEVICEID);
        HWAVEOUT after = h; EXPECT_EQ(after, NULL);
    }
}

/* The same gate answers are given repeatedly (no hidden state between failed calls). */
TEST(wo_open_gates_are_stateless) {
    REQUIRE(U(waveOutOpen));
    WAVEFORMATEX wf; wo_std(&wf);
    for (int k = 0; k < 200; k++) {
        EXPECT_EQ(wo_open(NULL, WAVE_MAPPER, NULL, 0, 0, 0), MMSYSERR_INVALPARAM);
        EXPECT_EQ(wo_open(NULL, WAVE_MAPPER, &wf, 0, 0, WAVE_MAPPED), MMSYSERR_INVALFLAG);
        EXPECT_EQ(wo_open(NULL, 0, &wf, 0, 0, 0), MMSYSERR_INVALPARAM);
        EXPECT_EQ(wo_open(NULL, WO_BADID, &wf, 0, 0, WAVE_FORMAT_QUERY), MMSYSERR_BADDEVICEID);
    }
}

/* winmm.dll exports the same entry point that winmmbase implements. */
TEST(wo_open_winmm_and_winmmbase_agree) {
    REQUIRE(U(waveOutOpen));
    __typeof__(&waveOutOpen) a = UIN("winmm.dll", waveOutOpen), b = UIN("winmmbase.dll", waveOutOpen);
    REQUIRE(a); REQUIRE(b);
    WAVEFORMATEX wf; wo_std(&wf);
    EXPECT_EQ(a(NULL, 0, NULL, 0, 0, 0), b(NULL, 0, NULL, 0, 0, 0));
    EXPECT_EQ(a(NULL, 0, &wf, 0, 0, 0x200), b(NULL, 0, &wf, 0, 0, 0x200));
    EXPECT_EQ(a(NULL, WO_BADID, &wf, 0, 0, WAVE_FORMAT_QUERY), b(NULL, WO_BADID, &wf, 0, 0, WAVE_FORMAT_QUERY));
    EXPECT_EQ(a(NULL, 0, &wf, 0, 0, 0), MMSYSERR_INVALPARAM);
    __typeof__(&waveOutClose) ca = UIN("winmm.dll", waveOutClose), cb = UIN("winmmbase.dll", waveOutClose);
    __typeof__(&waveOutGetID) ga = UIN("winmm.dll", waveOutGetID), gb = UIN("winmmbase.dll", waveOutGetID);
    REQUIRE(ca); REQUIRE(cb); REQUIRE(ga); REQUIRE(gb);
    EXPECT_EQ(ca(NULL), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(cb(NULL), MMSYSERR_INVALHANDLE);
    UINT id = 7;
    EXPECT_EQ(ga(NULL, NULL), MMSYSERR_INVALPARAM);
    EXPECT_EQ(gb(NULL, &id), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(id, 7);
}
