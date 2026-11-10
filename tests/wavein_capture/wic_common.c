/* wavein_capture: helpers + device gate tests. VM_TIMEOUT: 300 */
#include "wic.h"

static const GUID wic_sub_pcm   = {0x00000001, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
static const GUID wic_sub_float = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};

static int  g_have_n = 0;
static UINT g_n = 0, g_nout = 0;

UINT wic_ndev(void) {
    if (!g_have_n) {
        g_have_n = 1;
        if (!U(waveInGetNumDevs)) return 0;
        g_n = U(waveInGetNumDevs)();
        g_nout = U(waveOutGetNumDevs) ? U(waveOutGetNumDevs)() : 0;
        t_note("waveInGetNumDevs=%u waveOutGetNumDevs=%u", g_n, g_nout);
        for (UINT i = 0; i < g_n && i < 8; i++) {
            WAVEINCAPSA c; memset(&c, 0, sizeof c);
            MMRESULT r = U(waveInGetDevCapsA)(i, &c, sizeof c);
            t_note("waveIn %u r=%u \"%s\" mid=%u pid=%u ch=%u fmts=0x%lx", i, r, c.szPname, c.wMid, c.wPid, c.wChannels, c.dwFormats);
        }
    }
    return g_n;
}
UINT wic_nout(void) { wic_ndev(); return g_nout; }

void wic_pcm(WAVEFORMATEX *f, DWORD rate, WORD bits, WORD ch) {
    memset(f, 0, sizeof *f);
    f->wFormatTag = WAVE_FORMAT_PCM; f->nChannels = ch; f->nSamplesPerSec = rate; f->wBitsPerSample = bits;
    f->nBlockAlign = (WORD)(ch * bits / 8); f->nAvgBytesPerSec = rate * f->nBlockAlign; f->cbSize = 0;
}
void wic_float(WAVEFORMATEX *f, DWORD rate, WORD ch) {
    wic_pcm(f, rate, 32, ch); f->wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
}
void wic_ext(WAVEFORMATEXTENSIBLE *f, DWORD rate, WORD bits, WORD ch, int is_float) {
    memset(f, 0, sizeof *f);
    wic_pcm(&f->Format, rate, bits, ch);
    f->Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE; f->Format.cbSize = 22;
    f->Samples.wValidBitsPerSample = bits;
    f->dwChannelMask = ch == 1 ? SPEAKER_FRONT_CENTER : (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT);
    f->SubFormat = is_float ? wic_sub_float : wic_sub_pcm;
}
DWORD wic_bytes_ms(const WAVEFORMATEX *f, DWORD ms) {
    DWORD b = (DWORD)((unsigned long long)f->nAvgBytesPerSec * ms / 1000);
    return b - b % f->nBlockAlign;
}

static void wic_copyfmt(wic_s *s, const WAVEFORMATEX *f) {
    memset(&s->fx, 0, sizeof s->fx);
    size_t n = sizeof(WAVEFORMATEX) + (f->wFormatTag == WAVE_FORMAT_PCM ? 0 : f->cbSize);
    if (n > sizeof s->fx) n = sizeof s->fx;
    memcpy(&s->fx, f, n);
}

MMRESULT wic_open_cb(wic_s *s, UINT id, const WAVEFORMATEX *f, DWORD_PTR cb, DWORD_PTR inst, DWORD flags) {
    HWAVEIN h = (HWAVEIN)(ULONG_PTR)0x1234;
    wic_copyfmt(s, f);
    MMRESULT r = U(waveInOpen)(&h, id, f, cb, inst, flags);
    s->h = r == MMSYSERR_NOERROR ? h : NULL;
    if (r == MMSYSERR_NOERROR) CHECKF(h != NULL, "open succeeded but handle NULL");
    else EXPECT_EQ(h, NULL);  /* the out handle is zeroed before the device check (decomp waveInOpen) */
    return r;
}
MMRESULT wic_open_flags(wic_s *s, UINT id, const WAVEFORMATEX *f, DWORD extra) {
    memset(s, 0, sizeof *s);
    s->ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    return wic_open_cb(s, id, f, (DWORD_PTR)s->ev, 0, CALLBACK_EVENT | extra);
}
MMRESULT wic_open(wic_s *s, UINT id, const WAVEFORMATEX *f) { return wic_open_flags(s, id, f, 0); }
MMRESULT wic_open_std(wic_s *s, UINT id) {
    WAVEFORMATEX f; wic_pcm(&f, 44100, 16, 1); return wic_open(s, id, &f);
}

