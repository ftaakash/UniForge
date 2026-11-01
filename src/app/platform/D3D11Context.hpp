#pragma once

#include <windows.h>

#include <d3d11.h>
#include <dxgi1_2.h>
#include <string>
#include <wrl/client.h>

namespace uniforge::platform {

enum class RenderBackend {
    Hardware,    // a real GPU driver
    BasicRender, // hardware driver type, but no GPU driver is installed: Windows supplied the
                 // "Microsoft Basic Render Driver" (software). Seen in Windows Sandbox and VMs.
    Warp,        // explicit WARP device after the hardware attempt failed (or --force-warp)
};

struct RenderInfo {
    RenderBackend backend = RenderBackend::Hardware;
    std::string adapter;       // DXGI adapter description, e.g. "Microsoft Basic Render Driver"
    std::string featureLevel;  // "11_0"
    std::string hardwareError; // why no GPU is rendering (BasicRender and Warp only)

    // Rendering happens on the CPU: the status bar shows "Software rendering".
    [[nodiscard]] bool software() const noexcept { return backend != RenderBackend::Hardware; }

    // "D3D11 HW · NVIDIA GeForce … · FL 11_0" / "D3D11 WARP (software) · … · FL 11_1"
    [[nodiscard]] std::string describe() const;
};

// Direct3D 11 device, immediate context and flip-model swap chain for one window.
// Tries a hardware device first and falls back to WARP (the software rasteriser that ships
// with Windows), so the app also starts on VMs and Windows Sandbox without a GPU driver.
class D3D11Context {
public:
    enum class PresentResult { Ok, Occluded, DeviceLost };

    // forceWarp skips the hardware attempt (--force-warp, used to test the fallback path).
    bool create(HWND hwnd, bool forceWarp, std::string& error);
    void destroy();
    // Drops every D3D object and creates a new device + swap chain (after device removal).
    bool recreate(std::string& error);

    // Resizes the back buffers; returns false if the device was lost while doing so.
    bool resize(UINT width, UINT height);
    void beginFrame(const float clearColor[4]);
    PresentResult present();
    // True while the window is fully covered or on a locked screen (skip rendering).
    bool occluded();

    [[nodiscard]] ID3D11Device* device() const noexcept { return device_.Get(); }
    [[nodiscard]] ID3D11DeviceContext* context() const noexcept { return context_.Get(); }
    [[nodiscard]] const RenderInfo& info() const noexcept { return info_; }
    // Reason reported by the driver for the last DeviceLost result.
    [[nodiscard]] HRESULT lostReason() const noexcept { return lostReason_; }

private:
    bool createDevice(D3D_DRIVER_TYPE type, std::string& error);
    bool createSwapChain(std::string& error);
    void createRenderTarget();
    void readAdapterInfo();

    HWND hwnd_ = nullptr;
    bool forceWarp_ = false;
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> renderTarget_;
    D3D_FEATURE_LEVEL featureLevel_ = D3D_FEATURE_LEVEL_11_0;
    RenderInfo info_;
    HRESULT lostReason_ = S_OK;
};

} // namespace uniforge::platform
