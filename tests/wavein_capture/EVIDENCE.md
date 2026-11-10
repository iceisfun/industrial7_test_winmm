# wavein_capture: evidence

Area: the waveIn capture path (open, buffer queue, Start/Stop/Reset/Close, position, callbacks, formats) plus the
zero-device / out-of-range-id contract, which runs on every guest.

Files: `wic.h` / `wic_common.c` (helpers, device gate), `wic_nodev.c` (contract half, no device needed),
`wic_stream.c` (queue protocol, start/stop/reset/close), `wic_position.c` (waveInGetPosition),
`wic_callback.c` (FUNCTION / EVENT / THREAD / WINDOW / NULL delivery), `wic_formats.c` (format tables, one short
real capture per format).

Gating: `wic_ndev()` reads `waveInGetNumDevs` once (never timed) and notes the count and caps names. Device tests
skip with `NEED_DEV()` when it is 0. Recording per test is at most about 1 s; every wait is bounded (3 s for the
first buffer of a fresh stream).

Tags in the sources: **[decomp]** = winmmbase decompilation (reference corpus, winmmbase 10.0.19041),
**[VM]** = observed on the Win10 22H2 guest (build 19045.2965) with `tools/vm_run.sh`, everything else = documented
Win32 contract checked on the VM by the same run (all CHECKs passed there).

## Decomp-grounded (winmmbase)

| item | source |
|---|---|
| waveInOpen ladder: pwfx NULL -> 11; bad CALLBACK_WINDOW -> 11; flag masks 0x7E04 (mapper) / 0x7E10 (id) -> 10; phwi NULL -> 11 unless QUERY; `*phwi = NULL` before the device check; 0 devices -> 2; 0xFFFF == WAVE_MAPPER | waveInOpen 0x11290 (same shape as waveOutOpen 0x103b0, docs/research/02 2.1) |
| WAVE_FORMAT_QUERY ignores phwi completely (not zeroed) | same |
| waveInAddBuffer: NULL / cbwh < 0x30 / flags & 0xFFFFCFE0 -> 11, not PREPARED -> 34, INQUEUE -> 33, then handle -> 5, then WIDM_ADDBUFFER 0x38; DONE is not cleared by winmmbase | waveInAddBuffer 0x118c0 |
| waveInUnprepareHeader: INQUEUE -> 33, not PREPARED -> 0 without a handle check, success clears only PREPARED | waveInUnprepareHeader 0x11800 |
| waveInPrepareHeader: already PREPARED -> 0 without a handle check | 02 2.3 |
| waveInGetPosition: pmmt NULL -> 11 before the handle; handle -> 5; cbmmt goes to the driver (WIDM_GETPOS 0x3C) | waveInGetPosition 0x11a60 |
| waveInStart/Stop/Reset: handle -> 5 then forward (0x39 / 0x3A / 0x3B) | waveInStart 0x11960 |
| waveInClose: handle -> 5; busy -> 12; a nonzero driver answer is returned and the handle stays open | waveInClose 0x11650 |
| waveInGetID returns the stored requested id (WAVE_MAPPER for mapper opens) | 02 2.4 |
| DriverCallback dispatch (WINDOW PostMessage, THREAD PostThreadMessage, FUNCTION direct call, EVENT SetEvent) | DriverCallback 0x5560 |

## VM-observed (hda = intel-hda + hda-duplex; noaudio; hda-out)

Device: waveIn 0 "Line In (High Definition Audio " (wMid 1, wPid 65535, 2 ch, dwFormats 0xFFFFF); mapper
"Microsoft Sound Mapper" (wMid 1, wPid 2).

Stream protocol:
- Prepare -> flags exactly PREPARED (DONE/BEGINLOOP/ENDLOOP cleared), dwBytesRecorded left as it was.
- AddBuffer -> flags 0x12 and **dwBytesRecorded zeroed**; re-adding a DONE buffer without re-preparing -> 0x12
  (DONE cleared by the driver).
