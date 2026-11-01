# uniforge_services: application services shared by the GUI (and later the CLI).
# Portable C++17 + standard library only, so it also builds and is sanitizer-tested on Linux.
add_library(uniforge_services STATIC
    DemoJob.cpp
    GeometryModel.cpp
    DeviceManager.cpp
    JobRunner.cpp
    LogSink.cpp
    ThermalStudy.cpp)
target_include_directories(uniforge_services PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(uniforge_services PUBLIC uniforge_core)
find_package(Threads REQUIRED)
target_link_libraries(uniforge_services PUBLIC Threads::Threads)
uniforge_target_defaults(uniforge_services)
