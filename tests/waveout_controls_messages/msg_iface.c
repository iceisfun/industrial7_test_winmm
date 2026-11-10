/* waveOutMessage / waveInMessage on a device id: the interface, function-instance and string-id queries that
 * winmmbase (mregHandleInternalMessages) answers itself.
 *
 *  0x80D DRV_QUERYDEVICEINTERFACESIZE  dw1 = ULONG* (NULL -> 11); bytes including the terminator.
 *  0x80C DRV_QUERYDEVICEINTERFACE      dw1 = WCHAR*, dw2 = size in BYTES (count = dw2 / 2); NULL -> 11;
 *                                       StringCchCopyW: truncation -> MMSYSERR_MOREDATA (21), other failure
 *                                       (count 0) -> MMSYSERR_ERROR (1).
 *  0x812 DRV_QUERYFUNCTIONINSTANCEIDSIZE  dw1 = ULONG* (NULL -> 11).
 *  0x811 DRV_QUERYFUNCTIONINSTANCEID      dw1 = WCHAR*, dw2 = bytes (count = dw2 / 2); NULL -> 11; any copy
 *                                          failure (truncation or count 0) -> 21.
 *  0x80F DRV_QUERYSTRINGIDSIZE / 0x80E DRV_QUERYSTRINGID (StringCbCopyW, dw2 bytes; failure -> 21) /
 *  0x810 DRV_QUERYIDFROMSTRINGID (dw1 = string, dw2 = UINT*; NULL either -> 11). */
#include "msg_common.h"

#define BUFW 1024

static void fill(WCHAR *b, size_t n) { for (size_t i = 0; i < n; i++) b[i] = 0x2A2A; }

/* ---------------------------------------------------------------- NULL outputs */

TEST(wcm_iface_null_outputs_invalparam)
{
    wcm_dir d[2];
    REQUIRE(wcm_dirs(d));
    static const UINT msgs[] = { WCM_DRV_QUERYDEVICEINTERFACESIZE, WCM_DRV_QUERYDEVICEINTERFACE,
                                 WCM_DRV_QUERYFUNCTIONINSTANCEIDSIZE, WCM_DRV_QUERYFUNCTIONINSTANCEID,
                                 WCM_DRV_QUERYSTRINGIDSIZE, WCM_DRV_QUERYIDFROMSTRINGID };
    int ran = 0;
    for (int k = 0; k < 2; k++)
        for (UINT id = 0; id < d[k].n && id < 4; id++)
            for (size_t m = 0; m < sizeof msgs / sizeof msgs[0]; m++) {
                MMRESULT r = d[k].fn((void *)(ULONG_PTR)id, msgs[m], 0, 256);
                CHECKF(r == WCM_INVALPARAM, "%s id %u msg 0x%x r=%u", d[k].name, id, msgs[m], r);
                ran++;
            }
    if (!ran) t_skip("no wave device");
}

TEST(wcm_iface_null_outputs_on_mapper)
{
    wcm_dir d[2];
    REQUIRE(wcm_dirs(d));
    if (!wcm_mapper_present()) { t_skip("no mapper"); return; }
    static const UINT msgs[] = { WCM_DRV_QUERYDEVICEINTERFACESIZE, WCM_DRV_QUERYDEVICEINTERFACE,
                                 WCM_DRV_QUERYFUNCTIONINSTANCEIDSIZE, WCM_DRV_QUERYFUNCTIONINSTANCEID,
                                 WCM_DRV_QUERYSTRINGIDSIZE };
    for (int k = 0; k < 2; k++)
        for (size_t m = 0; m < sizeof msgs / sizeof msgs[0]; m++) {
            MMRESULT r = d[k].fn((void *)(ULONG_PTR)WAVE_MAPPER, msgs[m], 0, 256);
            CHECKF(r == WCM_INVALPARAM, "%s mapper msg 0x%x r=%u", d[k].name, msgs[m], r);
        }
}

/* ---------------------------------------------------------------- device interface */

