/* Live-handle contract of midiOutOpen/Close (only when a wave device exists; the synth renders through it).
 * Open = NewHandle(type 3) + MODM_OPEN; Close = MODM_CLOSE + FreeHandle. The GS synth is single-open per
 * process [VM]: each test closes every handle it opened before the next open.
 * Callback delivery itself (MOM_DONE etc.) belongs to midiout_device; here only the open/close notifications
 * prove that each callback type is accepted.
 */
#include "moc.h"

TEST(midiout_contract_live_open_close_id0) {
    REQUIRE(U(midiOutOpen)); REQUIRE(U(midiOutClose));
    if (!moc_can_open()) { t_skip("no wave device"); return; }
    HMIDIOUT h = MOC_SENT_HANDLE;
    EXPECT_EQ(U(midiOutOpen)(&h, 0, 0, 0, CALLBACK_NULL), MMSYSERR_NOERROR);
    CHECK(h != NULL && h != MOC_SENT_HANDLE);
    CHECKF((ULONG_PTR)h > 0xFFFF, "handle %p is a small value", h);
    EXPECT_EQ(U(midiOutClose)(h), MMSYSERR_NOERROR);
}

TEST(midiout_contract_live_open_close_mapper) {
    REQUIRE(U(midiOutOpen)); REQUIRE(U(midiOutClose));
    if (!moc_can_open()) { t_skip("no wave device"); return; }
    HMIDIOUT h = MOC_SENT_HANDLE;
    EXPECT_EQ(U(midiOutOpen)(&h, MIDI_MAPPER, 0, 0, CALLBACK_NULL), MMSYSERR_NOERROR);
    CHECK(h != NULL && h != MOC_SENT_HANDLE);
    EXPECT_EQ(U(midiOutClose)(h), MMSYSERR_NOERROR);
}

/* open/close cycles leave nothing behind */
TEST(midiout_contract_live_open_close_cycles) {
    REQUIRE(U(midiOutOpen)); REQUIRE(U(midiOutClose));
    if (!moc_can_open()) { t_skip("no wave device"); return; }
    for (int k = 0; k < 6; k++) {
        HMIDIOUT h = NULL;
        UINT id = (k & 1) ? MIDI_MAPPER : 0;
        CHECKF(U(midiOutOpen)(&h, id, 0, 0, CALLBACK_NULL) == MMSYSERR_NOERROR, "round %d id 0x%x", k, id);
        CHECKF(U(midiOutClose)(h) == MMSYSERR_NOERROR, "round %d close", k);
    }
}

/* the synth is single-open per process: a second open of id 0, or of the mapper, is ALLOCATED [VM] */
TEST(midiout_contract_live_single_open_allocated) {
    REQUIRE(U(midiOutOpen)); REQUIRE(U(midiOutClose));
    HMIDIOUT h = moc_open_id(0);
    if (!h) return;
    HMIDIOUT h2 = MOC_SENT_HANDLE;
    MMRESULT r = U(midiOutOpen)(&h2, 0, 0, 0, CALLBACK_NULL);
    t_note("second open id 0 -> %u", r);
    EXPECT_EQ(r, MMSYSERR_ALLOCATED);
    CHECK(h2 == NULL);
    if (r == MMSYSERR_NOERROR) U(midiOutClose)(h2);
    h2 = MOC_SENT_HANDLE;
    r = U(midiOutOpen)(&h2, MIDI_MAPPER, 0, 0, CALLBACK_NULL);
    t_note("mapper open while id 0 open -> %u", r);
    EXPECT_EQ(r, MMSYSERR_ALLOCATED);
    CHECK(h2 == NULL);
    if (r == MMSYSERR_NOERROR) U(midiOutClose)(h2);
    moc_close(h);
    /* free again after the close */
    h = moc_open_id(0);
    if (h) moc_close(h);
}

/* failing ladders still answer the same while a handle is open */
TEST(midiout_contract_live_ladder_unchanged_while_open) {
    REQUIRE(U(midiOutOpen)); REQUIRE(U(midiOutClose));
    HMIDIOUT h = moc_open_live();
    if (!h) return;
    HMIDIOUT h2 = MOC_SENT_HANDLE;
    EXPECT_EQ(U(midiOutOpen)(NULL, 0, 0, 0, 0), MMSYSERR_INVALPARAM);
    EXPECT_EQ(U(midiOutOpen)(&h2, 0, 0, 0, 2), MMSYSERR_INVALFLAG);
    CHECK(h2 == MOC_SENT_HANDLE);
    EXPECT_EQ(U(midiOutOpen)(&h2, 9, 0, 0, 0), MMSYSERR_BADDEVICEID);
    CHECK(h2 == NULL);
    moc_close(h);
}

