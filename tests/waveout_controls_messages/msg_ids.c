/* waveOutMessage / waveInMessage: the device-id path (contract half runs with and without devices). */
#include "msg_common.h"

/* Messages that are safe to send to an out-of-range id: they never reach a driver. */
static const UINT k_msgs[] = {
    0, WCM_WODM_GETNUMDEVS, WCM_WODM_GETVOLUME, WCM_WODM_GETPITCH, WCM_DRV_QUERYDRVENTRY, WCM_DRV_QUERYMAPPABLE,
    WCM_DRV_DEVCOUNT, WCM_DRV_QUERYMODULE, WCM_DRV_QUERYDEVICEINTERFACE, WCM_DRV_QUERYDEVICEINTERFACESIZE,
    WCM_DRV_QUERYSTRINGIDSIZE, WCM_DRV_QUERYFUNCTIONINSTANCEID, WCM_DRV_QUERYFUNCTIONINSTANCEIDSIZE,
    WCM_DRVM_MAPPER_PREFERRED_GET, WCM_DRVM_MAPPER_CONSOLEVOICECOM_GET, WCM_UNKNOWN_MSG, 0xFFFFFFFFu
};
#define NMSGS (sizeof k_msgs / sizeof k_msgs[0])

TEST(wcm_msg_exports_resolve)
{
    REQUIRE(U(waveOutMessage));
    REQUIRE(U(waveInMessage));
    EXPECT_EQ(t_proc_ord("winmm.dll", 172), t_proc_in("winmm.dll", "waveOutMessage"));
    EXPECT_EQ(t_proc_ord("winmm.dll", 153), t_proc_in("winmm.dll", "waveInMessage"));
    EXPECT_EQ(t_proc_ord("winmmbase.dll", 127), t_proc_in("winmmbase.dll", "waveOutMessage"));
    EXPECT_EQ(t_proc_ord("winmmbase.dll", 108), t_proc_in("winmmbase.dll", "waveInMessage"));
}

TEST(wcm_msg_winmm_and_winmmbase_agree)
{
    static const char *names[] = { "waveOutMessage", "waveInMessage" };
    for (int k = 0; k < 2; k++) {
        wcm_msg_fn a = (wcm_msg_fn)t_proc_in("winmm.dll", names[k]), b = (wcm_msg_fn)t_proc_in("winmmbase.dll", names[k]);
        REQUIRE(a); REQUIRE(b);
        DWORD sa = WCM_SENT, sb = WCM_SENT;
        EXPECT_EQ(a((void *)(ULONG_PTR)9999, WCM_DRV_QUERYDEVICEINTERFACESIZE, (DWORD_PTR)&sa, 0), WCM_BADDEVICEID);
        EXPECT_EQ(b((void *)(ULONG_PTR)9999, WCM_DRV_QUERYDEVICEINTERFACESIZE, (DWORD_PTR)&sb, 0), WCM_BADDEVICEID);
        EXPECT_EQ(sa, WCM_SENT);
        EXPECT_EQ(sb, WCM_SENT);
        sa = sb = WCM_SENT;
        MMRESULT ra = a((void *)0, WCM_DRV_QUERYDEVICEINTERFACESIZE, (DWORD_PTR)&sa, 0);
        MMRESULT rb = b((void *)0, WCM_DRV_QUERYDEVICEINTERFACESIZE, (DWORD_PTR)&sb, 0);
        EXPECT_EQ(ra, rb);
        EXPECT_EQ(sa, sb);
    }
}

/* id >= count (and != 0xFFFFFFFF) -> 2 for every message, outputs untouched. */
TEST(wcm_msg_out_of_range_ids_every_message)
{
    wcm_dir d[2];
    REQUIRE(wcm_dirs(d));
    wcm_note_devices();
    for (int k = 0; k < 2; k++) {
        UINT ids[] = { d[k].n, d[k].n + 1, 9999, 0xFFFE, 0x10000, 0x7FFFFFFF, 0x80000000u, 0xFFFFFFFEu };
        for (size_t i = 0; i < sizeof ids / sizeof ids[0]; i++)
            for (size_t m = 0; m < NMSGS; m++) {
                DWORD a = WCM_SENT, b = WCM_SENT;
                MMRESULT r = d[k].fn((void *)(ULONG_PTR)ids[i], k_msgs[m], (DWORD_PTR)&a, (DWORD_PTR)&b);
                CHECKF(r == WCM_BADDEVICEID, "%s id 0x%x msg 0x%x r=%u", d[k].name, ids[i], k_msgs[m], r);
                CHECKF(a == WCM_SENT && b == WCM_SENT, "%s id 0x%x msg 0x%x outputs touched", d[k].name, ids[i], k_msgs[m]);
            }
    }
}

