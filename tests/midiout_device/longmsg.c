/* midiOutLongMsg / Prepare / Unprepare on a live synth (id 0) and through the mapper.
 *
 * winmmbase!midiOutLongMsg (0x8620) [D], in order:
 *   pmh NULL, cbmh < 0x2C, dwFlags & 0xFFFFCFD0 -> 11; !PREPARED -> 64; INQUEUE -> 65; dwBufferLength 0 -> 11;
 *   then dwFlags &= ~MHDR_ISSTRM (8); handle type 3 -> midiMessage(MODM_LONGDATA = 8), type 9 -> 8, else 5.
 *   winmm itself never sets INQUEUE or DONE: the driver does (DONE + MOM_DONE when it has consumed the buffer).
 * winmmbase!midiOutUnprepareHeader (0x8390) [D]: handle first (5), ValidateHeader (11), not PREPARED -> 0
 *   untouched, INQUEUE -> 65, then MODM_UNPREPARE; a driver answer of 8 means "winmm does it": PREPARED cleared,
 *   bits 0x3000 kept from before the call.
 * winmmbase!midiOutPrepareHeader (0x8110) [D]: already PREPARED -> 0 untouched; else dwFlags = 0, MODM_PREPARE,
 *   on 8 winmm sets PREPARED itself.
 */
#include "mod.h"

#define PH(h, m) U(midiOutPrepareHeader)((h), (m), sizeof(MIDIHDR))
#define UH(h, m) U(midiOutUnprepareHeader)((h), (m), sizeof(MIDIHDR))
#define LM(h, m) U(midiOutLongMsg)((h), (m), sizeof(MIDIHDR))
#define REQ_LONG() do { REQUIRE(U(midiOutLongMsg)); REQUIRE(U(midiOutPrepareHeader)); REQUIRE(U(midiOutUnprepareHeader)); } while (0)

static const UINT g_ids[] = { 0, MIDI_MAPPER };
#define FOR_IDS(h) for (int ii_ = 0; ii_ < NELEM(g_ids); ii_++) \
    for (HMIDIOUT h = mod_open_null(g_ids[ii_]), *once_ = (HMIDIOUT *)1; once_ && h; once_ = NULL, mod_close(h))

/* never leave a queued header: reset, wait DONE, unprepare */
static void drain_hdr(HMIDIOUT h, MIDIHDR *m) {
    if (m->dwFlags & MHDR_INQUEUE) { U(midiOutReset)(h); mod_wait_done(m, 2000); }
    if (m->dwFlags & MHDR_PREPARED) U(midiOutUnprepareHeader)(h, m, sizeof(*m));
}

/* ================================================================ the full protocol */

/* GS reset sysex: prepare -> PREPARED, LongMsg -> 0, DONE within 2 s, unprepare -> 0 leaves DONE */
TEST(midiout_device_long_gs_reset_protocol) {
    MOD_NEED_LIVE(); REQ_LONG();
    FOR_IDS(h) {
        MIDIHDR m; mod_hdr(&m, mod_gs_reset, sizeof(mod_gs_reset));
        m.dwUser = 0x1234ABCD; m.dwOffset = 0xDEAD;
        EXPECT_EQ(PH(h, &m), MMSYSERR_NOERROR);
        EXPECT_EQ(m.dwFlags, MHDR_PREPARED);
        MMRESULT r = LM(h, &m);
        DWORD f_ret = m.dwFlags;
        EXPECT_EQ(r, MMSYSERR_NOERROR);
        t_note("id 0x%x flags right after LongMsg 0x%lx", g_ids[ii_], f_ret);
        CHECK(f_ret & MHDR_PREPARED);
        CHECKF(mod_wait_done(&m, 2000), "id 0x%x flags 0x%lx", g_ids[ii_], m.dwFlags);
        /* DONE can be set a moment before INQUEUE clears: wait for DONE without INQUEUE */
        for (int w = 0; w < 200 && (m.dwFlags & (MHDR_DONE | MHDR_INQUEUE)) != MHDR_DONE; w++) Sleep(10);
        EXPECT_EQ(m.dwFlags, MHDR_PREPARED | MHDR_DONE);
        EXPECT_EQ(UH(h, &m), MMSYSERR_NOERROR);
        EXPECT_EQ(m.dwFlags, MHDR_DONE);
        /* the caller's fields are untouched */
        EXPECT_EQ(m.dwUser, 0x1234ABCD);
        EXPECT_EQ(m.dwOffset, 0xDEAD);
        CHECK(m.lpData == (LPSTR)mod_gs_reset);
        EXPECT_EQ(m.dwBufferLength, sizeof(mod_gs_reset));
        t_note("id 0x%x after LongMsg: dwBytesRecorded=%lu lpNext=%p reserved=0x%llx", g_ids[ii_], m.dwBytesRecorded,
               m.lpNext, (unsigned long long)m.reserved);
    }
}

