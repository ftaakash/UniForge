#include "app/views/PlaceholderView.hpp"

#include "app/ui/widgets/Widgets.hpp"

namespace uniforge::views {

void PlaceholderView::draw(AppContext& /*ctx*/) {
    ui::beginCard("##placeholder");
    ui::emptyState(text_.emptyTitle, text_.emptyBody);
    ui::endCard();
}

} // namespace uniforge::views
