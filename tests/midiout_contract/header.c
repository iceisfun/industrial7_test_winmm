/* MIDIHDR protocol of midiOutLongMsg (winmmbase !0x8620), midiOutPrepareHeader (!0x8110) and
 * midiOutUnprepareHeader (!0x8390).
 *
 * LongMsg (header FIRST, handle last):
 *   pmh NULL || cbmh < 0x2C || dwFlags & 0xFFFFCFD0      -> 11
 *   !(dwFlags & MHDR_PREPARED)                           -> 64 MIDIERR_UNPREPARED
 *   dwFlags & MHDR_INQUEUE                               -> 65 MIDIERR_STILLPLAYING
 *   dwBufferLength == 0                                  -> 11
 *   dwFlags &= ~MHDR_ISSTRM  (written before the handle check)
 *   handle not a midiOut (3) / stream (9) handle         -> 5; stream handle -> 8
 * Prepare (handle FIRST): handle -> 5; ValidateHeader(type 3) -> 11; PREPARED already -> 0 untouched;
 *   dwFlags = 0; MODM_PREPARE; driver 8 -> winmm marks PREPARED.
 * Unprepare (handle FIRST): handle -> 5; ValidateHeader -> 11; not PREPARED -> 0 untouched;
 *   INQUEUE -> 65; MODM_UNPREPARE; driver 8 -> winmm clears PREPARED.
 */
#include "moc.h"

#define LM(h, p, c) U(midiOutLongMsg)((h), (p), (c))
#define PH(h, p, c) U(midiOutPrepareHeader)((h), (p), (c))
#define UH(h, p, c) U(midiOutUnprepareHeader)((h), (p), (c))
#define REQ3() do { REQUIRE(U(midiOutLongMsg)); REQUIRE(U(midiOutPrepareHeader)); REQUIRE(U(midiOutUnprepareHeader)); } while (0)

static char g_buf[256];
#define BH(i) ((HMIDIOUT)moc_bad_handles[i])

/* ================================================================ LongMsg front check */

TEST(midiout_contract_longmsg_null_11) {
    REQUIRE(U(midiOutLongMsg));
    static const UINT cbs[] = { 0, 1, 0x2B, 0x2C, sizeof(MIDIHDR), 0x1000, 0xFFFFFFFF };
    for (int i = 0; i < moc_nbad; i++)
        for (int j = 0; j < NELEM(cbs); j++)
            CHECKF(LM(BH(i), NULL, cbs[j]) == MMSYSERR_INVALPARAM, "h=%p cb=%u", BH(i), cbs[j]);
}

static void lm_small_cb(DWORD flags) {
    for (UINT cb = 0; cb < MOC_HDR_MINCB; cb++) {
        MIDIHDR m; moc_hdr(&m, g_buf, 16, flags);
        CHECKF(LM(NULL, &m, cb) == MMSYSERR_INVALPARAM, "cb=%u fl=0x%lx", cb, flags);
        CHECKF(m.dwFlags == flags, "cb=%u flags changed 0x%lx", cb, m.dwFlags);
    }
}
TEST(midiout_contract_longmsg_small_cb_11_unprepared) { REQUIRE(U(midiOutLongMsg)); lm_small_cb(0); }
TEST(midiout_contract_longmsg_small_cb_11_prepared) { REQUIRE(U(midiOutLongMsg)); lm_small_cb(MHDR_PREPARED); }
TEST(midiout_contract_longmsg_small_cb_11_inqueue) { REQUIRE(U(midiOutLongMsg)); lm_small_cb(MHDR_PREPARED | MHDR_INQUEUE); }
TEST(midiout_contract_longmsg_small_cb_11_isstrm) { REQUIRE(U(midiOutLongMsg)); lm_small_cb(MHDR_PREPARED | MHDR_ISSTRM); }

