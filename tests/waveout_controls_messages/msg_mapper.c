/* DRVM_MAPPER_PREFERRED_GET (0x2015) and DRVM_MAPPER_CONSOLEVOICECOM_GET (0x2017) through waveOutMessage /
 * waveInMessage, plus the handle path of the Message functions.
 *
 * winmmbase: both are answered without a driver call, only when the id resolves to the mapper driver (flag 2) and
 * both dw1 (UINT* device) and dw2 (DWORD* flags) are non-NULL; otherwise MMSYSERR_INVALPARAM (11). The device is
 * 0xFFFFFFFF unless a default string id resolves to a device; flags = DRVM_MAPPER_PREFERRED_FLAGS_PREFERREDONLY
 * (1) when "use preferred device only" is set. DRVM_MAPPER_PREFERRED_SET (0x2016) is never sent by this corpus.
 * A live handle sends any message straight to its driver (no internal handling). */
#include "msg_common.h"

/* ---------------------------------------------------------------- preferred get */

static void preferred_on(const wcm_dir *d, UINT msg, const char *tag)
{
    DWORD dev = 0xdead, fl = 0xdead;
    MMRESULT r = d->fn((void *)(ULONG_PTR)WAVE_MAPPER, msg, (DWORD_PTR)&dev, (DWORD_PTR)&fl);
    t_note("OBS %s mapper %s r=%u dev=0x%lx flags=0x%lx", d->name, tag, r, (unsigned long)dev, (unsigned long)fl);
    EXPECT_EQ(r, WCM_OK);
    if (r) return;
    CHECKF(dev == 0xFFFFFFFFu || dev < d->n, "%s %s dev %lu n %u", d->name, tag, (unsigned long)dev, d->n);
    CHECKF(fl == 0 || fl == 1, "%s %s flags 0x%lx", d->name, tag, (unsigned long)fl);
    /* repeatable */
    DWORD dev2 = 0xdead, fl2 = 0xdead;
    EXPECT_EQ(d->fn((void *)(ULONG_PTR)WAVE_MAPPER, msg, (DWORD_PTR)&dev2, (DWORD_PTR)&fl2), WCM_OK);
    EXPECT_EQ(dev2, dev);
    EXPECT_EQ(fl2, fl);
}

TEST(wcm_mapper_preferred_get_out)
{
    wcm_dir d[2];
    REQUIRE(wcm_dirs(d));
    if (!wcm_mapper_present()) { t_skip("no mapper (zero-device contract checked elsewhere)"); return; }
    wcm_note_devices();
    preferred_on(&d[0], WCM_DRVM_MAPPER_PREFERRED_GET, "preferred");
}

TEST(wcm_mapper_preferred_get_in)
{
    wcm_dir d[2];
    REQUIRE(wcm_dirs(d));
    if (!wcm_mapper_present()) { t_skip("no mapper"); return; }
    preferred_on(&d[1], WCM_DRVM_MAPPER_PREFERRED_GET, "preferred");
}

/* With exactly one device in a direction the preferred device is that device, flags 0. */
TEST(wcm_mapper_preferred_single_device)
{
    wcm_dir d[2];
    REQUIRE(wcm_dirs(d));
    int ran = 0;
    for (int k = 0; k < 2; k++) {
        if (d[k].n != 1) continue;
        DWORD dev = 0xdead, fl = 0xdead;
        EXPECT_EQ(d[k].fn((void *)(ULONG_PTR)WAVE_MAPPER, WCM_DRVM_MAPPER_PREFERRED_GET, (DWORD_PTR)&dev, (DWORD_PTR)&fl), WCM_OK);
        EXPECT_EQ(dev, 0);
        EXPECT_EQ(fl, 0);
        ran++;
    }
    if (!ran) t_skip("no direction with exactly one device");
}

TEST(wcm_mapper_consolevoicecom_get_out)
{
    wcm_dir d[2];
    REQUIRE(wcm_dirs(d));
    if (!wcm_mapper_present()) { t_skip("no mapper"); return; }
    preferred_on(&d[0], WCM_DRVM_MAPPER_CONSOLEVOICECOM_GET, "voicecom");
}

