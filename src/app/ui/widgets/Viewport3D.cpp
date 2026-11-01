#include "app/ui/widgets/Viewport3D.hpp"

#include <algorithm>
#include <cstdio>

#include "app/render/Picking.hpp"
#include "app/ui/Fonts.hpp"
#include "app/ui/ThemeViz.hpp"
#include "app/ui/widgets/DataViews.hpp"
#include "app/ui/widgets/Widgets.hpp"

namespace uniforge::ui {

namespace {

constexpr float kClickSlopPx = 3.0f; // a press that moves less than this is a click (pick)

std::array<float, 4> rgba(const ImVec4& c) {
    return {c.x, c.y, c.z, c.w};
}

render::Vec3 toVec(const io::Vec3d& v) {
    return {v.x, v.y, v.z};
}

render::Mat4 translation(render::Vec3 t) {
    render::Mat4 m = render::Mat4::identity();
    m.m[0][3] = t.x;
    m.m[1][3] = t.y;
    m.m[2][3] = t.z;
    return m;
}

} // namespace

void Viewport3D::draw(AppContext& ctx, const ViewportScene& scene, ImVec2 size) {
    const Theme& t = theme();

    // Scene layout: both models are placed relative to one shared origin.
    if (scene.mesh.get() != lastMesh_ || scene.points.get() != lastPoints_) {
        if (scene.points.get() != lastPoints_) {
            picked_.reset();
        }
        lastMesh_ = scene.mesh.get();
        lastPoints_ = scene.points.get();
        const io::Vec3d origin = sceneOrigin(lastMesh_, lastPoints_);
        io::BoundsAccumulator b;
        if (lastMesh_) {
            meshOffset_ = toVec(lastMesh_->origin) - toVec(origin);
            b.add(lastMesh_->source.bbMin);
            b.add(lastMesh_->source.bbMax);
        }
        if (lastPoints_) {
            pointOffset_ = toVec(lastPoints_->origin) - toVec(origin);
            b.add(lastPoints_->source.bbMin);
            b.add(lastPoints_->source.bbMax);
        }
        bounds_ = b.empty() ? render::Bounds{{-0.5, -0.5, -0.5}, {0.5, 0.5, 0.5}}
                            : render::Bounds{toVec(b.min()) - toVec(origin),
                                             toVec(b.max()) - toVec(origin)};
        fitPending_ = true; // auto-fit after every import (spec §8.5)
    }

    // Toolbar.
    if (secondaryButton("Fit  F")) {
        camera_.fit(bounds_);
    }
    ImGui::SameLine(0.0f, t.size.space8);
    if (secondaryButton("Reset  R")) {
        camera_.reset(bounds_);
    }
    ImGui::SameLine(0.0f, t.size.space12);
    ImGui::Checkbox("Wireframe  W", &wireframe_);
    ImGui::SameLine(0.0f, t.size.space12);
    ImGui::Checkbox("Axes", &axes_);
    ImGui::SameLine(0.0f, t.size.space12);
    ImGui::AlignTextToFramePadding();
    secondaryText("LMB orbit · MMB/Shift+LMB pan · wheel zoom · click a point to inspect");

    size.y = std::max(size.y - ImGui::GetFrameHeightWithSpacing(), 120.0f * t.dpiScale);
    size.x = std::max(size.x, 120.0f * t.dpiScale);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    camera_.setViewport(size.x, size.y);
    if (fitPending_) {
        camera_.reset(bounds_);
        fitPending_ = false;
    }

    ImGui::InvisibleButton("##viewport", size,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY); // the wheel zooms instead of scrolling the view
    handleInput(scene, bounds_, origin);

    // Render offscreen and show the result.
    ID3D11ShaderResourceView* image = nullptr;
    if (ctx.gfx &&
        renderer_.prepare(ctx.gfx->device(), ctx.runtime.deviceGeneration, renderError_)) {
        renderer_.setMesh(scene.mesh);
        renderer_.setPoints(scene.points);
        render::SceneParams p;
        p.viewProj = camera_.viewProjection();
        p.lightDir = render::normalize(camera_.target() - camera_.eye());
        p.meshOffset = meshOffset_;
        p.pointOffset = pointOffset_;
        p.showMesh = scene.showMesh;
        p.showPoints = scene.showPoints;
        p.wireframe = wireframe_;
        p.pointSizePx = scene.pointSizePx;
        p.background = rgba(viewportBackground());
        p.meshFill = rgba(meshFillColor());
        p.meshWire = rgba(meshWireColor());
        image = renderer_.render(ctx.gfx->context(), static_cast<int>(size.x),
                                 static_cast<int>(size.y), p);
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 end(origin.x + size.x, origin.y + size.y);
    if (image) {
        dl->AddImage(ImTextureRef(reinterpret_cast<ImTextureID>(image)), origin, end);
    } else {
        dl->AddRectFilled(origin, end, ImGui::GetColorU32(viewportBackground()));
        const std::string msg = "3D view unavailable: " + renderError_;
        dl->AddText(ImVec2(origin.x + t.size.space16, origin.y + t.size.space16),
                    ImGui::GetColorU32(statusColor(Status::Error)), msg.c_str());
    }
    dl->AddRect(origin, end, ImGui::GetColorU32(t.color.borderDefault));
    drawOverlays(scene, origin, size);
}

void Viewport3D::handleInput(const ViewportScene& scene, const render::Bounds& bounds,
                             ImVec2 origin) {
    const ImGuiIO& io = ImGui::GetIO();
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    if (active) {
        const bool panning = ImGui::IsMouseDown(ImGuiMouseButton_Middle) ||
                             (ImGui::IsMouseDown(ImGuiMouseButton_Left) && io.KeyShift);
        if (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f) {
            if (panning) {
                camera_.pan(io.MouseDelta.x, io.MouseDelta.y);
            } else if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                camera_.orbit(io.MouseDelta.x, io.MouseDelta.y);
            }
        }
    }
    if (hovered && io.MouseWheel != 0.0f) {
        camera_.zoom(io.MouseWheel, io.MousePos.x - origin.x, io.MousePos.y - origin.y);
    }
    if (ImGui::IsItemDeactivated() && !io.KeyShift &&
        io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Left] < kClickSlopPx * kClickSlopPx &&
        ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        pick(scene, io.MousePos.x - origin.x, io.MousePos.y - origin.y);
    }
    if (hovered) {
        if (ImGui::IsKeyPressed(ImGuiKey_F, false)) {
            camera_.fit(bounds);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_R, false)) {
            camera_.reset(bounds);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_W, false)) {
            wireframe_ = !wireframe_;
        }
    }
}

