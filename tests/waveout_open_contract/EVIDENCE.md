# waveout_open_contract: evidence

Primary exports: `waveOutOpen` (winmm ord 173, a jump stub into winmmbase!0x103b0), `waveOutClose`
(winmmbase!0x10800), `waveOutGetID` (winmmbase!0x11c00). Helpers: `ValidateHandle` (!0x11f30).
Files: `wo.h` / `wo_common.c` (helpers, no tests), `ladder.c` (parameter gates, no device needed),
`devices.c` (device ids against the count, zero-device contract), `lifecycle.c` (close / GetID, handle
validation, cross-type handles, threads, close with queued buffers), `callbacks.c` (WOM_OPEN / WOM_CLOSE per
callback type), `formats.c` (formats at open time, QUERY vs real open).

## 1. Decomp-grounded (hard CHECKs, hold in every audio configuration)

waveOutOpen ladder, in this exact order (`wo_gate` / `wo_expect_nodriver` in wo_common.c model it, and
`wo_open_ladder_cartesian_sweep` checks the model on every combination it can decide without a driver):

1. `pwfx == NULL` -> 11, before anything else (also with phwo NULL, bad flags, a bad window); `*phwo` untouched.
2. `dwCallback != 0 && ((fdwOpen >> 16) & 7) == 1 && !IsWindow(dwCallback)` -> 11. Only callback type 1 is
   validated; bit 19 (0x80000) and higher bits are outside the 3-bit field, so 0x90000 / 0xFFF90000 are still
   "window". A NULL window is accepted. Other types are never looked at by winmmbase.
3. `uDeviceID == 0xFFFF` -> WAVE_MAPPER (exact value only; 0xFFFE, 0x1FFFF stay device ids).
4. Mapper: `fdwOpen & 0x7E04`, any other id: `fdwOpen & 0x7E10` -> 10. Only the low word is masked.
5. Without WAVE_FORMAT_QUERY: phwo NULL -> 11, else `*phwo = NULL` (before the device check). With QUERY phwo
   is never read or written.
6. `wTotalWaveOutDevs == 0` -> 2 (every id, mapper and QUERY included).
7. Driver lookup (WAVE_MAPPED selects the mapper driver): failure (id out of range) -> 2.
8. Otherwise WODM_OPEN; the driver's code is returned; on failure the new handle is freed (`*phwo` stays NULL).
   The stored id is the requested one after the 0xFFFF rewrite, so `waveOutGetID` on a mapper or 0xFFFF open
   gives 0xFFFFFFFF, and on a WAVE_MAPPED open of id 0 gives 0.

waveOutClose: `ClientUpdatePnpInfo`, `ValidateHandle(h, 1)` -> 5 (NULL, ids <= 0xFFFF, -1, 0xFFFFFFFF, closed,
other handle types); busy -> 12; WODM_CLOSE: a nonzero driver code is returned and the handle stays open.

waveOutGetID: `puDeviceID == NULL` -> 11 first (even for a bad handle), then `ValidateHandle` -> 5; the out
value is written only on success.

Cross-type: winmm handles share one list with a type tag (1 waveOut, 2 waveIn, 3 midiOut, 4 midiIn, 5 mmio,
8 mixer); waveIn/midiOut/midiIn Close and waveInGetID validate their own type, so an HWAVEOUT gives 5 there and
an HWAVEIN / HMIDIOUT / HMIXER / HMMIO gives 5 to waveOutClose / waveOutGetID, and stays usable.

## 2. VM-observed (Win10 22H2 19045.2965 guest, tools/vm_run.sh)

Configurations: `hda` (QEMU intel-hda + hda-duplex: waveOut 1 "Speakers (High Definition Audio", waveIn 1,
mapper "Microsoft Sound Mapper") and `noaudio` (0 waveOut / waveIn). Every decomp-grounded row of section 1
was confirmed on the guest (the ladder sweep alone checks 4692 calls with HDA).

Observed with HDA and now CHECKed (driver / shared-mode engine answers, not winmmbase logic):

* `*phwo` is NULL after a failed open (id 9999 -> 2, and every refused format), as the decomp says. (An older
  probe line suggesting "handle untouched" read the variable in the same argument list as the call.)
* Ids n (== count), 0xFFFE, 0x10000, 0x7FFFFFFF, 0xFFFFFFFE, 0x80000000 -> 2, QUERY too; a live handle value
  used as the id -> 2. WAVE_MAPPED with id n or 9999 -> 2 (the mapper refuses it), `*phwo` NULL.
