# playsound_device: evidence

Area: PlaySound / sndPlaySound with a real wave device (Win10 guest with QEMU intel-hda + hda-duplex) and the same
cases with 0 devices (`--audio noaudio`). Only in-memory or in-scratch WAVE images, silent or about -30 dBFS, always
with SND_NODEFAULT. Files: `ps.h` / `ps_util.c` (helpers), `ps_sync.c`, `ps_async.c`, `ps_format.c`, `ps_file.c`,
`ps_threads.c`.

Legend: **[D]** read in the decomp of winmm 10.0.19041 (reference corpus), **[VM]** observed on the Win10 22H2 guest
(build 19045.2965) with this exe, **[K]** general Win32 knowledge, only used where D and VM agree.

## 1. Code path [D]

`PlaySoundW` and `sndPlaySoundW` call `sndPlaySoundI(psz, hmod, f)` directly; `PlaySoundA` converts the name only when
`psz > 0xFFFF` and (SND_MEMORY clear or SND_RESOURCE set), so memory images pass through untouched; `sndPlaySoundA` is
`PlaySoundA(psz, NULL, f)`. `PlaySound` (and unnamed ordinal 2) is the same code as `PlaySoundA`.

`sndPlaySoundI` order:

1. `f & 0xFF80DF20` -> FALSE.
2. `f & SND_NOSTOP` and a header is open (global `lpWavHdr != 0`) -> FALSE. Read without a lock, before anything else
   (also before the NULL stop).
3. SND_ASYNC -> `CreatehwndNotify` (window thread `mciwindow`, class from `CreateMMClass`); if that fails ASYNC is
   dropped.
4. `psz == NULL` with SND_ASYNC|SND_MEMORY -> ASYNC dropped (synchronous stop).
5. SND_MEMORY or NULL: with ASYNC, image must have 'RIFF' at 0 and 'WAVE' at 8, size = RIFF length + 8 must not wrap
   below 8; size in [1, 0x1FFFFF] -> LocalAlloc copy (flag 0x40000000, freed by soundFree), else used in place.
6. Names: an empty name returns TRUE before `sndMessage` (no stop, no post).
7. Synchronous: post 0x3CD (stop marker) to the notify window if it exists, then `sndMessage` on the calling thread.
   Asynchronous: `PostMessageW(hwndNotify, 0x3CC, f, buffer)`, return value = PostMessage result.

`mmWndProc(0x3CC)`: `PeekMessage(0x3CD..0x3CD, PM_NOREMOVE)`; if a stop is queued the play is dropped, else
`sndMessage`. `0x3BD` (MM_WOM_DONE) -> `WaveOutNotify` -> `soundClose` when WHDR_DONE.

`sndMessage` holds `SoundCritSec` for the whole call: NULL -> `soundFree` + TRUE; memory -> `soundFree` then
`soundLoadMemory` (parse) then `soundPlay`. `soundPlay` -> `soundClose`, session check, `soundOpen`, and for a sync
play `soundWait` (75 ms sleeps until WHDR_DONE or 2x the computed length) + `soundClose`.

`soundOpen`: `waveOutOpen(&h, WAVE_MAPPER, fmt, hwndNotify, 0, WAVE_ALLOWSYNC | (hwndNotify ? CALLBACK_WINDOW : 0) |
(SND_SYSTEM ? 0x20 : 0) | (SND_RING ? 0x100 : 0))`; failure -> FALSE. Then prepare, `dwLoops = 0xFFFFFFFF` +
BEGINLOOP|ENDLOOP only when SND_ASYNC and SND_LOOP are both set, write.

`soundInitWavHdr` (memory: size bound 0xFFFFFFFF; file: real file size): see the header comment of `ps_format.c` for
the full rule list (fmt cb >= 14, cb >= 18 strictness, PCM/float avg rule, EXTENSIBLE rules, chunk skip rule
`next offset >= RIFF length -> fail`, no data-vs-RIFF clamp, in-place memmove of a misaligned fmt body).

## 2. Results

(filled from the VM runs below)
