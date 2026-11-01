# uniforge_core: the portable engineering core. C++17 and the standard library only (spec §4.2):
# no Windows, ImGui or other third-party headers, so it also builds with GCC/Clang on Linux.
# The one exception is perf/SystemInfo_<platform>.cpp (CPU, RAM and OS facts for Task D).
add_library(uniforge_core STATIC
    common/AtomicFile.cpp
    common/CsvWriter.cpp
    common/MetaJson.cpp
    common/Rng.cpp
    common/SvgPlot.cpp
    hw/Clock.cpp
    hw/DeviceStateMachine.cpp
    hw/MockTransport.cpp
    hw/RetryPolicy.cpp
    io/ImportErrors.cpp
    io/LineReader.cpp
    io/StlParser.cpp
    io/StlWriter.cpp
    io/TextParse.cpp
    io/ThermalCsvParser.cpp
    perf/BatchGenerator.cpp
    perf/Benchmark.cpp
    perf/BenchmarkReportIO.cpp
    perf/ParallelRunner.cpp
    perf/SerialRunner.cpp
    perf/SystemInfo.cpp
    perf/WorkerPool.cpp
    thermal/Analytical.cpp
    thermal/Assembler.cpp
    thermal/ConvergenceStudy.cpp
    thermal/DirichletBC.cpp
    thermal/ErrorAnalysis.cpp
    thermal/RodProblem.cpp
    thermal/RodSolver.cpp
    thermal/ThomasSolver.cpp)
if(WIN32)
    target_sources(uniforge_core PRIVATE perf/SystemInfo_win.cpp)
else()
    target_sources(uniforge_core PRIVATE perf/SystemInfo_linux.cpp)
endif()
target_include_directories(uniforge_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
uniforge_target_defaults(uniforge_core)

# The worker pool uses std::thread (pthreads on Linux).
find_package(Threads REQUIRED)
target_link_libraries(uniforge_core PUBLIC Threads::Threads)

# Compiler flags of this build for the benchmark report (perf/SystemInfo.cpp), per configuration.
set(uniforge_config_flags
    "$<$<CONFIG:Debug>:${CMAKE_CXX_FLAGS_DEBUG}>$<$<CONFIG:Release>:${CMAKE_CXX_FLAGS_RELEASE}>$<$<CONFIG:RelWithDebInfo>:${CMAKE_CXX_FLAGS_RELWITHDEBINFO}>$<$<CONFIG:MinSizeRel>:${CMAKE_CXX_FLAGS_MINSIZEREL}>")
if(MSVC)
    set(uniforge_extra_flags "$<IF:$<CONFIG:Debug>,/MTd,/MT> /W4 /utf-8 /permissive-")
else()
    set(uniforge_extra_flags "-std=c++17 -Wall -Wextra")
    if(UNIFORGE_SANITIZE)
        string(APPEND uniforge_extra_flags " -fsanitize=${UNIFORGE_SANITIZE}")
    endif()
endif()
string(STRIP "${CMAKE_CXX_FLAGS}" uniforge_base_flags)
set_source_files_properties(perf/SystemInfo.cpp PROPERTIES COMPILE_DEFINITIONS
    "UNIFORGE_BUILD_FLAGS=\"${uniforge_base_flags} ${uniforge_config_flags} ${uniforge_extra_flags}\"")