/* Out-of-range ids with NULL parameters: still 2 (the id is resolved before any parameter is looked at). */
TEST(wcm_msg_out_of_range_ids_null_params)
{
    wcm_dir d[2];
    REQUIRE(wcm_dirs(d));
    for (int k = 0; k < 2; k++)
        for (size_t m = 0; m < NMSGS; m++) {
            MMRESULT r = d[k].fn((void *)(ULONG_PTR)9999, k_msgs[m], 0, 0);
            CHECKF(r == WCM_BADDEVICEID, "%s msg 0x%x r=%u", d[k].name, k_msgs[m], r);
        }
}

/* 0xFFFF is an ordinary (out-of-range) id for the Message functions. */
TEST(wcm_msg_ffff_is_not_the_mapper)
{
    wcm_dir d[2];
    REQUIRE(wcm_dirs(d));
    for (int k = 0; k < 2; k++) {
        if (d[k].n > 0xFFFF) continue;
        DWORD sz = WCM_SENT, dev = WCM_SENT, fl = WCM_SENT;
        EXPECT_EQ(d[k].fn((void *)(ULONG_PTR)0xFFFF, WCM_DRV_QUERYDEVICEINTERFACESIZE, (DWORD_PTR)&sz, 0), WCM_BADDEVICEID);
        EXPECT_EQ(sz, WCM_SENT);
        EXPECT_EQ(d[k].fn((void *)(ULONG_PTR)0xFFFF, WCM_DRVM_MAPPER_PREFERRED_GET, (DWORD_PTR)&dev, (DWORD_PTR)&fl), WCM_BADDEVICEID);
        EXPECT_EQ(dev, WCM_SENT);
        EXPECT_EQ(fl, WCM_SENT);
    }
}

/* Every spelling of the mapper id (low 32 bits 0xFFFFFFFF) is the same driver. */
TEST(wcm_msg_mapper_spellings_agree)
{
    wcm_dir d[2];
    REQUIRE(wcm_dirs(d));
    static const ULONG_PTR vals[] = { 0xFFFFFFFFu, (ULONG_PTR)~(ULONG_PTR)0, (ULONG_PTR)0x1FFFFFFFFull, (ULONG_PTR)0xABCD0000FFFFFFFFull };
    for (int k = 0; k < 2; k++) {
        DWORD s0 = WCM_SENT;
        MMRESULT r0 = d[k].fn((void *)vals[0], WCM_DRV_QUERYDEVICEINTERFACESIZE, (DWORD_PTR)&s0, 0);
        if (!wcm_dir_has_mapper(&d[k])) { EXPECT_EQ(r0, WCM_BADDEVICEID); EXPECT_EQ(s0, WCM_SENT); }
        for (size_t i = 1; i < 4; i++) {
            DWORD s = WCM_SENT;
            MMRESULT r = d[k].fn((void *)vals[i], WCM_DRV_QUERYDEVICEINTERFACESIZE, (DWORD_PTR)&s, 0);
            CHECKF(r == r0 && s == s0, "%s value 0x%llx r=%u s=0x%lx vs r=%u s=0x%lx", d[k].name, (unsigned long long)vals[i],
                   r, (unsigned long)s, r0, (unsigned long)s0);
        }
    }
}

