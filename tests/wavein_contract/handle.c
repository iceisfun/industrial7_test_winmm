/* Handle contract of waveInStart (!0x11960), waveInStop (!0x119b0), waveInReset (!0x11a00), waveInClose
 * (!0x11650), waveInGetPosition (!0x11a60), waveInGetID (!0x11bb0), waveInMessage (!0x10f70).
 *
 * ValidateHandle(h, 2): > 0xFFFF, != -1, != 0xFFFFFFFF, in the user VA range, on the global handle list and of
 * type 2 (waveIn). Anything else -> 5 (looked up in a list, never dereferenced).
 * GetPosition: pmmt NULL -> 11 before the handle. GetID: puDeviceID NULL -> 11 before the handle, no driver call,
 * returns the id given at open (mapper -> 0xFFFFFFFF). Message: a non-handle is treated as a device id
 * (low 32 bits): id < count or the mapper -> driver, else 2.
 */
#include "wic.h"

typedef MMRESULT (WINAPI *h1_fn)(HWAVEIN);
static h1_fn fn_of(int i) {
    switch (i) {
    case 0: return (h1_fn)U(waveInStart);
    case 1: return (h1_fn)U(waveInStop);
    case 2: return (h1_fn)U(waveInReset);
    default: return (h1_fn)U(waveInClose);
    }
}
static const char *fn_name[] = { "waveInStart", "waveInStop", "waveInReset", "waveInClose" };

/* ---------------------------------------------------------------- invalid handle values -> 5 */

#define BAD_HANDLE_TEST(idx, nm) \
TEST(wavein_contract_##nm##_bad_handles_5) { \
    h1_fn f = fn_of(idx); REQUIRE(f); \
    for (int i = 0; i < wic_nbad; i++) \
        CHECKF(f((HWAVEIN)wic_bad_handles[i]) == MMSYSERR_INVALHANDLE, "%s(%p)", fn_name[idx], (void *)wic_bad_handles[i]); \
} \
TEST(wavein_contract_##nm##_null_5) { h1_fn f = fn_of(idx); REQUIRE(f); EXPECT_EQ(f(NULL), MMSYSERR_INVALHANDLE); } \
TEST(wavein_contract_##nm##_mapper_value_5) { \
    h1_fn f = fn_of(idx); REQUIRE(f); \
    EXPECT_EQ(f((HWAVEIN)(ULONG_PTR)WAVE_MAPPER), MMSYSERR_INVALHANDLE); \
    EXPECT_EQ(f((HWAVEIN)(ULONG_PTR)-1), MMSYSERR_INVALHANDLE); \
    EXPECT_EQ(f((HWAVEIN)(ULONG_PTR)0), MMSYSERR_INVALHANDLE); \
} \
TEST(wavein_contract_##nm##_mmio_handle_5) { \
    h1_fn f = fn_of(idx); REQUIRE(f); \
    HMMIO m = wic_mmio_open(); if (!m) { t_skip("mmioOpen(memory) failed"); return; } \
    EXPECT_EQ(f((HWAVEIN)m), MMSYSERR_INVALHANDLE); \
    wic_mmio_close(m); \
    EXPECT_EQ(f((HWAVEIN)m), MMSYSERR_INVALHANDLE); \
} \
TEST(wavein_contract_##nm##_waveout_handle_5) { \
    h1_fn f = fn_of(idx); REQUIRE(f); \
    HWAVEOUT o = wic_waveout_open(); if (!o) { t_skip("no waveOut device to open"); return; } \
    EXPECT_EQ(f((HWAVEIN)o), MMSYSERR_INVALHANDLE); \
    EXPECT_EQ(U(waveOutClose)(o), MMSYSERR_NOERROR); \
} \
TEST(wavein_contract_##nm##_closed_handle_5) { \
    h1_fn f = fn_of(idx); REQUIRE(f); \
    HWAVEIN h = wic_closed_handle(); if (!h) return; \
    EXPECT_EQ(f(h), MMSYSERR_INVALHANDLE); \
    EXPECT_EQ(f(h), MMSYSERR_INVALHANDLE); \
}

BAD_HANDLE_TEST(0, start)
BAD_HANDLE_TEST(1, stop)
BAD_HANDLE_TEST(2, reset)
BAD_HANDLE_TEST(3, close)

/* A MIDI output handle is a different type as well (the GS synth is usually present even without audio). */
TEST(wavein_contract_handles_midiout_handle_5) {
    REQUIRE(U(midiOutOpen)); REQUIRE(U(midiOutClose)); REQUIRE(U(waveInClose));
    HMIDIOUT mo = NULL;
    MMRESULT r = U(midiOutOpen)(&mo, 0, 0, 0, CALLBACK_NULL);
    if (r != MMSYSERR_NOERROR || !mo) { t_note("midiOutOpen(0) -> %u", r); t_skip("no MIDI out handle"); return; }
    HWAVEIN h = (HWAVEIN)mo;
    EXPECT_EQ(U(waveInStart)(h), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveInStop)(h), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveInReset)(h), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveInClose)(h), MMSYSERR_INVALHANDLE);
    MMTIME t; t.wType = TIME_BYTES;
    EXPECT_EQ(U(waveInGetPosition)(h, &t, sizeof(t)), MMSYSERR_INVALHANDLE);
    UINT id = 0x77;
    EXPECT_EQ(U(waveInGetID)(h, &id), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(id, 0x77);
    WAVEHDR w; char b[16]; wic_hdr(&w, b, 16, 0);
    EXPECT_EQ(U(waveInPrepareHeader)(h, &w, sizeof(w)), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(midiOutClose)(mo), MMSYSERR_NOERROR);
}

