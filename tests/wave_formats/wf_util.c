#include "wf.h"

/* KSDATAFORMAT_SUBTYPE_* = {tag-0000-0010-8000-00AA00389B71} */
#define WF_KSGUID(tag) { (tag), 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 } }
const GUID WF_SUB_PCM   = WF_KSGUID(0x0001);
const GUID WF_SUB_FLOAT = WF_KSGUID(0x0003);
const GUID WF_SUB_ALAW  = WF_KSGUID(0x0006);
const GUID WF_SUB_MULAW = WF_KSGUID(0x0007);
const GUID WF_SUB_ADPCM = WF_KSGUID(0x0002);
const GUID WF_SUB_DRM   = WF_KSGUID(0x0009);
const GUID WF_SUB_BOGUS = { 0x6b1e2f0a, 0x51c3, 0x4d77, { 0x9a, 0x01, 0x5e, 0x3c, 0x77, 0x21, 0x0d, 0xe4 } };

static int g_n[2] = { -1, -1 };

UINT wf_ndev(int dir) {
    if (g_n[dir] < 0) {
        if (dir == WF_OUT) g_n[dir] = U(waveOutGetNumDevs) ? (int)U(waveOutGetNumDevs)() : 0;
        else               g_n[dir] = U(waveInGetNumDevs)  ? (int)U(waveInGetNumDevs)()  : 0;
    }
    return (UINT)g_n[dir];
}

const char *wf_dirname(int dir) { return dir == WF_OUT ? "out" : "in"; }

void wf_pcm(wf_fmt *f, WORD tag, WORD ch, DWORD rate, WORD bits) {
    memset(f, 0, sizeof *f);
    f->wFormatTag = tag; f->nChannels = ch; f->nSamplesPerSec = rate; f->wBitsPerSample = bits;
    f->nBlockAlign = (WORD)(ch * ((bits + 7) / 8));
    f->nAvgBytesPerSec = rate * f->nBlockAlign;
    f->cbSize = 0;
}

void wf_extfmt(wf_ext *f, WORD ch, DWORD rate, WORD bits, WORD valid, DWORD mask, const GUID *sub) {
    memset(f, 0, sizeof *f);
    f->wFormatTag = WAVE_FORMAT_EXTENSIBLE_; f->nChannels = ch; f->nSamplesPerSec = rate; f->wBitsPerSample = bits;
    f->nBlockAlign = (WORD)(ch * ((bits + 7) / 8));
    f->nAvgBytesPerSec = rate * f->nBlockAlign;
    f->cbSize = 22;
    f->wValidBitsPerSample = valid; f->dwChannelMask = mask; f->SubFormat = *sub;
}

static void put16(BYTE *p, WORD v) { p[0] = (BYTE)v; p[1] = (BYTE)(v >> 8); }
static void put32(BYTE *p, DWORD v) { put16(p, (WORD)v); put16(p + 2, (WORD)(v >> 16)); }

/* MS ADPCM: ADPCMWAVEFORMAT {wfx, wSamplesPerBlock, wNumCoef = 7, aCoef[7]{iCoef1, iCoef2}}, cbSize 32. */
void wf_msadpcm(wf_fmt *f, WORD ch, DWORD rate) {
    static const short coef[7][2] = { {256, 0}, {512, -256}, {0, 0}, {192, 64}, {240, 0}, {460, -208}, {392, -232} };
    memset(f, 0, sizeof *f);
    WORD block = (WORD)(rate <= 11025 ? 256 * ch : rate <= 22050 ? 512 * ch : 1024 * ch);
    WORD spb = (WORD)((block - 7 * ch) * 8 / (4 * ch) + 2);
    f->wFormatTag = WAVE_FORMAT_ADPCM_; f->nChannels = ch; f->nSamplesPerSec = rate; f->wBitsPerSample = 4;
    f->nBlockAlign = block; f->nAvgBytesPerSec = (DWORD)((unsigned long long)rate * block / spb);
    f->cbSize = 32;
    put16(f->extra, spb); put16(f->extra + 2, 7);
    for (int i = 0; i < 7; i++) { put16(f->extra + 4 + 4 * i, (WORD)coef[i][0]); put16(f->extra + 6 + 4 * i, (WORD)coef[i][1]); }
}

