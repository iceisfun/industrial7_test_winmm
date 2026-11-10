/* playsound_device: synchronous SND_MEMORY plays with and without a wave device.
 *
 * Path (winmm sndPlaySoundI -> sndMessage -> soundLoadMemory/soundInitWavHdr -> soundPlay -> soundOpen):
 * a synchronous memory play parses the caller's image in place, opens WAVE_MAPPER, writes one header and waits in
 * soundWait (75 ms sleeps) until WHDR_DONE or twice the image length has elapsed. With 0 wave devices the
 * waveOutOpen fails and the call returns FALSE at once.
 */
#include "ps.h"

#define DEV (ps_ndev() > 0)
#define EXPECT_PLAYED(r) do { BOOL r__ = (r); if (DEV) CHECKF(r__ == TRUE, "want TRUE (device), got %d", r__); \
                              else CHECKF(r__ == FALSE, "want FALSE (0 devices), got %d", r__); } while (0)

TEST(psd_env_note) {
    PS_REQ();
    t_note("PSENV devices=%u (device half %s)", ps_ndev(), ps_ndev() ? "runs" : "skipped: 0-device contract only");
    CHECK(ps_ndev() < 64);
    CHECK(ps_playing() == 0);
}

TEST(psd_probe_idle_returns_true) {
    PS_REQ();
    ps_stop();
    /* Nothing open: the NOSTOP check passes and the empty name returns TRUE. */
    for (int i = 0; i < 4; i++) CHECK(psW(L"", PS_NOSTOP | PS_NODEFAULT) == TRUE);
    CHECK(psA("", PS_NOSTOP | PS_NODEFAULT) == TRUE);
    CHECK(spsW(L"", PS_NOSTOP | PS_NODEFAULT) == TRUE);
    CHECK(spsA("", PS_NOSTOP | PS_NODEFAULT) == TRUE);
}

TEST(psd_sync_300ms_returns_after_image) {
    PS_REQ();
    wb_t w; double ms; ps_silent(&w, WAVE_FORMAT_PCM, 1, 8000, 16, 300);
    BOOL r = ps_timed(psW, w.b, PS_MEMSYNC, &ms);
    t_note("sync 300ms image: r=%d %.0f ms", r, ms);
    EXPECT_PLAYED(r);
    if (DEV) { CHECKF(ms >= 150.0, "%.0f ms", ms); CHECKF(ms < 3000.0, "%.0f ms", ms); }
    else CHECKF(ms < 2000.0, "0-device sync play took %.0f ms", ms);
    CHECK(ps_playing() == 0);         /* the sync play closed its header before returning */
    ps_stop(); wb_free(&w);
}

TEST(psd_sync_400ms_returns_after_image) {
    PS_REQ();
    wb_t w; double ms; ps_silent(&w, WAVE_FORMAT_PCM, 2, 44100, 16, 400);
    BOOL r = ps_timed(psW, w.b, PS_MEMSYNC, &ms);
    t_note("sync 400ms image: r=%d %.0f ms", r, ms);
    EXPECT_PLAYED(r);
    if (DEV) { CHECKF(ms >= 250.0, "%.0f ms", ms); CHECKF(ms < 3000.0, "%.0f ms", ms); }
    ps_stop(); wb_free(&w);
}

TEST(psd_sync_duration_scales_with_length) {
    PS_REQ();
    if (!DEV) { t_skip("needs a wave device"); return; }
    wb_t a, b; double ma, mb;
    ps_silent(&a, WAVE_FORMAT_PCM, 1, 22050, 16, 100);
    ps_silent(&b, WAVE_FORMAT_PCM, 1, 22050, 16, 700);
    CHECK(ps_timed(psW, a.b, PS_MEMSYNC, &ma) == TRUE);
    CHECK(ps_timed(psW, b.b, PS_MEMSYNC, &mb) == TRUE);
    t_note("100ms image %.0f ms, 700ms image %.0f ms", ma, mb);
    CHECKF(mb - ma > 300.0, "difference %.0f ms", mb - ma);
    CHECKF(mb >= 450.0 && mb < 3000.0, "%.0f ms", mb);
    ps_stop(); wb_free(&a); wb_free(&b);
}

