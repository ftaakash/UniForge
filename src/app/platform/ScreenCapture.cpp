#include "app/platform/ScreenCapture.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>
#include <wrl/client.h>

#include "app/render/PngWriter.hpp"

using Microsoft::WRL::ComPtr;

namespace uniforge::platform {

namespace {

std::optional<CaptureRequest> g_request;
std::optional<CaptureOutcome> g_outcome;

void fail(const CaptureRequest& r, std::string message) {
    g_outcome = CaptureOutcome{r.file, false, std::move(message)};
}

} // namespace

void requestCapture(CaptureRequest request) {
    g_request = std::move(request);
}

std::optional<CaptureRequest> takeCaptureRequest() {
    auto r = std::move(g_request);
    g_request.reset();
    return r;
}

std::optional<CaptureOutcome> takeCaptureOutcome() {
    auto r = std::move(g_outcome);
    g_outcome.reset();
    return r;
}

void runCapture(ID3D11Device* device, ID3D11DeviceContext* ctx, const CaptureRequest& req) {
    ComPtr<ID3D11RenderTargetView> rtv;
    ctx->OMGetRenderTargets(1, &rtv, nullptr);
    if (!rtv) {
        fail(req, "No render target to capture.");
        return;
    }
    ComPtr<ID3D11Resource> resource;
    rtv->GetResource(&resource);
    ComPtr<ID3D11Texture2D> backBuffer;
    if (FAILED(resource.As(&backBuffer))) {
        fail(req, "The render target is not a 2D texture.");
        return;
    }
    D3D11_TEXTURE2D_DESC desc{};
    backBuffer->GetDesc(&desc);

    // Clamp the rectangle to the back buffer.
    const int x0 = std::clamp(req.x, 0, static_cast<int>(desc.Width));
    const int y0 = std::clamp(req.y, 0, static_cast<int>(desc.Height));
    const int x1 = std::clamp(req.x + req.width, 0, static_cast<int>(desc.Width));
    const int y1 = std::clamp(req.y + req.height, 0, static_cast<int>(desc.Height));
    if (x1 <= x0 || y1 <= y0) {
        fail(req, "The plot is not visible on screen.");
        return;
    }
    const UINT w = static_cast<UINT>(x1 - x0);
    const UINT h = static_cast<UINT>(y1 - y0);

    D3D11_TEXTURE2D_DESC sd{};
    sd.Width = w;
    sd.Height = h;
    sd.MipLevels = 1;
    sd.ArraySize = 1;
    sd.Format = desc.Format;
    sd.SampleDesc.Count = 1;
    sd.Usage = D3D11_USAGE_STAGING;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(device->CreateTexture2D(&sd, nullptr, &staging))) {
        fail(req, "Could not create the capture texture.");
        return;
    }
    const D3D11_BOX box{static_cast<UINT>(x0), static_cast<UINT>(y0), 0,
                        static_cast<UINT>(x1), static_cast<UINT>(y1), 1};
    ctx->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, backBuffer.Get(), 0, &box);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        fail(req, "Could not read the captured pixels.");
        return;
    }
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(w) * h * 4);
    const bool bgra = desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM;
    for (UINT y = 0; y < h; ++y) {
        const auto* src = static_cast<const std::uint8_t*>(mapped.pData) + y * mapped.RowPitch;
        std::uint8_t* dst = &rgba[static_cast<std::size_t>(y) * w * 4];
        std::memcpy(dst, src, static_cast<std::size_t>(w) * 4);
        for (UINT i = 0; i < w; ++i) {
            if (bgra) {
                std::swap(dst[i * 4], dst[i * 4 + 2]);
            }
            dst[i * 4 + 3] = 255; // the swap chain alpha is not meaningful
        }
    }
    ctx->Unmap(staging.Get(), 0);

    const auto saved = render::savePng(req.file, rgba.data(), w, h);
    if (!saved) {
        fail(req, saved.error().message);
        return;
    }
    g_outcome = CaptureOutcome{req.file, true, {}};
}

} // namespace uniforge::platform
