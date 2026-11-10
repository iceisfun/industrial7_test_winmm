/* waveout_header_protocol: the WAVEHDR state machine on a live waveOut handle (device half) and the no-device
 * contract half. Gated on waveOutGetNumDevs (read once, never timed). Silence only; every wait is bounded (3 s).
 */
#include "whp.h"

/* ---- contract half (always runs) ------------------------------------------------------------------------------ */

TEST(whp_contract_device_count_and_mapper)
{
    REQUIRE(U(waveOutGetNumDevs)); REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutClose));
    UINT n = whp_numdevs();
    HWAVEOUT h = (HWAVEOUT)(ULONG_PTR)0x77; HANDLE ev = NULL;
    MMRESULT r = whp_open(WAVE_MAPPER, &h, &ev);
    if (n == 0) {
        EXPECT_EQ(r, MMSYSERR_BADDEVICEID);
        EXPECT_EQ(h, NULL);
    } else {
        EXPECT_EQ(r, MMSYSERR_NOERROR);
        WAVEOUTCAPSW c; memset(&c, 0, sizeof c);
        for (UINT i = 0; i < n && i < 8; i++)
            if (U(waveOutGetDevCapsW) && U(waveOutGetDevCapsW)(i, &c, sizeof c) == 0) t_note("waveOut %u \"%ls\"", i, c.szPname);
        if (r == 0) EXPECT_EQ(U(waveOutClose)(h), MMSYSERR_NOERROR);
    }
    /* an out-of-range id is BADDEVICEID in every configuration */
    HWAVEOUT h2 = NULL;
    EXPECT_EQ(whp_open(n + 7, &h2, &ev), MMSYSERR_BADDEVICEID);
    EXPECT_EQ(h2, NULL);
    if (ev) CloseHandle(ev);
}

TEST(whp_contract_failed_open_handle_is_rejected)
{
    /* whatever waveOutOpen left in *phwo after a failed open (NULL) is not a handle for the header calls */
    REQUIRE(whp_have_all()); REQUIRE(U(waveOutOpen));
    HWAVEOUT h = (HWAVEOUT)(ULONG_PTR)0x55; HANDLE ev = NULL;
    MMRESULT r = whp_open(whp_numdevs() + 3, &h, &ev);
    EXPECT_EQ(r, MMSYSERR_BADDEVICEID);
    char buf[16] = {0}; WAVEHDR w; whp_hdr_init(&w, 0, buf, sizeof buf);
    EXPECT_EQ(U(waveOutPrepareHeader)(h, &w, sizeof w), MMSYSERR_INVALHANDLE);
    w.dwFlags = WHDR_PREPARED;
    EXPECT_EQ(U(waveOutWrite)(h, &w, sizeof w), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveOutUnprepareHeader)(h, &w, sizeof w), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(w.dwFlags, WHDR_PREPARED);
    if (ev) CloseHandle(ev);
}

/* ---- basic cycle ---------------------------------------------------------------------------------------------- */

TEST(whp_dev_prepare_sets_prepared)
{
    HWAVEOUT h; HANDLE ev; if (!whp_need_dev(&h, &ev)) return;
    DWORD len; char *buf = whp_silence(100, &len);
    WAVEHDR w, ref; whp_hdr_init(&w, 0, buf, len); ref = w;
    EXPECT_EQ(U(waveOutPrepareHeader)(h, &w, sizeof w), MMSYSERR_NOERROR);
    EXPECT_EQ(w.dwFlags, WHDR_PREPARED);
    CHECK(w.lpData == ref.lpData && w.dwBufferLength == ref.dwBufferLength && w.dwUser == ref.dwUser);
    EXPECT_EQ(w.dwBytesRecorded, ref.dwBytesRecorded);
    EXPECT_EQ(w.dwLoops, ref.dwLoops);
    t_note("after prepare lpNext=%p reserved=0x%llx", (void *)w.lpNext, (unsigned long long)w.reserved);
    EXPECT_EQ(U(waveOutUnprepareHeader)(h, &w, sizeof w), MMSYSERR_NOERROR);
    EXPECT_EQ(w.dwFlags, 0);
    whp_close(h, ev);
}

TEST(whp_dev_prepare_masks_flags)
{
    /* dwFlags &= 0xC before WODM_PREPARE, then PREPARED: only BEGINLOOP|ENDLOOP survive */
    HWAVEOUT h; HANDLE ev; if (!whp_need_dev(&h, &ev)) return;
    DWORD len; char *buf = whp_silence(50, &len);
    static const DWORD before[] = { 0, WHDR_DONE, WHDR_INQUEUE, WHDR_DONE | WHDR_INQUEUE, WHDR_BEGINLOOP, WHDR_ENDLOOP,
                                    WHDR_DONE | WHDR_BEGINLOOP | WHDR_ENDLOOP, 0x1000, 0x2000, 0x3000, 0x301D };
    for (unsigned i = 0; i < sizeof before / sizeof before[0]; i++) {
        WAVEHDR w; whp_hdr_init(&w, before[i], buf, len);
        MMRESULT r = U(waveOutPrepareHeader)(h, &w, sizeof w);
        CHECKF(r == MMSYSERR_NOERROR, "prepare 0x%lx -> %u", before[i], r);
        DWORD want = (before[i] & (WHDR_BEGINLOOP | WHDR_ENDLOOP)) | WHDR_PREPARED;
        CHECKF(w.dwFlags == want, "prepare 0x%lx -> flags 0x%lx want 0x%lx", before[i], w.dwFlags, want);
        r = U(waveOutUnprepareHeader)(h, &w, sizeof w);
        CHECKF(r == MMSYSERR_NOERROR, "unprepare after 0x%lx -> %u", before[i], r);
        CHECKF(w.dwFlags == (want & ~WHDR_PREPARED), "unprepare -> 0x%lx", w.dwFlags);
    }
    whp_close(h, ev);
}

