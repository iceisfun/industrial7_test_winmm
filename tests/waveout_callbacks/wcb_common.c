/* Shared helpers for the waveout_callbacks area (see wcb.h). VM_TIMEOUT: 240 */
#include "wcb.h"

static UINT g_nout = 0xFFFFFFFF, g_nin = 0xFFFFFFFF;

UINT wcb_nout(void) {
    if (g_nout == 0xFFFFFFFF) g_nout = U(waveOutGetNumDevs) ? U(waveOutGetNumDevs)() : 0;
    return g_nout;
}
UINT wcb_nin(void) {
    if (g_nin == 0xFFFFFFFF) g_nin = U(waveInGetNumDevs) ? U(waveInGetNumDevs)() : 0;
    return g_nin;
}

void wcb_note_devices(void) {
    static int done;
    if (done) return;
    done = 1;
    UINT no = wcb_nout(), ni = wcb_nin();
    t_note("WCBENV waveout=%u wavein=%u", no, ni);
    for (UINT i = 0; i < no && i < 8; i++) {
        WAVEOUTCAPSA c; memset(&c, 0, sizeof c);
        if (U(waveOutGetDevCapsA)(i, &c, sizeof c) == 0) t_note("waveOut %u \"%s\" formats 0x%lx support 0x%lx", i, c.szPname, c.dwFormats, c.dwSupport);
    }
    for (UINT i = 0; i < ni && i < 8; i++) {
        WAVEINCAPSA c; memset(&c, 0, sizeof c);
        if (U(waveInGetDevCapsA)(i, &c, sizeof c) == 0) t_note("waveIn %u \"%s\" formats 0x%lx", i, c.szPname, c.dwFormats);
    }
}

void wcb_fmt(WAVEFORMATEX *f, DWORD rate, WORD bits, WORD ch) {
    memset(f, 0, sizeof *f);
    f->wFormatTag = WAVE_FORMAT_PCM;
    f->nChannels = ch;
    f->nSamplesPerSec = rate;
    f->wBitsPerSample = bits;
    f->nBlockAlign = (WORD)(ch * bits / 8);
    f->nAvgBytesPerSec = rate * f->nBlockAlign;
    f->cbSize = 0;
}
void wcb_fmt_default(WAVEFORMATEX *f) { wcb_fmt(f, 44100, 16, 2); }

DWORD wcb_bytes_for_ms(const WAVEFORMATEX *f, DWORD ms) {
    DWORD frames = (DWORD)((unsigned long long)f->nSamplesPerSec * ms / 1000);
    if (frames == 0) frames = 1;
    return frames * f->nBlockAlign;
}

char *wcb_silence(const WAVEFORMATEX *f, DWORD ms, DWORD *len) {
    DWORD n = wcb_bytes_for_ms(f, ms);
    char *p = (char *)HeapAlloc(GetProcessHeap(), 0, n ? n : 1);
    memset(p, f->wBitsPerSample == 8 ? 0x80 : 0, n);   /* 8-bit PCM is unsigned: 0x80 is the zero level */
    *len = n;
    return p;
}

void wcb_hdr(WAVEHDR *h, char *data, DWORD len) {
    memset(h, 0, sizeof *h);
    h->lpData = data;
    h->dwBufferLength = len;
}

/* ---- recorder ---- */
void wcb_log_init(wcb_log *L) {
    memset(L, 0, sizeof *L);
    L->ev = CreateEventA(NULL, FALSE, FALSE, NULL);
}
void wcb_log_free(wcb_log *L) { if (L->ev) CloseHandle(L->ev); L->ev = NULL; }

static void rec_add(wcb_log *L, UINT msg, DWORD_PTR h, DWORD_PTR inst, DWORD_PTR p1, DWORD_PTR p2) {
    LONG i = InterlockedIncrement(&L->total) - 1;
    if (i < WCB_MAXREC) {
        wcb_rec *r = &L->r[i];
        r->msg = msg; r->h = h; r->inst = inst; r->p1 = p1; r->p2 = p2;
        r->tid = GetCurrentThreadId();
        if ((msg == WOM_DONE || msg == WIM_DATA) && p1) {
            WAVEHDR *wh = (WAVEHDR *)p1;
            r->hdr_flags = wh->dwFlags;
            r->hdr_bytes = wh->dwBytesRecorded;
        }
        r->phandle = L->watch ? *L->watch : 0;
        InterlockedExchange(&L->n, i + 1 > L->n ? i + 1 : L->n);
    }
    if (L->ev) SetEvent(L->ev);
}

int wcb_count(wcb_log *L, UINT msg) {
    int c = 0, n = L->total < WCB_MAXREC ? L->total : WCB_MAXREC;
    for (int i = 0; i < n; i++) if (L->r[i].msg == msg) c++;
    return c;
}
int wcb_count_range(wcb_log *L, UINT lo, UINT hi) {
    int c = 0, n = L->total < WCB_MAXREC ? L->total : WCB_MAXREC;
    for (int i = 0; i < n; i++) if (L->r[i].msg >= lo && L->r[i].msg <= hi) c++;
    return c;
}

