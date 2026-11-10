#include "mh.h"
/* The global handle list under many live handles, closes from the middle of the list, handle values in callbacks,
 * caps queries through a handle, and midiOut / mixer handles across threads. VM_TIMEOUT: 180 */

#define NMANY 64

/* 64 live mmio handles: all distinct and valid; closing every third leaves the others valid; stale ones are 5. */
TEST(mh_list_many_mmio_handles) {
    REQUIRE(U(mmioOpenA)); REQUIRE(U(mmioClose)); REQUIRE(U(mmioWrite)); REQUIRE(U(mmioSeek));
    static char mem[NMANY][32];
    HMMIO h[NMANY];
    for (int i = 0; i < NMANY; i++) {
        MMIOINFO mi; memset(&mi, 0, sizeof mi);
        mi.fccIOProc = FOURCC_MEM; mi.pchBuffer = mem[i]; mi.cchBuffer = sizeof mem[i];
        h[i] = U(mmioOpenA)(NULL, &mi, MMIO_READWRITE);
        CHECKF(h[i] != NULL, "open %d", i);
        if (h[i]) EXPECT_EQ(U(mmioWrite)(h[i], (char *)&i, 1), 1);
    }
    for (int i = 0; i < NMANY; i++) for (int k = i + 1; k < NMANY; k++) if (h[i] && h[k] && h[i] == h[k]) CHECKF(0, "dup %d %d", i, k);
    for (int i = 0; i < NMANY; i += 3) if (h[i]) EXPECT_EQ(U(mmioClose)(h[i], 0), 0);
    for (int i = 0; i < NMANY; i++) {
        if (!h[i]) continue;
        if (i % 3 == 0) {
            EXPECT_EQ(U(mmioSeek)(h[i], 0, SEEK_CUR), -1);
            EXPECT_EQ(U(mmioClose)(h[i], 0), MMSYSERR_INVALHANDLE);
        } else {
            EXPECT_EQ(U(mmioSeek)(h[i], 0, SEEK_CUR), 1);
            EXPECT_EQ(mem[i][0], (char)i);
        }
    }
    /* close the rest newest-first, then oldest-first parity */
    for (int i = NMANY - 1; i >= 0; i--) if (h[i] && i % 3 == 1) EXPECT_EQ(U(mmioClose)(h[i], 0), 0);
    for (int i = 0; i < NMANY; i++) if (h[i] && i % 3 == 2) EXPECT_EQ(U(mmioClose)(h[i], 0), 0);
    for (int i = 0; i < NMANY; i++) if (h[i]) EXPECT_EQ(U(mmioClose)(h[i], 0), MMSYSERR_INVALHANDLE);
}

/* WOW values over a long list: lookups keep working while entries are removed from the middle. */
TEST(mh_list_wow_lookup_with_removals) {
    winmmbaseHandle32FromHandle16_pfn h32 = WB(winmmbaseHandle32FromHandle16);
    winmmbaseSetWOWHandle_pfn set = WB(winmmbaseSetWOWHandle);
    winmmbaseGetWOWHandle_pfn get = WB(winmmbaseGetWOWHandle);
    REQUIRE(h32); REQUIRE(set); REQUIRE(get); REQUIRE(U(mmioClose));
    HMMIO h[NMANY];
    for (int i = 0; i < NMANY; i++) {
        h[i] = mh_mmio_open();
        if (h[i]) set((HANDLE)h[i], (WORD)(0xC000 + i));
    }
    for (int i = 0; i < NMANY; i++) if (h[i]) { EXPECT_EQ(get((HANDLE)h[i]), 0xC000 + i); EXPECT_EQ(h32((WORD)(0xC000 + i)), (DWORD)(ULONG_PTR)h[i]); }
    for (int i = 1; i < NMANY; i += 2) if (h[i]) { EXPECT_EQ(U(mmioClose)(h[i], 0), 0); }
    for (int i = 0; i < NMANY; i++) {
        if (!h[i]) continue;
        ULONG_PTR want = (i & 1) ? 0 : (DWORD)(ULONG_PTR)h[i];
        EXPECT_EQ(h32((WORD)(0xC000 + i)), want);
    }
    for (int i = 0; i < NMANY; i += 2) if (h[i]) { set((HANDLE)h[i], 0); EXPECT_EQ(U(mmioClose)(h[i], 0), 0); }
    for (int i = 0; i < NMANY; i++) EXPECT_EQ(h32((WORD)(0xC000 + i)), 0);
}