/* a closed handle value passed to midiOutOpen's *phmo is just overwritten */
TEST(midiout_contract_live_reopen_after_close) {
    REQUIRE(U(midiOutOpen)); REQUIRE(U(midiOutClose));
    HMIDIOUT h = moc_open_id(0);
    if (!h) return;
    moc_close(h);
    HMIDIOUT h2 = h;
    EXPECT_EQ(U(midiOutOpen)(&h2, 0, 0, 0, CALLBACK_NULL), MMSYSERR_NOERROR);
    CHECK(h2 != NULL);
    moc_close(h2);
}

/* ---------------------------------------------------------------- callback types */

static volatile LONG g_fn_open, g_fn_close, g_fn_other;
static volatile DWORD_PTR g_fn_inst;
static void CALLBACK fn_cb(HMIDIOUT h, UINT msg, DWORD_PTR inst, DWORD_PTR p1, DWORD_PTR p2) {
    g_fn_inst = inst;
    if (msg == MOM_OPEN) InterlockedIncrement(&g_fn_open);
    else if (msg == MOM_CLOSE) InterlockedIncrement(&g_fn_close);
    else InterlockedIncrement(&g_fn_other);
}

TEST(midiout_contract_live_callback_function) {
    REQUIRE(U(midiOutOpen)); REQUIRE(U(midiOutClose));
    if (!moc_can_open()) { t_skip("no wave device"); return; }
    g_fn_open = g_fn_close = g_fn_other = 0; g_fn_inst = 0;
    HMIDIOUT h = NULL;
    EXPECT_EQ(U(midiOutOpen)(&h, 0, (DWORD_PTR)fn_cb, 0x1234, CALLBACK_FUNCTION), MMSYSERR_NOERROR);
    if (!h) return;
    for (int i = 0; i < 50 && !g_fn_open; i++) Sleep(10);
    EXPECT_EQ(g_fn_open, 1);
    EXPECT_EQ(g_fn_inst, 0x1234);
    EXPECT_EQ(U(midiOutClose)(h), MMSYSERR_NOERROR);
    for (int i = 0; i < 50 && !g_fn_close; i++) Sleep(10);
    EXPECT_EQ(g_fn_close, 1);
    EXPECT_EQ(g_fn_other, 0);
}

TEST(midiout_contract_live_callback_event) {
    REQUIRE(U(midiOutOpen)); REQUIRE(U(midiOutClose));
    if (!moc_can_open()) { t_skip("no wave device"); return; }
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    HMIDIOUT h = NULL;
    EXPECT_EQ(U(midiOutOpen)(&h, 0, (DWORD_PTR)ev, 0, CALLBACK_EVENT), MMSYSERR_NOERROR);
    if (h) {
        EXPECT_EQ(WaitForSingleObject(ev, 1000), WAIT_OBJECT_0);  /* MOM_OPEN */
        EXPECT_EQ(U(midiOutClose)(h), MMSYSERR_NOERROR);
        EXPECT_EQ(WaitForSingleObject(ev, 1000), WAIT_OBJECT_0);  /* MOM_CLOSE */
    }
    CloseHandle(ev);
}

TEST(midiout_contract_live_callback_window) {
    REQUIRE(U(midiOutOpen)); REQUIRE(U(midiOutClose)); REQUIRE(U(PeekMessageA)); REQUIRE(U(DestroyWindow));
    if (!moc_can_open()) { t_skip("no wave device"); return; }
    HWND w = moc_msg_window();
    if (!w) { t_skip("no window"); return; }
    HMIDIOUT h = NULL;
    EXPECT_EQ(U(midiOutOpen)(&h, 0, (DWORD_PTR)w, 0, CALLBACK_WINDOW), MMSYSERR_NOERROR);
    if (h) {
        MSG m; int got = 0;
        for (int i = 0; i < 100 && !got; i++) {
            while (U(PeekMessageA)(&m, w, 0, 0, PM_REMOVE))
                if (m.message == MM_MOM_OPEN) { got = 1; CHECK((HMIDIOUT)m.wParam == h); }
            if (!got) Sleep(10);
        }
        CHECK(got);
        EXPECT_EQ(U(midiOutClose)(h), MMSYSERR_NOERROR);
        got = 0;
        for (int i = 0; i < 100 && !got; i++) {
            while (U(PeekMessageA)(&m, w, 0, 0, PM_REMOVE))
                if (m.message == MM_MOM_CLOSE) got = 1;
            if (!got) Sleep(10);
        }
        CHECK(got);
    }
    U(DestroyWindow)(w);
}