/* IMA ADPCM: IMAADPCMWAVEFORMAT {wfx, wSamplesPerBlock}, cbSize 2. */
void wf_imaadpcm(wf_fmt *f, WORD ch, DWORD rate) {
    memset(f, 0, sizeof *f);
    WORD block = (WORD)(rate <= 11025 ? 256 * ch : rate <= 22050 ? 512 * ch : 1024 * ch);
    WORD spb = (WORD)((block - 4 * ch) * 8 / (4 * ch) + 1);
    f->wFormatTag = WAVE_FORMAT_IMA_ADPCM_; f->nChannels = ch; f->nSamplesPerSec = rate; f->wBitsPerSample = 4;
    f->nBlockAlign = block; f->nAvgBytesPerSec = (DWORD)((unsigned long long)rate * block / spb);
    f->cbSize = 2; put16(f->extra, spb);
}

/* GSM 6.10: GSM610WAVEFORMAT {wfx, wSamplesPerBlock = 320}, 65-byte blocks, mono. */
void wf_gsm610(wf_fmt *f, DWORD rate) {
    memset(f, 0, sizeof *f);
    f->wFormatTag = WAVE_FORMAT_GSM610_; f->nChannels = 1; f->nSamplesPerSec = rate; f->wBitsPerSample = 0;
    f->nBlockAlign = 65; f->nAvgBytesPerSec = rate * 65 / 320;
    f->cbSize = 2; put16(f->extra, 320);
}

/* G.711 a-law / mu-law: 8 bits per sample, no extra bytes. */
void wf_g711(wf_fmt *f, WORD tag, WORD ch, DWORD rate) {
    memset(f, 0, sizeof *f);
    f->wFormatTag = tag; f->nChannels = ch; f->nSamplesPerSec = rate; f->wBitsPerSample = 8;
    f->nBlockAlign = ch; f->nAvgBytesPerSec = rate * ch; f->cbSize = 0;
}

/* MPEG-1 layer 3: MPEGLAYER3WAVEFORMAT {wfx, wID = 1, fdwFlags = 2 (padding off), nBlockSize, nFramesPerBlock 1,
 * nCodecDelay 1393}, cbSize 12. */
void wf_mp3(wf_fmt *f, WORD ch, DWORD rate, DWORD kbps) {
    memset(f, 0, sizeof *f);
    f->wFormatTag = WAVE_FORMAT_MPEGLAYER3_; f->nChannels = ch; f->nSamplesPerSec = rate; f->wBitsPerSample = 0;
    f->nBlockAlign = 1; f->nAvgBytesPerSec = kbps * 1000 / 8; f->cbSize = 12;
    put16(f->extra, 1); put32(f->extra + 2, 2); put16(f->extra + 6, (WORD)(144 * kbps * 1000 / rate));
    put16(f->extra + 8, 1); put16(f->extra + 10, 1393);
}

#define WF_SENT ((ULONG_PTR)0x5A5A5A5A5A5A5A5Aull)

MMRESULT wf_query(int dir, UINT id, const void *fmt, DWORD extra) {
    MMRESULT r;
    if (dir == WF_OUT) {
        HWAVEOUT h = (HWAVEOUT)WF_SENT;
        r = U(waveOutOpen)(&h, id, (LPCWAVEFORMATEX)fmt, 0, 0, CALLBACK_NULL | WAVE_FORMAT_QUERY | extra);
        CHECKF(h == (HWAVEOUT)WF_SENT, "query wrote the handle slot: %p (r=%u)", h, r);
    } else {
        HWAVEIN h = (HWAVEIN)WF_SENT;
        r = U(waveInOpen)(&h, id, (LPCWAVEFORMATEX)fmt, 0, 0, CALLBACK_NULL | WAVE_FORMAT_QUERY | extra);
        CHECKF(h == (HWAVEIN)WF_SENT, "query wrote the handle slot: %p (r=%u)", h, r);
    }
    return r;
}