/* A freed handle's address may be reused by the next handle: the old value is then valid again (as the new
 * handle). Recorded, not asserted. */
TEST(mh_list_address_reuse_note) {
    REQUIRE(U(mmioClose)); REQUIRE(U(mmioGetInfo));
    HMMIO a = mh_mmio_open();
    if (!a) return;
    EXPECT_EQ(U(mmioClose)(a, 0), 0);
    HMMIO b = mh_mmio_open();
    if (!b) return;
    MMIOINFO mi;
    MMRESULT r = U(mmioGetInfo)(a, &mi, 0);
    t_note("closed %p, next open %p (%s); mmioGetInfo(old value) -> %u", a, b, a == b ? "reused" : "new address", r);
    if (a == b) EXPECT_EQ(r, 0); else EXPECT_EQ(r, MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(mmioClose)(b, 0), 0);
}

/* 16 live waveOut handles at once (shared mode), all distinct, all valid, each closes once. */
TEST(mh_list_many_waveout_handles) {
    REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutClose)); REQUIRE(U(waveOutGetID));
    if (!mh_devs()->wo) { t_note("no waveOut device"); return; }
    HWAVEOUT h[16]; int n = 0;
    for (int i = 0; i < 16; i++) {
        h[i] = NULL;
        MMRESULT r = mh_waveout_open(&h[i], NULL);
        if (r) { t_note("open %d -> %u", i, r); break; }
        n++;
    }
    CHECKF(n == 16, "opened %d of 16", n);
    for (int i = 0; i < n; i++) {
        UINT id = 0; EXPECT_EQ(U(waveOutGetID)(h[i], &id), 0); EXPECT_EQ(id, WAVE_MAPPER);
        for (int k = i + 1; k < n; k++) CHECK(h[i] != h[k]);
    }
    for (int i = 0; i < n; i += 2) EXPECT_EQ(U(waveOutClose)(h[i]), 0);
    for (int i = 1; i < n; i += 2) { UINT id; EXPECT_EQ(U(waveOutGetID)(h[i], &id), 0); }
    for (int i = 1; i < n; i += 2) EXPECT_EQ(U(waveOutClose)(h[i]), 0);
    for (int i = 0; i < n; i++) EXPECT_EQ(U(waveOutClose)(h[i]), MMSYSERR_INVALHANDLE);
}