TEST(whp_dev_prepare_twice_noop)
{
    HWAVEOUT h; HANDLE ev; if (!whp_need_dev(&h, &ev)) return;
    DWORD len; char *buf = whp_silence(50, &len);
    WAVEHDR w; whp_hdr_init(&w, 0, buf, len);
    EXPECT_EQ(U(waveOutPrepareHeader)(h, &w, sizeof w), MMSYSERR_NOERROR);
    WAVEHDR snap = w;
    EXPECT_EQ(U(waveOutPrepareHeader)(h, &w, sizeof w), MMSYSERR_NOERROR);
    CHECK(!memcmp(&w, &snap, sizeof w));
    EXPECT_EQ(U(waveOutUnprepareHeader)(h, &w, sizeof w), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveOutUnprepareHeader)(h, &w, sizeof w), MMSYSERR_NOERROR);
    EXPECT_EQ(w.dwFlags, 0);
    whp_close(h, ev);
}

TEST(whp_dev_write_unprepared_valid_handle)
{
    HWAVEOUT h; HANDLE ev; if (!whp_need_dev(&h, &ev)) return;
    DWORD len; char *buf = whp_silence(50, &len);
    WAVEHDR w; whp_hdr_init(&w, 0, buf, len);
    EXPECT_EQ(U(waveOutWrite)(h, &w, sizeof w), WAVERR_UNPREPARED);
    EXPECT_EQ(w.dwFlags, 0);
    w.dwFlags = WHDR_DONE;
    EXPECT_EQ(U(waveOutWrite)(h, &w, sizeof w), WAVERR_UNPREPARED);
    EXPECT_EQ(w.dwFlags, WHDR_DONE);
    whp_close(h, ev);
}

TEST(whp_dev_write_inqueue_then_done)
{
    HWAVEOUT h; HANDLE ev; if (!whp_need_dev(&h, &ev)) return;
    DWORD len; char *buf = whp_silence(300, &len);
    WAVEHDR w; whp_hdr_init(&w, 0, buf, len);
    w.dwBytesRecorded = 0xA5A5A5A5u; w.dwUser = 0xFEEDu;
    EXPECT_EQ(U(waveOutPrepareHeader)(h, &w, sizeof w), MMSYSERR_NOERROR);
    EXPECT_EQ(w.dwFlags, WHDR_PREPARED);
    EXPECT_EQ(U(waveOutWrite)(h, &w, sizeof w), MMSYSERR_NOERROR);
    DWORD right_after = w.dwFlags;
    EXPECT_EQ(right_after, WHDR_PREPARED | WHDR_INQUEUE);
    CHECK(whp_wait_done(&w, ev, WHP_WAIT_MS));
    EXPECT_EQ(w.dwFlags, WHDR_DONE | WHDR_PREPARED);
    CHECK(!(w.dwFlags & WHDR_INQUEUE));
    CHECK(w.lpData == buf);
    EXPECT_EQ(w.dwBufferLength, len);
    EXPECT_EQ(w.dwUser, 0xFEEDu);
    t_note("output dwBytesRecorded after done = 0x%lx (preset 0xA5A5A5A5)", w.dwBytesRecorded);
    t_note("after done lpNext=%p reserved=0x%llx", (void *)w.lpNext, (unsigned long long)w.reserved);
    EXPECT_EQ(U(waveOutUnprepareHeader)(h, &w, sizeof w), MMSYSERR_NOERROR);
    EXPECT_EQ(w.dwFlags, WHDR_DONE);
    whp_close(h, ev);
}

TEST(whp_dev_rewrite_done_header_clears_done)
{
    HWAVEOUT h; HANDLE ev; if (!whp_need_dev(&h, &ev)) return;
    DWORD len; char *buf = whp_silence(80, &len);
    WAVEHDR w; whp_hdr_init(&w, 0, buf, len);
    EXPECT_EQ(U(waveOutPrepareHeader)(h, &w, sizeof w), MMSYSERR_NOERROR);
    for (int round = 0; round < 3; round++) {
        EXPECT_EQ(U(waveOutWrite)(h, &w, sizeof w), MMSYSERR_NOERROR);
        CHECKF(!(w.dwFlags & WHDR_DONE) || (w.dwFlags & WHDR_INQUEUE) == 0, "round %d flags 0x%lx", round, w.dwFlags);
        CHECK(whp_wait_done(&w, ev, WHP_WAIT_MS));
        EXPECT_EQ(w.dwFlags, WHDR_DONE | WHDR_PREPARED);
    }
    EXPECT_EQ(U(waveOutUnprepareHeader)(h, &w, sizeof w), MMSYSERR_NOERROR);
    whp_close(h, ev);
}

TEST(whp_dev_write_clears_done_synchronously)
{
    HWAVEOUT h; HANDLE ev; if (!whp_need_dev(&h, &ev)) return;
    DWORD len; char *buf = whp_silence(400, &len);
    WAVEHDR w; whp_hdr_init(&w, 0, buf, len);
    EXPECT_EQ(U(waveOutPrepareHeader)(h, &w, sizeof w), MMSYSERR_NOERROR);
    w.dwFlags |= WHDR_DONE; /* as after a previous completion */
    EXPECT_EQ(U(waveOutWrite)(h, &w, sizeof w), MMSYSERR_NOERROR);
    EXPECT_EQ(w.dwFlags, WHDR_PREPARED | WHDR_INQUEUE);
    U(waveOutReset)(h);
    CHECK(whp_wait_done(&w, ev, WHP_WAIT_MS));
    EXPECT_EQ(U(waveOutUnprepareHeader)(h, &w, sizeof w), MMSYSERR_NOERROR);
    whp_close(h, ev);
}

/* ---- queued-state errors -------------------------------------------------------------------------------------- */