/* High bits are dropped: 0x100000000 is id 0. */
TEST(wcm_msg_high_bits_truncate_to_id)
{
    wcm_dir d[2];
    REQUIRE(wcm_dirs(d));
    static const ULONG_PTR hi[] = { 0x100000000ull, 0x123400000000ull, 0xFFFF00000000ull };
    for (int k = 0; k < 2; k++)
        for (size_t i = 0; i < 3; i++) {
            DWORD a = WCM_SENT, b = WCM_SENT;
            MMRESULT ra = d[k].fn((void *)hi[i], WCM_DRV_DEVCOUNT, (DWORD_PTR)&a, 0);
            MMRESULT rb = d[k].fn((void *)0, WCM_DRV_DEVCOUNT, (DWORD_PTR)&b, 0);
            CHECKF(ra == rb && a == b, "%s value 0x%llx", d[k].name, (unsigned long long)hi[i]);
            EXPECT_EQ(ra, d[k].n ? WCM_OK : WCM_BADDEVICEID);
        }
}

/* The table of non-handle values through the id path, with a message winmmbase answers itself. */
TEST(wcm_msg_nonhandle_table)
{
    wcm_dir d[2];
    REQUIRE(wcm_dirs(d));
    for (int k = 0; k < 2; k++)
        for (const wcm_val *v = wcm_nonhandles; v->name; v++) {
            DWORD cnt = WCM_SENT;
            MMRESULT r = d[k].fn((void *)v->v, WCM_DRV_DEVCOUNT, (DWORD_PTR)&cnt, 0);
            int cls = wcm_id_class(v->v, d[k].n, 0);
            if (cls == WCM_ID_BAD || (cls == WCM_ID_MAPPER && !wcm_dir_has_mapper(&d[k]))) {
                CHECKF(r == WCM_BADDEVICEID, "%s value %s r=%u", d[k].name, v->name, r);
                CHECKF(cnt == WCM_SENT, "%s value %s output touched", d[k].name, v->name);
            } else if (cls == WCM_ID_DEVICE) {
                CHECKF(r == WCM_OK, "%s value %s r=%u", d[k].name, v->name, r);
                CHECKF(cnt >= 1 && cnt <= d[k].n, "%s value %s count %lu", d[k].name, v->name, (unsigned long)cnt);
            } else {
                t_note("OBS %s mapper-class %s devcount r=%u v=0x%lx", d[k].name, v->name, r, (unsigned long)cnt);
            }
        }
}

/* ---------------------------------------------------------------- zero devices */

TEST(wcm_msg_zero_devices_contract)
{
    wcm_dir d[2];
    REQUIRE(wcm_dirs(d));
    if (wcm_mapper_present()) { t_skip("wave devices present"); return; }
    static const ULONG_PTR vals[] = { 0, 1, 0xFFFF, 0xFFFFFFFFu, (ULONG_PTR)~(ULONG_PTR)0, 9999 };
    for (int k = 0; k < 2; k++) {
        EXPECT_EQ(d[k].n, 0);
        for (size_t i = 0; i < sizeof vals / sizeof vals[0]; i++)
            for (size_t m = 0; m < NMSGS; m++) {
                DWORD a = WCM_SENT, b = WCM_SENT;
                MMRESULT r = d[k].fn((void *)vals[i], k_msgs[m], (DWORD_PTR)&a, (DWORD_PTR)&b);
                CHECKF(r == WCM_BADDEVICEID, "%s value 0x%llx msg 0x%x r=%u", d[k].name, (unsigned long long)vals[i], k_msgs[m], r);
                CHECKF(a == WCM_SENT && b == WCM_SENT, "%s value 0x%llx msg 0x%x outputs touched", d[k].name,
                       (unsigned long long)vals[i], k_msgs[m]);
            }
    }
}

/* With no devices, the preferred-device query on the mapper fails before writing either output. */
TEST(wcm_msg_zero_devices_preferred_get_untouched)
{
    wcm_dir d[2];
    REQUIRE(wcm_dirs(d));
    if (wcm_mapper_present()) { t_skip("wave devices present"); return; }
    for (int k = 0; k < 2; k++) {
        DWORD dev = 0xdead, fl = 0xdead;
        EXPECT_EQ(d[k].fn((void *)(ULONG_PTR)WAVE_MAPPER, WCM_DRVM_MAPPER_PREFERRED_GET, (DWORD_PTR)&dev, (DWORD_PTR)&fl),
                  WCM_BADDEVICEID);
        EXPECT_EQ(dev, 0xdead);
        EXPECT_EQ(fl, 0xdead);
        dev = fl = 0xdead;
        EXPECT_EQ(d[k].fn((void *)(ULONG_PTR)WAVE_MAPPER, WCM_DRVM_MAPPER_CONSOLEVOICECOM_GET, (DWORD_PTR)&dev, (DWORD_PTR)&fl),
                  WCM_BADDEVICEID);
        EXPECT_EQ(dev, 0xdead);
        EXPECT_EQ(fl, 0xdead);
    }
}

