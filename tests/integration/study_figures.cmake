# Runs `uniforge_cli study figures` on the committed official results (results/v2) for a few
# figures that need no large solve, and checks that each SVG was written and is well formed
# (build spec §12.1: every figure exists and maps to a CSV or run).
file(REMOVE_RECURSE "${WORK_DIR}")
execute_process(COMMAND "${CLI}" study figures --out "${RESULTS}" --fig "${WORK_DIR}"
                        --only P7,P8,P11,P14,P17,P18,P21,P22
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "study figures failed (${rc}):\n${out}\n${err}")
endif()
foreach(f P7_mms_convergence P8_spy P11_time_vs_dof P11b_setup_vs_dof P11c_solve_vs_dof
          P14_setup_vs_solve P17_architecture P18_algorithm_flow P21_failure_modes
          P22_bar_vs_cube)
    set(path "${WORK_DIR}/${f}.svg")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "missing figure ${path}")
    endif()
    file(READ "${path}" svg)
    if(NOT svg MATCHES "^<\\?xml" OR NOT svg MATCHES "</svg>\n$")
        message(FATAL_ERROR "figure ${f} is not a complete SVG")
    endif()
endforeach()
file(READ "${WORK_DIR}/P8_spy.svg" spy)
if(NOT spy MATCHES "data:image/png;base64,iVBORw0KGgo")
    message(FATAL_ERROR "P8 does not embed PNG rasters")
endif()
message(STATUS "study figures smoke test passed")
