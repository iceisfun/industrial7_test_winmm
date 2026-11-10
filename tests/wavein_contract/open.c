/* waveInOpen (winmmbase!0x11290) argument ladder.
 *
 * Order (decomp): pwfx NULL -> 11; CALLBACK_WINDOW with a non-NULL non-window -> 11; id 0xFFFF -> WAVE_MAPPER;
 * flags & (mapper ? 0x7E04 : 0x7E10) -> 10; (pointer wrap, unreachable); not QUERY: phwi NULL -> 11 else
 * *phwi = NULL; zero waveIn devices -> 2; driver resolve failure (bad id) -> 2; then WIDM_OPEN.
 * Every return before the "*phwi = NULL" step leaves *phwi untouched; WAVE_FORMAT_QUERY never touches it.
 */
#include "wic.h"

static WAVEFORMATEX g_f;
static WAVEFORMATEX *pcm(void) { wic_pcm(&g_f, 44100, 16, 1); return &g_f; }

/* A window handle value that is certainly not a window (destroyed window), or 0 if none could be made. */
static HWND dead_window(void) {
    static HWND dead;
    if (dead) return dead;
    HWND w = CreateWindowExA(0, "STATIC", "wic", WS_POPUP, 0, 0, 1, 1, NULL, NULL, GetModuleHandleA(NULL), NULL);
    if (!w) return NULL;
    DestroyWindow(w);
    if (IsWindow(w)) return NULL;
    dead = w;
    return dead;
}

/* ---------------------------------------------------------------- pwfx NULL -> 11, first */

static const UINT ids_all[] = { WAVE_MAPPER, 0xFFFF, 0, 1, 9999, 0xFFFFFFFE };
static const DWORD flags_mix[] = { 0, WAVE_FORMAT_QUERY, WAVE_MAPPED, WAVE_MAPPED_DEFAULT_COMMUNICATION_DEVICE,
                                   0x200, 0x4000, 0x7E14, CALLBACK_EVENT, CALLBACK_FUNCTION | WAVE_FORMAT_QUERY };

TEST(wavein_contract_open_pwfx_null_any_id_flags) {
    REQUIRE(U(waveInOpen));
    for (unsigned i = 0; i < sizeof(ids_all) / sizeof(ids_all[0]); i++)
        for (unsigned j = 0; j < sizeof(flags_mix) / sizeof(flags_mix[0]); j++) {
            HWAVEIN h = WIC_SENT_HANDLE;
            MMRESULT r = U(waveInOpen)(&h, ids_all[i], NULL, 0, 0, flags_mix[j]);
            CHECKF(r == MMSYSERR_INVALPARAM, "id=0x%x flags=0x%lx r=%u", ids_all[i], flags_mix[j], r);
            CHECKF(h == WIC_SENT_HANDLE, "id=0x%x flags=0x%lx: *phwi touched", ids_all[i], flags_mix[j]);
        }
}

TEST(wavein_contract_open_pwfx_null_phwi_null) {
    REQUIRE(U(waveInOpen));
    EXPECT_EQ(U(waveInOpen)(NULL, WAVE_MAPPER, NULL, 0, 0, 0), MMSYSERR_INVALPARAM);
    EXPECT_EQ(U(waveInOpen)(NULL, 0, NULL, 0, 0, CALLBACK_NULL), MMSYSERR_INVALPARAM);
    EXPECT_EQ(U(waveInOpen)(NULL, 9999, NULL, 0, 0, WAVE_FORMAT_QUERY), MMSYSERR_INVALPARAM);
}

TEST(wavein_contract_open_pwfx_null_beats_bad_window) {
    REQUIRE(U(waveInOpen));
    HWND dw = dead_window();
    if (!dw) { t_skip("no dead window"); return; }
    HWAVEIN h = WIC_SENT_HANDLE;
    EXPECT_EQ(U(waveInOpen)(&h, WAVE_MAPPER, NULL, (DWORD_PTR)dw, 0, CALLBACK_WINDOW), MMSYSERR_INVALPARAM);
    CHECK(h == WIC_SENT_HANDLE);
}

/* ---------------------------------------------------------------- CALLBACK_WINDOW validation */

TEST(wavein_contract_open_bad_window_11) {
    REQUIRE(U(waveInOpen));
    HWND dw = dead_window();
    if (!dw) { t_skip("no dead window"); return; }
    for (unsigned i = 0; i < sizeof(ids_all) / sizeof(ids_all[0]); i++) {
        HWAVEIN h = WIC_SENT_HANDLE;
        MMRESULT r = U(waveInOpen)(&h, ids_all[i], pcm(), (DWORD_PTR)dw, 0, CALLBACK_WINDOW);
        CHECKF(r == MMSYSERR_INVALPARAM, "id=0x%x r=%u", ids_all[i], r);
        CHECKF(h == WIC_SENT_HANDLE, "id=0x%x *phwi touched", ids_all[i]);
    }
}