/* ---------------------------------------------------------------- messages winmmbase answers itself */

/* 0x801..0x804 and 0x806 are reserved: MMSYSERR_NOTSUPPORTED without a driver call, on a device and on the mapper. */
TEST(wcm_msg_reserved_range_notsupported)
{
    wcm_dir d[2];
    REQUIRE(wcm_dirs(d));
    static const UINT msgs[] = { WCM_DRV_QUERYDRVENTRY, WCM_DRV_QUERYDEVNODE, WCM_DRV_QUERYNAME, WCM_DRV_QUERYDRIVERIDS,
                                 WCM_DRV_RESERVED6 };
    for (int k = 0; k < 2; k++) {
        if (!d[k].n) continue;
        for (size_t m = 0; m < 5; m++) {
            DWORD a = WCM_SENT, b = WCM_SENT;
            MMRESULT r = d[k].fn((void *)0, msgs[m], (DWORD_PTR)&a, (DWORD_PTR)&b);
            CHECKF(r == WCM_NOTSUPP, "%s id 0 msg 0x%x r=%u", d[k].name, msgs[m], r);
            CHECKF(a == WCM_SENT && b == WCM_SENT, "%s msg 0x%x touched", d[k].name, msgs[m]);
            r = d[k].fn((void *)0, msgs[m], 0, 0);
            CHECKF(r == WCM_NOTSUPP, "%s id 0 msg 0x%x NULLs r=%u", d[k].name, msgs[m], r);
        }
    }
    if (!d[0].n && !d[1].n) t_skip("no wave device");
}

TEST(wcm_msg_reserved_range_on_mapper)
{
    wcm_dir d[2];
    REQUIRE(wcm_dirs(d));
    if (!wcm_mapper_present()) { t_skip("no mapper"); return; }
    static const UINT msgs[] = { WCM_DRV_QUERYDRVENTRY, WCM_DRV_QUERYDEVNODE, WCM_DRV_QUERYNAME, WCM_DRV_QUERYDRIVERIDS,
                                 WCM_DRV_RESERVED6 };
    for (int k = 0; k < 2; k++)
        for (size_t m = 0; m < 5; m++) {
            DWORD a = WCM_SENT;
            MMRESULT r = d[k].fn((void *)(ULONG_PTR)WAVE_MAPPER, msgs[m], (DWORD_PTR)&a, 0);
            CHECKF(r == WCM_NOTSUPP, "%s mapper msg 0x%x r=%u", d[k].name, msgs[m], r);
            EXPECT_EQ(a, WCM_SENT);
        }
}

/* 0x808: the device count of the driver that owns the id. */
TEST(wcm_msg_devcount)
{
    wcm_dir d[2];
    REQUIRE(wcm_dirs(d));
    int ran = 0;
    for (int k = 0; k < 2; k++) {
        for (UINT id = 0; id < d[k].n && id < 4; id++) {
            DWORD c = WCM_SENT;
            EXPECT_EQ(d[k].fn((void *)(ULONG_PTR)id, WCM_DRV_DEVCOUNT, (DWORD_PTR)&c, 0), WCM_OK);
            CHECKF(c >= 1 && c <= d[k].n, "%s id %u count %lu n %u", d[k].name, id, (unsigned long)c, d[k].n);
            t_note("OBS %s id %u devcount=%lu", d[k].name, id, (unsigned long)c);
            ran++;
        }
        if (d[k].n) EXPECT_EQ(d[k].fn((void *)0, WCM_DRV_DEVCOUNT, 0, 0), WCM_INVALPARAM);
        if (wcm_mapper_present()) {
            DWORD c = WCM_SENT;
            MMRESULT r = d[k].fn((void *)(ULONG_PTR)WAVE_MAPPER, WCM_DRV_DEVCOUNT, (DWORD_PTR)&c, 0);
            t_note("OBS %s mapper devcount r=%u v=0x%lx", d[k].name, r, (unsigned long)c);
            EXPECT_EQ(d[k].fn((void *)(ULONG_PTR)WAVE_MAPPER, WCM_DRV_DEVCOUNT, 0, 0), WCM_INVALPARAM);
        }
    }
    if (!ran) t_note("no wave device: devcount only checked through the zero-device contract");
}

