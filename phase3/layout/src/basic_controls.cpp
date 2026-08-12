#include "basic_controls.h"

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

// Button has no measurement of its own any more. Its chrome is its default
// style's Padding and BorderThickness counted by ContentControl, and a string
// content is one empty line -- see ContentControl::MeasureOverride for the
// recordings. The 20-across, 32-floor pair that used to be transcribed here
// was the corpus's one Button read as a sum; L5-defaults-builtin-reachability
// then measured the same Button empty at [20, 13] and took the sum apart.

Size TextBox::MeasureOverride(Size) {
    // The editable text host is supplied by generic.xaml in the real runtime.
    // Its stable, externally observable floor is retained here even before a
    // platform text editor is attached.
    return {64.0, 32.0};
}

}  // namespace openxaml
