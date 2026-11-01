#pragma once

#include <array>
#include <memory>

#include "app/AppContext.hpp"
#include "app/ui/widgets/LogPanel.hpp"
#include "app/views/View.hpp"

namespace uniforge::ui {

// Fixed application layout (spec §8.6): NavRail | header + view + LogPanel | StatusBar.
// Layout comes from theme tokens, not saved docking state, so a fresh machine looks right.
class AppShell {
public:
    AppShell();
    void draw(AppContext& ctx);

private:
    std::array<std::unique_ptr<views::View>, kViewCount> views_;
    ViewId current_ = ViewId::Overview; // first run opens on Overview (spec §8.5)
    LogPanel log_;
};

} // namespace uniforge::ui