TEST(wcm_mapper_consolevoicecom_get_in)
{
    wcm_dir d[2];
    REQUIRE(wcm_dirs(d));
    if (!wcm_mapper_present()) { t_skip("no mapper"); return; }
    preferred_on(&d[1], WCM_DRVM_MAPPER_CONSOLEVOICECOM_GET, "voicecom");
}

/* The communications default and the console default agree when there is a single device. */
TEST(wcm_mapper_voicecom_matches_preferred_single)
{
    wcm_dir d[2];
    REQUIRE(wcm_dirs(d));
    int ran = 0;
    for (int k = 0; k < 2; k++) {
        if (d[k].n != 1) continue;
        DWORD a = 0xdead, fa = 0xdead, b = 0xdead, fb = 0xdead;
        EXPECT_EQ(d[k].fn((void *)(ULONG_PTR)WAVE_MAPPER, WCM_DRVM_MAPPER_PREFERRED_GET, (DWORD_PTR)&a, (DWORD_PTR)&fa), WCM_OK);
        EXPECT_EQ(d[k].fn((void *)(ULONG_PTR)WAVE_MAPPER, WCM_DRVM_MAPPER_CONSOLEVOICECOM_GET, (DWORD_PTR)&b, (DWORD_PTR)&fb), WCM_OK);
        EXPECT_EQ(a, b);
        EXPECT_EQ(fa, fb);
        ran++;
    }
    if (!ran) t_skip("no direction with exactly one device");
}

TEST(wcm_mapper_preferred_null_params)
{
    wcm_dir d[2];
    REQUIRE(wcm_dirs(d));
    if (!wcm_mapper_present()) { t_skip("no mapper"); return; }
    static const UINT msgs[] = { WCM_DRVM_MAPPER_PREFERRED_GET, WCM_DRVM_MAPPER_CONSOLEVOICECOM_GET };
    for (int k = 0; k < 2; k++)
        for (int m = 0; m < 2; m++) {
            DWORD x = 0xdead;
            CHECKF(d[k].fn((void *)(ULONG_PTR)WAVE_MAPPER, msgs[m], 0, 0) == WCM_INVALPARAM, "%s msg 0x%x NULL,NULL", d[k].name, msgs[m]);
            CHECKF(d[k].fn((void *)(ULONG_PTR)WAVE_MAPPER, msgs[m], (DWORD_PTR)&x, 0) == WCM_INVALPARAM, "%s msg 0x%x p,NULL", d[k].name, msgs[m]);
            EXPECT_EQ(x, 0xdead);
            CHECKF(d[k].fn((void *)(ULONG_PTR)WAVE_MAPPER, msgs[m], 0, (DWORD_PTR)&x) == WCM_INVALPARAM, "%s msg 0x%x NULL,p", d[k].name, msgs[m]);
            EXPECT_EQ(x, 0xdead);
        }
}

/* Sent to a real device id (not the mapper driver): MMSYSERR_INVALPARAM, outputs untouched. */
TEST(wcm_mapper_preferred_on_device_id_invalparam)
{
    wcm_dir d[2];
    REQUIRE(wcm_dirs(d));
    int ran = 0;
    static const UINT msgs[] = { WCM_DRVM_MAPPER_PREFERRED_GET, WCM_DRVM_MAPPER_CONSOLEVOICECOM_GET };
    for (int k = 0; k < 2; k++)
        for (UINT id = 0; id < d[k].n && id < 4; id++)
            for (int m = 0; m < 2; m++) {
                DWORD dev = 0xdead, fl = 0xdead;
                MMRESULT r = d[k].fn((void *)(ULONG_PTR)id, msgs[m], (DWORD_PTR)&dev, (DWORD_PTR)&fl);
                CHECKF(r == WCM_INVALPARAM, "%s id %u msg 0x%x r=%u", d[k].name, id, msgs[m], r);
                EXPECT_EQ(dev, 0xdead);
                EXPECT_EQ(fl, 0xdead);
                ran++;
            }
    if (!ran) t_skip("no wave device");
}

