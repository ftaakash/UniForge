# HLSL shaders are compiled to bytecode headers with fxc from the Windows SDK at build time.
# The app then creates shaders from those byte arrays and needs no D3DCompile() or
# d3dcompiler_47.dll at run time.

cmake_host_system_information(RESULT _uniforge_kits_root
    QUERY WINDOWS_REGISTRY "HKLM/SOFTWARE/Microsoft/Windows Kits/Installed Roots"
    VALUE KitsRoot10)
find_program(UNIFORGE_FXC fxc
    HINTS
        "$ENV{WindowsSdkVerBinPath}/x64"
        "${_uniforge_kits_root}/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64"
    DOC "HLSL compiler from the Windows SDK")

# uniforge_compile_shader(<out_header_var> SOURCE <file.hlsl> PROFILE <vs_4_0|ps_4_0> VARIABLE <name>)
# Adds a build step that writes <binary dir>/shaders/<source name>.h holding `const BYTE <name>[]`.
function(uniforge_compile_shader out_var)
    cmake_parse_arguments(SH "" "SOURCE;PROFILE;VARIABLE" "" ${ARGN})
    if(NOT UNIFORGE_FXC)
        message(FATAL_ERROR "fxc.exe not found. Install the Windows 10/11 SDK (it ships with the "
                            "'Desktop development with C++' workload).")
    endif()
    get_filename_component(name "${SH_SOURCE}" NAME_WE)
    set(header "${CMAKE_CURRENT_BINARY_DIR}/shaders/${name}.h")
    add_custom_command(
        OUTPUT "${header}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
        COMMAND "${UNIFORGE_FXC}" /nologo /O3 /WX /T ${SH_PROFILE} /E main
                /Vn ${SH_VARIABLE} /Fh "${header}" "${SH_SOURCE}"
        MAIN_DEPENDENCY "${SH_SOURCE}"
        COMMENT "fxc ${SH_PROFILE} ${name}.hlsl"
        VERBATIM)
    set(${out_var} "${header}" PARENT_SCOPE)
endfunction()