/* 0x809: the HMODULE of the driver that owns the id. */
TEST(wcm_msg_query_module)
{
    wcm_dir d[2];
    REQUIRE(wcm_dirs(d));
    for (int k = 0; k < 2; k++) {
        if (!d[k].n) continue;
        HMODULE m = (HMODULE)(ULONG_PTR)0xDEADDEADDEADDEADull;
        EXPECT_EQ(d[k].fn((void *)0, WCM_DRV_QUERYMODULE, (DWORD_PTR)&m, 0), WCM_OK);
        char path[MAX_PATH] = "";
        if (m && GetModuleFileNameA(m, path, sizeof path)) t_note("OBS %s id0 module %s", d[k].name, path);
        else t_note("OBS %s id0 module %p (no file name)", d[k].name, (void *)m);
        CHECKF(m != NULL && GetModuleFileNameA(m, path, sizeof path) > 0, "%s id 0 module %p not loaded", d[k].name, (void *)m);
        EXPECT_EQ(d[k].fn((void *)0, WCM_DRV_QUERYMODULE, 0, 0), WCM_INVALPARAM);
    }
    if (wcm_mapper_present())
        for (int k = 0; k < 2; k++) {
            HMODULE m = NULL;
            MMRESULT r = d[k].fn((void *)(ULONG_PTR)WAVE_MAPPER, WCM_DRV_QUERYMODULE, (DWORD_PTR)&m, 0);
            char path[MAX_PATH] = "";
            if (r == 0 && m) GetModuleFileNameA(m, path, sizeof path);
            t_note("OBS %s mapper module r=%u %s", d[k].name, r, path);
            EXPECT_EQ(r, WCM_OK);
            EXPECT_EQ(d[k].fn((void *)(ULONG_PTR)WAVE_MAPPER, WCM_DRV_QUERYMODULE, 0, 0), WCM_INVALPARAM);
        }
}

/* 0x80A copies the driver's name with a count of dw2 - 1 characters; any failure is MMSYSERR_INVALPARAM. */
TEST(wcm_msg_driver_name)
{
    wcm_dir d[2];
    REQUIRE(wcm_dirs(d));
    for (int k = 0; k < 2; k++) {
        if (!d[k].n) continue;
        WCHAR buf[300];
        for (int i = 0; i < 300; i++) buf[i] = 0x2A2A;
        MMRESULT r = d[k].fn((void *)0, WCM_DRV_DRIVERNAME, (DWORD_PTR)buf, 260);
        t_note("OBS %s id0 drivername r=%u \"%s\"", d[k].name, r, wcm_w2a(buf));
        EXPECT_EQ(r, WCM_OK);
        size_t len = wcm_wlen(buf, 300);
        CHECK(len > 0 && len < 259);
        CHECK(buf[len + 1] == 0x2A2A);                             /* nothing written past the terminator */
        EXPECT_EQ(d[k].fn((void *)0, WCM_DRV_DRIVERNAME, 0, 260), WCM_INVALPARAM);
        EXPECT_EQ(d[k].fn((void *)0, WCM_DRV_DRIVERNAME, (DWORD_PTR)buf, 0), WCM_INVALPARAM);
        EXPECT_EQ(d[k].fn((void *)0, WCM_DRV_DRIVERNAME, (DWORD_PTR)buf, 1), WCM_INVALPARAM);    /* count 0 */
        if (len >= 1) {
            /* count = len: one short of room for the terminator -> truncation failure -> 11 */
            for (int i = 0; i < 300; i++) buf[i] = 0x2A2A;
            EXPECT_EQ(d[k].fn((void *)0, WCM_DRV_DRIVERNAME, (DWORD_PTR)buf, (DWORD_PTR)len + 1), WCM_INVALPARAM);
            /* count = len + 1: exactly enough */
            for (int i = 0; i < 300; i++) buf[i] = 0x2A2A;
            EXPECT_EQ(d[k].fn((void *)0, WCM_DRV_DRIVERNAME, (DWORD_PTR)buf, (DWORD_PTR)len + 2), WCM_OK);
            EXPECT_EQ(wcm_wlen(buf, 300), len);
            EXPECT_EQ(buf[len + 1], 0x2A2A);
        }
    }
}

