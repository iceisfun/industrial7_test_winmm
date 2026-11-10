#!/usr/bin/env bash
# Run corpus exes on the local Windows 10 22H2 VM and split the results per area. See docs/VM_RUN.md.
#
#   tools/vm_run.sh [options] AREA|EXE[,AREA|EXE...] ...
#   tools/vm_run.sh --all [options]
#
# options:
#   --all             every build/*.exe (dx_* only with --destructive)
#   --destructive     allow dx_* areas (named ones too)
#   --audio MODE      virtual audio hardware of the guest (docs/VM_AUDIO.md):
#                       hda       (default) intel-hda + hda-duplex, wav backend (capture saved as <out>/_capture_b<N>.wav)
#                       hda-none  same codec, 'none' backend (no host file)
#                       hda-out   intel-hda + hda-output (render only), wav backend
#                       usb       usb-audio (render only), wav backend
#                       noaudio   no audio hardware at all (the no-device contract)
#   --filter SUBSTR   pass SUBSTR as the test-name filter (argv[1]) to every exe
#   --timeout SECS    per-exe timeout (default: max "VM_TIMEOUT: N" comment in tests/<area>/*.c, else 120)
#   --ready SECS      audio readiness budget in the guest (vmrun --audio-ready, default 90)
#   --out DIR         results dir (default results/win10 for hda, results/win10-<MODE> otherwise)
#   --runs DIR        where boot dirs go (default build/vmruns)
#   --tag NAME        boot dir name prefix (default winmm-<MODE>)
#   --no-build        do not run make first
#   --extra FILE      copy FILE into the payload too (repeatable)
#   --dry-run         build the payload(s), print run.cmd, do not boot
#
# Stages the exes + build/tools/vmrun.exe + a generated run.cmd into a payload dir and boots
# the script named by $WIN10_RUN (interface in docs/VM_RUN.md; one guest at a time). run.cmd first runs
# 'vmrun --audio-ready' (services RUNNING, device counts stable; '@@I7 AUDIO' line), then every exe under vmrun (framed,
# per-exe timeout, job object). tools/vm_split.py splits serial.log into <out>/<area>.txt and validates every area.
# Exes are packed into boots under an 840 s ORACLE_TIMEOUT cap (sum of per-exe timeouts + 180 s overhead).
# Exit code: 0 only if every area is PASS; 1 if any is FAIL/TIMEOUT/CRASHED/TRUNCATED/MISSING; 2 on usage/build/lab errors.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WIN10_RUN="${WIN10_RUN:-}"
OUT=""; RUNS="$ROOT/build/vmruns"; TAG=""; FILTER=""; TMO=""; ALL=0; DX=0; BUILD=1; DRY=0; AUDIO=hda; READY=90; EXTRA=(); ITEMS=()
while [ $# -gt 0 ]; do
  case "$1" in
    --all) ALL=1 ;;
    --destructive) DX=1 ;;
    --audio) AUDIO="$2"; shift ;;
    --filter) FILTER="$2"; shift ;;
    --timeout) TMO="$2"; shift ;;
    --ready) READY="$2"; shift ;;
    --out) OUT="$2"; shift ;;
    --runs) RUNS="$2"; shift ;;
    --tag) TAG="$2"; shift ;;
    --no-build) BUILD=0 ;;
    --extra) EXTRA+=("$2"); shift ;;
    --dry-run) DRY=1 ;;
    -h|--help) sed -n '2,33p' "$0"; exit 0 ;;
    -*) echo "vm_run: unknown option $1" >&2; exit 2 ;;
    *) IFS=',' read -ra parts <<< "$1"; ITEMS+=("${parts[@]}") ;;
  esac
  shift