/* cb 0x2C is the minimum (not sizeof(MIDIHDR) = 0x70): larger values pass to the state checks */
TEST(midiout_contract_longmsg_cb_min_2c) {
    REQUIRE(U(midiOutLongMsg));
    EXPECT_EQ(sizeof(MIDIHDR), 0x70);
    static const UINT cbs[] = { 0x2C, 0x2D, 0x30, 0x40, 0x6F, 0x70, 0x71, 0x100, 0x10000, 0xFFFFFFFF };
    for (int i = 0; i < NELEM(cbs); i++) {
        MIDIHDR m; moc_hdr(&m, g_buf, 16, 0);
        CHECKF(LM(NULL, &m, cbs[i]) == MIDIERR_UNPREPARED, "unprepared cb=0x%x", cbs[i]);
        moc_hdr(&m, g_buf, 16, MHDR_PREPARED);
        CHECKF(LM(NULL, &m, cbs[i]) == MMSYSERR_INVALHANDLE, "prepared cb=0x%x", cbs[i]);
    }
}

/* every bit of 0xFFFFCFD0 -> 11, with any combination of the legal bits */
static void lm_bad_bits(DWORD base) {
    for (int b = 0; b < 32; b++) {
        DWORD bit = 1u << b;
        if (!(bit & MOC_HDR_BADMASK)) continue;
        MIDIHDR m; moc_hdr(&m, g_buf, 16, base | bit);
        CHECKF(LM(NULL, &m, sizeof(m)) == MMSYSERR_INVALPARAM, "bit %d base 0x%lx", b, base);
        CHECKF(m.dwFlags == (base | bit), "bit %d: flags changed to 0x%lx", b, m.dwFlags);
    }
}
TEST(midiout_contract_longmsg_bad_flag_bits_base0) { REQUIRE(U(midiOutLongMsg)); lm_bad_bits(0); }
TEST(midiout_contract_longmsg_bad_flag_bits_prepared) { REQUIRE(U(midiOutLongMsg)); lm_bad_bits(MHDR_PREPARED); }
TEST(midiout_contract_longmsg_bad_flag_bits_inqueue) { REQUIRE(U(midiOutLongMsg)); lm_bad_bits(MHDR_PREPARED | MHDR_INQUEUE); }
TEST(midiout_contract_longmsg_bad_flag_bits_all_legal) { REQUIRE(U(midiOutLongMsg)); lm_bad_bits(0x302F); }

/* 0x10 is rejected for MIDI headers (it is legal for WAVEHDR) */
TEST(midiout_contract_longmsg_flag_0x10_rejected) {
    REQUIRE(U(midiOutLongMsg));
    static const DWORD fl[] = { 0x10, 0x12, 0x13, 0x16, 0x1A, 0x312F };
    for (int i = 0; i < NELEM(fl); i++) {
        MIDIHDR m; moc_hdr(&m, g_buf, 16, fl[i]);
        CHECKF(LM(NULL, &m, sizeof(m)) == MMSYSERR_INVALPARAM, "fl=0x%lx", fl[i]);
    }
}

/* legal bits: DONE 1, PREPARED 2, INQUEUE 4, ISSTRM 8, 0x20, 0x1000, 0x2000 */
TEST(midiout_contract_longmsg_legal_bits_pass_front) {
    REQUIRE(U(midiOutLongMsg));
    static const DWORD fl[] = { MHDR_DONE, MHDR_ISSTRM, 0x20, 0x1000, 0x2000, 0x3029 };
    for (int i = 0; i < NELEM(fl); i++) {
        MIDIHDR m; moc_hdr(&m, g_buf, 16, fl[i]);
        CHECKF(LM(NULL, &m, sizeof(m)) == MIDIERR_UNPREPARED, "unprepared fl=0x%lx", fl[i]);
        moc_hdr(&m, g_buf, 16, fl[i] | MHDR_PREPARED);
        CHECKF(LM(NULL, &m, sizeof(m)) == MMSYSERR_INVALHANDLE, "prepared fl=0x%lx", fl[i] | MHDR_PREPARED);
    }
}

/* ================================================================ LongMsg state checks */