/* The preferred device id agrees with the device the mapper opens (waveOutGetID reports WAVE_MAPPER for a
 * mapper handle, so compare through the function instance id of the preferred device instead). */
TEST(wcm_mapper_preferred_is_a_listed_device)
{
    wcm_dir d[2];
    REQUIRE(wcm_dirs(d));
    int ran = 0;
    for (int k = 0; k < 2; k++) {
        if (!d[k].n) continue;
        DWORD dev = 0xdead, fl = 0xdead;
        if (d[k].fn((void *)(ULONG_PTR)WAVE_MAPPER, WCM_DRVM_MAPPER_PREFERRED_GET, (DWORD_PTR)&dev, (DWORD_PTR)&fl)) continue;
        if (dev == 0xFFFFFFFFu) { t_note("OBS %s preferred device unset", d[k].name); continue; }
        DWORD sz = 0;
        EXPECT_EQ(d[k].fn((void *)(ULONG_PTR)dev, WCM_DRV_QUERYFUNCTIONINSTANCEIDSIZE, (DWORD_PTR)&sz, 0), WCM_OK);
        CHECK(sz >= 2);
        ran++;
    }
    if (!ran) t_skip("no preferred device to check");
}

/* ---------------------------------------------------------------- handle path */

/* A handle of the other direction is not a handle here: its low 32 bits are an (out-of-range) id. */
TEST(wcm_msg_handle_cross_direction)
{
    REQUIRE(U(waveOutMessage));
    REQUIRE(U(waveInMessage));
    if (!wcm_nout() || !wcm_nin()) { t_skip("needs both directions"); return; }
    HWAVEOUT ho; HWAVEIN hi;
    EXPECT_EQ(wcm_open_out(&ho, WAVE_MAPPER), 0);
    EXPECT_EQ(wcm_open_in(&hi, WAVE_MAPPER), 0);
    if (ho && hi) {
        DWORD a = WCM_SENT;
        if (wcm_id_class((ULONG_PTR)hi, wcm_nout(), 0) == WCM_ID_BAD) {
            EXPECT_EQ(U(waveOutMessage)((HWAVEOUT)hi, WCM_DRV_QUERYDEVICEINTERFACESIZE, (DWORD_PTR)&a, 0), WCM_BADDEVICEID);
            EXPECT_EQ(a, WCM_SENT);
            EXPECT_EQ(U(waveOutMessage)((HWAVEOUT)hi, WCM_WODM_GETVOLUME, (DWORD_PTR)&a, 0), WCM_BADDEVICEID);
            EXPECT_EQ(a, WCM_SENT);
        }
        if (wcm_id_class((ULONG_PTR)ho, wcm_nin(), 0) == WCM_ID_BAD) {
            EXPECT_EQ(U(waveInMessage)((HWAVEIN)ho, WCM_DRV_QUERYDEVICEINTERFACESIZE, (DWORD_PTR)&a, 0), WCM_BADDEVICEID);
            EXPECT_EQ(a, WCM_SENT);
            EXPECT_EQ(U(waveInMessage)((HWAVEIN)ho, WCM_UNKNOWN_MSG, 0, 0), WCM_BADDEVICEID);
        }
    }
    wcm_close_out(ho);
    wcm_close_in(hi);
}

TEST(wcm_msg_closed_handle_is_an_id)
{
    REQUIRE(U(waveOutMessage));
    HWAVEOUT c = wcm_closed_out();
    if (!c) { t_skip("no closed handle available (no device)"); return; }
    if (wcm_id_class((ULONG_PTR)c, wcm_nout(), 0) != WCM_ID_BAD) { t_skip("closed handle truncates to a live id"); return; }
    DWORD a = WCM_SENT;
    EXPECT_EQ(U(waveOutMessage)(c, WCM_DRV_QUERYDEVICEINTERFACESIZE, (DWORD_PTR)&a, 0), WCM_BADDEVICEID);
    EXPECT_EQ(a, WCM_SENT);
    EXPECT_EQ(U(waveOutMessage)(c, WCM_UNKNOWN_MSG, 0, 0), WCM_BADDEVICEID);
    EXPECT_EQ(U(waveOutMessage)(c, WCM_WODM_GETPITCH, (DWORD_PTR)&a, 0), WCM_BADDEVICEID);
    EXPECT_EQ(a, WCM_SENT);
}