TEST(whp_dev_write_twice_while_queued)
{
    HWAVEOUT h; HANDLE ev; if (!whp_need_dev(&h, &ev)) return;
    DWORD len; char *buf = whp_silence(1000, &len);
    WAVEHDR w; whp_hdr_init(&w, 0, buf, len);
    EXPECT_EQ(U(waveOutPrepareHeader)(h, &w, sizeof w), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveOutWrite)(h, &w, sizeof w), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveOutWrite)(h, &w, sizeof w), WAVERR_STILLPLAYING);
    EXPECT_EQ(w.dwFlags, WHDR_PREPARED | WHDR_INQUEUE);
    EXPECT_EQ(U(waveOutReset)(h), MMSYSERR_NOERROR);
    CHECK(whp_wait_done(&w, ev, WHP_WAIT_MS));
    EXPECT_EQ(w.dwFlags, WHDR_DONE | WHDR_PREPARED);
    EXPECT_EQ(U(waveOutUnprepareHeader)(h, &w, sizeof w), MMSYSERR_NOERROR);
    whp_close(h, ev);
}

TEST(whp_dev_unprepare_while_queued)
{
    HWAVEOUT h; HANDLE ev; if (!whp_need_dev(&h, &ev)) return;
    DWORD len; char *buf = whp_silence(1000, &len);
    WAVEHDR w; whp_hdr_init(&w, 0, buf, len);
    EXPECT_EQ(U(waveOutPrepareHeader)(h, &w, sizeof w), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveOutWrite)(h, &w, sizeof w), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveOutUnprepareHeader)(h, &w, sizeof w), WAVERR_STILLPLAYING);
    EXPECT_EQ(w.dwFlags, WHDR_PREPARED | WHDR_INQUEUE);
    EXPECT_EQ(U(waveOutReset)(h), MMSYSERR_NOERROR);
    CHECK(whp_wait_done(&w, ev, WHP_WAIT_MS));
    EXPECT_EQ(U(waveOutUnprepareHeader)(h, &w, sizeof w), MMSYSERR_NOERROR);
    EXPECT_EQ(w.dwFlags, WHDR_DONE);
    whp_close(h, ev);
}

TEST(whp_dev_prepare_while_queued_noop)
{
    HWAVEOUT h; HANDLE ev; if (!whp_need_dev(&h, &ev)) return;
    DWORD len; char *buf = whp_silence(1000, &len);
    WAVEHDR w; whp_hdr_init(&w, 0, buf, len);
    EXPECT_EQ(U(waveOutPrepareHeader)(h, &w, sizeof w), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveOutWrite)(h, &w, sizeof w), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveOutPrepareHeader)(h, &w, sizeof w), MMSYSERR_NOERROR);
    EXPECT_EQ(w.dwFlags, WHDR_PREPARED | WHDR_INQUEUE);
    EXPECT_EQ(U(waveOutReset)(h), MMSYSERR_NOERROR);
    CHECK(whp_wait_done(&w, ev, WHP_WAIT_MS));
    EXPECT_EQ(U(waveOutUnprepareHeader)(h, &w, sizeof w), MMSYSERR_NOERROR);
    whp_close(h, ev);
}

TEST(whp_dev_queued_header_to_second_handle)
{
    /* INQUEUE is tested before the handle: the same queued header given to another handle -> 33 */
    HWAVEOUT h; HANDLE ev; if (!whp_need_dev(&h, &ev)) return;
    HWAVEOUT h2 = NULL; HANDLE ev2 = NULL;
    MMRESULT r2 = whp_open(WAVE_MAPPER, &h2, &ev2);
    EXPECT_EQ(r2, MMSYSERR_NOERROR);
    DWORD len; char *buf = whp_silence(1000, &len);
    WAVEHDR w; whp_hdr_init(&w, 0, buf, len);
    EXPECT_EQ(U(waveOutPrepareHeader)(h, &w, sizeof w), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveOutWrite)(h, &w, sizeof w), MMSYSERR_NOERROR);
    if (r2 == 0) {
        EXPECT_EQ(U(waveOutWrite)(h2, &w, sizeof w), WAVERR_STILLPLAYING);
        EXPECT_EQ(U(waveOutUnprepareHeader)(h2, &w, sizeof w), WAVERR_STILLPLAYING);
        EXPECT_EQ(U(waveOutPrepareHeader)(h2, &w, sizeof w), MMSYSERR_NOERROR);
    }
    EXPECT_EQ(w.dwFlags, WHDR_PREPARED | WHDR_INQUEUE);
    EXPECT_EQ(U(waveOutReset)(h), MMSYSERR_NOERROR);
    CHECK(whp_wait_done(&w, ev, WHP_WAIT_MS));
    EXPECT_EQ(U(waveOutUnprepareHeader)(h, &w, sizeof w), MMSYSERR_NOERROR);
    if (r2 == 0) whp_close(h2, ev2); else if (ev2) CloseHandle(ev2);
    whp_close(h, ev);
}

TEST(whp_dev_paused_write_stays_queued)
{
    REQUIRE(U(waveOutPause)); REQUIRE(U(waveOutRestart));
    HWAVEOUT h; HANDLE ev; if (!whp_need_dev(&h, &ev)) return;
    DWORD len; char *buf = whp_silence(100, &len);
    WAVEHDR w; whp_hdr_init(&w, 0, buf, len);
    EXPECT_EQ(U(waveOutPause)(h), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveOutPrepareHeader)(h, &w, sizeof w), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveOutWrite)(h, &w, sizeof w), MMSYSERR_NOERROR);
    Sleep(400);
    EXPECT_EQ(w.dwFlags, WHDR_PREPARED | WHDR_INQUEUE);
    EXPECT_EQ(U(waveOutUnprepareHeader)(h, &w, sizeof w), WAVERR_STILLPLAYING);
    EXPECT_EQ(U(waveOutRestart)(h), MMSYSERR_NOERROR);
    CHECK(whp_wait_done(&w, ev, WHP_WAIT_MS));
    EXPECT_EQ(w.dwFlags, WHDR_DONE | WHDR_PREPARED);
    EXPECT_EQ(U(waveOutUnprepareHeader)(h, &w, sizeof w), MMSYSERR_NOERROR);
    whp_close(h, ev);
}

/* ---- close / reset -------------------------------------------------------------------------------------------- */