static void iface_on(const wcm_dir *d, UINT id)
{
    DWORD sz = WCM_SENT;
    MMRESULT r = d->fn((void *)(ULONG_PTR)id, WCM_DRV_QUERYDEVICEINTERFACESIZE, (DWORD_PTR)&sz, 0);
    EXPECT_EQ(r, WCM_OK);
    if (r) return;
    CHECKF(sz >= 2 && sz % 2 == 0 && sz < BUFW * 2, "%s id %u size %lu", d->name, id, (unsigned long)sz);
    if (sz < 2 || sz >= BUFW * 2) return;
    static WCHAR buf[BUFW];
    fill(buf, BUFW);
    EXPECT_EQ(d->fn((void *)(ULONG_PTR)id, WCM_DRV_QUERYDEVICEINTERFACE, (DWORD_PTR)buf, sz), WCM_OK);
    size_t len = wcm_wlen(buf, BUFW);
    EXPECT_EQ((len + 1) * 2, sz);
    EXPECT_EQ(buf[sz / 2], 0x2A2A);                 /* nothing written past the declared size */
    t_note("OBS %s id %u interface size=%lu \"%s\"", d->name, id, (unsigned long)sz, wcm_w2a(buf));
    if (len >= 4) {
        /* The stored value is either a "\\?\" / "\\.\" path or an endpoint id converted to its PnP interface. */
        CHECKF(buf[0] == '\\' && buf[1] == '\\' && (buf[2] == '?' || buf[2] == '.') && buf[3] == '\\',
               "%s id %u interface does not start with \\\\?\\", d->name, id);
    }
    /* bigger buffer: same answer */
    static WCHAR big[BUFW];
    fill(big, BUFW);
    EXPECT_EQ(d->fn((void *)(ULONG_PTR)id, WCM_DRV_QUERYDEVICEINTERFACE, (DWORD_PTR)big, BUFW * 2), WCM_OK);
    CHECK(!lstrcmpW(big, buf));
    /* odd byte count rounds down to whole characters: sz + 1 still fits */
    fill(big, BUFW);
    EXPECT_EQ(d->fn((void *)(ULONG_PTR)id, WCM_DRV_QUERYDEVICEINTERFACE, (DWORD_PTR)big, sz + 1), WCM_OK);
    /* one character short -> truncated, MOREDATA, still terminated */
    fill(big, BUFW);
    EXPECT_EQ(d->fn((void *)(ULONG_PTR)id, WCM_DRV_QUERYDEVICEINTERFACE, (DWORD_PTR)big, sz - 2), WCM_MOREDATA);
    if (sz >= 4) { EXPECT_EQ(big[sz / 2 - 2], 0); EXPECT_EQ(wcm_wlen(big, BUFW), len - 1); EXPECT_EQ(big[sz / 2 - 1], 0x2A2A); }
    fill(big, BUFW);
    EXPECT_EQ(d->fn((void *)(ULONG_PTR)id, WCM_DRV_QUERYDEVICEINTERFACE, (DWORD_PTR)big, sz - 1), WCM_MOREDATA);
    /* room for one character (the terminator only) */
    fill(big, BUFW);
    EXPECT_EQ(d->fn((void *)(ULONG_PTR)id, WCM_DRV_QUERYDEVICEINTERFACE, (DWORD_PTR)big, 2), WCM_MOREDATA);
    EXPECT_EQ(big[0], 0);
    EXPECT_EQ(big[1], 0x2A2A);
    /* count 0 -> the copy is rejected as an invalid parameter: MMSYSERR_ERROR, nothing written */
    fill(big, BUFW);
    EXPECT_EQ(d->fn((void *)(ULONG_PTR)id, WCM_DRV_QUERYDEVICEINTERFACE, (DWORD_PTR)big, 0), WCM_ERROR);
    EXPECT_EQ(big[0], 0x2A2A);
    fill(big, BUFW);
    EXPECT_EQ(d->fn((void *)(ULONG_PTR)id, WCM_DRV_QUERYDEVICEINTERFACE, (DWORD_PTR)big, 1), WCM_ERROR);
    EXPECT_EQ(big[0], 0x2A2A);
}

TEST(wcm_iface_device_interface_out)
{
    wcm_dir d[2];
    REQUIRE(wcm_dirs(d));
    if (!d[0].n) { t_skip("no waveOut device"); return; }
    wcm_note_devices();
    for (UINT id = 0; id < d[0].n && id < 4; id++) iface_on(&d[0], id);
}

