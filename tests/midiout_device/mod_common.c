#include "mod.h"

const BYTE mod_gs_reset[11] = { 0xF0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7F, 0x00, 0x41, 0xF7 };
const BYTE mod_gm_on[6]     = { 0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7 };
const BYTE mod_gm_off[6]    = { 0xF0, 0x7E, 0x7F, 0x09, 0x02, 0xF7 };

static UINT g_nout = (UINT)-1, g_nwave = (UINT)-1;

UINT mod_nout(void) {
    if (g_nout == (UINT)-1) {
        if (!U(midiOutGetNumDevs)) return 0;
        g_nout = U(midiOutGetNumDevs)();
    }
    return g_nout;
}

UINT mod_nwave(void) {
    if (g_nwave == (UINT)-1) {
        if (!U(waveOutGetNumDevs)) return 0;
        g_nwave = U(waveOutGetNumDevs)();
    }
    return g_nwave;
}

int mod_live(void) { return mod_nwave() > 0 && mod_nout() > 0; }

double mod_now(void) {
    static LARGE_INTEGER f;
    LARGE_INTEGER c;
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1000.0 / (double)f.QuadPart;
}

/* ---------------------------------------------------------------- open bookkeeping */

#define MOD_MAXOPEN 8
static HMIDIOUT g_open[MOD_MAXOPEN];
static int g_nopen;

static void track_add(HMIDIOUT h) {
    if (g_nopen < MOD_MAXOPEN) g_open[g_nopen++] = h;
}
static void track_del(HMIDIOUT h) {
    for (int i = 0; i < g_nopen; i++)
        if (g_open[i] == h) { g_open[i] = g_open[--g_nopen]; return; }
}
int mod_open_count(void) { return g_nopen; }
void mod_forget(HMIDIOUT h) { track_del(h); }

MMRESULT mod_open(HMIDIOUT *ph, UINT id, DWORD_PTR cb, DWORD_PTR inst, DWORD flags) {
    MMRESULT r = U(midiOutOpen)(ph, id, cb, inst, flags);
    if (r == MMSYSERR_NOERROR && ph && *ph) track_add(*ph);
    return r;
}

HMIDIOUT mod_open_null(UINT id) {
    HMIDIOUT h = NULL;
    CHECKF(g_nopen == 0, "a handle of an earlier test is still open (%d)", g_nopen);
    MMRESULT r = mod_open(&h, id, 0, 0, CALLBACK_NULL);
    CHECKF(r == MMSYSERR_NOERROR, "midiOutOpen(0x%x) = %u", id, r);
    if (r != MMSYSERR_NOERROR) return NULL;
    return h;
}

void mod_quiet(HMIDIOUT h) {
    for (DWORD ch = 0; ch < 16; ch++) {
        U(midiOutShortMsg)(h, MOD_MSG(0xB0 | ch, 123, 0));  /* all notes off */
        U(midiOutShortMsg)(h, MOD_MSG(0xB0 | ch, 120, 0));  /* all sound off */
    }
}

void mod_close(HMIDIOUT h) {
    if (!h) return;
    mod_quiet(h);
    U(midiOutReset)(h);
    MMRESULT r = U(midiOutClose)(h);
    CHECKF(r == MMSYSERR_NOERROR, "midiOutClose(%p) = %u", h, r);
    if (r == MMSYSERR_NOERROR) track_del(h);
}

/* ---------------------------------------------------------------- callback recorder */

static mod_rec_t g_rec[MOD_MAXREC];
static volatile LONG g_nrec;

void CALLBACK mod_cb(HMIDIOUT h, UINT msg, DWORD_PTR inst, DWORD_PTR p1, DWORD_PTR p2) {
    LONG i = InterlockedIncrement(&g_nrec) - 1;
    if (i >= MOD_MAXREC) return;
    mod_rec_t *r = &g_rec[i];
    r->h = h; r->msg = msg; r->inst = inst; r->p1 = p1; r->p2 = p2;
    r->tid = GetCurrentThreadId(); r->t = mod_now();
    r->hflags = (msg == MOM_DONE && p1) ? ((MIDIHDR *)p1)->dwFlags : 0;
    MemoryBarrier();
}

