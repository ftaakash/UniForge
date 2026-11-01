# Targets and their allowed dependencies (spec §4.2):
#   core/      uniforge_core      portable C++17, standard library only
#   app/       uniforge_services  JobRunner, ResultStore, LogSink (portable, links core)
#              UniForge.exe       Win32 + D3D11 + Dear ImGui GUI (links services)
#   launcher/  Launch.exe         starts app\UniForge.exe; no application logic
#   cli/       uniforge_cli.exe   headless evidence generator (portable, links core only)

# Commit recorded in the About view and in the evidence sidecars (value at configure time).
find_package(Git QUIET)
set(UNIFORGE_GIT_COMMIT "unknown")
if(GIT_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        OUTPUT_VARIABLE git_commit OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE git_result ERROR_QUIET)
    if(git_result EQUAL 0)
        set(UNIFORGE_GIT_COMMIT "${git_commit}")
    endif()
endif()

add_subdirectory(core)
add_subdirectory(app/services)
add_subdirectory(cli)

if(UNIFORGE_BUILD_APP)
    add_subdirectory(app)
    add_subdirectory(launcher)
endif()
