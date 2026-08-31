#include "basic_controls.h"

#include <algorithm>
#include <cmath>

namespace openxaml {
namespace {
const std::vector<std::string> kButtonOwners = {
    "Button", "ButtonBase", "ContentControl", "Control", kTextPropertyOwner,
    "FrameworkElement", "UIElement"};
const std::vector<std::string> kViewboxOwners = {
    "Viewbox", "FrameworkElement", "UIElement"};
const std::vector<std::string> kTextBoxOwners = {
    "TextBox", "TextBoxBase", "Control", kTextPropertyOwner, "FrameworkElement", "UIElement"};
const std::vector<std::string> kToolTipOwners = {
    "ToolTip", "ContentControl", "Control", kTextPropertyOwner, "FrameworkElement", "UIElement"};
const std::vector<std::string> kThumbOwners = {
    "Thumb", "Control", kTextPropertyOwner, "FrameworkElement", "UIElement"};
const DependencyProperty* const kText =
    RegisterProperty("TextBox", "Text", {std::string(), false, true});
const DependencyProperty* const kPlaceholder =
    RegisterProperty("TextBox", "PlaceholderText", {std::string(), false, true});
// Corpus-facing properties that do not change this dependency-free layout core.
const DependencyProperty* const kAcceptsReturn =
    RegisterProperty("TextBox", "AcceptsReturn", {false, false, false});
const DependencyProperty* const kReadOnly =
    RegisterProperty("TextBox", "IsReadOnly", {false, false, false});
const DependencyProperty* const kSpellCheck =
    RegisterProperty("TextBox", "IsSpellCheckEnabled", {true, false, false});
const DependencyProperty* const kMaxLength =
    RegisterProperty("TextBox", "MaxLength", {0, false, false});
const DependencyProperty* const kToolTipPlacement =
    RegisterProperty("ToolTip", "Placement", {std::string("Mouse"), false, false});
}  // namespace

const std::vector<std::string>& Button::Owners() { return kButtonOwners; }
const std::vector<std::string>& Viewbox::Owners() { return kViewboxOwners; }
const std::vector<std::string>& TextBox::Owners() { return kTextBoxOwners; }
const std::vector<std::string>& ToolTip::Owners() { return kToolTipOwners; }
const std::vector<std::string>& Thumb::Owners() { return kThumbOwners; }
const DependencyProperty& TextBox::TextProperty() { return *kText; }
const DependencyProperty& TextBox::PlaceholderTextProperty() { return *kPlaceholder; }

void Viewbox::SetChild(std::unique_ptr<Element> child) {
    if (child) Adopt(*child);
    child_ = std::move(child);
}

Size Viewbox::MeasureOverride(Size available) {
    const std::vector<Element*> children = Children();
    Element* const child = children.empty() ? nullptr : children.front();
    if (!child) return {};
    child->Measure({kInfinity, kInfinity});
    const Size natural = child->desired_size();
    double scale = kInfinity;
    if (natural.width > 0.0 && std::isfinite(available.width))
        scale = std::min(scale, available.width / natural.width);
    if (natural.height > 0.0 && std::isfinite(available.height))
        scale = std::min(scale, available.height / natural.height);
    if (!std::isfinite(scale)) scale = 1.0;
    return {natural.width * scale, natural.height * scale};
}

Size Viewbox::ArrangeOverride(Size final_size) {
    const std::vector<Element*> children = Children();
    Element* const child = children.empty() ? nullptr : children.front();
    if (!child) return final_size;
    Size natural = child->desired_size();
    if (natural.width <= 0.0 || natural.height <= 0.0) {
        child->Arrange({0.0, 0.0, 0.0, 0.0});
        return final_size;
    }
    const double scale = std::max(
        0.0, std::min(final_size.width / natural.width,
                      final_size.height / natural.height));
    child->set_visual_transform(VisualTransform::Scale(scale, scale));
    child->Arrange({(final_size.width - natural.width * scale) / 2.0,
                    (final_size.height - natural.height * scale) / 2.0,
                    natural.width, natural.height});
    return final_size;
}

// The two values L7-terminal-24911ba19e reads off the runtime's default
// ToolTip style. The case is a ToolTip around an empty line of text, and the
// oracle records it 18 x 30 with the line at 9 x 6 and 15.9609 tall:
//
//   9 across and 6 down puts the padding's left and top at 9 and 6, an
//   18-wide ToolTip around nothing puts its right at 9, and 30 - 6 - 16 puts
//   its bottom at 8. That is `ToolTipBorderPadding`, "9,6,9,8", in the
//   harvested dictionary; and
//   15.9609 is Segoe UI's line box at 12, where the case environment's own
//   font size is 14. That is `ToolTipContentThemeFontSize`.
//
// They go in the style slot rather than the local one because that is the slot
// a default style fills: markup that sets either property shadows it, and
// content that sets neither inherits the font size through it -- which is how
// the recorded TextBlock, which says nothing about fonts, measures at 12.
//
// Nothing else the style sets is visible to a measurement, so nothing else is
// here. `ToolTipMaxWidth` is in the same dictionary and no recording reaches
// it, so it stays out rather than arriving as a number nothing checks.
ToolTip::ToolTip() {
    SetStyleValue(PaddingProperty(), Thickness{9.0, 6.0, 9.0, 8.0});
    SetStyleValue(FontSizeProperty(), 12.0);
}

// Button has no measurement of its own any more. Its chrome is its default
// style's Padding and BorderThickness counted by ContentControl, and a string
// content is one empty line -- see ContentControl::MeasureOverride for the
// recordings. The 20-across, 32-floor pair that used to be transcribed here
// was the corpus's one Button read as a sum; L5-defaults-builtin-reachability
// then measured the same Button empty at [20, 13] and took the sum apart.

ComboBox::ComboBox() {
    set_background_brush(BrushValue::SolidColor(
        Color{0xff, 0x33, 0x33, 0x33}));
}

Size ComboBox::MeasureOverride(Size available) {
    const Size content = ContentControl::MeasureOverride(available);
    return {std::max(120.0, content.width + 32.0),
            std::max(32.0, content.height)};
}

Size ComboBox::ArrangeOverride(Size final_size) {
    ContentControl::ArrangeOverride(
        {std::max(0.0, final_size.width - 32.0), final_size.height});
    return final_size;
}

ToggleSwitch::ToggleSwitch() {
    set_background_brush(BrushValue::SolidColor(
        Color{0xff, 0x55, 0x55, 0x55}));
}

Size ToggleSwitch::MeasureOverride(Size available) {
    const Size content = ContentControl::MeasureOverride(available);
    return {std::max(40.0, content.width), std::max(20.0, content.height)};
}

Size ToggleSwitch::ArrangeOverride(Size final_size) {
    ContentControl::ArrangeOverride(final_size);
    if (thumb_) {
        constexpr double thumb_size = 16.0;
        const double x = is_on_
            ? std::max(2.0, final_size.width - thumb_size - 2.0)
            : 2.0;
        thumb_->Arrange(
            {x, std::max(0.0, (final_size.height - thumb_size) / 2.0),
             thumb_size, thumb_size});
    }
    return final_size;
}

Size CheckBox::MeasureOverride(Size available) {
    constexpr double indicator_column = 28.0;
    const Size content = ContentControl::MeasureOverride(
        {std::max(0.0, available.width - indicator_column), available.height});
    return {content.width + indicator_column, std::max(20.0, content.height)};
}

Size CheckBox::ArrangeOverride(Size final_size) {
    const std::vector<Element*> children = Children();
    if (!children.empty()) {
        constexpr double indicator_column = 28.0;
        children.front()->Arrange(
            {indicator_column, 0.0,
             std::max(0.0, final_size.width - indicator_column),
             final_size.height});
    }
    if (indicator_) {
        indicator_->Arrange(
            {2.0, std::max(0.0, (final_size.height - 18.0) / 2.0),
             18.0, 18.0});
    }
    return final_size;
}

Size TextBox::MeasureOverride(Size) {
    // The editable text host is supplied by generic.xaml in the real runtime.
    // Its stable, externally observable floor is retained here even before a
    // platform text editor is attached.
    return {64.0, 32.0};
}

}  // namespace openxaml