int wcb_wait_total(wcb_log *L, LONG want, DWORD ms) {
    DWORD t0 = GetTickCount();
    while (L->total < want) {
        DWORD el = GetTickCount() - t0;
        if (el >= ms) return 0;
        WaitForSingleObject(L->ev, ms - el > 50 ? 50 : ms - el);
    }
    return 1;
}

/* ---- function callback ---- */
#define WCB_MAXREG 8
static wcb_log *volatile g_reg[WCB_MAXREG];
wcb_log *wcb_fn_fallback;

void wcb_fn_register(wcb_log *L, DWORD_PTR inst) {
    L->inst_key = inst;
    for (int i = 0; i < WCB_MAXREG; i++) if (!g_reg[i]) { g_reg[i] = L; return; }
}
void wcb_fn_unregister(wcb_log *L) {
    for (int i = 0; i < WCB_MAXREG; i++) if (g_reg[i] == L) g_reg[i] = NULL;
}
static wcb_log *route(DWORD_PTR inst) {
    for (int i = 0; i < WCB_MAXREG; i++) { wcb_log *L = g_reg[i]; if (L && L->inst_key == inst) return L; }
    return wcb_fn_fallback;
}
/* Only records: no waveOut or waveIn call is made from inside a callback. */
void CALLBACK wcb_fn_out(HWAVEOUT h, UINT msg, DWORD_PTR inst, DWORD_PTR p1, DWORD_PTR p2) {
    wcb_log *L = route(inst);
    if (L) rec_add(L, msg, (DWORD_PTR)h, inst, p1, p2);
}
void CALLBACK wcb_fn_in(HWAVEIN h, UINT msg, DWORD_PTR inst, DWORD_PTR p1, DWORD_PTR p2) {
    wcb_log *L = route(inst);
    if (L) rec_add(L, msg, (DWORD_PTR)h, inst, p1, p2);
}

/* ---- window ---- */
static LRESULT CALLBACK wcb_wndproc(HWND w, UINT m, WPARAM wp, LPARAM lp) { return DefWindowProcA(w, m, wp, lp); }
static ATOM g_cls;

HWND wcb_win_create(void) {
    if (!g_cls) {
        WNDCLASSA wc; memset(&wc, 0, sizeof wc);
        wc.lpfnWndProc = wcb_wndproc;
        wc.hInstance = GetModuleHandleA(NULL);
        wc.lpszClassName = "winmmt_wcb_cls";
        g_cls = RegisterClassA(&wc);
    }
    return CreateWindowExA(0, "winmmt_wcb_cls", "", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, GetModuleHandleA(NULL), NULL);
}

void wcb_win_destroy(HWND w) {
    if (!w) return;
    MSG m;
    for (int i = 0; i < 256 && PeekMessageA(&m, w, 0, 0, PM_REMOVE); i++) {}
    DestroyWindow(w);
}

void wcb_win_pump(HWND w, wcb_log *L, LONG want, DWORD ms) {
    DWORD t0 = GetTickCount();
    for (;;) {
        MSG m;
        while (PeekMessageA(&m, w, 0, 0, PM_REMOVE)) {
            if (m.message >= MM_WOM_OPEN && m.message <= MM_WIM_DATA) rec_add(L, m.message, m.wParam, 0, m.lParam, 0);
            else DispatchMessageA(&m);
        }
        if (want >= 0 && L->total >= want) return;
        DWORD el = GetTickCount() - t0;
        if (el >= ms) return;
        MsgWaitForMultipleObjects(0, NULL, FALSE, ms - el > 20 ? 20 : ms - el, QS_ALLINPUT);
    }
}

int wcb_win_peek_count(HWND w, UINT lo, UINT hi) {
    /* Remove matching messages into a local list, then re-post them in order: PM_NOREMOVE cannot count.
     * Re-posted messages go behind any that arrive meanwhile and lose their time stamps: call it only once the
     * stream has quiesced, before an order check among the same range (as wcb_queued.c does). */
    MSG got[64]; int n = 0;
    while (n < 64 && PeekMessageA(&got[n], w, lo, hi, PM_REMOVE)) n++;
    for (int i = 0; i < n; i++) PostMessageA(w, got[i].message, got[i].wParam, got[i].lParam);
    return n;
}

/* ---- thread ---- */
static DWORD WINAPI thr_main(LPVOID p) {
    wcb_thr *T = (wcb_thr *)p;
    MSG m;
    PeekMessageA(&m, NULL, 0, 0, PM_NOREMOVE);   /* create the queue before announcing the id */
    SetEvent(T->ready);
    for (int guard = 0; guard < 100000; guard++) {
        while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) rec_add(&T->log, m.message, m.wParam, 0, m.lParam, 0);
        DWORD r = MsgWaitForMultipleObjects(1, &T->quit, FALSE, 50, QS_ALLINPUT | QS_ALLPOSTMESSAGE);
        if (r == WAIT_OBJECT_0) {
            while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) rec_add(&T->log, m.message, m.wParam, 0, m.lParam, 0);
            return 0;
        }
    }
    return 0;
}

