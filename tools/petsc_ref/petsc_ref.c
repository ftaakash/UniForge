/* PETSc reference solver for UniForge V3f (docs/v3_experiments.md).
 *
 * Loads K, f (and, if present, the reference solution) written by
 *   uniforge_cli study export --id <run id> --out results/v3 --to <dir>
 * and solves K x = f with PETSc's CG preconditioned by GAMG (smoothed aggregation), with the
 * same stopping rule as UniForge's PCG: ||f - K x_k||_2 <= rtol ||f||_2 on the recursive
 * (unpreconditioned) residual, x_0 = 0. Setup (KSPSetUp, which builds the GAMG hierarchy) and
 * solve are timed separately, -reps times; the medians are printed as one CSV line:
 *   id,rows,nnz,reps,setup_s,solve_s,total_s,iterations,reason,true_rel_residual,err_vs_ref_K
 * Extra PETSc options (e.g. -pc_gamg_threshold 0.02) are taken from the command line.
 *
 * Usage: petsc_ref -dir <dir> -id <run id> [-reps 5] [-rtol 1e-10] [PETSc options]
 */
#include <petscksp.h>
#include <petsctime.h>

static int compareReal(const void *a, const void *b)
{
  const PetscReal x = *(const PetscReal *)a, y = *(const PetscReal *)b;
  return (x > y) - (x < y);
}

static PetscReal median(PetscReal *v, PetscInt n)
{
  qsort(v, (size_t)n, sizeof(PetscReal), compareReal);
  return (n % 2) ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

int main(int argc, char **argv)
{
  char        dir[PETSC_MAX_PATH_LEN] = ".", id[256] = "", file[PETSC_MAX_PATH_LEN + 300];
  PetscInt    reps = 5, rows, nnzLocal, its = 0;
  PetscReal   rtol = 1e-10, normF, trueRel = 0, err = -1;
  PetscReal   setup[64], solve[64], total[64];
  PetscBool   hasRef = PETSC_FALSE;
  Mat         A;
  Vec         f, x, r, ref = NULL;
  PetscViewer viewer;
  MatInfo     info;
  KSPConvergedReason reason = KSP_CONVERGED_ITERATING;

  PetscCall(PetscInitialize(&argc, &argv, NULL, NULL));
  PetscCall(PetscOptionsGetString(NULL, NULL, "-dir", dir, sizeof dir, NULL));
  PetscCall(PetscOptionsGetString(NULL, NULL, "-id", id, sizeof id, NULL));
  PetscCall(PetscOptionsGetInt(NULL, NULL, "-reps", &reps, NULL));
  PetscCall(PetscOptionsGetReal(NULL, NULL, "-rtol", &rtol, NULL));
  PetscCheck(id[0] != '\0', PETSC_COMM_WORLD, PETSC_ERR_USER, "give -id <run id>");
  PetscCheck(reps >= 1 && reps <= 64, PETSC_COMM_WORLD, PETSC_ERR_USER, "-reps must be 1..64");

  PetscCall(PetscSNPrintf(file, sizeof file, "%s/%s.K.petsc", dir, id));
  PetscCall(PetscViewerBinaryOpen(PETSC_COMM_WORLD, file, FILE_MODE_READ, &viewer));
  PetscCall(MatCreate(PETSC_COMM_WORLD, &A));
  PetscCall(MatSetType(A, MATSEQAIJ));
  PetscCall(MatLoad(A, viewer));
  PetscCall(PetscViewerDestroy(&viewer));
  PetscCall(MatSetOption(A, MAT_SPD, PETSC_TRUE));
  PetscCall(MatGetSize(A, &rows, NULL));
  PetscCall(MatGetInfo(A, MAT_LOCAL, &info));
  nnzLocal = (PetscInt)info.nz_used;

  PetscCall(PetscSNPrintf(file, sizeof file, "%s/%s.f.petsc", dir, id));
  PetscCall(PetscViewerBinaryOpen(PETSC_COMM_WORLD, file, FILE_MODE_READ, &viewer));
  PetscCall(VecCreate(PETSC_COMM_WORLD, &f));
  PetscCall(VecLoad(f, viewer));
  PetscCall(PetscViewerDestroy(&viewer));

  PetscCall(PetscSNPrintf(file, sizeof file, "%s/%s.ref.petsc", dir, id));
  PetscCall(PetscTestFile(file, 'r', &hasRef));
  if (hasRef) {
    PetscCall(PetscViewerBinaryOpen(PETSC_COMM_WORLD, file, FILE_MODE_READ, &viewer));
    PetscCall(VecCreate(PETSC_COMM_WORLD, &ref));
    PetscCall(VecLoad(ref, viewer));
    PetscCall(PetscViewerDestroy(&viewer));
  }
  PetscCall(VecDuplicate(f, &x));
  PetscCall(VecDuplicate(f, &r));
  PetscCall(VecNorm(f, NORM_2, &normF));

  for (PetscInt k = 0; k < reps; ++k) {
    KSP           ksp;
    PC            pc;
    PetscLogDouble t0, t1, t2;
    PetscCall(KSPCreate(PETSC_COMM_WORLD, &ksp));
    PetscCall(KSPSetOperators(ksp, A, A));
    PetscCall(KSPSetType(ksp, KSPCG));
    PetscCall(KSPGetPC(ksp, &pc));
    PetscCall(PCSetType(pc, PCGAMG));
    PetscCall(KSPSetNormType(ksp, KSP_NORM_UNPRECONDITIONED));
    PetscCall(KSPSetTolerances(ksp, rtol, 0.0, PETSC_DEFAULT, 10000));
    PetscCall(KSPSetFromOptions(ksp));
    PetscCall(VecSet(x, 0.0));
    PetscCall(PetscTime(&t0));
    PetscCall(KSPSetUp(ksp));
    PetscCall(PCSetUp(pc));
    PetscCall(PetscTime(&t1));
    PetscCall(KSPSolve(ksp, f, x));
    PetscCall(PetscTime(&t2));
    PetscCall(KSPGetIterationNumber(ksp, &its));
    PetscCall(KSPGetConvergedReason(ksp, &reason));
    setup[k] = (PetscReal)(t1 - t0);
    solve[k] = (PetscReal)(t2 - t1);
    total[k] = (PetscReal)(t2 - t0);
    PetscCall(KSPDestroy(&ksp));
  }
  /* True residual and error of the last solve (not timed). */
  PetscCall(MatMult(A, x, r));
  PetscCall(VecAYPX(r, -1.0, f));
  PetscCall(VecNorm(r, NORM_2, &trueRel));
  trueRel = normF > 0 ? trueRel / normF : trueRel;
  if (ref) {
    PetscCall(VecAXPY(ref, -1.0, x));
    PetscCall(VecNorm(ref, NORM_INFINITY, &err));
  }
  PetscCall(PetscPrintf(PETSC_COMM_WORLD, "%s,%" PetscInt_FMT ",%" PetscInt_FMT ",%" PetscInt_FMT
                        ",%.6g,%.6g,%.6g,%" PetscInt_FMT ",%s,%.3e,%.3e\n",
                        id, rows, nnzLocal, reps, (double)median(setup, reps),
                        (double)median(solve, reps), (double)median(total, reps), its,
                        KSPConvergedReasons[reason], (double)trueRel, (double)err));

  PetscCall(VecDestroy(&x));
  PetscCall(VecDestroy(&r));
  PetscCall(VecDestroy(&f));
  if (ref) PetscCall(VecDestroy(&ref));
  PetscCall(MatDestroy(&A));
  PetscCall(PetscFinalize());
  return 0;
}