TEST(wcm_msg_closed_wavein_handle_is_an_id)
{
    REQUIRE(U(waveInMessage));
    if (!wcm_nin()) { t_skip("no waveIn device"); return; }
    HWAVEIN hi;
    EXPECT_EQ(wcm_open_in(&hi, 0), 0);
    if (!hi) return;
    EXPECT_EQ(U(waveInClose)(hi), 0);
    if (wcm_id_class((ULONG_PTR)hi, wcm_nin(), 0) != WCM_ID_BAD) { t_skip("closed handle truncates to a live id"); return; }
    DWORD a = WCM_SENT;
    EXPECT_EQ(U(waveInMessage)(hi, WCM_DRV_QUERYDEVICEINTERFACESIZE, (DWORD_PTR)&a, 0), WCM_BADDEVICEID);
    EXPECT_EQ(a, WCM_SENT);
}

/* On a live handle, the WODM_GET* messages answer exactly like the APIs (same driver path, dw2 = 0). */
TEST(wcm_msg_handle_unknown_and_queries_record)
{
    REQUIRE(U(waveOutMessage));
    if (!wcm_nout()) { t_skip("no waveOut device"); return; }
    UINT ids[2] = { WAVE_MAPPER, 0 };
    for (int i = 0; i < 2; i++) {
        HWAVEOUT h;
        EXPECT_EQ(wcm_open_out(&h, ids[i]), 0);
        if (!h) continue;
        const char *tag = i ? "dev0" : "mapper";
        MMRESULT r = U(waveOutMessage)(h, WCM_UNKNOWN_MSG, 0, 0);
        t_note("OBS handle %s msg 0x7FFF r=%u", tag, r);
        DWORD sz = WCM_SENT;
        r = U(waveOutMessage)(h, WCM_DRV_QUERYDEVICEINTERFACESIZE, (DWORD_PTR)&sz, 0);
        t_note("OBS handle %s ifsize r=%u v=0x%lx", tag, r, (unsigned long)sz);
        DWORD v = WCM_SENT, va = WCM_SENT;
        MMRESULT rm = U(waveOutMessage)(h, WCM_WODM_GETVOLUME, (DWORD_PTR)&v, 0);
        MMRESULT ra = U(waveOutGetVolume)(h, &va);
        t_note("OBS handle %s getvolume msg r=%u v=0x%lx api r=%u v=0x%lx", tag, rm, (unsigned long)v, ra, (unsigned long)va);
        EXPECT_EQ(ra, WCM_OK);
        MMTIME t; memset(&t, 0, sizeof t); t.wType = TIME_BYTES;
        r = U(waveOutMessage)(h, WCM_WODM_GETPOS, (DWORD_PTR)&t, sizeof t);
        t_note("OBS handle %s getpos r=%u type=%u cb=%lu", tag, r, t.wType, (unsigned long)t.u.cb);
        wcm_close_out(h);
    }
}

/* waveInMessage on a live waveIn handle goes to the driver too. */
TEST(wcm_msg_wavein_handle_record)
{
    REQUIRE(U(waveInMessage));
    if (!wcm_nin()) { t_skip("no waveIn device"); return; }
    HWAVEIN h;
    EXPECT_EQ(wcm_open_in(&h, 0), 0);
    if (!h) return;
    MMRESULT r = U(waveInMessage)(h, WCM_UNKNOWN_MSG, 0, 0);
    t_note("OBS wavein handle msg 0x7FFF r=%u", r);
    DWORD sz = WCM_SENT;
    r = U(waveInMessage)(h, WCM_DRV_QUERYDEVICEINTERFACESIZE, (DWORD_PTR)&sz, 0);
    t_note("OBS wavein handle ifsize r=%u v=0x%lx", r, (unsigned long)sz);
    wcm_close_in(h);
}

