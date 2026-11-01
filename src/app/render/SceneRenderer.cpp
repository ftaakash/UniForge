#include "app/render/SceneRenderer.hpp"

#include <cstring>
#include <vector>

#include "app/services/GeometryModel.hpp"
#include "app/ui/ThemeViz.hpp"

// fxc-compiled bytecode (cmake/Shaders.cmake).
#include "scene_mesh_ps.h"
#include "scene_mesh_vs.h"
#include "scene_points_ps.h"
#include "scene_points_vs.h"

using Microsoft::WRL::ComPtr;

namespace uniforge::render {

namespace {

constexpr DXGI_FORMAT kColorFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr DXGI_FORMAT kDepthFormat = DXGI_FORMAT_D32_FLOAT;
constexpr UINT kColormapTexels = 256;
constexpr UINT kWantedSamples = 4;

// Constant buffers mirror the HLSL cbuffers (16-byte aligned rows).
struct MeshConstants {
    float viewProj[16];
    float offset[4];
    float color[4];
    float lightDir[4];
    float params[4];
};

struct PointConstants {
    float viewProj[16];
    float offset[4];
    float viewport[4];
};

void copyMatrix(const Mat4& m, float out[16]) {
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            out[r * 4 + c] = static_cast<float>(m.m[r][c]);
        }
    }
}

void copyVec(Vec3 v, float out[4], float w = 0.0f) {
    out[0] = static_cast<float>(v.x);
    out[1] = static_cast<float>(v.y);
    out[2] = static_cast<float>(v.z);
    out[3] = w;
}

ComPtr<ID3D11Buffer> makeBuffer(ID3D11Device* device, UINT bytes, UINT bind, const void* data,
                                bool dynamic = false) {
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = bytes;
    desc.BindFlags = bind;
    desc.Usage = dynamic ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_IMMUTABLE;
    desc.CPUAccessFlags = dynamic ? D3D11_CPU_ACCESS_WRITE : 0;
    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = data;
    ComPtr<ID3D11Buffer> buffer;
    device->CreateBuffer(&desc, data ? &init : nullptr, &buffer);
    return buffer;
}

template <class T>
void updateConstants(ID3D11DeviceContext* ctx, ID3D11Buffer* cb, const T& value) {
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(ctx->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        std::memcpy(mapped.pData, &value, sizeof value);
        ctx->Unmap(cb, 0);
    }
}

} // namespace

struct SceneRenderer::Impl {
    ID3D11Device* device = nullptr; // not owned; identity check only together with generation
    std::uint64_t generation = ~0ULL;
    UINT samples = 1;

    ComPtr<ID3D11VertexShader> meshVs, pointsVs;
    ComPtr<ID3D11PixelShader> meshPs, pointsPs;
    ComPtr<ID3D11InputLayout> meshLayout, pointsLayout;
    ComPtr<ID3D11Buffer> meshCb, pointsCb;
    ComPtr<ID3D11RasterizerState> solidRs, wireRs;
    ComPtr<ID3D11DepthStencilState> depthLess, depthLessEqual;
    ComPtr<ID3D11BlendState> opaque;
    ComPtr<ID3D11SamplerState> linearClamp;
    ComPtr<ID3D11ShaderResourceView> colormapSrv;

    // Render target: MSAA colour + depth, resolved into `resolved` that ImGui samples.
    int width = 0, height = 0;
    ComPtr<ID3D11Texture2D> colorTex, resolvedTex, depthTex;
    ComPtr<ID3D11RenderTargetView> rtv;
    ComPtr<ID3D11DepthStencilView> dsv;
    ComPtr<ID3D11ShaderResourceView> resolvedSrv;

    // Uploaded models (pointer identity of the model they came from).
    const MeshModel* meshSource = nullptr;
    ComPtr<ID3D11Buffer> meshVb;
    UINT meshVertices = 0;
    const PointModel* pointsSource = nullptr;
    ComPtr<ID3D11Buffer> pointsPosVb, pointsValueVb;
    UINT pointCount = 0;

    bool createPipeline(std::string& error);
    bool resizeTarget(int w, int h);
    void releaseAll();
};

void SceneRenderer::Impl::releaseAll() {
    *this = Impl{};
}

