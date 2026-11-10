#include "moc.h"

const ULONG_PTR moc_bad_handles[] = {
    0,                       /* NULL */
    1, 2, 9, 0x10, 0xFFFF,   /* small integers (<= 0xFFFF are never handles) */
    0x10000,                 /* first value past the small-int range, not on the handle list */
    0x12345670,              /* plausible user address, not on the handle list */
    0xFFFFFFFFull,           /* explicitly rejected (MIDI_MAPPER as a value) */
    (ULONG_PTR)-1,           /* explicitly rejected */
};
const int moc_nbad = sizeof(moc_bad_handles) / sizeof(moc_bad_handles[0]);

const ULONG_PTR moc_bad_ids[] = {
    9, 0x10, 0xFF, 0xFFFF, 0x10000, 0x12345670, 0x7FFFFFFF, 0x80000000ull, 0xFFFFFFFEull,
};
const int moc_nbadid = sizeof(moc_bad_ids) / sizeof(moc_bad_ids[0]);

static UINT g_nout = (UINT)-1, g_nwave = (UINT)-1;

UINT moc_nout(void) {
    if (g_nout == (UINT)-1) {
        if (!U(midiOutGetNumDevs)) return 0;
        g_nout = U(midiOutGetNumDevs)();
        static int noted;
        if (!noted) { noted = 1; t_note("midiOutGetNumDevs=%u", g_nout); }
    }
    return g_nout;
}

UINT moc_nwave(void) {
    if (g_nwave == (UINT)-1) {
        if (!U(waveOutGetNumDevs)) return 0;
        g_nwave = U(waveOutGetNumDevs)();
        static int noted;
        if (!noted) { noted = 1; t_note("waveOutGetNumDevs=%u", g_nwave); }
    }
    return g_nwave;
}

int moc_can_open(void) { return moc_nwave() > 0 && moc_nout() > 0; }

HMIDIOUT moc_open_id(UINT id) {
    if (!moc_can_open()) { t_skip("no wave device: the synth cannot be opened (waveOutGetNumDevs=%u)", moc_nwave()); return NULL; }
    HMIDIOUT h = NULL;
    MMRESULT r = U(midiOutOpen)(&h, id, 0, 0, CALLBACK_NULL);
    if (r != MMSYSERR_NOERROR || !h) {
        t_note("midiOutOpen(%u) = %u with %u wave device(s)", id, r, moc_nwave());
        t_skip("cannot open midiOut %u", id);
        return NULL;
    }
    return h;
}

HMIDIOUT moc_open_live(void) {
    if (!moc_can_open()) { t_skip("no wave device: the synth cannot be opened (waveOutGetNumDevs=%u)", moc_nwave()); return NULL; }
    HMIDIOUT h = NULL;
    MMRESULT r = U(midiOutOpen)(&h, 0, 0, 0, CALLBACK_NULL);
    if (r != MMSYSERR_NOERROR) { h = NULL; r = U(midiOutOpen)(&h, MIDI_MAPPER, 0, 0, CALLBACK_NULL); }
    if (r != MMSYSERR_NOERROR || !h) {
        t_note("midiOutOpen failed %u with %u wave device(s)", r, moc_nwave());
        t_skip("cannot open a midiOut device");
        return NULL;
    }
    return h;
}

void moc_close(HMIDIOUT h) {
    if (!h) return;
    for (DWORD ch = 0; ch < 16; ch++) U(midiOutShortMsg)(h, 0x7BB0 | ch);   /* all notes off */
    U(midiOutReset)(h);
    MMRESULT r = U(midiOutClose)(h);
    CHECKF(r == MMSYSERR_NOERROR, "midiOutClose(%p) = %u", h, r);
}

HMIDIOUT moc_closed_handle(void) {
    HMIDIOUT h = moc_open_live();
    if (!h) return NULL;
    if (U(midiOutClose)(h) != MMSYSERR_NOERROR) { t_skip("midiOutClose failed"); return NULL; }
    return h;
}

static char g_mmio_buf[64];
HMMIO moc_mmio_open(void) {
    if (!U(mmioOpenA)) return NULL;
    MMIOINFO mi; memset(&mi, 0, sizeof(mi));
    mi.fccIOProc = FOURCC_MEM;
    mi.pchBuffer = g_mmio_buf;
    mi.cchBuffer = sizeof(g_mmio_buf);
    return U(mmioOpenA)(NULL, &mi, MMIO_READWRITE);
}
void moc_mmio_close(HMMIO h) { if (h && U(mmioClose)) U(mmioClose)(h, 0); }

HWND moc_msg_window(void) {
    if (!U(CreateWindowExA)) return NULL;
    return U(CreateWindowExA)(0, "STATIC", "midiout_contract", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, NULL, NULL);
}

void moc_hdr(MIDIHDR *h, void *buf, DWORD len, DWORD flags) {
    memset(h, 0, sizeof(*h));
    h->lpData = (LPSTR)buf;
    h->dwBufferLength = len;
    h->dwBytesRecorded = 0;
    h->dwFlags = flags;
}

/* Area-wide facts printed once, so every run records the configuration it ran in. */
TEST(midiout_contract_environment) {
    REQUIRE(U(midiOutGetNumDevs)); REQUIRE(U(waveOutGetNumDevs));
    UINT n = moc_nout(), w = moc_nwave();
    t_note("MOCENV midiout=%u waveout=%u live=%d", n, w, moc_can_open());
    /* the GS synth is enumerated on every guest, with or without an audio endpoint [VM] */
    CHECKF(n >= 1, "midiOutGetNumDevs=%u", n);
    /* the count is stable across calls */
    EXPECT_EQ(U(midiOutGetNumDevs)(), n);
    EXPECT_EQ(U(midiOutGetNumDevs)(), n);
}