TEST(wavein_contract_open_bad_window_with_query_11) {
    REQUIRE(U(waveInOpen));
    HWND dw = dead_window();
    if (!dw) { t_skip("no dead window"); return; }
    EXPECT_EQ(U(waveInOpen)(NULL, WAVE_MAPPER, pcm(), (DWORD_PTR)dw, 0, CALLBACK_WINDOW | WAVE_FORMAT_QUERY), MMSYSERR_INVALPARAM);
    EXPECT_EQ(U(waveInOpen)(NULL, 0, pcm(), (DWORD_PTR)dw, 0, CALLBACK_WINDOW | WAVE_FORMAT_QUERY), MMSYSERR_INVALPARAM);
}

TEST(wavein_contract_open_bad_window_beats_bad_flags) {
    REQUIRE(U(waveInOpen));
    HWND dw = dead_window();
    if (!dw) { t_skip("no dead window"); return; }
    HWAVEIN h = WIC_SENT_HANDLE;
    EXPECT_EQ(U(waveInOpen)(&h, WAVE_MAPPER, pcm(), (DWORD_PTR)dw, 0, CALLBACK_WINDOW | WAVE_MAPPED), MMSYSERR_INVALPARAM);
    EXPECT_EQ(U(waveInOpen)(&h, 0, pcm(), (DWORD_PTR)dw, 0, CALLBACK_WINDOW | 0x4000), MMSYSERR_INVALPARAM);
    CHECK(h == WIC_SENT_HANDLE);
}

TEST(wavein_contract_open_bad_window_beats_phwi_null) {
    REQUIRE(U(waveInOpen));
    HWND dw = dead_window();
    if (!dw) { t_skip("no dead window"); return; }
    EXPECT_EQ(U(waveInOpen)(NULL, WAVE_MAPPER, pcm(), (DWORD_PTR)dw, 0, CALLBACK_WINDOW), MMSYSERR_INVALPARAM);
}

/* A NULL window with CALLBACK_WINDOW means "no callback": validation passes. */
TEST(wavein_contract_open_null_window_passes_validation) {
    REQUIRE(U(waveInOpen));
    MMRESULT r = U(waveInOpen)(NULL, WAVE_MAPPER, pcm(), 0, 0, CALLBACK_WINDOW | WAVE_FORMAT_QUERY);
    if (!wic_nin()) EXPECT_EQ(r, MMSYSERR_BADDEVICEID);
    else EXPECT_EQ(r, MMSYSERR_NOERROR);
}

TEST(wavein_contract_open_real_window_passes_validation) {
    REQUIRE(U(waveInOpen));
    HWND w = GetDesktopWindow();
    CHECK(IsWindow(w));
    MMRESULT r = U(waveInOpen)(NULL, WAVE_MAPPER, pcm(), (DWORD_PTR)w, 0, CALLBACK_WINDOW | WAVE_FORMAT_QUERY);
    if (!wic_nin()) EXPECT_EQ(r, MMSYSERR_BADDEVICEID);
    else EXPECT_EQ(r, MMSYSERR_NOERROR);
}

/* Only the WINDOW type is validated at this layer: other callback kinds with a value that is not a
 * window pass (queried, so no handle is created and nothing is ever called). */
TEST(wavein_contract_open_non_window_callback_types_not_validated) {
    REQUIRE(U(waveInOpen));
    HWND dw = dead_window();
    if (!dw) { t_skip("no dead window"); return; }
    static const DWORD types[] = { CALLBACK_NULL, CALLBACK_THREAD, CALLBACK_FUNCTION, CALLBACK_EVENT, 0x40000, 0x60000, 0x70000 };
    for (unsigned i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
        MMRESULT r = U(waveInOpen)(NULL, WAVE_MAPPER, pcm(), (DWORD_PTR)dw, 0, types[i] | WAVE_FORMAT_QUERY);
        if (!wic_nin()) CHECKF(r == MMSYSERR_BADDEVICEID, "type=0x%lx r=%u", types[i], r);
        else CHECKF(r == MMSYSERR_NOERROR, "type=0x%lx r=%u", types[i], r);
    }
}

