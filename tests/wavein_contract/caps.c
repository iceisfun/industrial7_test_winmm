/* waveInGetDevCapsW (winmmbase!0x11000) / waveInGetDevCapsA (!0x110f0).
 *
 * Order (decomp): cbwic == 0 -> 0 (nothing written, pointer and id not checked); pwic NULL -> 11; then a
 * valid HWAVEIN is accepted in place of an id, else waveIDMessage(id & 0xFFFFFFFF): id < count or
 * id == 0xFFFFFFFF (mapper, 2 when there is no waveIn driver at all) else 2. Unlike waveOutGetDevCaps there is
 * no 0xFFFF -> mapper rewrite. A asks the driver for a full WAVEINCAPS2W into a zeroed local, converts the
 * name and copies min(cbwic, 0x60 = sizeof(WAVEINCAPS2A)) bytes.
 */
#include "wic.h"

#define FILLB 0xA5

static int all_bytes(const void *p, size_t n, unsigned char v) {
    const unsigned char *b = p;
    for (size_t i = 0; i < n; i++) if (b[i] != v) return 0;
    return 1;
}

/* ---------------------------------------------------------------- cb == 0 */

static const UINT_PTR cap_ids[] = { WAVE_MAPPER, 0, 1, 0xFFFF, 9999, (UINT_PTR)-1, 0xFFFFFFFE };

TEST(wavein_contract_caps_w_cb0_null_ok) {
    REQUIRE(U(waveInGetDevCapsW));
    for (unsigned i = 0; i < sizeof(cap_ids) / sizeof(cap_ids[0]); i++)
        CHECKF(U(waveInGetDevCapsW)(cap_ids[i], NULL, 0) == MMSYSERR_NOERROR, "id=0x%llx", (unsigned long long)cap_ids[i]);
}
TEST(wavein_contract_caps_a_cb0_null_ok) {
    REQUIRE(U(waveInGetDevCapsA));
    for (unsigned i = 0; i < sizeof(cap_ids) / sizeof(cap_ids[0]); i++)
        CHECKF(U(waveInGetDevCapsA)(cap_ids[i], NULL, 0) == MMSYSERR_NOERROR, "id=0x%llx", (unsigned long long)cap_ids[i]);
}
TEST(wavein_contract_caps_w_cb0_untouched) {
    REQUIRE(U(waveInGetDevCapsW));
    WAVEINCAPSW c; memset(&c, FILLB, sizeof(c));
    EXPECT_EQ(U(waveInGetDevCapsW)(WAVE_MAPPER, &c, 0), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveInGetDevCapsW)(0, &c, 0), MMSYSERR_NOERROR);
    CHECK(all_bytes(&c, sizeof(c), FILLB));
}
TEST(wavein_contract_caps_a_cb0_untouched) {
    REQUIRE(U(waveInGetDevCapsA));
    WAVEINCAPSA c; memset(&c, FILLB, sizeof(c));
    EXPECT_EQ(U(waveInGetDevCapsA)(WAVE_MAPPER, &c, 0), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveInGetDevCapsA)(0, &c, 0), MMSYSERR_NOERROR);
    CHECK(all_bytes(&c, sizeof(c), FILLB));
}

/* ---------------------------------------------------------------- NULL -> 11 before the id */

TEST(wavein_contract_caps_w_null_11) {
    REQUIRE(U(waveInGetDevCapsW));
    static const UINT cbs[] = { 1, 4, sizeof(WAVEINCAPSW), sizeof(WAVEINCAPS2W), 0x1000 };
    for (unsigned i = 0; i < sizeof(cap_ids) / sizeof(cap_ids[0]); i++)
        for (unsigned j = 0; j < 5; j++)
            CHECKF(U(waveInGetDevCapsW)(cap_ids[i], NULL, cbs[j]) == MMSYSERR_INVALPARAM, "id=0x%llx cb=%u", (unsigned long long)cap_ids[i], cbs[j]);
}
TEST(wavein_contract_caps_a_null_11) {
    REQUIRE(U(waveInGetDevCapsA));
    static const UINT cbs[] = { 1, 4, sizeof(WAVEINCAPSA), sizeof(WAVEINCAPS2A), 0x1000 };
    for (unsigned i = 0; i < sizeof(cap_ids) / sizeof(cap_ids[0]); i++)
        for (unsigned j = 0; j < 5; j++)
            CHECKF(U(waveInGetDevCapsA)(cap_ids[i], NULL, cbs[j]) == MMSYSERR_INVALPARAM, "id=0x%llx cb=%u", (unsigned long long)cap_ids[i], cbs[j]);
}

