# Targets and their allowed dependencies (spec §4.2):
#   core/      uniforge_core      portable C++17, standard library only
#   app/       uniforge_services  JobRunner, ResultStore, LogSink (portable, links core)
#              UniForge.exe       Win32 + D3D11 + Dear ImGui GUI (links services)
#   launcher/  Launch.exe         starts app\UniForge.exe; no application logic
#   cli/       uniforge_cli.exe   evidence generator (Phase 2+)

add_subdirectory(core)
add_subdirectory(app/services)

