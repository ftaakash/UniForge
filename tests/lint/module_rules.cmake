# Module dependency rules of the V2 core (build spec §4.2), checked as a CTest test:
#   solvers, sparse and viz must not include fem3d; sparse must not include solvers.
# Usage: cmake -DSRC=<repo>/src/core -P module_rules.cmake
set(rules
    "solvers|core/fem3d/"
    "sparse|core/fem3d/"
    "sparse|core/solvers/"
    "viz|core/fem3d/"
    "viz|core/solvers/")
set(violations "")
foreach(rule IN LISTS rules)
    string(REPLACE "|" ";" parts "${rule}")
    list(GET parts 0 module)
    list(GET parts 1 forbidden)
    file(GLOB_RECURSE files "${SRC}/${module}/*.hpp" "${SRC}/${module}/*.cpp")
    foreach(f IN LISTS files)
        file(STRINGS "${f}" hits REGEX "#include \"${forbidden}")
        if(hits)
            list(APPEND violations "${f}: ${hits}")
        endif()
    endforeach()
endforeach()
if(violations)
    string(REPLACE ";" "\n  " text "${violations}")
    message(FATAL_ERROR "Module dependency rule broken (build spec §4.2):\n  ${text}")
endif()
message(STATUS "Module dependency rules hold.")
