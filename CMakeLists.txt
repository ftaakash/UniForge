cmake_minimum_required(VERSION 3.25)

project(UniForge
    VERSION 0.1.0
    DESCRIPTION "Unified Engineering Workbench"
    LANGUAGES CXX)

# Static MSVC runtime (/MT, /MTd) so the packaged executables need no VC++ redistributable.
# Must be set before any target is created.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerOptions)
include(Warnings)

option(UNIFORGE_BUILD_TESTS "Build the test suite" ON)
option(UNIFORGE_BUILD_APP "Build the Windows GUI, launcher and CLI" ${WIN32})
option(UNIFORGE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
set(UNIFORGE_SANITIZE "" CACHE STRING
    "GCC/Clang sanitizers to enable, e.g. 'address,undefined' or 'thread' (empty = none)")

if(UNIFORGE_BUILD_APP AND NOT WIN32)
    message(FATAL_ERROR "UNIFORGE_BUILD_APP requires Windows (Win32 + Direct3D 11).")
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

add_subdirectory(src)

if(UNIFORGE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
