/* playsound_device: SND_ASYNC plays, looping, stopping, SND_NOSTOP, replacement, the image copy.
 *
 * Decomp (winmm sndPlaySoundI / mmWndProc / sndMessage / soundOpen):
 *  - SND_ASYNC creates winmm's notify window thread (CreatehwndNotify) and posts message 0x3CC to it; the API
 *    returns the PostMessage result (TRUE), before any parsing or device open. Failures happen later, silently.
 *  - SND_ASYNC|SND_MEMORY first requires 'RIFF' at 0 and 'WAVE' at 8, and RIFF length + 8 must not wrap below 8.
 *    Size = RIFF length + 8 in [1, 0x1FFFFF] -> the image is LocalAlloc-copied (the caller may free it at once);
 *    larger -> the caller's buffer is used as is.
 *  - SND_NOSTOP returns FALSE while a wave header is open (lpWavHdr), checked before anything else.
 *  - PlaySound(NULL, NULL, 0) stops synchronously (soundFree under SoundCritSec) and returns TRUE.
 *  - SND_ASYNC|SND_LOOP: dwLoops = 0xFFFFFFFF with WHDR_BEGINLOOP|WHDR_ENDLOOP: plays until stopped.
 */
#include "ps.h"

#define DEV (ps_ndev() > 0)

static void mk(wb_t *w, DWORD ms) { ps_silent(w, WAVE_FORMAT_PCM, 1, 22050, 16, ms); }

TEST(psd_async_returns_immediately) {
    PS_REQ();
    wb_t w; double ms; mk(&w, 1000);
    BOOL r = ps_timed(psW, w.b, PS_MEMASYNC, &ms);
    t_note("async 1 s image returned %d after %.0f ms", r, ms);
    CHECK(r == TRUE);                         /* posted: TRUE with and without devices */
    CHECKF(ms < 200.0, "async call took %.0f ms", ms);
    ps_stop(); wb_free(&w);
}

TEST(psd_async_plays_then_finishes) {
    PS_REQ();
    wb_t w; mk(&w, 250);
    CHECK(psW(w.b, PS_MEMASYNC) == TRUE);
    if (DEV) {
        CHECKF(ps_wait_playing(1, 2000), "async sound never started");
        CHECKF(ps_wait_playing(0, 3000), "async 250 ms sound still open after 3 s");
    } else {
        Sleep(300);
        CHECK(ps_playing() == 0);
    }
    ps_stop(); wb_free(&w);
}

TEST(psd_async_nostop_after_finish_true) {
    PS_REQ();
    wb_t w; mk(&w, 150);
    CHECK(psW(w.b, PS_MEMASYNC) == TRUE);
    if (DEV) CHECK(ps_wait_playing(1, 2000));
    CHECK(ps_wait_playing(0, 3000));
    /* The finished header was closed by the WOM_DONE handler: NOSTOP passes again. */
    CHECK(psW(w.b, PS_MEMASYNC | PS_NOSTOP) == TRUE);
    if (DEV) CHECK(ps_wait_playing(0, 3000));
    ps_stop(); wb_free(&w);
}

TEST(psd_async_loop_plays_until_stopped) {
    PS_REQ();
    wb_t w; mk(&w, 100);
    CHECK(psW(w.b, PS_MEMLOOP) == TRUE);
    if (DEV) {
        CHECKF(ps_wait_playing(1, 2000), "loop never started");
        Sleep(600);                                   /* six times the image length */
        CHECKF(ps_playing() == 1, "loop ended on its own");
        CHECK(psW(NULL, 0) == TRUE);
        CHECKF(ps_playing() == 0, "stop is synchronous");
    } else {
        Sleep(300); CHECK(ps_playing() == 0);
        CHECK(psW(NULL, 0) == TRUE);
    }
    ps_stop(); wb_free(&w);
}

