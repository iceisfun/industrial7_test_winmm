#include "ee.h"
/* waveOutGetDevCapsA / waveOutGetDevCapsW (winmmbase!0xffa0 / !0xfe00).
 *
 * Validation order [decomp]: cbwoc == 0 -> 0 (nothing written, pointer not looked at); pwoc == NULL -> 11; id 0xFFFF
 * (exactly) -> WAVE_MAPPER; a valid HWAVEOUT goes to waveMessage, anything else to waveIDMessage with the id masked to
 * 32 bits (id < count, or 0xFFFFFFFF = mapper, else 2). The mapper (msacm32.drv) is loaded only when waveOut + waveIn
 * devices exist, so with no wave device even the mapper id is 2.
 * The A form asks the driver for a zeroed 0x84-byte WAVEOUTCAPS2W, converts szPname (Iwcstombs, 32 bytes) and copies
 * min(cbwoc, 100) bytes (sizeof WAVEOUTCAPS2A): short sizes truncate without error, bytes past 100 are untouched.
 * VM_TIMEOUT: 120 */

/* Byte offsets of WAVEOUTCAPS[2]A / [2]W. */
#define A_FORMATS 40
#define A_CHANNELS 44
#define A_SUPPORT 48
#define A_SIZE 52
#define A2_SIZE 100
#define W_FORMATS 72
#define W_CHANNELS 76
#define W_SUPPORT 80
#define W_SIZE 84
#define W2_SIZE 132

static UINT nwo(void) { return U(waveOutGetNumDevs)(); }
static UINT nwave(void) { return U(waveOutGetNumDevs)() + U(waveInGetNumDevs)(); }

TEST(wocaps_struct_sizes) {
    /* The offsets the tests rely on (SDK layout). */
    EXPECT_EQ(sizeof(WAVEOUTCAPSA), A_SIZE);
    EXPECT_EQ(sizeof(WAVEOUTCAPS2A), A2_SIZE);
    EXPECT_EQ(sizeof(WAVEOUTCAPSW), W_SIZE);
    EXPECT_EQ(sizeof(WAVEOUTCAPS2W), W2_SIZE);
    EXPECT_EQ(offsetof(WAVEOUTCAPSA, dwFormats), A_FORMATS);
    EXPECT_EQ(offsetof(WAVEOUTCAPSW, dwFormats), W_FORMATS);
    EXPECT_EQ(offsetof(WAVEOUTCAPSW, dwSupport), W_SUPPORT);
    EXPECT_EQ(offsetof(WAVEOUTCAPS2A, ManufacturerGuid), A_SIZE);
    EXPECT_EQ(offsetof(WAVEOUTCAPS2W, ManufacturerGuid), W_SIZE);
}

/* ---- parameter ladder: cb 0 first, then NULL, then the id ------------------------------------------------- */
static UINT_PTR ladder_ids[16]; static int n_ladder;
static void make_ladder_ids(void) {
    UINT n = nwo(); n_ladder = 0;
    UINT_PTR v[] = { 0, 1, n, n + 1, WAVE_MAPPER, 0xFFFF, 0xFFFE, 0x10000, 0x7FFFFFFF, 0xFFFFFFFE, 0x80000000u, 12345 };
    for (size_t i = 0; i < sizeof v / sizeof v[0]; i++) ladder_ids[n_ladder++] = v[i];
}
TEST(wocaps_w_cb0_null_ok_every_id) {
    REQUIRE(U(waveOutGetDevCapsW)); REQUIRE(U(waveOutGetNumDevs));
    make_ladder_ids();
    for (int i = 0; i < n_ladder; i++)
        CHECKF(U(waveOutGetDevCapsW)(ladder_ids[i], NULL, 0) == MMSYSERR_NOERROR, "id 0x%llx", (unsigned long long)ladder_ids[i]);
}
TEST(wocaps_a_cb0_null_ok_every_id) {
    REQUIRE(U(waveOutGetDevCapsA)); REQUIRE(U(waveOutGetNumDevs));
    make_ladder_ids();
    for (int i = 0; i < n_ladder; i++)
        CHECKF(U(waveOutGetDevCapsA)(ladder_ids[i], NULL, 0) == MMSYSERR_NOERROR, "id 0x%llx", (unsigned long long)ladder_ids[i]);
}
TEST(wocaps_w_cb0_buffer_untouched) {
    REQUIRE(U(waveOutGetDevCapsW)); REQUIRE(U(waveOutGetNumDevs));
    make_ladder_ids();
    for (int i = 0; i < n_ladder; i++) {
        unsigned char b[W2_SIZE + 16]; ee_fill(b, sizeof b, 0xC3);
        CHECKF(U(waveOutGetDevCapsW)(ladder_ids[i], (LPWAVEOUTCAPSW)b, 0) == 0, "id 0x%llx", (unsigned long long)ladder_ids[i]);
        CHECKF(ee_all(b, sizeof b, 0xC3), "id 0x%llx wrote with cb 0", (unsigned long long)ladder_ids[i]);
    }
}
TEST(wocaps_a_cb0_buffer_untouched) {
    REQUIRE(U(waveOutGetDevCapsA)); REQUIRE(U(waveOutGetNumDevs));
    make_ladder_ids();
    for (int i = 0; i < n_ladder; i++) {
        unsigned char b[A2_SIZE + 16]; ee_fill(b, sizeof b, 0xC3);
        CHECKF(U(waveOutGetDevCapsA)(ladder_ids[i], (LPWAVEOUTCAPSA)b, 0) == 0, "id 0x%llx", (unsigned long long)ladder_ids[i]);
        CHECKF(ee_all(b, sizeof b, 0xC3), "id 0x%llx wrote with cb 0", (unsigned long long)ladder_ids[i]);
    }
}
static const UINT ladder_cbs[] = { 1, 2, 4, 8, A_SIZE, W_SIZE, A2_SIZE, W2_SIZE, 200, 0x7FFFFFFF, 0xFFFFFFFF };
TEST(wocaps_w_null_is_invalparam_before_id) {
    REQUIRE(U(waveOutGetDevCapsW)); REQUIRE(U(waveOutGetNumDevs));
    make_ladder_ids();
    for (int i = 0; i < n_ladder; i++)
        for (size_t k = 0; k < sizeof ladder_cbs / sizeof ladder_cbs[0]; k++)
            CHECKF(U(waveOutGetDevCapsW)(ladder_ids[i], NULL, ladder_cbs[k]) == MMSYSERR_INVALPARAM, "id 0x%llx cb %u",
                   (unsigned long long)ladder_ids[i], ladder_cbs[k]);
}
TEST(wocaps_a_null_is_invalparam_before_id) {
    REQUIRE(U(waveOutGetDevCapsA)); REQUIRE(U(waveOutGetNumDevs));
    make_ladder_ids();
    for (int i = 0; i < n_ladder; i++)
        for (size_t k = 0; k < sizeof ladder_cbs / sizeof ladder_cbs[0]; k++)
            CHECKF(U(waveOutGetDevCapsA)(ladder_ids[i], NULL, ladder_cbs[k]) == MMSYSERR_INVALPARAM, "id 0x%llx cb %u",
                   (unsigned long long)ladder_ids[i], ladder_cbs[k]);
}

