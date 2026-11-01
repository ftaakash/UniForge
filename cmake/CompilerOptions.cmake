# Shared per-target compiler settings. Every UniForge target calls uniforge_target_defaults().

function(uniforge_target_defaults target)
    target_compile_features(${target} PUBLIC cxx_std_17)
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)

    if(MSVC)
        # /utf-8: source and execution charset (unit glyphs such as degree signs).
        # /permissive- and /Zc:__cplusplus: standard conformance.
        # Wrapped in COMPILE_LANGUAGE:CXX so rc.exe (resource scripts) never sees them.
        target_compile_options(${target} PRIVATE
            "$<$<COMPILE_LANGUAGE:CXX>:/utf-8;/permissive-;/Zc:__cplusplus;/EHsc>")
        target_compile_definitions(${target} PRIVATE
            UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
    endif()

    if(UNIFORGE_SANITIZE AND NOT MSVC)
        target_compile_options(${target} PRIVATE
            -fsanitize=${UNIFORGE_SANITIZE} -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=${UNIFORGE_SANITIZE})
    endif()

    uniforge_enable_warnings(${target})
endfunction()