TEST(psd_async_loop_stop_variants) {
    PS_REQ();
    if (!DEV) { t_skip("needs a wave device"); return; }
    wb_t w; mk(&w, 100);
    struct { const char *n; int how; } v[] = { { "W NULL 0", 0 }, { "A NULL 0", 1 }, { "sndW NULL", 2 }, { "sndA NULL", 3 },
                                               { "W NULL MEMORY|ASYNC", 4 }, { "W NULL PURGE", 5 } };
    for (int i = 0; i < 6; i++) {
        CHECK(psW(w.b, PS_MEMLOOP) == TRUE);
        CHECKF(ps_wait_playing(1, 2000), "%s: loop never started", v[i].n);
        BOOL r = FALSE;
        switch (v[i].how) {
        case 0: r = psW(NULL, 0); break;
        case 1: r = psA(NULL, 0); break;
        case 2: r = spsW(NULL, 0); break;
        case 3: r = spsA(NULL, 0); break;
        case 4: r = psW(NULL, PS_MEMORY | PS_ASYNC); break;
        case 5: r = psW(NULL, PS_PURGE); break;
        }
        CHECKF(r == TRUE, "%s: %d", v[i].n, r);
        CHECKF(ps_playing() == 0, "%s: still playing", v[i].n);
    }
    ps_stop(); wb_free(&w);
}

TEST(psd_async_loop_stop_posted) {
    PS_REQ();
    if (!DEV) { t_skip("needs a wave device"); return; }
    wb_t w; mk(&w, 100);
    CHECK(psW(w.b, PS_MEMLOOP) == TRUE);
    CHECK(ps_wait_playing(1, 2000));
    /* NULL + SND_ASYNC without MEMORY: the stop goes through the notify window (asynchronously). */
    CHECK(psW(NULL, PS_ASYNC) == TRUE);
    CHECKF(ps_wait_playing(0, 2000), "posted stop did not stop the loop");
    ps_stop(); wb_free(&w);
}

TEST(psd_async_nostop_while_loop_false) {
    PS_REQ();
    wb_t a, b; mk(&a, 100); ps_silent(&b, WAVE_FORMAT_PCM, 1, 8000, 8, 100);
    CHECK(psW(a.b, PS_MEMLOOP) == TRUE);
    if (!DEV) {
        Sleep(200);
        CHECK(psW(b.b, PS_MEMASYNC | PS_NOSTOP) == TRUE);   /* nothing could open: NOSTOP passes */
        ps_stop(); wb_free(&a); wb_free(&b); return;
    }
    CHECK(ps_wait_playing(1, 2000));
    CHECK(psW(b.b, PS_MEMASYNC | PS_NOSTOP) == FALSE);
    CHECK(psW(b.b, PS_MEMSYNC | PS_NOSTOP) == FALSE);
    CHECK(psW(b.b, PS_MEMLOOP | PS_NOSTOP) == FALSE);
    CHECK(psA(b.b, PS_MEMSYNC | PS_NOSTOP) == FALSE);
    CHECK(spsW(b.b, PS_MEMSYNC | PS_NOSTOP) == FALSE);
    CHECK(spsA(b.b, PS_MEMSYNC | PS_NOSTOP) == FALSE);
    /* NOSTOP is checked before the NULL stop too. */
    CHECK(psW(NULL, PS_NOSTOP) == FALSE);
    CHECK(psW(L"", PS_NOSTOP | PS_NODEFAULT) == FALSE);
    CHECKF(ps_playing() == 1, "a failed NOSTOP call must not stop the loop");
    ps_stop();
    CHECK(ps_playing() == 0);
    /* After the stop NOSTOP passes. */
    CHECK(psW(b.b, PS_MEMSYNC | PS_NOSTOP) == TRUE);
    CHECK(psW(NULL, PS_NOSTOP) == TRUE);
    ps_stop(); wb_free(&a); wb_free(&b);
}

TEST(psd_async_nostop_invalid_flag_precedes) {
    PS_REQ();
    wb_t w; mk(&w, 100);
    CHECK(psW(w.b, PS_MEMLOOP) == TRUE);
    if (DEV) CHECK(ps_wait_playing(1, 2000));
    /* The flag mask is checked before NOSTOP: FALSE either way, and the loop keeps going. */
    CHECK(psW(w.b, PS_MEMASYNC | PS_NOSTOP | 0x20) == FALSE);
    if (DEV) CHECK(ps_playing() == 1);
    ps_stop(); wb_free(&w);
}

TEST(psd_async_replace_loop_with_loop) {
    PS_REQ();
    wb_t a, b; mk(&a, 100); ps_silent(&b, WAVE_FORMAT_PCM, 2, 44100, 16, 100);
    CHECK(psW(a.b, PS_MEMLOOP) == TRUE);
    if (DEV) CHECK(ps_wait_playing(1, 2000));
    CHECK(psW(b.b, PS_MEMLOOP) == TRUE);        /* replaces the first sound, no error */
    if (DEV) { Sleep(300); CHECK(ps_playing() == 1); }
    ps_stop();
    CHECK(ps_playing() == 0);
    wb_free(&a); wb_free(&b);
}