/* ---- out-of-range ids: 2 and the buffer is not written ---------------------------------------------------- */
static void bad_ids(int wide) {
    UINT n = nwo();
    UINT_PTR v[] = { n, n + 1, n + 2, 0xFFFE, 0x10000, 0x10001, 0x7FFFFFFF, 0x80000000u, 0xFFFFFFFE, 0x0001FFFF, 12345 };
    for (size_t i = 0; i < sizeof v / sizeof v[0]; i++) {
        unsigned char b[W2_SIZE + 16]; ee_fill(b, sizeof b, 0x6B);
        MMRESULT r = wide ? U(waveOutGetDevCapsW)(v[i], (LPWAVEOUTCAPSW)b, W2_SIZE) : U(waveOutGetDevCapsA)(v[i], (LPWAVEOUTCAPSA)b, A2_SIZE);
        CHECKF(r == MMSYSERR_BADDEVICEID, "id 0x%llx -> %u", (unsigned long long)v[i], r);
        CHECKF(ee_all(b, sizeof b, 0x6B), "id 0x%llx wrote the buffer", (unsigned long long)v[i]);
    }
}
TEST(wocaps_w_bad_ids_baddeviceid_untouched) { REQUIRE(U(waveOutGetDevCapsW)); REQUIRE(U(waveOutGetNumDevs)); bad_ids(1); }
TEST(wocaps_a_bad_ids_baddeviceid_untouched) { REQUIRE(U(waveOutGetDevCapsA)); REQUIRE(U(waveOutGetNumDevs)); bad_ids(0); }

/* 64-bit ids: the id path masks uDeviceID to 32 bits [decomp: waveIDMessage(..., uDeviceID & 0xffffffff)]. */
static void high_ids(int wide) {
    UINT n = nwo(), nw = nwave();
    unsigned char ref[W2_SIZE], b[W2_SIZE];
    UINT cb = wide ? W2_SIZE : A2_SIZE;
#define CAPS(id, p) (wide ? U(waveOutGetDevCapsW)((id), (LPWAVEOUTCAPSW)(p), cb) : U(waveOutGetDevCapsA)((id), (LPWAVEOUTCAPSA)(p), cb))
    /* 0x1_00000000 behaves as id 0. */
    memset(ref, 0, sizeof ref); memset(b, 0, sizeof b);
    MMRESULT r0 = CAPS(0, ref), r1 = CAPS((UINT_PTR)0x100000000ull, b);
    EXPECT_EQ(r1, r0);
    EXPECT_EQ(r0, n ? MMSYSERR_NOERROR : MMSYSERR_BADDEVICEID);
    if (!r0 && !r1) CHECK(!memcmp(ref, b, cb));
    /* 0xFFFFFFFF_FFFFFFFF and 0x1_FFFFFFFF behave as WAVE_MAPPER. */
    memset(ref, 0, sizeof ref);
    MMRESULT rm = CAPS(WAVE_MAPPER, ref);
    EXPECT_EQ(rm, nw ? MMSYSERR_NOERROR : MMSYSERR_BADDEVICEID);
    UINT_PTR mv[] = { (UINT_PTR)-1, (UINT_PTR)0x1FFFFFFFFull, (UINT_PTR)0xABCD00000000FFFFull + 0xFFFF0000ull };
    for (size_t i = 0; i < sizeof mv / sizeof mv[0]; i++) {
        memset(b, 0, sizeof b);
        MMRESULT r = CAPS(mv[i], b);
        CHECKF(r == rm, "id 0x%llx -> %u (mapper %u)", (unsigned long long)mv[i], r, rm);
        if (!r && !rm) CHECK(!memcmp(ref, b, cb));
    }
    /* 0x1_0000FFFF masks to 0xFFFF, which is only rewritten to the mapper when it is the whole value. */
    ee_fill(b, sizeof b, 0x77);
    EXPECT_EQ(CAPS((UINT_PTR)0x10000FFFFull, b), MMSYSERR_BADDEVICEID);
    CHECK(ee_all(b, sizeof b, 0x77));
#undef CAPS
}
TEST(wocaps_w_high_dword_of_id_ignored) { REQUIRE(U(waveOutGetDevCapsW)); REQUIRE(U(waveOutGetNumDevs)); high_ids(1); }
TEST(wocaps_a_high_dword_of_id_ignored) { REQUIRE(U(waveOutGetDevCapsA)); REQUIRE(U(waveOutGetNumDevs)); high_ids(0); }