done
case "$AUDIO" in hda|hda-none|hda-out|usb|noaudio) ;; *) echo "vm_run: bad --audio $AUDIO" >&2; exit 2 ;; esac
[ -n "$OUT" ] || { [ "$AUDIO" = hda ] && OUT="$ROOT/results/win10" || OUT="$ROOT/results/win10-$AUDIO"; }
[ -n "$TAG" ] || TAG="winmm-$AUDIO"
[[ "$FILTER" != *'"'* ]] || { echo "vm_run: --filter may not contain quotes" >&2; exit 2; }
cd "$ROOT"
if [ "$BUILD" = 1 ]; then make -s all >&2 || { echo "vm_run: build failed" >&2; exit 2; }; fi
[ -f build/tools/vmrun.exe ] || { echo "vm_run: build/tools/vmrun.exe missing (make tools)" >&2; exit 2; }
if [ "$DRY" = 0 ]; then
  [ -n "$WIN10_RUN" ] && [ -x "$WIN10_RUN" ] || {
    echo "vm_run: set WIN10_RUN to an executable script that boots a Windows 10 22H2 guest with a payload dir and collects" >&2
    echo "vm_run: COM1 output into serial.log (usage: WIN10_RUN PAYLOAD_DIR BOOT_DIR; see docs/VM_RUN.md)" >&2; exit 2; }
fi