/* ---------------------------------------------------------------- bad ids -> 2 */

static void bad_ids(UINT_PTR *out, int *n) {
    UINT nin = wic_nin(); int k = 0;
    out[k++] = nin; out[k++] = nin + 1; out[k++] = 9999; out[k++] = 0xFFFE;
    out[k++] = 0xFFFF; /* no 0xFFFF -> mapper rewrite in waveInGetDevCaps */
    out[k++] = 0x10000; out[k++] = 0xFFFFFFFE; out[k++] = 0x7FFFFFFF;
    *n = k;
}

TEST(wavein_contract_caps_w_bad_id_2) {
    REQUIRE(U(waveInGetDevCapsW));
    UINT_PTR ids[16]; int n; bad_ids(ids, &n);
    for (int i = 0; i < n; i++) {
        WAVEINCAPSW c; memset(&c, FILLB, sizeof(c));
        MMRESULT r = U(waveInGetDevCapsW)(ids[i], &c, sizeof(c));
        CHECKF(r == MMSYSERR_BADDEVICEID, "id=0x%llx r=%u", (unsigned long long)ids[i], r);
        CHECKF(all_bytes(&c, sizeof(c), FILLB), "id=0x%llx buffer written", (unsigned long long)ids[i]);
    }
}
TEST(wavein_contract_caps_a_bad_id_2) {
    REQUIRE(U(waveInGetDevCapsA));
    UINT_PTR ids[16]; int n; bad_ids(ids, &n);
    for (int i = 0; i < n; i++) {
        WAVEINCAPSA c; memset(&c, FILLB, sizeof(c));
        MMRESULT r = U(waveInGetDevCapsA)(ids[i], &c, sizeof(c));
        CHECKF(r == MMSYSERR_BADDEVICEID, "id=0x%llx r=%u", (unsigned long long)ids[i], r);
        CHECKF(all_bytes(&c, sizeof(c), FILLB), "id=0x%llx buffer written", (unsigned long long)ids[i]);
    }
}

/* The id is truncated to 32 bits: 0x1_0000_0000 + n behaves like n (n >= count -> 2 always). */
TEST(wavein_contract_caps_id_truncated_to_32_bits) {
    REQUIRE(U(waveInGetDevCapsW));
    WAVEINCAPSW a, b;
    MMRESULT r1 = U(waveInGetDevCapsW)((UINT_PTR)0x100000000ull + 9999, &a, sizeof(a));
    EXPECT_EQ(r1, MMSYSERR_BADDEVICEID);
    MMRESULT r0 = U(waveInGetDevCapsW)((UINT_PTR)0x100000000ull, &a, sizeof(a));
    if (!wic_nin()) { EXPECT_EQ(r0, MMSYSERR_BADDEVICEID); return; }
    EXPECT_EQ(r0, MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveInGetDevCapsW)(0, &b, sizeof(b)), MMSYSERR_NOERROR);
    CHECK(!memcmp(a.szPname, b.szPname, sizeof(a.szPname)));
    MMRESULT rm = U(waveInGetDevCapsW)((UINT_PTR)0xFFFFFFFF00000000ull | WAVE_MAPPER, &a, sizeof(a));
    EXPECT_EQ(rm, MMSYSERR_NOERROR);
}

/* ---------------------------------------------------------------- zero devices */

