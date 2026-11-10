#include "wic.h"

const ULONG_PTR wic_bad_handles[] = {
    0,                       /* NULL */
    1, 2, 0x10, 0xFFFF,      /* small integers (<= 0xFFFF are never handles) */
    0x10000,                 /* first value past the small-int range, not on the handle list */
    0x12345670,              /* plausible user address, not on the handle list */
    0xFFFFFFFFull,           /* explicitly rejected */
    (ULONG_PTR)-1,           /* explicitly rejected */
};
const int wic_nbad = sizeof(wic_bad_handles) / sizeof(wic_bad_handles[0]);

static UINT g_nin = (UINT)-1, g_nout = (UINT)-1;

UINT wic_nin(void) {
    if (g_nin == (UINT)-1) {
        if (!U(waveInGetNumDevs)) return 0;
        g_nin = U(waveInGetNumDevs)();
        static int noted;
        if (!noted) { noted = 1; t_note("waveInGetNumDevs=%u", g_nin); }
    }
    return g_nin;
}

UINT wic_nout(void) {
    if (g_nout == (UINT)-1) {
        if (!U(waveOutGetNumDevs)) return 0;
        g_nout = U(waveOutGetNumDevs)();
    }
    return g_nout;
}

void wic_pcm(WAVEFORMATEX *f, DWORD rate, WORD bits, WORD ch) {
    memset(f, 0, sizeof(*f));
    f->wFormatTag = WAVE_FORMAT_PCM;
    f->nChannels = ch;
    f->nSamplesPerSec = rate;
    f->wBitsPerSample = bits;
    f->nBlockAlign = (WORD)(ch * bits / 8);
    f->nAvgBytesPerSec = rate * f->nBlockAlign;
    f->cbSize = 0;
}

MMRESULT wic_open(HWAVEIN *ph, UINT id) {
    WAVEFORMATEX f; wic_pcm(&f, 44100, 16, 1);
    return U(waveInOpen)(ph, id, &f, 0, 0, CALLBACK_NULL);
}

HWAVEIN wic_open_any(void) {
    if (!wic_nin()) { t_skip("no waveIn device (waveInGetNumDevs=0)"); return NULL; }
    HWAVEIN h = NULL;
    MMRESULT r = wic_open(&h, WAVE_MAPPER);
    if (r != MMSYSERR_NOERROR) { h = NULL; r = wic_open(&h, 0); }
    if (r != MMSYSERR_NOERROR || !h) { t_note("waveInOpen failed %u with %u device(s)", r, wic_nin()); t_skip("cannot open a waveIn device"); return NULL; }
    return h;
}

HWAVEIN wic_closed_handle(void) {
    HWAVEIN h = wic_open_any();
    if (!h) return NULL;
    if (U(waveInClose)(h) != MMSYSERR_NOERROR) { t_skip("waveInClose failed"); return NULL; }
    return h;
}

static char g_mmio_buf[64];
HMMIO wic_mmio_open(void) {
    if (!U(mmioOpenA)) return NULL;
    MMIOINFO mi; memset(&mi, 0, sizeof(mi));
    mi.fccIOProc = FOURCC_MEM;
    mi.pchBuffer = g_mmio_buf;
    mi.cchBuffer = sizeof(g_mmio_buf);
    return U(mmioOpenA)(NULL, &mi, MMIO_READWRITE);
}
void wic_mmio_close(HMMIO h) { if (h && U(mmioClose)) U(mmioClose)(h, 0); }

HWAVEOUT wic_waveout_open(void) {
    if (!wic_nout() || !U(waveOutOpen)) return NULL;
    WAVEFORMATEX f; wic_pcm(&f, 44100, 16, 2);
    HWAVEOUT h = NULL;
    if (U(waveOutOpen)(&h, WAVE_MAPPER, &f, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) return NULL;
    return h;
}

void wic_hdr(WAVEHDR *h, void *buf, DWORD len, DWORD flags) {
    memset(h, 0, sizeof(*h));
    h->lpData = (LPSTR)buf;
    h->dwBufferLength = len;
    h->dwFlags = flags;
}
