#include "scroll_viewer.h"

#include <algorithm>
#include <cmath>

namespace openxaml {
namespace {

constexpr double kScrollBarExtent = 16.0;
const DependencyProperty* const kPadding =
    RegisterProperty("ScrollViewer", "Padding", {Thickness{}, false, true});
const DependencyProperty* const kHorizontalVisibility = RegisterProperty(
    "ScrollViewer", "HorizontalScrollBarVisibility",
    {static_cast<int>(ScrollBarVisibility::Disabled), false, true});
const DependencyProperty* const kVerticalVisibility = RegisterProperty(
    "ScrollViewer", "VerticalScrollBarVisibility",
    {static_cast<int>(ScrollBarVisibility::Auto), false, true});
const DependencyProperty* const kHorizontalMode = RegisterProperty(
    "ScrollViewer", "HorizontalScrollMode", {static_cast<int>(ScrollMode::Auto), false, true});
const DependencyProperty* const kVerticalMode = RegisterProperty(
    "ScrollViewer", "VerticalScrollMode", {static_cast<int>(ScrollMode::Auto), false, true});
const DependencyProperty* const kBringIntoView = RegisterProperty(
    "ScrollViewer", "BringIntoViewOnFocusChange", {true, false, false});
const DependencyProperty* const kVerticalChaining = RegisterProperty(
    "ScrollViewer", "IsVerticalScrollChainingEnabled", {true, false, false});
const DependencyProperty* const kBackground =
    RegisterProperty("ScrollViewer", "Background", {std::string(), false, false});
const std::vector<std::string> kOwners = {
    "ScrollViewer", "ContentControl", "Control", kTextPropertyOwner,
    "FrameworkElement", "UIElement"};

bool Forced(ScrollBarVisibility visibility) {
    return visibility == ScrollBarVisibility::Visible;
}

bool Automatic(ScrollBarVisibility visibility) {
    return visibility == ScrollBarVisibility::Auto;
}

}  // namespace

const std::vector<std::string>& ScrollViewer::Owners() { return kOwners; }
const DependencyProperty& ScrollViewer::PaddingProperty() { return *kPadding; }
const DependencyProperty& ScrollViewer::HorizontalScrollBarVisibilityProperty() { return *kHorizontalVisibility; }
const DependencyProperty& ScrollViewer::VerticalScrollBarVisibilityProperty() { return *kVerticalVisibility; }
const DependencyProperty& ScrollViewer::HorizontalScrollModeProperty() { return *kHorizontalMode; }
const DependencyProperty& ScrollViewer::VerticalScrollModeProperty() { return *kVerticalMode; }
const DependencyProperty& ScrollViewer::BringIntoViewOnFocusChangeProperty() { return *kBringIntoView; }
const DependencyProperty& ScrollViewer::IsVerticalScrollChainingEnabledProperty() { return *kVerticalChaining; }

void ScrollViewer::SetContent(std::unique_ptr<Element> content) {
    if (content) Adopt(*content);
    content_ = std::move(content);
}

bool ScrollViewer::AxisCanScroll(ScrollBarVisibility visibility, ScrollMode mode) const {
    return visibility != ScrollBarVisibility::Disabled && mode != ScrollMode::Disabled;
}

Size ScrollViewer::MeasureOverride(Size available) {
    const Thickness inset = padding();
    const Size client{std::max(0.0, available.width - inset.horizontal()),
                      std::max(0.0, available.height - inset.vertical())};
    horizontal_bar_visible_ = Forced(horizontal_scroll_bar_visibility());
    vertical_bar_visible_ = Forced(vertical_scroll_bar_visibility());
    const std::vector<Element*> children = Children();
    Element* content = children.empty() ? nullptr : children.front();
    if (!content) return {inset.horizontal() + (vertical_bar_visible_ ? kScrollBarExtent : 0.0),
                          inset.vertical() + (horizontal_bar_visible_ ? kScrollBarExtent : 0.0)};

    const bool can_h = AxisCanScroll(horizontal_scroll_bar_visibility(), horizontal_scroll_mode());
    const bool can_v = AxisCanScroll(vertical_scroll_bar_visibility(), vertical_scroll_mode());
    content->Measure({can_h ? kInfinity : client.width,
                      can_v ? kInfinity : client.height});
    extent_ = content->desired_size();

    // Auto bars interact: reserving one can make the other axis overflow.
    // Two passes reach the stable pair without baking ordering into the result.
    for (int pass = 0; pass < 2; ++pass) {
        const double viewport_width = std::max(0.0, client.width -
            (vertical_bar_visible_ ? kScrollBarExtent : 0.0));
        const double viewport_height = std::max(0.0, client.height -
            (horizontal_bar_visible_ ? kScrollBarExtent : 0.0));
        if (Automatic(horizontal_scroll_bar_visibility()) && can_h &&
            std::isfinite(viewport_width) && GreaterThan(extent_.width, viewport_width))
            horizontal_bar_visible_ = true;
        if (Automatic(vertical_scroll_bar_visibility()) && can_v &&
            std::isfinite(viewport_height) && GreaterThan(extent_.height, viewport_height))
            vertical_bar_visible_ = true;
    }

    const double bars_width = vertical_bar_visible_ ? kScrollBarExtent : 0.0;
    const double bars_height = horizontal_bar_visible_ ? kScrollBarExtent : 0.0;
    Size desired{extent_.width + inset.horizontal() + bars_width,
                 extent_.height + inset.vertical() + bars_height};
    if (std::isfinite(available.width)) desired.width = std::min(desired.width, available.width);
    if (std::isfinite(available.height)) desired.height = std::min(desired.height, available.height);
    return desired;
}

Size ScrollViewer::ArrangeOverride(Size final_size) {
    const Thickness inset = padding();
    viewport_ = {
        std::max(0.0, final_size.width - inset.horizontal() -
                          (vertical_bar_visible_ ? kScrollBarExtent : 0.0)),
        std::max(0.0, final_size.height - inset.vertical() -
                          (horizontal_bar_visible_ ? kScrollBarExtent : 0.0))};
    ScrollTo(horizontal_offset_, vertical_offset_);
    const std::vector<Element*> children = Children();
    Element* content = children.empty() ? nullptr : children.front();
    if (content) {
        const double width = AxisCanScroll(horizontal_scroll_bar_visibility(), horizontal_scroll_mode())
                                 ? std::max(extent_.width, viewport_.width)
                                 : viewport_.width;
        const double height = AxisCanScroll(vertical_scroll_bar_visibility(), vertical_scroll_mode())
                                  ? std::max(extent_.height, viewport_.height)
                                  : viewport_.height;
        content->Arrange({inset.left - horizontal_offset_, inset.top - vertical_offset_, width, height});
    }
    return final_size;
}

void ScrollViewer::ScrollTo(double horizontal, double vertical) {
    horizontal_offset_ = std::max(0.0, std::min(horizontal, std::max(0.0, extent_.width - viewport_.width)));
    vertical_offset_ = std::max(0.0, std::min(vertical, std::max(0.0, extent_.height - viewport_.height)));
}

}  // namespace openxaml