void Viewport3D::pick(const ViewportScene& scene, double x, double y) {
    if (!scene.points || !scene.showPoints) {
        return;
    }
    const render::Mat4 vp = camera_.viewProjection() * translation(pointOffset_);
    const auto hit =
        render::pickNearest(scene.points->positions, vp, camera_.width(), camera_.height(), x, y);
    picked_ = hit ? std::optional<std::size_t>(hit->index) : std::nullopt;
}

void Viewport3D::drawOverlays(const ViewportScene& scene, ImVec2 origin, ImVec2 size) {
    const Theme& t = theme();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
    ImGui::PushFont(fonts().ui, t.type.label);

    if (!scene.mesh && !scene.points) {
        const char* msg = "Nothing loaded — open an STL or CSV, drop files here, or Load samples.";
        const ImVec2 ts = ImGui::CalcTextSize(msg);
        dl->AddText(ImVec2(origin.x + (size.x - ts.x) * 0.5f, origin.y + (size.y - ts.y) * 0.5f),
                    ImGui::GetColorU32(t.color.textSecondary), msg);
    }

    // Axes triad, bottom-left: world axes rotated into view space.
    if (axes_) {
        const render::Mat4 view = camera_.view();
        const float len = 34.0f * t.dpiScale;
        const ImVec2 c(origin.x + 48.0f * t.dpiScale, origin.y + size.y - 48.0f * t.dpiScale);
        const char* names[3] = {"X", "Y", "Z"};
        for (int a = 0; a < 3; ++a) {
            const float dx = static_cast<float>(view.m[0][a]);
            const float dy = static_cast<float>(view.m[1][a]);
            const ImVec2 tip(c.x + dx * len, c.y - dy * len);
            const ImU32 col = ImGui::GetColorU32(axisColor(a));
            dl->AddLine(c, tip, col, 2.0f * t.dpiScale);
            dl->AddText(ImVec2(tip.x + dx * 8.0f - 4.0f, tip.y - dy * 8.0f - 7.0f), col, names[a]);
        }
    }

    // Bounding-box dimensions, bottom-right.
    if (scene.mesh || scene.points) {
        const render::Vec3 d = bounds_.max - bounds_.min;
        char text[96];
        std::snprintf(text, sizeof text, "bbox %.4g × %.4g × %.4g m", d.x, d.y, d.z);
        const ImVec2 ts = ImGui::CalcTextSize(text);
        const ImVec2 p(origin.x + size.x - ts.x - t.size.space12,
                       origin.y + size.y - ts.y - t.size.space8);
        dl->AddRectFilled(ImVec2(p.x - 6, p.y - 3), ImVec2(p.x + ts.x + 6, p.y + ts.y + 3),
                          ImGui::GetColorU32(withAlpha(t.color.bgSurface, 0.85f)),
                          t.size.radiusControl);
        dl->AddText(p, ImGui::GetColorU32(t.color.textPrimary), text);
    }

    // Colour legend on the right edge.
    if (scene.points && scene.showPoints) {
        const float h = std::min(size.y - 70.0f * t.dpiScale, 260.0f * t.dpiScale);
        if (h > 80.0f * t.dpiScale) {
            ImVec2 tl(origin.x + size.x, origin.y + t.size.space12);
            const float w = 120.0f * t.dpiScale;
            tl.x -= w + t.size.space12;
            colorLegend(dl, tl, h, scene.points->source.Tmin, scene.points->source.Tmax, "T [°C]");
        }
    }

    // Pick ring.
    if (picked_ && scene.points && *picked_ < scene.points->values.size()) {
        const std::size_t i = *picked_;
        const auto& pos = scene.points->positions;
        const render::Vec3 p{pos[i * 3] + pointOffset_.x, pos[i * 3 + 1] + pointOffset_.y,
                             pos[i * 3 + 2] + pointOffset_.z};
        const auto s = render::projectToScreen(camera_.viewProjection(), p, size.x, size.y);
        if (s.visible) {
            const float r =
                std::max(scene.pointSizePx * 0.5f + 4.0f * t.dpiScale, 7.0f * t.dpiScale);
            dl->AddCircle(
                ImVec2(origin.x + static_cast<float>(s.x), origin.y + static_cast<float>(s.y)), r,
                ImGui::GetColorU32(pickColor()), 0, 2.0f * t.dpiScale);
        }
    }
    ImGui::PopFont();
    dl->PopClipRect();
}

} // namespace uniforge::ui
