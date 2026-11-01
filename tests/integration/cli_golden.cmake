# Integration test (spec §10, N8): the CLI regenerates the FEA evidence byte for byte.
#   1. `uniforge_cli fea` into <WORK_DIR>/run1, `uniforge_cli all` (fea + gen-samples) into
#      <WORK_DIR>/run2
#   2. every golden CSV in <GOLDEN_DIR> equals the CSV from run 1
#   3. run 1 and run 2 produce identical CSVs and SVGs (only the .meta.json sidecars differ)
# Run by CTest as: cmake -DCLI=... -DGOLDEN_DIR=... -DWORK_DIR=... -P cli_golden.cmake

foreach(var CLI GOLDEN_DIR WORK_DIR)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "cli_golden.cmake: -D${var}=... is required")
    endif()
endforeach()

file(REMOVE_RECURSE "${WORK_DIR}")

foreach(run IN ITEMS "fea;run1" "all;run2")
    list(GET run 0 command)
    list(GET run 1 dir)
    execute_process(
        COMMAND "${CLI}" ${command} --out "${WORK_DIR}/${dir}" --data "${WORK_DIR}/${dir}/data"
        RESULT_VARIABLE exit_code
        OUTPUT_VARIABLE output
        ERROR_VARIABLE errors)
    if(NOT exit_code EQUAL 0)
        message(FATAL_ERROR "uniforge_cli ${command} exited with ${exit_code}\n${output}\n${errors}")
    endif()
endforeach()

file(GLOB golden_files RELATIVE "${GOLDEN_DIR}" "${GOLDEN_DIR}/*.csv")
list(LENGTH golden_files golden_count)
if(golden_count EQUAL 0)
    message(FATAL_ERROR "No golden CSV files in ${GOLDEN_DIR}")
endif()

set(failures "")
foreach(name IN LISTS golden_files)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E compare_files "${GOLDEN_DIR}/${name}" "${WORK_DIR}/run1/csv/${name}"
        RESULT_VARIABLE differs)
    if(NOT differs EQUAL 0)
        string(APPEND failures "  differs from golden: ${name}\n")
    endif()
endforeach()

file(GLOB_RECURSE generated RELATIVE "${WORK_DIR}/run1" "${WORK_DIR}/run1/*.csv" "${WORK_DIR}/run1/*.svg")
list(LENGTH generated generated_count)
foreach(name IN LISTS generated)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E compare_files "${WORK_DIR}/run1/${name}" "${WORK_DIR}/run2/${name}"
        RESULT_VARIABLE differs)
    if(NOT differs EQUAL 0)
        string(APPEND failures "  not reproducible between runs: ${name}\n")
    endif()
endforeach()

if(failures)
    message(FATAL_ERROR "CLI evidence check failed:\n${failures}")
endif()
message(STATUS "CLI evidence OK: ${golden_count} CSVs match golden files; "
               "${generated_count} CSV/SVG files identical across two runs")
