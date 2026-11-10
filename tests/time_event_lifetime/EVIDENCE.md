# time_event_lifetime: evidence

Area scope: lifetime and multi-thread semantics of `timeSetEvent` / `timeKillEvent` and the
`timeBeginPeriod` reference each event holds. Files: `cap.c` (slot table, ids), `kill.c` (kill while a
callback runs, kill from inside callbacks, kill races), `thread.c` (single worker, serialization,
concurrent set/kill), `period.c` (period references), `callback.c` (callback kinds, args, re-arm),
`exit.c` (process exit and module pinning). Scenarios that need a clean slot table or a fresh process
run in CHILD processes and report "key value" records back to the parent (`tl.h`).

## Decomp-grounded (winmm 10.0.19041, kernel32)

| fact | source | tests |
|---|---|---|
| fuEvent & 0xFFFFFE0E -> 0; uDelay outside [wPeriodMin, 1000000] -> 0; checks come before any generation or period use | winmm!timeSetEvent 0x2a20 | tl_cap_id_layout_fresh_process, tl_period_invalid_set_takes_nothing |
| period = uRes > TimerData ? TimerData : max(uRes, wPeriodMin); uDelay < period -> TimerData; TimerData = ceil(NtQueryTimerResolution max / 10000) (0x37 if >= 55 ms) | timeSetEvent, TimeInit 0x23c0 | tl_period_table_* |
| timeBeginPeriod(period) before the slot search; a failed allocation calls timeEndPeriod(period) | timeSetEvent | tl_cap_failed_set_releases_period |
| a failing timeBeginPeriod (16-bit counter at 0xFFFF -> 97) makes timeSetEvent return 0 | kernel32!timeBeginPeriod 0x1e110 | tl_period_saturated_counter_fails_set |
| 16 slots; scan for the lowest free slot; full -> 0 without consuming a generation | timeThreadSetEvent 0x2b90 | tl_cap_16_*, tl_cap_id_layout_fresh_process |
| id = generation + slot; generation is a 16-bit counter stepping 0x10 per allocation, skipping 0 | timeThreadSetEvent | tl_cap_id_layout_fresh_process, tl_cap_generation_wraps_skipping_zero |
| timeKillEvent: no worker yet -> 97; slot[id & 15].id != id -> 97; else timeEndPeriod(slot period), id = 0, NtCancelTimer (result 0 / 97) | timeKillEvent 0x2e50 | tl_cap_kill_before_and_after_thread, tl_kill_* |
| a free slot holds id 0, so timeKillEvent(0) matches an empty slot 0 once the worker exists, and calls timeEndPeriod with the stale period of that slot | timeKillEvent | tl_cap_kill_before_and_after_thread, tl_period_kill0_consumes_stale_period |
| synchronous kill waits on the "callback done" manual-reset event only when a callback is running, it is uTimerID's, the caller is not the worker and fuEvent has 0x100 | timeKillEvent, timeThread 0x26c0 | tl_kill_sync_*, tl_kill_sync_other_event_does_not_wait_for_running_callback |
| callbacks run outside the timer lock; after the callback the worker re-arms / frees only if the slot still has the same id: a kill inside a callback ends the event, the period is released once | timeThread | tl_kill_self_*, tl_kill_self_oneshot_releases_period_once, tl_cb_rearm_chain_* |
| one worker thread, priority 15 (THREAD_PRIORITY_TIME_CRITICAL), runs every callback serially; never torn down | TimeInitThread 0x2d7c, timeThread | tl_thread_* |
| worker dispatch on fuEvent & 0xF0: 0 -> fptc(id, 0, dwUser, 0, 0) if fptc != NULL (5th arg slot zeroed), 0x10 SetEvent, 0x20 PulseEvent, others -> nothing | timeThread | tl_cb_kind_*, tl_cb_null_function, tl_cb_args_* |
| one-shot completion: slot id = 0 then timeEndPeriod(period), after the callback | timeThread | tl_period_oneshot_*, tl_cap_oneshot_completion_frees_slot |
| TimeInitThread pins winmm with LoadLibraryEx of its own path before creating the worker | TimeInitThread, LoadWINMM 0x3104 | tl_exit_worker_pins_winmm |
| kernel32 counts periods [wPeriodMin, uMax) separately; End on a zero count -> 97; periods >= uMax uncounted | kernel32!timeEndPeriod 0x1d190 | tl_period_* (tl_refs helper) |

## VM-observed (Win10 22H2 guest, build 19045.2965)

First boot (hda, virtual HDA device present): 128 tests, 6723 checks, 0 failures, 15.9 s; every
decomp-derived expectation above held without change. Observed values:

- wPeriodMin 1, TimerData/uMax 16 (NtQueryTimerResolution max 156250).
- First id in a fresh process 0x10; ids 0x10*(n) + slot exactly as modelled; generation wrap after 4095
  allocations returns to 0x10 (0 skipped).
- timeKillEvent before any timeSetEvent -> 97 for every id including 0; after the worker exists and slot 0
  is empty, timeKillEvent(0) -> 0 and consumes the stale period of slot 0 (another event's reference).
- Saturating period 6 to 0xFFFF references makes timeSetEvent(res 6) return 0; other periods unaffected.
- Synchronous kill returns only after the 60-200 ms callback finished; non-synchronous kill returned in
  0 ms with the callback still running (noted).
- Worker priority 15; one worker thread for every event; serialization max 1 in-callback across 2/4/8/16
  periodic events; 4x4 concurrent timeSetEvent -> 16 distinct ids in slots 0..15.
- System timer resolution 156250 before, 10000 while a 1 ms event lives, 156250 after.
- Process exit with live periodic / full table / callback running / ExitProcess from inside a callback:
  exit codes intact, each child exits within ~30 ms.
- FreeLibrary after LoadLibrary("winmm.dll") in a fresh child: unloaded when no event was ever set,
  still loaded after a set+kill (worker pin). Promoted to a hard check after this boot.
- Callback args: (id, 0, dwUser, 0, 0); 64-bit dwUser passed through intact.

The noaudio boot (no virtual audio device) runs the same binary; this area does not touch audio devices.

## Knowledge / t_note only

- Without TIME_KILL_SYNCHRONOUS the kill returns while the callback still runs (decomp: no wait); the
  elapsed time is only noted.
- Process timer resolution while a 1 ms event lives is noted before/after (system-wide value); only the
  "<= 1 ms while live" bound is checked.
- GetLastError after timeSetEvent is noted, not checked.
- The "no event -> winmm unloads" result was a note in the first boot and is now a check.