TEST(wavein_contract_caps_zero_devices_every_id_2) {
    REQUIRE(U(waveInGetDevCapsW)); REQUIRE(U(waveInGetDevCapsA));
    if (wic_nin()) { t_skip("waveIn devices present"); return; }
    WAVEINCAPSW w; WAVEINCAPSA a;
    MMRESULT rmw = U(waveInGetDevCapsW)(WAVE_MAPPER, &w, sizeof(w));
    MMRESULT rma = U(waveInGetDevCapsA)(WAVE_MAPPER, &a, sizeof(a));
    if (wic_nout()) {
        /* an output-only guest still has a mapper entry in the waveIn list */
        t_note("mapper caps with 0 waveIn / %u waveOut: W=%u A=%u", wic_nout(), rmw, rma);
        CHECK(rmw == rma);
    } else {
        EXPECT_EQ(rmw, MMSYSERR_BADDEVICEID);
        EXPECT_EQ(rma, MMSYSERR_BADDEVICEID);
    }
    EXPECT_EQ(U(waveInGetDevCapsW)(0, &w, sizeof(w)), MMSYSERR_BADDEVICEID);
    EXPECT_EQ(U(waveInGetDevCapsA)(0, &a, sizeof(a)), MMSYSERR_BADDEVICEID);
    EXPECT_EQ(U(waveInGetDevCapsW)(0, &w, 4), MMSYSERR_BADDEVICEID);
    EXPECT_EQ(U(waveInGetDevCapsA)(0, &a, 4), MMSYSERR_BADDEVICEID);
}

/* ---------------------------------------------------------------- device half */

static int have_dev(void) { if (!wic_nin()) { t_skip("no waveIn device"); return 0; } return 1; }

TEST(wavein_contract_caps_dev_each_id_w) {
    REQUIRE(U(waveInGetDevCapsW));
    if (!have_dev()) return;
    for (UINT i = 0; i < wic_nin() && i < 8; i++) {
        WAVEINCAPSW c; memset(&c, FILLB, sizeof(c));
        EXPECT_EQ(U(waveInGetDevCapsW)(i, &c, sizeof(c)), MMSYSERR_NOERROR);
        char nm[64]; WideCharToMultiByte(CP_UTF8, 0, c.szPname, -1, nm, sizeof(nm), NULL, NULL);
        t_note("waveIn %u \"%s\" mid=%u pid=%u ver=0x%x fmts=0x%lx ch=%u", i, nm, c.wMid, c.wPid, c.vDriverVersion, c.dwFormats, c.wChannels);
        CHECK(c.szPname[0] != 0);
        CHECK(wcsnlen(c.szPname, MAXPNAMELEN) < MAXPNAMELEN);
        CHECK(c.wChannels >= 1);
    }
}

TEST(wavein_contract_caps_dev_mapper_w) {
    REQUIRE(U(waveInGetDevCapsW));
    if (!have_dev()) return;
    WAVEINCAPSW c; memset(&c, FILLB, sizeof(c));
    EXPECT_EQ(U(waveInGetDevCapsW)(WAVE_MAPPER, &c, sizeof(c)), MMSYSERR_NOERROR);
    char nm[64]; WideCharToMultiByte(CP_UTF8, 0, c.szPname, -1, nm, sizeof(nm), NULL, NULL);
    t_note("waveIn mapper \"%s\" mid=%u pid=%u ver=0x%x fmts=0x%lx ch=%u", nm, c.wMid, c.wPid, c.vDriverVersion, c.dwFormats, c.wChannels);
    CHECK(c.szPname[0] != 0);
    if (!t_is_wine()) EXPECT_WSTR(c.szPname, L"Microsoft Sound Mapper");
}

TEST(wavein_contract_caps_dev_mapper_a) {
    REQUIRE(U(waveInGetDevCapsA));
    if (!have_dev()) return;
    WAVEINCAPSA c; memset(&c, FILLB, sizeof(c));
    EXPECT_EQ(U(waveInGetDevCapsA)(WAVE_MAPPER, &c, sizeof(c)), MMSYSERR_NOERROR);
    if (!t_is_wine()) EXPECT_STR(c.szPname, "Microsoft Sound Mapper");
}

