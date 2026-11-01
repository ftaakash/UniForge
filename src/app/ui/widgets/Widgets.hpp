#pragma once

#include <imgui.h>

#include "app/ui/Theme.hpp"

// Small shared building blocks (spec §8.4). Every view uses these instead of styling ImGui
// widgets itself, so the same thing looks the same everywhere.
namespace uniforge::ui {

// Title, task chip and one-line purpose at the top of each view.
void viewHeader(const char* title, const char* tag, const char* purpose);

// Rounded surface panel with card padding. Always pair with endCard().
// width 0 = fill the available width; height grows with the content.
void beginCard(const char* id, float width = 0.0f);
void endCard();

// Card heading in the semibold face.
void cardTitle(const char* text);

// Centred placeholder for a view or panel with nothing to show yet.
void emptyState(const char* title, const char* body);

// Small rounded label, e.g. the "Task C" chip or "SIMULATED".
void chip(const char* text, const ImVec4& color);

// Glyph + word in the status colour, then optional detail text (never colour-only).
void statusLabel(Status status, const char* word, const char* detail = nullptr);

// Accent-filled main action. When disabled, `disabledReason` is shown as a tooltip.
bool primaryButton(const char* label, bool enabled = true, const char* disabledReason = nullptr);
bool secondaryButton(const char* label, bool enabled = true, const char* disabledReason = nullptr);

// Text in text.secondary.
void secondaryText(const char* fmt, ...) IM_FMTARGS(1);

} // namespace uniforge::ui
