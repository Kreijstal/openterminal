#include "basic_controls.h"

#include <algorithm>

namespace openxaml {
namespace {
const std::vector<std::string> kButtonOwners = {
    "Button", "ButtonBase", "ContentControl", "Control", kTextPropertyOwner,
    "FrameworkElement", "UIElement"};
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
const std::vector<std::string>& TextBox::Owners() { return kTextBoxOwners; }
const std::vector<std::string>& ToolTip::Owners() { return kToolTipOwners; }
const std::vector<std::string>& Thumb::Owners() { return kThumbOwners; }
const DependencyProperty& TextBox::TextProperty() { return *kText; }
const DependencyProperty& TextBox::PlaceholderTextProperty() { return *kPlaceholder; }

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

// What the runtime's default Button template adds around the content, as the
// corpus's one Button records it. L7-terminal-0e66f8e18d measures
// `<Button Grid.Row="0">Create</Button>` at three available sizes and answers
// 20 x 32 every time, with the panel beside it 420 wide against a 400-wide
// TextBox -- so the 20 is real and the string inside is not in it.
//
// Content that measures nothing therefore leaves 20 across and 32 down. Which
// part of the 32 is chrome and which is the style's minimum height is not
// something one Button can say, and neither is what the horizontal 20 is made
// of; the recorded sum is what is written here, as a width the chrome adds and
// a height it will not go below.
Size Button::MeasureOverride(Size available) {
    const Size content = ContentControl::MeasureOverride(available);
    return {content.width + 20.0, std::max(32.0, content.height)};
}

Size Button::ArrangeOverride(Size final_size) {
    return ContentControl::ArrangeOverride(final_size);
}

Size TextBox::MeasureOverride(Size) {
    // The editable text host is supplied by generic.xaml in the real runtime.
    // Its stable, externally observable floor is retained here even before a
    // platform text editor is attached.
    return {64.0, 32.0};
}

}  // namespace openxaml
