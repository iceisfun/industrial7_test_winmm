# time_basic: evidence

Area: `timeGetTime`, `timeGetSystemTime`, `timeGetDevCaps`, `timeBeginPeriod`, `timeEndPeriod`
(winmm ordinals 141, 140, 139, 137, 138). Files: `devcaps.c`, `systime.c`, `gettime.c`,
`period.c`, `identity.c`, shared helpers in `tb.h`.

Legend: **[decomp]** read in the Win10 19041 reference corpus (kernel32 / winmm), **[VM]** observed
on the local Windows 10 22H2 guest (build 19045.2965) by this corpus, **[knowledge]** documented
Win32 behaviour or header layout. Hard `CHECK`s are only [decomp] or [VM]; environment-dependent
values are `t_note`d.

## Module layout

| fact | source |
|---|---|
| winmm's five exports are `48 FF 25 disp32` (rex.W jmp qword [rip+disp32]) 7-byte thunks | [decomp] winmm!0x3190, 0xa460, 0xa440, 0x1060, 0x1040 asm; [VM] |
| thunk slot lies inside winmm's image and holds kernel32's export of the same name | [decomp] import from api-ms-win-mm-time-l1-1-0; [VM] |
| kernel32 exports the bodies (timeGetTime 0x1c1a0, timeBeginPeriod 0x1e110, timeEndPeriod 0x1d190, timeGetDevCaps 0x22840, timeGetSystemTime 0x6a040) | [decomp] |
| ordinals 137..141 in winmm; timeSetEvent 143 / timeKillEvent 142 are winmm bodies, not exported by kernel32 | [decomp] winmm export table; [VM] |
| winmmbase does not export the five functions | tools/winmmbase_exports.txt (real DLL); [VM] |
| `LoadLibrary("api-ms-win-mm-time-l1-1-0.dll")` + GetProcAddress gives the kernel32 addresses | [VM] |

## TimeInit (kernel32!0x1c0b0, once via InitOnceExecuteOnce)

`NtQueryTimerResolution(&coarsest, &finest, &cur)`; `wPeriodMin = (finest + 9900) / 10000`, at
least 1 (10 if the query fails); `uMax = (coarsest + 9999) / 10000` when `coarsest + 9999 < 550000`,
else 55. Captures `GetTickCount()` and `KUSER_SHARED_DATA.InterruptTime` together (retrying up to
100 times until GetTickCount is stable). **[decomp]** The tests recompute both bounds at run time
from their own `NtQueryTimerResolution` call; nothing hard-codes 1 or 16.

## timeGetDevCaps [decomp kernel32!0x22840]

* `cbtc < 8 || ptc == NULL` -> 97 TIMERR_NOCANDO (not TIMERR_STRUCT, not MMSYSERR_INVALPARAM); the
  buffer is not written.
* otherwise writes exactly 8 bytes `{wPeriodMin, 1000000}` and returns 0; bytes past 8 untouched for
  any cbtc (tested 8..64, 0x100, 0x1000, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF).
* last error untouched.
* [VM] wPeriodMin = 1, wPeriodMax = 1000000.

## timeGetSystemTime [decomp kernel32!0x6a040]

* `cbmmt < 12 || pmmt == NULL` -> 129 TIMERR_STRUCT, nothing written.
* otherwise `u.ms = timeGetTime()`, `wType = TIME_MS` (input wType ignored: TIME_SAMPLES, TIME_SMPTE,
  0, 0xFFFFFFFF ... all come back TIME_MS); only bytes 0..7 written, so `u.smpte.fps/dummy/pad`
  (bytes 8..11) survive.
* `u.ms` lies between timeGetTime() read before and after the call.

## timeGetTime [decomp kernel32!0x1c1a0]

* `(int)((InterruptTime - base) / 10000) + tickBase`: the tests bracket each call with reads of
  `KUSER_SHARED_DATA.InterruptTime` and CHECK that `t - floor(I/10000)` spans at most two adjacent
  values over 5000 samples, and that deltas over a Sleep match the interrupt-time delta within
  +-1 ms.
* monotonic per thread (1000, 200k calls; 4 and 8 threads x 100k) and across threads (a value
  published by one thread is never ahead of a later read in another).
* starts in step with GetTickCount (the bounds used are loose: |diff| < 1000 ms, rate over 300 ms
  within 100 ms; observed deltas are t_noted).
* granularity follows the current timer resolution (t_note only).

## timeBeginPeriod / timeEndPeriod [decomp kernel32!0x1e110 / !0x1d190]

* `uPeriod < wPeriodMin` -> 97 for both (so Begin(0)/End(0) -> 97).
* `uPeriod >= uMax` -> 0 and nothing counted (Begin 0, End 0, End again 0, End without Begin 0;
  tested uMax..100 and 24 large values up to 0xFFFFFFFF, plus wPeriodMax and wPeriodMax+1).
* `[wPeriodMin, uMax)`: a 16-bit count per period, per process. End on a zero count -> 97;
  counts of different periods are independent; a mismatched End does not consume another period.
* the count saturates at 0xFFFF: 65535 Begins succeed, the next -> 97 and is not counted (exactly
  65535 Ends then succeed) [decomp `!= -1` test]; run in a child process.
* counts are process wide (Begin on one thread, End on another) and die with the process (a child
  that leaks Begins does not affect the parent; a child does not see the parent's count).
* winmm and kernel32 entry points share the same counts.
* SRW-locked: 4 threads x 2000 Begin/End pairs, including 0->1 transitions, stay balanced.
* last error untouched.
* resolution: while a period is held `NtQueryTimerResolution` current is not coarser than before
  (CHECK); exact values t_noted, since the global resolution is the finest over all processes.

## VM runs

(filled in below after the runs)