/* ---- the mapper ------------------------------------------------------------------------------------------- */
TEST(wocaps_mapper_present_iff_wave_devices) {
    REQUIRE(U(waveOutGetDevCapsW)); REQUIRE(U(waveOutGetDevCapsA)); REQUIRE(U(waveInGetNumDevs));
    WAVEOUTCAPSW w; WAVEOUTCAPSA a;
    MMRESULT want = nwave() ? MMSYSERR_NOERROR : MMSYSERR_BADDEVICEID;
    EXPECT_EQ(U(waveOutGetDevCapsW)(WAVE_MAPPER, &w, sizeof w), want);
    EXPECT_EQ(U(waveOutGetDevCapsA)(WAVE_MAPPER, &a, sizeof a), want);
    EXPECT_EQ(U(waveOutGetDevCapsW)(0xFFFF, &w, sizeof w), want);
    EXPECT_EQ(U(waveOutGetDevCapsA)(0xFFFF, &a, sizeof a), want);
}
TEST(wocaps_mapper_fields_w) {
    REQUIRE(U(waveOutGetDevCapsW)); REQUIRE(U(waveInGetNumDevs));
    if (!nwave()) { t_skip("no wave device: no mapper"); return; }
    WAVEOUTCAPSW c; memset(&c, 0xEE, sizeof c);
    EXPECT_EQ(U(waveOutGetDevCapsW)(WAVE_MAPPER, &c, sizeof c), MMSYSERR_NOERROR);
    EXPECT_EQ(c.wMid, MM_MICROSOFT);
    EXPECT_EQ(c.wPid, MM_WAVE_MAPPER);
    EXPECT_EQ(c.vDriverVersion, 0x500);
    EXPECT_WSTR(c.szPname, L"Microsoft Sound Mapper");
    EXPECT_EQ(c.dwFormats, 0xFFFFF);
    t_note("mapper W: channels=%u reserved=%u support=0x%lx", c.wChannels, c.wReserved1, c.dwSupport);
}
TEST(wocaps_mapper_fields_a) {
    REQUIRE(U(waveOutGetDevCapsA)); REQUIRE(U(waveInGetNumDevs));
    if (!nwave()) { t_skip("no wave device: no mapper"); return; }
    WAVEOUTCAPSA c; memset(&c, 0xEE, sizeof c);
    EXPECT_EQ(U(waveOutGetDevCapsA)(WAVE_MAPPER, &c, sizeof c), MMSYSERR_NOERROR);
    EXPECT_EQ(c.wMid, MM_MICROSOFT);
    EXPECT_EQ(c.wPid, MM_WAVE_MAPPER);
    EXPECT_EQ(c.vDriverVersion, 0x500);
    EXPECT_STR(c.szPname, "Microsoft Sound Mapper");
    EXPECT_EQ(c.dwFormats, 0xFFFFF);
}
TEST(wocaps_mapper_0xffff_same_bytes) {
    REQUIRE(U(waveOutGetDevCapsW)); REQUIRE(U(waveOutGetDevCapsA)); REQUIRE(U(waveInGetNumDevs));
    if (!nwave()) { t_skip("no wave device"); return; }
    WAVEOUTCAPS2W a, b; WAVEOUTCAPS2A c, d;
    memset(&a, 0, sizeof a); memset(&b, 0, sizeof b); memset(&c, 0, sizeof c); memset(&d, 0, sizeof d);
    EXPECT_EQ(U(waveOutGetDevCapsW)(WAVE_MAPPER, (LPWAVEOUTCAPSW)&a, sizeof a), 0);
    EXPECT_EQ(U(waveOutGetDevCapsW)(0xFFFF, (LPWAVEOUTCAPSW)&b, sizeof b), 0);
    CHECK(!memcmp(&a, &b, sizeof a));
    EXPECT_EQ(U(waveOutGetDevCapsA)(WAVE_MAPPER, (LPWAVEOUTCAPSA)&c, sizeof c), 0);
    EXPECT_EQ(U(waveOutGetDevCapsA)(0xFFFF, (LPWAVEOUTCAPSA)&d, sizeof d), 0);
    CHECK(!memcmp(&c, &d, sizeof c));
}
TEST(wocaps_mapper_caps2_guids_recorded) {
    REQUIRE(U(waveOutGetDevCapsW)); REQUIRE(U(waveInGetNumDevs));
    if (!nwave()) { t_skip("no wave device"); return; }
    WAVEOUTCAPS2W c; memset(&c, 0xEE, sizeof c);
    EXPECT_EQ(U(waveOutGetDevCapsW)(WAVE_MAPPER, (LPWAVEOUTCAPSW)&c, sizeof c), 0);
    t_note("mapper caps2 bytes 84..131 untouched=%d manuf=%08lx prod=%08lx name=%08lx", ee_all((char *)&c + W_SIZE, W2_SIZE - W_SIZE, 0xEE),
           c.ManufacturerGuid.Data1, c.ProductGuid.Data1, c.NameGuid.Data1);
}

