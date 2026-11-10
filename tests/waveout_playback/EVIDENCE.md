# waveout_playback: evidence

Area scope: `waveOutPause`, `waveOutRestart`, `waveOutReset`, `waveOutBreakLoop`, `waveOutGetPosition` (winmm.dll
exports; the code is in winmmbase.dll), plus the playback protocol around them (WAVEHDR flags, completion order,
loops, `waveOutClose` while playing, concurrent handles, open/close churn).

Files: `contract.c` (no device needed), `position.c`, `pause_restart.c`, `reset_close.c`, `loop.c`, `sequences.c`,
shared helpers `wp.h` / `wp_common.c`. Every device test starts with `WP_NEED_DEV()`: with `waveOutGetNumDevs() == 0`
it checks the no-device open contract (mapper and id 0 -> `MMSYSERR_BADDEVICEID`) and skips.

Three kinds of evidence are used below: **[decomp]** (winmmbase 10.0.19041 in the reference corpus), **[VM]**
(observed on the Win10 22H2 guest, build 19045.2965, QEMU intel-hda + hda-duplex and no-audio configurations) and
**[knowledge]** (documented Win32 contract, used only for checks that the VM also passed).

## Decomp-grounded (winmmbase)

| fact | where |
|---|---|
| `waveOutPause` / `Restart` / `BreakLoop`: `ClientUpdatePnpInfo`, `ValidateHandle(h, 1)` -> 5, then `waveMessage` with WODM 10 / 11 / 0x14 | winmmbase!0x10b10, waveOutRestart, !0x10c10 |
| `waveOutReset`: handle -> 5, WODM_RESET 0x0C; a driver 6 on a "device gone" handle becomes 0 | winmmbase!0x10bb0 |
| `waveOutGetPosition`: `pmmt == NULL` -> 11 **before** the handle check; then handle -> 5; then WODM_GETPOS 0x0D with (pmmt, cbmmt). winmmbase does not look at cbmmt or wType | winmmbase!0x10c60 |
| `ValidateHandle` rejects values <= 0xFFFF, 0xFFFFFFFF, ~0 and anything outside the user VA range without a lookup, otherwise compares `value - 0x50` against its handle list and requires the stored type to match: values are never dereferenced, a live HWAVEIN (type 2) or HMIDIOUT is not a waveOut handle | winmmbase!0x11f30 |
| `waveMessage` returns 6 for a "device gone" handle and 12 for a busy one before calling the driver | winmmbase!0xfa24 |
| PrepareHeader keeps BEGINLOOP/ENDLOOP (`dwFlags &= 0xC` before WODM_PREPARE), so loop flags survive prepare/unprepare | docs/research/02 section 2.3 |

Tests: every `wp_contract_*` test (bad-handle tables for all five functions, NULL pmmt with every bad handle and
cbmmt, MMTIME untouched on the handle failure, closed handle, HWAVEIN / HMIDIOUT cross-type, device ids as handles).
These pass identically with and without an audio device.

## VM-observed (driver behaviour: wdmaud.drv is not in the decomp corpus)

Observed in `results/wp-hda` / `results/wp2-hda`, guest with
1 waveOut ("Speakers (High Definition Audio", mid 1 pid 65535, dwSupport 0x2e), mapper "Microsoft Sound Mapper".

Position (`waveOutGetPosition`):
- Right after open: 0 for TIME_SAMPLES and TIME_BYTES.
- After draining N frames (any split: 1x100 ms ... 32x5 ms): TIME_SAMPLES == N exactly, TIME_BYTES == N x nBlockAlign
  exactly; holds for mapper and device 0 at 8000/8/1, 11025/8/2, 22050/16/1, 44100/16/1, 44100/16/2, 48000/16/2,
  96000/16/2, 44100/24/2, 48000/32/2.
- TIME_SAMPLES and TIME_BYTES are honoured; **every other wType** (TIME_MS 1, TIME_SMPTE 8, TIME_MIDI 0x10,
  TIME_TICKS 0x20, 0, 6, 0x40, 0xFFFF) comes back rewritten to **TIME_BYTES (4)** with the byte count.
- **cbmmt < 12 (sizeof(MMTIME)) -> MMSYSERR_ERROR (1)** for every wType, nothing written (wType and value untouched);
  cbmmt >= 12 -> 0 and exactly 12 bytes written (nothing beyond, even for cbmmt 4096).
