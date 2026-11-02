# Warning levels for UniForge's own code. Third-party targets do not use this.

function(uniforge_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W4>)
        if(UNIFORGE_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/WX>)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
            -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual -Wnull-dereference)
        if(UNIFORGE_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
            # GCC 13 reports false-positive null dereferences inside libstdc++ (std::string)
            # when ASan/UBSan instrumentation is on: keep the warning, do not fail on it.
            if(UNIFORGE_SANITIZE)
                target_compile_options(${target} PRIVATE -Wno-error=null-dereference)
            endif()
        endif()
    endif()
endfunction()