TEST(midiout_contract_longmsg_unprepared_64_any_handle) {
    REQUIRE(U(midiOutLongMsg));
    static const DWORD fl[] = { 0, MHDR_DONE, MHDR_INQUEUE, MHDR_ISSTRM, MHDR_INQUEUE | MHDR_DONE, 0x302D };
    for (int i = 0; i < moc_nbad; i++)
        for (int j = 0; j < NELEM(fl); j++) {
            MIDIHDR m; moc_hdr(&m, g_buf, 16, fl[j]);
            CHECKF(LM(BH(i), &m, sizeof(m)) == MIDIERR_UNPREPARED, "h=%p fl=0x%lx", BH(i), fl[j]);
            CHECKF(m.dwFlags == fl[j], "h=%p flags changed 0x%lx", BH(i), m.dwFlags);
        }
}

/* UNPREPARED beats a zero length and a NULL buffer */
TEST(midiout_contract_longmsg_unprepared_beats_length) {
    REQUIRE(U(midiOutLongMsg));
    MIDIHDR m; moc_hdr(&m, NULL, 0, 0);
    EXPECT_EQ(LM(NULL, &m, sizeof(m)), MIDIERR_UNPREPARED);
    moc_hdr(&m, NULL, 0, MHDR_INQUEUE);
    EXPECT_EQ(LM(NULL, &m, sizeof(m)), MIDIERR_UNPREPARED);
}

TEST(midiout_contract_longmsg_inqueue_65_any_handle) {
    REQUIRE(U(midiOutLongMsg));
    static const DWORD fl[] = { MHDR_PREPARED | MHDR_INQUEUE, MHDR_PREPARED | MHDR_INQUEUE | MHDR_DONE,
                                MHDR_PREPARED | MHDR_INQUEUE | MHDR_ISSTRM, 0x302F };
    for (int i = 0; i < moc_nbad; i++)
        for (int j = 0; j < NELEM(fl); j++) {
            MIDIHDR m; moc_hdr(&m, g_buf, 16, fl[j]);
            CHECKF(LM(BH(i), &m, sizeof(m)) == MIDIERR_STILLPLAYING, "h=%p fl=0x%lx", BH(i), fl[j]);
            CHECKF(m.dwFlags == fl[j], "h=%p flags changed 0x%lx", BH(i), m.dwFlags);
        }
}

TEST(midiout_contract_longmsg_inqueue_beats_length) {
    REQUIRE(U(midiOutLongMsg));
    MIDIHDR m; moc_hdr(&m, NULL, 0, MHDR_PREPARED | MHDR_INQUEUE);
    EXPECT_EQ(LM(NULL, &m, sizeof(m)), MIDIERR_STILLPLAYING);
}

TEST(midiout_contract_longmsg_zero_length_11_any_handle) {
    REQUIRE(U(midiOutLongMsg));
    static const DWORD fl[] = { MHDR_PREPARED, MHDR_PREPARED | MHDR_DONE, MHDR_PREPARED | MHDR_ISSTRM, 0x302B };
    for (int i = 0; i < moc_nbad; i++)
        for (int j = 0; j < NELEM(fl); j++) {
            MIDIHDR m; moc_hdr(&m, g_buf, 0, fl[j]);
            CHECKF(LM(BH(i), &m, sizeof(m)) == MMSYSERR_INVALPARAM, "h=%p fl=0x%lx", BH(i), fl[j]);
            /* the ISSTRM clear happens only after the length check */
            CHECKF(m.dwFlags == fl[j], "h=%p flags changed 0x%lx", BH(i), m.dwFlags);
        }
}