/* ---- handle values seen by callbacks ---- */
typedef struct { HWAVEOUT h[3]; UINT msg[3]; DWORD tid[3]; volatile LONG n; } cbrec_t;
static void CALLBACK wo_cb(HWAVEOUT hwo, UINT msg, DWORD_PTR inst, DWORD_PTR p1, DWORD_PTR p2) {
    cbrec_t *r = (cbrec_t *)inst;
    LONG i = InterlockedIncrement(&r->n) - 1;
    if (i < 3) { r->h[i] = hwo; r->msg[i] = msg; r->tid[i] = GetCurrentThreadId(); }
}
TEST(mh_callback_function_receives_handle) {
    REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutClose));
    if (!mh_devs()->wo) { t_note("no waveOut device"); return; }
    static cbrec_t rec; memset(&rec, 0, sizeof rec);
    WAVEFORMATEX wf; mh_pcm(&wf);
    HWAVEOUT h = NULL;
    MMRESULT r = U(waveOutOpen)(&h, WAVE_MAPPER, &wf, (DWORD_PTR)wo_cb, (DWORD_PTR)&rec, CALLBACK_FUNCTION);
    EXPECT_EQ(r, 0);
    if (r) return;
    EXPECT_EQ(mh_play_silence(h, 30, 3000), 0);
    EXPECT_EQ(U(waveOutClose)(h), 0);
    for (int w = 0; w < 100 && rec.n < 3; w++) Sleep(10);   /* up to 1 s for the last callback */
    Sleep(20);
    EXPECT_EQ(rec.n, 3);
    if (rec.n == 3) {
        EXPECT_EQ(rec.msg[0], WOM_OPEN); EXPECT_EQ(rec.msg[1], WOM_DONE); EXPECT_EQ(rec.msg[2], WOM_CLOSE);
        for (int i = 0; i < 3; i++) EXPECT_EQ(rec.h[i], h);   /* the same value the open returned */
        t_note("callback threads: open %lu done %lu close %lu (caller %lu)", rec.tid[0], rec.tid[1], rec.tid[2], GetCurrentThreadId());
    }
}

/* CALLBACK_EVENT: the handle is valid inside the open/close event sequence; WOM_* through a thread queue carry it. */
typedef struct { DWORD tid; HANDLE ready; HWAVEOUT seen[4]; UINT msgs[4]; int n; } qthr_t;
static DWORD WINAPI queue_thread(void *p) {
    qthr_t *q = (qthr_t *)p; MSG m;
    PeekMessageA(&m, NULL, 0, 0, PM_NOREMOVE);
    SetEvent(q->ready);
    DWORD t0 = GetTickCount();
    while (q->n < 3 && GetTickCount() - t0 < 5000) {
        if (MsgWaitForMultipleObjects(0, NULL, FALSE, 100, QS_ALLPOSTMESSAGE) != WAIT_OBJECT_0) continue;
        while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) {
            if (m.message >= MM_WOM_OPEN && m.message <= MM_WOM_DONE && q->n < 4) {
                q->seen[q->n] = (HWAVEOUT)m.wParam; q->msgs[q->n] = m.message; q->n++;
            }
        }
    }
    return 0;
}
TEST(mh_callback_thread_messages_carry_handle) {
    REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutClose));
    if (!mh_devs()->wo) { t_note("no waveOut device"); return; }
    qthr_t q; memset(&q, 0, sizeof q);
    q.ready = CreateEventA(NULL, TRUE, FALSE, NULL);
    HANDLE t = CreateThread(NULL, 0, queue_thread, &q, 0, &q.tid);
    if (!t) { CloseHandle(q.ready); return; }
    WaitForSingleObject(q.ready, 5000);
    WAVEFORMATEX wf; mh_pcm(&wf);
    HWAVEOUT h = NULL;
    MMRESULT r = U(waveOutOpen)(&h, WAVE_MAPPER, &wf, q.tid, 0, CALLBACK_THREAD);
    EXPECT_EQ(r, 0);
    if (!r) {
        EXPECT_EQ(mh_play_silence(h, 30, 3000), 0);
        EXPECT_EQ(U(waveOutClose)(h), 0);
    }
    WaitForSingleObject(t, 6000); CloseHandle(t); CloseHandle(q.ready);
    if (r) return;
    EXPECT_EQ(q.n, 3);
    for (int i = 0; i < q.n && i < 3; i++) EXPECT_EQ(q.seen[i], h);
    if (q.n == 3) { EXPECT_EQ(q.msgs[0], MM_WOM_OPEN); EXPECT_EQ(q.msgs[1], MM_WOM_DONE); EXPECT_EQ(q.msgs[2], MM_WOM_CLOSE); }
}

