# uniforge_cli.exe: headless evidence generator on the same core as the GUI (spec D7).
# Portable C++17 + uniforge_core, so it also builds on Linux and the golden-file test runs there.
# The few OS services of import-bench (process memory, child process) sit behind
# platform/Process.hpp with one source file per platform.
add_executable(uniforge_cli
    main.cpp
    commands/BenchCommand.cpp
    commands/BackpropCommand.cpp
    commands/FeaCommand.cpp
    commands/ImportBenchCommand.cpp
    commands/SamplesCommand.cpp)
if(WIN32)
    target_sources(uniforge_cli PRIVATE cli.manifest platform/Process_win.cpp)
else()
    target_sources(uniforge_cli PRIVATE platform/Process_posix.cpp)
endif()
target_include_directories(uniforge_cli PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(uniforge_cli PRIVATE uniforge_core)
target_compile_definitions(uniforge_cli PRIVATE
    UNIFORGE_VERSION="${PROJECT_VERSION}"
    UNIFORGE_GIT_COMMIT="${UNIFORGE_GIT_COMMIT}")
uniforge_target_defaults(uniforge_cli)