/* the same with GM system on / off and a short universal sysex */
TEST(midiout_device_long_gm_messages) {
    MOD_NEED_LIVE(); REQ_LONG();
    static const BYTE dev_inquiry[] = { 0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7 };
    struct { const BYTE *b; DWORD n; const char *name; } t[] = {
        { mod_gm_on, sizeof(mod_gm_on), "gm-on" }, { mod_gm_off, sizeof(mod_gm_off), "gm-off" },
        { dev_inquiry, sizeof(dev_inquiry), "inquiry" }, { mod_gs_reset, sizeof(mod_gs_reset), "gs-reset" },
    };
    FOR_IDS(h) {
        for (int i = 0; i < NELEM(t); i++)
            CHECKF(mod_send_long(h, t[i].b, t[i].n) == MMSYSERR_NOERROR, "id 0x%x %s", g_ids[ii_], t[i].name);
    }
}

/* a header is reusable: prepare once, send several times (DONE set again each time), unprepare once */
TEST(midiout_device_long_resend_prepared) {
    MOD_NEED_LIVE(); REQ_LONG();
    FOR_IDS(h) {
        MIDIHDR m; mod_hdr(&m, mod_gm_on, sizeof(mod_gm_on));
        EXPECT_EQ(PH(h, &m), MMSYSERR_NOERROR);
        for (int k = 0; k < 4; k++) {
            /* DONE from the previous round is still set: LongMsg does not refuse it [D] */
            MMRESULT r = LM(h, &m);
            CHECKF(r == MMSYSERR_NOERROR, "id 0x%x round %d r=%u", g_ids[ii_], k, r);
            CHECKF(mod_wait_done(&m, 2000), "round %d", k);
            CHECKF((m.dwFlags & (MHDR_PREPARED | MHDR_INQUEUE)) == MHDR_PREPARED, "round %d flags 0x%lx", k, m.dwFlags);
        }
        EXPECT_EQ(UH(h, &m), MMSYSERR_NOERROR);
        EXPECT_EQ(m.dwFlags & MHDR_PREPARED, 0);
    }
}

/* DONE cleared by the caller between sends comes back */
TEST(midiout_device_long_done_set_by_driver) {
    MOD_NEED_LIVE(); REQ_LONG();
    FOR_IDS(h) {
        MIDIHDR m; mod_hdr(&m, mod_gs_reset, sizeof(mod_gs_reset));
        EXPECT_EQ(PH(h, &m), MMSYSERR_NOERROR);
        for (int k = 0; k < 3; k++) {
            m.dwFlags &= ~MHDR_DONE;
            EXPECT_EQ(LM(h, &m), MMSYSERR_NOERROR);
            CHECKF(mod_wait_done(&m, 2000), "round %d flags 0x%lx", k, m.dwFlags);
        }
        EXPECT_EQ(UH(h, &m), MMSYSERR_NOERROR);
    }
}