/* ---- device caps contents --------------------------------------------------------------------------------- */
TEST(wocaps_device_fields_sane) {
    REQUIRE(U(waveOutGetDevCapsW)); REQUIRE(U(waveOutGetNumDevs));
    UINT n = nwo();
    if (!n) { t_skip("no waveOut device"); return; }
    for (UINT i = 0; i < n; i++) {
        WAVEOUTCAPSW c; memset(&c, 0xEE, sizeof c);
        EXPECT_EQ(U(waveOutGetDevCapsW)(i, &c, sizeof c), MMSYSERR_NOERROR);
        int len = ee_wlen(c.szPname, MAXPNAMELEN);
        CHECKF(len >= 1 && len <= MAXPNAMELEN - 1, "id %u name length %d", i, len);
        CHECKF(c.wChannels >= 1 && c.wChannels <= 8, "id %u channels %u", i, c.wChannels);
        t_note("waveOut %u \"%ls\" mid=%u pid=%u ver=0x%x formats=0x%lx ch=%u res=%u support=0x%lx", i, c.szPname, c.wMid, c.wPid,
               c.vDriverVersion, c.dwFormats, c.wChannels, c.wReserved1, c.dwSupport);
    }
}
TEST(wocaps_device_name_nul_terminated_in_32) {
    REQUIRE(U(waveOutGetDevCapsW)); REQUIRE(U(waveOutGetDevCapsA)); REQUIRE(U(waveOutGetNumDevs));
    UINT n = nwo();
    for (UINT i = 0; i < n; i++) {
        WAVEOUTCAPSW w; WAVEOUTCAPSA a;
        memset(&w, 0xEE, sizeof w); memset(&a, 0xEE, sizeof a);
        EXPECT_EQ(U(waveOutGetDevCapsW)(i, &w, sizeof w), 0);
        EXPECT_EQ(U(waveOutGetDevCapsA)(i, &a, sizeof a), 0);
        CHECKF(ee_wlen(w.szPname, MAXPNAMELEN) < MAXPNAMELEN, "W name of %u unterminated", i);
        CHECKF(ee_alen(a.szPname, MAXPNAMELEN) < MAXPNAMELEN, "A name of %u unterminated", i);
    }
}
TEST(wocaps_a_name_is_converted_w_name) {
    REQUIRE(U(waveOutGetDevCapsW)); REQUIRE(U(waveOutGetDevCapsA)); REQUIRE(U(waveInGetNumDevs));
    UINT n = nwo();
    for (UINT i = 0; i <= n; i++) {
        UINT id = i == n ? WAVE_MAPPER : i;
        if (id == WAVE_MAPPER && !nwave()) break;
        WAVEOUTCAPSW w; WAVEOUTCAPSA a; char conv[64];
        EXPECT_EQ(U(waveOutGetDevCapsW)(id, &w, sizeof w), 0);
        EXPECT_EQ(U(waveOutGetDevCapsA)(id, &a, sizeof a), 0);
        int k = WideCharToMultiByte(CP_ACP, 0, w.szPname, -1, conv, sizeof conv, NULL, NULL);
        CHECK(k > 0);
        if (k > 0 && strlen(conv) < MAXPNAMELEN) EXPECT_STR(a.szPname, conv);
    }
}
TEST(wocaps_a_w_numeric_fields_equal) {
    REQUIRE(U(waveOutGetDevCapsW)); REQUIRE(U(waveOutGetDevCapsA)); REQUIRE(U(waveInGetNumDevs));
    UINT n = nwo();
    for (UINT i = 0; i <= n; i++) {
        UINT id = i == n ? WAVE_MAPPER : i;
        if (id == WAVE_MAPPER && !nwave()) break;
        WAVEOUTCAPS2W w; WAVEOUTCAPS2A a;
        memset(&w, 0, sizeof w); memset(&a, 0, sizeof a);
        EXPECT_EQ(U(waveOutGetDevCapsW)(id, (LPWAVEOUTCAPSW)&w, sizeof w), 0);
        EXPECT_EQ(U(waveOutGetDevCapsA)(id, (LPWAVEOUTCAPSA)&a, sizeof a), 0);
        EXPECT_EQ(a.wMid, w.wMid);
        EXPECT_EQ(a.wPid, w.wPid);
        EXPECT_EQ(a.vDriverVersion, w.vDriverVersion);
        EXPECT_EQ(a.dwFormats, w.dwFormats);
        EXPECT_EQ(a.wChannels, w.wChannels);
        EXPECT_EQ(a.wReserved1, w.wReserved1);
        EXPECT_EQ(a.dwSupport, w.dwSupport);
        CHECK(!memcmp(&a.ManufacturerGuid, &w.ManufacturerGuid, sizeof(GUID)));
        CHECK(!memcmp(&a.ProductGuid, &w.ProductGuid, sizeof(GUID)));
        CHECK(!memcmp(&a.NameGuid, &w.NameGuid, sizeof(GUID)));
    }
}
TEST(wocaps_device_caps2_recorded) {
    REQUIRE(U(waveOutGetDevCapsW)); REQUIRE(U(waveOutGetNumDevs));
    UINT n = nwo();
    for (UINT i = 0; i < n; i++) {
        WAVEOUTCAPS2W c; memset(&c, 0xEE, sizeof c);
        EXPECT_EQ(U(waveOutGetDevCapsW)(i, (LPWAVEOUTCAPSW)&c, sizeof c), 0);
        const GUID *g = &c.NameGuid;
        t_note("waveOut %u caps2 tail untouched=%d manuf=%08lx-%04x prod=%08lx-%04x name=%08lx-%04x", i,
               ee_all((char *)&c + W_SIZE, W2_SIZE - W_SIZE, 0xEE), c.ManufacturerGuid.Data1, c.ManufacturerGuid.Data2,
               c.ProductGuid.Data1, c.ProductGuid.Data2, g->Data1, g->Data2);
    }
}