TEST(midiout_contract_live_callback_thread) {
    REQUIRE(U(midiOutOpen)); REQUIRE(U(midiOutClose)); REQUIRE(U(PeekMessageA));
    if (!moc_can_open()) { t_skip("no wave device"); return; }
    MSG m;
    U(PeekMessageA)(&m, NULL, 0, 0, PM_NOREMOVE);   /* make sure this thread has a queue */
    while (U(PeekMessageA)(&m, (HWND)-1, MM_MOM_OPEN, MM_MOM_DONE, PM_REMOVE)) {}
    HMIDIOUT h = NULL;
    EXPECT_EQ(U(midiOutOpen)(&h, 0, (DWORD_PTR)GetCurrentThreadId(), 0, CALLBACK_THREAD), MMSYSERR_NOERROR);
    if (h) {
        int got = 0;
        for (int i = 0; i < 100 && !got; i++) {
            while (U(PeekMessageA)(&m, (HWND)-1, MM_MOM_OPEN, MM_MOM_DONE, PM_REMOVE))
                if (m.message == MM_MOM_OPEN) { got = 1; CHECK((HMIDIOUT)m.wParam == h); }
            if (!got) Sleep(10);
        }
        CHECK(got);
        EXPECT_EQ(U(midiOutClose)(h), MMSYSERR_NOERROR);
        got = 0;
        for (int i = 0; i < 100 && !got; i++) {
            while (U(PeekMessageA)(&m, (HWND)-1, MM_MOM_OPEN, MM_MOM_DONE, PM_REMOVE))
                if (m.message == MM_MOM_CLOSE) got = 1;
            if (!got) Sleep(10);
        }
        CHECK(got);
    }
}

/* CALLBACK_WINDOW with dwCallback 0 opens with no notification target */
TEST(midiout_contract_live_window_null_target) {
    REQUIRE(U(midiOutOpen)); REQUIRE(U(midiOutClose));
    if (!moc_can_open()) { t_skip("no wave device"); return; }
    HMIDIOUT h = NULL;
    EXPECT_EQ(U(midiOutOpen)(&h, 0, 0, 0, CALLBACK_WINDOW), MMSYSERR_NOERROR);
    if (h) EXPECT_EQ(U(midiOutClose)(h), MMSYSERR_NOERROR);
}

/* Reset on a fresh handle (nothing queued) */
TEST(midiout_contract_live_reset_idle_0) {
    REQUIRE(U(midiOutOpen)); REQUIRE(U(midiOutClose)); REQUIRE(U(midiOutReset));
    HMIDIOUT h = moc_open_live();
    if (!h) return;
    EXPECT_EQ(U(midiOutReset)(h), MMSYSERR_NOERROR);
    EXPECT_EQ(U(midiOutReset)(h), MMSYSERR_NOERROR);
    moc_close(h);
}

/* a waveOut handle is a foreign type for every midiOut entry */
TEST(midiout_contract_live_waveout_handle_rejected) {
    REQUIRE(U(midiOutShortMsg)); REQUIRE(U(midiOutGetID)); REQUIRE(U(midiOutClose)); REQUIRE(U(midiOutGetVolume));
    REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutClose));
    if (!moc_nwave()) { t_skip("no wave device"); return; }
    WAVEFORMATEX f; memset(&f, 0, sizeof(f));
    f.wFormatTag = WAVE_FORMAT_PCM; f.nChannels = 2; f.nSamplesPerSec = 44100; f.wBitsPerSample = 16;
    f.nBlockAlign = 4; f.nAvgBytesPerSec = 44100 * 4;
    HWAVEOUT wo = NULL;
    if (U(waveOutOpen)(&wo, WAVE_MAPPER, &f, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) { t_skip("waveOutOpen failed"); return; }
    HMIDIOUT h = (HMIDIOUT)wo;
    UINT id = 77; DWORD v = 0;
    MIDIHDR m; char b[8]; moc_hdr(&m, b, 8, MHDR_PREPARED);
    EXPECT_EQ(U(midiOutShortMsg)(h, 0x7BB0), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(midiOutGetID)(h, &id), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(midiOutLongMsg)(h, &m, sizeof(m)), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(midiOutPrepareHeader)(h, &m, sizeof(m)), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(midiOutReset)(h), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(midiOutGetVolume)(h, &v), MMSYSERR_BADDEVICEID);
    EXPECT_EQ(U(midiOutClose)(h), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveOutClose)(wo), MMSYSERR_NOERROR);
}