/* ---------------------------------------------------------------- GetPosition */

TEST(wavein_contract_getpos_null_mmtime_11_any_handle) {
    REQUIRE(U(waveInGetPosition));
    static const UINT cbs[] = { 0, 1, sizeof(MMTIME), 0x1000 };
    for (int i = 0; i < wic_nbad; i++)
        for (int j = 0; j < 4; j++)
            CHECKF(U(waveInGetPosition)((HWAVEIN)wic_bad_handles[i], NULL, cbs[j]) == MMSYSERR_INVALPARAM, "h=%p cb=%u", (void *)wic_bad_handles[i], cbs[j]);
}

TEST(wavein_contract_getpos_bad_handle_5_untouched) {
    REQUIRE(U(waveInGetPosition));
    static const UINT cbs[] = { 0, 1, sizeof(MMTIME) - 1, sizeof(MMTIME), 0x1000 };
    for (int i = 0; i < wic_nbad; i++)
        for (int j = 0; j < 5; j++) {
            MMTIME t; memset(&t, 0xA5, sizeof(t)); t.wType = TIME_BYTES;
            MMRESULT r = U(waveInGetPosition)((HWAVEIN)wic_bad_handles[i], &t, cbs[j]);
            CHECKF(r == MMSYSERR_INVALHANDLE, "h=%p cb=%u r=%u", (void *)wic_bad_handles[i], cbs[j], r);
            CHECK(t.wType == TIME_BYTES && t.u.cb == 0xA5A5A5A5);
        }
}

TEST(wavein_contract_getpos_mmio_and_closed_5) {
    REQUIRE(U(waveInGetPosition));
    HMMIO m = wic_mmio_open(); if (!m) { t_skip("mmioOpen(memory) failed"); return; }
    MMTIME t; t.wType = TIME_BYTES;
    EXPECT_EQ(U(waveInGetPosition)((HWAVEIN)m, &t, sizeof(t)), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveInGetPosition)((HWAVEIN)m, NULL, sizeof(t)), MMSYSERR_INVALPARAM);
    wic_mmio_close(m);
    EXPECT_EQ(U(waveInGetPosition)((HWAVEIN)m, &t, sizeof(t)), MMSYSERR_INVALHANDLE);
}

TEST(wavein_contract_getpos_waveout_handle_5) {
    REQUIRE(U(waveInGetPosition));
    HWAVEOUT o = wic_waveout_open(); if (!o) { t_skip("no waveOut device to open"); return; }
    MMTIME t; t.wType = TIME_BYTES;
    EXPECT_EQ(U(waveInGetPosition)((HWAVEIN)o, &t, sizeof(t)), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveInGetPosition)((HWAVEIN)o, NULL, sizeof(t)), MMSYSERR_INVALPARAM);
    EXPECT_EQ(U(waveOutClose)(o), MMSYSERR_NOERROR);
}

TEST(wavein_contract_getpos_closed_handle_5) {
    REQUIRE(U(waveInGetPosition));
    HWAVEIN h = wic_closed_handle(); if (!h) return;
    MMTIME t; t.wType = TIME_BYTES;
    EXPECT_EQ(U(waveInGetPosition)(h, &t, sizeof(t)), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveInGetPosition)(h, NULL, 0), MMSYSERR_INVALPARAM);
}

/* ---------------------------------------------------------------- GetID */

TEST(wavein_contract_getid_null_11_any_handle) {
    REQUIRE(U(waveInGetID));
    for (int i = 0; i < wic_nbad; i++)
        CHECKF(U(waveInGetID)((HWAVEIN)wic_bad_handles[i], NULL) == MMSYSERR_INVALPARAM, "h=%p", (void *)wic_bad_handles[i]);
}