/* ---- size protocol of the A form [decomp: memcpy(pwoc, caps2a, min(cbwoc, 100))] ---------------------------- */
static void a_size_sweep(UINT id) {
    WAVEOUTCAPS2A full; memset(&full, 0, sizeof full);
    if (U(waveOutGetDevCapsA)(id, (LPWAVEOUTCAPSA)&full, sizeof full) != 0) { CHECKF(0, "full caps of %u failed", id); return; }
    int bad_ret = 0, bad_prefix = 0, bad_tail = 0;
    for (UINT cb = 1; cb <= 160; cb++) {
        unsigned char b[200]; ee_fill(b, sizeof b, 0x9D);
        if (U(waveOutGetDevCapsA)(id, (LPWAVEOUTCAPSA)b, cb) != 0) { bad_ret++; continue; }
        UINT copied = cb < A2_SIZE ? cb : A2_SIZE;
        if (memcmp(b, &full, copied)) bad_prefix++;
        if (!ee_all(b + copied, sizeof b - copied, 0x9D)) bad_tail++;
    }
    CHECKF(bad_ret == 0, "id %u: %d sizes failed", id, bad_ret);
    CHECKF(bad_prefix == 0, "id %u: %d sizes copied a different prefix", id, bad_prefix);
    CHECKF(bad_tail == 0, "id %u: %d sizes wrote past min(cb,100)", id, bad_tail);
}
TEST(wocaps_a_size_sweep_devices) {
    REQUIRE(U(waveOutGetDevCapsA)); REQUIRE(U(waveOutGetNumDevs));
    UINT n = nwo();
    if (!n) { t_skip("no waveOut device"); return; }
    for (UINT i = 0; i < n; i++) a_size_sweep(i);
}
TEST(wocaps_a_size_sweep_mapper) {
    REQUIRE(U(waveOutGetDevCapsA)); REQUIRE(U(waveInGetNumDevs));
    if (!nwave()) { t_skip("no wave device"); return; }
    a_size_sweep(WAVE_MAPPER);
}
TEST(wocaps_a_cb4_writes_ids_only) {
    REQUIRE(U(waveOutGetDevCapsA)); REQUIRE(U(waveOutGetNumDevs));
    if (!nwo()) { t_skip("no waveOut device"); return; }
    WAVEOUTCAPSA full, c;
    EXPECT_EQ(U(waveOutGetDevCapsA)(0, &full, sizeof full), 0);
    memset(&c, 0x41, sizeof c);
    EXPECT_EQ(U(waveOutGetDevCapsA)(0, &c, 4), 0);
    EXPECT_EQ(c.wMid, full.wMid);
    EXPECT_EQ(c.wPid, full.wPid);
    EXPECT_EQ(c.vDriverVersion, 0x41414141);
}
TEST(wocaps_a_classic_size_leaves_guids) {
    REQUIRE(U(waveOutGetDevCapsA)); REQUIRE(U(waveInGetNumDevs));
    if (!nwave()) { t_skip("no wave device"); return; }
    UINT id = nwo() ? 0 : WAVE_MAPPER;
    unsigned char b[A2_SIZE]; ee_fill(b, sizeof b, 0x3C);
    EXPECT_EQ(U(waveOutGetDevCapsA)(id, (LPWAVEOUTCAPSA)b, A_SIZE), 0);
    CHECK(ee_all(b + A_SIZE, A2_SIZE - A_SIZE, 0x3C));
    CHECK(b[EE_PNAME_OFF] != 0x3C);
}
TEST(wocaps_a_name_truncated_size) {
    REQUIRE(U(waveOutGetDevCapsA)); REQUIRE(U(waveInGetNumDevs));
    if (!nwave()) { t_skip("no wave device"); return; }
    UINT id = nwo() ? 0 : WAVE_MAPPER;
    WAVEOUTCAPSA full; EXPECT_EQ(U(waveOutGetDevCapsA)(id, &full, sizeof full), 0);
    /* cb ending inside szPname: a raw byte prefix, not a re-terminated string. */
    unsigned char b[A2_SIZE]; ee_fill(b, sizeof b, 0x5F);
    EXPECT_EQ(U(waveOutGetDevCapsA)(id, (LPWAVEOUTCAPSA)b, EE_PNAME_OFF + 5), 0);
    CHECK(!memcmp(b + EE_PNAME_OFF, full.szPname, 5));
    EXPECT_EQ(b[EE_PNAME_OFF + 5], 0x5F);
}

/* ---- size protocol of the W form (the driver copies: wdmaud for devices, msacm32 for the mapper) ------------ */
static size_t written_len(UINT_PTR id, UINT cb) {
    unsigned char b1[300], b2[300];
    ee_fill(b1, sizeof b1, 0x00); ee_fill(b2, sizeof b2, 0xFF);
    U(waveOutGetDevCapsW)(id, (LPWAVEOUTCAPSW)b1, cb);
    U(waveOutGetDevCapsW)(id, (LPWAVEOUTCAPSW)b2, cb);
    size_t a = ee_untouched_from(b1, sizeof b1, 0x00), b = ee_untouched_from(b2, sizeof b2, 0xFF);
    return a > b ? a : b;
}
/* [VM hda] wdmaud (devices): cbwoc < sizeof(WAVEOUTCAPSW) -> 11; otherwise 0, the first 84 bytes are WAVEOUTCAPSW, the
 * WAVEOUTCAPS2W guids follow only when cbwoc >= 132, and every byte up to cbwoc is written (the rest is zeroed).
 * msacm32 (the mapper): cbwoc < 84 -> 11 with nothing written; otherwise exactly the 84-byte WAVEOUTCAPSW, nothing
 * after it (the mapper has no CAPS2 part). The A form hides all of this: it always asks for the full 0x84 bytes. */