bool SceneRenderer::Impl::createPipeline(std::string& error) {
    HRESULT hr =
        device->CreateVertexShader(g_scene_mesh_vs, sizeof g_scene_mesh_vs, nullptr, &meshVs);
    if (SUCCEEDED(hr)) {
        hr = device->CreatePixelShader(g_scene_mesh_ps, sizeof g_scene_mesh_ps, nullptr, &meshPs);
    }
    if (SUCCEEDED(hr)) {
        hr = device->CreateVertexShader(g_scene_points_vs, sizeof g_scene_points_vs, nullptr,
                                        &pointsVs);
    }
    if (SUCCEEDED(hr)) {
        hr = device->CreatePixelShader(g_scene_points_ps, sizeof g_scene_points_ps, nullptr,
                                       &pointsPs);
    }
    if (SUCCEEDED(hr)) {
        const D3D11_INPUT_ELEMENT_DESC mesh[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };
        hr = device->CreateInputLayout(mesh, 2, g_scene_mesh_vs, sizeof g_scene_mesh_vs,
                                       &meshLayout);
    }
    if (SUCCEEDED(hr)) {
        const D3D11_INPUT_ELEMENT_DESC points[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1},
            {"TEXCOORD", 0, DXGI_FORMAT_R32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        };
        hr = device->CreateInputLayout(points, 2, g_scene_points_vs, sizeof g_scene_points_vs,
                                       &pointsLayout);
    }
    if (FAILED(hr)) {
        error = "creating the viewport shaders failed (HRESULT 0x" +
                std::to_string(static_cast<unsigned long>(hr)) + ")";
        return false;
    }
    meshCb = makeBuffer(device, sizeof(MeshConstants), D3D11_BIND_CONSTANT_BUFFER, nullptr, true);
    pointsCb =
        makeBuffer(device, sizeof(PointConstants), D3D11_BIND_CONSTANT_BUFFER, nullptr, true);

    D3D11_RASTERIZER_DESC rs{};
    rs.FillMode = D3D11_FILL_SOLID;
    rs.CullMode = D3D11_CULL_NONE; // STL winding is not reliable
    rs.DepthClipEnable = TRUE;
    rs.MultisampleEnable = TRUE;
    rs.AntialiasedLineEnable = FALSE;
    device->CreateRasterizerState(&rs, &solidRs);
    rs.FillMode = D3D11_FILL_WIREFRAME;
    rs.DepthBias = -1000; // pull the wire in front of its own faces
    rs.SlopeScaledDepthBias = -1.0f;
    device->CreateRasterizerState(&rs, &wireRs);

    D3D11_DEPTH_STENCIL_DESC ds{};
    ds.DepthEnable = TRUE;
    ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    ds.DepthFunc = D3D11_COMPARISON_LESS;
    device->CreateDepthStencilState(&ds, &depthLess);
    ds.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    device->CreateDepthStencilState(&ds, &depthLessEqual);

    D3D11_BLEND_DESC bd{};
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    device->CreateBlendState(&bd, &opaque);

    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    device->CreateSamplerState(&sd, &linearClamp);

    // Inferno colour map as a 1D texture.
    std::vector<std::uint8_t> texels(kColormapTexels * 4);
    for (UINT i = 0; i < kColormapTexels; ++i) {
        const auto rgb = ui::inferno(static_cast<float>(i) / (kColormapTexels - 1));
        for (int c = 0; c < 3; ++c) {
            texels[i * 4 + static_cast<UINT>(c)] =
                static_cast<std::uint8_t>(rgb[static_cast<std::size_t>(c)] * 255.0f + 0.5f);
        }
        texels[i * 4 + 3] = 255;
    }
    D3D11_TEXTURE1D_DESC td{};
    td.Width = kColormapTexels;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = kColorFormat;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = texels.data();
    ComPtr<ID3D11Texture1D> tex;
    if (FAILED(device->CreateTexture1D(&td, &init, &tex)) ||
        FAILED(device->CreateShaderResourceView(tex.Get(), nullptr, &colormapSrv))) {
        error = "creating the colour-map texture failed";
        return false;
    }

    // MSAA 4× if both formats support it; otherwise single-sampled.
    UINT colorQ = 0, depthQ = 0;
    device->CheckMultisampleQualityLevels(kColorFormat, kWantedSamples, &colorQ);
    device->CheckMultisampleQualityLevels(kDepthFormat, kWantedSamples, &depthQ);
    samples = (colorQ > 0 && depthQ > 0) ? kWantedSamples : 1;
    return meshCb && pointsCb && solidRs && wireRs && depthLess && opaque && linearClamp;
}

