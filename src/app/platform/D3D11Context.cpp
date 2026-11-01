#include "app/platform/D3D11Context.hpp"

#include <iterator>

#include "app/platform/Text.hpp"

using Microsoft::WRL::ComPtr;

namespace uniforge::platform {

namespace {

const char* featureLevelName(D3D_FEATURE_LEVEL level) {
    switch (level) {
    case D3D_FEATURE_LEVEL_11_1:
        return "11_1";
    case D3D_FEATURE_LEVEL_11_0:
        return "11_0";
    case D3D_FEATURE_LEVEL_10_1:
        return "10_1";
    case D3D_FEATURE_LEVEL_10_0:
        return "10_0";
    default:
        return "other";
    }
}

bool isDeviceLost(HRESULT hr) {
    return hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET;
}

} // namespace

std::string RenderInfo::describe() const {
    std::string s = software() ? "D3D11 WARP (software)" : "D3D11 HW";
    if (!adapter.empty()) {
        s += " · " + adapter;
    }
    s += " · FL " + featureLevel;
    return s;
}

bool D3D11Context::create(HWND hwnd, bool forceWarp, std::string& error) {
    hwnd_ = hwnd;
    forceWarp_ = forceWarp;
    info_ = {};

    std::string hardwareError = "skipped (--force-warp)";
    if (!forceWarp && createDevice(D3D_DRIVER_TYPE_HARDWARE, hardwareError) &&
        createSwapChain(hardwareError)) {
        info_.backend = RenderBackend::Hardware;
    } else {
        destroy();
        if (!createDevice(D3D_DRIVER_TYPE_WARP, error) || !createSwapChain(error)) {
            error =
                "No Direct3D 11 device available. Hardware: " + hardwareError + ". WARP: " + error;
            destroy();
            return false;
        }
        info_.backend = RenderBackend::Warp;
        info_.hardwareError = hardwareError;
    }
    createRenderTarget();
    readAdapterInfo();
    return true;
}

void D3D11Context::destroy() {
    if (context_) {
        context_->ClearState();
        context_->Flush();
    }
    renderTarget_.Reset();
    swapChain_.Reset();
    context_.Reset();
    device_.Reset();
}

bool D3D11Context::recreate(std::string& error) {
    destroy();
    return create(hwnd_, forceWarp_, error);
}

bool D3D11Context::createDevice(D3D_DRIVER_TYPE type, std::string& error) {
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
                                        D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    const HRESULT hr =
        D3D11CreateDevice(nullptr, type, nullptr, 0, levels, static_cast<UINT>(std::size(levels)),
                          D3D11_SDK_VERSION, &device_, &featureLevel_, &context_);
    if (FAILED(hr)) {
        error = "D3D11CreateDevice failed " + hresultText(hr);
        return false;
    }
    return true;
}

bool D3D11Context::createSwapChain(std::string& error) {
    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIFactory2> factory;
    HRESULT hr = device_.As(&dxgiDevice);
    if (SUCCEEDED(hr)) {
        hr = dxgiDevice->GetAdapter(&adapter);
    }
    if (SUCCEEDED(hr)) {
        hr = adapter->GetParent(IID_PPV_ARGS(&factory));
    }
    if (FAILED(hr)) {
        error = "DXGI factory lookup failed " + hresultText(hr);
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.Scaling = DXGI_SCALING_NONE;
    hr =
        factory->CreateSwapChainForHwnd(device_.Get(), hwnd_, &desc, nullptr, nullptr, &swapChain_);
    if (FAILED(hr)) {
        error = "CreateSwapChainForHwnd failed " + hresultText(hr);
        return false;
    }
    factory->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_ALT_ENTER);
    return true;
}

void D3D11Context::createRenderTarget() {
    ComPtr<ID3D11Texture2D> backBuffer;
    if (SUCCEEDED(swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) {
        device_->CreateRenderTargetView(backBuffer.Get(), nullptr, &renderTarget_);
    }
}

void D3D11Context::readAdapterInfo() {
    info_.featureLevel = featureLevelName(featureLevel_);
    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIAdapter1> adapter1;
    DXGI_ADAPTER_DESC1 desc{};
    if (FAILED(device_.As(&dxgiDevice)) || FAILED(dxgiDevice->GetAdapter(&adapter)) ||
        FAILED(adapter.As(&adapter1)) || FAILED(adapter1->GetDesc1(&desc))) {
        return;
    }
    info_.adapter = toUtf8(desc.Description);

    // Without a GPU driver, a "hardware" device request still succeeds on the Microsoft Basic
    // Render Driver (vendor 0x1414, device 0x8C), which rasterises on the CPU like WARP.
    // Found in the Windows Sandbox test: it must be reported as software rendering.
    constexpr UINT kMicrosoftVendor = 0x1414;
    constexpr UINT kBasicRenderDevice = 0x8C;
    const bool softwareAdapter =
        (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0 ||
        (desc.VendorId == kMicrosoftVendor && desc.DeviceId == kBasicRenderDevice);
    if (info_.backend == RenderBackend::Hardware && softwareAdapter) {
        info_.backend = RenderBackend::BasicRender;
        info_.hardwareError = "no GPU driver installed; Windows supplied its software adapter";
    }
}

bool D3D11Context::resize(UINT width, UINT height) {
    if (!swapChain_ || width == 0 || height == 0) {
        return true;
    }
    context_->OMSetRenderTargets(0, nullptr, nullptr);
    renderTarget_.Reset();
    const HRESULT hr = swapChain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    if (isDeviceLost(hr)) {
        lostReason_ = hr == DXGI_ERROR_DEVICE_REMOVED ? device_->GetDeviceRemovedReason() : hr;
        return false;
    }
    createRenderTarget();
    return true;
}

void D3D11Context::beginFrame(const float clearColor[4]) {
    ID3D11RenderTargetView* rtv = renderTarget_.Get();
    context_->OMSetRenderTargets(1, &rtv, nullptr);
    context_->ClearRenderTargetView(rtv, clearColor);
}

D3D11Context::PresentResult D3D11Context::present() {
    const HRESULT hr = swapChain_->Present(1, 0); // vsync
    if (isDeviceLost(hr)) {
        lostReason_ = hr == DXGI_ERROR_DEVICE_REMOVED ? device_->GetDeviceRemovedReason() : hr;
        return PresentResult::DeviceLost;
    }
    return hr == DXGI_STATUS_OCCLUDED ? PresentResult::Occluded : PresentResult::Ok;
}

bool D3D11Context::occluded() {
    return swapChain_ && swapChain_->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED;
}

} // namespace uniforge::platform