* WAVE_MAPPED with id 0 -> 0, GetID 0; mapper and 0xFFFF opens -> GetID 0xFFFFFFFF;
  WAVE_MAPPED_DEFAULT_COMMUNICATION_DEVICE with the mapper -> 0 (open and QUERY).
* Pass-through bits 0x20, 0x40, 0x80, 0x100, 0x8000, 0x80000, 0x100000, 0x01000000, 0x80000000 are ignored by
  the driver: QUERY -> 0 (mapper and id 0), real open of id 0 -> 0.
* Callback types 4, 6, 7 with dwCallback 0: QUERY -> 0 and real open -> 0 (mapper and id 0).
* Same device opened 2 and 8 times concurrently: all 0, distinct handles; parallel open/close on 3 threads;
  close and GetID from another thread; a handle opened on an exited thread stays valid.
* `waveOutClose` with a written 1 s silent buffer still queued -> 33 (WAVERR_STILLPLAYING), the handle stays
  open (GetID 0) and no WOM_CLOSE is sent; after Reset + Unprepare the close is 0 and WOM_CLOSE follows; the
  sequence is WOM_OPEN, one WOM_DONE (dwParam1 = the header), WOM_CLOSE.
* Close with a prepared but never written header -> 0; the header keeps WHDR_PREPARED and Unprepare on the
  closed handle -> 5.
* WOM_OPEN / WOM_CLOSE for CALLBACK_FUNCTION (WOM_OPEN already delivered when waveOutOpen returns, hwo = the
  returned handle, dwInstance verbatim incl. 64-bit values, dwParam1 0), CALLBACK_EVENT (one signal per
  notification), CALLBACK_THREAD (hwnd NULL, wParam = handle), CALLBACK_WINDOW (own thread and another thread's
  window). QUERY opens, failed opens and CALLBACK_NULL with an event value deliver nothing.
* Cross-type: HWAVEIN, HMIDIOUT (GS synth), HMIXER, HMMIO -> 5 from waveOutClose / waveOutGetID (out value
  untouched); HWAVEOUT -> 5 from waveInClose, waveInGetID, midiOutClose, midiInClose, mixerClose.
* Formats (formats.c table, four answers per row): standard PCM 8..32 bit, 1..2 ch, 1000..1000000 Hz and
  IEEE float 32 -> 0 on query; 0 / 3..8 channels, rate 0, bits 0 / 12 / 20, tags 0 / 0x7777 -> 32 on query.
  A real open does not always match the query: bits 64, nBlockAlign 0 or 3, float 64 are accepted by QUERY but
  refused by a real open (mapper 32, device 0 -> 11 or 32), rate 1000000 opens through the mapper but not on
  the device (32), and a device-0 real open of an unsupported PCM layout answers **11** where the query answers
  32. nAvgBytesPerSec 0 or wrong and PCM cbSize garbage are accepted. A-law / mu-law: mapper 0 (ACM), device 32,
  mapper + WAVE_FORMAT_DIRECT 32. WAVE_FORMAT_EXTENSIBLE (PCM 16/24-in-32, float, 5.1, mask 0) -> 0; with
  cbSize < 22 the mapper says 32 and the device 11.

Zero devices (`noaudio`, waveOut 0 / waveIn 0 / mixer 0): every gate code of section 1 is unchanged; every
open that passes the gates -> 2 for ids 0, 1, 2, 7, 0xFFFF, WAVE_MAPPER, 9999, 0xFFFE, 0x10000, 0x7FFFFFFF,
0xFFFFFFFE, with QUERY too, with WAVE_MAPPED on ids and 0x10 on the mapper, and for any format (6 ch, rate 0,
tag 0x7777, all-zero); a non-QUERY failed open leaves `*phwo` NULL (decomp step 5 happens before the device
check), a QUERY leaves it untouched; no callback or event fires. The cartesian sweep checks 5100 calls.

Note for the corpus: `tests/core_smoke/smoke.c` (smoke_waveout_tone) expects the handle UNTOUCHED (1) after
the zero-device `waveOutOpen(WAVE_MAPPER)` failure; decomp and this noaudio run show it is set to NULL.

Runs: hda boots `woc-hda-*` (99 tests, the format table then pinned) and `woc-hda2-*`; noaudio boots
`woc-noa-*` and `woc-noa2-*` (all PASS, see the final status in the commit that adds this file).

## 3. Knowledge, not CHECKed

* Why the device-0 real open answers 11 for formats the query calls 32 is inside wdmaud.drv / the audio
  engine (not in the decomp corpus); the table pins the answers, not a mechanism.
* The 24/32-bit PCM and high-rate acceptance comes from the shared-mode engine and is specific to this
  endpoint (48 kHz float mix format); other hardware may answer differently.
