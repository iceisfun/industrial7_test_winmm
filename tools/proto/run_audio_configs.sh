#!/usr/bin/env bash
# Boot the Win10 oracle once per virtual audio configuration and collect serial.log (+ the host-side
# wav capture for wav-backed configs).   run_audio_configs.sh OUTROOT config...
# configs: noaudio hda-duplex-none hda-duplex-wav hda-output-wav ich9-micro-wav usb-audio-wav legacy-isa-pci
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"; ROOT="${1:?outroot}"; shift; mkdir -p "$ROOT"
make_payload() { local d="$ROOT/payload"; rm -rf "$d"; mkdir -p "$d"; cp "$HERE/audprobe.exe" "$HERE/run.cmd" "$d/"; echo "$d"; }
PAY="$(make_payload)"
for cfg in "$@"; do
  OUT="$ROOT/$cfg"; mkdir -p "$OUT"; WAV="$OUT/out.wav"
  case "$cfg" in
    noaudio)         EX="" ;;
    hda-duplex-none) EX="-audiodev none,id=snd0 -device intel-hda -device hda-duplex,audiodev=snd0" ;;
    hda-duplex-wav)  EX="-audiodev wav,id=snd0,path=$WAV -device intel-hda -device hda-duplex,audiodev=snd0" ;;
    hda-output-wav)  EX="-audiodev wav,id=snd0,path=$WAV -device intel-hda -device hda-output,audiodev=snd0" ;;
    ich9-micro-wav)  EX="-audiodev wav,id=snd0,path=$WAV -device ich9-intel-hda -device hda-micro,audiodev=snd0" ;;
    usb-audio-wav)   EX="-audiodev wav,id=snd0,path=$WAV -device usb-audio,audiodev=snd0" ;;
    legacy-isa-pci)  EX="-audiodev none,id=a1 -audiodev none,id=a2 -audiodev none,id=a3 -device AC97,audiodev=a1 -device ES1370,audiodev=a2 -device sb16,audiodev=a3" ;;
    *) echo "unknown config $cfg" >&2; continue ;;
  esac
  echo "== $cfg: $EX"; T0=$(date +%s)
  W10_EXTRA="$EX" ORACLE_TIMEOUT=900 "$HOME/w10lab/tools/win10_run.sh" "$PAY" "$OUT" > "$OUT/run.log" 2>&1
  echo "== $cfg done in $(( $(date +%s) - T0 ))s; $(wc -c < "$OUT/serial.log") bytes serial"
  rm -rf "$OUT/stage" "$OUT/payload.iso"
  [ -f "$WAV" ] && python3 -I "$HERE/wavscan.py" "$WAV" > "$OUT/wavscan.txt" 2>&1
done
