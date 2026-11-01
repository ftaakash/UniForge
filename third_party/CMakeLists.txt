# Vendored third-party code (versions and licences in DEPENDENCIES.md).
# These targets deliberately skip uniforge_target_defaults(): upstream code keeps its own warning
# level, and the include directories are SYSTEM so /W4 /WX applies only to UniForge sources.

if(UNIFORGE_BUILD_APP)
    add_library(imgui STATIC
        imgui/imgui.cpp
        imgui/imgui_draw.cpp
        imgui/imgui_tables.cpp
        imgui/imgui_widgets.cpp)
    target_include_directories(imgui SYSTEM PUBLIC imgui)
    target_compile_features(imgui PUBLIC cxx_std_17)
    target_compile_options(imgui PRIVATE /utf-8)

    add_library(implot STATIC
        implot/implot.cpp
        implot/implot_items.cpp)
    target_include_directories(implot SYSTEM PUBLIC implot)
    target_link_libraries(implot PUBLIC imgui)
    target_compile_options(implot PRIVATE /utf-8)
endif()

if(UNIFORGE_BUILD_TESTS)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    # Keep GoogleTest on the same static runtime (/MT) as the rest of the build.
    set(gtest_force_shared_crt OFF CACHE BOOL "" FORCE)
    add_subdirectory(googletest EXCLUDE_FROM_ALL)
    # Treat GoogleTest headers as system headers so test code builds cleanly with /W4 /WX.
    set_target_properties(gtest gtest_main PROPERTIES SYSTEM TRUE)
endif()
