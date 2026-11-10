# waveout_header_protocol: evidence

Exports under test (all called through `U()`): `waveOutPrepareHeader`, `waveOutUnprepareHeader`, `waveOutWrite`.
winmm.dll exports them as `jmp [IAT]` thunks into winmmbase (api-ms-win-mm-mme-l1-1-0); the tests call the winmm
export and, in `whp_parity_*`, the winmm export pinned with `t_proc_in` and winmmbase directly.

Files: `gate.c` (front gate), `ladder.c` (per-function ladder without a live handle), `device.c` (contract half and
the device half), `sequence.c` (longer sequences, threads, several handles), `whp_common.c` / `whp.h` (helpers).

## 1. Decomp-grounded (winmmbase 10.0.19041, reference corpus)

`waveOutPrepareHeader` (0x10910), `waveOutUnprepareHeader` (0x109b0), `waveOutWrite` (0x10a70) all start with

    if (pwh == NULL || cbwh < 0x30 || (pwh->dwFlags & 0xFFFFCFE0)) return 11;   /* MMSYSERR_INVALPARAM */

so the gate precedes every handle check; allowed bits are DONE 1, PREPARED 2, BEGINLOOP 4, ENDLOOP 8, INQUEUE 0x10
and the two legacy bits 0x1000 / 0x2000. cbwh is otherwise unchecked (64, 0x1000, 0xFFFFFFFF pass the gate).

| function | ladder after the gate |
|---|---|
| Prepare | PREPARED -> 0, no handle check, header untouched. Else `ClientUpdatePnpInfo`, `ValidateHandle(h, 1)` -> 5. Else `dwFlags &= 0xC`, WODM_PREPARE (7); 8 or 0 -> set PREPARED, 0 |
| Unprepare | INQUEUE -> 33 (before PREPARED). Not PREPARED -> 0, no handle check. Else handle -> 5. Else WODM_UNPREPARE (8); 8 or 0 -> clear PREPARED (only bit 2) |
| Write | not PREPARED -> 34 (before INQUEUE). INQUEUE -> 33. Else handle -> 5. Else clear DONE, WODM_WRITE (9) |

`ValidateHandle`: values <= 0xFFFF, -1 and 0xFFFFFFFF are rejected outright, others must be on the handle list with
type 1 (an HWAVEIN is type 2) -> 5 without dereferencing. Consequences pinned as hard CHECKs: a header rejected by
the gate or the ladder is never written (masking and DONE clearing happen after the handle check); queued headers
give 33 on any handle, including a second live one; a closed handle gives 5 only on the paths that reach the handle.

## 2. VM-observed (Win10 22H2 19045.2965, tools/vm_run.sh)

Run 1 (boots `whpA-20261110-091859` hda, `whpN-20261110-091904` noaudio): 107 tests PASS in both, 0 failures
(hda: 18418 checks; noaudio: 17724 checks, 45 device tests skipped by `waveOutGetNumDevs() == 0`). The whole
gate/ladder model of section 1 holds on the VM in both configurations, including with live, closed and wrong-kind
(HWAVEIN, HMIDIOUT) handles. Observations promoted to hard CHECKs after run 1:

| observation (hda, mapper 44100/16/2, CALLBACK_EVENT / FUNCTION) | test |
|---|---|
| Prepare -> 0x2; Write -> 0x12 synchronously; completion -> 0x3; Unprepare -> 0x1 (DONE kept) | whp_dev_write_inqueue_then_done |
| Prepare keeps only BEGINLOOP/ENDLOOP: DONE/INQUEUE/0x1000/0x2000 before -> dropped | whp_dev_prepare_masks_flags |
| Write twice while queued -> 33; Unprepare while queued -> 33; Prepare while queued -> 0, flags unchanged | whp_dev_write_twice_while_queued, whp_dev_unprepare_while_queued, whp_dev_prepare_while_queued_noop |
| a queued header given to a second live handle -> Write 33, Unprepare 33, Prepare 0 | whp_dev_queued_header_to_second_handle |
| Close with a queued buffer -> 33, handle still valid (GetID -> WAVE_MAPPER), Reset -> DONE, Close -> 0 | whp_dev_close_with_queued_buffer |
| Reset returns every queued header as 0x3 synchronously, one WOM_DONE each | whp_dev_reset_then_close_all_done, whp_dev_callback_reset_returns_all |
| WOM_DONE param1 = the header, in write order, flags 0x3 inside the callback; 7 callbacks for 5 headers | whp_dev_callback_done_order_and_flags |
| Close with a prepared but idle header -> 0; the stale handle then gives 5 on Unprepare/Write | whp_dev_close_with_prepared_unqueued |
| dwBytesRecorded (output) is rewritten to 0 by completion; prepare leaves it | whp_dev_write_inqueue_then_done, whp_seq_bytesrecorded_timeline |
| 0x1000 / 0x2000 set after prepare: Write 0, flags 0x1012 / 0x2012, final 0x1003 / 0x2003 | whp_dev_internal_bit_after_prepare |
| header prepared on handle A, written and unprepared on handle B: 0 / done / 0 | whp_dev_cross_handle_use |
| zero-length buffer, lengths not a multiple of nBlockAlign, a 2-byte buffer: prepare 0, write 0, DONE arrives | whp_dev_zero_length_buffer, whp_dev_unaligned_length_buffer |
| GetLastError untouched by gate and handle rejections | whp_gate_last_error_untouched |
| winmm!waveOutWrite is a separate thunk address from winmmbase!waveOutWrite (same behaviour) | whp_parity_* |

Still notes only: BEGINLOOP|ENDLOOP with dwLoops 3 on a 50 ms buffer completed after about 125 ms (loop count
not evidently honoured; timing is not asserted); the concurrent two-thread write of one header gave 0 / 33.

## 3. Knowledge / notes only

- Output headers: dwBytesRecorded is not part of the output contract; the tests only `t_note` it.
- lpNext / reserved are driver-owned; noted, never checked.
- Concurrent writes of one header from two threads: the header check is not atomic with the driver queue; the
  test accepts one 0 plus one 0/33 and notes the pair.
