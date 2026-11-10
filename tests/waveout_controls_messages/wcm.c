#include "wcm.h"

static UINT g_nout = 0xFFFFFFFFu, g_nin = 0xFFFFFFFFu;

UINT wcm_nout(void)
{
    if (g_nout == 0xFFFFFFFFu) {
        if (!U(waveOutGetNumDevs)) return 0;
        g_nout = U(waveOutGetNumDevs)();   /* may wait for AudioSrv: deliberately not timed */
    }
    return g_nout;
}

UINT wcm_nin(void)
{
    if (g_nin == 0xFFFFFFFFu) {
        if (!U(waveInGetNumDevs)) return 0;
        g_nin = U(waveInGetNumDevs)();
    }
    return g_nin;
}

int wcm_mapper_present(void) { return wcm_nout() + wcm_nin() != 0; }

void wcm_note_devices(void)
{
    static int done;
    if (done) return;
    done = 1;
    UINT no = wcm_nout(), ni = wcm_nin();
    t_note("DEVICES waveout=%u wavein=%u mapper_expected=%d", no, ni, wcm_mapper_present());
    if (!U(waveOutGetDevCapsA) || !U(waveInGetDevCapsA)) return;
    for (UINT i = 0; i < no && i < 8; i++) {
        WAVEOUTCAPSA c; memset(&c, 0, sizeof c);
        MMRESULT r = U(waveOutGetDevCapsA)(i, &c, sizeof c);
        t_note("waveout %u r=%u name=\"%s\" mid=%u pid=%u support=0x%lx", i, r, c.szPname, c.wMid, c.wPid,
               (unsigned long)c.dwSupport);
    }
    for (UINT i = 0; i < ni && i < 8; i++) {
        WAVEINCAPSA c; memset(&c, 0, sizeof c);
        MMRESULT r = U(waveInGetDevCapsA)(i, &c, sizeof c);
        t_note("wavein %u r=%u name=\"%s\"", i, r, c.szPname);
    }
}

void wcm_fmt(WAVEFORMATEX *f, DWORD rate, WORD bits, WORD ch)
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

MMRESULT wcm_open_out(HWAVEOUT *h, UINT id)
{
    WAVEFORMATEX f; wcm_fmt(&f, 44100, 16, 2);
    *h = NULL;
    if (!U(waveOutOpen)) return 0xFFFF;
    return U(waveOutOpen)(h, id, &f, 0, 0, CALLBACK_NULL);
}

MMRESULT wcm_open_in(HWAVEIN *h, UINT id)
{
    WAVEFORMATEX f; wcm_fmt(&f, 44100, 16, 1);
    *h = NULL;
    if (!U(waveInOpen)) return 0xFFFF;
    return U(waveInOpen)(h, id, &f, 0, 0, CALLBACK_NULL);
}

void wcm_close_out(HWAVEOUT h)
{
    if (!h) return;
    MMRESULT r = U(waveOutClose)(h);
    if (r) { U(waveOutReset)(h); r = U(waveOutClose)(h); }
    if (r) t_note("waveOutClose cleanup r=%u", r);
}

void wcm_close_in(HWAVEIN h)
{
    if (!h) return;
    MMRESULT r = U(waveInClose)(h);
    if (r) { U(waveInReset)(h); r = U(waveInClose)(h); }
    if (r) t_note("waveInClose cleanup r=%u", r);
}

HWAVEOUT wcm_closed_out(void)
{
    static HWAVEOUT closed;
    static int tried;
    if (!tried) {
        tried = 1;
        HWAVEOUT h;
        if (wcm_nout() && wcm_open_out(&h, WAVE_MAPPER) == 0) {
            closed = h;
            if (U(waveOutClose)(h) != 0) { wcm_close_out(h); }
        }
    }
    return closed;
}

static char g_stack_dummy_anchor;
static int  g_heap_dummy;

/* None of these is a live waveOut handle. ValidateHandle only compares them against its list. */
const wcm_val wcm_nonhandles[] = {
    { "zero",        0 },
    { "one",         1 },
    { "two",         2 },
    { "0x7F",        0x7F },
    { "0xFFFE",      0xFFFE },
    { "0xFFFF",      0xFFFF },
    { "0x10000",     0x10000 },
    { "0x12345",     0x12345 },
    { "0x7FFFFFFF",  0x7FFFFFFF },
    { "0xFFFFFFFE",  0xFFFFFFFEu },
    { "0xFFFFFFFF",  0xFFFFFFFFu },
    { "0x100000000", (ULONG_PTR)0x100000000ull },
    { "0x1FFFFFFFF", (ULONG_PTR)0x1FFFFFFFFull },
    { "0x123400000009", (ULONG_PTR)0x123400000009ull },
    { "minus1",      (ULONG_PTR)~(ULONG_PTR)0 },
    { "minus2",      (ULONG_PTR)~(ULONG_PTR)1 },
    { "kernelish",   (ULONG_PTR)0xFFFF800000001000ull },
    { "static_addr", (ULONG_PTR)&g_stack_dummy_anchor },
    { "static_int",  (ULONG_PTR)&g_heap_dummy },
    { NULL, 0 }
};

int wcm_id_class(ULONG_PTR v, UINT n, int map_ffff)
{
    if (map_ffff && v == 0xFFFF) return WCM_ID_MAPPER;
    UINT id = (UINT)(v & 0xFFFFFFFFu);
    if (id == 0xFFFFFFFFu) return WCM_ID_MAPPER;
    if (id < n) return WCM_ID_DEVICE;
    return WCM_ID_BAD;
}

const char *wcm_w2a(const WCHAR *w)
{
    static char buf[4][512];
    static int k;
    char *b = buf[k++ & 3];
    if (!w) { strcpy(b, "(null)"); return b; }
    size_t i = 0;
    for (; i < sizeof buf[0] - 1 && w[i]; i++) b[i] = (w[i] >= 0x20 && w[i] < 0x7F) ? (char)w[i] : '?';
    b[i] = 0;
    return b;
}

size_t wcm_wlen(const WCHAR *w, size_t cap)
{
    size_t i = 0;
    while (i < cap && w[i]) i++;
    return i;
}