bool SceneRenderer::Impl::resizeTarget(int w, int h) {
    if (w == width && h == height && rtv) {
        return true;
    }
    colorTex.Reset();
    resolvedTex.Reset();
    depthTex.Reset();
    rtv.Reset();
    dsv.Reset();
    resolvedSrv.Reset();
    width = w;
    height = h;

    D3D11_TEXTURE2D_DESC td{};
    td.Width = static_cast<UINT>(w);
    td.Height = static_cast<UINT>(h);
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = kColorFormat;
    td.SampleDesc.Count = samples;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | (samples == 1 ? D3D11_BIND_SHADER_RESOURCE : 0U);
    if (FAILED(device->CreateTexture2D(&td, nullptr, &colorTex)) ||
        FAILED(device->CreateRenderTargetView(colorTex.Get(), nullptr, &rtv))) {
        return false;
    }
    if (samples > 1) {
        td.SampleDesc.Count = 1;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(device->CreateTexture2D(&td, nullptr, &resolvedTex))) {
            return false;
        }
    } else {
        resolvedTex = colorTex;
    }
    if (FAILED(device->CreateShaderResourceView(resolvedTex.Get(), nullptr, &resolvedSrv))) {
        return false;
    }
    td.Format = kDepthFormat;
    td.SampleDesc.Count = samples;
    td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    return SUCCEEDED(device->CreateTexture2D(&td, nullptr, &depthTex)) &&
           SUCCEEDED(device->CreateDepthStencilView(depthTex.Get(), nullptr, &dsv));
}

SceneRenderer::SceneRenderer()
    : impl_(std::make_unique<Impl>()) {}

SceneRenderer::~SceneRenderer() = default;

int SceneRenderer::msaaSamples() const noexcept {
    return static_cast<int>(impl_->samples);
}

bool SceneRenderer::prepare(ID3D11Device* device, std::uint64_t deviceGeneration,
                            std::string& error) {
    if (impl_->device == device && impl_->generation == deviceGeneration && impl_->meshVs) {
        return true;
    }
    impl_->releaseAll();
    impl_->device = device;
    impl_->generation = deviceGeneration;
    if (!device) {
        error = "no Direct3D device";
        return false;
    }
    if (!impl_->createPipeline(error)) {
        impl_->meshVs.Reset(); // retried next frame
        return false;
    }
    return true;
}

void SceneRenderer::setMesh(const std::shared_ptr<const MeshModel>& mesh) {
    Impl& d = *impl_;
    if (mesh.get() == d.meshSource && (d.meshVb || !mesh)) {
        return;
    }
    d.meshSource = mesh.get();
    d.meshVb.Reset();
    d.meshVertices = 0;
    if (!mesh || mesh->positions.empty() || !d.device) {
        return;
    }
    const std::size_t verts = mesh->positions.size() / 3;
    std::vector<float> interleaved(verts * 6);
    for (std::size_t i = 0; i < verts; ++i) {
        std::memcpy(&interleaved[i * 6], &mesh->positions[i * 3], 3 * sizeof(float));
        std::memcpy(&interleaved[i * 6 + 3], &mesh->normals[i * 3], 3 * sizeof(float));
    }
    d.meshVb = makeBuffer(d.device, static_cast<UINT>(interleaved.size() * sizeof(float)),
                          D3D11_BIND_VERTEX_BUFFER, interleaved.data());
    d.meshVertices = d.meshVb ? static_cast<UINT>(verts) : 0;
}

void SceneRenderer::setPoints(const std::shared_ptr<const PointModel>& points) {
    Impl& d = *impl_;
    if (points.get() == d.pointsSource && (d.pointsPosVb || !points)) {
        return;
    }
    d.pointsSource = points.get();
    d.pointsPosVb.Reset();
    d.pointsValueVb.Reset();
    d.pointCount = 0;
    if (!points || points->values.empty() || !d.device) {
        return;
    }
    d.pointsPosVb =
        makeBuffer(d.device, static_cast<UINT>(points->positions.size() * sizeof(float)),
                   D3D11_BIND_VERTEX_BUFFER, points->positions.data());
    d.pointsValueVb = makeBuffer(d.device, static_cast<UINT>(points->values.size() * sizeof(float)),
                                 D3D11_BIND_VERTEX_BUFFER, points->values.data());
    d.pointCount =
        (d.pointsPosVb && d.pointsValueVb) ? static_cast<UINT>(points->values.size()) : 0;
}

