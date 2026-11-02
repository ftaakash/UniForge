#include "app/App.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <shellapi.h> // DragQueryFileW (after windows.h, which App.hpp includes)
#include <thread>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <implot.h>

#include "app/BuildInfo.hpp"
#include "app/platform/CrashHandler.hpp"
#include "app/platform/Paths.hpp"
#include "app/platform/ScreenCapture.hpp"
#include "app/platform/SystemTheme.hpp"
#include "app/platform/Text.hpp"
#include "app/ui/Fonts.hpp"
#include "app/ui/Theme.hpp"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam,
                                                             LPARAM lParam);

namespace uniforge {

namespace {

constexpr std::uint64_t kSimulatedLossFrame = 120; // ~2 s after start at 60 Hz

std::string formatSeconds(const std::optional<double>& s) {
    if (!s) {
        return "n/a";
    }
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.3f s", *s);
    return buf;
}

} // namespace

App::App()
    : jobs_(&log_) {}

App::~App() {
    if (imguiStarted_) {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImPlot::DestroyContext();
        ImGui::DestroyContext();
    }
    gfx_.destroy();
}

int App::run() {
    if (!startServices() || !startWindowAndDevice()) {
        return 1;
    }
    startImGui();
    window_.show();
    log_.info("ui ready, entering frame loop");
    if (options_.simulateCrash) {
        log_.warn("--simulate-crash: raising an unhandled exception");
        platform::simulateCrash();
    }

    while (window_.pumpMessages()) {
        frame();
    }
    log_.info("shutting down");
    jobs_.cancel(); // ~JobRunner joins the job thread
    return 0;
}

bool App::startServices() {
    options_ = platform::parseCommandLine();
    runtime_.paths = platform::resolveAppPaths();
    runtime_.logicalCpus = std::thread::hardware_concurrency();

    const auto& paths = runtime_.paths;
    platform::installCrashHandler(paths.logsDir);
    const bool logOpen = log_.openFile(paths.logsDir / L"uniforge.log");
    log_.info(std::string("UniForge ") + build::kVersion + " (commit " + build::kGitCommit +
              ") starting");
    log_.info("package dir: " + platform::pathToUtf8(paths.packageDir));
    if (paths.packageWritable) {
        log_.info("state dir: " + platform::pathToUtf8(paths.stateDir));
    } else {
        log_.warn("package dir is read-only; logs and imgui.ini fall back to " +
                  platform::pathToUtf8(paths.stateDir));
    }
    if (!logOpen) {
        log_.warn("cannot open log file in " + platform::pathToUtf8(paths.logsDir) +
                  "; logging to memory only");
    }
    for (const auto& arg : options_.unknown) {
        log_.warn("ignored command-line argument: " + arg);
    }
    return true;
}

bool App::startWindowAndDevice() {
    platform::Win32Window::Callbacks callbacks;
    callbacks.preTranslate = [this](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if (msg == WM_DROPFILES) {
            takeDroppedFiles(wParam);
            return true;
        }
        return imguiStarted_ && ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam) != 0;
    };
    callbacks.onResize = [this](UINT w, UINT h) {
        resizePending_ = true;
        resizeWidth_ = w;
        resizeHeight_ = h;
    };
    callbacks.onDpiChanged = [this](float) { dpiPending_ = true; };
    callbacks.onSystemThemeChanged = [this] { themePending_ = true; };

    std::string error;
    if (!window_.create(L"UniForge — Unified Engineering Workbench", std::move(callbacks), error)) {
        fatal("Could not create the main window.\n\n" + error);
        return false;
    }
    if (!gfx_.create(window_.handle(), options_.forceWarp, error)) {
        fatal("Could not initialise Direct3D 11.\n\n" + error);
        return false;
    }
    DragAcceptFiles(window_.handle(), TRUE); // STL/CSV drag-drop onto the Geometry view
    runtime_.render = gfx_.info();
    log_.info("render backend: " + runtime_.render.describe());
    if (runtime_.render.software()) {
        log_.warn("hardware Direct3D 11 device unavailable (" + runtime_.render.hardwareError +
                  "); using WARP software rendering");
    }
    return true;
}

void App::startImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    iniPathUtf8_ = platform::pathToUtf8(runtime_.paths.iniFile);
    io.IniFilename = iniPathUtf8_.c_str();
    io.LogFilename = nullptr;

    ui::FontSet fonts;
    std::string fontError;
    if (!ui::loadFonts(fonts, fontError)) {
        log_.error("font loading failed: " + fontError + "using the built-in font");
    }
    for (const auto& cp : ui::missingGlyphs(fonts)) {
        log_.warn("font glyph missing: " + cp);
    }

    applyTheme();
    ImGui_ImplWin32_Init(window_.handle());
    ImGui_ImplDX11_Init(gfx_.device(), gfx_.context());
    imguiStarted_ = true;
}