/* A and W agree on every numeric field and the converted name. */
TEST(wavein_contract_caps_dev_a_matches_w) {
    REQUIRE(U(waveInGetDevCapsA)); REQUIRE(U(waveInGetDevCapsW));
    if (!have_dev()) return;
    UINT_PTR ids[9]; int n = 0;
    ids[n++] = WAVE_MAPPER;
    for (UINT i = 0; i < wic_nin() && n < 9; i++) ids[n++] = i;
    for (int i = 0; i < n; i++) {
        WAVEINCAPSW w; WAVEINCAPSA a;
        EXPECT_EQ(U(waveInGetDevCapsW)(ids[i], &w, sizeof(w)), MMSYSERR_NOERROR);
        EXPECT_EQ(U(waveInGetDevCapsA)(ids[i], &a, sizeof(a)), MMSYSERR_NOERROR);
        CHECK(a.wMid == w.wMid); CHECK(a.wPid == w.wPid); CHECK(a.vDriverVersion == w.vDriverVersion);
        CHECK(a.dwFormats == w.dwFormats); CHECK(a.wChannels == w.wChannels);
        char conv[64]; WideCharToMultiByte(CP_ACP, 0, w.szPname, -1, conv, sizeof(conv), NULL, NULL);
        conv[MAXPNAMELEN - 1] = 0;
        EXPECT_STR(a.szPname, conv);
    }
}

/* A copies exactly min(cb, sizeof(WAVEINCAPS2A) = 0x60) bytes. */
static void a_partial(UINT_PTR id, UINT cb) {
    unsigned char buf[0x100]; memset(buf, FILLB, sizeof(buf));
    unsigned char full[0x100]; memset(full, 0, sizeof(full));
    EXPECT_EQ(U(waveInGetDevCapsA)(id, (LPWAVEINCAPSA)full, sizeof(WAVEINCAPS2A)), MMSYSERR_NOERROR);
    MMRESULT r = U(waveInGetDevCapsA)(id, (LPWAVEINCAPSA)buf, cb);
    CHECKF(r == MMSYSERR_NOERROR, "cb=%u r=%u", cb, r);
    UINT n = cb < 0x60 ? cb : 0x60;
    CHECKF(!memcmp(buf, full, n), "cb=%u copied prefix differs", cb);
    CHECKF(all_bytes(buf + n, sizeof(buf) - n, FILLB), "cb=%u wrote past %u", cb, n);
}
TEST(wavein_contract_caps_dev_a_partial_sizes) {
    REQUIRE(U(waveInGetDevCapsA));
    if (!have_dev()) return;
    static const UINT cbs[] = { 1, 2, 4, 7, 8, 9, 20, 39, 40, 47, 48, 50, 0x5F, 0x60, 0x61, 0x80, 0x100 };
    for (unsigned i = 0; i < sizeof(cbs) / sizeof(cbs[0]); i++) { a_partial(WAVE_MAPPER, cbs[i]); a_partial(0, cbs[i]); }
}

TEST(wavein_contract_caps_dev_a_caps2_layout) {
    REQUIRE(U(waveInGetDevCapsA));
    if (!have_dev()) return;
    WAVEINCAPS2A c; memset(&c, FILLB, sizeof(c));
    EXPECT_EQ(U(waveInGetDevCapsA)(0, (LPWAVEINCAPSA)&c, sizeof(c)), MMSYSERR_NOERROR);
    CHECK(!all_bytes(&c.ManufacturerGuid, sizeof(GUID), FILLB));
    t_note("caps2A id0 mfg=%08lx prod=%08lx name=%08lx", c.ManufacturerGuid.Data1, c.ProductGuid.Data1, c.NameGuid.Data1);
}

