#pragma once

#include "app/views/View.hpp"

namespace uniforge::views {

// About / Diagnostics (spec §8.6): version, build, render backend, both launch-to-ready
// numbers, paths, licences and the AI-assistance disclosure.
class AboutView final : public View {
public:
    [[nodiscard]] const char* title() const override { return "About"; }
    [[nodiscard]] const char* tag() const override { return ""; }
    [[nodiscard]] const char* purpose() const override {
        return "Version, render backend, launch timing, file locations and licences.";
    }
    void draw(AppContext& ctx) override;
};

} // namespace uniforge::views