MMRESULT wf_open_close(int dir, UINT id, const void *fmt, DWORD extra) {
    MMRESULT r;
    if (dir == WF_OUT) {
        HWAVEOUT h = (HWAVEOUT)WF_SENT;
        r = U(waveOutOpen)(&h, id, (LPCWAVEFORMATEX)fmt, 0, 0, CALLBACK_NULL | extra);
        if (r == MMSYSERR_NOERROR) {
            CHECKF(h != NULL && h != (HWAVEOUT)WF_SENT, "open succeeded with handle %p", h);
            if (h && h != (HWAVEOUT)WF_SENT) EXPECT_EQ(U(waveOutClose)(h), MMSYSERR_NOERROR);
        } else {
            CHECKF(h == NULL, "failed open left handle %p (r=%u)", h, r);
        }
    } else {
        HWAVEIN h = (HWAVEIN)WF_SENT;
        r = U(waveInOpen)(&h, id, (LPCWAVEFORMATEX)fmt, 0, 0, CALLBACK_NULL | extra);
        if (r == MMSYSERR_NOERROR) {
            CHECKF(h != NULL && h != (HWAVEIN)WF_SENT, "open succeeded with handle %p", h);
            if (h && h != (HWAVEIN)WF_SENT) EXPECT_EQ(U(waveInClose)(h), MMSYSERR_NOERROR);
        } else {
            CHECKF(h == NULL, "failed open left handle %p (r=%u)", h, r);
        }
    }
    return r;
}

int wf_pre_driver(int dir, UINT id) {
    UINT n = wf_ndev(dir);
    if (n == 0) return MMSYSERR_BADDEVICEID;
    if (id != WAVE_MAPPER && id != 0xFFFF && id >= n) return MMSYSERR_BADDEVICEID;
    return -1;
}

static MMRESULT judge(const char *how, const char *label, int dir, UINT id, MMRESULT r, int exp) {
    int gate = wf_pre_driver(dir, id);
    const char *who = id == WAVE_MAPPER ? "mapper" : id == 0xFFFF ? "0xFFFF" : "id";
    if (gate >= 0) {
        CHECKF(r == (MMRESULT)gate, "%s %s %s %s%d: got %u want %d (no device / bad id)", how, wf_dirname(dir), label,
               who, (int)id, r, gate);
    } else if (exp >= 0) {
        CHECKF(r == (MMRESULT)exp, "%s %s %s %s%d: got %u want %d", how, wf_dirname(dir), label, who, (int)id, r, exp);
    } else {
        if (exp == WF_ANY_OK_OR_BAD)
            CHECKF(r == MMSYSERR_NOERROR || r == WAVERR_BADFORMAT, "%s %s %s %s%d: got %u want 0 or 32", how,
                   wf_dirname(dir), label, who, (int)id, r);
        if (exp != WF_NOTE_IF_NOT0 || r != MMSYSERR_NOERROR)
            t_note("CELL %s %s %s %s%d r=%u", how, wf_dirname(dir), label, who, (int)id, r);
    }
    return r;
}

MMRESULT wf_cell(const char *label, int dir, UINT id, const void *fmt, DWORD extra, int exp) {
    return judge("query", label, dir, id, wf_query(dir, id, fmt, extra), exp);
}

MMRESULT wf_cell_open(const char *label, int dir, UINT id, const void *fmt, DWORD extra, int exp) {
    return judge("open", label, dir, id, wf_open_close(dir, id, fmt, extra), exp);
}

int wf_need_dev(int dir) {
    if (wf_ndev(dir) == 0) { t_skip("no wave%s device", dir == WF_OUT ? "Out" : "In"); return 1; }
    return 0;
}