TEST(psd_async_replace_chain) {
    PS_REQ();
    wb_t w[4];
    for (int i = 0; i < 4; i++) ps_silent(&w[i], WAVE_FORMAT_PCM, (WORD)(1 + (i & 1)), 11025u * (1 + i), 16, 100);
    for (int i = 0; i < 4; i++) CHECK(psW(w[i].b, PS_MEMLOOP) == TRUE);
    if (DEV) { CHECK(ps_wait_playing(1, 2000)); Sleep(200); CHECK(ps_playing() == 1); }
    ps_stop();
    CHECK(ps_playing() == 0);
    for (int i = 0; i < 4; i++) wb_free(&w[i]);
}

TEST(psd_async_replace_loop_with_oneshot) {
    PS_REQ();
    if (!DEV) { t_skip("needs a wave device"); return; }
    wb_t a, b; mk(&a, 100); mk(&b, 150);
    CHECK(psW(a.b, PS_MEMLOOP) == TRUE);
    CHECK(ps_wait_playing(1, 2000));
    CHECK(psW(b.b, PS_MEMASYNC) == TRUE);
    /* The one-shot replaced the loop, so the player goes idle on its own. */
    CHECKF(ps_wait_playing(0, 3000), "loop survived its replacement");
    ps_stop(); wb_free(&a); wb_free(&b);
}

TEST(psd_sync_replaces_async_loop) {
    PS_REQ();
    wb_t a, b; mk(&a, 100); mk(&b, 150);
    CHECK(psW(a.b, PS_MEMLOOP) == TRUE);
    if (DEV) CHECK(ps_wait_playing(1, 2000));
    double ms; BOOL r = ps_timed(psW, b.b, PS_MEMSYNC, &ms);
    if (DEV) { CHECK(r == TRUE); CHECKF(ms < 3000.0, "%.0f ms", ms); CHECKF(ps_playing() == 0, "loop survived a sync play"); }
    else CHECK(r == FALSE);
    ps_stop(); wb_free(&a); wb_free(&b);
}

TEST(psd_async_then_immediate_stop) {
    PS_REQ();
    wb_t w; mk(&w, 100);
    int late = 0;
    for (int i = 0; i < 5; i++) {
        CHECK(psW(w.b, PS_MEMLOOP) == TRUE);
        CHECK(psW(NULL, 0) == TRUE);
        /* The stop posts 0x3CD before freeing; a 0x3CC still queued sees it and is dropped. A 0x3CC the notify
           thread already took off the queue can still start after the stop (race), so this is only counted. */
        Sleep(150);
        late += ps_playing();
        ps_stop();
        CHECK(ps_playing() == 0);
    }
    t_note("loop still started after an immediate stop: %d of 5", late);
    wb_free(&w);
}

TEST(psd_async_then_sync_cancels_pending) {
    PS_REQ();
    wb_t a, b; mk(&a, 100); mk(&b, 80);
    CHECK(psW(a.b, PS_MEMLOOP) == TRUE);
    BOOL r = psW(b.b, PS_MEMSYNC);
    if (DEV) CHECK(r == TRUE); else CHECK(r == FALSE);
    Sleep(200);
    int p = ps_playing();
    t_note("async loop queued just before a sync play: playing afterwards=%d", p);
    ps_stop(); wb_free(&a); wb_free(&b);
}

/* ---- the copy of small images --------------------------------------------- */
TEST(psd_async_image_copied_overwrite) {
    PS_REQ();
    wb_t w; mk(&w, 100);
    CHECK(psW(w.b, PS_MEMLOOP) == TRUE);
    memset(w.b, 0xCC, w.n);            /* the posted play uses the copy, not this buffer */
    if (DEV) { CHECKF(ps_wait_playing(1, 2000), "copied image did not play"); }
    else { Sleep(200); CHECK(ps_playing() == 0); }
    ps_stop(); wb_free(&w);
}

