# time_events: evidence

Area: `timeSetEvent` (winmm ordinal 143, body winmm!0x2a20) and `timeKillEvent` (ordinal 142, winmm!0x2e50), with
their helpers `timeThreadSetEvent` (0x2b90), `timeSetTimerEvent` (0x2c90), `timeThread` (0x26c0), `TimeInit`
(0x23c0), `TimeInitThread` (0x2d7c), and kernel32 `timeBeginPeriod` (0x1e110) / `timeEndPeriod` (0x1d190), which
the winmm exports reach. Decomp source: the Win10 reference corpus API (winmm 10.0.19041.546). Oracle: the local
Win10 22H2 VM (build 19045.2965), runs in `hda` and `noaudio` (the area does not depend on audio devices).

Legend: **D** = read in the decomp/asm, **V** = observed on the VM (hard CHECK passes there), **K** = documented API
knowledge only (used for t_note, never for a hard CHECK unless also D or V).

## Argument validation (te_params.c)

| fact | basis |
|---|---|
| `fuEvent & 0xFFFFFE0E` != 0 -> 0; the accepted bits are exactly 0x1F1 (bits 0, 4, 5, 6, 7, 8) | D, V |
| `uDelay` must be in [wPeriodMin, 1000000]; 0, 1000001, 0x80000000, 0xFFFFFFFF -> 0; 1000000 accepted | D, V |
| `uResolution` never fails a call (0, huge, larger than uDelay) | D, V |
| fptc is not validated: NULL function callback accepted, nothing called; NULL event handle harmless | D, V |
| rejected calls: no slot, no generation bump, no period reference, LastError untouched | D, V |

## Ids and the slot table (te_ids.c, te_child.c)

| fact | basis |
|---|---|
| 16 slots per process; the 17th live event -> 0; a full-table failure does not bump the generation | D, V |
| id = 16-bit generation (+0x10 per successful allocation, 0 skipped) + lowest free slot | D, V |
| fresh process: first id 0x10; 16 back-to-back: 0x10, 0x21, 0x32, ... 0x10F; after a kill of slot 5: 0x115 | D, V (child) |
| generation wraps 0xFFF0 -> 0x10 (child, 4104 allocations) | D, V (child) |
| timeKillEvent: slot(id & 0xF).id must equal id, else 97; high-bit / other-generation variants -> 97 and the live event survives | D, V |
| before the first timeSetEvent any id (including 0) -> 97 | D, V (child) |
| timeKillEvent(0) after slot 0 was used and freed -> 0 (a free slot holds id 0); with slot 0 live -> 97 | D, V (child only) |
| kill twice: 0 then 97; kill of a fired one-shot (all callback kinds) -> 97 | D, V |
| an event may be killed from any thread and armed from a thread that has since exited | D, V |

## Timer period reference (te_period.c)

kernel32 counts per period for wPeriodMin <= p < uMax (uMax = ceil(coarsest resolution) = TimerData, 16 on the VM)
and is a no-op above; so `timeEndPeriod(p)` == 0 while an event holds p is observable (every probe is restored).

| fact | basis |
|---|---|
| period = TimerData if uResolution > TimerData, else max(uResolution, wPeriodMin); uDelay < period -> TimerData | D, V (table over delays x resolutions) |
| the event holds one reference from arm until kill / one-shot completion; two events on p hold two | D, V |
| a full-table failure gives its period back; kill of an unknown id releases nothing | D, V |
| a one-shot that kills itself in its callback releases its period once | D, V |

## Dispatch (te_callback.c, te_events.c)

| fact | basis |
|---|---|
| function callback `fptc(id, 0, dwUser, 0, 0)`; dwUser passed as a full 64-bit value | D (asm: rdx=0, r9=0, [rsp+0x20]=0), V |
| one shared timer thread in the process, not the caller, THREAD_PRIORITY_TIME_CRITICAL (SetThreadPriority 15), created lazily by the first timeSetEvent | D, V |
| dispatches are serial (one callback at a time; a blocked callback delays other due events) | D, V |
| fuEvent & 0xF0: 0 function, 0x10 SetEvent, 0x20 PulseEvent, anything else (0x30, 0x40, 0x80, ...) dispatches nothing but the event still runs/finishes | D, V |
| SetEvent/PulseEvent run under the timer lock in the same hold that frees a one-shot, so a woken waiter's kill is already 97 | D, V |
| PULSE wakes blocked waiters (all of them on a manual event) and never leaves the event signalled | D (dispatch), K (PulseEvent), V |
| timeSetEvent/timeKillEvent are callable from inside a callback; killing self (one-shot or periodic, with or without TIME_KILL_SYNCHRONOUS) -> 0, no further ticks, no deadlock | D, V |
| TIME_KILL_SYNCHRONOUS: a kill from another thread waits for that event's running callback; without the flag it returns while the callback still runs; killing a different event never waits | D, V |
| the timer thread waits alertably (NtWaitForMultipleObjects Alertable=TRUE): a queued user APC runs on it | D (asm r9b=1), V |
| after timeKillEvent returns (sync flag or event kinds), the event is never dispatched again | D, V |

## Timing (te_timing.c, loose bounds only)

| fact | basis |
|---|---|
| one-shot: relative NT timer of uDelay ms; not early (>= delay - 2 ms), bounded lateness | D, V (values in NOTE lines) |
| periodic: absolute schedule (arm time + k * uDelay), so 25 ticks at 20 ms end near 500 ms | D, V |
| a late dispatch arms the NT timer with 0: missed ticks are caught up back to back after a stall | D, V |
| counts: 20 ms over 1 s in [25, 75], 10 ms in [50, 150] | V (loose) |

## VM runs

Both boots (2026-11-10, build/time_events.exe from this tree): **PASS**, 134 tests, 2879 checks, 0 failures,
0 skips, ~21.4 s per run, no timeout.

| mode | guest devices | result |
|---|---|---|
| hda | waveout 1, wavein 1, mixer 2 | PASS tests=134 checks_pass=2879 |
| noaudio | waveout 0, wavein 0, mixer 0 | PASS tests=134 checks_pass=2879 |

Observed values (NOTE lines, identical within 1 ms in both modes): wPeriodMin 1, uMax/TimerData 16; timer thread
priority 15; one-shots 10/30/50/100/250 ms fired after 10/30/50-51/100/250 ms; one-shot 40 ms with a TimerData
period fired after 46-47 ms; periodic 20 ms -> 50 callbacks/s, 10 ms -> 100-101, 1 ms -> 501 in 500 ms, 50 ms with a
coarse period -> 20; tick 25 at 20 ms landed at 500-501 ms (drift <= 1 ms); after a 400 ms stall 35 callbacks in
700 ms with ticks 2..10 dispatched within the same millisecond (catch-up); TIME_KILL_SYNCHRONOUS kill waited 313 ms
for a 300 ms callback; process handle count unchanged by arming an EVENT_SET event (68 -> 68, no handle duplicated).
Results files: `results/win10/time_events.txt`, `results/win10-noaudio/time_events.txt` (not tracked).

Under Wine (plumbing only, not an oracle) the flag mask, the period clamp/accounting and LastError differ; those
are not expectations.
