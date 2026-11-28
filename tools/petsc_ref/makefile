# Builds the PETSc reference driver (V3f). Needs PETSC_DIR (and PETSC_ARCH) of a PETSc build:
#   make -f makefile petsc_ref
CFLAGS   =
FFLAGS   =
CPPFLAGS =
CLEANFILES = petsc_ref

include ${PETSC_DIR}/lib/petsc/conf/variables
include ${PETSC_DIR}/lib/petsc/conf/rules

petsc_ref: petsc_ref.o
	-${CLINKER} -o petsc_ref petsc_ref.o ${PETSC_KSP_LIB}
	${RM} petsc_ref.o
