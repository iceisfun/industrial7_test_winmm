/* End-to-end sequences the way real consumers drive the synth (Web MIDI style sysex senders, DirectShow, games):
 * open with a callback, mix short and long messages, wait for MOM_DONE, unprepare, close; repeated over several
 * opens. Every step's result is pinned; the content is quiet and short.
 */
#include "mod.h"

#define PH(h, m) U(midiOutPrepareHeader)((h), (m), sizeof(MIDIHDR))
#define UH(h, m) U(midiOutUnprepareHeader)((h), (m), sizeof(MIDIHDR))
#define LM(h, m) U(midiOutLongMsg)((h), (m), sizeof(MIDIHDR))
#define REQ_LONG() do { REQUIRE(U(midiOutLongMsg)); REQUIRE(U(midiOutPrepareHeader)); REQUIRE(U(midiOutUnprepareHeader)); } while (0)

static const UINT g_ids[] = { 0, MIDI_MAPPER };

/* sysex sender: heap header and buffer, MOM_DONE awaited, unprepare, free; repeated */
TEST(midiout_device_seq_sysex_sender) {
    MOD_NEED_LIVE(); REQ_LONG();
    for (int i = 0; i < NELEM(g_ids); i++) {
        mod_rec_reset();
        HMIDIOUT h = NULL;
        if (mod_open(&h, g_ids[i], (DWORD_PTR)mod_cb, 0, CALLBACK_FUNCTION) != MMSYSERR_NOERROR) { CHECK(0); continue; }
        for (int k = 0; k < 5; k++) {
            MIDIHDR *m = (MIDIHDR *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(MIDIHDR));
            BYTE *b = (BYTE *)HeapAlloc(GetProcessHeap(), 0, sizeof(mod_gs_reset));
            memcpy(b, (k & 1) ? mod_gm_on : mod_gs_reset, (k & 1) ? sizeof(mod_gm_on) : sizeof(mod_gs_reset));
            m->lpData = (LPSTR)b;
            m->dwBufferLength = (k & 1) ? sizeof(mod_gm_on) : sizeof(mod_gs_reset);
            EXPECT_EQ(PH(h, m), MMSYSERR_NOERROR);
            EXPECT_EQ(LM(h, m), MMSYSERR_NOERROR);
            CHECKF(mod_wait_msg(MOM_DONE, k + 1, 2000), "id 0x%x send %d", g_ids[i], k);
            EXPECT_EQ(UH(h, m), MMSYSERR_NOERROR);
            HeapFree(GetProcessHeap(), 0, b);
            HeapFree(GetProcessHeap(), 0, m);
        }
        EXPECT_EQ(mod_rec_count_msg(MOM_DONE), 5);
        mod_close(h);
    }
}

/* short and long messages interleaved */
TEST(midiout_device_seq_interleaved) {
    MOD_NEED_LIVE(); REQ_LONG();
    for (int i = 0; i < NELEM(g_ids); i++) {
        HMIDIOUT h = mod_open_null(g_ids[i]);
        if (!h) continue;
        MIDIHDR m; mod_hdr(&m, mod_gs_reset, sizeof(mod_gs_reset));
        EXPECT_EQ(PH(h, &m), MMSYSERR_NOERROR);
        for (int k = 0; k < 3; k++) {
            EXPECT_EQ(U(midiOutShortMsg)(h, MOD_MSG(0xC0, k * 8, 0)), MMSYSERR_NOERROR);
            EXPECT_EQ(LM(h, &m), MMSYSERR_NOERROR);
            /* short messages are accepted while the long buffer is (possibly) still with the driver */
            EXPECT_EQ(U(midiOutShortMsg)(h, MOD_MSG(0xB0, 123, 0)), MMSYSERR_NOERROR);
            CHECK(mod_wait_done(&m, 2000));
            m.dwFlags &= ~MHDR_DONE;
        }
        EXPECT_EQ(UH(h, &m), MMSYSERR_NOERROR);
        EXPECT_EQ(mod_note(h, 0, 76, 10, 40), MMSYSERR_NOERROR);
        mod_close(h);
    }
}

