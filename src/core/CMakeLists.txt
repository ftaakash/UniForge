# uniforge_core: the portable engineering core. C++17 and the standard library only (spec §4.2):
# no Windows, ImGui or other third-party headers, so it also builds with GCC/Clang on Linux.
# The one exception is perf/SystemInfo_<platform>.cpp (CPU, RAM and OS facts for Task D).
add_library(uniforge_core STATIC
    common/AtomicFile.cpp
    common/CsvWriter.cpp
    common/MetaJson.cpp
    common/Rng.cpp
    common/SvgPlot.cpp
    hw/Adapters.cpp
    hw/Clock.cpp
    hw/DeviceStateMachine.cpp
    hw/MockAdapter.cpp
    hw/MockTransport.cpp
    hw/RetryPolicy.cpp
    fem3d/Assembly.cpp
    fem3d/Design.cpp
    fem3d/Element.cpp
    fem3d/HexMesh.cpp
    fem3d/Post.cpp
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
    ml/GradientCheck.cpp
    ml/TinyNet.cpp
    ml/Trainer.cpp
    solvers/Observers.cpp
    solvers/amg/Amg.cpp
    solvers/direct/Cholesky.cpp
    solvers/direct/Ordering.cpp
    solvers/external/HypreSolver.cpp
    solvers/SolverFactory.cpp
    solvers/krylov/Ic0.cpp
    solvers/krylov/Lanczos.cpp
    solvers/krylov/Pcg.cpp
    solvers/krylov/Preconditioner.cpp
    sparse/CsrMatrix.cpp
    sparse/KernelPool.cpp
    sparse/SparseOps.cpp
    sparse/VectorOps.cpp
    study/Collect.cpp
    study/Figures.cpp
    study/Plan.cpp
    study/Runner.cpp
    thermal/Analytical.cpp
    thermal/Assembler.cpp
    thermal/ConvergenceStudy.cpp
    thermal/DirichletBC.cpp
    thermal/ErrorAnalysis.cpp
    thermal/RodProblem.cpp
    thermal/RodSolver.cpp
    thermal/ThomasSolver.cpp
    viz/Colormap.cpp
    viz/Image.cpp
    viz/PngWriter.cpp
    viz/Raster3D.cpp
    viz/Spy.cpp
    viz/SvgCanvas.cpp)
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

# Optional reference solver (V3f): hypre, built separately and found through HYPRE_ROOT. Off by
# default, so the normal build stays offline and dependency-free (DEPENDENCIES.md).
option(UNIFORGE_WITH_HYPRE "Link hypre (BoomerAMG) as the external reference solver" OFF)
if(UNIFORGE_WITH_HYPRE)
    set(HYPRE_ROOT "" CACHE PATH "hypre install prefix (contains lib/cmake/HYPRE)")
    enable_language(C) # hypre is a C library; its package config looks for OpenMP::OpenMP_C
    find_package(HYPRE CONFIG REQUIRED PATHS "${HYPRE_ROOT}" NO_DEFAULT_PATH)
    find_package(OpenMP REQUIRED COMPONENTS C CXX)
    target_link_libraries(uniforge_core PRIVATE HYPRE::HYPRE OpenMP::OpenMP_CXX)
    target_compile_definitions(uniforge_core PRIVATE UNIFORGE_HAVE_HYPRE=1)
endif()

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
