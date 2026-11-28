#!/usr/bin/env bash
# V3f PETSc reference runs (docs/v3_experiments.md). Run inside WSL Ubuntu with a PETSc build:
#   PETSC_DIR=/opt/petsc-src/petsc-3.26.1 PETSC_ARCH=arch-opt \
#     bash run_v3f_petsc.sh <export dir> <output csv>
# The export dir holds <id>.K/f/ref.petsc written by `uniforge_cli study export`. Each system is
# solved by PETSc CG + GAMG (5 repetitions, medians) with GAMG's default settings and with the
# strength threshold 0.02 (UniForge's θ0), on one pinned virtual CPU.
set -euo pipefail
src="$1"
out="$2"
here="$(cd "$(dirname "$0")" && pwd)"
work=/tmp/v3f_petsc
mkdir -p "$work"
make -s -C "$here" -f makefile petsc_ref PETSC_DIR="$PETSC_DIR" PETSC_ARCH="$PETSC_ARCH"
cp -u "$src"/*.petsc "$work"/
echo "config,id,rows,nnz,reps,setup_s,solve_s,total_s,iterations,reason,true_rel_residual,err_vs_ref_K" > "$out"
for s in 1.000 0.794 0.630 0.500 0.397 0.315 0.250 0.198; do
  id="V3F-HS7-s$s-AMG-PCG"
  line=$(taskset -c 5 "$here/petsc_ref" -dir "$work" -id "$id" -reps 5)
  echo "GAMG-default,$line" | tee -a "$out"
  line=$(taskset -c 5 "$here/petsc_ref" -dir "$work" -id "$id" -reps 5 -pc_gamg_threshold 0.02)
  echo "GAMG-thr0.02,$line" | tee -a "$out"
done
