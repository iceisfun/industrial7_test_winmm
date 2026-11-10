/* playsound_device helpers (no tests here). VM_TIMEOUT: 300 */
#include "ps.h"
#include <math.h>

const GUID ps_sub_pcm   = { 0x00000001, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
const GUID ps_sub_float = { 0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
const GUID ps_sub_bogus = { 0x12345678, 0x9abc, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };

int ps_have_api(void) {
    return U(PlaySoundW) && U(PlaySoundA) && U(sndPlaySoundW) && U(sndPlaySoundA) && U(waveOutGetNumDevs);
}

UINT ps_ndev(void) {
    static int done; static UINT n;
    if (!done) {
        done = 1;
        if (U(waveOutGetNumDevs)) n = U(waveOutGetNumDevs)();   /* may wait for AudioSrv: never timed */
        t_note("PSENV waveOutGetNumDevs=%u", n);
        if (U(waveOutGetDevCapsW))
            for (UINT i = 0; i < n && i < 8; i++) {
                WAVEOUTCAPSW c; memset(&c, 0, sizeof c);
                MMRESULT r = U(waveOutGetDevCapsW)(i, &c, sizeof c);
                char nm[64]; WideCharToMultiByte(CP_UTF8, 0, c.szPname, -1, nm, sizeof nm, NULL, NULL);
                t_note("PSENV waveout %u r=%u name=\"%s\" ch=%u fmts=0x%lx", i, r, nm, c.wChannels, c.dwFormats);
            }
    }
    return n;
}

double ps_now(void) {
    static LARGE_INTEGER f; LARGE_INTEGER c;
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1000.0 / (double)f.QuadPart;
}

BOOL psW(const void *p, DWORD f) { return U(PlaySoundW)((LPCWSTR)p, NULL, f); }
BOOL psA(const void *p, DWORD f) { return U(PlaySoundA)((LPCSTR)p, NULL, f); }
BOOL spsW(const void *p, UINT f) { return U(sndPlaySoundW)((LPCWSTR)p, f); }
BOOL spsA(const void *p, UINT f) { return U(sndPlaySoundA)((LPCSTR)p, f); }
BOOL ps_timed(BOOL (*fn)(const void *, DWORD), const void *p, DWORD f, double *ms) {
    double t0 = ps_now(); BOOL r = fn(p, f); *ms = ps_now() - t0; return r;
}

void ps_stop(void) { BOOL r = U(PlaySoundW)(NULL, NULL, 0); CHECKF(r == TRUE, "PlaySoundW(NULL,NULL,0) = %d", r); }
int ps_playing(void) { return !U(PlaySoundW)(L"", NULL, PS_NOSTOP | PS_NODEFAULT); }
int ps_wait_playing(int want, DWORD timeout_ms) {
    double t0 = ps_now();
    for (;;) {
        if (ps_playing() == want) return 1;
        if (ps_now() - t0 > timeout_ms) return 0;
        Sleep(10);
    }
}

/* ---- builder ----------------------------------------------------------- */
void wb_init_magic(wb_t *w, DWORD cap, const char riff[4], const char wave[4]) {
    w->b = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, cap); w->cap = w->b ? cap : 0; w->n = 0;
    wb_raw(w, riff, 4); wb_u32(w, 0); wb_raw(w, wave, 4);
}
void wb_init(wb_t *w, DWORD cap) { wb_init_magic(w, cap, "RIFF", "WAVE"); }
void wb_raw(wb_t *w, const void *p, DWORD len) {
    if (!w->b || w->n + len > w->cap || w->n + len < w->n) { w->n = w->cap; return; }
    memcpy(w->b + w->n, p, len); w->n += len;
}
void wb_u16(wb_t *w, WORD v) { wb_raw(w, &v, 2); }
void wb_u32(wb_t *w, DWORD v) { wb_raw(w, &v, 4); }
void wb_fill(wb_t *w, BYTE v, DWORD len) {
    if (!w->b || w->n + len > w->cap || w->n + len < w->n) { w->n = w->cap; return; }
    memset(w->b + w->n, v, len); w->n += len;
}
void wb_chunk(wb_t *w, const char id[4], DWORD len) { wb_raw(w, id, 4); wb_u32(w, len); }
DWORD wb_avg(WORD ch, DWORD rate, WORD bits) { return rate * ch * bits / 8; }
void wb_fmt(wb_t *w, WORD tag, WORD ch, DWORD rate, WORD bits) {
    WORD al = (WORD)(ch * bits / 8);
    wb_fmt_raw(w, 16, tag, ch, rate, rate * al, al, bits, 0, NULL, 0);
}
void wb_fmt_raw(wb_t *w, DWORD cb, WORD tag, WORD ch, DWORD rate, DWORD avg, WORD align, WORD bits,
                WORD cbsize, const void *extra, DWORD extralen) {
    BYTE body[0x500]; DWORD k = 0;
    memset(body, 0, sizeof body);
    memcpy(body + 0, &tag, 2); memcpy(body + 2, &ch, 2); memcpy(body + 4, &rate, 4);
    memcpy(body + 8, &avg, 4); memcpy(body + 12, &align, 2); memcpy(body + 14, &bits, 2);
    memcpy(body + 16, &cbsize, 2);
    if (extra && extralen) memcpy(body + 18, extra, extralen < sizeof body - 18 ? extralen : sizeof body - 18);
    k = cb < sizeof body ? cb : sizeof body;
    wb_chunk(w, "fmt ", cb);
    wb_raw(w, body, k);
}
void wb_fmt_ext(wb_t *w, WORD ch, DWORD rate, WORD bits, WORD valid, DWORD mask, const GUID *sub, DWORD avg_override) {
    BYTE ex[22]; WORD al = (WORD)(ch * bits / 8);
    memcpy(ex, &valid, 2); memcpy(ex + 2, &mask, 4); memcpy(ex + 6, sub, 16);
    wb_fmt_raw(w, 40, 0xFFFE, ch, rate, avg_override ? avg_override : rate * al, al, bits, 22, ex, 22);
}
void wb_data(wb_t *w, DWORD bytes, BYTE fill) { wb_chunk(w, "data", bytes); wb_fill(w, fill, bytes); }
void wb_set_riff(wb_t *w, DWORD v) { if (w->b && w->cap >= 8) memcpy(w->b + 4, &v, 4); }
void wb_finish(wb_t *w) { wb_set_riff(w, w->n - 8); }
void wb_free(wb_t *w) { if (w->b) HeapFree(GetProcessHeap(), 0, w->b); w->b = NULL; w->cap = w->n = 0; }

void ps_silent(wb_t *w, WORD tag, WORD ch, DWORD rate, WORD bits, DWORD ms) {
    WORD al = (WORD)(ch * bits / 8); if (!al) al = 1;
    DWORD bytes = (DWORD)((ULONGLONG)rate * al * ms / 1000); bytes -= bytes % al;
    wb_init(w, bytes + 256);
    wb_fmt(w, tag, ch, rate, bits);
    wb_data(w, bytes, (tag == WAVE_FORMAT_PCM && bits == 8) ? 0x80 : 0);
    wb_finish(w);
}
void ps_tone(wb_t *w, DWORD rate, DWORD hz, DWORD ms) {
    DWORD ns = rate * ms / 1000;
    wb_init(w, ns * 2 + 256);
    wb_fmt(w, WAVE_FORMAT_PCM, 1, rate, 16);
    wb_chunk(w, "data", ns * 2);
    for (DWORD i = 0; i < ns; i++) {
        double env = 1.0; DWORD fade = rate / 100;          /* 10 ms fades: no clicks */
        if (i < fade) env = (double)i / fade; else if (ns - i < fade) env = (double)(ns - i) / fade;
        short s = (short)(1036.0 * env * sin(2.0 * 3.14159265358979 * hz * i / rate));
        wb_u16(w, (WORD)s);
    }
    wb_finish(w);
}

int ps_write_file(const wb_t *w, DWORD nbytes, const char *leaf, WCHAR *out, size_t cch) {
    char path[MAX_PATH]; DWORD wr = 0;
    t_tmpfile(path, sizeof path, leaf);
    MultiByteToWideChar(CP_ACP, 0, path, -1, out, (int)cch);
    HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    BOOL ok = WriteFile(h, w->b, nbytes, &wr, NULL) && wr == nbytes;
    CloseHandle(h);
    return ok;
}