/* several open/send/close cycles with a callback each time: one OPEN, one DONE, one CLOSE per cycle */
TEST(midiout_device_seq_cycles) {
    MOD_NEED_LIVE(); REQ_LONG();
    for (int c = 0; c < 6; c++) {
        UINT id = g_ids[c & 1];
        mod_rec_reset();
        HMIDIOUT h = NULL;
        if (mod_open(&h, id, (DWORD_PTR)mod_cb, c, CALLBACK_FUNCTION) != MMSYSERR_NOERROR) { CHECKF(0, "cycle %d", c); continue; }
        MIDIHDR m; mod_hdr(&m, mod_gm_on, sizeof(mod_gm_on));
        EXPECT_EQ(PH(h, &m), MMSYSERR_NOERROR);
        EXPECT_EQ(LM(h, &m), MMSYSERR_NOERROR);
        CHECK(mod_wait_msg(MOM_DONE, 1, 2000));
        EXPECT_EQ(UH(h, &m), MMSYSERR_NOERROR);
        mod_close(h);
        CHECK(mod_wait_msg(MOM_CLOSE, 1, 1000));
        Sleep(10);
        CHECKF(mod_rec_count_msg(MOM_OPEN) == 1 && mod_rec_count_msg(MOM_DONE) == 1 && mod_rec_count_msg(MOM_CLOSE) == 1,
               "cycle %d: open %d done %d close %d", c, mod_rec_count_msg(MOM_OPEN), mod_rec_count_msg(MOM_DONE),
               mod_rec_count_msg(MOM_CLOSE));
        for (int k = 0; k < mod_rec_count(); k++) CHECK(mod_rec(k)->inst == (DWORD_PTR)c);
    }
}

/* a scale of short quiet notes on the mapper and on the synth (TONE-free: MIDI is not announced) */
TEST(midiout_device_seq_quiet_phrase) {
    MOD_NEED_LIVE();
    static const BYTE notes[] = { 60, 62, 64, 65, 67 };
    for (int i = 0; i < NELEM(g_ids); i++) {
        HMIDIOUT h = mod_open_null(g_ids[i]);
        if (!h) continue;
        EXPECT_EQ(U(midiOutShortMsg)(h, MOD_MSG(0xB0, 7, 60)), MMSYSERR_NOERROR);
        for (int k = 0; k < NELEM(notes); k++)
            CHECKF(mod_note(h, 0, notes[k], 12, 50) == MMSYSERR_NOERROR, "note %u", notes[k]);
        EXPECT_EQ(U(midiOutShortMsg)(h, MOD_MSG(0xB0, 7, 100)), MMSYSERR_NOERROR);
        mod_close(h);
    }
}

/* the drum channel through the mapper: a couple of soft hits */
TEST(midiout_device_seq_drum_channel) {
    MOD_NEED_LIVE();
    HMIDIOUT h = mod_open_null(MIDI_MAPPER);
    if (!h) return;
    static const BYTE kit[] = { 42, 44, 37 };
    for (int k = 0; k < NELEM(kit); k++)
        CHECKF(mod_note(h, 9, kit[k], 10, 40) == MMSYSERR_NOERROR, "drum %u", kit[k]);
    mod_close(h);
}

/* the whole stack in one thread, the close from a second thread while the first waits for MOM_CLOSE */
typedef struct { HMIDIOUT h; MMRESULT r; } cl_t;
static DWORD WINAPI close_thread(LPVOID p) { cl_t *c = (cl_t *)p; mod_quiet(c->h); c->r = U(midiOutClose)(c->h); return 0; }
TEST(midiout_device_seq_close_from_worker) {
    MOD_NEED_LIVE();
    mod_rec_reset();
    HMIDIOUT h = NULL;
    if (mod_open(&h, 0, (DWORD_PTR)mod_cb, 0, CALLBACK_FUNCTION) != MMSYSERR_NOERROR) { CHECK(0); return; }
    cl_t c = { h, 0xFFFF };
    HANDLE th = CreateThread(NULL, 0, close_thread, &c, 0, NULL);
    CHECK(mod_wait_msg(MOM_CLOSE, 1, 3000));
    CHECK(WaitForSingleObject(th, 5000) == WAIT_OBJECT_0);
    CloseHandle(th);
    EXPECT_EQ(c.r, MMSYSERR_NOERROR);
    if (c.r == MMSYSERR_NOERROR) mod_forget(h); else mod_close(h);
    /* MOM_CLOSE was delivered on the closing thread's call */
    for (int k = 0; k < mod_rec_count(); k++)
        if (mod_rec(k)->msg == MOM_CLOSE) t_note("MOM_CLOSE on %s thread", mod_rec(k)->tid == GetCurrentThreadId() ? "the test" : "another");
}

/* the synth is per process: a child process can open it while this process holds it */
CHILD(mod_child_open) {
    HMIDIOUT h = NULL;
    MMRESULT r = U(midiOutOpen)(&h, 0, 0, 0, CALLBACK_NULL);
    if (r == MMSYSERR_NOERROR) {
        U(midiOutShortMsg)(h, MOD_MSG(0xB0, 123, 0));
        U(midiOutClose)(h);
    }
    return (int)r;
}

TEST(midiout_device_seq_other_process_open) {
    MOD_NEED_LIVE();
    HMIDIOUT h = mod_open_null(0);
    if (!h) return;
    DWORD code = t_run_child("mod_child_open", NULL, 30000);
    t_note("child midiOutOpen(0) while this process holds id 0: %lu", code);
    /* result recorded only; whether a second process can open id 0 is unverified */
    mod_close(h);
}