TEST(wcm_iface_device_interface_in)
{
    wcm_dir d[2];
    REQUIRE(wcm_dirs(d));
    if (!d[1].n) { t_skip("no waveIn device"); return; }
    for (UINT id = 0; id < d[1].n && id < 4; id++) iface_on(&d[1], id);
}

/* The size query writes exactly one DWORD. */
TEST(wcm_iface_size_writes_one_dword)
{
    wcm_dir d[2];
    REQUIRE(wcm_dirs(d));
    int ran = 0;
    for (int k = 0; k < 2; k++) {
        if (!d[k].n) continue;
        static const UINT msgs[] = { WCM_DRV_QUERYDEVICEINTERFACESIZE, WCM_DRV_QUERYFUNCTIONINSTANCEIDSIZE };
        for (int m = 0; m < 2; m++) {
            unsigned char b[16];
            memset(b, 0xA5, sizeof b);
            EXPECT_EQ(d[k].fn((void *)0, msgs[m], (DWORD_PTR)(b + 4), 0), WCM_OK);
            for (int i = 0; i < 4; i++) EXPECT_EQ(b[i], 0xA5);
            for (int i = 8; i < 16; i++) EXPECT_EQ(b[i], 0xA5);
            ran++;
        }
    }
    if (!ran) t_skip("no wave device");
}

TEST(wcm_iface_mapper_record)
{
    wcm_dir d[2];
    REQUIRE(wcm_dirs(d));
    if (!wcm_mapper_present()) { t_skip("no mapper"); return; }
    for (int k = 0; k < 2; k++) {
        DWORD sz = WCM_SENT, fsz = WCM_SENT;
        MMRESULT r = d[k].fn((void *)(ULONG_PTR)WAVE_MAPPER, WCM_DRV_QUERYDEVICEINTERFACESIZE, (DWORD_PTR)&sz, 0);
        MMRESULT rf = d[k].fn((void *)(ULONG_PTR)WAVE_MAPPER, WCM_DRV_QUERYFUNCTIONINSTANCEIDSIZE, (DWORD_PTR)&fsz, 0);
        WCHAR b[64]; fill(b, 64);
        MMRESULT rs = d[k].fn((void *)(ULONG_PTR)WAVE_MAPPER, WCM_DRV_QUERYDEVICEINTERFACE, (DWORD_PTR)b, sizeof b);
        WCHAR f[64]; fill(f, 64);
        MMRESULT rfs = d[k].fn((void *)(ULONG_PTR)WAVE_MAPPER, WCM_DRV_QUERYFUNCTIONINSTANCEID, (DWORD_PTR)f, sizeof f);
        t_note("OBS %s mapper ifsize r=%u v=0x%lx if r=%u b0=0x%x fisize r=%u v=0x%lx fi r=%u f0=0x%x", d[k].name, r,
               (unsigned long)sz, rs, b[0], rf, (unsigned long)fsz, rfs, f[0]);
        WCHAR z[4]; fill(z, 4);
        MMRESULT r0 = d[k].fn((void *)(ULONG_PTR)WAVE_MAPPER, WCM_DRV_QUERYDEVICEINTERFACE, (DWORD_PTR)z, 0);
        MMRESULT rf0 = d[k].fn((void *)(ULONG_PTR)WAVE_MAPPER, WCM_DRV_QUERYFUNCTIONINSTANCEID, (DWORD_PTR)z, 0);
        t_note("OBS %s mapper if(cb0) r=%u fi(cb0) r=%u z0=0x%x", d[k].name, r0, rf0, z[0]);
    }
}

/* ---------------------------------------------------------------- function instance id */

