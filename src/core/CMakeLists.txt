# uniforge_core: the portable engineering core. Phase 1 only needs the header-only job
# primitives in common/ (CancelToken, Progress); Phase 2 turns this into a static library
# when the first sources (thermal solver, writers) arrive.
add_library(uniforge_core INTERFACE)
target_include_directories(uniforge_core INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(uniforge_core INTERFACE cxx_std_17)
