#pragma once

#include <cstdint>
#include <vector>

#include "app/AppContext.hpp"
#include "app/services/LogSink.hpp"

namespace uniforge::ui {

// Collapsible log at the bottom of the content area (spec §7.1): level filter, text search,
// copy to clipboard and "open folder". Ctrl+L toggles it.
class LogPanel {
public:
    void draw(const AppContext& ctx, float width, float height);
    [[nodiscard]] bool visible() const noexcept { return visible_; }
    void toggle() noexcept { visible_ = !visible_; }

private:
    void refresh(const LogSink& log);
    [[nodiscard]] bool passesFilter(const LogEntry& e) const;
    void drawToolbar(const AppContext& ctx);
    void drawLines();

    bool visible_ = true;
    int minLevel_ = 1; // index into LogLevel: Info and above by default
    char search_[128] = {};
    std::vector<LogEntry> entries_;
    std::vector<int> shown_; // indices into entries_ that pass the filter
    std::uint64_t seenVersion_ = ~0ull;
    bool filterDirty_ = true;
    bool scrollToBottom_ = true;
};

} // namespace uniforge::ui