/* 0x805 DRV_QUERYMAPPABLE: answered from the Wave Mapper registry key, no driver call. */
TEST(wcm_msg_query_mappable)
{
    wcm_dir d[2];
    REQUIRE(wcm_dirs(d));
    for (int k = 0; k < 2; k++) {
        if (!d[k].n) continue;
        MMRESULT r = d[k].fn((void *)0, WCM_DRV_QUERYMAPPABLE, 0, 0);
        t_note("OBS %s id0 querymappable(0,0) r=%u", d[k].name, r);
        CHECK(r == WCM_OK || r == WCM_NOTSUPP);
        DWORD a = WCM_SENT, b = WCM_SENT;
        MMRESULT r2 = d[k].fn((void *)0, WCM_DRV_QUERYMAPPABLE, (DWORD_PTR)&a, 0);
        MMRESULT r3 = d[k].fn((void *)0, WCM_DRV_QUERYMAPPABLE, 0, (DWORD_PTR)&b);
        t_note("OBS %s id0 querymappable(p,0) r=%u (0,p) r=%u", d[k].name, r2, r3);
        EXPECT_EQ(a, WCM_SENT);
        EXPECT_EQ(b, WCM_SENT);
    }
}

/* Unknown messages on an id go to the driver. */
TEST(wcm_msg_unknown_message_record)
{
    wcm_dir d[2];
    REQUIRE(wcm_dirs(d));
    for (int k = 0; k < 2; k++) {
        if (d[k].n) {
            MMRESULT r = d[k].fn((void *)0, WCM_UNKNOWN_MSG, 0, 0);
            t_note("OBS %s id0 msg 0x7FFF r=%u", d[k].name, r);
            EXPECT_EQ(r, WCM_NOTSUPP);
            r = d[k].fn((void *)0, 0x4FFF, 0, 0);
            t_note("OBS %s id0 msg 0x4FFF r=%u", d[k].name, r);
        }
        if (wcm_mapper_present()) {
            MMRESULT r = d[k].fn((void *)(ULONG_PTR)WAVE_MAPPER, WCM_UNKNOWN_MSG, 0, 0);
            t_note("OBS %s mapper msg 0x7FFF r=%u", d[k].name, r);
        }
    }
}

/* WODM_GETNUMDEVS / WIDM_GETNUMDEVS through the id path reach the driver. */
TEST(wcm_msg_getnumdevs_record)
{
    wcm_dir d[2];
    REQUIRE(wcm_dirs(d));
    for (int k = 0; k < 2; k++) {
        if (!d[k].n) continue;
        MMRESULT r = d[k].fn((void *)0, WCM_WODM_GETNUMDEVS, 0, 0);
        t_note("OBS %s id0 GETNUMDEVS r=%u", d[k].name, r);
    }
}

/* waveOutMessage(id, WODM_GETVOLUME) and waveOutGetVolume(id) reach the same driver entry. */
TEST(wcm_msg_getvolume_id_parity)
{
    REQUIRE(U(waveOutMessage));
    REQUIRE(U(waveOutGetVolume));
    UINT n = wcm_nout();
    if (!n) { t_skip("no waveOut device"); return; }
    for (UINT id = 0; id < n && id < 4; id++) {
        DWORD a = WCM_SENT, b = WCM_SENT;
        MMRESULT ra = U(waveOutGetVolume)((HWAVEOUT)(ULONG_PTR)id, &a);
        MMRESULT rb = U(waveOutMessage)((HWAVEOUT)(ULONG_PTR)id, WCM_WODM_GETVOLUME, (DWORD_PTR)&b, 0);
        t_note("OBS id %u getvolume api r=%u v=0x%lx msg r=%u v=0x%lx", id, ra, (unsigned long)a, rb, (unsigned long)b);
        EXPECT_EQ(ra, WCM_OK);
    }
}