static void fiid_on(const wcm_dir *d, UINT id)
{
    DWORD sz = WCM_SENT;
    MMRESULT r = d->fn((void *)(ULONG_PTR)id, WCM_DRV_QUERYFUNCTIONINSTANCEIDSIZE, (DWORD_PTR)&sz, 0);
    EXPECT_EQ(r, WCM_OK);
    if (r) return;
    CHECKF(sz >= 2 && sz % 2 == 0 && sz < BUFW * 2, "%s id %u size %lu", d->name, id, (unsigned long)sz);
    if (sz < 2 || sz >= BUFW * 2) return;
    static WCHAR buf[BUFW];
    fill(buf, BUFW);
    EXPECT_EQ(d->fn((void *)(ULONG_PTR)id, WCM_DRV_QUERYFUNCTIONINSTANCEID, (DWORD_PTR)buf, sz), WCM_OK);
    size_t len = wcm_wlen(buf, BUFW);
    EXPECT_EQ((len + 1) * 2, sz);
    EXPECT_EQ(buf[sz / 2], 0x2A2A);
    t_note("OBS %s id %u fiid size=%lu \"%s\"", d->name, id, (unsigned long)sz, wcm_w2a(buf));
    /* An MMDevice endpoint id: "{0.0.0.00000000}.{guid}" for render, "{0.0.1.00000000}.{guid}" for capture. */
    static const WCHAR pre_out[] = L"{0.0.0.", pre_in[] = L"{0.0.1.";
    const WCHAR *pre = d->is_out ? pre_out : pre_in;
    CHECKF(len >= 7 && !memcmp(buf, pre, 7 * sizeof(WCHAR)), "%s id %u fiid prefix \"%s\"", d->name, id, wcm_w2a(buf));
    if (len >= 2) CHECK(buf[len - 1] == '}');
    /* truncation / count 0: every copy failure is MOREDATA here */
    static WCHAR b2[BUFW];
    fill(b2, BUFW);
    EXPECT_EQ(d->fn((void *)(ULONG_PTR)id, WCM_DRV_QUERYFUNCTIONINSTANCEID, (DWORD_PTR)b2, sz - 2), WCM_MOREDATA);
    if (sz >= 4) { EXPECT_EQ(b2[sz / 2 - 2], 0); EXPECT_EQ(b2[sz / 2 - 1], 0x2A2A); }
    fill(b2, BUFW);
    EXPECT_EQ(d->fn((void *)(ULONG_PTR)id, WCM_DRV_QUERYFUNCTIONINSTANCEID, (DWORD_PTR)b2, 2), WCM_MOREDATA);
    EXPECT_EQ(b2[0], 0);
    fill(b2, BUFW);
    EXPECT_EQ(d->fn((void *)(ULONG_PTR)id, WCM_DRV_QUERYFUNCTIONINSTANCEID, (DWORD_PTR)b2, 0), WCM_MOREDATA);
    EXPECT_EQ(b2[0], 0x2A2A);
    fill(b2, BUFW);
    EXPECT_EQ(d->fn((void *)(ULONG_PTR)id, WCM_DRV_QUERYFUNCTIONINSTANCEID, (DWORD_PTR)b2, 1), WCM_MOREDATA);
    EXPECT_EQ(b2[0], 0x2A2A);
    fill(b2, BUFW);
    EXPECT_EQ(d->fn((void *)(ULONG_PTR)id, WCM_DRV_QUERYFUNCTIONINSTANCEID, (DWORD_PTR)b2, sz + 1), WCM_OK);
    CHECK(!lstrcmpW(b2, buf));
    fill(b2, BUFW);
    EXPECT_EQ(d->fn((void *)(ULONG_PTR)id, WCM_DRV_QUERYFUNCTIONINSTANCEID, (DWORD_PTR)b2, BUFW * 2), WCM_OK);
    CHECK(!lstrcmpW(b2, buf));
}

TEST(wcm_iface_function_instance_id_out)
{
    wcm_dir d[2];
    REQUIRE(wcm_dirs(d));
    if (!d[0].n) { t_skip("no waveOut device"); return; }
    for (UINT id = 0; id < d[0].n && id < 4; id++) fiid_on(&d[0], id);
}

TEST(wcm_iface_function_instance_id_in)
{
    wcm_dir d[2];
    REQUIRE(wcm_dirs(d));
    if (!d[1].n) { t_skip("no waveIn device"); return; }
    for (UINT id = 0; id < d[1].n && id < 4; id++) fiid_on(&d[1], id);
}