TEST(whp_dev_close_with_queued_buffer)
{
    REQUIRE(U(waveOutGetID));
    HWAVEOUT h; HANDLE ev; if (!whp_need_dev(&h, &ev)) return;
    DWORD len; char *buf = whp_silence(1500, &len);
    WAVEHDR w; whp_hdr_init(&w, 0, buf, len);
    EXPECT_EQ(U(waveOutPrepareHeader)(h, &w, sizeof w), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveOutWrite)(h, &w, sizeof w), MMSYSERR_NOERROR);
    MMRESULT rc = U(waveOutClose)(h);
    EXPECT_EQ(rc, WAVERR_STILLPLAYING);
    if (rc == MMSYSERR_NOERROR) { CloseHandle(ev); return; }   /* handle is gone; nothing to clean */
    UINT id = 0x1234;
    EXPECT_EQ(U(waveOutGetID)(h, &id), MMSYSERR_NOERROR);       /* still a valid handle */
    EXPECT_EQ(id, WAVE_MAPPER);
    EXPECT_EQ(w.dwFlags & (WHDR_PREPARED | WHDR_INQUEUE), WHDR_PREPARED | WHDR_INQUEUE);
    EXPECT_EQ(U(waveOutReset)(h), MMSYSERR_NOERROR);
    CHECK(whp_wait_done(&w, ev, WHP_WAIT_MS));
    EXPECT_EQ(w.dwFlags, WHDR_DONE | WHDR_PREPARED);
    EXPECT_EQ(U(waveOutUnprepareHeader)(h, &w, sizeof w), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveOutClose)(h), MMSYSERR_NOERROR);
    CloseHandle(ev);
}

TEST(whp_dev_reset_then_close_all_done)
{
    HWAVEOUT h; HANDLE ev; if (!whp_need_dev(&h, &ev)) return;
    DWORD len; char *buf = whp_silence(500, &len);
    WAVEHDR w[4];
    for (int i = 0; i < 4; i++) {
        whp_hdr_init(&w[i], 0, buf, len);
        EXPECT_EQ(U(waveOutPrepareHeader)(h, &w[i], sizeof w[i]), MMSYSERR_NOERROR);
    }
    for (int i = 0; i < 4; i++) EXPECT_EQ(U(waveOutWrite)(h, &w[i], sizeof w[i]), MMSYSERR_NOERROR);
    for (int i = 0; i < 4; i++) CHECKF(w[i].dwFlags & WHDR_INQUEUE, "hdr %d flags 0x%lx", i, w[i].dwFlags);
    EXPECT_EQ(U(waveOutReset)(h), MMSYSERR_NOERROR);
    /* Reset returns after the driver gave every buffer back */
    for (int i = 0; i < 4; i++) CHECKF(w[i].dwFlags == (WHDR_DONE | WHDR_PREPARED), "hdr %d after reset 0x%lx", i, w[i].dwFlags);
    for (int i = 0; i < 4; i++) {
        EXPECT_EQ(U(waveOutUnprepareHeader)(h, &w[i], sizeof w[i]), MMSYSERR_NOERROR);
        EXPECT_EQ(w[i].dwFlags, WHDR_DONE);
    }
    EXPECT_EQ(U(waveOutClose)(h), MMSYSERR_NOERROR);
    CloseHandle(ev);
}

TEST(whp_dev_reset_leaves_unqueued_prepared_alone)
{
    HWAVEOUT h; HANDLE ev; if (!whp_need_dev(&h, &ev)) return;
    DWORD len; char *buf = whp_silence(50, &len);
    WAVEHDR w; whp_hdr_init(&w, 0, buf, len);
    EXPECT_EQ(U(waveOutPrepareHeader)(h, &w, sizeof w), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveOutReset)(h), MMSYSERR_NOERROR);
    EXPECT_EQ(w.dwFlags, WHDR_PREPARED);
    EXPECT_EQ(U(waveOutReset)(h), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveOutUnprepareHeader)(h, &w, sizeof w), MMSYSERR_NOERROR);
    EXPECT_EQ(w.dwFlags, 0);
    whp_close(h, ev);
}

TEST(whp_dev_close_with_prepared_unqueued)
{
    /* a prepared but idle header does not keep the device busy; afterwards the closed handle is just invalid */
    HWAVEOUT h; HANDLE ev; if (!whp_need_dev(&h, &ev)) return;
    DWORD len; char *buf = whp_silence(50, &len);
    WAVEHDR w; whp_hdr_init(&w, 0, buf, len);
    EXPECT_EQ(U(waveOutPrepareHeader)(h, &w, sizeof w), MMSYSERR_NOERROR);
    MMRESULT rc = U(waveOutClose)(h);
    t_note("waveOutClose with a prepared idle header = %u", rc);
    if (rc != MMSYSERR_NOERROR) {
        EXPECT_EQ(U(waveOutUnprepareHeader)(h, &w, sizeof w), MMSYSERR_NOERROR);
        EXPECT_EQ(U(waveOutClose)(h), MMSYSERR_NOERROR);
        CloseHandle(ev);
        return;
    }
    EXPECT_EQ(w.dwFlags, WHDR_PREPARED);
    /* the stale handle: prepared header -> 5 and the flags stay */
    EXPECT_EQ(U(waveOutUnprepareHeader)(h, &w, sizeof w), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveOutWrite)(h, &w, sizeof w), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(w.dwFlags, WHDR_PREPARED);
    CloseHandle(ev);
}

TEST(whp_dev_closed_handle_ladder)
{
    HWAVEOUT h; HANDLE ev; if (!whp_need_dev(&h, &ev)) return;
    EXPECT_EQ(U(waveOutClose)(h), MMSYSERR_NOERROR);
    char buf[16] = {0}; WAVEHDR w;
    whp_hdr_init(&w, 0, buf, sizeof buf);
    EXPECT_EQ(U(waveOutPrepareHeader)(h, &w, sizeof w), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveOutUnprepareHeader)(h, &w, sizeof w), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveOutWrite)(h, &w, sizeof w), WAVERR_UNPREPARED);
    whp_hdr_init(&w, WHDR_PREPARED, buf, sizeof buf);
    EXPECT_EQ(U(waveOutPrepareHeader)(h, &w, sizeof w), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveOutUnprepareHeader)(h, &w, sizeof w), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveOutWrite)(h, &w, sizeof w), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(w.dwFlags, WHDR_PREPARED);
    whp_hdr_init(&w, WHDR_PREPARED | WHDR_INQUEUE, buf, sizeof buf);
    EXPECT_EQ(U(waveOutWrite)(h, &w, sizeof w), WAVERR_STILLPLAYING);
    EXPECT_EQ(U(waveOutUnprepareHeader)(h, &w, sizeof w), WAVERR_STILLPLAYING);
    CloseHandle(ev);
}