/* ---- basic PCM / float formats ----------------------------------------- */
typedef struct { const char *name; WORD tag, ch; DWORD rate; WORD bits; int ok_dev; } fmtcase;
static const fmtcase g_basic[] = {
    { "pcm8_mono_11025",    1, 1, 11025,  8, 1 },
    { "pcm8_mono_8000",     1, 1,  8000,  8, 1 },
    { "pcm8_stereo_22050",  1, 2, 22050,  8, 1 },
    { "pcm16_mono_8000",    1, 1,  8000, 16, 1 },
    { "pcm16_mono_22050",   1, 1, 22050, 16, 1 },
    { "pcm16_stereo_44100", 1, 2, 44100, 16, 1 },
    { "pcm16_stereo_48000", 1, 2, 48000, 16, 1 },
    { "pcm16_mono_96000",   1, 1, 96000, 16, 1 },
    { "pcm24_stereo_48000", 1, 2, 48000, 24, 1 },
    { "pcm32_stereo_48000", 1, 2, 48000, 32, 1 },
    { "float32_stereo_44100", 3, 2, 44100, 32, 1 },
    { "float32_mono_48000", 3, 1, 48000, 32, 1 },
};

static void run_basic(int i, BOOL (*fn)(const void *, DWORD)) {
    const fmtcase *c = &g_basic[i]; wb_t w; double ms;
    ps_silent(&w, c->tag, c->ch, c->rate, c->bits, 120);
    BOOL r = ps_timed(fn, w.b, PS_MEMSYNC, &ms);
    t_note("%s: r=%d %.0f ms", c->name, r, ms);
    if (DEV) CHECKF(r == (BOOL)c->ok_dev, "%s: got %d", c->name, r);
    else CHECKF(r == FALSE, "%s: got %d with 0 devices", c->name, r);
    if (DEV && r) CHECKF(ms >= 40.0 && ms < 3000.0, "%s: %.0f ms", c->name, ms);
    wb_free(&w);
}
#define BASIC(i) TEST(psd_sync_fmt_##i) { PS_REQ(); run_basic(i, psW); ps_stop(); }
BASIC(0) BASIC(1) BASIC(2) BASIC(3) BASIC(4) BASIC(5) BASIC(6) BASIC(7) BASIC(8) BASIC(9) BASIC(10) BASIC(11)

TEST(psd_sync_fmt_table_ansi) {
    PS_REQ();
    for (int i = 0; i < (int)(sizeof g_basic / sizeof g_basic[0]); i++) run_basic(i, psA);
    ps_stop();
}

TEST(psd_sync_pcm16_stereo_44100_buffer_untouched) {
    PS_REQ();
    wb_t w; ps_silent(&w, WAVE_FORMAT_PCM, 2, 44100, 16, 80);
    BYTE *copy = HeapAlloc(GetProcessHeap(), 0, w.n); memcpy(copy, w.b, w.n);
    EXPECT_PLAYED(psW(w.b, PS_MEMSYNC));
    /* fmt body at offset 20 is DWORD aligned: the in-place realignment does not touch the image. */
    CHECK(memcmp(copy, w.b, w.n) == 0);
    HeapFree(GetProcessHeap(), 0, copy); ps_stop(); wb_free(&w);
}

/* ---- entry-point parity -------------------------------------------------- */
TEST(psd_sync_sndPlaySoundW_parity) {
    PS_REQ();
    wb_t w; double m1, m2; ps_silent(&w, WAVE_FORMAT_PCM, 1, 11025, 8, 200);
    double t0 = ps_now(); BOOL a = spsW(w.b, PS_MEMSYNC); m1 = ps_now() - t0;
    BOOL b = ps_timed(psW, w.b, PS_MEMSYNC, &m2);
    t_note("sndPlaySoundW %d %.0f ms, PlaySoundW %d %.0f ms", a, m1, b, m2);
    CHECK(a == b);
    EXPECT_PLAYED(a);
    if (DEV) CHECKF(m1 >= 100.0 && m1 < 3000.0, "%.0f ms", m1);
    ps_stop(); wb_free(&w);
}

TEST(psd_sync_sndPlaySoundA_parity) {
    PS_REQ();
    wb_t w; ps_silent(&w, WAVE_FORMAT_PCM, 1, 11025, 8, 100);
    /* PlaySoundA converts the name only when SND_MEMORY is clear (or SND_RESOURCE): the image is passed through. */
    BOOL a = spsA(w.b, PS_MEMSYNC), b = psA(w.b, PS_MEMSYNC), c = psW(w.b, PS_MEMSYNC);
    CHECK(a == c); CHECK(b == c);
    EXPECT_PLAYED(a);
    ps_stop(); wb_free(&w);
}