/* CALLBACK_WINDOW is (fdwOpen >> 16 & 7) == 1: 0x90000 has the same low three type bits. */
TEST(wavein_contract_open_window_type_uses_low_three_bits) {
    REQUIRE(U(waveInOpen));
    HWND dw = dead_window();
    if (!dw) { t_skip("no dead window"); return; }
    EXPECT_EQ(U(waveInOpen)(NULL, WAVE_MAPPER, pcm(), (DWORD_PTR)dw, 0, 0x90000 | WAVE_FORMAT_QUERY), MMSYSERR_INVALPARAM);
    EXPECT_EQ(U(waveInOpen)(NULL, WAVE_MAPPER, pcm(), (DWORD_PTR)dw, 0, 0xF90000 | WAVE_FORMAT_QUERY), MMSYSERR_INVALPARAM);
}

/* ---------------------------------------------------------------- flag masks */

/* Expected result of a QUERY with phwi NULL and one extra flag bit, before the device check. */
static int bit_invalid(UINT id, DWORD bit) {
    int mapper = (id == WAVE_MAPPER || id == 0xFFFF);
    return (bit & (mapper ? 0x7E04u : 0x7E10u)) != 0;
}

static void flag_bit_query(UINT id) {
    for (int b = 1; b < 16; b++) {
        DWORD bit = 1u << b;
        MMRESULT r = U(waveInOpen)(NULL, id, pcm(), 0, 0, WAVE_FORMAT_QUERY | bit);
        if (bit_invalid(id, bit)) CHECKF(r == MMSYSERR_INVALFLAG, "id=0x%x bit=0x%lx r=%u", id, bit, r);
        else if (!wic_nin()) CHECKF(r == MMSYSERR_BADDEVICEID, "id=0x%x bit=0x%lx r=%u", id, bit, r);
        else CHECKF(r == MMSYSERR_NOERROR, "id=0x%x bit=0x%lx r=%u", id, bit, r);   /* [VM] every pass bit is accepted */
    }
}

TEST(wavein_contract_open_flagbits_query_mapper) { REQUIRE(U(waveInOpen)); flag_bit_query(WAVE_MAPPER); }
TEST(wavein_contract_open_flagbits_query_ffff) { REQUIRE(U(waveInOpen)); flag_bit_query(0xFFFF); }
TEST(wavein_contract_open_flagbits_query_id0) { REQUIRE(U(waveInOpen)); flag_bit_query(0); }
TEST(wavein_contract_open_flagbits_query_id9999) {
    REQUIRE(U(waveInOpen));
    /* bad id: invalid bits -> 10, others -> 2 regardless of device count */
    for (int b = 1; b < 16; b++) {
        DWORD bit = 1u << b;
        MMRESULT r = U(waveInOpen)(NULL, 9999, pcm(), 0, 0, WAVE_FORMAT_QUERY | bit);
        if (bit_invalid(9999, bit)) CHECKF(r == MMSYSERR_INVALFLAG, "bit=0x%lx r=%u", bit, r);
        else CHECKF(r == MMSYSERR_BADDEVICEID, "bit=0x%lx r=%u", bit, r);  /* WAVE_MAPPED too: the mapper rejects id 9999 [VM] */
    }
}

/* Each always-invalid bit (0x200..0x4000) rejected for mapper and ids; *phwi left untouched. */
static const DWORD bad_bits[] = { 0x200, 0x400, 0x800, 0x1000, 0x2000, 0x4000, 0x7E00 };
TEST(wavein_contract_open_invalid_bits_leave_phwi) {
    REQUIRE(U(waveInOpen));
    for (unsigned i = 0; i < sizeof(ids_all) / sizeof(ids_all[0]); i++)
        for (unsigned j = 0; j < sizeof(bad_bits) / sizeof(bad_bits[0]); j++) {
            HWAVEIN h = WIC_SENT_HANDLE;
            MMRESULT r = U(waveInOpen)(&h, ids_all[i], pcm(), 0, 0, bad_bits[j]);
            CHECKF(r == MMSYSERR_INVALFLAG, "id=0x%x flags=0x%lx r=%u", ids_all[i], bad_bits[j], r);
            CHECKF(h == WIC_SENT_HANDLE, "id=0x%x flags=0x%lx *phwi touched", ids_all[i], bad_bits[j]);
        }
}

TEST(wavein_contract_open_mapped_with_mapper_10) {
    REQUIRE(U(waveInOpen));
    HWAVEIN h = WIC_SENT_HANDLE;
    EXPECT_EQ(U(waveInOpen)(&h, WAVE_MAPPER, pcm(), 0, 0, WAVE_MAPPED), MMSYSERR_INVALFLAG);
    CHECK(h == WIC_SENT_HANDLE);
    EXPECT_EQ(U(waveInOpen)(&h, WAVE_MAPPER, pcm(), 0, 0, WAVE_MAPPED | WAVE_FORMAT_QUERY), MMSYSERR_INVALFLAG);
    CHECK(h == WIC_SENT_HANDLE);
}