/* ---- multiple headers and completion order -------------------------------------------------------------------- */

static volatile LONG g_cb_n;
static DWORD_PTR g_cb_p1[64];
static UINT g_cb_msg[64];
static DWORD g_cb_flags[64];
static void CALLBACK whp_cb(HWAVEOUT h, UINT msg, DWORD_PTR inst, DWORD_PTR p1, DWORD_PTR p2)
{
    LONG i = InterlockedIncrement(&g_cb_n) - 1;
    if (i < 64) {
        g_cb_msg[i] = msg; g_cb_p1[i] = p1;
        g_cb_flags[i] = (msg == WOM_DONE && p1) ? ((WAVEHDR *)p1)->dwFlags : 0;
    }
}

TEST(whp_dev_callback_done_order_and_flags)
{
    REQUIRE(whp_have_all()); REQUIRE(U(waveOutOpen));
    if (whp_numdevs() == 0) { t_skip("no waveOut device"); return; }
    g_cb_n = 0;
    WAVEFORMATEX f; whp_fmt(&f, 44100, 16, 2);
    HWAVEOUT h = NULL;
    MMRESULT r = U(waveOutOpen)(&h, WAVE_MAPPER, &f, (DWORD_PTR)whp_cb, 0x99, CALLBACK_FUNCTION);
    EXPECT_EQ(r, MMSYSERR_NOERROR);
    if (r) return;
    DWORD len; char *buf = whp_silence(60, &len);
    WAVEHDR w[5];
    for (int i = 0; i < 5; i++) {
        whp_hdr_init(&w[i], 0, buf, len);
        EXPECT_EQ(U(waveOutPrepareHeader)(h, &w[i], sizeof w[i]), MMSYSERR_NOERROR);
    }
    for (int i = 0; i < 5; i++) EXPECT_EQ(U(waveOutWrite)(h, &w[i], sizeof w[i]), MMSYSERR_NOERROR);
    CHECK(whp_wait_all_done(w, 5, NULL, WHP_WAIT_MS));
    Sleep(50);
    for (int i = 0; i < 5; i++) EXPECT_EQ(U(waveOutUnprepareHeader)(h, &w[i], sizeof w[i]), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveOutClose)(h), MMSYSERR_NOERROR);
    LONG n = g_cb_n; if (n > 64) n = 64;
    EXPECT_EQ(n, 7); /* WOM_OPEN, 5 x WOM_DONE, WOM_CLOSE */
    if (n >= 7) {
        EXPECT_EQ(g_cb_msg[0], WOM_OPEN);
        for (int i = 0; i < 5; i++) {
            EXPECT_EQ(g_cb_msg[1 + i], WOM_DONE);
            CHECKF(g_cb_p1[1 + i] == (DWORD_PTR)&w[i], "WOM_DONE %d param1 %p want %p", i, (void *)g_cb_p1[1 + i], (void *)&w[i]);
            CHECKF(g_cb_flags[1 + i] == (WHDR_DONE | WHDR_PREPARED), "flags inside WOM_DONE %d = 0x%lx", i, g_cb_flags[1 + i]);
        }
        EXPECT_EQ(g_cb_msg[6], WOM_CLOSE);
    }
}

TEST(whp_dev_callback_reset_returns_all)
{
    REQUIRE(whp_have_all()); REQUIRE(U(waveOutOpen));
    if (whp_numdevs() == 0) { t_skip("no waveOut device"); return; }
    g_cb_n = 0;
    WAVEFORMATEX f; whp_fmt(&f, 44100, 16, 2);
    HWAVEOUT h = NULL;
    MMRESULT r = U(waveOutOpen)(&h, WAVE_MAPPER, &f, (DWORD_PTR)whp_cb, 0, CALLBACK_FUNCTION);
    EXPECT_EQ(r, MMSYSERR_NOERROR);
    if (r) return;
    DWORD len; char *buf = whp_silence(700, &len);
    WAVEHDR w[3];
    for (int i = 0; i < 3; i++) {
        whp_hdr_init(&w[i], 0, buf, len);
        EXPECT_EQ(U(waveOutPrepareHeader)(h, &w[i], sizeof w[i]), MMSYSERR_NOERROR);
        EXPECT_EQ(U(waveOutWrite)(h, &w[i], sizeof w[i]), MMSYSERR_NOERROR);
    }
    EXPECT_EQ(U(waveOutReset)(h), MMSYSERR_NOERROR);
    CHECK(whp_wait_all_done(w, 3, NULL, WHP_WAIT_MS));
    Sleep(50);
    LONG n = g_cb_n;
    int dones = 0;
    for (LONG i = 0; i < n && i < 64; i++) if (g_cb_msg[i] == WOM_DONE) dones++;
    EXPECT_EQ(dones, 3);
    for (int i = 0; i < 3; i++) {
        EXPECT_EQ(w[i].dwFlags, WHDR_DONE | WHDR_PREPARED);
        EXPECT_EQ(U(waveOutUnprepareHeader)(h, &w[i], sizeof w[i]), MMSYSERR_NOERROR);
    }
    EXPECT_EQ(U(waveOutClose)(h), MMSYSERR_NOERROR);
}