/* ---- caps through a handle ---- */
TEST(mh_devcaps_through_handle) {
    REQUIRE(U(waveOutGetDevCapsA)); REQUIRE(U(waveOutOpen)); REQUIRE(U(waveInGetDevCapsA)); REQUIRE(U(waveInOpen));
    const mh_counts *c = mh_devs();
    if (c->wo) {
        WAVEFORMATEX wf; mh_pcm(&wf);
        HWAVEOUT h = NULL;
        EXPECT_EQ(U(waveOutOpen)(&h, 0, &wf, 0, 0, CALLBACK_NULL), 0);
        if (h) {
            WAVEOUTCAPSA a, b; memset(&a, 0, sizeof a); memset(&b, 0, sizeof b);
            EXPECT_EQ(U(waveOutGetDevCapsA)((UINT_PTR)h, &a, sizeof a), 0);
            EXPECT_EQ(U(waveOutGetDevCapsA)(0, &b, sizeof b), 0);
            EXPECT_STR(a.szPname, b.szPname);
            EXPECT_EQ(a.wMid, b.wMid); EXPECT_EQ(a.wPid, b.wPid);
            EXPECT_EQ(U(waveOutGetDevCapsA)((UINT_PTR)h, &a, 0), 0);    /* cb 0 -> 0 before anything else */
            EXPECT_EQ(U(waveOutGetDevCapsA)((UINT_PTR)h, NULL, sizeof a), MMSYSERR_INVALPARAM);
            EXPECT_EQ(U(waveOutClose)(h), 0);
            /* after close the value is an (out of range) id */
            if ((DWORD)(ULONG_PTR)h >= c->wo && (DWORD)(ULONG_PTR)h != 0xFFFFFFFF && (ULONG_PTR)h != 0xFFFF)
                EXPECT_EQ(U(waveOutGetDevCapsA)((UINT_PTR)h, &a, sizeof a), MMSYSERR_BADDEVICEID);
        }
    }
    if (c->wi) {
        HWAVEIN h = NULL;
        WAVEFORMATEX wf; mh_pcm(&wf);
        EXPECT_EQ(U(waveInOpen)(&h, 0, &wf, 0, 0, CALLBACK_NULL), 0);
        if (h) {
            WAVEINCAPSA a, b; memset(&a, 0, sizeof a); memset(&b, 0, sizeof b);
            EXPECT_EQ(U(waveInGetDevCapsA)((UINT_PTR)h, &a, sizeof a), 0);
            EXPECT_EQ(U(waveInGetDevCapsA)(0, &b, sizeof b), 0);
            EXPECT_STR(a.szPname, b.szPname);
            EXPECT_EQ(U(waveInClose)(h), 0);
        }
    }
    /* a live mmio handle is not a wave handle: as an id it is out of range */
    HMMIO m = mh_mmio_open();
    if (m) {
        WAVEOUTCAPSA a;
        if ((DWORD)(ULONG_PTR)m >= c->wo && (DWORD)(ULONG_PTR)m != 0xFFFFFFFF)
            EXPECT_EQ(U(waveOutGetDevCapsA)((UINT_PTR)m, &a, sizeof a), MMSYSERR_BADDEVICEID);
        U(mmioClose)(m, 0);
    }
}

TEST(mh_midiout_devcaps_through_handle) {
    REQUIRE(U(midiOutGetDevCapsA)); REQUIRE(U(midiOutOpen)); REQUIRE(U(midiOutClose));
    const mh_counts *c = mh_devs();
    if (!c->wo || !c->mo) { t_note("no waveOut device"); return; }
    HMIDIOUT h = NULL;
    EXPECT_EQ(U(midiOutOpen)(&h, 0, 0, 0, CALLBACK_NULL), 0);
    if (!h) return;
    MIDIOUTCAPSA a, b; memset(&a, 0, sizeof a); memset(&b, 0, sizeof b);
    EXPECT_EQ(U(midiOutGetDevCapsA)((UINT_PTR)h, &a, sizeof a), 0);
    EXPECT_EQ(U(midiOutGetDevCapsA)(0, &b, sizeof b), 0);
    EXPECT_STR(a.szPname, b.szPname);
    EXPECT_EQ(a.wTechnology, b.wTechnology);
    EXPECT_EQ(U(midiOutClose)(h), 0);
}

