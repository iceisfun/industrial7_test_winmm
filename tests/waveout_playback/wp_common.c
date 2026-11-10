#include "wp.h"
#include <math.h>

static UINT g_nout = (UINT)-1, g_nin = (UINT)-1;
static int g_listed;

UINT wp_nout(void) {
    if (g_nout == (UINT)-1) g_nout = U(waveOutGetNumDevs) ? U(waveOutGetNumDevs)() : 0;
    return g_nout;
}
UINT wp_nin(void) {
    if (g_nin == (UINT)-1) g_nin = U(waveInGetNumDevs) ? U(waveInGetNumDevs)() : 0;
    return g_nin;
}

int wp_have_dev(void) {
    UINT n = wp_nout();
    if (!g_listed) {
        g_listed = 1;
        t_note("waveOut devices: %u", n);
        if (U(waveOutGetDevCapsA)) {
            WAVEOUTCAPSA c;
            for (UINT i = 0; i < n; i++) {
                memset(&c, 0, sizeof c);
                if (U(waveOutGetDevCapsA)(i, &c, sizeof c) == MMSYSERR_NOERROR)
                    t_note("waveOut %u: \"%s\" mid %u pid %u support 0x%lx", i, c.szPname, c.wMid, c.wPid, c.dwSupport);
            }
            memset(&c, 0, sizeof c);
            if (n && U(waveOutGetDevCapsA)(WAVE_MAPPER, &c, sizeof c) == MMSYSERR_NOERROR)
                t_note("waveOut mapper: \"%s\"", c.szPname);
        }
    }
    return n > 0;
}

int wp_need_dev(void) {
    if (wp_have_dev()) return 1;
    /* No-device contract (docs/VM_AUDIO.md, VM): opening the mapper or id 0 fails with BADDEVICEID. */
    WAVEFORMATEX wf; wp_fmt_std(&wf);
    HWAVEOUT h = NULL;
    if (U(waveOutOpen)) {
        EXPECT_EQ(U(waveOutOpen)(&h, WAVE_MAPPER, &wf, 0, 0, CALLBACK_NULL), MMSYSERR_BADDEVICEID);
        EXPECT_EQ(U(waveOutOpen)(&h, 0, &wf, 0, 0, CALLBACK_NULL), MMSYSERR_BADDEVICEID);
    }
    t_skip("no waveOut device: device half not run");
    return 0;
}

void wp_fmt(WAVEFORMATEX *wf, DWORD rate, WORD ch, WORD bits) {
    memset(wf, 0, sizeof *wf);
    wf->wFormatTag = WAVE_FORMAT_PCM;
    wf->nChannels = ch;
    wf->nSamplesPerSec = rate;
    wf->wBitsPerSample = bits;
    wf->nBlockAlign = (WORD)(ch * bits / 8);
    wf->nAvgBytesPerSec = rate * wf->nBlockAlign;
    wf->cbSize = 0;
}
void wp_fmt_std(WAVEFORMATEX *wf) { wp_fmt(wf, WP_RATE, WP_CH, WP_BITS); }

MMRESULT wp_open_ex(wp_dev *d, UINT id, const WAVEFORMATEX *wf) {
    memset(d, 0, sizeof *d);
    d->wf = *wf;
    d->id = id;
    d->ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    MMRESULT r = U(waveOutOpen)(&d->h, id, &d->wf, (DWORD_PTR)d->ev, 0, CALLBACK_EVENT);
    if (r != MMSYSERR_NOERROR) { CloseHandle(d->ev); d->ev = NULL; d->h = NULL; return r; }
    WaitForSingleObject(d->ev, WP_OPEN_WAIT);   /* WOM_OPEN is signalled before waveOutOpen returns */
    return r;
}
MMRESULT wp_open(wp_dev *d) { WAVEFORMATEX wf; wp_fmt_std(&wf); return wp_open_ex(d, WAVE_MAPPER, &wf); }

MMRESULT wp_close(wp_dev *d) {
    MMRESULT r = MMSYSERR_INVALHANDLE;
    if (d->h) {
        U(waveOutReset)(d->h);
        r = U(waveOutClose)(d->h);
        d->h = NULL;
    }
    if (d->ev) { CloseHandle(d->ev); d->ev = NULL; }
    return r;
}