TEST(wavein_contract_getid_bad_handle_5_untouched) {
    REQUIRE(U(waveInGetID));
    for (int i = 0; i < wic_nbad; i++) {
        UINT id = 0xCAFE;
        CHECKF(U(waveInGetID)((HWAVEIN)wic_bad_handles[i], &id) == MMSYSERR_INVALHANDLE, "h=%p", (void *)wic_bad_handles[i]);
        CHECK(id == 0xCAFE);
    }
}

TEST(wavein_contract_getid_mmio_handle_5) {
    REQUIRE(U(waveInGetID));
    HMMIO m = wic_mmio_open(); if (!m) { t_skip("mmioOpen(memory) failed"); return; }
    UINT id = 0xCAFE;
    EXPECT_EQ(U(waveInGetID)((HWAVEIN)m, &id), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(id, 0xCAFE);
    EXPECT_EQ(U(waveInGetID)((HWAVEIN)m, NULL), MMSYSERR_INVALPARAM);
    wic_mmio_close(m);
}

TEST(wavein_contract_getid_waveout_handle_5) {
    REQUIRE(U(waveInGetID));
    HWAVEOUT o = wic_waveout_open(); if (!o) { t_skip("no waveOut device to open"); return; }
    UINT id = 0xCAFE;
    EXPECT_EQ(U(waveInGetID)((HWAVEIN)o, &id), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(id, 0xCAFE);
    EXPECT_EQ(U(waveOutClose)(o), MMSYSERR_NOERROR);
}

TEST(wavein_contract_getid_closed_handle_5) {
    REQUIRE(U(waveInGetID));
    HWAVEIN h = wic_closed_handle(); if (!h) return;
    UINT id = 0xCAFE;
    EXPECT_EQ(U(waveInGetID)(h, &id), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(id, 0xCAFE);
}

/* ---------------------------------------------------------------- Message: non-handles are device ids */

TEST(wavein_contract_message_bad_id_2) {
    REQUIRE(U(waveInMessage));
    UINT n = wic_nin();
    ULONG_PTR ids[] = { 9999, n, n + 1, 0xFFFF, 0x10000, 0xFFFFFFFE, 0x12345670 };
    static const UINT msgs[] = { 0, DRV_QUERYDEVICEINTERFACESIZE, DRV_QUERYDEVICEINTERFACE, 0x4000, 0xFFFFFFFF };
    for (unsigned i = 0; i < sizeof(ids) / sizeof(ids[0]); i++)
        for (unsigned j = 0; j < sizeof(msgs) / sizeof(msgs[0]); j++)
            CHECKF(U(waveInMessage)((HWAVEIN)ids[i], msgs[j], 0, 0) == MMSYSERR_BADDEVICEID, "id=0x%llx msg=0x%x", (unsigned long long)ids[i], msgs[j]);
}

/* Upper 32 bits are dropped: 0x1_0000_270F behaves like id 9999. */
TEST(wavein_contract_message_id_truncated) {
    REQUIRE(U(waveInMessage));
    EXPECT_EQ(U(waveInMessage)((HWAVEIN)(ULONG_PTR)0x10000270Full, DRV_QUERYDEVICEINTERFACESIZE, 0, 0), MMSYSERR_BADDEVICEID);
    if (!wic_nin()) EXPECT_EQ(U(waveInMessage)((HWAVEIN)(ULONG_PTR)0x100000000ull, DRV_QUERYDEVICEINTERFACESIZE, 0, 0), MMSYSERR_BADDEVICEID);
}

TEST(wavein_contract_message_zero_devices_2) {
    REQUIRE(U(waveInMessage));
    if (wic_nin()) { t_skip("waveIn devices present"); return; }
    static const UINT msgs[] = { 0, DRV_QUERYDEVICEINTERFACESIZE, DRV_QUERYDEVICEINTERFACE, DRVM_MAPPER_PREFERRED_GET, 0x4000 };
    DWORD a = 0x55, b = 0x66;
    for (unsigned j = 0; j < sizeof(msgs) / sizeof(msgs[0]); j++) {
        CHECKF(U(waveInMessage)(NULL, msgs[j], (DWORD_PTR)&a, (DWORD_PTR)&b) == MMSYSERR_BADDEVICEID, "id0 msg=0x%x", msgs[j]);
        CHECKF(U(waveInMessage)((HWAVEIN)1, msgs[j], (DWORD_PTR)&a, (DWORD_PTR)&b) == MMSYSERR_BADDEVICEID, "id1 msg=0x%x", msgs[j]);
        MMRESULT r = U(waveInMessage)((HWAVEIN)(ULONG_PTR)WAVE_MAPPER, msgs[j], (DWORD_PTR)&a, (DWORD_PTR)&b);
        if (!wic_nout()) CHECKF(r == MMSYSERR_BADDEVICEID, "mapper msg=0x%x r=%u", msgs[j], r);
        else t_note("mapper msg=0x%x with 0 waveIn / %u waveOut -> %u", msgs[j], wic_nout(), r);
    }
    CHECK(a == 0x55 && b == 0x66);
}

/* A handle of another type is not a waveIn handle: its value becomes a (bad) device id -> 2, not 5. */
TEST(wavein_contract_message_mmio_handle_2) {
    REQUIRE(U(waveInMessage));
    HMMIO m = wic_mmio_open(); if (!m) { t_skip("mmioOpen(memory) failed"); return; }
    EXPECT_EQ(U(waveInMessage)((HWAVEIN)m, DRV_QUERYDEVICEINTERFACESIZE, 0, 0), MMSYSERR_BADDEVICEID);
    wic_mmio_close(m);
}

TEST(wavein_contract_message_waveout_handle_2) {
    REQUIRE(U(waveInMessage));
    HWAVEOUT o = wic_waveout_open(); if (!o) { t_skip("no waveOut device to open"); return; }
    DWORD sz = 0x55;
    EXPECT_EQ(U(waveInMessage)((HWAVEIN)o, DRV_QUERYDEVICEINTERFACESIZE, (DWORD_PTR)&sz, 0), MMSYSERR_BADDEVICEID);
    EXPECT_EQ(sz, 0x55);
    EXPECT_EQ(U(waveOutClose)(o), MMSYSERR_NOERROR);
}

TEST(wavein_contract_message_closed_handle_2) {
    REQUIRE(U(waveInMessage));
    HWAVEIN h = wic_closed_handle(); if (!h) return;
    EXPECT_EQ(U(waveInMessage)(h, DRV_QUERYDEVICEINTERFACESIZE, 0, 0), MMSYSERR_BADDEVICEID);
}

/* And vice versa: waveOut* rejects an HWAVEIN. */
TEST(wavein_contract_cross_wavein_to_waveout_5) {
    REQUIRE(U(waveOutClose)); REQUIRE(U(waveOutReset)); REQUIRE(U(waveOutGetID)); REQUIRE(U(waveOutGetPosition));
    REQUIRE(U(waveOutPause)); REQUIRE(U(waveOutRestart));
    HWAVEIN h = wic_open_any(); if (!h) return;
    HWAVEOUT o = (HWAVEOUT)h;
    EXPECT_EQ(U(waveOutClose)(o), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveOutReset)(o), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveOutPause)(o), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveOutRestart)(o), MMSYSERR_INVALHANDLE);
    UINT id = 0xCAFE;
    EXPECT_EQ(U(waveOutGetID)(o, &id), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(id, 0xCAFE);
    MMTIME t; t.wType = TIME_BYTES;
    EXPECT_EQ(U(waveOutGetPosition)(o, &t, sizeof(t)), MMSYSERR_INVALHANDLE);
    /* the HWAVEIN is still alive after all of that */
    EXPECT_EQ(U(waveInClose)(h), MMSYSERR_NOERROR);
}

TEST(wavein_contract_cross_wavein_to_midiout_5) {
    REQUIRE(U(midiOutClose)); REQUIRE(U(midiOutReset));
    HWAVEIN h = wic_open_any(); if (!h) return;
    EXPECT_EQ(U(midiOutClose)((HMIDIOUT)h), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(midiOutReset)((HMIDIOUT)h), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveInClose)(h), MMSYSERR_NOERROR);
}

/* waveOutClose on an HWAVEIN and waveInClose on an HWAVEOUT, both handles survive. */
TEST(wavein_contract_cross_close_both_ways) {
    REQUIRE(U(waveInClose)); REQUIRE(U(waveOutClose));
    HWAVEOUT o = wic_waveout_open();
    if (!o) { t_skip("no waveOut device to open"); return; }
    HWAVEIN h = NULL;
    if (!wic_nin() || wic_open(&h, WAVE_MAPPER)) { t_skip("no waveIn device"); U(waveOutClose)(o); return; }
    EXPECT_EQ(U(waveInClose)((HWAVEIN)o), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveOutClose)((HWAVEOUT)h), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveInClose)(h), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveOutClose)(o), MMSYSERR_NOERROR);
}