/* ---- midiOut / mixer across threads ---- */
typedef struct { HMIDIOUT mo; HMIXER mx; DWORD r1, r2; } open_t;
static DWORD WINAPI midi_mixer_open_thread(void *p) {
    open_t *o = (open_t *)p;
    o->r1 = U(midiOutOpen)(&o->mo, 0, 0, 0, CALLBACK_NULL);
    o->r2 = U(mixerOpen)(&o->mx, 0, 0, 0, 0);
    return 0;
}
TEST(mh_thread_midiout_mixer_open_a_close_b) {
    REQUIRE(U(midiOutOpen)); REQUIRE(U(midiOutClose)); REQUIRE(U(mixerOpen)); REQUIRE(U(mixerClose)); REQUIRE(U(midiOutShortMsg));
    const mh_counts *c = mh_devs();
    if (!c->wo || !c->mx) { t_note("needs waveOut + mixer devices (have %u/%u)", c->wo, c->mx); return; }
    open_t o; memset(&o, 0, sizeof o);
    HANDLE t = CreateThread(NULL, 0, midi_mixer_open_thread, &o, 0, NULL);
    if (!t) return;
    WaitForSingleObject(t, 10000); CloseHandle(t);
    EXPECT_EQ(o.r1, 0);
    EXPECT_EQ(o.r2, 0);
    if (o.mo) {
        EXPECT_EQ(U(midiOutShortMsg)(o.mo, 0x000078B0), 0);
        EXPECT_EQ(U(midiOutClose)(o.mo), 0);
        EXPECT_EQ(U(midiOutClose)(o.mo), MMSYSERR_INVALHANDLE);
    }
    if (o.mx) {
        UINT id = 9;
        EXPECT_EQ(U(mixerGetID)((HMIXEROBJ)o.mx, &id, MIXER_OBJECTF_HMIXER), 0);
        EXPECT_EQ(id, 0);
        EXPECT_EQ(U(mixerClose)(o.mx), 0);
        EXPECT_EQ(U(mixerClose)(o.mx), MMSYSERR_INVALHANDLE);
    }
}

/* waveIn handle opened in one thread, started/stopped/reset/closed in another (capture data is not inspected). */
typedef struct { HWAVEIN h; DWORD r[5]; } wi_t;
static DWORD WINAPI wavein_ops_thread(void *p) {
    wi_t *w = (wi_t *)p;
    w->r[0] = U(waveInStart)(w->h);
    Sleep(50);
    w->r[1] = U(waveInStop)(w->h);
    w->r[2] = U(waveInReset)(w->h);
    w->r[3] = U(waveInClose)(w->h);
    w->r[4] = U(waveInClose)(w->h);
    return 0;
}
TEST(mh_thread_wavein_open_main_ops_worker) {
    REQUIRE(U(waveInOpen)); REQUIRE(U(waveInStart)); REQUIRE(U(waveInStop)); REQUIRE(U(waveInReset)); REQUIRE(U(waveInClose));
    if (!mh_devs()->wi) { t_note("no waveIn device"); return; }
    wi_t w; memset(&w, 0, sizeof w);
    EXPECT_EQ(mh_wavein_open(&w.h), 0);
    if (!w.h) return;
    HANDLE t = CreateThread(NULL, 0, wavein_ops_thread, &w, 0, NULL);
    if (!t) { U(waveInClose)(w.h); return; }
    WaitForSingleObject(t, 10000); CloseHandle(t);
    EXPECT_EQ(w.r[0], 0); EXPECT_EQ(w.r[1], 0); EXPECT_EQ(w.r[2], 0); EXPECT_EQ(w.r[3], 0);
    EXPECT_EQ(w.r[4], MMSYSERR_INVALHANDLE);
}