/* the length is the only buffer check: lpData NULL / dwBytesRecorded are not looked at */
TEST(midiout_contract_longmsg_buffer_pointer_not_checked) {
    REQUIRE(U(midiOutLongMsg));
    MIDIHDR m; moc_hdr(&m, NULL, 16, MHDR_PREPARED);
    EXPECT_EQ(LM(NULL, &m, sizeof(m)), MMSYSERR_INVALHANDLE);
    moc_hdr(&m, g_buf, 16, MHDR_PREPARED);
    m.dwBytesRecorded = 0xFFFFFFFF;
    EXPECT_EQ(LM(NULL, &m, sizeof(m)), MMSYSERR_INVALHANDLE);
    moc_hdr(&m, g_buf, 0xFFFFFFFF, MHDR_PREPARED);
    EXPECT_EQ(LM(NULL, &m, sizeof(m)), MMSYSERR_INVALHANDLE);
}

/* ================================================================ LongMsg handle check */

TEST(midiout_contract_longmsg_bad_handle_5) {
    REQUIRE(U(midiOutLongMsg));
    for (int i = 0; i < moc_nbad; i++) {
        MIDIHDR m; moc_hdr(&m, g_buf, 16, MHDR_PREPARED);
        CHECKF(LM(BH(i), &m, sizeof(m)) == MMSYSERR_INVALHANDLE, "h=%p", BH(i));
        EXPECT_EQ(m.dwFlags, MHDR_PREPARED);
    }
}

/* MHDR_ISSTRM is cleared before the handle is checked; the other bits survive */
TEST(midiout_contract_longmsg_clears_isstrm_before_handle) {
    REQUIRE(U(midiOutLongMsg));
    static const DWORD fl[] = { MHDR_PREPARED | MHDR_ISSTRM, MHDR_PREPARED | MHDR_ISSTRM | MHDR_DONE,
                                MHDR_PREPARED | MHDR_ISSTRM | 0x20, MHDR_PREPARED | MHDR_ISSTRM | 0x3000,
                                MHDR_PREPARED | MHDR_DONE | 0x3020 };
    for (int i = 0; i < moc_nbad; i++)
        for (int j = 0; j < NELEM(fl); j++) {
            MIDIHDR m; moc_hdr(&m, g_buf, 16, fl[j]);
            CHECKF(LM(BH(i), &m, sizeof(m)) == MMSYSERR_INVALHANDLE, "h=%p fl=0x%lx", BH(i), fl[j]);
            CHECKF(m.dwFlags == (fl[j] & ~MHDR_ISSTRM), "h=%p fl=0x%lx -> 0x%lx", BH(i), fl[j], m.dwFlags);
        }
}

TEST(midiout_contract_longmsg_other_fields_untouched) {
    REQUIRE(U(midiOutLongMsg));
    MIDIHDR m; moc_hdr(&m, g_buf, 16, MHDR_PREPARED | MHDR_ISSTRM);
    m.dwUser = 0x1122334455667788ull; m.dwBytesRecorded = 7; m.dwOffset = 9;
    m.lpNext = (LPMIDIHDR)(ULONG_PTR)0x1234; m.reserved = 0x55;
    for (int k = 0; k < 8; k++) m.dwReserved[k] = 0xA0 + k;
    MIDIHDR copy = m;
    EXPECT_EQ(LM(NULL, &m, sizeof(m)), MMSYSERR_INVALHANDLE);
    copy.dwFlags &= ~MHDR_ISSTRM;
    CHECK(!memcmp(&m, &copy, sizeof(m)));
}