- Monotonic while playing (15 distinct values in 17 reads over 300 ms), never above what was queued, always a whole
  number of frames in TIME_BYTES.
- A buffer whose length is not a whole number of frames (+1/+2/+3 bytes): the partial frame is not counted
  (17643 bytes -> 17640 bytes / 4410 samples).
- A zero-length header is accepted by waveOutWrite (0) and completes; it adds nothing to the position.
- Positions are per handle (a second handle opened mid-play starts at 0); an idle drained handle does not drift.

Completion order: DONE observed strictly in submission order while polling (3x100 ms, 32x5 ms), flags go
PREPARED (2) -> PREPARED|INQUEUE (0x12) -> PREPARED|DONE (3). With CALLBACK_FUNCTION: WOM_OPEN, one WOM_DONE per
header with dwParam1 = that header in order, WOM_CLOSE during close.

Pause / Restart:
- Pause and Restart return 0 idle, repeated, and in any order (Restart without Pause is a no-op).
- Paused playback position is frozen exactly (diff 0 frames over 200 ms); TIME_BYTES == TIME_SAMPLES x 4 while
  paused; queued headers stay PREPARED|INQUEUE.
- Pause before any write: written headers stay INQUEUE, position 0, until Restart; then they complete with exact
  totals. Pause after a drain also holds the next write.
- Five pause/restart cycles over a 500 ms buffer: total still exact.
- Pause is per handle: a second handle keeps playing.
- Close while paused with queued headers -> 33; paused with nothing queued -> 0.

Reset:
- Returns every queued header synchronously as PREPARED|DONE (INQUEUE clear) and the position is **0** afterwards
  (also after a full drain). With CALLBACK_FUNCTION all WOM_DONE callbacks (in submission order) have been delivered
  when waveOutReset returns.
- **Reset clears the paused state**: after Pause+Reset a new write plays without Restart (both idle and with a queue).
- Write/Reset ten rounds on one header: always DONE, position 0; the handle plays normally afterwards.

Close: with queued headers `waveOutClose` -> **WAVERR_STILLPLAYING (33)**, the handle stays open and fully usable
(Pause/BreakLoop/GetPosition/Restart, buffers still complete); after Reset (or a drain) and unprepare -> 0; with
done-but-still-prepared headers -> 0. The CALLBACK_EVENT event is signalled by WOM_CLOSE and is not closed by winmm.

Loops:
- BEGINLOOP|ENDLOOP with dwLoops n plays n times (n = 1, 2, 3, 5); **dwLoops 0 plays once**; the header comes back
  with its loop flags and dwLoops unchanged.
- A loop spanning several headers (BEGINLOOP on the first, ENDLOOP on the last) repeats the whole span dwLoops times;
  the count comes from the BEGINLOOP header.
- ENDLOOP alone, or dwLoops without loop flags: plays once.
- **A lone BEGINLOOP header with no ENDLOOP is held** (still INQUEUE long after its play time); Reset returns it.
- waveOutBreakLoop during a loop: the current pass finishes and no further pass is played (6 x 50 ms of a
  30-pass loop, DONE about 47 ms after the call). BreakLoop issued before the loop header starts has no effect on that
  later loop (all 4 passes play). BreakLoop with nothing looping -> 0, no effect.
- Pause during a loop holds it; Restart finishes the remaining passes exactly.

Concurrency and churn: two and four handles play concurrently with exact per-handle positions; Reset on one handle
does not disturb another; Pause/Restart/GetPosition from a second thread works. 20 open/close cycles (also with
play + reset in each): process handle count unchanged (159 -> 159).

No-device guest (`--audio noaudio`): `waveOutGetNumDevs` = 0, `waveOutOpen(WAVE_MAPPER / 0)` = 2, every contract test
passes unchanged; device tests skip after asserting that open contract.

## Knowledge only

None of the hard checks rely on knowledge alone; the slack in `wp_pause_freezes_position` (10 ms of frames) and the
handle-count slack (+4) are deliberate tolerances, the observed values were exact.

## Runs

| run | config | result |
|---|---|---|
| results/wp-hda | hda, first draft (181 tests) | 180/181; the one FAIL (lone BEGINLOOP never completes) became `wp_loop_beginloop_only_is_held` |
| results/wp-noaudio | noaudio, first draft | PASS (116 device tests skipped) |
