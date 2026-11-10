/* waveout_header_protocol: shared helper implementations (no TESTs here). */
#include "whp.h"

const char *const whp_fn_name[WHP_NFN] = { "waveOutPrepareHeader", "waveOutUnprepareHeader", "waveOutWrite" };

whp_hdr_fn whp_fn(int which)
{
    switch (which) {
    case WHP_PREP:   return (whp_hdr_fn)U(waveOutPrepareHeader);
    case WHP_UNPREP: return (whp_hdr_fn)U(waveOutUnprepareHeader);
    case WHP_WRITE:  return (whp_hdr_fn)U(waveOutWrite);
    }
    return NULL;
}

int whp_have_all(void)
{
    return whp_fn(WHP_PREP) && whp_fn(WHP_UNPREP) && whp_fn(WHP_WRITE);
}

const ULONG_PTR whp_bad_handles[WHP_NBAD] = {
    0, 1, 0xFFFF, 0x1234, (ULONG_PTR)-1, 0xFFFFFFFFu,
};

void whp_hdr_init(WAVEHDR *h, DWORD flags, char *buf, DWORD len)
{
    memset(h, 0, sizeof *h);
    h->lpData = buf;
    h->dwBufferLength = len;
    h->dwBytesRecorded = 0x5A5A0001u;
    h->dwUser = (DWORD_PTR)0x11223344u;
    h->dwFlags = flags;
    h->dwLoops = 0;
    h->lpNext = NULL;
    h->reserved = 0;
}

int whp_hdr_same_except_flags(const WAVEHDR *a, const WAVEHDR *b)
{
    return a->lpData == b->lpData && a->dwBufferLength == b->dwBufferLength &&
           a->dwBytesRecorded == b->dwBytesRecorded && a->dwUser == b->dwUser &&
           a->dwLoops == b->dwLoops && a->lpNext == b->lpNext && a->reserved == b->reserved;
}

UINT whp_numdevs(void)
{
    static int done; static UINT n;
    if (!done) {
        __typeof__(&waveOutGetNumDevs) f = U(waveOutGetNumDevs);
        n = f ? f() : 0;
        done = 1;
        t_note("waveOutGetNumDevs=%u", n);
    }
    return n;
}

void whp_fmt(WAVEFORMATEX *f, DWORD rate, WORD bits, WORD ch)
{
    memset(f, 0, sizeof *f);
    f->wFormatTag = WAVE_FORMAT_PCM;
    f->nChannels = ch;
    f->nSamplesPerSec = rate;
    f->wBitsPerSample = bits;
    f->nBlockAlign = (WORD)(ch * bits / 8);
    f->nAvgBytesPerSec = rate * f->nBlockAlign;
    f->cbSize = 0;
}

MMRESULT whp_open_fmt(UINT id, const WAVEFORMATEX *f, HWAVEOUT *ph, HANDLE *ev)
{
    __typeof__(&waveOutOpen) op = U(waveOutOpen);
    if (!op) return MMSYSERR_NOTSUPPORTED;
    if (!*ev) *ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    *ph = NULL;
    MMRESULT r = op(ph, id, f, (DWORD_PTR)*ev, 0, CALLBACK_EVENT);
    if (r == MMSYSERR_NOERROR) WaitForSingleObject(*ev, 1000); /* WOM_OPEN signal */
    return r;
}

MMRESULT whp_open(UINT id, HWAVEOUT *ph, HANDLE *ev)
{
    WAVEFORMATEX f; whp_fmt(&f, 44100, 16, 2);
    return whp_open_fmt(id, &f, ph, ev);
}

int whp_need_dev(HWAVEOUT *ph, HANDLE *ev)
{
    *ph = NULL; *ev = NULL;
    if (!whp_have_all() || !U(waveOutOpen) || !U(waveOutClose) || !U(waveOutReset)) { t_skip("missing exports"); return 0; }
    if (whp_numdevs() == 0) { t_skip("no waveOut device (contract half covers this configuration)"); return 0; }
    MMRESULT r = whp_open(WAVE_MAPPER, ph, ev);
    if (r != MMSYSERR_NOERROR) {
        t_note("waveOutOpen(WAVE_MAPPER, 44100/16/2) = %u with %u devices", r, whp_numdevs());
        CHECKF(r == MMSYSERR_NOERROR, "mapper open failed with devices present: %u", r);
        if (*ev) { CloseHandle(*ev); *ev = NULL; }
        t_skip("cannot open the mapper");
        return 0;
    }
    return 1;
}

void whp_close(HWAVEOUT h, HANDLE ev)
{
    if (h) {
        U(waveOutReset)(h);
        MMRESULT r = U(waveOutClose)(h);
        if (r != MMSYSERR_NOERROR) t_note("cleanup waveOutClose = %u", r);
    }
    if (ev) CloseHandle(ev);
}

int whp_wait_done(WAVEHDR *h, HANDLE ev, DWORD ms)
{
    DWORD t0 = GetTickCount();
    for (;;) {
        if (((volatile WAVEHDR *)h)->dwFlags & WHDR_DONE) return 1;
        if (GetTickCount() - t0 >= ms) return 0;
        if (ev) WaitForSingleObject(ev, 50); else Sleep(10);
    }
}

int whp_wait_all_done(WAVEHDR *h, int n, HANDLE ev, DWORD ms)
{
    DWORD t0 = GetTickCount();
    for (;;) {
        int all = 1;
        for (int i = 0; i < n; i++) if (!(((volatile WAVEHDR *)&h[i])->dwFlags & WHDR_DONE)) all = 0;
        if (all) return 1;
        if (GetTickCount() - t0 >= ms) return 0;
        if (ev) WaitForSingleObject(ev, 50); else Sleep(10);
    }
}

static char g_silence[2 * 44100 * 4];
char *whp_silence(DWORD ms, DWORD *len)
{
    if (ms > 2000) ms = 2000;
    *len = ms * 44100 / 1000 * 4;
    return g_silence;
}