TEST(whp_dev_ring_of_headers)
{
    HWAVEOUT h; HANDLE ev; if (!whp_need_dev(&h, &ev)) return;
    DWORD len; char *buf = whp_silence(20, &len);
    WAVEHDR w[6];
    for (int i = 0; i < 6; i++) {
        whp_hdr_init(&w[i], 0, buf, len);
        EXPECT_EQ(U(waveOutPrepareHeader)(h, &w[i], sizeof w[i]), MMSYSERR_NOERROR);
    }
    int writes = 0, errs = 0;
    for (int round = 0; round < 3; round++) {
        for (int i = 0; i < 6; i++) { if (U(waveOutWrite)(h, &w[i], sizeof w[i])) errs++; writes++; }
        if (!whp_wait_all_done(w, 6, ev, WHP_WAIT_MS)) { CHECKF(0, "round %d did not finish", round); break; }
        for (int i = 0; i < 6; i++) CHECKF(w[i].dwFlags == (WHDR_DONE | WHDR_PREPARED), "round %d hdr %d 0x%lx", round, i, w[i].dwFlags);
    }
    EXPECT_EQ(errs, 0);
    EXPECT_EQ(writes, 18);
    for (int i = 0; i < 6; i++) EXPECT_EQ(U(waveOutUnprepareHeader)(h, &w[i], sizeof w[i]), MMSYSERR_NOERROR);
    whp_close(h, ev);
}

/* ---- unusual buffers ------------------------------------------------------------------------------------------ */

TEST(whp_dev_zero_length_buffer)
{
    HWAVEOUT h; HANDLE ev; if (!whp_need_dev(&h, &ev)) return;
    char dummy[4] = {0};
    WAVEHDR w; whp_hdr_init(&w, 0, dummy, 0);
    MMRESULT rp = U(waveOutPrepareHeader)(h, &w, sizeof w);
    t_note("zero-length prepare = %u flags 0x%lx", rp, w.dwFlags);
    EXPECT_EQ(rp, MMSYSERR_NOERROR);
    if (rp == 0) {
        MMRESULT rw = U(waveOutWrite)(h, &w, sizeof w);
        t_note("zero-length write = %u flags right after 0x%lx", rw, w.dwFlags);
        EXPECT_EQ(rw, MMSYSERR_NOERROR);
        if (rw == 0) {
            int done = whp_wait_done(&w, ev, WHP_WAIT_MS);
            t_note("zero-length DONE arrived=%d flags 0x%lx", done, w.dwFlags);
            CHECK(done);
        }
        U(waveOutReset)(h);
        EXPECT_EQ(U(waveOutUnprepareHeader)(h, &w, sizeof w), MMSYSERR_NOERROR);
    }
    whp_close(h, ev);
}

TEST(whp_dev_unaligned_length_buffer)
{
    /* dwBufferLength not a multiple of nBlockAlign (4) */
    HWAVEOUT h; HANDLE ev; if (!whp_need_dev(&h, &ev)) return;
    DWORD len; char *buf = whp_silence(40, &len);
    for (DWORD extra = 1; extra <= 3; extra++) {
        WAVEHDR w; whp_hdr_init(&w, 0, buf, len + extra);
        MMRESULT rp = U(waveOutPrepareHeader)(h, &w, sizeof w);
        MMRESULT rw = rp ? 0xFFFF : U(waveOutWrite)(h, &w, sizeof w);
        int done = rw == 0 ? whp_wait_done(&w, ev, WHP_WAIT_MS) : -1;
        t_note("len %%4=%lu: prepare=%u write=%u done=%d flags=0x%lx", extra, rp, rw, done, w.dwFlags);
        EXPECT_EQ(rp, MMSYSERR_NOERROR);
        EXPECT_EQ(rw, MMSYSERR_NOERROR);
        CHECK(done == 1);
        U(waveOutReset)(h);
        if (rp == 0) EXPECT_EQ(U(waveOutUnprepareHeader)(h, &w, sizeof w), MMSYSERR_NOERROR);
    }
    /* shorter than one block */
    WAVEHDR w; whp_hdr_init(&w, 0, buf, 2);
    MMRESULT rp = U(waveOutPrepareHeader)(h, &w, sizeof w);
    MMRESULT rw = rp ? 0xFFFF : U(waveOutWrite)(h, &w, sizeof w);
    int done = rw == 0 ? whp_wait_done(&w, ev, WHP_WAIT_MS) : -1;
    t_note("len 2 (< nBlockAlign): prepare=%u write=%u done=%d", rp, rw, done);
    U(waveOutReset)(h);
    if (rp == 0) U(waveOutUnprepareHeader)(h, &w, sizeof w);
    whp_close(h, ev);
}

TEST(whp_dev_loop_flags)
{
    HWAVEOUT h; HANDLE ev; if (!whp_need_dev(&h, &ev)) return;
    DWORD len; char *buf = whp_silence(50, &len);
    WAVEHDR w; whp_hdr_init(&w, WHDR_DONE | WHDR_BEGINLOOP | WHDR_ENDLOOP, buf, len);
    w.dwLoops = 3;
    EXPECT_EQ(U(waveOutPrepareHeader)(h, &w, sizeof w), MMSYSERR_NOERROR);
    EXPECT_EQ(w.dwFlags, WHDR_BEGINLOOP | WHDR_ENDLOOP | WHDR_PREPARED);
    EXPECT_EQ(U(waveOutWrite)(h, &w, sizeof w), MMSYSERR_NOERROR);
    EXPECT_EQ(w.dwFlags, WHDR_BEGINLOOP | WHDR_ENDLOOP | WHDR_PREPARED | WHDR_INQUEUE);
    DWORD t0 = GetTickCount();
    int done = whp_wait_done(&w, ev, WHP_WAIT_MS);
    CHECK(done);
    t_note("loop x3 of 50 ms done after ~%lu ms flags 0x%lx loops %lu", GetTickCount() - t0, w.dwFlags, w.dwLoops);
    EXPECT_EQ(w.dwFlags, WHDR_DONE | WHDR_BEGINLOOP | WHDR_ENDLOOP | WHDR_PREPARED);
    U(waveOutReset)(h);
    EXPECT_EQ(U(waveOutUnprepareHeader)(h, &w, sizeof w), MMSYSERR_NOERROR);
    EXPECT_EQ(w.dwFlags, WHDR_DONE | WHDR_BEGINLOOP | WHDR_ENDLOOP);
    whp_close(h, ev);
}

