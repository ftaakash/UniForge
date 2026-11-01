# GoogleTest suites, registered with CTest per test case and labelled
# unit / numerical / negative / integration (spec §10). Run one label with `ctest -L unit`.
include(GoogleTest)

# uniforge_add_test(<target> LABEL <label> SOURCES <files...> [LIBS <targets...>])
function(uniforge_add_test target)
    cmake_parse_arguments(T "" "LABEL" "SOURCES;LIBS" ${ARGN})
    add_executable(${target} ${T_SOURCES})
    target_link_libraries(${target} PRIVATE GTest::gtest_main ${T_LIBS})
    uniforge_target_defaults(${target})
    gtest_discover_tests(${target}
        PROPERTIES LABELS ${T_LABEL}
        DISCOVERY_TIMEOUT 30)
endfunction()

uniforge_add_test(unit_common
    LABEL unit
    SOURCES
        unit/common/CsvWriterTest.cpp
        unit/common/MetaJsonTest.cpp
        unit/common/ResultTest.cpp
        unit/common/RngTest.cpp
        unit/common/SvgPlotTest.cpp
    LIBS uniforge_core)

uniforge_add_test(unit_thermal
    LABEL unit
    SOURCES unit/thermal/ElementMatricesTest.cpp
    LIBS uniforge_core)

uniforge_add_test(numerical_thermal
    LABEL numerical
    SOURCES
        numerical/thermal/ConvergenceStudyTest.cpp
        numerical/thermal/RodSolverTest.cpp
    LIBS uniforge_core)
target_include_directories(numerical_thermal PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}") # support/

uniforge_add_test(negative_thermal
    LABEL negative
    SOURCES negative/thermal/ThermalInputTest.cpp
    LIBS uniforge_core)

# Importers (Task B). Fixture files are read from the source tree.
uniforge_add_test(unit_io
    LABEL unit
    SOURCES
        unit/io/LineReaderTest.cpp
        unit/io/StlParserTest.cpp
        unit/io/TextParseTest.cpp
        unit/io/ThermalCsvParserTest.cpp
    LIBS uniforge_core)
target_compile_definitions(unit_io PRIVATE
    UNIFORGE_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

uniforge_add_test(negative_io
    LABEL negative
    SOURCES negative/io/ImportFixtureTest.cpp
    LIBS uniforge_core)
target_compile_definitions(negative_io PRIVATE
    UNIFORGE_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

# The CLI regenerates the committed FEA evidence byte for byte (golden files in golden/fea/).
add_test(NAME integration.cli_fea_matches_golden
    COMMAND "${CMAKE_COMMAND}"
        "-DCLI=$<TARGET_FILE:uniforge_cli>"
        "-DGOLDEN_DIR=${CMAKE_CURRENT_SOURCE_DIR}/golden/fea"
        "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/cli_golden_$<CONFIG>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/integration/cli_golden.cmake")
set_tests_properties(integration.cli_fea_matches_golden PROPERTIES LABELS integration)

# Simulated devices (Task E): ManualClock keeps these instant and deterministic.
uniforge_add_test(unit_hw
    LABEL unit
    SOURCES unit/hw/HwAdapterTest.cpp
    LIBS uniforge_core)

uniforge_add_test(negative_hw
    LABEL negative
    SOURCES negative/hw/HwRecoveryTest.cpp
    LIBS uniforge_core)

uniforge_add_test(unit_services
    LABEL unit
    SOURCES
        unit/services/DeviceManagerTest.cpp
        unit/services/JobRunnerTest.cpp
        unit/services/LogSinkTest.cpp
    LIBS uniforge_services)

if(UNIFORGE_BUILD_APP)
    # stb_truetype (shipped inside Dear ImGui) reads the TTF cmap tables for the coverage test.
    # Its implementation is third-party code, so it is compiled without UniForge's /W4 /WX.
    add_library(test_stb_truetype STATIC support/StbTruetypeImpl.cpp)
    target_include_directories(test_stb_truetype SYSTEM PUBLIC "${PROJECT_SOURCE_DIR}/third_party/imgui")

    uniforge_add_test(unit_ui_fonts
        LABEL unit
        SOURCES unit/ui/FontCoverageTest.cpp
        LIBS test_stb_truetype)
    target_include_directories(unit_ui_fonts PRIVATE "${PROJECT_SOURCE_DIR}/src")
    target_compile_definitions(unit_ui_fonts PRIVATE
        UNIFORGE_FONTS_DIR="${PROJECT_SOURCE_DIR}/third_party/fonts")
endif()

# Task B samples: gen-samples reproduces data/ byte for byte, and the committed files import.
add_test(NAME integration.cli_samples_match_data
    COMMAND "${CMAKE_COMMAND}"
        "-DCLI=$<TARGET_FILE:uniforge_cli>"
        "-DDATA_DIR=${PROJECT_SOURCE_DIR}/data"
        "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/cli_samples_$<CONFIG>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/integration/samples_golden.cmake")
set_tests_properties(integration.cli_samples_match_data PROPERTIES LABELS integration)

uniforge_add_test(integration_samples
    LABEL integration
    SOURCES integration/SampleDataTest.cpp
    LIBS uniforge_core)
target_compile_definitions(integration_samples PRIVATE
    UNIFORGE_DATA_DIR="${PROJECT_SOURCE_DIR}/data")

# Phase 4: UI-less import -> render-model pipeline and the Thermal view's study job.
uniforge_add_test(integration_views
    LABEL integration
    SOURCES integration/GeometryPipelineTest.cpp
    LIBS uniforge_services)
target_compile_definitions(integration_views PRIVATE
    UNIFORGE_DATA_DIR="${PROJECT_SOURCE_DIR}/data")

if(UNIFORGE_BUILD_APP)
    # Phase 4: number formatting (spec §8.3), camera fit/orbit/pan/zoom, picking, PNG export.
    uniforge_add_test(unit_viewcore
        LABEL unit
        SOURCES
            unit/render/CameraTest.cpp
            unit/render/PickingTest.cpp
            unit/render/PngWriterTest.cpp
            unit/ui/FormatTest.cpp
        LIBS uniforge_viewcore)
endif()
# Multicore batch (Task D): Rng/batch/pool units, serial == parallel agreement, benchmark protocol.
uniforge_add_test(unit_perf
    LABEL unit
    SOURCES
        unit/perf/BatchGeneratorTest.cpp
        unit/perf/WorkerPoolTest.cpp
    LIBS uniforge_core)

uniforge_add_test(numerical_perf
    LABEL numerical
    SOURCES
        numerical/perf/BenchmarkTest.cpp
        numerical/perf/RunnerAgreementTest.cpp
    LIBS uniforge_core)

# Task F: one-neuron backprop values, finite-difference check and loss curve.
uniforge_add_test(numerical_ml
    LABEL numerical
    SOURCES numerical/ml/BackpropTest.cpp
    LIBS uniforge_core)

uniforge_add_test(negative_ml
    LABEL negative
    SOURCES negative/ml/BackpropInputTest.cpp
    LIBS uniforge_core)

# Task F evidence: `backprop` and `all` regenerate the golden CSVs in golden/backprop/.
add_test(NAME integration.cli_backprop_matches_golden
    COMMAND "${CMAKE_COMMAND}"
        "-DCLI=$<TARGET_FILE:uniforge_cli>"
        "-DGOLDEN_DIR=${CMAKE_CURRENT_SOURCE_DIR}/golden/backprop"
        "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/cli_backprop_$<CONFIG>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/integration/backprop_golden.cmake")
set_tests_properties(integration.cli_backprop_matches_golden PROPERTIES LABELS integration)