/* MHDR_ISSTRM (8) is cleared by LongMsg before the handle is validated [D] */
TEST(midiout_device_long_clears_isstrm) {
    MOD_NEED_LIVE(); REQ_LONG();
    FOR_IDS(h) {
        MIDIHDR m; mod_hdr(&m, mod_gs_reset, sizeof(mod_gs_reset));
        EXPECT_EQ(PH(h, &m), MMSYSERR_NOERROR);
        m.dwFlags |= MHDR_ISSTRM;
        EXPECT_EQ(LM(h, &m), MMSYSERR_NOERROR);
        CHECK(mod_wait_done(&m, 2000));
        EXPECT_EQ(m.dwFlags & MHDR_ISSTRM, 0);
        drain_hdr(h, &m);
    }
    /* also on a NULL handle: cleared, then 5 */
    MIDIHDR m; mod_hdr(&m, mod_gs_reset, sizeof(mod_gs_reset));
    m.dwFlags = MHDR_PREPARED | MHDR_ISSTRM;
    EXPECT_EQ(LM(NULL, &m), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(m.dwFlags, MHDR_PREPARED);
}

/* the 0x1000 / 0x2000 bits are allowed by the masks; Prepare keeps neither (dwFlags = 0 first) */
/* UNVERIFIED: pin after the first Win10 run */
TEST(midiout_device_long_reserved_bits) {
    MOD_NEED_LIVE(); REQ_LONG();
    static const DWORD bits[] = { 0x1000, 0x2000, 0x3000 };
    FOR_IDS(h) {
        for (int i = 0; i < NELEM(bits); i++) {
            MIDIHDR m; mod_hdr(&m, mod_gs_reset, sizeof(mod_gs_reset));
            m.dwFlags = bits[i];
            EXPECT_EQ(PH(h, &m), MMSYSERR_NOERROR);
            CHECKF(m.dwFlags == MHDR_PREPARED, "bits 0x%lx -> 0x%lx", bits[i], m.dwFlags);
            m.dwFlags |= bits[i];
            MMRESULT r = LM(h, &m);
            CHECKF(r == MMSYSERR_NOERROR, "bits 0x%lx r=%u", bits[i], r);
            if (r == MMSYSERR_NOERROR) CHECK(mod_wait_done(&m, 2000));
            t_note("id 0x%x bits 0x%lx after send 0x%lx", g_ids[ii_], bits[i], m.dwFlags);
            r = UH(h, &m);
            EXPECT_EQ(r, MMSYSERR_NOERROR);
            CHECKF((m.dwFlags & 0x3000) == bits[i], "unprepare kept 0x%lx", m.dwFlags & 0x3000);
            EXPECT_EQ(m.dwFlags & MHDR_PREPARED, 0);
        }
    }
}

/* ================================================================ odd content */

/* sysex without the closing F7, a lone F0, a lone F7: the driver's answer is recorded */
TEST(midiout_device_long_unterminated_sysex) {
    MOD_NEED_LIVE(); REQ_LONG();
    static const BYTE no_f7[] = { 0xF0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7F, 0x00, 0x41 };
    static const BYTE f0[] = { 0xF0 };
    static const BYTE f7[] = { 0xF7 };
    static const BYTE f0f7[] = { 0xF0, 0xF7 };
    struct { const BYTE *b; DWORD n; const char *name; } t[] = {
        { no_f7, sizeof(no_f7), "no-f7" }, { f0, 1, "f0" }, { f7, 1, "f7" }, { f0f7, 2, "f0f7" },
    };
    FOR_IDS(h) {
        for (int i = 0; i < NELEM(t); i++) {
            MIDIHDR m; mod_hdr(&m, t[i].b, t[i].n);
            EXPECT_EQ(PH(h, &m), MMSYSERR_NOERROR);
            MMRESULT r = LM(h, &m);
            int done = r == MMSYSERR_NOERROR ? mod_wait_done(&m, 2000) : -1;
            t_note("id 0x%x %s: LongMsg=%u done=%d flags=0x%lx", g_ids[ii_], t[i].name, r, done, m.dwFlags);
            CHECKF(r == MMSYSERR_NOERROR, "%s r=%u", t[i].name, r);
            /* completion of a malformed sysex is driver-defined: recorded only; drain_hdr resets if still queued */
            if (done != 1) t_note("id 0x%x %s: not DONE within 2 s (INQUEUE kept); resetting", g_ids[ii_], t[i].name);
            drain_hdr(h, &m);
        }
        /* the handle still works after the odd buffers */
        EXPECT_EQ(mod_send_long(h, mod_gs_reset, sizeof(mod_gs_reset)), MMSYSERR_NOERROR);
    }
}

/* channel messages in a long buffer (no sysex framing) */
TEST(midiout_device_long_channel_bytes) {
    MOD_NEED_LIVE(); REQ_LONG();
    static const BYTE msgs[] = { 0xB0, 123, 0, 0xC0, 0, 0x90, 60, 0, 0x80, 60, 0 };
    FOR_IDS(h) {
        MIDIHDR m; mod_hdr(&m, msgs, sizeof(msgs));
        EXPECT_EQ(PH(h, &m), MMSYSERR_NOERROR);
        MMRESULT r = LM(h, &m);
        int done = r == MMSYSERR_NOERROR ? mod_wait_done(&m, 2000) : -1;
        t_note("id 0x%x channel bytes in a long buffer: %u done=%d", g_ids[ii_], r, done);
        EXPECT_EQ(r, MMSYSERR_NOERROR);
        CHECK(done == 1);
        drain_hdr(h, &m);
    }
}

/* two sysex messages in one buffer, and a buffer with trailing bytes after F7 */
TEST(midiout_device_long_multiple_sysex_in_buffer) {
    MOD_NEED_LIVE(); REQ_LONG();
    BYTE b[64]; DWORD n = 0;
    memcpy(b + n, mod_gm_on, sizeof(mod_gm_on)); n += sizeof(mod_gm_on);
    memcpy(b + n, mod_gs_reset, sizeof(mod_gs_reset)); n += sizeof(mod_gs_reset);
    BYTE tail[16]; memcpy(tail, mod_gm_on, sizeof(mod_gm_on)); tail[6] = 0x00; tail[7] = 0x00;
    FOR_IDS(h) {
        EXPECT_EQ(mod_send_long(h, b, n), MMSYSERR_NOERROR);
        EXPECT_EQ(mod_send_long(h, tail, 8), MMSYSERR_NOERROR);
    }
}

/* larger buffers: 1 KB and 16 KB of repeated GS resets (DONE within 2 s) */
TEST(midiout_device_long_large_buffers) {
    MOD_NEED_LIVE(); REQ_LONG();
    static const DWORD sizes[] = { 1100, 16500 };
    BYTE *buf = (BYTE *)HeapAlloc(GetProcessHeap(), 0, 16500);
    if (!buf) { t_skip("no memory"); return; }
    for (DWORD i = 0; i < 16500; i++) buf[i] = mod_gs_reset[i % sizeof(mod_gs_reset)];
    FOR_IDS(h) {
        for (int i = 0; i < NELEM(sizes); i++) {
            MIDIHDR m; mod_hdr(&m, buf, sizes[i]);
            EXPECT_EQ(PH(h, &m), MMSYSERR_NOERROR);
            double t0 = mod_now();
            MMRESULT r = LM(h, &m);
            double t1 = mod_now();
            DWORD f = m.dwFlags;
            int done = r == MMSYSERR_NOERROR ? mod_wait_done(&m, 2000) : -1;
            t_note("id 0x%x %lu bytes: r=%u flags at return 0x%lx call %.1f ms done %.1f ms", g_ids[ii_], sizes[i], r, f,
                   t1 - t0, mod_now() - t0);
            EXPECT_EQ(r, MMSYSERR_NOERROR);
            CHECK(done == 1);
            drain_hdr(h, &m);
        }
    }
    HeapFree(GetProcessHeap(), 0, buf);
}

/* more than 64 KB: the midiOut path has no 64 KB limit in winmm (that limit is the stream prepare's) */
/* UNVERIFIED: pin after the first Win10 run */
TEST(midiout_device_long_over_64k) {
    MOD_NEED_LIVE(); REQ_LONG();
    DWORD n = 70000;
    BYTE *buf = (BYTE *)HeapAlloc(GetProcessHeap(), 0, n);
    if (!buf) { t_skip("no memory"); return; }
    for (DWORD i = 0; i < n; i++) buf[i] = mod_gs_reset[i % sizeof(mod_gs_reset)];
    HMIDIOUT h = mod_open_null(0);
    if (h) {
        MIDIHDR m; mod_hdr(&m, buf, n);
        MMRESULT r = PH(h, &m);
        t_note("prepare 70000 -> %u flags 0x%lx", r, m.dwFlags);
        EXPECT_EQ(r, MMSYSERR_NOERROR);
        if (r == MMSYSERR_NOERROR) {
            r = LM(h, &m);
            int done = r == MMSYSERR_NOERROR ? mod_wait_done(&m, 2000) : -1;
            t_note("long 70000 -> %u done=%d flags 0x%lx", r, done, m.dwFlags);
            drain_hdr(h, &m);
        }
        mod_close(h);
    }
    HeapFree(GetProcessHeap(), 0, buf);
}

/* the minimum header size 0x2C (the pre-dwOffset MIDIHDR) is accepted end to end */
TEST(midiout_device_long_short_header_size) {
    MOD_NEED_LIVE(); REQ_LONG();
    static const UINT cbs[] = { 0x2C, 0x30, sizeof(MIDIHDR), sizeof(MIDIHDR) + 16 };
    FOR_IDS(h) {
        for (int i = 0; i < NELEM(cbs); i++) {
            MIDIHDR m; mod_hdr(&m, mod_gs_reset, sizeof(mod_gs_reset));
            CHECKF(U(midiOutPrepareHeader)(h, &m, cbs[i]) == MMSYSERR_NOERROR, "cb 0x%x prepare", cbs[i]);
            MMRESULT r = U(midiOutLongMsg)(h, &m, cbs[i]);
            CHECKF(r == MMSYSERR_NOERROR, "cb 0x%x long r=%u", cbs[i], r);
            if (r == MMSYSERR_NOERROR) CHECK(mod_wait_done(&m, 2000));
            CHECKF(U(midiOutUnprepareHeader)(h, &m, cbs[i]) == MMSYSERR_NOERROR, "cb 0x%x unprepare", cbs[i]);
            EXPECT_EQ(m.dwFlags & MHDR_PREPARED, 0);
        }
    }
}

/* ================================================================ several headers */

TEST(midiout_device_long_several_headers) {
    MOD_NEED_LIVE(); REQ_LONG();
    FOR_IDS(h) {
        MIDIHDR m[4];
        for (int i = 0; i < 4; i++) {
            mod_hdr(&m[i], (i & 1) ? mod_gm_on : mod_gs_reset, (i & 1) ? sizeof(mod_gm_on) : sizeof(mod_gs_reset));
            m[i].dwUser = 100 + i;
            EXPECT_EQ(PH(h, &m[i]), MMSYSERR_NOERROR);
        }
        for (int i = 0; i < 4; i++) CHECKF(LM(h, &m[i]) == MMSYSERR_NOERROR, "hdr %d", i);
        for (int i = 0; i < 4; i++) CHECKF(mod_wait_done(&m[i], 2000), "hdr %d flags 0x%lx", i, m[i].dwFlags);
        for (int i = 0; i < 4; i++) {
            CHECKF(UH(h, &m[i]) == MMSYSERR_NOERROR, "hdr %d", i);
            EXPECT_EQ(m[i].dwUser, 100 + i);
            drain_hdr(h, &m[i]);
        }
    }
}

/* ================================================================ protocol errors on a live handle */

/* unprepared -> 64, INQUEUE -> 65 at LongMsg and Unprepare; flags untouched by the refusals */
TEST(midiout_device_long_state_errors) {
    MOD_NEED_LIVE(); REQ_LONG();
    FOR_IDS(h) {
        MIDIHDR m; mod_hdr(&m, mod_gs_reset, sizeof(mod_gs_reset));
        m.dwFlags = MHDR_DONE;
        EXPECT_EQ(LM(h, &m), MIDIERR_UNPREPARED);
        EXPECT_EQ(m.dwFlags, MHDR_DONE);
        EXPECT_EQ(PH(h, &m), MMSYSERR_NOERROR);
        EXPECT_EQ(m.dwFlags, MHDR_PREPARED);
        m.dwFlags |= MHDR_INQUEUE;
        EXPECT_EQ(LM(h, &m), MIDIERR_STILLPLAYING);
        EXPECT_EQ(UH(h, &m), MIDIERR_STILLPLAYING);
        EXPECT_EQ(m.dwFlags, MHDR_PREPARED | MHDR_INQUEUE);
        m.dwFlags &= ~MHDR_INQUEUE;
        EXPECT_EQ(UH(h, &m), MMSYSERR_NOERROR);
        EXPECT_EQ(m.dwFlags, 0);
        /* after unprepare the header is unprepared again */
        EXPECT_EQ(LM(h, &m), MIDIERR_UNPREPARED);
    }
}

/* a header really queued: while the driver holds it (INQUEUE seen), Unprepare answers 65 */
TEST(midiout_device_long_unprepare_while_queued) {
    MOD_NEED_LIVE(); REQ_LONG();
    DWORD n = 16500;
    BYTE *buf = (BYTE *)HeapAlloc(GetProcessHeap(), 0, n);
    if (!buf) { t_skip("no memory"); return; }
    for (DWORD i = 0; i < n; i++) buf[i] = mod_gs_reset[i % sizeof(mod_gs_reset)];
    HMIDIOUT h = mod_open_null(0);
    if (h) {
        int seen = 0;
        for (int round = 0; round < 3 && !seen; round++) {
            MIDIHDR m; mod_hdr(&m, buf, n);
            EXPECT_EQ(PH(h, &m), MMSYSERR_NOERROR);
            EXPECT_EQ(LM(h, &m), MMSYSERR_NOERROR);
            if (m.dwFlags & MHDR_INQUEUE) {
                seen = 1;
                MMRESULT r = UH(h, &m);
                if (m.dwFlags & MHDR_INQUEUE || r == MIDIERR_STILLPLAYING) {
                    EXPECT_EQ(r, MIDIERR_STILLPLAYING);
                    CHECK(m.dwFlags & MHDR_PREPARED);
                }
            }
            CHECK(mod_wait_done(&m, 2000));
            EXPECT_EQ(m.dwFlags & MHDR_INQUEUE, 0);
            drain_hdr(h, &m);
            EXPECT_EQ(m.dwFlags & MHDR_PREPARED, 0);
        }
        t_note("INQUEUE observed after LongMsg returned: %d (0 = the synth consumed the buffer synchronously)", seen);
        mod_close(h);
        if (!seen) { HeapFree(GetProcessHeap(), 0, buf); t_skip("INQUEUE never observed: queued-unprepare not exercised"); return; }
    }
    HeapFree(GetProcessHeap(), 0, buf);
}

/* PROBE (records only, asserts nothing; not a verified test): prepared on one handle, sent on another handle of the same device (after the first closed) */
TEST(midiout_device_long_header_moved_between_handles) {
    MOD_NEED_LIVE(); REQ_LONG();
    HMIDIOUT a = mod_open_null(0);
    if (!a) return;
    MIDIHDR m; mod_hdr(&m, mod_gs_reset, sizeof(mod_gs_reset));
    EXPECT_EQ(PH(a, &m), MMSYSERR_NOERROR);
    mod_close(a);
    EXPECT_EQ(m.dwFlags, MHDR_PREPARED);
    HMIDIOUT b = mod_open_null(MIDI_MAPPER);
    if (!b) return;
    MMRESULT r = LM(b, &m);
    int done = r == MMSYSERR_NOERROR ? mod_wait_done(&m, 2000) : -1;
    t_note("header prepared on a closed handle, sent on another: %u done=%d flags 0x%lx", r, done, m.dwFlags);
    r = UH(b, &m);
    t_note("unprepare on the second handle: %u flags 0x%lx", r, m.dwFlags);
    drain_hdr(b, &m);
    mod_close(b);
}

/* ================================================================ close with a buffer just sent */

TEST(midiout_device_long_close_after_send) {
    MOD_NEED_LIVE(); REQ_LONG();
    static const UINT ids[] = { 0, MIDI_MAPPER };
    for (int i = 0; i < NELEM(ids); i++) {
        HMIDIOUT h = mod_open_null(ids[i]);
        if (!h) continue;
        MIDIHDR m; mod_hdr(&m, mod_gs_reset, sizeof(mod_gs_reset));
        EXPECT_EQ(PH(h, &m), MMSYSERR_NOERROR);
        EXPECT_EQ(LM(h, &m), MMSYSERR_NOERROR);
        /* closing with the header still prepared (DONE or not) */
        MMRESULT r = U(midiOutClose)(h);
        t_note("id 0x%x close right after LongMsg: %u flags 0x%lx", ids[i], r, m.dwFlags);
        if (r == MMSYSERR_NOERROR) {
            mod_forget(h);
            CHECK(m.dwFlags & MHDR_DONE);
            /* the header stays PREPARED; its handle is gone */
            EXPECT_EQ(m.dwFlags & MHDR_PREPARED, MHDR_PREPARED);
            EXPECT_EQ(UH(h, &m), MMSYSERR_INVALHANDLE);
        } else {
            EXPECT_EQ(r, MIDIERR_STILLPLAYING);
            drain_hdr(h, &m);
            mod_close(h);
        }
    }
}
