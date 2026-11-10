#include "mh.h"
/* Helpers shared by the mme_handles_threads files (no TESTs here). */

const mh_badv MH_BAD[] = {
    { "NULL", 0 },
    { "1", 1 },
    { "2", 2 },
    { "0x8000", 0x8000 },
    { "0xFFFF", 0xFFFF },
    { "-1", (ULONG_PTR)-1 },
    { "0xFFFFFFFF", 0xFFFFFFFFu },
    { "-2", (ULONG_PTR)-2 },
};
const int MH_NBAD = sizeof MH_BAD / sizeof MH_BAD[0];

void mh_expect_bad(const mh_op *ops, int n, ULONG_PTR h, const char *tag) {
    for (int i = 0; i < n; i++) {
        ULONG_PTR r = ops[i].call(h);
        CHECKF(r == ops[i].bad, "%s(%s=0x%llx) -> 0x%llx, want 0x%llx", ops[i].name, tag, (unsigned long long)h,
               (unsigned long long)r, (unsigned long long)ops[i].bad);
    }
}

void mh_expect_all_bad(const mh_op *ops, int n) {
    for (int b = 0; b < MH_NBAD; b++) mh_expect_bad(ops, n, MH_BAD[b].v, MH_BAD[b].name);
}

void mh_op_contract(const mh_op *ops, int n, const char *name) {
    const mh_op *op = NULL;
    for (int i = 0; i < n; i++) if (!strcmp(ops[i].name, name)) op = &ops[i];
    if (!op) { CHECKF(0, "no op named %s", name); return; }
    char fn[64]; int k = 0;
    while (name[k] && name[k] != '(' && k < 63) { fn[k] = name[k]; k++; }
    fn[k] = 0;
    if (!t_proc(fn)) { t_skip("missing export: %s", fn); return; }
    if (!U(mmioOpenA) || !U(mmioClose)) { t_skip("missing mmio exports"); return; }
    mh_expect_bad(op, 1, 0, "NULL");
    for (int b = 0; b < MH_NBAD; b++) mh_expect_bad(op, 1, MH_BAD[b].v, MH_BAD[b].name);
    ULONG_PTR dead = mh_closed_mmio();
    if (dead) mh_expect_bad(op, 1, dead, "freed HMMIO");
    if (!(op->accept_mask & (1u << MHT_MMIO))) {
        HMMIO m = mh_mmio_open();
        if (m) {
            mh_expect_bad(op, 1, (ULONG_PTR)m, "live HMMIO");
            EXPECT_EQ(U(mmioClose)(m, 0), 0);   /* still a valid mmio handle after the foreign call */
        }
    }
}

static mh_counts g_counts; static int g_counts_ok;
const mh_counts *mh_devs(void) {
    if (!g_counts_ok) {
        g_counts.wo = U(waveOutGetNumDevs) ? U(waveOutGetNumDevs)() : 0;
        g_counts.wi = U(waveInGetNumDevs) ? U(waveInGetNumDevs)() : 0;
        g_counts.mo = U(midiOutGetNumDevs) ? U(midiOutGetNumDevs)() : 0;
        g_counts.mi = U(midiInGetNumDevs) ? U(midiInGetNumDevs)() : 0;
        g_counts.mx = U(mixerGetNumDevs) ? U(mixerGetNumDevs)() : 0;
        g_counts_ok = 1;
    }
    return &g_counts;
}

static char g_membuf[16][256];
static LONG g_memslot;
HMMIO mh_mmio_open(void) {
    if (!U(mmioOpenA)) return NULL;
    MMIOINFO mi; memset(&mi, 0, sizeof mi);
    mi.fccIOProc = FOURCC_MEM;
    mi.pchBuffer = g_membuf[(InterlockedIncrement(&g_memslot) & 15)];
    mi.cchBuffer = sizeof g_membuf[0];
    return U(mmioOpenA)(NULL, &mi, MMIO_READWRITE);
}

ULONG_PTR mh_closed_mmio(void) {
    HMMIO h = mh_mmio_open();
    if (!h) return 0;
    if (U(mmioClose)(h, 0) != 0) return 0;
    return (ULONG_PTR)h;
}

void mh_pcm(WAVEFORMATEX *wf) {
    wf->wFormatTag = WAVE_FORMAT_PCM; wf->nChannels = 2; wf->nSamplesPerSec = 44100;
    wf->wBitsPerSample = 16; wf->nBlockAlign = 4; wf->nAvgBytesPerSec = 44100 * 4; wf->cbSize = 0;
}