TEST(whp_dev_cb64_full_cycle)
{
    HWAVEOUT h; HANDLE ev; if (!whp_need_dev(&h, &ev)) return;
    DWORD len; char *buf = whp_silence(60, &len);
    static union { WAVEHDR w; char raw[64]; } b;
    memset(&b, 0, sizeof b);
    whp_hdr_init(&b.w, 0, buf, len);
    MMRESULT rp = U(waveOutPrepareHeader)(h, &b.w, 64);
    EXPECT_EQ(rp, MMSYSERR_NOERROR);
    if (rp == 0) {
        EXPECT_EQ(U(waveOutWrite)(h, &b.w, 64), MMSYSERR_NOERROR);
        CHECK(whp_wait_done(&b.w, ev, WHP_WAIT_MS));
        U(waveOutReset)(h);
        EXPECT_EQ(U(waveOutUnprepareHeader)(h, &b.w, 64), MMSYSERR_NOERROR);
        EXPECT_EQ(b.w.dwFlags, WHDR_DONE);
    }
    whp_close(h, ev);
}

TEST(whp_dev_internal_bit_after_prepare)
{
    /* 0x1000 / 0x2000 set by the caller after prepare pass the gate; record what the driver does with them */
    HWAVEOUT h; HANDLE ev; if (!whp_need_dev(&h, &ev)) return;
    DWORD len; char *buf = whp_silence(60, &len);
    static const DWORD extra[] = { 0x1000, 0x2000 };
    for (int i = 0; i < 2; i++) {
        WAVEHDR w; whp_hdr_init(&w, 0, buf, len);
        EXPECT_EQ(U(waveOutPrepareHeader)(h, &w, sizeof w), MMSYSERR_NOERROR);
        w.dwFlags |= extra[i];
        MMRESULT rw = U(waveOutWrite)(h, &w, sizeof w);
        DWORD after = w.dwFlags;
        int done = rw == 0 ? whp_wait_done(&w, ev, WHP_WAIT_MS) : -1;
        t_note("write with 0x%lx: %u flags right after 0x%lx done=%d final 0x%lx", extra[i], rw, after, done, w.dwFlags);
        U(waveOutReset)(h);
        w.dwFlags &= ~0x3000u;
        EXPECT_EQ(U(waveOutUnprepareHeader)(h, &w, sizeof w), MMSYSERR_NOERROR);
    }
    whp_close(h, ev);
}

/* ---- prepare on one handle, use with another ------------------------------------------------------------------ */

TEST(whp_dev_cross_handle_use)
{
    HWAVEOUT h; HANDLE ev; if (!whp_need_dev(&h, &ev)) return;
    HWAVEOUT h2 = NULL; HANDLE ev2 = NULL;
    if (whp_open(WAVE_MAPPER, &h2, &ev2) != 0) { whp_close(h, ev); if (ev2) CloseHandle(ev2); t_skip("second open failed"); return; }
    DWORD len; char *buf = whp_silence(60, &len);
    WAVEHDR w; whp_hdr_init(&w, 0, buf, len);
    EXPECT_EQ(U(waveOutPrepareHeader)(h, &w, sizeof w), MMSYSERR_NOERROR);
    MMRESULT rw = U(waveOutWrite)(h2, &w, sizeof w);
    int done = rw == 0 ? whp_wait_done(&w, ev2, WHP_WAIT_MS) : -1;
    t_note("prepared on A, written on B: %u done=%d flags 0x%lx", rw, done, w.dwFlags);
    U(waveOutReset)(h2);
    MMRESULT ru = U(waveOutUnprepareHeader)(h2, &w, sizeof w);
    t_note("unprepare on B: %u flags 0x%lx", ru, w.dwFlags);
    if (w.dwFlags & WHDR_PREPARED) U(waveOutUnprepareHeader)(h, &w, sizeof w);
    whp_close(h2, ev2);
    whp_close(h, ev);
}

/* ---- every device id, a few formats --------------------------------------------------------------------------- */

static void cycle_on(HWAVEOUT h, HANDLE ev, char *buf, DWORD len, const char *what)
{
    WAVEHDR w; whp_hdr_init(&w, 0, buf, len);
    CHECKF(U(waveOutPrepareHeader)(h, &w, sizeof w) == 0, "%s prepare", what);
    CHECKF(w.dwFlags == WHDR_PREPARED, "%s prepared flags 0x%lx", what, w.dwFlags);
    CHECKF(U(waveOutWrite)(h, &w, sizeof w) == 0, "%s write", what);
    CHECKF(w.dwFlags == (WHDR_PREPARED | WHDR_INQUEUE) || w.dwFlags == (WHDR_PREPARED | WHDR_DONE), "%s queued flags 0x%lx", what, w.dwFlags);
    CHECKF(whp_wait_done(&w, ev, WHP_WAIT_MS), "%s done", what);
    CHECKF(w.dwFlags == (WHDR_PREPARED | WHDR_DONE), "%s done flags 0x%lx", what, w.dwFlags);
    CHECKF(U(waveOutUnprepareHeader)(h, &w, sizeof w) == 0, "%s unprepare", what);
    CHECKF(w.dwFlags == WHDR_DONE, "%s unprepared flags 0x%lx", what, w.dwFlags);
}

TEST(whp_dev_each_device_id)
{
    REQUIRE(whp_have_all()); REQUIRE(U(waveOutOpen));
    UINT n = whp_numdevs();
    if (n == 0) { t_skip("no waveOut device"); return; }
    DWORD len; char *buf = whp_silence(50, &len);
    for (UINT id = 0; id < n && id < 8; id++) {
        HWAVEOUT h = NULL; HANDLE ev = NULL;
        MMRESULT r = whp_open(id, &h, &ev);
        CHECKF(r == 0, "open id %u -> %u", id, r);
        char what[32]; snprintf(what, sizeof what, "id %u", id);
        if (r == 0) cycle_on(h, ev, buf, len, what);
        if (r == 0) whp_close(h, ev); else if (ev) CloseHandle(ev);
    }
}