TEST(psd_async_image_copied_free) {
    PS_REQ();
    wb_t w; mk(&w, 100);
    BYTE *p = VirtualAlloc(NULL, w.n, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    REQUIRE(p);
    memcpy(p, w.b, w.n);
    CHECK(psW(p, PS_MEMLOOP) == TRUE);
    VirtualFree(p, 0, MEM_RELEASE);    /* now unmapped: any later read of it would fault */
    if (DEV) CHECKF(ps_wait_playing(1, 2000), "copied image did not play");
    else { Sleep(200); CHECK(ps_playing() == 0); }
    ps_stop(); wb_free(&w);
}

TEST(psd_async_oneshot_copied_free) {
    PS_REQ();
    wb_t w; mk(&w, 200);
    BYTE *p = VirtualAlloc(NULL, w.n, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    REQUIRE(p);
    memcpy(p, w.b, w.n);
    CHECK(psW(p, PS_MEMASYNC) == TRUE);
    VirtualFree(p, 0, MEM_RELEASE);
    if (DEV) CHECK(ps_wait_playing(1, 2000));
    CHECK(ps_wait_playing(0, 3000));
    ps_stop(); wb_free(&w);
}

TEST(psd_async_caller_buffer_unchanged) {
    PS_REQ();
    /* Misaligned fmt body (JUNK of 2 bytes first): the realignment happens in the copy, not in the caller's image. */
    wb_t w; wb_init(&w, 4096);
    wb_chunk(&w, "JUNK", 2); wb_u16(&w, 0x5A5A);
    wb_fmt(&w, WAVE_FORMAT_PCM, 1, 8000, 16);
    wb_data(&w, 1600, 0); wb_finish(&w);
    BYTE *copy = HeapAlloc(GetProcessHeap(), 0, w.n); memcpy(copy, w.b, w.n);
    CHECK(psW(w.b, PS_MEMLOOP) == TRUE);
    if (DEV) CHECK(ps_wait_playing(1, 2000)); else Sleep(200);
    CHECK(memcmp(copy, w.b, w.n) == 0);
    HeapFree(GetProcessHeap(), 0, copy); ps_stop(); wb_free(&w);
}

TEST(psd_async_copy_boundary_1fffff) {
    PS_REQ();
    /* Size exactly 0x1FFFFF (RIFF length 0x1FFFF7): still copied. */
    wb_t w; wb_init(&w, 0x200000 + 64);
    wb_fmt(&w, WAVE_FORMAT_PCM, 1, 48000, 8);
    wb_data(&w, 0x1FFFFF - 44, 0x80); wb_finish(&w);
    CHECK(w.n == 0x1FFFFF);
    CHECK(psW(w.b, PS_MEMLOOP) == TRUE);
    memset(w.b, 0xCC, 64);             /* header gone: only a copy can still play */
    if (DEV) CHECKF(ps_wait_playing(1, 3000), "0x1FFFFF-byte image was not copied");
    else { Sleep(200); CHECK(ps_playing() == 0); }
    ps_stop(); wb_free(&w);
}

TEST(psd_async_large_image_not_copied_plays) {
    PS_REQ();
    /* Size 0x200000 + : played from the caller's buffer, which must stay valid until the stop. */
    wb_t w; wb_init(&w, 0x210000);
    wb_fmt(&w, WAVE_FORMAT_PCM, 1, 48000, 8);
    wb_data(&w, 0x200000, 0x80); wb_finish(&w);
    CHECK(psW(w.b, PS_MEMLOOP) == TRUE);
    if (DEV) CHECKF(ps_wait_playing(1, 3000), "large image did not play");
    else { Sleep(200); CHECK(ps_playing() == 0); }
    ps_stop();
    wb_free(&w);
}

TEST(psd_async_large_image_overwritten_header) {
    PS_REQ();
    wb_t w; wb_init(&w, 0x210000);
    wb_fmt(&w, WAVE_FORMAT_PCM, 1, 48000, 8);
    wb_data(&w, 0x200000, 0x80); wb_finish(&w);
    CHECK(psW(w.b, PS_MEMLOOP) == TRUE);
    memcpy(w.b, "XXXX", 4);           /* not copied: the window thread may now see a broken image */
    Sleep(400);
    int p = ps_playing();
    t_note("large async image, header overwritten right after the call: playing=%d", p);
    if (!DEV) CHECK(p == 0);
    ps_stop();
    wb_free(&w);
}

/* ---- the RIFF/WAVE gate and size wrap of ASYNC|MEMORY ---------------------- */
TEST(psd_async_riff_gate) {
    PS_REQ();
    wb_t w; mk(&w, 50);
    memcpy(w.b, "RIFX", 4); CHECK(psW(w.b, PS_MEMASYNC) == FALSE);
    memcpy(w.b, "riff", 4); CHECK(psW(w.b, PS_MEMASYNC) == FALSE);
    memcpy(w.b, "RIFF", 4); memcpy(w.b + 8, "WAVX", 4); CHECK(psW(w.b, PS_MEMASYNC) == FALSE);
    memcpy(w.b + 8, "wave", 4); CHECK(psW(w.b, PS_MEMASYNC) == FALSE);
    memcpy(w.b + 8, "WAVE", 4);
    wb_set_riff(&w, 0xFFFFFFF8); CHECK(psW(w.b, PS_MEMASYNC) == FALSE);    /* size wraps to 0 */
    wb_set_riff(&w, 0xFFFFFFFF); CHECK(psW(w.b, PS_MEMASYNC) == FALSE);    /* wraps to 7 */
    wb_set_riff(&w, 0xFFFFFFFA); CHECK(psW(w.b, PS_MEMASYNC) == FALSE);
    wb_finish(&w);
    CHECK(psW(w.b, PS_MEMASYNC) == TRUE);
    ps_stop(); wb_free(&w);
}

TEST(psd_async_gate_is_ascii_too) {
    PS_REQ();
    wb_t w; mk(&w, 50);
    memcpy(w.b, "RIFX", 4);
    CHECK(psA(w.b, PS_MEMASYNC) == FALSE);
    CHECK(spsW(w.b, PS_MEMASYNC) == FALSE);
    CHECK(spsA(w.b, PS_MEMASYNC) == FALSE);
    memcpy(w.b, "RIFF", 4);
    CHECK(psA(w.b, PS_MEMASYNC) == TRUE);
    CHECK(spsA(w.b, PS_MEMASYNC) == TRUE);
    ps_stop(); wb_free(&w);
}

/* ---- PURGE / NOWAIT / other accepted bits with ASYNC --------------------- */
typedef struct { const char *n; DWORD f; } aflag;
static const aflag g_aflags[] = {
    { "purge", PS_MEMLOOP | PS_PURGE }, { "nowait", PS_MEMLOOP | PS_NOWAIT }, { "application", PS_MEMLOOP | PS_APPLICATION },
    { "alias_bit", PS_MEMLOOP | PS_ALIAS }, { "filename_bit", PS_MEMLOOP | PS_FILENAME }, { "system", PS_MEMLOOP | PS_SYSTEM },
    { "purge_nowait", PS_MEMLOOP | PS_PURGE | PS_NOWAIT },
};
static void run_aflag(int i) {
    wb_t w; mk(&w, 100);
    BOOL r = psW(w.b, g_aflags[i].f);
    CHECKF(r == TRUE, "%s: %d", g_aflags[i].n, r);
    if (DEV) { int ok = ps_wait_playing(1, 2000); t_note("%s: playing=%d", g_aflags[i].n, ok); CHECK(ok); }
    else { Sleep(150); CHECK(ps_playing() == 0); }
    ps_stop(); CHECK(ps_playing() == 0);
    wb_free(&w);
}
#define AFLAG(i) TEST(psd_async_flag_##i) { PS_REQ(); run_aflag(i); ps_stop(); }
AFLAG(0) AFLAG(1) AFLAG(2) AFLAG(3) AFLAG(4) AFLAG(5) AFLAG(6)

TEST(psd_purge_stops_loop) {
    PS_REQ();
    wb_t w; mk(&w, 100);
    CHECK(psW(w.b, PS_MEMLOOP) == TRUE);
    if (DEV) CHECK(ps_wait_playing(1, 2000));
    BOOL r = psW(NULL, PS_PURGE);
    t_note("PlaySound(NULL, NULL, SND_PURGE) during a loop = %d", r);
    CHECK(r == TRUE);
    CHECK(ps_playing() == 0);
    ps_stop(); wb_free(&w);
}

TEST(psd_async_nodefault_absent_still_memory_ok) {
    PS_REQ();
    /* Without SND_NODEFAULT a valid memory image still plays (no default sound is involved). */
    wb_t w; mk(&w, 100);
    CHECK(psW(w.b, PS_MEMORY | PS_ASYNC | PS_LOOP) == TRUE);
    if (DEV) CHECK(ps_wait_playing(1, 2000));
    ps_stop(); wb_free(&w);
}

TEST(psd_async_many_posts_bounded) {
    PS_REQ();
    wb_t w; mk(&w, 60);
    double t0 = ps_now();
    for (int i = 0; i < 20; i++) CHECK(psW(w.b, PS_MEMASYNC) == TRUE);
    CHECKF(ps_now() - t0 < 1000.0, "20 posts took %.0f ms", ps_now() - t0);
    CHECK(ps_wait_playing(0, 5000));
    ps_stop(); wb_free(&w);
}