/* Render and capture endpoints have different function instance ids. */
TEST(wcm_iface_out_and_in_differ)
{
    wcm_dir d[2];
    REQUIRE(wcm_dirs(d));
    if (!d[0].n || !d[1].n) { t_skip("needs both directions"); return; }
    static WCHAR a[BUFW], b[BUFW];
    fill(a, BUFW); fill(b, BUFW);
    EXPECT_EQ(d[0].fn((void *)0, WCM_DRV_QUERYFUNCTIONINSTANCEID, (DWORD_PTR)a, sizeof a), WCM_OK);
    EXPECT_EQ(d[1].fn((void *)0, WCM_DRV_QUERYFUNCTIONINSTANCEID, (DWORD_PTR)b, sizeof b), WCM_OK);
    CHECK(lstrcmpW(a, b) != 0);
    fill(a, BUFW); fill(b, BUFW);
    EXPECT_EQ(d[0].fn((void *)0, WCM_DRV_QUERYDEVICEINTERFACE, (DWORD_PTR)a, sizeof a), WCM_OK);
    EXPECT_EQ(d[1].fn((void *)0, WCM_DRV_QUERYDEVICEINTERFACE, (DWORD_PTR)b, sizeof b), WCM_OK);
    t_note("OBS interface out==in: %d", !lstrcmpW(a, b));
}

/* Repeated queries return identical strings. */
TEST(wcm_iface_queries_stable)
{
    wcm_dir d[2];
    REQUIRE(wcm_dirs(d));
    int ran = 0;
    for (int k = 0; k < 2; k++) {
        if (!d[k].n) continue;
        static WCHAR first[BUFW], again[BUFW];
        static const UINT msgs[] = { WCM_DRV_QUERYDEVICEINTERFACE, WCM_DRV_QUERYFUNCTIONINSTANCEID };
        for (int m = 0; m < 2; m++) {
            fill(first, BUFW);
            EXPECT_EQ(d[k].fn((void *)0, msgs[m], (DWORD_PTR)first, sizeof first), WCM_OK);
            for (int i = 0; i < 8; i++) {
                fill(again, BUFW);
                EXPECT_EQ(d[k].fn((void *)0, msgs[m], (DWORD_PTR)again, sizeof again), WCM_OK);
                CHECK(!lstrcmpW(first, again));
            }
            ran++;
        }
    }
    if (!ran) t_skip("no wave device");
}

/* ---------------------------------------------------------------- string ids */

static void stringid_on(const wcm_dir *d, UINT id)
{
    DWORD sz = WCM_SENT;
    MMRESULT r = d->fn((void *)(ULONG_PTR)id, WCM_DRV_QUERYSTRINGIDSIZE, (DWORD_PTR)&sz, 0);
    EXPECT_EQ(r, WCM_OK);
    if (r) return;
    CHECKF(sz >= 2 && sz % 2 == 0 && sz < BUFW * 2, "%s id %u stringid size %lu", d->name, id, (unsigned long)sz);
    if (sz < 2 || sz >= BUFW * 2) return;
    static WCHAR buf[BUFW];
    fill(buf, BUFW);
    EXPECT_EQ(d->fn((void *)(ULONG_PTR)id, WCM_DRV_QUERYSTRINGID, (DWORD_PTR)buf, sz), WCM_OK);
    size_t len = wcm_wlen(buf, BUFW);
    CHECKF((len + 1) * 2 <= sz, "%s id %u stringid len %u size %lu", d->name, id, (unsigned)len, (unsigned long)sz);
    t_note("OBS %s id %u stringid size=%lu len=%u \"%s\"", d->name, id, (unsigned long)sz, (unsigned)len, wcm_w2a(buf));
    /* round trip: the string id names this device id */
    UINT back = 0xDEADu;
    MMRESULT rb = d->fn((void *)(ULONG_PTR)id, WCM_DRV_QUERYIDFROMSTRINGID, (DWORD_PTR)buf, (DWORD_PTR)&back);
    t_note("OBS %s id %u idfromstringid r=%u id=%u", d->name, id, rb, back);
    EXPECT_EQ(rb, WCM_OK);
    EXPECT_EQ(back, id);
    /* exact fit by bytes, one byte short, zero bytes */
    static WCHAR b2[BUFW];
    fill(b2, BUFW);
    EXPECT_EQ(d->fn((void *)(ULONG_PTR)id, WCM_DRV_QUERYSTRINGID, (DWORD_PTR)b2, (len + 1) * 2), WCM_OK);
    CHECK(!lstrcmpW(b2, buf));
    fill(b2, BUFW);
    EXPECT_EQ(d->fn((void *)(ULONG_PTR)id, WCM_DRV_QUERYSTRINGID, (DWORD_PTR)b2, len * 2), WCM_MOREDATA);
    if (len) EXPECT_EQ(b2[len - 1], 0);
    fill(b2, BUFW);
    EXPECT_EQ(d->fn((void *)(ULONG_PTR)id, WCM_DRV_QUERYSTRINGID, (DWORD_PTR)b2, 0), WCM_MOREDATA);
    EXPECT_EQ(b2[0], 0x2A2A);
}

