# waveout_callbacks: evidence

Callback delivery for waveOut and waveIn (CALLBACK_NULL / WINDOW / THREAD / FUNCTION / EVENT) on Windows 10 x64
(winmm.dll 10.0.19041.546 stub -> winmmbase -> wdmaud.drv). Files:

| file | content |
|---|---|
| `wcb.h`, `wcb_common.c` | device gating (counts read once, never timed), recorders for the five delivery types, silent buffers, bounded waits (3 s) |
| `wcb_contract.c` | every configuration: failed opens deliver nothing, validation order, QUERY never delivers |
| `wcb_function.c` | CALLBACK_FUNCTION on real devices: OPEN/DONE/CLOSE sequence, parameters, threads, reset/close/pause/loops |
| `wcb_queued.c` | CALLBACK_EVENT, CALLBACK_WINDOW (message-only window), CALLBACK_THREAD (worker queue) |
| `wcb_wavein.c` | WIM_OPEN / WIM_DATA / WIM_CLOSE for every type on a capture device |
| `wcb_types.c` | CALLBACK_NULL, unknown type values, handle protocol around a callback-bearing handle, mixed types at once |

Device gating: the device half runs only when `waveOutGetNumDevs()` / `waveInGetNumDevs()` > 0 (`t_skip` otherwise);
the contract half always runs and uses id = count as the out-of-range id. Audio is silence only (no TONE notes).

## 1. Decomp-grounded (winmmbase, corpus 19041)

* `DriverCallback` (winmmbase!0x5560): NULL target -> no delivery (FALSE); type = flags & 7 with flags = fdwOpen >> 16:
  1 `PostMessageA(hwnd, msg, hdev, dwParam1)`; 2 `PostThreadMessageA(tid, msg, hdev, dwParam1)` then
  `mmTaskSignal(tid)` = `PostThreadMessageA(tid, WM_USER, 0, 0)`; 3 direct call `cb(hdev, msg, dwUser, p1, p2)`;
  5 `SetEvent`; any other type -> nothing. Tests: `wcb_thr_task_signal_follows_each_message`,
  `wcb_unknown_types_real_open` (event under type 4/6/7 never signalled), NULL-target tests
  (`wcb_fn_null_pointer_target`, `wcb_ev_null_event_handle`, `wcb_win_null_hwnd_no_messages`, zero thread id).
* `waveOutOpen` (winmmbase!0x103b0) / `waveInOpen` (!0x11290) order: pwfx NULL -> 11 (handle untouched);
  `dwCallback != 0 && ((fdwOpen >> 16) & 7) == 1 && !IsWindow` -> 11, so 0x90000 / 0xF90000 / 0x7FF90000 also take
  the window check, and a dead HWND under any other type is not validated; flag masks 0x7E04 (mapper) / 0x7E10
  (device) -> 10; phwo NULL (non-QUERY) -> 11 else `*phwo = NULL`; 0 devices -> 2; bad id -> 2. QUERY ignores phwo
  and the type field. Tests: `wcb_out_*` / `wcb_in_*` in wcb_contract.c.
* The handle is stored in `*phwo` only after the driver's WODM_OPEN returns, i.e. after WOM_OPEN was delivered:
  `wcb_fn_open_handle_stored_after_callback` (callback reads `*phwo` == NULL), `wcb_in_fn_open_close_each_id`.
* `waveOutGetID` returns the id stored at open (WAVE_MAPPER stays 0xFFFFFFFF): `wcb_handle_getid_on_callback_handles`.
* Header ladder (34 unprepared, 33 in queue, 11 size) delivers no WOM_DONE: `wcb_handle_write_errors_no_callback`.

## 2. VM-observed (Win10 22H2 19045.2965 guest, tools/vm_run.sh; hda = intel-hda + hda-duplex, noaudio = none)

hda: waveOut 1 "Speakers (High Definition Audio", waveIn 1 "Line In (High Definition Audio ".

* WOM_OPEN / WIM_OPEN function callback and CALLBACK_EVENT signal happen synchronously inside the open, on the
  caller thread; WOM_CLOSE likewise inside waveOutClose on the caller thread (mapper and id 0).
* Window and thread targets: WOM_OPEN is already queued when waveOutOpen returns, WOM_CLOSE when waveOutClose returns;
  wParam = handle, lParam = 0 for OPEN/CLOSE and LPWAVEHDR for DONE / WIM_DATA.
* CALLBACK_THREAD sequence: `WOM_OPEN, WM_USER, WOM_DONE, WM_USER, ..., WOM_CLOSE, WM_USER` (every MM message followed
  by the mmTaskSignal WM_USER with wParam 0, lParam 0).
* WOM_DONE / WIM_DATA: one per buffer, submission order, dwParam1 = LPWAVEHDR, dwParam2 = 0, header already
  DONE|PREPARED (INQUEUE clear) at delivery, delivered on one worker thread that is not the caller.
* dwInstance passed through with all 64 bits (0, 0xFFFFFFFF, 0x100000000, 0xFFFFFFFFFFFFFFFF, ...).
* waveOutReset: every queued buffer comes back with DONE|PREPARED and its WOM_DONE has been delivered before Reset
  returns (4 of 4). Close with a queued buffer -> 33 and no WOM_CLOSE; after Reset Close -> 0 with WOM_CLOSE.
* Pause holds completions; Restart releases them. A single looped header (BEGINLOOP|ENDLOOP, any dwLoops) yields one
  WOM_DONE; BreakLoop on a 1000-loop header completes it within 3 s.
* A zero-length buffer and a non-block-aligned buffer are accepted (write 0) and complete with one WOM_DONE.
* waveIn: Reset without Start returns buffers with dwBytesRecorded 0 through WIM_DATA; waveInStop on a partially
  filled 2 s buffer returns it (13440 of 176400 bytes after 300 ms); Close with queued buffers -> 33.
* Unknown callback types 4/6/7 (0x40000/0x60000/0x70000) on a real open: accepted (0) on mapper and id 0, nothing is
  delivered. EVENT with extra high bits (0xD0000, 0x150000, 0x1050000): accepted and the event is signalled
  (the driver keeps only the low 3 bits of the field). 6-ch PCM real open on id 0 with WAVE_FORMAT_DIRECT -> 11;
  6-ch QUERY through the mapper -> 32; unknown format tag through the mapper -> 32.
* noaudio: every open (all types, mapper, 0xFFFF, 0, 1, QUERY) -> 2, nothing delivered, handle NULL.

## 3. Knowledge / documentation only

* Restricted calls inside a function callback (documented deadlock): the recorders only copy data and SetEvent; no
  reentrancy test.
* CALLBACK_NULL capture fill timing is recorded with t_note (first VM run: the first CALLBACK_NULL capture in the
  process did not fill within 3 s); only the post-Reset buffer protocol is checked.
