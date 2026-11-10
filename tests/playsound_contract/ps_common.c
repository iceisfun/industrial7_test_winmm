#include "ps.h"

const char *const ps_ep_name[EP_N] = { "PlaySoundA", "PlaySoundW", "PlaySound", "ordinal2", "sndPlaySoundA", "sndPlaySoundW" };

typedef BOOL (WINAPI *ps_ansi_fn)(LPCSTR, HMODULE, DWORD);

static ps_ansi_fn ord2(void) { return (ps_ansi_fn)t_proc_ord("winmm.dll", 2); }

int ps_have_all(void) {
    return U(PlaySoundA) && U(PlaySoundW) && U(PlaySound) && ord2() && U(sndPlaySoundA) && U(sndPlaySoundW);
}

/* Up to 2 KiB names; the ANSI copy lives in a zero-padded static so a mistaken memory parse never reads past it. */
static char g_ansi[4096];

BOOL ps_call(int ep, int kind, const void *psz, HMODULE hmod, DWORD f) {
    const void *w = kind == PSZ_NULL ? NULL : psz;
    const void *a = w;
    if (kind == PSZ_WIDE && (ep == EP_A || ep == EP_PS || ep == EP_ORD2 || ep == EP_SNDA)) {
        memset(g_ansi, 0, sizeof g_ansi);
        WideCharToMultiByte(CP_ACP, 0, (LPCWSTR)psz, -1, g_ansi, sizeof g_ansi - 16, NULL, NULL);
        a = g_ansi;
    }
    switch (ep) {
    case EP_A:    return U(PlaySoundA)((LPCSTR)a, hmod, f);
    case EP_W:    return U(PlaySoundW)((LPCWSTR)w, hmod, f);
    case EP_PS:   return U(PlaySound)((LPCSTR)a, hmod, f);
    case EP_ORD2: return ord2()((LPCSTR)a, hmod, f);
    case EP_SNDA: return U(sndPlaySoundA)((LPCSTR)a, (UINT)f);
    case EP_SNDW: return U(sndPlaySoundW)((LPCWSTR)w, (UINT)f);
    }
    return -1;
}

BOOL ps_stop(void) { return U(PlaySoundW) ? U(PlaySoundW)(NULL, NULL, 0) : FALSE; }

/* A sync stop posts 0x3CD to the notify window; mmWndProc drops every queued async play that sees a pending 0x3CD,
 * so stop + short drain + stop leaves nothing playing and nothing queued. */
void ps_quiesce(void) { ps_stop(); Sleep(120); ps_stop(); }

UINT ps_ndev(void) {
    static int done; static UINT n;
    if (!done) { n = U(waveOutGetNumDevs) ? U(waveOutGetNumDevs)() : 0; done = 1; t_note("waveOutGetNumDevs=%u", n); }
    return n;
}

/* sndPlaySoundI: SND_NOSTOP with a current wave header -> FALSE before anything else; an empty name otherwise
 * returns TRUE at once without stopping anything. */
BOOL ps_is_playing(void) { return !U(PlaySoundW)(L"", NULL, SND_NOSTOP | SND_NODEFAULT); }

BOOL ps_wait_playing(DWORD ms) {
    DWORD t0 = GetTickCount();
    do { if (ps_is_playing()) return TRUE; Sleep(50); } while (GetTickCount() - t0 < ms);
    return ps_is_playing();
}

/* ---- images ---------------------------------------------------------------------------------------------- */
void img_begin(ps_img *m, BYTE *buf, DWORD cap) {
    memset(buf, 0, cap); m->b = buf; m->cap = cap; m->n = 0;
    img_raw(m, "RIFF", 4); DWORD z = 0; img_raw(m, &z, 4); img_raw(m, "WAVE", 4);
}
void img_raw(ps_img *m, const void *p, DWORD n) { if (m->n + n <= m->cap) { if (p) memcpy(m->b + m->n, p, n); m->n += n; } }
void img_chunk_decl(ps_img *m, const char *id, const void *data, DWORD n, DWORD declared) {
    img_raw(m, id, 4); img_raw(m, &declared, 4); img_raw(m, data, n);
}
void img_chunk(ps_img *m, const char *id, const void *data, DWORD n) { img_chunk_decl(m, id, data, n, n); }
void img_pad(ps_img *m) { if (m->n & 1) { BYTE z = 0; img_raw(m, &z, 1); } }
void img_set_riff_len(ps_img *m, DWORD len) { memcpy(m->b + 4, &len, 4); }
DWORD img_end(ps_img *m) { img_set_riff_len(m, m->n - 8); return m->n; }

void fmt_pcm(ps_fmt *f, WORD ch, DWORD rate, WORD bits) {
    memset(f, 0, sizeof *f);
    f->tag = WAVE_FORMAT_PCM; f->ch = ch; f->rate = rate; f->bits = bits;
    f->align = (WORD)(ch * bits / 8); f->avg = rate * f->align;
}

DWORD ps_make_pcm(BYTE *buf, DWORD cap, WORD ch, DWORD rate, WORD bits, DWORD ms) {
    ps_img m; ps_fmt f; fmt_pcm(&f, ch, rate, bits);
    DWORD n = f.avg * ms / 1000; n -= n % f.align;
    img_begin(&m, buf, cap);
    img_chunk(&m, "fmt ", &f, 16);
    img_chunk(&m, "data", NULL, n);
    return img_end(&m);
}
DWORD ps_make_silent(BYTE *buf, DWORD cap) { return ps_make_pcm(buf, cap, 1, 8000, 16, 100); }

/* ---- files ----------------------------------------------------------------------------------------------- */
const WCHAR *ps_tmpdirW(void) {
    static WCHAR w[MAX_PATH];
    if (!w[0]) MultiByteToWideChar(CP_ACP, 0, t_tmpdir(), -1, w, MAX_PATH);
    return w;
}
void ps_pathW(WCHAR *out, size_t cch, const WCHAR *leaf) { _snwprintf(out, cch, L"%ls%ls", ps_tmpdirW(), leaf); out[cch - 1] = 0; }
BOOL ps_write_fileW(const WCHAR *path, const void *p, DWORD n) {
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    DWORD w = 0; BOOL ok = WriteFile(h, p, n, &w, NULL) && w == n;
    CloseHandle(h);
    return ok;
}

/* ---- sweeps ---------------------------------------------------------------------------------------------- */
static int g_sw_fail, g_sw_hidden;
void ps_sweep_begin(void) { g_sw_fail = g_sw_hidden = 0; }
void ps_expect(int ok, const char *file, int line, const char *fmt, ...) {
    if (ok) { t_check(1, file, line, "", NULL); return; }
    if (++g_sw_fail > 12) { g_sw_hidden++; return; }
    char msg[512]; va_list a; va_start(a, fmt); vsnprintf(msg, sizeof msg, fmt, a); va_end(a);
    t_check(0, file, line, "row", "%s", msg);
}
void ps_sweep_end(const char *file, int line) {
    if (g_sw_hidden) t_check(0, file, line, "sweep", "%d more mismatching rows not printed", g_sw_hidden);
    g_sw_fail = g_sw_hidden = 0;
}
