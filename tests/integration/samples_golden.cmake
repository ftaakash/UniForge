# Integration test (spec §7.2, B4): `uniforge_cli gen-samples` regenerates the committed sample
# files in data/ byte for byte, and `uniforge_cli inspect` imports every generated file cleanly.
# Run by CTest as: cmake -DCLI=... -DDATA_DIR=... -DWORK_DIR=... -P samples_golden.cmake

foreach(var CLI DATA_DIR WORK_DIR)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "samples_golden.cmake: -D${var}=... is required")
    endif()
endforeach()

file(REMOVE_RECURSE "${WORK_DIR}")

execute_process(
    COMMAND "${CLI}" gen-samples --data "${WORK_DIR}"
    RESULT_VARIABLE exit_code
    OUTPUT_VARIABLE output
    ERROR_VARIABLE errors)
if(NOT exit_code EQUAL 0)
    message(FATAL_ERROR "uniforge_cli gen-samples exited with ${exit_code}\n${output}\n${errors}")
endif()

set(samples
    geometry/rod_10x10mm_1m.stl
    geometry/heatsink_fins.stl
    thermal/rod_nodal_temperature_SYNTHETIC.csv)
set(failures "")
set(generated "")
foreach(name IN LISTS samples)
    if(NOT EXISTS "${DATA_DIR}/${name}")
        string(APPEND failures "  missing committed sample: data/${name}\n")
        continue()
    endif()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E compare_files "${DATA_DIR}/${name}" "${WORK_DIR}/${name}"
        RESULT_VARIABLE differs)
    if(NOT differs EQUAL 0)
        string(APPEND failures "  differs from the committed copy: data/${name}\n")
    endif()
    list(APPEND generated "${WORK_DIR}/${name}")
endforeach()

execute_process(
    COMMAND "${CLI}" inspect ${generated}
    RESULT_VARIABLE exit_code
    OUTPUT_VARIABLE output
    ERROR_VARIABLE errors)
if(NOT exit_code EQUAL 0)
    string(APPEND failures "  inspect rejected a generated sample:\n${output}${errors}")
endif()

if(failures)
    message(FATAL_ERROR "Sample data check failed:\n${failures}")
endif()
message(STATUS "Sample data OK: 3 files identical to data/ and imported cleanly\n${output}")