/* 0xFFFF is rewritten to WAVE_MAPPER before the mask is chosen. */
TEST(wavein_contract_open_ffff_is_mapper_for_mask) {
    REQUIRE(U(waveInOpen));
    HWAVEIN h = WIC_SENT_HANDLE;
    EXPECT_EQ(U(waveInOpen)(&h, 0xFFFF, pcm(), 0, 0, WAVE_MAPPED), MMSYSERR_INVALFLAG);
    CHECK(h == WIC_SENT_HANDLE);
    MMRESULT r = U(waveInOpen)(NULL, 0xFFFF, pcm(), 0, 0, WAVE_MAPPED_DEFAULT_COMMUNICATION_DEVICE | WAVE_FORMAT_QUERY);
    CHECKF(r != MMSYSERR_INVALFLAG, "r=%u", r);
    if (!wic_nin()) EXPECT_EQ(r, MMSYSERR_BADDEVICEID);
}

/* 0xFFFE and 0x1FFFF are ordinary ids: the specific-device mask applies. */
TEST(wavein_contract_open_fffe_is_specific_for_mask) {
    REQUIRE(U(waveInOpen));
    EXPECT_EQ(U(waveInOpen)(NULL, 0xFFFE, pcm(), 0, 0, WAVE_MAPPED_DEFAULT_COMMUNICATION_DEVICE | WAVE_FORMAT_QUERY), MMSYSERR_INVALFLAG);
    EXPECT_EQ(U(waveInOpen)(NULL, 0x1FFFF, pcm(), 0, 0, WAVE_MAPPED_DEFAULT_COMMUNICATION_DEVICE | WAVE_FORMAT_QUERY), MMSYSERR_INVALFLAG);
    EXPECT_EQ(U(waveInOpen)(NULL, 0xFFFE, pcm(), 0, 0, WAVE_FORMAT_QUERY), MMSYSERR_BADDEVICEID);
}

TEST(wavein_contract_open_commdefault_with_id_10) {
    REQUIRE(U(waveInOpen));
    HWAVEIN h = WIC_SENT_HANDLE;
    EXPECT_EQ(U(waveInOpen)(&h, 0, pcm(), 0, 0, WAVE_MAPPED_DEFAULT_COMMUNICATION_DEVICE), MMSYSERR_INVALFLAG);
    EXPECT_EQ(U(waveInOpen)(&h, 1, pcm(), 0, 0, WAVE_MAPPED_DEFAULT_COMMUNICATION_DEVICE | WAVE_MAPPED), MMSYSERR_INVALFLAG);
    CHECK(h == WIC_SENT_HANDLE);
}

/* Flag check precedes the phwi NULL check. */
TEST(wavein_contract_open_bad_flags_beat_phwi_null) {
    REQUIRE(U(waveInOpen));
    EXPECT_EQ(U(waveInOpen)(NULL, WAVE_MAPPER, pcm(), 0, 0, WAVE_MAPPED), MMSYSERR_INVALFLAG);
    EXPECT_EQ(U(waveInOpen)(NULL, 0, pcm(), 0, 0, 0x200), MMSYSERR_INVALFLAG);
    EXPECT_EQ(U(waveInOpen)(NULL, 9999, pcm(), 0, 0, WAVE_MAPPED_DEFAULT_COMMUNICATION_DEVICE), MMSYSERR_INVALFLAG);
}

/* Flags above 0xFFFF other than the callback type field are not checked at this layer. */
TEST(wavein_contract_open_high_flag_bits_not_checked) {
    REQUIRE(U(waveInOpen));
    static const DWORD hi[] = { 0x80000, 0x100000, 0x01000000, 0x80000000 };
    for (unsigned i = 0; i < sizeof(hi) / sizeof(hi[0]); i++) {
        MMRESULT r = U(waveInOpen)(NULL, WAVE_MAPPER, pcm(), 0, 0, hi[i] | WAVE_FORMAT_QUERY);
        if (!wic_nin()) CHECKF(r == MMSYSERR_BADDEVICEID, "flags=0x%lx r=%u", hi[i], r);
        else CHECKF(r != MMSYSERR_INVALFLAG, "flags=0x%lx r=%u", hi[i], r);
    }
}

/* ---------------------------------------------------------------- phwi */

TEST(wavein_contract_open_phwi_null_11) {
    REQUIRE(U(waveInOpen));
    for (unsigned i = 0; i < sizeof(ids_all) / sizeof(ids_all[0]); i++) {
        MMRESULT r = U(waveInOpen)(NULL, ids_all[i], pcm(), 0, 0, CALLBACK_NULL);
        CHECKF(r == MMSYSERR_INVALPARAM, "id=0x%x r=%u", ids_all[i], r);
    }
}