MMRESULT wic_buf(wic_s *s, int i, DWORD bytes) {
    if (i >= WIC_MAXB) return MMSYSERR_ERROR;
    s->mem[i] = HeapAlloc(GetProcessHeap(), 0, bytes ? bytes : 1);
    memset(s->mem[i], 0xCD, bytes ? bytes : 1);
    memset(&s->hdr[i], 0, sizeof(WAVEHDR));
    s->hdr[i].lpData = s->mem[i]; s->hdr[i].dwBufferLength = bytes;
    if (i >= s->nb) s->nb = i + 1;
    return U(waveInPrepareHeader)(s->h, &s->hdr[i], sizeof(WAVEHDR));
}
MMRESULT wic_add(wic_s *s, int i) { return U(waveInAddBuffer)(s->h, &s->hdr[i], sizeof(WAVEHDR)); }

static void wic_wait_tick(wic_s *s, DWORD ms) {
    if (s && s->ev) WaitForSingleObject(s->ev, ms); else Sleep(ms);
}
int wic_wait_done(wic_s *s, int i, DWORD ms) {
    DWORD t0 = wic_now();
    while (!(((volatile WAVEHDR *)&s->hdr[i])->dwFlags & WHDR_DONE)) {
        if (wic_now() - t0 > ms) return 0;
        wic_wait_tick(s, 10);
    }
    return 1;
}
int wic_wait_all(wic_s *s, int from, int to, DWORD ms) {
    DWORD t0 = wic_now();
    for (int i = from; i < to; i++) {
        DWORD el = wic_now() - t0;
        if (!wic_wait_done(s, i, el >= ms ? 0 : ms - el)) return 0;
    }
    return 1;
}
void wic_pump(DWORD ms) { Sleep(ms); }

MMRESULT wic_teardown(wic_s *s) {
    MMRESULT r = 0;
    if (s->h) {
        U(waveInReset)(s->h);
        for (int i = 0; i < s->nb; i++)
            if (s->hdr[i].dwFlags & WHDR_PREPARED) U(waveInUnprepareHeader)(s->h, &s->hdr[i], sizeof(WAVEHDR));
        r = U(waveInClose)(s->h);
        if (r) { U(waveInReset)(s->h); r = U(waveInClose)(s->h); }
        s->h = NULL;
    }
    for (int i = 0; i < s->nb; i++) if (s->mem[i]) { HeapFree(GetProcessHeap(), 0, s->mem[i]); s->mem[i] = NULL; }
    s->nb = 0;
    if (s->ev) { CloseHandle(s->ev); s->ev = NULL; }
    return r;
}

DWORD wic_pos(HWAVEIN h, UINT type, UINT *out_type) {
    MMTIME m; memset(&m, 0, sizeof m); m.wType = type;
    MMRESULT r = U(waveInGetPosition)(h, &m, sizeof m);
    if (out_type) *out_type = m.wType;
    if (r) return 0xFFFFFFFF;
    switch (m.wType) {
    case TIME_BYTES: return m.u.cb;
    case TIME_SAMPLES: return m.u.sample;
    case TIME_MS: return m.u.ms;
    case TIME_TICKS: return m.u.ticks;
    default: return m.u.cb;
    }
}

