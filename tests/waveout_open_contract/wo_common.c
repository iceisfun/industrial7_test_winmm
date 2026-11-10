#include "wo.h"
/* Helper bodies for the waveout_open_contract area. No TESTs here. VM_TIMEOUT: 180 */

static UINT g_ndev = 0xFFFFFFFF, g_nin = 0xFFFFFFFF;

UINT wo_ndev(void) {
    if (g_ndev == 0xFFFFFFFF) {
        g_ndev = U(waveOutGetNumDevs) ? U(waveOutGetNumDevs)() : 0;
        t_note("waveOutGetNumDevs = %u", g_ndev);
    }
    return g_ndev;
}
UINT wo_nwavein(void) {
    if (g_nin == 0xFFFFFFFF) g_nin = U(waveInGetNumDevs) ? U(waveInGetNumDevs)() : 0;
    return g_nin;
}
const char *wo_capsname(UINT id) {
    static char name[8][MAXPNAMELEN + 1]; static int slot;
    char *s = name[slot++ & 7];
    WAVEOUTCAPSA c; s[0] = 0;
    if (U(waveOutGetDevCapsA) && U(waveOutGetDevCapsA)(id, &c, sizeof c) == MMSYSERR_NOERROR) {
        memcpy(s, c.szPname, MAXPNAMELEN); s[MAXPNAMELEN] = 0;
    }
    return s;
}

void wo_pcm(WAVEFORMATEX *wf, WORD ch, DWORD rate, WORD bits) {
    memset(wf, 0, sizeof *wf);
    wf->wFormatTag = WAVE_FORMAT_PCM; wf->nChannels = ch; wf->nSamplesPerSec = rate; wf->wBitsPerSample = bits;
    wf->nBlockAlign = (WORD)(ch * bits / 8); wf->nAvgBytesPerSec = rate * wf->nBlockAlign; wf->cbSize = 0;
}
void wo_std(WAVEFORMATEX *wf) { wo_pcm(wf, 2, 44100, 16); }

