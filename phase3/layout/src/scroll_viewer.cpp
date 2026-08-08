#include "scroll_viewer.h"

#include <algorithm>
#include <cmath>

namespace openxaml {
namespace {

// A scrollbar is 16 across its track and never shorter than 32 along it, the
// length of the two line buttons it is built from. Both numbers are recorded:
// L3-scroll-shape-zero-width-child asks for 16x32 around a 0x16 child, which
// is the vertical bar alone, and L3-scroll-free-visible-visible asks for 60x48
// around 60x40, which is that 32 minimum plus the horizontal bar's thickness.
// The horizontal bar's 32 minimum width is the mirror of the vertical bar's
// and is the one number here the corpus does not pin: no recorded case forces
// a horizontal bar next to content narrower than 44.
constexpr double kScrollBarExtent = 16.0;
constexpr double kScrollBarMinLength = 32.0;
const DependencyProperty* const kPadding =
    RegisterProperty("ScrollViewer", "Padding", {Thickness{}, false, true});
const DependencyProperty* const kHorizontalVisibility = RegisterProperty(
    "ScrollViewer", "HorizontalScrollBarVisibility",
    {static_cast<int>(ScrollBarVisibility::Disabled), false, true});
// Visible, not Auto: a bare viewer around a 0x16 child is recorded asking for
// 16x32, which only a bar that is already there can explain.
const DependencyProperty* const kVerticalVisibility = RegisterProperty(
    "ScrollViewer", "VerticalScrollBarVisibility",
    {static_cast<int>(ScrollBarVisibility::Visible), false, true});
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

// What unbounds an axis at measure is the scrollbar visibility alone.
// ScrollMode moves no recorded number: L3-scroll-mode-* pairs cases that
// differ only in HorizontalScrollMode/VerticalScrollMode and records the same
// content size for both, so the mode governs whether an offset may move, not
// what the content was offered.
bool Unbounded(ScrollBarVisibility visibility) {
    return visibility != ScrollBarVisibility::Disabled;
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

Size ScrollViewer::MeasureOverride(Size available) {
    const Thickness inset = padding();
    const Size client{std::max(0.0, available.width - inset.horizontal()),
                      std::max(0.0, available.height - inset.vertical())};
    const std::vector<Element*> children = Children();
    Element* content = children.empty() ? nullptr : children.front();
    extent_ = Size{};
    if (content) {
        const Size offered{Unbounded(horizontal_scroll_bar_visibility()) ? kInfinity : client.width,
                           Unbounded(vertical_scroll_bar_visibility()) ? kInfinity : client.height};
        content->Measure(offered);
        extent_ = content->desired_size();
    }

    // The bars are overlaid on the content rather than subtracted from it --
    // the whole padded client stays the viewport either way -- so an automatic
    // bar decides against the client size and the two axes do not interact.
    horizontal_bar_visible_ =
        Forced(horizontal_scroll_bar_visibility()) ||
        (Automatic(horizontal_scroll_bar_visibility()) && std::isfinite(client.width) &&
         GreaterThan(extent_.width, client.width));
    vertical_bar_visible_ =
        Forced(vertical_scroll_bar_visibility()) ||
        (Automatic(vertical_scroll_bar_visibility()) && std::isfinite(client.height) &&
         GreaterThan(extent_.height, client.height));

    // A visible bar holds open the auto row or column it sits in, and the
    // content spans across both tracks, so each axis is the larger of what the
    // padded content asked for and what the two bars need side by side: the
    // crossing bar's thickness plus this bar's own minimum length.
    const double vertical_track = vertical_bar_visible_ ? kScrollBarExtent : 0.0;
    const double horizontal_track = horizontal_bar_visible_ ? kScrollBarExtent : 0.0;
    Size desired{std::max(extent_.width + inset.horizontal(),
                          vertical_track + (horizontal_bar_visible_ ? kScrollBarMinLength : 0.0)),
                 std::max(extent_.height + inset.vertical(),
                          horizontal_track + (vertical_bar_visible_ ? kScrollBarMinLength : 0.0))};
    if (std::isfinite(available.width)) desired.width = std::min(desired.width, available.width);
    if (std::isfinite(available.height)) desired.height = std::min(desired.height, available.height);
    return desired;
}

Size ScrollViewer::ArrangeOverride(Size final_size) {
    const Thickness inset = padding();
    viewport_ = {std::max(0.0, final_size.width - inset.horizontal()),
                 std::max(0.0, final_size.height - inset.vertical())};
    ScrollTo(horizontal_offset_, vertical_offset_);
    const std::vector<Element*> children = Children();
    Element* content = children.empty() ? nullptr : children.front();
    if (content) {
        // The padding is the presenter's margin, so it moves the presenter and
        // not the content within it: every recorded content slot under a padded
        // viewer sits at the origin.
        content->Arrange({-horizontal_offset_, -vertical_offset_,
                          std::max(extent_.width, viewport_.width),
                          std::max(extent_.height, viewport_.height)});
    }
    return final_size;
}

void ScrollViewer::ScrollTo(double horizontal, double vertical) {
    horizontal_offset_ = std::max(0.0, std::min(horizontal, std::max(0.0, extent_.width - viewport_.width)));
    vertical_offset_ = std::max(0.0, std::min(vertical, std::max(0.0, extent_.height - viewport_.height)));
}

}  // namespace openxaml
