# mme_handles_threads: evidence

Area scope: MME handle validation and lifetime across every handle type (waveOut, waveIn, midiOut, midiIn, mmio,
mixer, midiStream), handles across threads, leak checks, the first-use device-count race, and the winmmbase WOW
handle-value table. Files:

| file | content |
|---|---|
| `mh.h`, `mh.c` | op tables, never-valid value table, device gating, mmio memory files, live-handle "zoo" |
| `per_op.c` | one TEST per handle-taking entry point (73): never-valid values, freed value, live HMMIO |
| `wave_handles.c` | waveOut/waveIn op tables, parameter-before-handle ladders, closed/double close, cross types |
| `midi_handles.c` | midiOut/midiIn/midiStream tables, stream handle in midiOut calls, single-open synth |
| `mmio_mixer_handles.c` | mmio and mixer tables, chunk helper order, mixer object-type resolution, WOW table |
| `threads_lifetime.c` | cross-thread open/use/close, 4-thread concurrent play, close vs GetPosition, reset from another thread, leaks, child first-use race |
| `list_integrity.c` | 64 live handles with removals from the middle, WOW lookups over a long list, callback handle identity, caps through a handle |

Device gating: every device test reads `mh_devs()` (the `*GetNumDevs` counts, read once, never timed) and runs the
device half only when the count is nonzero; the zero-device half asserts the contract (BADDEVICEID, NULLed out
handle). HMMIO memory files are the always-available live handle of another type.

## 1. Decomp-grounded (winmmbase 10.0.19041, reference corpus)

* `ValidateHandle` (winmmbase!0x11f30): rejects `<= 0xFFFF`, `-1`, `0xFFFFFFFF`, outside the user VA range, then
  walks `pHandleList` under `HandleListCritSec` comparing `h - 0x50`; type at `h - 0x48`. No dereference of a value
  that is not on the list.
* `NewHandle` (0x11c48): header of 0x50 bytes zeroed, type at +8, creating thread id at +0x10, inserted at the
  list HEAD. `FreeHandle` (0x11dc8): unlinks, zeroes type/flags, frees.
* Accepted types per entry point: `midiOutClose`, `midiOutGetID` -> 3 only; `midiOutShortMsg`, `midiOutLongMsg`,
  `midiOutReset`, `midiOutPrepareHeader`, `midiOutUnprepareHeader`, `midiOutCachePatches`,
  `midiOutCacheDrumPatches`, `midiOutMessage`, `midiOutGetVolume` -> 3 or 9; `midiInSetThru` (midiConnect /
  midiDisconnect) source -> 4 or 3, target -> 3; all waveOut* -> 1, waveIn* -> 2, mmio* -> 5, `mixerClose` -> 8,
  `midiStream*` -> 9.
* `midiOutLongMsg` on a stream handle -> 8 (MMSYSERR_NOTSUPPORTED) after the header checks;
  `midiOutPrepareHeader(stream, cb 44)` -> 11 (stream needs cbmh >= 0x70).
* Check order: `waveOutGetID/GetPosition/GetPitch/GetPlaybackRate` pointer NULL -> 11 first; WAVEHDR front check
  (NULL, cb < 48, flags & 0xFFFFCFE0) -> 11 first; Prepare(already PREPARED) -> 0 and Unprepare(not PREPARED) -> 0
  without a handle check; Write: not PREPARED -> 34, INQUEUE -> 33, then the handle. waveOutPrepareHeader validates
  the handle before masking dwFlags (a rejected header keeps its flags); `waveInPrepareHeader` and
  `midiInPrepareHeader` zero dwFlags BEFORE the handle check. midiOutPrepare/Unprepare: handle FIRST.
  midiInAddBuffer: header, 64, 65, then handle. midiStream*: handle first (Position(bad, NULL) -> 5,
  Property(bad, NULL, 0) -> 5, Out(bad, NULL) -> 5).
* mmio: `mmioClose/GetInfo/SetInfo/Flush/Advance/SetBuffer` -> 5 (handle before the info pointer);
  `mmioRead/Write/Seek` -> -1; `mmioSendMessage` -> 0; `mmioCreateChunk` / `mmioAscend` fail in their internal
  `mmioSeek` -> 263; `mmioAscend(NULL, dirty)` and `mmioDescend(NULL)` -> 258; Ascend with a negative data offset
  -> 272. `mmioDescend` reads handle fields before validation, so only NULL is used as a bad handle there.
* `waveOutClose` / `midiOutClose` / `waveInClose` / `midiInClose`: set busy bit 2 inside the per-handle lock, call
  the driver, then `FreeHandle`; `waveMessage` returns 12 when the busy bit is set. Hence a poller racing a close
  can see 0, 12 or 5.
* `waveOutMessage` / `waveInMessage` / `midiOutMessage` / `midiInMessage`: a non-handle is a device id;
  `>= count` (and not 0xFFFFFFFF) -> 2. `mixerMessage`: non-handle -> `IMixerMessageId` (bad id -> 2); a live
  HMIXER with uMsg < 0x4000 -> 11.
* `IMixerGetID` / `IMixerGetIDFromEndpoint`: flags & 0x0FFFFFFF -> 10; NULL out -> 11; writes -1 first;
  MIXER/HMIXER: value < mixer count is an id, else must be a live HMIXER (-> 5 for HMIXER, 2 for MIXER);
  HWAVEOUT/HWAVEIN/HMIDIOUT go through the matching GetID (-> 5); MIDIIN, HMIDIIN and undefined nibbles -> 11.
  The same resolver runs in mixerGetLineInfoW / GetLineControlsW / GetControlDetailsW after their own checks.
* `winmmbaseHandle32FromHandle16` (0xf000): walks the list from the head comparing the 16-bit WOW value at
  `h - 0x38`; returns the LOW 32 BITS of the first match, 0 if none. Fresh handles carry 0.
  `winmmbaseGet/SetWOWHandle` read/write that slot without validation (tests call them only on live handles).
  Not exported by winmm.dll.

## 2. VM-observed (Win10 22H2 guest, tools/vm_run.sh, hda and noaudio)

(filled from the runs below)

## 3. Knowledge / notes only

* Address reuse after a close is recorded with t_note (`mh_list_address_reuse_note`), not asserted beyond the
  consistent outcome (reused -> valid again, otherwise 5).
* Process handle counts across 200 mmio and 50 waveOut cycles: CHECK within slack (2 / 16), exact deltas noted.
