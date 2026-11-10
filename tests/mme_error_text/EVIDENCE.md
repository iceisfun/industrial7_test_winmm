# mme_error_text: evidence

Exports under test (winmm.dll ordinals): waveOutGetErrorTextA 164, W 165; waveInGetErrorTextA 148, W 149;
midiOutGetErrorTextA 72, W 73; midiInGetErrorTextA 56, W 57. Each winmm export is a one-instruction jump into
winmmbase. In winmmbase the Out and In names share one body:

| body (winmmbase RVA) | exports | helper |
|---|---|---|
| 0x10320 | waveOutGetErrorTextW, waveInGetErrorTextW | waveGetErrorTextW 0x102c4 |
| 0x10340 | waveOutGetErrorTextA, waveInGetErrorTextA | inline LoadStringA |
| 0x7d40 | midiOutGetErrorTextW, midiInGetErrorTextW | midiGetErrorTextW 0x7cec |
| 0x7d60 | midiOutGetErrorTextA, midiInGetErrorTextA | inline LoadStringA |

## Decomp-grounded (hard CHECKs)

* `cchText == 0` -> 0 before anything else. The pointer may be NULL and the buffer is not touched.
  The last error is unchanged.
* `pszText == NULL` with `cchText > 0` -> 11 (MMSYSERR_INVALPARAM). This comes before the range check, so a bad code
  with a NULL pointer also gives 11.
* Then `pszText[0] = 0`. The range check runs on the full 32-bit code:
  * wave: `code < 22 || code - 32 < 4`
  * midi: `code < 22 || code - 64 < 8`
* A code outside the range -> 9 (MMSYSERR_BADERRNUM). Only `pszText[0]` is written and the last error is unchanged.
  High-word values such as 0x10002 are out of range.
* A code inside the range with `cchText < 2` (that is, 1) -> 0 with the empty string, because LoadString is skipped.
  This holds even for code 21.
* Otherwise the body calls `LoadString{A,W}(winmmbase, code, pszText, cchText)`. A return of 0 -> 9, else 0. The text
  is the winmmbase string resource whose id equals the code. The tests compare it byte for byte with a direct
  LoadString on winmmbase.dll.
* The Out and In variants give identical results and identical buffer bytes. The tests sweep codes 0..300 with
  cch 0/1/2/3/5/9/33/256.
* The wave and midi texts are equal for codes 0..20 (same string ids).
* Truncation follows LoadString: at a given cch the text has `min(len, cch - 1)` characters and is a prefix of the full
  text. For A this is checked only when the ANSI code page is single-byte. Nothing is written at or after
  `pszText[cch]`.
* The ordinal and name exports resolve to the same address. winmmbase exports give the same answers as winmm.
* mmsystem.h constants: WAVERR 32..35, MIDIERR 64..71, MAXERRORLENGTH 256.

## VM-observed (Win10 22H2 19045.2965 guest, the oracle)

* Code 21 (MMSYSERR_MOREDATA) has no string resource. LoadString fails, so the result is 9 for cch >= 2 in all
  8 exports. This was first seen in the docs/research/02 pass-2 probe and is confirmed by this area.
* The success set is exactly wave 0..20 and 32..35, and midi 0..20 and 64..71, in A and W.
* Every text fits in MAXERRORLENGTH: a 256-character buffer and an 8192-character buffer give the same result.
* LoadString writes nothing after the terminator. The bytes between the NUL and `pszText[cch]` are untouched in both
  A and W (`*_tail_after_nul`).
* The results are the same with the virtual HDA device and with no audio hardware. The error text does not depend on
  the device set.
* See the VM run section below for the run records.

## Knowledge / notes only (t_note, never CHECKed)

* The exact English text of codes 0, 2, 32 and 64 and of every other code is logged by `mme_error_text_note_*`. It
  depends on the locale and the MUI language.
* The last error after the failed LoadString for code 21.
* The export layout: winmm has 8 distinct stubs, and winmmbase aliases Out/In onto one address. This is an
  implementation detail, not a contract.
* Duplicate texts within a family, control characters.

## Absent exports (CHECK)

winmm.dll has no mixerGetErrorTextA/W, aux/joy/time GetErrorText, or undecorated `waveOutGetErrorText`. The helpers
`waveGetErrorTextW` and `midiGetErrorTextW` are internal and not exported. GetProcAddress fails with
ERROR_PROC_NOT_FOUND.

## VM runs
