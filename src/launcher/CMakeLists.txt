# Launch.exe: starts app\UniForge.exe with the launch timestamp. No application logic and no
# dependency on any other UniForge target.
configure_file(launcher.rc.in "${CMAKE_CURRENT_BINARY_DIR}/generated/launcher.rc" @ONLY)

add_executable(Launch WIN32
    Launch.cpp
    launcher.manifest
    "${CMAKE_CURRENT_BINARY_DIR}/generated/launcher.rc")
uniforge_target_defaults(Launch)