TEST(wavein_contract_open_phwi_null_with_query_ok) {
    REQUIRE(U(waveInOpen));
    MMRESULT r = U(waveInOpen)(NULL, WAVE_MAPPER, pcm(), 0, 0, WAVE_FORMAT_QUERY);
    if (!wic_nin()) EXPECT_EQ(r, MMSYSERR_BADDEVICEID); else EXPECT_EQ(r, MMSYSERR_NOERROR);
    r = U(waveInOpen)(NULL, 9999, pcm(), 0, 0, WAVE_FORMAT_QUERY);
    EXPECT_EQ(r, MMSYSERR_BADDEVICEID);
}

/* *phwi is zeroed before the device check: a failing open with a valid phwi leaves NULL. */
TEST(wavein_contract_open_failure_nulls_phwi) {
    REQUIRE(U(waveInOpen));
    HWAVEIN h = WIC_SENT_HANDLE;
    EXPECT_EQ(U(waveInOpen)(&h, 9999, pcm(), 0, 0, CALLBACK_NULL), MMSYSERR_BADDEVICEID);
    CHECK(h == NULL);
    h = WIC_SENT_HANDLE;
    EXPECT_EQ(U(waveInOpen)(&h, 0xFFFFFFFE, pcm(), 0, 0, CALLBACK_NULL), MMSYSERR_BADDEVICEID);
    CHECK(h == NULL);
    h = WIC_SENT_HANDLE;
    EXPECT_EQ(U(waveInOpen)(&h, wic_nin(), pcm(), 0, 0, CALLBACK_NULL), MMSYSERR_BADDEVICEID);
    CHECK(h == NULL);
}

/* WAVE_FORMAT_QUERY never writes *phwi, success or failure. */
TEST(wavein_contract_open_query_leaves_phwi) {
    REQUIRE(U(waveInOpen));
    static const UINT ids[] = { WAVE_MAPPER, 0xFFFF, 0, 9999 };
    for (unsigned i = 0; i < 4; i++) {
        HWAVEIN h = WIC_SENT_HANDLE;
        MMRESULT r = U(waveInOpen)(&h, ids[i], pcm(), 0, 0, WAVE_FORMAT_QUERY);
        CHECKF(h == WIC_SENT_HANDLE, "id=0x%x r=%u *phwi touched", ids[i], r);
        if (!wic_nin() || ids[i] == 9999) CHECKF(r == MMSYSERR_BADDEVICEID, "id=0x%x r=%u", ids[i], r);
        else CHECKF(r == MMSYSERR_NOERROR, "id=0x%x r=%u", ids[i], r);
    }
}

/* ---------------------------------------------------------------- zero devices */

TEST(wavein_contract_open_zero_devices_every_id_2) {
    REQUIRE(U(waveInOpen));
    if (wic_nin()) { t_skip("waveIn devices present (%u)", wic_nin()); return; }
    for (unsigned i = 0; i < sizeof(ids_all) / sizeof(ids_all[0]); i++) {
        HWAVEIN h = WIC_SENT_HANDLE;
        MMRESULT r = U(waveInOpen)(&h, ids_all[i], pcm(), 0, 0, CALLBACK_NULL);
        CHECKF(r == MMSYSERR_BADDEVICEID, "id=0x%x r=%u", ids_all[i], r);
        CHECKF(h == NULL, "id=0x%x *phwi not NULLed", ids_all[i]);
        r = U(waveInOpen)(NULL, ids_all[i], pcm(), 0, 0, WAVE_FORMAT_QUERY);
        CHECKF(r == MMSYSERR_BADDEVICEID, "query id=0x%x r=%u", ids_all[i], r);
    }
}

/* With no device the format is never looked at. */
TEST(wavein_contract_open_zero_devices_format_ignored) {
    REQUIRE(U(waveInOpen));
    if (wic_nin()) { t_skip("waveIn devices present"); return; }
    WAVEFORMATEX f; memset(&f, 0, sizeof(f));
    f.wFormatTag = 0x1234;
    EXPECT_EQ(U(waveInOpen)(NULL, WAVE_MAPPER, &f, 0, 0, WAVE_FORMAT_QUERY), MMSYSERR_BADDEVICEID);
    HWAVEIN h = WIC_SENT_HANDLE;
    EXPECT_EQ(U(waveInOpen)(&h, 0, &f, 0, 0, 0), MMSYSERR_BADDEVICEID);
    CHECK(h == NULL);
}

