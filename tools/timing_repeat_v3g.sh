#!/usr/bin/env bash
# V3g timing repeat (docs/v3_experiments.md): HS-7 at 0.198 mm (3.76 M DOF), AMG-PCG-CH-SP on
# 8 threads, observer off, 5 solves per run. Conditions are interleaved round by round so a
# slow drift of the machine affects every condition alike. Run from worktrees/v3 (Git Bash):
#   bash tools/timing_repeat_v3g.sh <phase label> <rounds> <cond>...
# cond: v3b (code ecbf093, unpinned), v3c (code 93f65d6, unpinned),
#       nopin (current code, UNIFORGE_NO_PIN=1), pin (current code, pinned).
set -euo pipefail
label="$1"
rounds="$2"
shift 2
id="V3B-HS7-s0.198-AMG-PCG-CH-SP-noobs-p8"
out="../v3_timing"
mkdir -p "$out"
for r in $(seq 1 "$rounds"); do
  for cond in "$@"; do
    dir="$out/$label-$cond-r$r"
    rm -rf "$dir"
    case "$cond" in
      v3b) exe="../v3_old/ecbf093/build/bin/Release/uniforge_cli.exe"; env="" ;;
      v3c) exe="../v3_old/93f65d6/build/bin/Release/uniforge_cli.exe"; env="" ;;
      nopin) exe="build/windows-msvc/bin/Release/uniforge_cli.exe"; env="UNIFORGE_NO_PIN=1" ;;
      pin) exe="build/windows-msvc/bin/Release/uniforge_cli.exe"; env="" ;;
    esac
    env $env "$exe" study run --id "$id" --out "$dir" --allow-dirty > /dev/null
    awk -F, -v l="$label" -v c="$cond" -v r="$r" \
      'NR==1{for(i=1;i<=NF;i++)h[$i]=i} NR==2{printf "%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n", l,c,r,$h["setup_s"],$h["solve_s_median"],$h["solve_s_min"],$h["solve_s_max"],$h["iterations"],$h["core"],$h["cpu_load_pct"]}' \
      "$dir/runs/$id.csv"
  done
done