/* ---------------------------------------------------------------- waveOutGetID / waveInGetID next to the Message id */

TEST(wcm_getid_contract)
{
    REQUIRE(U(waveOutGetID));
    REQUIRE(U(waveInGetID));
    UINT id = 0xDEADu;
    EXPECT_EQ(U(waveOutGetID)(NULL, NULL), WCM_INVALPARAM);
    EXPECT_EQ(U(waveInGetID)(NULL, NULL), WCM_INVALPARAM);
    for (const wcm_val *v = wcm_nonhandles; v->name; v++) {
        id = 0xDEADu;
        CHECKF(U(waveOutGetID)((HWAVEOUT)v->v, &id) == WCM_INVALHANDLE, "out value %s", v->name);
        EXPECT_EQ(id, 0xDEADu);
        CHECKF(U(waveInGetID)((HWAVEIN)v->v, &id) == WCM_INVALHANDLE, "in value %s", v->name);
        EXPECT_EQ(id, 0xDEADu);
        CHECKF(U(waveOutGetID)((HWAVEOUT)v->v, NULL) == WCM_INVALPARAM, "out NULL value %s", v->name);
    }
}

TEST(wcm_getid_live_handles)
{
    REQUIRE(U(waveOutGetID));
    UINT n = wcm_nout();
    if (!n) { t_skip("no waveOut device"); return; }
    HWAVEOUT h;
    UINT id = 0xDEADu;
    EXPECT_EQ(wcm_open_out(&h, WAVE_MAPPER), 0);
    if (h) {
        EXPECT_EQ(U(waveOutGetID)(h, &id), WCM_OK);
        EXPECT_EQ(id, WAVE_MAPPER);                 /* the id requested at open, not the device picked */
        EXPECT_EQ(U(waveOutGetID)(h, NULL), WCM_INVALPARAM);
        wcm_close_out(h);
    }
    for (UINT d = 0; d < n && d < 4; d++) {
        EXPECT_EQ(wcm_open_out(&h, d), 0);
        if (!h) continue;
        id = 0xDEADu;
        EXPECT_EQ(U(waveOutGetID)(h, &id), WCM_OK);
        EXPECT_EQ(id, d);
        /* the id from GetID addresses the same device through the id path of waveOutMessage */
        DWORD a = 0, b = 0;
        EXPECT_EQ(U(waveOutMessage)((HWAVEOUT)(ULONG_PTR)id, WCM_DRV_QUERYFUNCTIONINSTANCEIDSIZE, (DWORD_PTR)&a, 0), WCM_OK);
        EXPECT_EQ(U(waveOutMessage)((HWAVEOUT)(ULONG_PTR)d, WCM_DRV_QUERYFUNCTIONINSTANCEIDSIZE, (DWORD_PTR)&b, 0), WCM_OK);
        EXPECT_EQ(a, b);
        wcm_close_out(h);
    }
}

TEST(wcm_getid_wavein_live_handles)
{
    REQUIRE(U(waveInGetID));
    UINT n = wcm_nin();
    if (!n) { t_skip("no waveIn device"); return; }
    HWAVEIN h;
    UINT id = 0xDEADu;
    EXPECT_EQ(wcm_open_in(&h, WAVE_MAPPER), 0);
    if (h) {
        EXPECT_EQ(U(waveInGetID)(h, &id), WCM_OK);
        EXPECT_EQ(id, WAVE_MAPPER);
        id = 0xDEADu;
        EXPECT_EQ(U(waveOutGetID)((HWAVEOUT)h, &id), WCM_INVALHANDLE);
        EXPECT_EQ(id, 0xDEADu);
        wcm_close_in(h);
    }
    EXPECT_EQ(wcm_open_in(&h, 0), 0);
    if (h) {
        id = 0xDEADu;
        EXPECT_EQ(U(waveInGetID)(h, &id), WCM_OK);
        EXPECT_EQ(id, 0);
        wcm_close_in(h);
    }
}
