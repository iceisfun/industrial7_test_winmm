#!/usr/bin/env bash
# Host-side playback verification: run areas on the VM with a wav-backed audio device and check that every tone the
# tests announced was really rendered. See docs/VM_AUDIO.md section 4.
#
#   tools/wav_capture.sh AREA[,AREA...] [vm_run.sh options]   boot (default --audio hda, --out results/win10-capture), then check
#   tools/wav_capture.sh --check OUTDIR                       check an existing results dir again
#
# A test announces a tone by printing  t_note("TONE hz=<Hz> ms=<duration>")  (in the order it plays them), i.e. the
# result line "  NOTE TONE hz=1000 ms=300". For every boot N, the announced tones in <out>/_serial_b<N>.log are passed
# in order to tools/wavscan.py --expect against <out>/_capture_b<N>.wav; the report goes to <out>/_capture_b<N>.check.
# Exit code: vm_run.sh's code OR 1 if any announced tone is missing from the capture (or the capture is absent).
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
check() { # check OUTDIR
  local out="$1" rc=0 n=0
  for s in "$out"/_serial_b*.log; do
    [ -f "$s" ] || continue
    local b; b="$(basename "$s" .log)"; b="${b#_serial_}"
    local exp; exp="$(tr -d '\r' < "$s" | grep -ao 'NOTE TONE hz=[0-9]\+ ms=[0-9]\+' | sed 's/NOTE TONE hz=\([0-9]*\) ms=\([0-9]*\)/\1:\2/' | paste -sd, -)"
    [ -n "$exp" ] || { echo "wav_capture: $b: no announced tones"; continue; }
    n=$((n + 1))
    if [ ! -s "$out/_capture_$b.wav" ]; then echo "wav_capture: $b: announced tones ($exp) but no capture (audio mode without wav backend?)"; rc=1; continue; fi
    python3 -I "$ROOT/tools/wavscan.py" "$out/_capture_$b.wav" --expect "$exp" > "$out/_capture_$b.check" 2>&1 || rc=1
    grep -E '^(file=|TONE)' "$out/_capture_$b.check" | sed "s/^/wav_capture: $b: /"
  done
  [ $n -gt 0 ] || echo "wav_capture: no boot announced any tone"
  return $rc
}
if [ "${1:-}" = "--check" ]; then check "${2:?results dir}"; exit $?; fi
[ $# -ge 1 ] || { sed -n '2,13p' "$0"; exit 2; }
OUT="$ROOT/results/win10-capture"; AUDIO=hda; prev=""
for a in "$@"; do [ "$prev" = --out ] && OUT="$a"; [ "$prev" = --audio ] && AUDIO="$a"; prev="$a"; done
case "$AUDIO" in hda|hda-out|usb) ;; *) echo "wav_capture: --audio $AUDIO has no wav backend (use hda, hda-out or usb)" >&2; exit 2 ;; esac
"$ROOT/tools/vm_run.sh" --audio "$AUDIO" --out "$OUT" "$@"; rc=$?
check "$OUT" || rc=1
exit $rc
