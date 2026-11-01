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

uniforge_add_test(unit_services
    LABEL unit
    SOURCES
        unit/services/JobRunnerTest.cpp
        unit/services/LogSinkTest.cpp
    LIBS uniforge_services)

