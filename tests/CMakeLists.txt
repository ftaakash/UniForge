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
        unit/common/SvgPlotTest.cpp
    LIBS uniforge_core)

uniforge_add_test(unit_services
    LABEL unit
    SOURCES
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