int wcb_thr_start(wcb_thr *T) {
    memset(T, 0, sizeof *T);
    wcb_log_init(&T->log);
    T->ready = CreateEventA(NULL, TRUE, FALSE, NULL);
    T->quit = CreateEventA(NULL, TRUE, FALSE, NULL);
    T->th = CreateThread(NULL, 0, thr_main, T, 0, &T->tid);
    if (!T->th) return 0;
    return WaitForSingleObject(T->ready, WCB_WAIT_MS) == WAIT_OBJECT_0;
}

void wcb_thr_stop(wcb_thr *T) {
    if (T->th) {
        SetEvent(T->quit);
        if (WaitForSingleObject(T->th, WCB_WAIT_MS) != WAIT_OBJECT_0) t_note("callback worker thread did not stop in time");
        CloseHandle(T->th);
    }
    if (T->ready) CloseHandle(T->ready);
    if (T->quit) CloseHandle(T->quit);
    wcb_log_free(&T->log);
    T->th = NULL;
}

int wcb_thr_wait(wcb_thr *T, UINT lo, UINT hi, int want, DWORD ms) {
    DWORD t0 = GetTickCount();
    for (;;) {
        if (wcb_count_range(&T->log, lo, hi) >= want) return 1;
        DWORD el = GetTickCount() - t0;
        if (el >= ms) return 0;
        WaitForSingleObject(T->log.ev, ms - el > 50 ? 50 : ms - el);
    }
}

int wcb_ev_wait(HANDLE ev, DWORD ms) { return WaitForSingleObject(ev, ms) == WAIT_OBJECT_0; }

int wcb_out_ids(UINT *ids, int cap) {
    int k = 0;
    UINT n = wcb_nout();
    if (n == 0) return 0;
    ids[k++] = WAVE_MAPPER;
    for (UINT i = 0; i < n && k < cap; i++) ids[k++] = i;
    return k;
}
int wcb_in_ids(UINT *ids, int cap) {
    int k = 0;
    UINT n = wcb_nin();
    if (n == 0) return 0;
    ids[k++] = WAVE_MAPPER;
    for (UINT i = 0; i < n && k < cap; i++) ids[k++] = i;
    return k;
}
const char *wcb_idname(UINT id) {
    static char b[4][24]; static int k;
    char *s = b[k++ & 3];
    if (id == WAVE_MAPPER) return "WAVE_MAPPER";
    sprintf(s, "id %u", id);
    return s;
}

int wcb_play_silence_poll(HWAVEOUT h, const WAVEFORMATEX *f, int nbuf, DWORD ms) {
    WAVEHDR hd[16]; char *d[16]; DWORD len;
    if (nbuf > 16) nbuf = 16;
    for (int i = 0; i < nbuf; i++) {
        d[i] = wcb_silence(f, ms, &len);
        wcb_hdr(&hd[i], d[i], len);
        U(waveOutPrepareHeader)(h, &hd[i], sizeof hd[i]);
        U(waveOutWrite)(h, &hd[i], sizeof hd[i]);
    }
    DWORD t0 = GetTickCount();
    int done = 0;
    while (GetTickCount() - t0 < WCB_WAIT_MS) {
        done = 0;
        for (int i = 0; i < nbuf; i++) if (hd[i].dwFlags & WHDR_DONE) done++;
        if (done == nbuf) break;
        Sleep(10);
    }
    if (done != nbuf) U(waveOutReset)(h);
    for (int i = 0; i < nbuf; i++) {
        U(waveOutUnprepareHeader)(h, &hd[i], sizeof hd[i]);
        HeapFree(GetProcessHeap(), 0, d[i]);
    }
    return done;
}

int wcb_check_done_order(wcb_log *L, UINT m, WAVEHDR *hdrs, int n) {
    int k = 0, ok = 1, cnt = L->total < WCB_MAXREC ? L->total : WCB_MAXREC;
    for (int i = 0; i < cnt; i++) {
        if (L->r[i].msg != m) continue;
        if (k >= n || L->r[i].p1 != (DWORD_PTR)&hdrs[k]) ok = 0;
        k++;
    }
    return ok && k == n;
}

const char *wcb_msgname(UINT m) {
    switch (m) {
    case MM_WOM_OPEN: return "WOM_OPEN"; case MM_WOM_CLOSE: return "WOM_CLOSE"; case MM_WOM_DONE: return "WOM_DONE";
    case MM_WIM_OPEN: return "WIM_OPEN"; case MM_WIM_CLOSE: return "WIM_CLOSE"; case MM_WIM_DATA: return "WIM_DATA";
    case WM_USER: return "WM_USER";
    default: return "?";
    }
}