TEST(wavein_contract_open_zero_devices_callback_kinds_2) {
    REQUIRE(U(waveInOpen));
    if (wic_nin()) { t_skip("waveIn devices present"); return; }
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    HWAVEIN h = WIC_SENT_HANDLE;
    EXPECT_EQ(U(waveInOpen)(&h, WAVE_MAPPER, pcm(), (DWORD_PTR)ev, 0, CALLBACK_EVENT), MMSYSERR_BADDEVICEID);
    CHECK(h == NULL);
    CHECK(WaitForSingleObject(ev, 0) == WAIT_TIMEOUT);
    h = WIC_SENT_HANDLE;
    EXPECT_EQ(U(waveInOpen)(&h, WAVE_MAPPER, pcm(), GetCurrentThreadId(), 0, CALLBACK_THREAD), MMSYSERR_BADDEVICEID);
    CHECK(h == NULL);
    MSG m; CHECK(!PeekMessageA(&m, NULL, MM_WIM_OPEN, MM_WIM_DATA, PM_REMOVE));
    CloseHandle(ev);
}

/* ---------------------------------------------------------------- device half */

static void open_close(UINT id, DWORD flags, UINT want_id) {
    HWAVEIN h = WIC_SENT_HANDLE;
    MMRESULT r = U(waveInOpen)(&h, id, pcm(), 0, 0, flags);
    CHECKF(r == MMSYSERR_NOERROR, "id=0x%x flags=0x%lx r=%u", id, flags, r);
    if (r) return;
    CHECK(h != NULL && h != WIC_SENT_HANDLE);
    UINT got = 0x12345678;
    EXPECT_EQ(U(waveInGetID)(h, &got), MMSYSERR_NOERROR);
    CHECKF(got == want_id, "id=0x%x flags=0x%lx GetID=0x%x want 0x%x", id, flags, got, want_id);
    EXPECT_EQ(U(waveInClose)(h), MMSYSERR_NOERROR);
}

TEST(wavein_contract_open_dev_mapper) {
    REQUIRE(U(waveInOpen)); REQUIRE(U(waveInClose)); REQUIRE(U(waveInGetID));
    if (!wic_nin()) { t_skip("no waveIn device"); return; }
    open_close(WAVE_MAPPER, CALLBACK_NULL, WAVE_MAPPER);
}
TEST(wavein_contract_open_dev_ffff_is_mapper) {
    REQUIRE(U(waveInOpen)); REQUIRE(U(waveInClose)); REQUIRE(U(waveInGetID));
    if (!wic_nin()) { t_skip("no waveIn device"); return; }
    open_close(0xFFFF, CALLBACK_NULL, WAVE_MAPPER);
}
TEST(wavein_contract_open_dev_each_id) {
    REQUIRE(U(waveInOpen)); REQUIRE(U(waveInClose)); REQUIRE(U(waveInGetID));
    if (!wic_nin()) { t_skip("no waveIn device"); return; }
    for (UINT i = 0; i < wic_nin() && i < 8; i++) open_close(i, CALLBACK_NULL, i);
}
TEST(wavein_contract_open_dev_mapped_id0) {
    REQUIRE(U(waveInOpen)); REQUIRE(U(waveInClose)); REQUIRE(U(waveInGetID));
    if (!wic_nin()) { t_skip("no waveIn device"); return; }
    open_close(0, WAVE_MAPPED, 0);
}
TEST(wavein_contract_open_dev_mapper_commdefault) {
    REQUIRE(U(waveInOpen)); REQUIRE(U(waveInClose)); REQUIRE(U(waveInGetID));
    if (!wic_nin()) { t_skip("no waveIn device"); return; }
    HWAVEIN h = NULL;
    MMRESULT r = U(waveInOpen)(&h, WAVE_MAPPER, pcm(), 0, 0, WAVE_MAPPED_DEFAULT_COMMUNICATION_DEVICE);
    EXPECT_EQ(r, MMSYSERR_NOERROR);
    if (r == MMSYSERR_NOERROR) {
        UINT id = 7;
        EXPECT_EQ(U(waveInGetID)(h, &id), MMSYSERR_NOERROR);
        EXPECT_EQ(id, WAVE_MAPPER);
        EXPECT_EQ(U(waveInClose)(h), MMSYSERR_NOERROR);
    }
}
TEST(wavein_contract_open_dev_allowsync_direct) {
    REQUIRE(U(waveInOpen)); REQUIRE(U(waveInClose));
    if (!wic_nin()) { t_skip("no waveIn device"); return; }
    static const DWORD fl[] = { WAVE_ALLOWSYNC, WAVE_FORMAT_DIRECT, WAVE_ALLOWSYNC | WAVE_FORMAT_DIRECT };
    for (int i = 0; i < 3; i++) {
        HWAVEIN h = NULL;
        MMRESULT r = U(waveInOpen)(&h, 0, pcm(), 0, 0, fl[i]);
        CHECKF(r == MMSYSERR_NOERROR, "flags=0x%lx r=%u", fl[i], r);
        if (r == MMSYSERR_NOERROR) EXPECT_EQ(U(waveInClose)(h), MMSYSERR_NOERROR);
    }
}