/* W hands (pwic, cbwic) to the driver unchanged. The inbox driver [VM] rejects cbwic < sizeof(WAVEINCAPSW)
 * (80) with 11 and writes nothing; for larger sizes it writes at most cbwic bytes. */
TEST(wavein_contract_caps_dev_w_partial_sizes) {
    REQUIRE(U(waveInGetDevCapsW));
    if (!have_dev()) return;
    static const UINT cbs[] = { 1, 4, 8, 0x48, 79, sizeof(WAVEINCAPSW), 81, sizeof(WAVEINCAPS2W), 0x100 };
    WAVEINCAPS2W full; memset(&full, 0, sizeof(full));
    EXPECT_EQ(U(waveInGetDevCapsW)(0, (LPWAVEINCAPSW)&full, sizeof(full)), MMSYSERR_NOERROR);
    for (unsigned i = 0; i < sizeof(cbs) / sizeof(cbs[0]); i++) {
        unsigned char buf[0x120]; memset(buf, FILLB, sizeof(buf));
        MMRESULT r = U(waveInGetDevCapsW)(0, (LPWAVEINCAPSW)buf, cbs[i]);
        UINT last = 0; for (UINT k = 0; k < sizeof(buf); k++) if (buf[k] != FILLB) last = k + 1;
        t_note("W cb=%u -> %u, last written byte %u", cbs[i], r, last);
        if (cbs[i] < sizeof(WAVEINCAPSW)) {
            CHECKF(r == MMSYSERR_INVALPARAM, "cb=%u r=%u", cbs[i], r);
            CHECKF(last == 0, "cb=%u wrote %u bytes", cbs[i], last);
        } else {
            CHECKF(r == MMSYSERR_NOERROR, "cb=%u r=%u", cbs[i], r);
            CHECKF(last <= cbs[i], "cb=%u wrote %u bytes", cbs[i], last);
            CHECKF(!memcmp(buf, &full, sizeof(WAVEINCAPSW)), "cb=%u WAVEINCAPSW part differs", cbs[i]);
        }
    }
}

/* A valid HWAVEIN is accepted in place of the id. */
TEST(wavein_contract_caps_dev_handle_as_id) {
    REQUIRE(U(waveInGetDevCapsW)); REQUIRE(U(waveInGetDevCapsA)); REQUIRE(U(waveInOpen)); REQUIRE(U(waveInClose));
    if (!have_dev()) return;
    HWAVEIN h = NULL;
    EXPECT_EQ(wic_open(&h, 0), MMSYSERR_NOERROR);
    if (!h) return;
    WAVEINCAPSW a, b; WAVEINCAPSA c;
    EXPECT_EQ(U(waveInGetDevCapsW)((UINT_PTR)h, &a, sizeof(a)), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveInGetDevCapsW)(0, &b, sizeof(b)), MMSYSERR_NOERROR);
    CHECK(!memcmp(a.szPname, b.szPname, sizeof(a.szPname)));
    CHECK(a.dwFormats == b.dwFormats);
    EXPECT_EQ(U(waveInGetDevCapsA)((UINT_PTR)h, &c, sizeof(c)), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveInGetDevCapsW)((UINT_PTR)h, NULL, sizeof(a)), MMSYSERR_INVALPARAM);
    EXPECT_EQ(U(waveInGetDevCapsW)((UINT_PTR)h, NULL, 0), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveInClose)(h), MMSYSERR_NOERROR);
}

TEST(wavein_contract_caps_dev_mapper_handle_as_id) {
    REQUIRE(U(waveInGetDevCapsW)); REQUIRE(U(waveInOpen)); REQUIRE(U(waveInClose));
    if (!have_dev()) return;
    HWAVEIN h = NULL;
    EXPECT_EQ(wic_open(&h, WAVE_MAPPER), MMSYSERR_NOERROR);
    if (!h) return;
    WAVEINCAPSW a; memset(&a, 0, sizeof(a));
    MMRESULT r = U(waveInGetDevCapsW)((UINT_PTR)h, &a, sizeof(a));
    char nm[64]; WideCharToMultiByte(CP_UTF8, 0, a.szPname, -1, nm, sizeof(nm), NULL, NULL);
    t_note("caps(mapper handle) -> %u \"%s\"", r, nm);
    EXPECT_EQ(r, MMSYSERR_NOERROR);
    if (!t_is_wine()) EXPECT_WSTR(a.szPname, L"Microsoft Sound Mapper");
    EXPECT_EQ(U(waveInClose)(h), MMSYSERR_NOERROR);
}