TEST(psd_sync_export_PlaySound_and_ordinal2) {
    PS_REQ();
    typedef BOOL (WINAPI *pfn)(LPCSTR, HMODULE, DWORD);
    pfn p = (pfn)t_proc_in("winmm.dll", "PlaySound"), o = (pfn)t_proc_ord("winmm.dll", 2);
    REQUIRE(p); REQUIRE(o);
    wb_t w; ps_silent(&w, WAVE_FORMAT_PCM, 1, 8000, 16, 100);
    EXPECT_PLAYED(p((LPCSTR)w.b, NULL, PS_MEMSYNC));
    EXPECT_PLAYED(o((LPCSTR)w.b, NULL, PS_MEMSYNC));
    EXPECT_PLAYED(U(PlaySoundA)((LPCSTR)w.b, NULL, PS_MEMSYNC));
    ps_stop(); wb_free(&w);
}

TEST(psd_sync_hmod_ignored_for_memory) {
    PS_REQ();
    wb_t w; ps_silent(&w, WAVE_FORMAT_PCM, 1, 8000, 16, 80);
    EXPECT_PLAYED(U(PlaySoundW)((LPCWSTR)w.b, GetModuleHandleA(NULL), PS_MEMSYNC));
    EXPECT_PLAYED(U(PlaySoundW)((LPCWSTR)w.b, t_module("winmm.dll"), PS_MEMSYNC));
    ps_stop(); wb_free(&w);
}

/* ---- flags accepted on a synchronous memory play ------------------------ */
typedef struct { const char *name; DWORD f; } flagcase;
static const flagcase g_syncflags[] = {
    { "plain",        PS_MEMSYNC },
    { "loop_no_async", PS_MEMSYNC | PS_LOOP },     /* soundOpen loops only when ASYNC|LOOP: plays once */
    { "purge",        PS_MEMSYNC | PS_PURGE },
    { "nowait",       PS_MEMSYNC | PS_NOWAIT },
    { "nostop_idle",  PS_MEMSYNC | PS_NOSTOP },
    { "application",  PS_MEMSYNC | PS_APPLICATION },
    { "alias_bit",    PS_MEMSYNC | PS_ALIAS },     /* name lookup bits are ignored for SND_MEMORY */
    { "filename_bit", PS_MEMSYNC | PS_FILENAME },
    { "system",       PS_MEMSYNC | PS_SYSTEM },
    { "purge_nowait", PS_MEMSYNC | PS_PURGE | PS_NOWAIT },
    { "loop_purge",   PS_MEMSYNC | PS_LOOP | PS_PURGE },
};
static void run_syncflag(int i) {
    wb_t w; double ms; ps_silent(&w, WAVE_FORMAT_PCM, 1, 8000, 16, 200);
    BOOL r = ps_timed(psW, w.b, g_syncflags[i].f, &ms);
    t_note("%s (0x%lx): r=%d %.0f ms", g_syncflags[i].name, g_syncflags[i].f, r, ms);
    EXPECT_PLAYED(r);
    if (DEV && r) CHECKF(ms >= 100.0 && ms < 3000.0, "%s: %.0f ms", g_syncflags[i].name, ms);
    CHECK(ps_playing() == 0);
    wb_free(&w);
}
#define SFLAG(i) TEST(psd_sync_flag_##i) { PS_REQ(); run_syncflag(i); ps_stop(); }
SFLAG(0) SFLAG(1) SFLAG(2) SFLAG(3) SFLAG(4) SFLAG(5) SFLAG(6) SFLAG(7) SFLAG(8) SFLAG(9) SFLAG(10)

TEST(psd_sync_loop_without_async_plays_once) {
    PS_REQ();
    if (!DEV) { CHECK(TRUE); t_skip("needs a wave device (0-device case covered by psd_sync_flag_1)"); return; }
    wb_t w; double ms; ps_silent(&w, WAVE_FORMAT_PCM, 1, 8000, 16, 300);
    BOOL r = ps_timed(psW, w.b, PS_MEMORY | PS_LOOP | PS_NODEFAULT, &ms);
    CHECK(r == TRUE);
    CHECKF(ms >= 150.0 && ms < 3000.0, "LOOP without ASYNC returned after %.0f ms", ms);
    CHECK(ps_playing() == 0);    /* nothing keeps looping after return */
    ps_stop(); wb_free(&w);
}

