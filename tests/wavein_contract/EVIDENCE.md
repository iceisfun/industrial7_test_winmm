# wavein_contract: evidence

Area: argument, header and handle contract of the waveIn API in winmm.dll (implementation in winmmbase.dll,
Windows 10 19041 / 19045). Primary exports, each called through `U()`: waveInOpen, waveInClose,
waveInGetDevCapsA, waveInGetDevCapsW, waveInPrepareHeader, waveInUnprepareHeader, waveInAddBuffer, waveInStart,
waveInStop, waveInReset, waveInGetPosition, waveInGetID, waveInMessage.

Files: `open.c` (waveInOpen ladder), `caps.c` (GetDevCapsA/W), `header.c` (WAVEHDR protocol), `handle.c`
(invalid / foreign / closed handles, Message as device id), `device.c` (real-handle state on a device),
`wic_common.c` + `wic.h` (gating and helpers).

Gating: `waveInGetNumDevs` is read once (never timed). The contract half runs everywhere; device tests
`t_skip` when the count is 0. Foreign handles: a memory HMMIO (always available, lives in the same winmmbase
handle list as type 5), an HWAVEOUT (when a render device exists), an HMIDIOUT (when `midiOutOpen(0)` succeeds).

Legend: **D** = read from the winmmbase decomp (RVA given), **VM** = observed on the local Win10 22H2 guest
(build 19045.2965, QEMU intel-hda + hda-duplex = `hda`, no audio device = `noaudio`, render-only `hda-out`),
**K** = general Win32 knowledge, only used where D or VM also agree.

## Decomp-grounded (D, all confirmed on the VM)

| Behaviour | Source |
|---|---|
| waveInOpen: pwfx NULL -> 11 first (even with bad flags, NULL phwi, bad window); `*phwi` untouched | D 0x11290, VM |
| CALLBACK_WINDOW (`(fdwOpen>>16 & 7) == 1`, so 0x90000 counts) with a non-window -> 11, also with QUERY, before flags | D, VM |
| NULL window with CALLBACK_WINDOW passes; THREAD/FUNCTION/EVENT/types 4..7 not validated | D, VM |
| id 0xFFFF -> WAVE_MAPPER before the flag mask; mask 0x7E04 for the mapper, 0x7E10 for ids -> 10, `*phwi` untouched | D, VM |
| phwi NULL -> 11 unless WAVE_FORMAT_QUERY; otherwise `*phwi = NULL` before the device check | D, VM |
| QUERY never writes `*phwi` | D, VM |
| 0 waveIn devices -> 2 for every id / QUERY / format (format never inspected), no callback fired | D, VM noaudio |
| bad id (>= count, 0xFFFE, 0x10000, handle value) -> 2 and `*phwi` NULL | D, VM |
| GetDevCaps: cb 0 -> 0 (no pointer/id check); NULL -> 11; HWAVEIN accepted as id; no 0xFFFF rewrite (0xFFFF -> 2); id truncated to 32 bits; foreign / closed handle -> 2 | D 0x11000 / 0x110f0, VM |
| GetDevCapsA copies min(cb, 0x60) bytes from a zeroed WAVEINCAPS2W conversion | D 0x110f0, VM |
| Header front check (NULL, cb < 0x30, flags & 0xFFFFCFE0) -> 11 before anything else in Prepare/Unprepare/AddBuffer | D 0x11760/0x11800/0x118c0, VM |
| Prepare: PREPARED -> 0 with no handle check; else `dwFlags = 0` (every bit) **before** the handle check, so a bad handle returns 5 with dwFlags 0; success -> exactly PREPARED (0x2) | D, VM |
| Unprepare: INQUEUE -> 33 (even without PREPARED); not PREPARED -> 0 without handle check; PREPARED + bad handle -> 5 | D, VM |
| AddBuffer: not PREPARED -> 34 (even with INQUEUE); INQUEUE -> 33; bad handle -> 5 | D, VM |
| Start/Stop/Reset/Close: handle check (type 2) -> 5 for NULL, small ints, 0xFFFFFFFF, -1, foreign and closed handles | D, VM |
| GetPosition: pmmt NULL -> 11 before the handle; GetID: NULL -> 11 before the handle, no driver call, returns the id passed to open (mapper / 0xFFFF / mapper+0x10 -> 0xFFFFFFFF) | D 0x11a60/0x11bb0, VM |
| waveInMessage: a non-waveIn value is a device id (low 32 bits); out of range -> 2; so foreign/closed handles -> 2, not 5 | D 0x10f70 + waveIDMessage, VM |
| Mapper internal messages: DRVM_MAPPER_PREFERRED_GET / CONSOLEVOICECOM_GET with a NULL pointer -> 11, on a plain device id -> 11; DRV_QUERYDEVICEINTERFACESIZE NULL -> 11; DRV_QUERYDEVICEINTERFACE short buffer -> 21 (MOREDATA) | D mregHandleInternalMessages, VM |

## VM-observed (driver / environment; pinned on this guest)

- `hda`: waveIn 1 "Line In (High Definition Audio " (wMid 1, wPid 65535, dwFormats 0xFFFFF, 2 ch); mapper
  "Microsoft Sound Mapper" (wMid 1, wPid 2, vDriverVersion 0x500). Mapper handle caps -> mapper name.
- Pass-through open flags (0x2, 0x8, 0x10 with the mapper, 0x20, 0x40, 0x80, 0x100, 0x8000) with QUERY -> 0;
  ALLOWSYNC / FORMAT_DIRECT real opens -> 0; WAVE_MAPPED with id 9999 -> 2.
- Format queries: PCM 8000/8/1 .. 48000/16/2 -> 0 on the mapper and id 0; tag 0x1234, 6 channels, 0 channels,
  0 Hz -> 32 (WAVERR_BADFORMAT); a real open with a bad tag -> 32 and `*phwi` NULL.
- Two concurrent opens of the mapper -> both 0. CALLBACK_EVENT signalled on open and close; never for a query.
- GetDevCapsW: the driver rejects cb < 80 (sizeof WAVEINCAPSW) with 11 and writes nothing; cb >= 80 -> 0.
- Prepare of a zero-length / NULL-data buffer -> 0; Prepare with cb 112 -> 0; Unprepare through another valid
  HWAVEIN -> 0.
- AddBuffer on a stopped handle: dwFlags 0x12 (DONE cleared), dwBytesRecorded 0; second add 33, Unprepare 33,
  Close 33 (handle stays open); Reset -> buffer back 0x3 with 0 bytes; Unprepare -> 0x1; Close -> 0.
- Start twice -> 0, Stop/Reset idle -> 0, Close while started (no buffers) -> 0, double close -> 5.
- GetPosition: cbmmt < 12 -> 1 (MMSYSERR_ERROR); only TIME_SAMPLES and TIME_BYTES are honoured, every other
  wType (MS, SMPTE, MIDI, TICKS, 0, 0x77) comes back as TIME_BYTES; position 0 after Reset.
- Messages through an open handle go straight to the driver: DRV_QUERYDEVICEINTERFACESIZE and a user message
  -> 8 (NOTSUPPORTED). Through id 0: interface size 264, string `\\?\hdaudio#...\elineinwave`.
- PREFERRED_GET / CONSOLEVOICECOM_GET on the mapper -> 0, id 0 (flags noted only).
- `noaudio` / `hda-out`: see the run notes below.

## Knowledge only

None of the CHECKs rests on knowledge alone; t_note lines record device names, flags of the preferred-device
messages and the recorded byte counts.

## Runs

See the section appended after the VM runs.