TEST(wavein_contract_open_dev_bad_ids_2) {
    REQUIRE(U(waveInOpen));
    if (!wic_nin()) { t_skip("no waveIn device"); return; }
    UINT bad[] = { wic_nin(), wic_nin() + 1, 9999, 0xFFFE, 0x10000, 0xFFFFFFFE };
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        HWAVEIN h = WIC_SENT_HANDLE;
        CHECKF(U(waveInOpen)(&h, bad[i], pcm(), 0, 0, 0) == MMSYSERR_BADDEVICEID, "id=0x%x", bad[i]);
        CHECKF(h == NULL, "id=0x%x", bad[i]);
        CHECKF(U(waveInOpen)(NULL, bad[i], pcm(), 0, 0, WAVE_FORMAT_QUERY) == MMSYSERR_BADDEVICEID, "query id=0x%x", bad[i]);
    }
}

/* An open handle is not accepted as a device id (unlike GetDevCaps). */
TEST(wavein_contract_open_dev_handle_as_id_2) {
    REQUIRE(U(waveInOpen)); REQUIRE(U(waveInClose));
    HWAVEIN h1 = wic_open_any();
    if (!h1) return;
    HWAVEIN h2 = WIC_SENT_HANDLE;
    EXPECT_EQ(U(waveInOpen)(&h2, (UINT)(ULONG_PTR)h1, pcm(), 0, 0, CALLBACK_NULL), MMSYSERR_BADDEVICEID);
    CHECK(h2 == NULL);
    EXPECT_EQ(U(waveInClose)(h1), MMSYSERR_NOERROR);
}

TEST(wavein_contract_open_dev_two_handles) {
    REQUIRE(U(waveInOpen)); REQUIRE(U(waveInClose));
    if (!wic_nin()) { t_skip("no waveIn device"); return; }
    HWAVEIN a = NULL, b = NULL;
    EXPECT_EQ(wic_open(&a, WAVE_MAPPER), MMSYSERR_NOERROR);
    MMRESULT r = wic_open(&b, WAVE_MAPPER);
    EXPECT_EQ(r, MMSYSERR_NOERROR);   /* [VM] shared-mode capture: two opens coexist */
    if (r == MMSYSERR_NOERROR) { CHECK(a != b); EXPECT_EQ(U(waveInClose)(b), MMSYSERR_NOERROR); }
    if (a) EXPECT_EQ(U(waveInClose)(a), MMSYSERR_NOERROR);
}

TEST(wavein_contract_open_dev_query_formats) {
    REQUIRE(U(waveInOpen));
    if (!wic_nin()) { t_skip("no waveIn device"); return; }
    static const struct { DWORD rate; WORD bits, ch; } ok[] = {
        { 8000, 8, 1 }, { 11025, 8, 1 }, { 22050, 16, 1 }, { 44100, 16, 1 }, { 44100, 16, 2 }, { 48000, 16, 2 },
    };
    WAVEFORMATEX f;
    for (unsigned i = 0; i < sizeof(ok) / sizeof(ok[0]); i++) {
        wic_pcm(&f, ok[i].rate, ok[i].bits, ok[i].ch);
        MMRESULT r = U(waveInOpen)(NULL, WAVE_MAPPER, &f, 0, 0, WAVE_FORMAT_QUERY);
        CHECKF(r == MMSYSERR_NOERROR, "mapper %lu/%u/%u r=%u", ok[i].rate, ok[i].bits, ok[i].ch, r);
        r = U(waveInOpen)(NULL, 0, &f, 0, 0, WAVE_FORMAT_QUERY);
        CHECKF(r == MMSYSERR_NOERROR, "id0 %lu/%u/%u r=%u", ok[i].rate, ok[i].bits, ok[i].ch, r);
    }
}