TEST(psd_sync_invalid_flags_rejected_before_play) {
    PS_REQ();
    wb_t w; ps_silent(&w, WAVE_FORMAT_PCM, 1, 8000, 16, 300);
    static const DWORD bad[] = { 0x20, 0x100, 0x200, 0x400, 0x800, 0x1000, 0x4000, 0x8000, 0x800000, 0x1000000, 0x80000000 };
    for (int i = 0; i < (int)(sizeof bad / sizeof bad[0]); i++) {
        double ms; BOOL r = ps_timed(psW, w.b, PS_MEMSYNC | bad[i], &ms);
        CHECKF(r == FALSE, "flag 0x%lx: %d", bad[i], r);
        CHECKF(ms < 150.0, "flag 0x%lx: rejected after %.0f ms (no play expected)", bad[i], ms);
    }
    ps_stop(); wb_free(&w);
}

TEST(psd_sync_repeated_plays) {
    PS_REQ();
    wb_t w; ps_silent(&w, WAVE_FORMAT_PCM, 1, 22050, 16, 60);
    for (int i = 0; i < 6; i++) EXPECT_PLAYED(psW(w.b, PS_MEMSYNC));
    CHECK(ps_playing() == 0);
    ps_stop(); wb_free(&w);
}

TEST(psd_sync_alternating_formats) {
    PS_REQ();
    wb_t a, b; ps_silent(&a, WAVE_FORMAT_PCM, 1, 11025, 8, 60); ps_silent(&b, WAVE_FORMAT_PCM, 2, 48000, 16, 60);
    for (int i = 0; i < 4; i++) { EXPECT_PLAYED(psW(a.b, PS_MEMSYNC)); EXPECT_PLAYED(psW(b.b, PS_MEMSYNC)); }
    ps_stop(); wb_free(&a); wb_free(&b);
}

TEST(psd_sync_buffer_free_after_return) {
    PS_REQ();
    wb_t w; ps_silent(&w, WAVE_FORMAT_PCM, 1, 8000, 16, 100);
    EXPECT_PLAYED(psW(w.b, PS_MEMSYNC));
    memset(w.b, 0xCC, w.n);       /* the sync play is over: the image is no longer referenced */
    wb_free(&w);
    Sleep(50);
    CHECK(ps_playing() == 0);
    ps_stop();
}

TEST(psd_sync_quiet_tone) {
    PS_REQ();
    wb_t w; ps_tone(&w, 44100, 880, 300);
    if (DEV) t_note("TONE hz=%d ms=%d", 880, 300);
    double ms; BOOL r = ps_timed(psW, w.b, PS_MEMSYNC, &ms);
    t_note("quiet tone 880 Hz: r=%d %.0f ms", r, ms);
    EXPECT_PLAYED(r);
    if (DEV) CHECKF(ms >= 150.0 && ms < 3000.0, "%.0f ms", ms);
    ps_stop(); wb_free(&w);
}

/* ---- the stop calls on an idle player ------------------------------------ */
TEST(psd_stop_variants_idle) {
    PS_REQ();
    CHECK(psW(NULL, 0) == TRUE);
    CHECK(psW(NULL, PS_NODEFAULT) == TRUE);
    CHECK(psW(NULL, PS_MEMORY) == TRUE);
    CHECK(psW(NULL, PS_MEMORY | PS_ASYNC) == TRUE);   /* ASYNC dropped when MEMORY+NULL: synchronous stop */
    CHECK(psW(NULL, PS_PURGE) == TRUE);
    CHECK(psW(NULL, PS_NOSTOP) == TRUE);              /* nothing playing: NOSTOP passes */
    CHECK(psA(NULL, 0) == TRUE);
    CHECK(spsW(NULL, 0) == TRUE);
    CHECK(spsA(NULL, 0) == TRUE);
    CHECK(ps_playing() == 0);
}

TEST(psd_stop_async_idle) {
    PS_REQ();
    /* NULL + ASYNC (no MEMORY): the stop is posted to winmm's notify window; PostMessage succeeds. */
    CHECK(psW(NULL, PS_ASYNC) == TRUE);
    CHECK(psW(NULL, PS_ASYNC | PS_NODEFAULT) == TRUE);
    CHECK(psA(NULL, PS_ASYNC) == TRUE);
    CHECK(ps_wait_playing(0, 2000));
    ps_stop();
}