void mod_rec_reset(void) { InterlockedExchange(&g_nrec, 0); memset(g_rec, 0, sizeof(g_rec)); }
int  mod_rec_count(void) { LONG n = g_nrec; return n > MOD_MAXREC ? MOD_MAXREC : (int)n; }
const mod_rec_t *mod_rec(int i) { return &g_rec[i]; }
int  mod_rec_count_msg(UINT msg) {
    int n = 0, c = mod_rec_count();
    for (int i = 0; i < c; i++) if (g_rec[i].msg == msg) n++;
    return n;
}
int mod_wait_msg(UINT msg, int n, DWORD timeout_ms) {
    double end = mod_now() + timeout_ms;
    for (;;) {
        if (mod_rec_count_msg(msg) >= n) return 1;
        if (mod_now() > end) return 0;
        Sleep(5);
    }
}

/* ---------------------------------------------------------------- message pumps */

int mod_pump_count(HWND w, UINT msg, DWORD timeout_ms, int want, WPARAM *wp, LPARAM *lp) {
    if (!U(PeekMessageA)) return 0;
    int got = 0;
    double end = mod_now() + timeout_ms;
    MSG m;
    for (;;) {
        while (U(PeekMessageA)(&m, w, msg, msg, PM_REMOVE)) {
            if (!got) { if (wp) *wp = m.wParam; if (lp) *lp = m.lParam; }
            got++;
        }
        if (got >= want || mod_now() > end) return got;
        Sleep(5);
    }
}

void mod_pump_drain(HWND w) {
    if (!U(PeekMessageA)) return;
    MSG m;
    for (int i = 0; i < 1000 && U(PeekMessageA)(&m, w, MM_MOM_OPEN, MM_MOM_DONE, PM_REMOVE); i++) {}
}

HWND mod_window(void) {
    if (!U(CreateWindowExA)) return NULL;
    return U(CreateWindowExA)(0, "STATIC", "midiout_device", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, NULL, NULL);
}

/* ---------------------------------------------------------------- headers */

void mod_hdr(MIDIHDR *m, const void *buf, DWORD len) {
    memset(m, 0, sizeof(*m));
    m->lpData = (LPSTR)buf;
    m->dwBufferLength = len;
}

int mod_wait_done(volatile MIDIHDR *m, DWORD timeout_ms) {
    double end = mod_now() + timeout_ms;
    for (;;) {
        if (m->dwFlags & MHDR_DONE) return 1;
        if (mod_now() > end) return 0;
        Sleep(5);
    }
}

MMRESULT mod_send_long(HMIDIOUT h, const void *buf, DWORD len) {
    MIDIHDR m;
    mod_hdr(&m, buf, len);
    MMRESULT r = U(midiOutPrepareHeader)(h, &m, sizeof(m));
    CHECKF(r == MMSYSERR_NOERROR, "prepare = %u", r);
    if (r) return r;
    MMRESULT rl = U(midiOutLongMsg)(h, &m, sizeof(m));
    if (rl == MMSYSERR_NOERROR) CHECKF(mod_wait_done(&m, 2000), "no MHDR_DONE in 2 s (flags 0x%lx)", m.dwFlags);
    r = U(midiOutUnprepareHeader)(h, &m, sizeof(m));
    CHECKF(r == MMSYSERR_NOERROR, "unprepare = %u (flags 0x%lx)", r, m.dwFlags);
    if (r == MIDIERR_STILLPLAYING) {   /* never leave a queued header behind */
        U(midiOutReset)(h);
        mod_wait_done(&m, 2000);
        U(midiOutUnprepareHeader)(h, &m, sizeof(m));
    }
    return rl;
}

MMRESULT mod_note(HMIDIOUT h, int ch, int note, int vel, DWORD ms) {
    if (vel > 20) vel = 20;
    if (ms > 100) ms = 100;
    MMRESULT r = U(midiOutShortMsg)(h, MOD_MSG(0x90 | (ch & 15), note & 0x7F, vel));
    Sleep(ms);
    U(midiOutShortMsg)(h, MOD_MSG(0x80 | (ch & 15), note & 0x7F, 0));
    return r;
}

/* Recorded once per run: the configuration this exe saw. */
TEST(midiout_device_environment) {
    REQUIRE(U(midiOutGetNumDevs)); REQUIRE(U(waveOutGetNumDevs));
    UINT n = mod_nout(), w = mod_nwave();
    t_note("MODENV midiout=%u waveout=%u live=%d", n, w, mod_live());
    /* the GS synth is enumerated with or without an audio endpoint [VM] */
    CHECKF(n >= 1, "midiOutGetNumDevs=%u", n);
    EXPECT_EQ(U(midiOutGetNumDevs)(), n);
    EXPECT_EQ(U(waveOutGetNumDevs)(), w);
}