MMRESULT mh_waveout_open(HWAVEOUT *h, HANDLE ev) {
    WAVEFORMATEX wf; mh_pcm(&wf);
    return U(waveOutOpen)(h, WAVE_MAPPER, &wf, (DWORD_PTR)ev, 0, ev ? CALLBACK_EVENT : CALLBACK_NULL);
}

MMRESULT mh_wavein_open(HWAVEIN *h) {
    WAVEFORMATEX wf; mh_pcm(&wf);
    return U(waveInOpen)(h, WAVE_MAPPER, &wf, 0, 0, CALLBACK_NULL);
}

DWORD mh_play_silence(HWAVEOUT h, int ms, DWORD wait_ms) {
    DWORD bytes = 44100 * 4 / 1000 * ms;
    char *buf = (char *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes);
    if (!buf) return 0xFFFF0001;
    WAVEHDR wh; memset(&wh, 0, sizeof wh); wh.lpData = buf; wh.dwBufferLength = bytes;
    DWORD r = U(waveOutPrepareHeader)(h, &wh, sizeof wh);
    if (r) { HeapFree(GetProcessHeap(), 0, buf); return r; }
    r = U(waveOutWrite)(h, &wh, sizeof wh);
    if (!r) {
        DWORD t0 = GetTickCount();
        while (!(wh.dwFlags & WHDR_DONE) && GetTickCount() - t0 < wait_ms) Sleep(5);
        if (!(wh.dwFlags & WHDR_DONE)) { U(waveOutReset)(h); r = 0xFFFF0000; }
        else if (wh.dwFlags != (WHDR_DONE | WHDR_PREPARED)) r = 0xFFFF0002;
    }
    DWORD u = U(waveOutUnprepareHeader)(h, &wh, sizeof wh);
    if (!r) r = u;
    HeapFree(GetProcessHeap(), 0, buf);
    return r;
}

void mh_zoo_open(mh_zoo *z) {
    memset(z, 0, sizeof *z);
    const mh_counts *c = mh_devs();
    z->mmio = mh_mmio_open();
    if (c->wo && mh_waveout_open(&z->wo, NULL) != 0) z->wo = NULL;
    if (c->wi && mh_wavein_open(&z->wi) != 0) z->wi = NULL;
    if (c->mx && U(mixerOpen)(&z->mx, 0, 0, 0, 0) != 0) z->mx = NULL;
    if (c->wo && c->mo && U(midiOutOpen)(&z->mo, 0, 0, 0, CALLBACK_NULL) != 0) z->mo = NULL;
}

void mh_zoo_close(mh_zoo *z) {
    if (z->mmio) EXPECT_EQ(U(mmioClose)(z->mmio, 0), 0);
    if (z->wo) EXPECT_EQ(U(waveOutClose)(z->wo), 0);
    if (z->wi) EXPECT_EQ(U(waveInClose)(z->wi), 0);
    if (z->mx) EXPECT_EQ(U(mixerClose)(z->mx), 0);
    if (z->mo) EXPECT_EQ(U(midiOutClose)(z->mo), 0);
    memset(z, 0, sizeof *z);
}

void mh_zoo_cross(const mh_zoo *z, const mh_op *ops, int n, const char *family) {
    struct { ULONG_PTR h; int type; const char *name; } live[] = {
        { (ULONG_PTR)z->mmio, MHT_MMIO, "HMMIO" }, { (ULONG_PTR)z->wo, MHT_WAVEOUT, "HWAVEOUT" },
        { (ULONG_PTR)z->wi, MHT_WAVEIN, "HWAVEIN" }, { (ULONG_PTR)z->mx, MHT_MIXER, "HMIXER" },
        { (ULONG_PTR)z->mo, MHT_MIDIOUT, "HMIDIOUT" },
    };
    for (int k = 0; k < (int)(sizeof live / sizeof live[0]); k++) {
        if (!live[k].h) continue;
        for (int i = 0; i < n; i++) {
            if (ops[i].accept_mask & (1u << live[k].type)) continue;
            ULONG_PTR r = ops[i].call(live[k].h);
            CHECKF(r == ops[i].bad, "%s: %s(live %s) -> 0x%llx, want 0x%llx", family, ops[i].name, live[k].name,
                   (unsigned long long)r, (unsigned long long)ops[i].bad);
        }
    }
}