static const UINT w_cbs[] = { 1, 2, 3, 4, 6, 8, 9, 40, 71, 72, 76, 80, 83, 84, 85, 99, 100, 120, 131, 132, 133, 150, 200, 260 };
static void w_size_record(UINT_PTR id, const char *what) {
    char rec[600]; int rl = 0;
    for (size_t k = 0; k < sizeof w_cbs / sizeof w_cbs[0]; k++) {
        MMRESULT r; unsigned char b[300]; ee_fill(b, sizeof b, 0xB4);
        r = U(waveOutGetDevCapsW)(id, (LPWAVEOUTCAPSW)b, w_cbs[k]);
        rl += snprintf(rec + rl, sizeof rec - rl, " %u:%u/%u", w_cbs[k], r, (unsigned)written_len(id, w_cbs[k]));
    }
    t_note("%s W sizes (cb:ret/written):%s", what, rec);
}
TEST(wocaps_w_size_protocol_devices) {
    REQUIRE(U(waveOutGetDevCapsW)); REQUIRE(U(waveOutGetNumDevs));
    UINT n = nwo();
    if (!n) { t_skip("no waveOut device"); return; }
    for (UINT i = 0; i < n; i++) {
        WAVEOUTCAPS2W full; memset(&full, 0, sizeof full);
        EXPECT_EQ(U(waveOutGetDevCapsW)(i, (LPWAVEOUTCAPSW)&full, sizeof full), 0);
        char w[32]; snprintf(w, sizeof w, "id %u", i); w_size_record(i, w);
        for (size_t k = 0; k < sizeof w_cbs / sizeof w_cbs[0]; k++) {
            UINT cb = w_cbs[k];
            unsigned char b[300]; ee_fill(b, sizeof b, 0xB4);
            MMRESULT r = U(waveOutGetDevCapsW)(i, (LPWAVEOUTCAPSW)b, cb);
            if (cb < W_SIZE) { CHECKF(r == MMSYSERR_INVALPARAM, "id %u cb %u -> %u", i, cb, r); continue; }
            CHECKF(r == 0, "id %u cb %u -> %u", i, cb, r);
            if (r) continue;
            CHECKF(!memcmp(b, &full, W_SIZE), "id %u cb %u classic part differs", i, cb);
            if (cb >= W2_SIZE) {
                CHECKF(!memcmp(b + W_SIZE, (char *)&full + W_SIZE, W2_SIZE - W_SIZE), "id %u cb %u guids differ", i, cb);
                CHECKF(ee_all(b + W2_SIZE, cb - W2_SIZE, 0), "id %u cb %u: bytes 132..cb not zero", i, cb);
            } else {
                CHECKF(ee_all(b + W_SIZE, cb - W_SIZE, 0), "id %u cb %u: bytes 84..cb not zero", i, cb);
            }
            CHECKF(ee_all(b + cb, sizeof b - cb, 0xB4), "id %u cb %u wrote past cb", i, cb);
        }
    }
}
TEST(wocaps_w_size_protocol_mapper) {
    REQUIRE(U(waveOutGetDevCapsW)); REQUIRE(U(waveInGetNumDevs));
    if (!nwave()) { t_skip("no wave device"); return; }
    WAVEOUTCAPSW full; memset(&full, 0, sizeof full);
    EXPECT_EQ(U(waveOutGetDevCapsW)(WAVE_MAPPER, &full, sizeof full), 0);
    w_size_record(WAVE_MAPPER, "mapper");
    for (size_t k = 0; k < sizeof w_cbs / sizeof w_cbs[0]; k++) {
        UINT cb = w_cbs[k];
        unsigned char b[300]; ee_fill(b, sizeof b, 0xB4);
        MMRESULT r = U(waveOutGetDevCapsW)(WAVE_MAPPER, (LPWAVEOUTCAPSW)b, cb);
        if (cb < W_SIZE) {
            CHECKF(r == MMSYSERR_INVALPARAM, "mapper cb %u -> %u", cb, r);
            CHECKF(ee_all(b, sizeof b, 0xB4), "mapper cb %u wrote on failure", cb);
            continue;
        }
        CHECKF(r == 0, "mapper cb %u -> %u", cb, r);
        CHECKF(!memcmp(b, &full, W_SIZE), "mapper cb %u differs", cb);
        CHECKF(ee_all(b + W_SIZE, sizeof b - W_SIZE, 0xB4), "mapper cb %u wrote past 84", cb);
    }
}
/* The A form succeeds for every size where the W form refuses (it never passes the caller's size down). */
TEST(wocaps_a_succeeds_where_w_refuses) {
    REQUIRE(U(waveOutGetDevCapsW)); REQUIRE(U(waveOutGetDevCapsA)); REQUIRE(U(waveInGetNumDevs));
    if (!nwave()) { t_skip("no wave device"); return; }
    UINT_PTR id = nwo() ? 0 : WAVE_MAPPER;
    unsigned char b[300];
    for (UINT cb = 1; cb < W_SIZE; cb += 7) {
        EXPECT_EQ(U(waveOutGetDevCapsW)(id, (LPWAVEOUTCAPSW)b, cb), MMSYSERR_INVALPARAM);
        EXPECT_EQ(U(waveOutGetDevCapsA)(id, (LPWAVEOUTCAPSA)b, cb), MMSYSERR_NOERROR);
    }
}
TEST(wocaps_w_classic_size_no_overrun) {
    REQUIRE(U(waveOutGetDevCapsW)); REQUIRE(U(waveInGetNumDevs));
    if (!nwave()) { t_skip("no wave device"); return; }
    UINT_PTR ids[2] = { WAVE_MAPPER, 0 };
    for (int k = 0; k < (nwo() ? 2 : 1); k++) {
        unsigned char b[W2_SIZE + 8]; ee_fill(b, sizeof b, 0xD2);
        EXPECT_EQ(U(waveOutGetDevCapsW)(ids[k], (LPWAVEOUTCAPSW)b, W_SIZE), 0);
        CHECKF(ee_all(b + W_SIZE, sizeof b - W_SIZE, 0xD2), "id 0x%llx wrote past sizeof(WAVEOUTCAPSW)", (unsigned long long)ids[k]);
        CHECK(((WCHAR *)(b + EE_PNAME_OFF))[0] != 0);
    }
}