TEST(wcm_iface_string_id_out)
{
    wcm_dir d[2];
    REQUIRE(wcm_dirs(d));
    if (!d[0].n) { t_skip("no waveOut device"); return; }
    for (UINT id = 0; id < d[0].n && id < 4; id++) stringid_on(&d[0], id);
}

TEST(wcm_iface_string_id_in)
{
    wcm_dir d[2];
    REQUIRE(wcm_dirs(d));
    if (!d[1].n) { t_skip("no waveIn device"); return; }
    for (UINT id = 0; id < d[1].n && id < 4; id++) stringid_on(&d[1], id);
}

TEST(wcm_iface_id_from_string_id_params)
{
    wcm_dir d[2];
    REQUIRE(wcm_dirs(d));
    int ran = 0;
    for (int k = 0; k < 2; k++) {
        if (!d[k].n) continue;
        UINT out = 0xDEADu;
        EXPECT_EQ(d[k].fn((void *)0, WCM_DRV_QUERYIDFROMSTRINGID, 0, (DWORD_PTR)&out), WCM_INVALPARAM);
        EXPECT_EQ(out, 0xDEADu);
        EXPECT_EQ(d[k].fn((void *)0, WCM_DRV_QUERYIDFROMSTRINGID, (DWORD_PTR)L"x", 0), WCM_INVALPARAM);
        static const WCHAR *bogus[] = { L"", L"nope", L"00000000:wdmaud.drv:wave:00000000", L"{0.0.0.00000000}" };
        for (int i = 0; i < 4; i++) {
            out = 0xDEADu;
            MMRESULT r = d[k].fn((void *)0, WCM_DRV_QUERYIDFROMSTRINGID, (DWORD_PTR)bogus[i], (DWORD_PTR)&out);
            t_note("OBS %s idfromstringid bogus %d r=%u out=0x%x", d[k].name, i, r, out);
            CHECK(r != WCM_OK || out < d[k].n);
        }
        ran++;
    }
    if (!ran) t_skip("no wave device");
}

/* The string id of a waveOut device is not found in the waveIn list (lookup is per device type). */
TEST(wcm_iface_string_id_cross_direction)
{
    wcm_dir d[2];
    REQUIRE(wcm_dirs(d));
    if (!d[0].n || !d[1].n) { t_skip("needs both directions"); return; }
    static WCHAR s[BUFW];
    fill(s, BUFW);
    EXPECT_EQ(d[0].fn((void *)0, WCM_DRV_QUERYSTRINGID, (DWORD_PTR)s, sizeof s), WCM_OK);
    UINT out = 0xDEADu;
    MMRESULT r = d[1].fn((void *)0, WCM_DRV_QUERYIDFROMSTRINGID, (DWORD_PTR)s, (DWORD_PTR)&out);
    t_note("OBS waveOut string id looked up through waveIn: r=%u out=0x%x", r, out);
}

TEST(wcm_iface_string_id_mapper_record)
{
    wcm_dir d[2];
    REQUIRE(wcm_dirs(d));
    if (!wcm_mapper_present()) { t_skip("no mapper"); return; }
    for (int k = 0; k < 2; k++) {
        DWORD sz = WCM_SENT;
        MMRESULT r = d[k].fn((void *)(ULONG_PTR)WAVE_MAPPER, WCM_DRV_QUERYSTRINGIDSIZE, (DWORD_PTR)&sz, 0);
        static WCHAR b[BUFW];
        fill(b, BUFW);
        MMRESULT rs = d[k].fn((void *)(ULONG_PTR)WAVE_MAPPER, WCM_DRV_QUERYSTRINGID, (DWORD_PTR)b, sizeof b);
        t_note("OBS %s mapper stringid size r=%u v=%lu str r=%u \"%s\"", d[k].name, r, (unsigned long)sz, rs, wcm_w2a(b));
    }
}
