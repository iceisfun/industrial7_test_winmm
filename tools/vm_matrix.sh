#!/usr/bin/env bash
# Run the whole corpus on the local Win10 VM in several audio configurations and summarise. See docs/VM_RUN.md.
#
#   tools/vm_matrix.sh [--modes hda,noaudio] [--twice] [other vm_run.sh options, e.g. --destructive --filter X]
#
# --modes   audio modes to run (default hda,noaudio: the default device set and the no-device contract; every test
#           must PASS in both). Each mode writes results/win10 (hda) or results/win10-<mode>.
# --twice   run every mode a second time into <dir>_b and compare per test; a test whose outcome differs between the
#           two runs of the same mode is FLAKY and must not carry hard CHECKs (t_note instead).
# Prints the per-area status table of every mode and the failures (tools/summarize.py --failures).
# Exit code: non-zero if any area is not PASS in any mode, or (with --twice) any test is flaky.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; cd "$ROOT"
MODES="hda,noaudio"; TWICE=0; ARGS=(); prev=""
for a in "$@"; do
  if [ "$prev" = --modes ]; then MODES="$a"; prev=""; continue; fi
  case "$a" in --modes) prev=--modes ;; --twice) TWICE=1 ;; *) ARGS+=("$a") ;; esac
done
make -s all || exit 2
dir_for() { [ "$1" = hda ] && echo "results/win10" || echo "results/win10-$1"; }
rc=0
IFS=',' read -ra ML <<< "$MODES"
for m in "${ML[@]}"; do
  d="$(dir_for "$m")"
  tools/vm_run.sh --all --no-build --audio "$m" --out "$d" "${ARGS[@]+"${ARGS[@]}"}" || rc=1
  if [ "$TWICE" = 1 ]; then tools/vm_run.sh --all --no-build --audio "$m" --out "${d}_b" --tag "winmm-$m-b" "${ARGS[@]+"${ARGS[@]}"}" || rc=1; fi
done
for m in "${ML[@]}"; do
  d="$(dir_for "$m")"
  echo "== audio=$m ($d/_vm_status.log)"
  sed -n 's/^/  /p' "$d/_audio.log" 2>/dev/null | grep '@@I7 AUDIO ' || echo "  (no @@I7 AUDIO line)"
  sort -k1,1 -k2,2 "$d/_vm_status.log" 2>/dev/null
  python3 tools/summarize.py "$d" --failures 2>/dev/null | sed 's/^/  /' || true
  if [ "$TWICE" = 1 ]; then
    python3 - "$ROOT/$d" "$ROOT/${d}_b" <<'EOF' || rc=1
import glob, os, re, sys
def outcomes(d):
    r = {}
    for f in glob.glob(os.path.join(d, "*.txt")):
        if os.path.basename(f).startswith("_"):
            continue
        for line in open(f, errors="replace"):
            m = re.match(r"^(OK  |FAIL|SKIP) (\S+)$", line.rstrip("\n"))
            if m:
                r[(os.path.basename(f)[:-4], m.group(2))] = m.group(1).strip()
    return r
a, b = outcomes(sys.argv[1]), outcomes(sys.argv[2])
keys = set(a) | set(b)
flaky = sorted(k for k in keys if a.get(k) != b.get(k))
for k in flaky:
    print("  FLAKY %s %s: %s vs %s" % (k[0], k[1], a.get(k), b.get(k)))
print("  vm_matrix: %d tests compared, %d flaky" % (len(keys), len(flaky)))
sys.exit(1 if flaky else 0)
EOF
  fi
done
exit $rc