/* ---- stability and threads -------------------------------------------------------------------------------- */
TEST(wocaps_repeat_identical) {
    REQUIRE(U(waveOutGetDevCapsW)); REQUIRE(U(waveOutGetDevCapsA)); REQUIRE(U(waveInGetNumDevs));
    UINT n = nwo();
    for (UINT i = 0; i <= n; i++) {
        UINT id = i == n ? WAVE_MAPPER : i;
        if (id == WAVE_MAPPER && !nwave()) break;
        WAVEOUTCAPS2W r, c; WAVEOUTCAPS2A ra, ca; int bad = 0;
        memset(&r, 0, sizeof r); memset(&ra, 0, sizeof ra);
        EXPECT_EQ(U(waveOutGetDevCapsW)(id, (LPWAVEOUTCAPSW)&r, sizeof r), 0);
        EXPECT_EQ(U(waveOutGetDevCapsA)(id, (LPWAVEOUTCAPSA)&ra, sizeof ra), 0);
        for (int k = 0; k < 50; k++) {
            memset(&c, 0, sizeof c); memset(&ca, 0, sizeof ca);
            if (U(waveOutGetDevCapsW)(id, (LPWAVEOUTCAPSW)&c, sizeof c) || memcmp(&c, &r, sizeof c)) bad++;
            if (U(waveOutGetDevCapsA)(id, (LPWAVEOUTCAPSA)&ca, sizeof ca) || memcmp(&ca, &ra, sizeof ca)) bad++;
        }
        CHECKF(bad == 0, "id 0x%x: %d differing results", id, bad);
    }
}
typedef struct { HANDLE go; UINT id; WAVEOUTCAPS2W ref; volatile LONG bad; } caps_thr;
static DWORD WINAPI caps_thread(LPVOID p) {
    caps_thr *c = (caps_thr *)p;
    WaitForSingleObject(c->go, 10000);
    for (int i = 0; i < 100; i++) {
        WAVEOUTCAPS2W x; memset(&x, 0, sizeof x);
        if (ee_caps(F_WAVEOUT, 1, c->id, &x, sizeof x) || memcmp(&x, &c->ref, sizeof x)) InterlockedIncrement(&c->bad);
        WAVEOUTCAPSA a;
        if (ee_caps(F_WAVEOUT, 0, c->id, &a, sizeof a)) InterlockedIncrement(&c->bad);
    }
    return 0;
}
TEST(wocaps_4_threads_concurrent) {
    REQUIRE(U(waveOutGetDevCapsW)); REQUIRE(U(waveInGetNumDevs));
    if (!nwave()) { t_skip("no wave device"); return; }
    caps_thr c; memset(&c, 0, sizeof c);
    c.id = nwo() ? 0 : WAVE_MAPPER;
    EXPECT_EQ(U(waveOutGetDevCapsW)(c.id, (LPWAVEOUTCAPSW)&c.ref, sizeof c.ref), 0);
    c.go = CreateEventA(NULL, TRUE, FALSE, NULL);
    HANDLE th[4];
    for (int i = 0; i < 4; i++) th[i] = CreateThread(NULL, 0, caps_thread, &c, 0, NULL);
    SetEvent(c.go);
    EXPECT_EQ(WaitForMultipleObjects(4, th, TRUE, 30000), WAIT_OBJECT_0);
    for (int i = 0; i < 4; i++) CloseHandle(th[i]);
    CloseHandle(c.go);
    EXPECT_EQ(c.bad, 0);
}

/* ---- handles in place of ids ------------------------------------------------------------------------------ */
static HWAVEOUT open_out(UINT id) {
    WAVEFORMATEX wf = { WAVE_FORMAT_PCM, 2, 44100, 44100 * 4, 4, 16, 0 };
    HWAVEOUT h = NULL;
    return U(waveOutOpen)(&h, id, &wf, 0, 0, CALLBACK_NULL) == MMSYSERR_NOERROR ? h : NULL;
}
TEST(wocaps_by_device_handle_equals_by_id) {
    REQUIRE(U(waveOutGetDevCapsW)); REQUIRE(U(waveOutGetDevCapsA)); REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutClose));
    UINT n = nwo();
    if (!n) { t_skip("no waveOut device"); return; }
    for (UINT i = 0; i < n; i++) {
        HWAVEOUT h = open_out(i);
        CHECKF(h != NULL, "open %u", i);
        if (!h) continue;
        WAVEOUTCAPS2W a, b; WAVEOUTCAPSA c, d;
        memset(&a, 0, sizeof a); memset(&b, 0, sizeof b); memset(&c, 0, sizeof c); memset(&d, 0, sizeof d);
        EXPECT_EQ(U(waveOutGetDevCapsW)(i, (LPWAVEOUTCAPSW)&a, sizeof a), 0);
        EXPECT_EQ(U(waveOutGetDevCapsW)((UINT_PTR)h, (LPWAVEOUTCAPSW)&b, sizeof b), 0);
        CHECK(!memcmp(&a, &b, W_SIZE));
        EXPECT_WSTR(a.szPname, b.szPname);
        EXPECT_EQ(U(waveOutGetDevCapsA)(i, &c, sizeof c), 0);
        EXPECT_EQ(U(waveOutGetDevCapsA)((UINT_PTR)h, &d, sizeof d), 0);
        CHECK(!memcmp(&c, &d, sizeof c));
        /* cb 0 and NULL keep their order with a handle too. */
        EXPECT_EQ(U(waveOutGetDevCapsW)((UINT_PTR)h, NULL, 0), 0);
        EXPECT_EQ(U(waveOutGetDevCapsW)((UINT_PTR)h, NULL, sizeof a), MMSYSERR_INVALPARAM);
        EXPECT_EQ(U(waveOutGetDevCapsA)((UINT_PTR)h, NULL, sizeof c), MMSYSERR_INVALPARAM);
        EXPECT_EQ(U(waveOutClose)(h), 0);
    }
}
TEST(wocaps_by_mapper_handle) {
    REQUIRE(U(waveOutGetDevCapsW)); REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutClose));
    if (!nwo()) { t_skip("no waveOut device"); return; }
    HWAVEOUT h = open_out(WAVE_MAPPER);
    CHECK(h != NULL);
    if (!h) return;
    WAVEOUTCAPSW m, d, x;
    EXPECT_EQ(U(waveOutGetDevCapsW)(WAVE_MAPPER, &m, sizeof m), 0);
    EXPECT_EQ(U(waveOutGetDevCapsW)(0, &d, sizeof d), 0);
    MMRESULT r = U(waveOutGetDevCapsW)((UINT_PTR)h, &x, sizeof x);
    EXPECT_EQ(r, 0);
    if (!r) t_note("caps by mapper handle: \"%ls\" mid=%u pid=%u (mapper \"%ls\", device 0 \"%ls\")", x.szPname, x.wMid, x.wPid, m.szPname, d.szPname);
    EXPECT_EQ(U(waveOutClose)(h), 0);
}
TEST(wocaps_closed_handle_is_baddeviceid) {
    REQUIRE(U(waveOutGetDevCapsW)); REQUIRE(U(waveOutGetDevCapsA)); REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutClose));
    if (!nwo()) { t_skip("no waveOut device"); return; }
    HWAVEOUT h = open_out(0);
    CHECK(h != NULL);
    if (!h) return;
    EXPECT_EQ(U(waveOutClose)(h), 0);
    /* Not on the handle list any more: treated as a (huge) device id. */
    WAVEOUTCAPSW w; WAVEOUTCAPSA a;
    EXPECT_EQ(U(waveOutGetDevCapsW)((UINT_PTR)h, &w, sizeof w), MMSYSERR_BADDEVICEID);
    EXPECT_EQ(U(waveOutGetDevCapsA)((UINT_PTR)h, &a, sizeof a), MMSYSERR_BADDEVICEID);
}
TEST(wocaps_wavein_handle_is_not_waveout) {
    REQUIRE(U(waveOutGetDevCapsW)); REQUIRE(U(waveInOpen)); REQUIRE(U(waveInClose)); REQUIRE(U(waveInGetNumDevs));
    if (!U(waveInGetNumDevs)()) { t_skip("no waveIn device"); return; }
    WAVEFORMATEX wf = { WAVE_FORMAT_PCM, 1, 44100, 44100 * 2, 2, 16, 0 };
    HWAVEIN h = NULL;
    MMRESULT r = U(waveInOpen)(&h, 0, &wf, 0, 0, CALLBACK_NULL);
    EXPECT_EQ(r, 0);
    if (r) return;
    WAVEOUTCAPSW w; WAVEOUTCAPSA a;
    /* ValidateHandle(type waveOut) fails for an HWAVEIN: id path, out of range. */
    EXPECT_EQ(U(waveOutGetDevCapsW)((UINT_PTR)h, &w, sizeof w), MMSYSERR_BADDEVICEID);
    EXPECT_EQ(U(waveOutGetDevCapsA)((UINT_PTR)h, &a, sizeof a), MMSYSERR_BADDEVICEID);
    EXPECT_EQ(U(waveInClose)(h), 0);
}
TEST(wocaps_unchanged_while_open) {
    REQUIRE(U(waveOutGetDevCapsW)); REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutClose));
    if (!nwo()) { t_skip("no waveOut device"); return; }
    WAVEOUTCAPS2W a, b, c;
    memset(&a, 0, sizeof a); memset(&b, 0, sizeof b); memset(&c, 0, sizeof c);
    EXPECT_EQ(U(waveOutGetDevCapsW)(0, (LPWAVEOUTCAPSW)&a, sizeof a), 0);
    HWAVEOUT h = open_out(0);
    CHECK(h != NULL);
    EXPECT_EQ(U(waveOutGetDevCapsW)(0, (LPWAVEOUTCAPSW)&b, sizeof b), 0);
    CHECK(!memcmp(&a, &b, sizeof a));
    if (h) EXPECT_EQ(U(waveOutClose)(h), 0);
    EXPECT_EQ(U(waveOutGetDevCapsW)(0, (LPWAVEOUTCAPSW)&c, sizeof c), 0);
    CHECK(!memcmp(&a, &c, sizeof a));
}

