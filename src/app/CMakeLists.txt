# UniForge.exe: Win32 + Direct3D 11 + Dear ImGui front end (presentation layer).
# Depends on uniforge_services (and through it uniforge_core); nothing depends on it.

# --- Dear ImGui platform/renderer backends with fxc-precompiled shaders ------------------
uniforge_compile_shader(IMGUI_VS_HEADER
    SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/shaders/imgui_dx11_vs.hlsl"
    PROFILE vs_4_0 VARIABLE g_imgui_dx11_vs)
uniforge_compile_shader(IMGUI_PS_HEADER
    SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/shaders/imgui_dx11_ps.hlsl"
    PROFILE ps_4_0 VARIABLE g_imgui_dx11_ps)

set(IMGUI_BACKENDS "${PROJECT_SOURCE_DIR}/third_party/imgui/backends")
add_library(imgui_backends STATIC
    "${IMGUI_BACKENDS}/imgui_impl_win32.cpp"
    "${IMGUI_BACKENDS}/imgui_impl_dx11.cpp"
    "${IMGUI_VS_HEADER}"
    "${IMGUI_PS_HEADER}")
target_include_directories(imgui_backends SYSTEM PUBLIC "${IMGUI_BACKENDS}")
target_include_directories(imgui_backends PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/shaders")
# Patch in third_party/patches/: use the bytecode headers instead of D3DCompile().
target_compile_definitions(imgui_backends PRIVATE IMGUI_IMPL_DX11_PRECOMPILED_SHADERS)
target_compile_options(imgui_backends PRIVATE /utf-8)
target_link_libraries(imgui_backends PUBLIC imgui d3d11 dxgi dwmapi)

# --- Build information and resources -----------------------------------------------------
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
string(STRIP "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_RELEASE} /MT /W4 /WX /utf-8 /permissive-"
       UNIFORGE_RELEASE_FLAGS)
configure_file(BuildInfo.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/generated/app/BuildInfo.hpp" @ONLY)

set(UNIFORGE_FONTS_DIR "${PROJECT_SOURCE_DIR}/third_party/fonts")
set(UNIFORGE_RC_NAME "UniForge")
set(UNIFORGE_RC_DESCRIPTION "UniForge Unified Engineering Workbench")
configure_file(app.rc.in "${CMAKE_CURRENT_BINARY_DIR}/generated/app.rc" @ONLY)

# --- UniForge.exe ------------------------------------------------------------------------
add_executable(UniForge WIN32
    main.cpp
    App.cpp
    platform/D3D11Context.cpp
    platform/LaunchTiming.cpp
    platform/Paths.cpp
    platform/Resources.cpp
    platform/SystemTheme.cpp
    platform/Text.cpp
    platform/Win32Window.cpp
    ui/AppShell.cpp
    ui/Fonts.cpp
    ui/Theme.cpp
    ui/widgets/LogPanel.cpp
    ui/widgets/NavRail.cpp
    ui/widgets/StatusBar.cpp
    ui/widgets/Widgets.cpp
    views/AboutView.cpp
    views/OverviewView.cpp
    views/PlaceholderView.cpp
    app.manifest
    "${CMAKE_CURRENT_BINARY_DIR}/generated/app.rc")
target_include_directories(UniForge PRIVATE
    "${PROJECT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
    "${CMAKE_CURRENT_SOURCE_DIR}") # resource.h for rc.exe
target_link_libraries(UniForge PRIVATE uniforge_services imgui implot imgui_backends shell32 ole32)
uniforge_target_defaults(UniForge)