EXES=()
if [ "$ALL" = 1 ]; then
  for e in build/*.exe; do [ -f "$e" ] || continue; n="$(basename "$e" .exe)"; [[ "$n" == dx_* && "$DX" = 0 ]] && continue; EXES+=("$e"); done
fi
for it in "${ITEMS[@]}"; do
  [ -z "$it" ] && continue
  if [ -f "$it" ]; then e="$it"; else e="build/$(basename "$it" .exe).exe"; fi
  [ -f "$e" ] || { echo "vm_run: no such exe: $it ($e)" >&2; exit 2; }
  n="$(basename "$e" .exe)"; [[ "$n" == dx_* && "$DX" = 0 ]] && { echo "vm_run: $n is destructive; pass --destructive" >&2; exit 2; }
  EXES+=("$e")
done
[ ${#EXES[@]} -gt 0 ] || { echo "vm_run: nothing to run (give AREA names or --all)" >&2; exit 2; }

tmo_for() {
  local n="$1" t
  if [ -n "$TMO" ]; then echo "$TMO"; return; fi
  t="$(grep -ho 'VM_TIMEOUT: *[0-9]\+' "tests/$n"/*.c 2>/dev/null | grep -o '[0-9]\+' | sort -n | tail -1)"
  echo "${t:-120}"
}

# QEMU audio devices per mode (W10_EXTRA; the runner appends it to the qemu command line)
audio_extra() { # audio_extra <bootdir>
  local wav="$1/out.wav"
  case "$AUDIO" in
    hda)      echo "-audiodev wav,id=snd0,path=$wav -device intel-hda -device hda-duplex,audiodev=snd0" ;;
    hda-none) echo "-audiodev none,id=snd0 -device intel-hda -device hda-duplex,audiodev=snd0" ;;
    hda-out)  echo "-audiodev wav,id=snd0,path=$wav -device intel-hda -device hda-output,audiodev=snd0" ;;
    usb)      echo "-audiodev wav,id=snd0,path=$wav -device usb-audio,audiodev=snd0" ;;
    noaudio)  echo "" ;;
  esac
}
WANT=1; [ "$AUDIO" = noaudio ] && WANT=0

CAP=840; OVER=$((180 + READY))
BOOTS=(); cur=""; cursum=0
for e in "${EXES[@]}"; do
  n="$(basename "$e" .exe)"; t="$(tmo_for "$n")"
  if [ -n "$cur" ] && [ $((cursum + t + OVER)) -gt $CAP ]; then BOOTS+=("$cur"); cur=""; cursum=0; fi
  cur="$cur $e"; cursum=$((cursum + t))
done
[ -n "$cur" ] && BOOTS+=("$cur")

STAMP="$(date +%Y%m%d-%H%M%S)"; rc=0; b=0
[ "$DRY" = 1 ] || { mkdir -p "$OUT"; : > "$OUT/_vm_status.log"; rm -f "$OUT/_audio.log" "$OUT"/_capture_b*.wav "$OUT"/_capture_b*.txt; }
for boot in "${BOOTS[@]}"; do
  b=$((b + 1))
  BOOTDIR="$RUNS/$TAG-$STAMP-$$-b$b"; [ "$DRY" = 1 ] && BOOTDIR="$ROOT/build/vmpay/$TAG-$STAMP-b$b"
  PAY="$BOOTDIR/payload"; mkdir -p "$PAY"
  cp build/tools/vmrun.exe "$PAY/"
  for x in "${EXTRA[@]+"${EXTRA[@]}"}"; do cp "$x" "$PAY/"; done
  sum=0; names=()
  {
    echo "@echo off"
    echo "rem generated by tools/vm_run.sh: vmrun.exe owns COM1 while it runs (COM1 is exclusive)"
    echo "cd /d %~dp0"
    echo ">COM1 echo @@I7 RUNSTART $TAG boot=$b audio=$AUDIO"
    echo ">COM1 ver"
    echo "vmrun.exe --audio-ready $READY --want-render $WANT"
    for e in $boot; do
      n="$(basename "$e" .exe)"; t="$(tmo_for "$n")"; sum=$((sum + t)); names+=("$n")
      cp "$e" "$PAY/"
      echo "vmrun.exe -t $t -n $n -- $n.exe${FILTER:+ \"$FILTER\"}"
    done
    echo ">COM1 echo @@I7 RUNEND $TAG boot=$b"
  } > "$PAY/run.cmd"
  OT=$((sum + OVER)); [ $OT -gt $CAP ] && OT=$CAP
  if [ "$DRY" = 1 ]; then echo "== boot $b: $PAY (ORACLE_TIMEOUT=$OT) W10_EXTRA='$(audio_extra "$BOOTDIR")'"; ls "$PAY"; cat "$PAY/run.cmd"; continue; fi
  echo "vm_run: boot $b/${#BOOTS[@]} audio=$AUDIO: ${names[*]} (ORACLE_TIMEOUT=$OT) -> $BOOTDIR" >&2
  W10_EXTRA="$(audio_extra "$BOOTDIR")" ORACLE_TIMEOUT=$OT "$WIN10_RUN" "$PAY" "$BOOTDIR" >&2
  rm -f "$BOOTDIR/payload.iso"; rm -rf "$BOOTDIR/stage"
  [ -s "$BOOTDIR/serial.log" ] || echo "vm_run: empty serial.log for boot $b" >&2
  grep -a -q "@@I7 RUNEND" "$BOOTDIR/serial.log" 2>/dev/null || echo "vm_run: boot $b did not reach RUNEND (guest died or ORACLE_TIMEOUT)" >&2
  grep -a "@@I7 AUDIO " "$BOOTDIR/serial.log" 2>/dev/null | tr -d '\r' | sed 's/^/vm_run: /' >&2
  cp "$BOOTDIR/serial.log" "$OUT/_serial_b$b.log" 2>/dev/null
  if [ -s "$BOOTDIR/out.wav" ]; then
    cp "$BOOTDIR/out.wav" "$OUT/_capture_b$b.wav"
    python3 -I "$ROOT/tools/wavscan.py" "$OUT/_capture_b$b.wav" > "$OUT/_capture_b$b.scan" 2>&1 || true
  fi
  exp="$(IFS=,; echo "${names[*]}")"
  python3 "$ROOT/tools/vm_split.py" "$BOOTDIR/serial.log" "$OUT" --expect "$exp" --audio "$AUDIO" || rc=1
done
[ "$DRY" = 1 ] && exit 0
echo "vm_run: results in $OUT (status: $OUT/_vm_status.log, audio: $OUT/_audio.log)" >&2
exit $rc