/* ---- no-device contract ----------------------------------------------------------------------------------- */
TEST(wocaps_no_device_contract) {
    REQUIRE(U(waveOutGetDevCapsW)); REQUIRE(U(waveOutGetDevCapsA)); REQUIRE(U(waveInGetNumDevs));
    if (nwo()) { t_skip("waveOut devices present"); return; }
    UINT_PTR ids[] = { 0, 1, WAVE_MAPPER, 0xFFFF };
    for (size_t i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        WAVEOUTCAPSW w; WAVEOUTCAPSA a;
        MMRESULT want = (ids[i] == WAVE_MAPPER || ids[i] == 0xFFFF) && nwave() ? 0 : MMSYSERR_BADDEVICEID;
        CHECKF(U(waveOutGetDevCapsW)(ids[i], &w, sizeof w) == want, "W id 0x%llx", (unsigned long long)ids[i]);
        CHECKF(U(waveOutGetDevCapsA)(ids[i], &a, sizeof a) == want, "A id 0x%llx", (unsigned long long)ids[i]);
        EXPECT_EQ(U(waveOutGetDevCapsW)(ids[i], NULL, sizeof w), MMSYSERR_INVALPARAM);
        EXPECT_EQ(U(waveOutGetDevCapsW)(ids[i], NULL, 0), 0);
    }
}

/* waveOutGetDevCaps goes through the same path as waveOutMessage(id, WODM_GETDEVCAPS): the device's driver gets
 * the call. The message form with a device id: same bytes for the mapper (no device-interface wrapping). */
TEST(wocaps_mapper_equals_wodm_getdevcaps_message) {
    REQUIRE(U(waveOutGetDevCapsW)); REQUIRE(U(waveOutMessage)); REQUIRE(U(waveInGetNumDevs));
    if (!nwave()) {
        WAVEOUTCAPSW m;
        EXPECT_EQ(U(waveOutMessage)((HWAVEOUT)(UINT_PTR)WAVE_MAPPER, WODM_GETDEVCAPS, (DWORD_PTR)&m, sizeof m), MMSYSERR_BADDEVICEID);
        return;
    }
    WAVEOUTCAPSW a, b; memset(&a, 0, sizeof a); memset(&b, 0, sizeof b);
    EXPECT_EQ(U(waveOutGetDevCapsW)(WAVE_MAPPER, &a, sizeof a), 0);
    EXPECT_EQ(U(waveOutMessage)((HWAVEOUT)(UINT_PTR)WAVE_MAPPER, WODM_GETDEVCAPS, (DWORD_PTR)&b, sizeof b), 0);
    CHECK(!memcmp(&a, &b, sizeof a));
}
