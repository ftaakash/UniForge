#pragma once

#include <array>

#include <imgui.h>

// Data-visualisation tokens (spec §8.2). Kept apart from Theme.cpp so the colour tables of the
// plots and the 3D viewport live in one place; like Theme.cpp it is the only place with
// literal colours for what it covers.
namespace uniforge::ui {

// Fixed series colours: the same mesh size always gets the same colour in every plot.
// Index 0..3 = N 10/20/40/80 (Okabe–Ito), 4..5 = extras. Other indices wrap.
[[nodiscard]] ImVec4 seriesColor(int index);
// Index for a mesh size N of the Task C study (10, 20, 40, 80; others map to the extras).
[[nodiscard]] int seriesIndexForMesh(int elements);

// Reference curves (exact solution, slope-2 guide) use text.primary, dashed, drawn on top.
[[nodiscard]] ImVec4 referenceColor();

// Mesh fill and picking ring in the 3D viewport, and the viewport background.
[[nodiscard]] ImVec4 meshFillColor();
[[nodiscard]] ImVec4 meshWireColor();
[[nodiscard]] ImVec4 pickColor();
[[nodiscard]] ImVec4 viewportBackground();
// Axes triad colours x/y/z.
[[nodiscard]] ImVec4 axisColor(int axis);

// Inferno colour map (CC0) at t in [0, 1] (clamped).
[[nodiscard]] std::array<float, 3> inferno(float t);
[[nodiscard]] ImVec4 colormapColor(float t);

} // namespace uniforge::ui