ID3D11ShaderResourceView* SceneRenderer::render(ID3D11DeviceContext* ctx, int widthPx, int heightPx,
                                                const SceneParams& p) {
    Impl& d = *impl_;
    if (!d.meshVs || widthPx <= 0 || heightPx <= 0 || !d.resizeTarget(widthPx, heightPx)) {
        return nullptr;
    }
    ID3D11RenderTargetView* rtv = d.rtv.Get();
    ctx->OMSetRenderTargets(1, &rtv, d.dsv.Get());
    ctx->ClearRenderTargetView(rtv, p.background.data());
    ctx->ClearDepthStencilView(d.dsv.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
    D3D11_VIEWPORT vp{0.0f, 0.0f, static_cast<float>(widthPx), static_cast<float>(heightPx),
                      0.0f, 1.0f};
    ctx->RSSetViewports(1, &vp);
    const float blendFactor[4] = {0, 0, 0, 0};
    ctx->OMSetBlendState(d.opaque.Get(), blendFactor, 0xFFFFFFFF);
    ctx->GSSetShader(nullptr, nullptr, 0);

    if (p.showMesh && d.meshVertices > 0) {
        MeshConstants c{};
        copyMatrix(p.viewProj, c.viewProj);
        copyVec(p.meshOffset, c.offset);
        copyVec(p.lightDir, c.lightDir);
        std::memcpy(c.color, p.meshFill.data(), sizeof c.color);
        updateConstants(ctx, d.meshCb.Get(), c);

        const UINT stride = 6 * sizeof(float), offset = 0;
        ID3D11Buffer* vb = d.meshVb.Get();
        ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        ctx->IASetInputLayout(d.meshLayout.Get());
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(d.meshVs.Get(), nullptr, 0);
        ctx->PSSetShader(d.meshPs.Get(), nullptr, 0);
        ID3D11Buffer* cb = d.meshCb.Get();
        ctx->VSSetConstantBuffers(0, 1, &cb);
        ctx->PSSetConstantBuffers(0, 1, &cb);
        ctx->RSSetState(d.solidRs.Get());
        ctx->OMSetDepthStencilState(d.depthLess.Get(), 0);
        ctx->Draw(d.meshVertices, 0);

        if (p.wireframe) {
            std::memcpy(c.color, p.meshWire.data(), sizeof c.color);
            c.params[0] = 1.0f;
            updateConstants(ctx, d.meshCb.Get(), c);
            ctx->RSSetState(d.wireRs.Get());
            ctx->OMSetDepthStencilState(d.depthLessEqual.Get(), 0);
            ctx->Draw(d.meshVertices, 0);
        }
    }

    if (p.showPoints && d.pointCount > 0) {
        PointConstants c{};
        copyMatrix(p.viewProj, c.viewProj);
        copyVec(p.pointOffset, c.offset);
        c.viewport[0] = static_cast<float>(widthPx);
        c.viewport[1] = static_cast<float>(heightPx);
        c.viewport[2] = p.pointSizePx;
        updateConstants(ctx, d.pointsCb.Get(), c);

        ID3D11Buffer* vbs[2] = {d.pointsPosVb.Get(), d.pointsValueVb.Get()};
        const UINT strides[2] = {3 * sizeof(float), sizeof(float)};
        const UINT offsets[2] = {0, 0};
        ctx->IASetVertexBuffers(0, 2, vbs, strides, offsets);
        ctx->IASetInputLayout(d.pointsLayout.Get());
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        ctx->VSSetShader(d.pointsVs.Get(), nullptr, 0);
        ctx->PSSetShader(d.pointsPs.Get(), nullptr, 0);
        ID3D11Buffer* cb = d.pointsCb.Get();
        ctx->VSSetConstantBuffers(0, 1, &cb);
        ID3D11ShaderResourceView* srv = d.colormapSrv.Get();
        ctx->PSSetShaderResources(0, 1, &srv);
        ID3D11SamplerState* sampler = d.linearClamp.Get();
        ctx->PSSetSamplers(0, 1, &sampler);
        ctx->RSSetState(d.solidRs.Get());
        ctx->OMSetDepthStencilState(d.depthLess.Get(), 0);
        ctx->DrawInstanced(4, d.pointCount, 0, 0);
    }

    if (d.samples > 1) {
        ctx->ResolveSubresource(d.resolvedTex.Get(), 0, d.colorTex.Get(), 0, kColorFormat);
    }
    ID3D11RenderTargetView* none = nullptr;
    ctx->OMSetRenderTargets(1, &none, nullptr);
    return d.resolvedSrv.Get();
}

} // namespace uniforge::render