void wp_hdr_bytes(WAVEHDR *wh, DWORD bytes) {
    memset(wh, 0, sizeof *wh);
    wh->lpData = (LPSTR)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes ? bytes : 1);
    wh->dwBufferLength = bytes;
}
void wp_hdr_ms(WAVEHDR *wh, const wp_dev *d, DWORD ms) {
    DWORD frames = (DWORD)((DWORDLONG)ms * d->wf.nSamplesPerSec / 1000);
    wp_hdr_bytes(wh, frames * d->wf.nBlockAlign);
    /* 8-bit PCM silence is 0x80 */
    if (d->wf.wBitsPerSample == 8) memset(wh->lpData, 0x80, wh->dwBufferLength);
}
void wp_fill_tone(WAVEHDR *wh, const wp_dev *d, int hz) {
    if (d->wf.wBitsPerSample != 16) return;
    DWORD ch = d->wf.nChannels, frames = wh->dwBufferLength / d->wf.nBlockAlign, rate = d->wf.nSamplesPerSec;
    short *p = (short *)wh->lpData;
    double ramp = rate / 200.0, amp = 0.0316 * 32767;   /* -30 dBFS peak */
    for (DWORD i = 0; i < frames; i++) {
        double env = 1.0;
        if (i < ramp) env = i / ramp; else if (i > frames - ramp) env = (frames - i) / ramp;
        short s = (short)(amp * env * sin(6.283185307179586 * hz * i / rate));
        for (DWORD c = 0; c < ch; c++) p[i * ch + c] = s;
    }
}
void wp_free_hdr(WAVEHDR *wh) {
    if (wh->lpData) HeapFree(GetProcessHeap(), 0, wh->lpData);
    wh->lpData = NULL;
}

MMRESULT wp_prep(wp_dev *d, WAVEHDR *wh) { return U(waveOutPrepareHeader)(d->h, wh, sizeof *wh); }
MMRESULT wp_write(wp_dev *d, WAVEHDR *wh) { return U(waveOutWrite)(d->h, wh, sizeof *wh); }
MMRESULT wp_unprep(wp_dev *d, WAVEHDR *wh) { return U(waveOutUnprepareHeader)(d->h, wh, sizeof *wh); }

int wp_wait_all(wp_dev *d, WAVEHDR *hs, int n, DWORD ms) {
    DWORD t0 = GetTickCount();
    for (;;) {
        int all = 1;
        for (int i = 0; i < n; i++) if (!(hs[i].dwFlags & WHDR_DONE)) { all = 0; break; }
        if (all) return 1;
        if (GetTickCount() - t0 > ms) return 0;
        if (d && d->ev) WaitForSingleObject(d->ev, 20); else Sleep(5);
    }
}
int wp_wait_done(wp_dev *d, WAVEHDR *wh, DWORD ms) { return wp_wait_all(d, wh, 1, ms); }

MMRESULT wp_pos(HWAVEOUT h, UINT type, DWORD *val, UINT *type_out) {
    MMTIME mt;
    memset(&mt, 0, sizeof mt);
    mt.wType = type;
    MMRESULT r = U(waveOutGetPosition)(h, &mt, sizeof mt);
    if (val) *val = mt.u.cb;
    if (type_out) *type_out = mt.wType;
    return r;
}
DWORD wp_samples(HWAVEOUT h) { DWORD v; UINT t; return wp_pos(h, TIME_SAMPLES, &v, &t) == 0 && t == TIME_SAMPLES ? v : 0xFFFFFFFF; }
DWORD wp_bytes(HWAVEOUT h) { DWORD v; UINT t; return wp_pos(h, TIME_BYTES, &v, &t) == 0 && t == TIME_BYTES ? v : 0xFFFFFFFF; }

HWAVEOUT wp_closed_handle(void) {
    if (!wp_have_dev()) return NULL;
    wp_dev d;
    if (wp_open(&d) != MMSYSERR_NOERROR) return NULL;
    HWAVEOUT h = d.h;
    if (wp_close(&d) != MMSYSERR_NOERROR) return NULL;
    return h;
}

static int g_static_word;
int wp_bad_handles(wp_badh *o, int cap) {
    static char heap_note;
    static void *heapblk;
    int n = 0;
    if (!heapblk) heapblk = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 256);
#define ADD(nm, v) do { if (n < cap) { o[n].name = nm; o[n].h = (HWAVEOUT)(ULONG_PTR)(v); n++; } } while (0)
    ADD("NULL", 0);
    ADD("1", 1);
    ADD("0xFFFF", 0xFFFF);
    ADD("0x10000", 0x10000);
    ADD("0xFFFFFFFF", 0xFFFFFFFFu);
    ADD("all-ones", ~(ULONG_PTR)0);
    ADD("static-addr", &g_static_word);
    ADD("heap-block+0x50", (char *)heapblk + 0x50);
    ADD("module-base", t_module("winmm.dll"));
    ADD("pseudo-thread", GetCurrentThread());   /* -2 */
#undef ADD
    (void)heap_note;
    return n;
}