- Buffers are not filled before Start; Start with no buffers then AddBuffer works; Start twice -> 0.
- First 250 ms buffer (44.1 kHz/16/1 through the mapper) DONE after about 500 ms; full buffers come back 0x3
  with dwBytesRecorded == dwBufferLength, strictly in AddBuffer order.
- **Stop mid-buffer returns the partial buffer DONE** (e.g. 6314 of 88200 bytes, block aligned); buffers behind it
  stay 0x12; Stop twice / without Start -> 0; Start after Stop resumes and the position continues.
- Reset -> 0 returns every queued buffer DONE before it returns (unstarted ones with 0 bytes), position -> 0, the
  stream is stopped until the next Start.
- Close with queued buffers (started or not, or after Stop with buffers still queued) -> 33 (WAVERR_STILLPLAYING),
  the handle stays usable, no WIM_CLOSE; after Reset -> 0. Close with only DONE buffers -> 0.
- A 1001-byte buffer (16-bit mono) is accepted and comes back with dwBytesRecorded 1001.
- A zero-length buffer: Prepare -> 0, AddBuffer -> 0 (0x12), returned DONE with 0 bytes. Whether the buffer queued
  behind it completes is not pinned (it did not within 3 s on the VM; noted only).
- Two simultaneous mapper opens both capture (shared mode).
- `waveInMessage(DRV_QUERYDEVICEINTERFACESIZE)`: on a mapper handle -> 8 (NOTSUPPORTED); on device id 0 -> 1.

Position:
- 0 before Start (also with queued buffers); >= recorded bytes while streaming; frozen after Stop with
  samples * nBlockAlign == bytes for every tested format; 0 after Reset (also while running).
- TIME_BYTES / TIME_SAMPLES honoured; TIME_MS, TIME_SMPTE, TIME_MIDI, TIME_TICKS and unknown types (0, 3, 0x40,
  0x1234) come back as TIME_BYTES with result 0.
- cbmmt < sizeof(MMTIME) (0, 1, 4, 8, 11) -> **1 (MMSYSERR_ERROR)**, MMTIME untouched; >= 12 -> 0.

Callbacks:
- FUNCTION: WIM_OPEN (0x3BE) on the caller thread before waveInOpen returns, one WIM_DATA (0x3C0) per buffer with
  dwParam1 = the WAVEHDR (already DONE|PREPARED, dwBytesRecorded final) on a driver thread, WIM_CLOSE (0x3BF)
  before waveInClose returns; dwInstance passed through, dwParam2 0. Reset of unstarted buffers delivers one
  WIM_DATA each with 0 bytes. A query or a failed open delivers nothing.
- EVENT: signalled for open, each buffer, Reset returns, and close; not for Prepare/AddBuffer.
- THREAD and WINDOW: MM_WIM_OPEN / MM_WIM_DATA (lParam = header) / MM_WIM_CLOSE with wParam = the handle.
- CALLBACK_WINDOW with a NULL hwnd opens and records with no messages; a destroyed hwnd -> 11 on every guest.

Formats:
- QUERY, DIRECT_QUERY (mapper and device 0) and real 2 x 40 ms captures succeed for PCM 8/16-bit, 1/2 ch, 8 to
  96 kHz, IEEE float 32-bit, and EXTENSIBLE PCM/float.
- Unknown tag 0x9999 -> 32 on both; 6-channel PCM and EXTENSIBLE with cbSize 0 -> 32 through the mapper but
  **11 (INVALPARAM) on device 0**; channels 0 / rate 0 / bits 12 / bits 0 -> 32 on both; nBlockAlign 3 and rate
  1000 are accepted (0).
- Captured silence: 16-bit samples in [-1, 1], 8-bit in [127, 129], float within [-1, 1] (noted, not asserted
  beyond "the engine wrote real samples").

No device (noaudio) and output-only (hda-out): every open / QUERY on WAVE_MAPPER, 0xFFFF, 0, 1, 9999 -> 2 with
the handle zeroed (QUERY: untouched) and no callback for any callback type.

## Knowledge (documented contract, confirmed by passing CHECKs)

Flag values (DONE 1, PREPARED 2, INQUEUE 0x10), message ids, MMTIME layout (12 bytes), error-text availability for
every capture result code.
