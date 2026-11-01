#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "app/AppContext.hpp"
#include "app/platform/D3D11Context.hpp"
#include "app/platform/LaunchTiming.hpp"
#include "app/platform/Win32Window.hpp"
#include "app/services/JobRunner.hpp"
#include "app/services/LogSink.hpp"
#include "app/services/ResultStore.hpp"
#include "app/ui/AppShell.hpp"

namespace uniforge {

// Owns the window, the D3D11 device, Dear ImGui and the services, and runs the frame loop.
// Everything here runs on the UI thread; background work goes through JobRunner.
class App {
public:
    App();
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    // Returns the process exit code.
    int run();

private:
    bool startServices();
    bool startWindowAndDevice();
    void startImGui();
    void frame();
    void recoverDevice();
    void applyTheme();
    void recordFirstPresent();
    void fatal(const std::string& what);
    void takeDroppedFiles(WPARAM drop); // WM_DROPFILES wParam (an HDROP)

    platform::LaunchOptions options_;
    RuntimeInfo runtime_;
    LogSink log_;
    ResultStore results_;
    JobRunner jobs_; // after log_ and results_: destroyed (and joined) before them
    platform::Win32Window window_;
    platform::D3D11Context gfx_;
    ui::AppShell shell_;

    std::string iniPathUtf8_; // ImGui keeps a pointer to this
    bool imguiStarted_ = false;
    bool dark_ = false;
    bool occluded_ = false;
    bool resizePending_ = false;
    UINT resizeWidth_ = 0;
    UINT resizeHeight_ = 0;
    bool dpiPending_ = false;
    bool themePending_ = false;
    bool simulatedLossDone_ = false;
    std::uint64_t frames_ = 0;
    std::vector<std::filesystem::path> droppedFiles_; // WM_DROPFILES, handed to the next frame
};

} // namespace uniforge
