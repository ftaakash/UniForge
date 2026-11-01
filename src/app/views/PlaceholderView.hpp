#pragma once

#include "app/views/View.hpp"

namespace uniforge::views {

// A task view that is not built yet: header plus an EmptyState that says when it arrives.
class PlaceholderView final : public View {
public:
    struct Text {
        const char* title;
        const char* tag;
        const char* purpose;
        const char* emptyTitle;
        const char* emptyBody;
    };

    explicit PlaceholderView(Text text)
        : text_(text) {}

    [[nodiscard]] const char* title() const override { return text_.title; }
    [[nodiscard]] const char* tag() const override { return text_.tag; }
    [[nodiscard]] const char* purpose() const override { return text_.purpose; }
    void draw(AppContext& ctx) override;

private:
    Text text_;
};

} // namespace uniforge::views
