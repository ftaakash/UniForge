#pragma once

#include <d3d11.h>
#include <filesystem>
#include <optional>
#include <string>

namespace uniforge::platform {

// One pending "save this part of the window as PNG" request (plot export, spec §8.4 PlotPanel).
// A view records the plot's screen rectangle while drawing; App runs the capture after Dear
// ImGui has rendered into the back buffer and before Present, so the PNG shows exactly the
// frame on screen.
struct CaptureRequest {
    std::filesystem::path file;
    int x = 0, y = 0, width = 0, height = 0; // pixels in the back buffer
};

// Global single-slot queue (UI thread only).
void requestCapture(CaptureRequest request);
[[nodiscard]] std::optional<CaptureRequest> takeCaptureRequest();

// Result of the last finished capture for the view to report ("Saved T_vs_x.png"); taken once.
struct CaptureOutcome {
    std::filesystem::path file;
    bool ok = false;
    std::string message; // error text when !ok
};
[[nodiscard]] std::optional<CaptureOutcome> takeCaptureOutcome();

// Copies the region from the render target currently bound to `ctx` (the back buffer) into a
// staging texture and saves it as PNG. Records the outcome for takeCaptureOutcome().
void runCapture(ID3D11Device* device, ID3D11DeviceContext* ctx, const CaptureRequest& request);

} // namespace uniforge::platform