volatile LONG wic_cbn;
wic_cbrec wic_cblog[64];
void wic_cb_reset(void) { wic_cbn = 0; memset(wic_cblog, 0, sizeof wic_cblog); }
void CALLBACK wic_cbproc(HWAVEIN h, UINT msg, DWORD_PTR inst, DWORD_PTR p1, DWORD_PTR p2) {
    LONG i = InterlockedIncrement(&wic_cbn) - 1;
    if (i < 0 || i >= 64) return;
    wic_cbrec *r = &wic_cblog[i];
    r->msg = msg; r->h = h; r->inst = inst; r->p1 = p1; r->p2 = p2; r->tid = GetCurrentThreadId();
    if (msg == WIM_DATA && p1) {
        WAVEHDR *w = (WAVEHDR *)p1; r->flags = w->dwFlags; r->rec = w->dwBytesRecorded; r->len = w->dwBufferLength;
    }
}
int wic_cb_count(UINT msg) {
    int n = 0, m = wic_cbn > 64 ? 64 : (int)wic_cbn;
    for (int i = 0; i < m; i++) if (wic_cblog[i].msg == msg) n++;
    return n;
}

/* ------------------------------------------------------------------ gate tests */

TEST(wic_gate_numdevs) {
    REQUIRE(U(waveInGetNumDevs));
    UINT n = wic_ndev();
    CHECKF(n < 64, "implausible waveIn count %u", n);
    /* a second read must agree (no device change while the test runs) */
    EXPECT_EQ(U(waveInGetNumDevs)(), n);
}

TEST(wic_gate_caps_each_device) {
    REQUIRE(U(waveInGetDevCapsW)); REQUIRE(U(waveInGetDevCapsA));
    UINT n = wic_ndev();
    for (UINT i = 0; i < n; i++) {
        WAVEINCAPSW w; memset(&w, 0xAB, sizeof w);
        EXPECT_EQ(U(waveInGetDevCapsW)(i, &w, sizeof w), MMSYSERR_NOERROR);
        CHECKF(w.szPname[0] != 0, "device %u has an empty name", i);
        CHECKF(w.wChannels >= 1 && w.wChannels <= 8, "device %u channels %u", i, w.wChannels);
        WAVEINCAPSA a; memset(&a, 0xAB, sizeof a);
        EXPECT_EQ(U(waveInGetDevCapsA)(i, &a, sizeof a), MMSYSERR_NOERROR);
        CHECK(memchr(a.szPname, 0, sizeof a.szPname) != NULL);
        EXPECT_EQ(a.wMid, w.wMid); EXPECT_EQ(a.wPid, w.wPid); EXPECT_EQ(a.wChannels, w.wChannels);
        EXPECT_EQ(a.dwFormats, w.dwFormats);
    }
    /* first id past the end */
    WAVEINCAPSW w;
    EXPECT_EQ(U(waveInGetDevCapsW)(n, &w, sizeof w), MMSYSERR_BADDEVICEID);
    EXPECT_EQ(U(waveInGetDevCapsW)(n + 1, &w, sizeof w), MMSYSERR_BADDEVICEID);
}

TEST(wic_gate_mapper_caps) {
    REQUIRE(U(waveInGetDevCapsW));
    UINT n = wic_ndev(), no = wic_nout();
    WAVEINCAPSW w; memset(&w, 0, sizeof w);
    MMRESULT r = U(waveInGetDevCapsW)(WAVE_MAPPER, &w, sizeof w);
    if (n) {
        EXPECT_EQ(r, MMSYSERR_NOERROR);
        CHECK(w.szPname[0] != 0);
    } else if (!no) {
        EXPECT_EQ(r, MMSYSERR_BADDEVICEID);   /* no audio at all [VM noaudio] */
    } else {
        /* output-only guest: the mapper entry exists because the wave mapper loads for any wave device */
        t_note("output-only guest: waveInGetDevCaps(WAVE_MAPPER) = %u", r);
    }
}

TEST(wic_gate_caps_device_vs_mapper_names) {
    REQUIRE(U(waveInGetDevCapsA));
    NEED_DEV();
    WAVEINCAPSA m, d;
    EXPECT_EQ(U(waveInGetDevCapsA)(WAVE_MAPPER, &m, sizeof m), 0);
    EXPECT_EQ(U(waveInGetDevCapsA)(0, &d, sizeof d), 0);
    t_note("mapper \"%s\" mid=%u pid=%u; device0 \"%s\"", m.szPname, m.wMid, m.wPid, d.szPname);
    CHECK(strcmp(m.szPname, d.szPname) != 0);   /* the mapper reports its own name, not the endpoint's */
}