TEST(whp_dev_formats)
{
    REQUIRE(whp_have_all()); REQUIRE(U(waveOutOpen));
    if (whp_numdevs() == 0) { t_skip("no waveOut device"); return; }
    static const struct { DWORD rate; WORD bits, ch; } fm[] = {
        { 8000, 8, 1 }, { 11025, 8, 2 }, { 22050, 16, 1 }, { 44100, 16, 2 }, { 48000, 16, 2 }, { 96000, 16, 2 },
    };
    static char b8[4410 * 2];
    memset(b8, 0x80, sizeof b8);  /* 8-bit silence */
    for (unsigned i = 0; i < sizeof fm / sizeof fm[0]; i++) {
        WAVEFORMATEX f; whp_fmt(&f, fm[i].rate, fm[i].bits, fm[i].ch);
        HWAVEOUT h = NULL; HANDLE ev = NULL;
        MMRESULT r = whp_open_fmt(WAVE_MAPPER, &f, &h, &ev);
        char what[48]; snprintf(what, sizeof what, "%lu/%u/%u", fm[i].rate, fm[i].bits, fm[i].ch);
        CHECKF(r == 0, "open %s -> %u", what, r);
        if (r == 0) {
            DWORD len = f.nAvgBytesPerSec / 20; /* 50 ms */
            DWORD l2; char *buf = fm[i].bits == 8 ? b8 : whp_silence(1000, &l2);
            if (fm[i].bits == 8 && len > sizeof b8) len = sizeof b8;
            cycle_on(h, ev, buf, len, what);
            whp_close(h, ev);
        } else if (ev) CloseHandle(ev);
    }
}

/* ---- handles of the wrong kind -------------------------------------------------------------------------------- */

TEST(whp_dev_wavein_handle_rejected)
{
    REQUIRE(whp_have_all()); REQUIRE(U(waveInOpen)); REQUIRE(U(waveInClose)); REQUIRE(U(waveInGetNumDevs));
    if (U(waveInGetNumDevs)() == 0) { t_skip("no waveIn device"); return; }
    WAVEFORMATEX f; whp_fmt(&f, 44100, 16, 1);
    HWAVEIN hi = NULL;
    MMRESULT r = U(waveInOpen)(&hi, WAVE_MAPPER, &f, 0, 0, CALLBACK_NULL);
    EXPECT_EQ(r, MMSYSERR_NOERROR);
    if (r) return;
    char buf[64] = {0}; WAVEHDR w;
    whp_hdr_init(&w, 0, buf, sizeof buf);
    EXPECT_EQ(U(waveOutPrepareHeader)((HWAVEOUT)hi, &w, sizeof w), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(w.dwFlags, 0);
    whp_hdr_init(&w, WHDR_PREPARED, buf, sizeof buf);
    EXPECT_EQ(U(waveOutWrite)((HWAVEOUT)hi, &w, sizeof w), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveOutUnprepareHeader)((HWAVEOUT)hi, &w, sizeof w), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(w.dwFlags, WHDR_PREPARED);
    EXPECT_EQ(U(waveInClose)(hi), MMSYSERR_NOERROR);
}

TEST(whp_dev_midiout_handle_rejected)
{
    REQUIRE(whp_have_all()); REQUIRE(U(midiOutOpen)); REQUIRE(U(midiOutClose));
    if (whp_numdevs() == 0) { t_skip("midiOut mapper opens only with a wave device"); return; }
    HMIDIOUT hm = NULL;
    MMRESULT r = U(midiOutOpen)(&hm, MIDI_MAPPER, 0, 0, CALLBACK_NULL);
    if (r) { t_note("midiOutOpen(MIDI_MAPPER) = %u", r); t_skip("no midi handle"); return; }
    char buf[64] = {0}; WAVEHDR w;
    whp_hdr_init(&w, 0, buf, sizeof buf);
    EXPECT_EQ(U(waveOutPrepareHeader)((HWAVEOUT)hm, &w, sizeof w), MMSYSERR_INVALHANDLE);
    whp_hdr_init(&w, WHDR_PREPARED, buf, sizeof buf);
    EXPECT_EQ(U(waveOutWrite)((HWAVEOUT)hm, &w, sizeof w), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveOutUnprepareHeader)((HWAVEOUT)hm, &w, sizeof w), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(w.dwFlags, WHDR_PREPARED);
    EXPECT_EQ(U(midiOutClose)(hm), MMSYSERR_NOERROR);
}

TEST(whp_dev_valid_handle_gate_still_first)
{
    /* with a live handle the gate answers exactly as with NULL */
    HWAVEOUT h; HANDLE ev; if (!whp_need_dev(&h, &ev)) return;
    char buf[64] = {0};
    for (int fn = 0; fn < WHP_NFN; fn++) {
        whp_hdr_fn f = whp_fn(fn);
        WAVEHDR w;
        CHECKF(f(h, NULL, sizeof w) == MMSYSERR_INVALPARAM, "%s NULL", whp_fn_name[fn]);
        whp_hdr_init(&w, 0, buf, sizeof buf);
        CHECKF(f(h, &w, 47) == MMSYSERR_INVALPARAM, "%s cb 47", whp_fn_name[fn]);
        CHECKF(f(h, &w, 0) == MMSYSERR_INVALPARAM, "%s cb 0", whp_fn_name[fn]);
        for (int bit = 5; bit < 32; bit++) {
            if (bit == 12 || bit == 13) continue;
            whp_hdr_init(&w, 1u << bit, buf, sizeof buf);
            CHECKF(f(h, &w, sizeof w) == MMSYSERR_INVALPARAM, "%s bit %d", whp_fn_name[fn], bit);
            CHECKF(w.dwFlags == (1u << bit), "%s bit %d flags", whp_fn_name[fn], bit);
        }
    }
    whp_close(h, ev);
}