/* A closed handle is no longer a handle; its truncated value is not a valid id. */
TEST(wavein_contract_caps_dev_closed_handle_2) {
    REQUIRE(U(waveInGetDevCapsW)); REQUIRE(U(waveInGetDevCapsA));
    HWAVEIN h = wic_closed_handle();
    if (!h) return;
    WAVEINCAPSW a; WAVEINCAPSA c;
    EXPECT_EQ(U(waveInGetDevCapsW)((UINT_PTR)h, &a, sizeof(a)), MMSYSERR_BADDEVICEID);
    EXPECT_EQ(U(waveInGetDevCapsA)((UINT_PTR)h, &c, sizeof(c)), MMSYSERR_BADDEVICEID);
}

/* Handles of another type are not accepted as an HWAVEIN (they fall through to the id path). */
TEST(wavein_contract_caps_mmio_handle_2) {
    REQUIRE(U(waveInGetDevCapsW)); REQUIRE(U(waveInGetDevCapsA));
    HMMIO m = wic_mmio_open();
    if (!m) { t_skip("mmioOpen(memory) failed"); return; }
    WAVEINCAPSW a; WAVEINCAPSA c;
    EXPECT_EQ(U(waveInGetDevCapsW)((UINT_PTR)m, &a, sizeof(a)), MMSYSERR_BADDEVICEID);
    EXPECT_EQ(U(waveInGetDevCapsA)((UINT_PTR)m, &c, sizeof(c)), MMSYSERR_BADDEVICEID);
    wic_mmio_close(m);
}

TEST(wavein_contract_caps_waveout_handle_2) {
    REQUIRE(U(waveInGetDevCapsW)); REQUIRE(U(waveInGetDevCapsA));
    HWAVEOUT o = wic_waveout_open();
    if (!o) { t_skip("no waveOut device to open"); return; }
    WAVEINCAPSW a; WAVEINCAPSA c;
    EXPECT_EQ(U(waveInGetDevCapsW)((UINT_PTR)o, &a, sizeof(a)), MMSYSERR_BADDEVICEID);
    EXPECT_EQ(U(waveInGetDevCapsA)((UINT_PTR)o, &c, sizeof(c)), MMSYSERR_BADDEVICEID);
    U(waveOutClose)(o);
}

/* And vice versa: an HWAVEIN is not a waveOut caps handle. */
TEST(wavein_contract_caps_wavein_handle_to_waveout_caps_2) {
    REQUIRE(U(waveOutGetDevCapsW));
    HWAVEIN h = wic_open_any();
    if (!h) return;
    WAVEOUTCAPSW c;
    EXPECT_EQ(U(waveOutGetDevCapsW)((UINT_PTR)h, &c, sizeof(c)), MMSYSERR_BADDEVICEID);
    EXPECT_EQ(U(waveInClose)(h), MMSYSERR_NOERROR);
}

/* The repeated query is stable. */
TEST(wavein_contract_caps_dev_stable) {
    REQUIRE(U(waveInGetDevCapsW));
    if (!have_dev()) return;
    WAVEINCAPSW a, b;
    memset(&a, 0, sizeof(a)); memset(&b, 0, sizeof(b));
    EXPECT_EQ(U(waveInGetDevCapsW)(0, &a, sizeof(a)), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveInGetDevCapsW)(0, &b, sizeof(b)), MMSYSERR_NOERROR);
    CHECK(!memcmp(&a, &b, sizeof(a)));
}