TEST(wavein_contract_open_dev_query_bad_format) {
    REQUIRE(U(waveInOpen));
    if (!wic_nin()) { t_skip("no waveIn device"); return; }
    WAVEFORMATEX f;
    wic_pcm(&f, 44100, 16, 1); f.wFormatTag = 0x1234;
    MMRESULT r1 = U(waveInOpen)(NULL, WAVE_MAPPER, &f, 0, 0, WAVE_FORMAT_QUERY);
    MMRESULT r2 = U(waveInOpen)(NULL, 0, &f, 0, 0, WAVE_FORMAT_QUERY);
    EXPECT_EQ(r1, WAVERR_BADFORMAT);
    EXPECT_EQ(r2, WAVERR_BADFORMAT);
    wic_pcm(&f, 44100, 16, 6);
    MMRESULT r3 = U(waveInOpen)(NULL, 0, &f, 0, 0, WAVE_FORMAT_QUERY);
    wic_pcm(&f, 44100, 16, 0);
    MMRESULT r4 = U(waveInOpen)(NULL, 0, &f, 0, 0, WAVE_FORMAT_QUERY);
    wic_pcm(&f, 0, 16, 1);
    MMRESULT r5 = U(waveInOpen)(NULL, 0, &f, 0, 0, WAVE_FORMAT_QUERY);
    /* [VM] the inbox HDA capture endpoint: 6 channels, 0 channels and 0 Hz are all 32 */
    EXPECT_EQ(r3, WAVERR_BADFORMAT);
    EXPECT_EQ(r4, WAVERR_BADFORMAT);
    EXPECT_EQ(r5, WAVERR_BADFORMAT);
}

/* A real (non-query) open with an unsupported format fails and NULLs *phwi. */
TEST(wavein_contract_open_dev_bad_format_nulls_phwi) {
    REQUIRE(U(waveInOpen));
    if (!wic_nin()) { t_skip("no waveIn device"); return; }
    WAVEFORMATEX f; wic_pcm(&f, 44100, 16, 1); f.wFormatTag = 0x1234;
    HWAVEIN h = WIC_SENT_HANDLE;
    MMRESULT r = U(waveInOpen)(&h, 0, &f, 0, 0, CALLBACK_NULL);
    EXPECT_EQ(r, WAVERR_BADFORMAT);
    CHECK(h == NULL);
    if (r == MMSYSERR_NOERROR && h && h != WIC_SENT_HANDLE) U(waveInClose)(h);
}

/* CALLBACK_EVENT: open signals the event once (WIM_OPEN), close signals it again (WIM_CLOSE). */
TEST(wavein_contract_open_dev_event_signalled) {
    REQUIRE(U(waveInOpen)); REQUIRE(U(waveInClose));
    if (!wic_nin()) { t_skip("no waveIn device"); return; }
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    HWAVEIN h = NULL;
    EXPECT_EQ(U(waveInOpen)(&h, WAVE_MAPPER, pcm(), (DWORD_PTR)ev, 0, CALLBACK_EVENT), MMSYSERR_NOERROR);
    if (h) {
        CHECK(WaitForSingleObject(ev, 2000) == WAIT_OBJECT_0);
        EXPECT_EQ(U(waveInClose)(h), MMSYSERR_NOERROR);
        CHECK(WaitForSingleObject(ev, 2000) == WAIT_OBJECT_0);
    }
    CloseHandle(ev);
}

/* A query never creates a handle, so no callback fires. */
TEST(wavein_contract_open_dev_query_no_callback) {
    REQUIRE(U(waveInOpen));
    if (!wic_nin()) { t_skip("no waveIn device"); return; }
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    EXPECT_EQ(U(waveInOpen)(NULL, WAVE_MAPPER, pcm(), (DWORD_PTR)ev, 0, CALLBACK_EVENT | WAVE_FORMAT_QUERY), MMSYSERR_NOERROR);
    CHECK(WaitForSingleObject(ev, 300) == WAIT_TIMEOUT);
    CloseHandle(ev);
}

/* A NULL window with CALLBACK_WINDOW opens with no callback. */
TEST(wavein_contract_open_dev_null_window_opens) {
    REQUIRE(U(waveInOpen)); REQUIRE(U(waveInClose));
    if (!wic_nin()) { t_skip("no waveIn device"); return; }
    HWAVEIN h = NULL;
    EXPECT_EQ(U(waveInOpen)(&h, WAVE_MAPPER, pcm(), 0, 0, CALLBACK_WINDOW), MMSYSERR_NOERROR);
    if (h) EXPECT_EQ(U(waveInClose)(h), MMSYSERR_NOERROR);
}

/* waveInOpen does not open output devices: a waveIn id beyond the input count fails even when
 * that many output devices exist. */
TEST(wavein_contract_open_counts_input_only) {
    REQUIRE(U(waveInOpen));
    UINT nout = wic_nout(), nin = wic_nin();
    t_note("waveOut=%u waveIn=%u", nout, nin);
    if (nout <= nin) { t_skip("no extra output devices"); return; }
    HWAVEIN h = WIC_SENT_HANDLE;
    EXPECT_EQ(U(waveInOpen)(&h, nin, pcm(), 0, 0, 0), MMSYSERR_BADDEVICEID);
    CHECK(h == NULL);
}