TEST(midiout_contract_longmsg_mmio_handle_5) {
    REQUIRE(U(midiOutLongMsg));
    HMMIO mm = moc_mmio_open();
    if (!mm) { t_skip("no mmio handle"); return; }
    MIDIHDR m; moc_hdr(&m, g_buf, 16, MHDR_PREPARED | MHDR_ISSTRM);
    EXPECT_EQ(LM((HMIDIOUT)mm, &m, sizeof(m)), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(m.dwFlags, MHDR_PREPARED);
    moc_hdr(&m, g_buf, 16, 0);
    EXPECT_EQ(LM((HMIDIOUT)mm, &m, sizeof(m)), MIDIERR_UNPREPARED);
    moc_mmio_close(mm);
}

/* ================================================================ Prepare/Unprepare: handle first */

TEST(midiout_contract_prepare_bad_handle_5_null_header) {
    REQUIRE(U(midiOutPrepareHeader));
    for (int i = 0; i < moc_nbad; i++) {
        CHECKF(PH(BH(i), NULL, sizeof(MIDIHDR)) == MMSYSERR_INVALHANDLE, "h=%p", BH(i));
        CHECKF(PH(BH(i), NULL, 0) == MMSYSERR_INVALHANDLE, "h=%p cb0", BH(i));
    }
}

TEST(midiout_contract_unprepare_bad_handle_5_null_header) {
    REQUIRE(U(midiOutUnprepareHeader));
    for (int i = 0; i < moc_nbad; i++) {
        CHECKF(UH(BH(i), NULL, sizeof(MIDIHDR)) == MMSYSERR_INVALHANDLE, "h=%p", BH(i));
        CHECKF(UH(BH(i), NULL, 0) == MMSYSERR_INVALHANDLE, "h=%p cb0", BH(i));
    }
}

/* with a bad handle nothing in the header is examined or written, whatever its state */
TEST(midiout_contract_prepare_unprepare_bad_handle_header_untouched) {
    REQ3();
    static const DWORD fl[] = { 0, MHDR_DONE, MHDR_PREPARED, MHDR_INQUEUE, MHDR_PREPARED | MHDR_INQUEUE, 0x10, 0xFFFFFFFF, 0x302F };
    static const UINT cbs[] = { 0, 0x2B, 0x2C, sizeof(MIDIHDR) };
    for (int i = 0; i < moc_nbad; i++)
        for (int j = 0; j < NELEM(fl); j++)
            for (int k = 0; k < NELEM(cbs); k++) {
                MIDIHDR m; moc_hdr(&m, g_buf, 16, fl[j]);
                CHECKF(PH(BH(i), &m, cbs[k]) == MMSYSERR_INVALHANDLE, "prepare h=%p fl=0x%lx cb=%u", BH(i), fl[j], cbs[k]);
                CHECKF(m.dwFlags == fl[j], "prepare flags changed 0x%lx", m.dwFlags);
                CHECKF(UH(BH(i), &m, cbs[k]) == MMSYSERR_INVALHANDLE, "unprepare h=%p fl=0x%lx cb=%u", BH(i), fl[j], cbs[k]);
                CHECKF(m.dwFlags == fl[j], "unprepare flags changed 0x%lx", m.dwFlags);
            }
}

TEST(midiout_contract_prepare_unprepare_mmio_handle_5) {
    REQ3();
    HMMIO mm = moc_mmio_open();
    if (!mm) { t_skip("no mmio handle"); return; }
    MIDIHDR m; moc_hdr(&m, g_buf, 16, 0);
    EXPECT_EQ(PH((HMIDIOUT)mm, &m, sizeof(m)), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(PH((HMIDIOUT)mm, NULL, sizeof(m)), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(m.dwFlags, 0);
    m.dwFlags = MHDR_PREPARED;
    EXPECT_EQ(UH((HMIDIOUT)mm, &m, sizeof(m)), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(m.dwFlags, MHDR_PREPARED);
    moc_mmio_close(mm);
}

/* ================================================================ Prepare/Unprepare on a live handle */

TEST(midiout_contract_prepare_live_header_checks_11) {
    REQ3(); REQUIRE(U(midiOutOpen)); REQUIRE(U(midiOutClose));
    HMIDIOUT h = moc_open_live();
    if (!h) return;
    MIDIHDR m;
    EXPECT_EQ(PH(h, NULL, sizeof(m)), MMSYSERR_INVALPARAM);
    EXPECT_EQ(UH(h, NULL, sizeof(m)), MMSYSERR_INVALPARAM);
    for (UINT cb = 0; cb < MOC_HDR_MINCB; cb++) {
        moc_hdr(&m, g_buf, 16, 0);
        CHECKF(PH(h, &m, cb) == MMSYSERR_INVALPARAM, "prepare cb=%u", cb);
        CHECKF(m.dwFlags == 0, "prepare cb=%u wrote flags", cb);
        moc_hdr(&m, g_buf, 16, MHDR_PREPARED);
        CHECKF(UH(h, &m, cb) == MMSYSERR_INVALPARAM, "unprepare cb=%u", cb);
        CHECKF(m.dwFlags == MHDR_PREPARED, "unprepare cb=%u wrote flags", cb);
    }
    for (int b = 0; b < 32; b++) {
        DWORD bit = 1u << b;
        if (!(bit & MOC_HDR_BADMASK)) continue;
        moc_hdr(&m, g_buf, 16, bit);
        CHECKF(PH(h, &m, sizeof(m)) == MMSYSERR_INVALPARAM, "prepare bit %d", b);
        moc_hdr(&m, g_buf, 16, bit | MHDR_PREPARED);
        CHECKF(UH(h, &m, sizeof(m)) == MMSYSERR_INVALPARAM, "unprepare bit %d", b);
        CHECKF(m.dwFlags == (bit | MHDR_PREPARED), "unprepare bit %d flags 0x%lx", b, m.dwFlags);
    }
    moc_close(h);
}

TEST(midiout_contract_prepare_live_sets_prepared_only) {
    REQ3(); REQUIRE(U(midiOutOpen)); REQUIRE(U(midiOutClose));
    HMIDIOUT h = moc_open_live();
    if (!h) return;
    /* prepare zeroes every flag (DONE, INQUEUE, ISSTRM, 0x20, 0x1000, 0x2000) before marking PREPARED */
    static const DWORD fl[] = { 0, MHDR_DONE, MHDR_INQUEUE, MHDR_ISSTRM, 0x20, 0x1000, 0x2000, 0x302D };
    for (int i = 0; i < NELEM(fl); i++) {
        MIDIHDR m; moc_hdr(&m, g_buf, 16, fl[i]);
        MMRESULT r = PH(h, &m, sizeof(m));
        CHECKF(r == MMSYSERR_NOERROR, "fl=0x%lx r=%u", fl[i], r);
        CHECKF(m.dwFlags == MHDR_PREPARED, "fl=0x%lx -> 0x%lx", fl[i], m.dwFlags);
        CHECKF(UH(h, &m, sizeof(m)) == MMSYSERR_NOERROR, "unprepare fl=0x%lx", fl[i]);
        CHECKF(m.dwFlags == 0, "unprepare -> 0x%lx", m.dwFlags);
    }
    moc_close(h);
}

TEST(midiout_contract_prepare_live_min_cb_2c) {
    REQ3(); REQUIRE(U(midiOutOpen)); REQUIRE(U(midiOutClose));
    HMIDIOUT h = moc_open_live();
    if (!h) return;
    static const UINT cbs[] = { 0x2C, 0x30, 0x6F, 0x70, 0x100 };
    for (int i = 0; i < NELEM(cbs); i++) {
        MIDIHDR m; moc_hdr(&m, g_buf, 16, 0);
        CHECKF(PH(h, &m, cbs[i]) == MMSYSERR_NOERROR, "prepare cb=0x%x", cbs[i]);
        CHECKF(m.dwFlags == MHDR_PREPARED, "cb=0x%x flags 0x%lx", cbs[i], m.dwFlags);
        CHECKF(UH(h, &m, cbs[i]) == MMSYSERR_NOERROR, "unprepare cb=0x%x", cbs[i]);
        CHECKF(m.dwFlags == 0, "cb=0x%x flags 0x%lx", cbs[i], m.dwFlags);
    }
    moc_close(h);
}

/* PREPARED already set: 0 and nothing changes (other bits kept) */
TEST(midiout_contract_prepare_live_already_prepared_0) {
    REQ3(); REQUIRE(U(midiOutOpen)); REQUIRE(U(midiOutClose));
    HMIDIOUT h = moc_open_live();
    if (!h) return;
    static const DWORD fl[] = { MHDR_PREPARED, MHDR_PREPARED | MHDR_DONE, MHDR_PREPARED | MHDR_INQUEUE, MHDR_PREPARED | 0x3028 };
    for (int i = 0; i < NELEM(fl); i++) {
        MIDIHDR m; moc_hdr(&m, g_buf, 16, fl[i]);
        CHECKF(PH(h, &m, sizeof(m)) == MMSYSERR_NOERROR, "fl=0x%lx", fl[i]);
        CHECKF(m.dwFlags == fl[i], "fl=0x%lx -> 0x%lx", fl[i], m.dwFlags);
    }
    /* a real prepare twice */
    MIDIHDR m; moc_hdr(&m, g_buf, 16, 0);
    EXPECT_EQ(PH(h, &m, sizeof(m)), MMSYSERR_NOERROR);
    EXPECT_EQ(PH(h, &m, sizeof(m)), MMSYSERR_NOERROR);
    EXPECT_EQ(m.dwFlags, MHDR_PREPARED);
    EXPECT_EQ(UH(h, &m, sizeof(m)), MMSYSERR_NOERROR);
    moc_close(h);
}

/* Unprepare: not PREPARED -> 0 untouched (also with INQUEUE); PREPARED|INQUEUE -> 65; twice -> 0 */
TEST(midiout_contract_unprepare_live_ladder) {
    REQ3(); REQUIRE(U(midiOutOpen)); REQUIRE(U(midiOutClose));
    HMIDIOUT h = moc_open_live();
    if (!h) return;
    static const DWORD notprep[] = { 0, MHDR_DONE, MHDR_INQUEUE, MHDR_ISSTRM, 0x302D };
    for (int i = 0; i < NELEM(notprep); i++) {
        MIDIHDR m; moc_hdr(&m, g_buf, 16, notprep[i]);
        CHECKF(UH(h, &m, sizeof(m)) == MMSYSERR_NOERROR, "fl=0x%lx", notprep[i]);
        CHECKF(m.dwFlags == notprep[i], "fl=0x%lx -> 0x%lx", notprep[i], m.dwFlags);
    }
    static const DWORD inq[] = { MHDR_PREPARED | MHDR_INQUEUE, MHDR_PREPARED | MHDR_INQUEUE | MHDR_DONE, 0x302F };
    for (int i = 0; i < NELEM(inq); i++) {
        MIDIHDR m; moc_hdr(&m, g_buf, 16, inq[i]);
        CHECKF(UH(h, &m, sizeof(m)) == MIDIERR_STILLPLAYING, "fl=0x%lx", inq[i]);
        CHECKF(m.dwFlags == inq[i], "fl=0x%lx -> 0x%lx", inq[i], m.dwFlags);
    }
    MIDIHDR m; moc_hdr(&m, g_buf, 16, 0);
    EXPECT_EQ(PH(h, &m, sizeof(m)), MMSYSERR_NOERROR);
    EXPECT_EQ(UH(h, &m, sizeof(m)), MMSYSERR_NOERROR);
    EXPECT_EQ(m.dwFlags, 0);
    EXPECT_EQ(UH(h, &m, sizeof(m)), MMSYSERR_NOERROR);
    EXPECT_EQ(m.dwFlags, 0);
    moc_close(h);
}

/* Unprepare keeps DONE and the other bits, clearing only PREPARED */
TEST(midiout_contract_unprepare_live_keeps_other_bits) {
    REQ3(); REQUIRE(U(midiOutOpen)); REQUIRE(U(midiOutClose));
    HMIDIOUT h = moc_open_live();
    if (!h) return;
    MIDIHDR m; moc_hdr(&m, g_buf, 16, 0);
    EXPECT_EQ(PH(h, &m, sizeof(m)), MMSYSERR_NOERROR);
    m.dwFlags |= MHDR_DONE;
    MMRESULT r = UH(h, &m, sizeof(m));
    EXPECT_EQ(r, MMSYSERR_NOERROR);
    t_note("unprepare of PREPARED|DONE -> flags 0x%lx", m.dwFlags);
    CHECK(!(m.dwFlags & MHDR_PREPARED));
    CHECK(m.dwFlags & MHDR_DONE);
    moc_close(h);
}

/* a prepared header with zero length, or a NULL buffer, prepares fine (the length is a LongMsg check) */
TEST(midiout_contract_prepare_live_buffer_not_checked) {
    REQ3(); REQUIRE(U(midiOutOpen)); REQUIRE(U(midiOutClose));
    HMIDIOUT h = moc_open_live();
    if (!h) return;
    MIDIHDR m; moc_hdr(&m, g_buf, 0, 0);
    MMRESULT r = PH(h, &m, sizeof(m));
    t_note("prepare length 0 -> %u flags 0x%lx", r, m.dwFlags);
    if (r == MMSYSERR_NOERROR) {
        EXPECT_EQ(LM(h, &m, sizeof(m)), MMSYSERR_INVALPARAM);
        EXPECT_EQ(UH(h, &m, sizeof(m)), MMSYSERR_NOERROR);
    }
    moc_close(h);
}

/* LongMsg front checks on a live handle are the same as on a bad one */
TEST(midiout_contract_longmsg_live_front_checks) {
    REQ3(); REQUIRE(U(midiOutOpen)); REQUIRE(U(midiOutClose));
    HMIDIOUT h = moc_open_live();
    if (!h) return;
    MIDIHDR m;
    EXPECT_EQ(LM(h, NULL, sizeof(m)), MMSYSERR_INVALPARAM);
    moc_hdr(&m, g_buf, 16, 0);
    EXPECT_EQ(LM(h, &m, 0x2B), MMSYSERR_INVALPARAM);
    EXPECT_EQ(LM(h, &m, sizeof(m)), MIDIERR_UNPREPARED);
    moc_hdr(&m, g_buf, 16, MHDR_PREPARED | MHDR_INQUEUE);
    EXPECT_EQ(LM(h, &m, sizeof(m)), MIDIERR_STILLPLAYING);
    moc_hdr(&m, g_buf, 0, MHDR_PREPARED);
    EXPECT_EQ(LM(h, &m, sizeof(m)), MMSYSERR_INVALPARAM);
    moc_hdr(&m, g_buf, 16, MHDR_PREPARED | 0x10);
    EXPECT_EQ(LM(h, &m, sizeof(m)), MMSYSERR_INVALPARAM);
    moc_close(h);
}

/* a closed handle: prepare/unprepare/longmsg answer 5 */
TEST(midiout_contract_header_closed_handle_5) {
    REQ3(); REQUIRE(U(midiOutOpen)); REQUIRE(U(midiOutClose));
    HMIDIOUT h = moc_closed_handle();
    if (!h) return;
    MIDIHDR m; moc_hdr(&m, g_buf, 16, 0);
    EXPECT_EQ(PH(h, &m, sizeof(m)), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(m.dwFlags, 0);
    m.dwFlags = MHDR_PREPARED;
    EXPECT_EQ(UH(h, &m, sizeof(m)), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(LM(h, &m, sizeof(m)), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(m.dwFlags, MHDR_PREPARED);
}

/* A header prepared on one handle, unprepared after that handle closed: 5 and still PREPARED */
TEST(midiout_contract_unprepare_after_close_5) {
    REQ3(); REQUIRE(U(midiOutOpen)); REQUIRE(U(midiOutClose));
    HMIDIOUT h = moc_open_live();
    if (!h) return;
    MIDIHDR m; moc_hdr(&m, g_buf, 16, 0);
    EXPECT_EQ(PH(h, &m, sizeof(m)), MMSYSERR_NOERROR);
    moc_close(h);
    EXPECT_EQ(UH(h, &m, sizeof(m)), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(m.dwFlags, MHDR_PREPARED);
}
