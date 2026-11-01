#pragma once

#include "app/AppContext.hpp"

namespace uniforge::views {

// One page of the workbench. AppShell draws the header from title/tag/purpose and calls
// draw() for the body inside a scrollable region.
class View {
public:
    virtual ~View() = default;
    [[nodiscard]] virtual const char* title() const = 0;
    [[nodiscard]] virtual const char* tag() const = 0; // "Task C"; empty for none
    [[nodiscard]] virtual const char* purpose() const = 0;
    virtual void draw(AppContext& ctx) = 0;
};

} // namespace uniforge::views