void App::applyTheme() {
    dark_ = options_.forceDark.value_or(platform::systemPrefersDarkApps());
    ui::applyTheme(dark_ ? ui::ThemeMode::Dark : ui::ThemeMode::Light, window_.dpiScale());
    window_.setDarkTitleBar(dark_);
    char buf[96];
    std::snprintf(buf, sizeof buf, "theme: %s%s, scale %.0f %%", dark_ ? "dark" : "light",
                  options_.forceDark ? " (--theme)" : " (follows Windows app mode)",
                  window_.dpiScale() * 100.0f);
    log_.info(buf);
}

void App::frame() {
    if (window_.minimized() || (occluded_ && gfx_.occluded())) {
        std::this_thread::sleep_for(std::chrono::milliseconds(15)); // nothing visible to draw
        return;
    }
    occluded_ = false;

    if (resizePending_) {
        resizePending_ = false;
        if (!gfx_.resize(resizeWidth_, resizeHeight_)) {
            recoverDevice();
            return;
        }
    }
    if (dpiPending_ || themePending_) {
        if (dpiPending_) {
            ui::onDpiChanged();
        }
        dpiPending_ = themePending_ = false;
        applyTheme();
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    AppContext ctx{log_, jobs_, results_, runtime_, std::nullopt};
    ctx.gfx = &gfx_;
    ctx.window = window_.handle();
    ctx.droppedFiles.swap(droppedFiles_);
    shell_.draw(ctx);
    ImGui::Render();

    const ImVec4& bg = ui::theme().color.bgApp;
    const float clear[4] = {bg.x, bg.y, bg.z, 1.0f};
    gfx_.beginFrame(clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    if (auto capture = platform::takeCaptureRequest()) { // plot PNG export: this exact frame
        platform::runCapture(gfx_.device(), gfx_.context(), *capture);
    }
    auto result = gfx_.present();
    ++frames_;

    if (options_.simulateDeviceLost && !simulatedLossDone_ && frames_ == kSimulatedLossFrame) {
        simulatedLossDone_ = true;
        log_.warn("--simulate-device-lost: treating this Present as DXGI_ERROR_DEVICE_REMOVED");
        result = platform::D3D11Context::PresentResult::DeviceLost;
    }
    if (result == platform::D3D11Context::PresentResult::DeviceLost) {
        recoverDevice();
        return;
    }
    occluded_ = result == platform::D3D11Context::PresentResult::Occluded;
    if (!runtime_.launchMeasured) {
        recordFirstPresent();
    }
}

void App::recoverDevice() {
    log_.warn("Direct3D device lost (reason " + platform::hresultText(gfx_.lostReason()) +
              "); recreating device, swap chain and UI textures");
    // Restart both backends, not only DX11: in the docking branch ImGui_ImplDX11_Shutdown()
    // calls DestroyPlatformWindows(), which also frees the Win32 backend's main-viewport data.
    // Restarting only the renderer left Win32 reading freed memory on the next frame.
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    std::string error;
    if (!gfx_.recreate(error)) {
        imguiStarted_ = false;
        fatal("The graphics device was lost and could not be recreated.\n\n" + error);
        PostQuitMessage(3);
        return;
    }
    ImGui_ImplWin32_Init(window_.handle());
    ImGui_ImplDX11_Init(gfx_.device(), gfx_.context());
    runtime_.render = gfx_.info();
    ++runtime_.deviceGeneration; // views rebuild their own GPU resources
    log_.info("render backend after recovery: " + runtime_.render.describe());
}

void App::recordFirstPresent() {
    runtime_.launch = platform::measureLaunchTiming(options_.launchT0);
    runtime_.launchMeasured = true;
    const std::string fromLauncher = formatSeconds(runtime_.launch.fromLauncherSeconds);
    const std::string fromProcess = formatSeconds(runtime_.launch.fromProcessSeconds);
    log_.info("launch-to-ready (first Present): " + fromLauncher + " from Launch.exe, " +
              fromProcess + " from UniForge.exe start");

    // One line per launch, kept across runs for the report (spec §7.1, §9).
    std::ofstream launchLog(runtime_.paths.logsDir / L"launch.log", std::ios::app);
    if (launchLog) {
        launchLog << formatTimestampUtc(std::chrono::system_clock::now())
                  << " launch_to_ready=" << fromLauncher << " exe_to_ready=" << fromProcess
                  << " backend=" << (runtime_.render.software() ? "WARP" : "HW") << " adapter=\""
                  << runtime_.render.adapter << "\"\n";
    }
}

void App::takeDroppedFiles(WPARAM wParam) {
    const auto drop = reinterpret_cast<HDROP>(wParam);
    const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    for (UINT i = 0; i < count; ++i) {
        const UINT len = DragQueryFileW(drop, i, nullptr, 0);
        std::wstring path(len, L' ');
        DragQueryFileW(drop, i, path.data(), len + 1);
        droppedFiles_.emplace_back(path);
    }
    DragFinish(drop);
}

void App::fatal(const std::string& what) {
    log_.error(what);
    const std::string text = what + "\n\nLog file: " + platform::pathToUtf8(log_.filePath());
    MessageBoxW(window_.handle(), platform::toWide(text).c_str(), L"UniForge",
                MB_OK | MB_ICONERROR);
}

} // namespace uniforge
