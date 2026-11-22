# Runs `uniforge_cli study batch --only T0` into WORK_DIR and checks that every planned run left a
# converged result, that collect wrote the experiment table, and that a second batch resumes
# without executing anything (build spec §9, N6).
file(REMOVE_RECURSE "${WORK_DIR}")
execute_process(COMMAND "${CLI}" study batch --only T0 --allow-dirty --out "${WORK_DIR}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "study batch failed (${rc}):\n${out}\n${err}")
endif()
foreach(id REF DIRECT-ND PCG-J AMG-PCG)
    set(f "${WORK_DIR}/runs/T0-bar-s10.000-${id}.csv")
    if(NOT EXISTS "${f}")
        message(FATAL_ERROR "missing result ${f}")
    endif()
    file(READ "${f}" text)
    if(NOT text MATCHES ",Converged,")
        message(FATAL_ERROR "run ${id} did not converge:\n${text}")
    endif()
endforeach()
if(NOT EXISTS "${WORK_DIR}/csv/T0.csv")
    message(FATAL_ERROR "collect did not write csv/T0.csv")
endif()
execute_process(COMMAND "${CLI}" study batch --only T0 --allow-dirty --out "${WORK_DIR}"
    RESULT_VARIABLE rc2 OUTPUT_VARIABLE out2)
if(NOT out2 MATCHES "0 runs executed, 4 already done")
    message(FATAL_ERROR "resume did not skip finished runs:\n${out2}")
endif()
message(STATUS "study batch smoke test passed")
