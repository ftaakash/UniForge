# uniforge_core: the portable engineering core. C++17 and the standard library only (spec §4.2):
# no Windows, ImGui or other third-party headers, so it also builds with GCC/Clang on Linux.
add_library(uniforge_core STATIC
    common/AtomicFile.cpp
    common/CsvWriter.cpp
    common/MetaJson.cpp
    common/SvgPlot.cpp
    io/ImportErrors.cpp
    io/LineReader.cpp
    io/TextParse.cpp
    thermal/Analytical.cpp
    thermal/Assembler.cpp
    thermal/ConvergenceStudy.cpp
    thermal/DirichletBC.cpp
    thermal/ErrorAnalysis.cpp
    thermal/RodProblem.cpp
    thermal/RodSolver.cpp
    thermal/ThomasSolver.cpp)
target_include_directories(uniforge_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
uniforge_target_defaults(uniforge_core)
