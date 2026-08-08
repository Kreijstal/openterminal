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

Size Button::MeasureOverride(Size available) {
    const Size content = ContentControl::MeasureOverride(available);
    const Thickness p{12, 6, 12, 6};
    return {std::max(64.0, content.width + p.horizontal()),
            std::max(32.0, content.height + p.vertical())};
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