static const GUID wo_ks_pcm   = { 0x00000001, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
static const GUID wo_ks_float = { 0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
void wo_ext(WAVEFORMATEXTENSIBLE *x, WORD ch, DWORD rate, WORD bits, WORD validbits, DWORD mask, int is_float) {
    memset(x, 0, sizeof *x);
    x->Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE; x->Format.nChannels = ch; x->Format.nSamplesPerSec = rate;
    x->Format.wBitsPerSample = bits; x->Format.nBlockAlign = (WORD)(ch * bits / 8);
    x->Format.nAvgBytesPerSec = rate * x->Format.nBlockAlign; x->Format.cbSize = 22;
    x->Samples.wValidBitsPerSample = validbits; x->dwChannelMask = mask;
    x->SubFormat = is_float ? wo_ks_float : wo_ks_pcm;
}

MMRESULT wo_open(HWAVEOUT *ph, UINT id, const WAVEFORMATEX *wf, DWORD_PTR cb, DWORD_PTR inst, DWORD fl) {
    return U(waveOutOpen)(ph, id, wf, cb, inst, fl);
}
MMRESULT wo_close(HWAVEOUT h) { return U(waveOutClose)(h); }
MMRESULT wo_getid(HWAVEOUT h, UINT *id) { return U(waveOutGetID)(h, id); }
MMRESULT wo_open_std(HWAVEOUT *ph, UINT id, DWORD fl) {
    WAVEFORMATEX wf; wo_std(&wf);
    return wo_open(ph, id, &wf, 0, 0, fl);
}
void wo_release(MMRESULT r, DWORD fl, HWAVEOUT h) {
    if (r == MMSYSERR_NOERROR && !(fl & WAVE_FORMAT_QUERY) && h) {
        MMRESULT c = wo_close(h);
        CHECKF(c == MMSYSERR_NOERROR, "close of a handle the test opened returned %u", c);
    }
}

UINT wo_gate(UINT id, int pwfx_null, DWORD_PTR cb, DWORD fl, int phwo_null, int cb_is_window) {
    if (pwfx_null) return MMSYSERR_INVALPARAM;
    if (cb && ((fl >> 16) & 7) == 1 && !cb_is_window) return MMSYSERR_INVALPARAM;
    if (id == 0xFFFF) id = WAVE_MAPPER;
    if (fl & (id == WAVE_MAPPER ? WO_MASK_MAPPER : WO_MASK_DEVICE)) return MMSYSERR_INVALFLAG;
    if (!(fl & WAVE_FORMAT_QUERY) && phwo_null) return MMSYSERR_INVALPARAM;
    return WO_UNSET;
}
UINT wo_expect_nodriver(UINT id, int pwfx_null, DWORD_PTR cb, DWORD fl, int phwo_null, int cb_is_window) {
    UINT g = wo_gate(id, pwfx_null, cb, fl, phwo_null, cb_is_window);
    if (g != WO_UNSET) return g;
    if (wo_ndev() == 0) return MMSYSERR_BADDEVICEID;
    if (id == 0xFFFF || id == WAVE_MAPPER) return WO_UNSET;
    if (fl & WAVE_MAPPED) return WO_UNSET;
    if (id >= wo_ndev()) return MMSYSERR_BADDEVICEID;
    return WO_UNSET;
}

/* ---- user32 ---- */
HWND wo_msgwnd(void) {
    if (!U(CreateWindowExA)) return NULL;
    return U(CreateWindowExA)(0, "STATIC", "wo_open_contract", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, NULL, NULL);
}
void wo_destroy(HWND w) { if (w && U(DestroyWindow)) U(DestroyWindow)(w); }
BOOL wo_iswindow(HWND w) { return U(IsWindow) ? U(IsWindow)(w) : FALSE; }
int wo_wait_msg(UINT msg, DWORD ms, MSG *out) {
    DWORD t0 = GetTickCount();
    MSG m;
    for (;;) {
        while (U(PeekMessageA)(&m, NULL, msg, msg, PM_REMOVE)) { if (out) *out = m; return 1; }
        if (GetTickCount() - t0 >= ms) return 0;
        if (U(MsgWaitForMultipleObjects)) U(MsgWaitForMultipleObjects)(0, NULL, FALSE, 20, QS_ALLINPUT); else Sleep(10);
    }
}
void wo_drain(void) {
    MSG m; int n = 0;
    while (n++ < 1000 && U(PeekMessageA)(&m, NULL, 0, 0, PM_REMOVE)) { }
}

/* ---- callback recorder ---- */
void CALLBACK wo_rec_cb(HWAVEOUT h, UINT msg, DWORD_PTR inst, DWORD_PTR p1, DWORD_PTR p2) {
    wo_rec *r = (wo_rec *)inst;
    if (!r) return;
    LONG i = InterlockedIncrement(&r->n) - 1;
    if (i < 16) { r->msg[i] = msg; r->h[i] = h; r->inst[i] = inst; r->p1[i] = p1; r->tid[i] = GetCurrentThreadId(); }
}
int wo_rec_wait(wo_rec *r, LONG n, DWORD ms) {
    DWORD t0 = GetTickCount();
    while (r->n < n) { if (GetTickCount() - t0 >= ms) return 0; Sleep(5); }
    return 1;
}

DWORD wo_on_thread(DWORD (WINAPI *fn)(void *), void *arg, DWORD ms) {
    HANDLE th = CreateThread(NULL, 0, fn, arg, 0, NULL);
    DWORD code = 0xDEAD0000;
    if (!th) return code;
    if (WaitForSingleObject(th, ms) == WAIT_OBJECT_0) GetExitCodeThread(th, &code); else code = 0xDEAD0001;
    CloseHandle(th);
    return code;
}

int wo_nodev_contract(DWORD fl) {
    if (wo_ndev() != 0) return 0;
    WAVEFORMATEX wf; wo_std(&wf);
    static const UINT ids[] = { 0, 1, WAVE_MAPPER, 0xFFFF };
    for (unsigned i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        DWORD f = fl;
        UINT id = ids[i];
        if ((id == WAVE_MAPPER || id == 0xFFFF) ? (f & WO_MASK_MAPPER) : (f & WO_MASK_DEVICE)) continue;
        HWAVEOUT h = WO_SENTINEL;
        MMRESULT r = wo_open(&h, id, &wf, 0, 0, f);
        CHECKF(r == MMSYSERR_BADDEVICEID, "zero devices: id 0x%x flags 0x%lx -> %u, want 2", id, f, r);
        HWAVEOUT after = h;
        if (f & WAVE_FORMAT_QUERY) CHECKF(after == WO_SENTINEL, "zero devices: QUERY wrote *phwo (%p)", after);
        else CHECKF(after == NULL, "zero devices: *phwo %p after failed open, want NULL", after);
        if (r == MMSYSERR_NOERROR && !(f & WAVE_FORMAT_QUERY)) wo_close(h);
    }
    return 1;
}
