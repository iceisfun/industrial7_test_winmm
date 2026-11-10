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

(filled from the runs below)

## 3. Knowledge / recorded only (t_note, not CHECKed)

Rows printed as `OBS ...` are driver (wdmaud / shared-mode engine / ACM) answers recorded for the record:
pass-through flag bits 0x20..0x100, 0x8000 and high bits, callback types 4/6/7 without QUERY, odd formats
(rate/bits/channels 0, tags 0 / 0x7777, A-law / mu-law, extensible cbSize variants), WAVE_MAPPED with an
out-of-range id.
