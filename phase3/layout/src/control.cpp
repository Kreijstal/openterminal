#include "control.h"

#include <algorithm>

#include "chrome.h"
#include "default_styles.h"
#include "text.h"

namespace openxaml {
namespace {

const DependencyProperty* const kTemplateFocusTarget = RegisterProperty(
    "Control", "Control.IsTemplateFocusTarget", {false, false, false});
const DependencyProperty* const kControlPadding =
    RegisterProperty("Control", "Padding", {Thickness{}, false, true});
// Beside Padding because the recorded chrome is the two of them together:
// L5-defaults-builtin-reachability measures a bare `<Button/>` at [20, 13],
// which is ButtonPadding "8,4,8,5" plus ButtonBorderThemeThickness 2 on every
// side and nothing else -- no floor, no minimum, no content line. Until this
// property existed, the default style's BorderThickness setter had nothing to
// land on and the whole chrome had to be transcribed into Button as a sum.
const DependencyProperty* const kControlBorderThickness =
    RegisterProperty("Control", "BorderThickness", {Thickness{}, false, true});

// Left/Top, which is what the corpus records: L0-props-content-stretch arranges
// the Border child of a 400 x 300 ContentControl at 0 x 0, and the
// L0-props-inherits-* cases arrange the child of a stretched ContentControl at
// the height its inherited font gives it. Neither affects measure -- the
// content asks for the same size wherever it is then placed.
const DependencyProperty* const kHorizontalContentAlignment =
    RegisterProperty("Control", "HorizontalContentAlignment",
                     {static_cast<int>(HorizontalAlignment::Left), false, false, true});
const DependencyProperty* const kVerticalContentAlignment =
    RegisterProperty("Control", "VerticalContentAlignment",
                     {static_cast<int>(VerticalAlignment::Top), false, false, true});

// The half of Content that is not an element. It never becomes a node -- the
// probe's walk records nothing under the corpus's one Button -- but it is not
// weightless: L7-terminal-0e66f8e18d measures `<Button>Create</Button>` 19
// taller than L5-defaults-builtin-reachability measures `<Button/>`, and 19 is
// Segoe UI's line box at 14 after layout rounding. So a string content holds
// one empty line open, and that is a measurement, which is why the property is
// marked as affecting one. The glyphs stay out: the same recording holds the
// Button 20 wide around a word that is 41 wide on its own.
const DependencyProperty* const kContent =
    RegisterProperty("ContentControl", "Content", {std::string(), false, true});

const std::vector<std::string> kOwners = {"ContentControl", "Control", kTextPropertyOwner,
                                          "FrameworkElement", "UIElement"};

}  // namespace

const std::vector<std::string>& ContentControl::Owners() { return kOwners; }
const DependencyProperty& ContentControl::ContentProperty() { return *kContent; }
const DependencyProperty& Control::PaddingProperty() { return *kControlPadding; }
const DependencyProperty& Control::BorderThicknessProperty() { return *kControlBorderThickness; }
const DependencyProperty& Control::HorizontalContentAlignmentProperty() {
    return *kHorizontalContentAlignment;
}
const DependencyProperty& Control::VerticalContentAlignmentProperty() {
    return *kVerticalContentAlignment;
}

void Control::SetTemplate(std::shared_ptr<const ControlTemplate> value) {
    if (template_ == value) return;
    template_ = std::move(value);
    template_root_.reset();
}

bool Control::ApplyTemplate() {
    if (template_root_ || !template_) return template_root_ != nullptr;
    template_root_ = template_->Build(*this);
    if (template_root_) Adopt(*template_root_);
    return template_root_ != nullptr;
}

Size Control::MeasureOverride(Size available) {
    ApplyTemplate();
    if (!template_root_) return {};
    template_root_->Measure(available);
    return template_root_->desired_size();
}

Size Control::ArrangeOverride(Size final_size) {
    ApplyTemplate();
    if (template_root_)
        template_root_->Arrange({0.0, 0.0, final_size.width, final_size.height});
    return final_size;
}

void ContentControl::SetContent(std::unique_ptr<Element> content) {
    if (content) Adopt(*content);
    content_ = std::move(content);
}

Size ContentControl::MeasureOverride(Size available) {
    // Straight through to the content, less the chrome the default template's
    // ContentPresenter would apply. The presenter itself is not modelled --
    // nothing in the corpus reports one inside a ContentControl -- but its
    // observable effects are: the padding and border thickness here, and the
    // content alignment in Arrange below. The border is layout-rounded and the
    // padding is not, the same asymmetry every drawn edge gets -- see chrome.h.
    //
    // That this is the whole of the chrome is a recorded answer:
    // L5-defaults-builtin-reachability measures a bare `<Button/>` at [20, 13],
    // which is its default style's Padding "8,4,8,5" plus BorderThickness 2
    // exactly, and L7-terminal-24911ba19e measures a ToolTip 18 x 30 around a
    // 0 x 15.9609 line, which is its padding "9,6,9,8" added around element
    // content on both axes.
    const Thickness border = RoundBorderThickness(*this, border_thickness());
    const Thickness inset = padding();
    const Size chrome{border.horizontal() + inset.horizontal(),
                      border.vertical() + inset.vertical()};
    if (Children().empty()) {
        // A string Content never becomes an element here, but it is not
        // nothing: the corpus's one Button with a string in it measures one
        // Segoe UI line taller than the same Button empty, and no wider. So a
        // string is an empty line box of the control's font -- height and no
        // glyphs -- see the Content registration above for the two recordings.
        const double line =
            content_text().empty() ? 0.0 : EmptyLineHeight(font_family(), font_size());
        return {chrome.width, chrome.height + line};
    }

    Element* content = Children().front();
    content->Measure({std::max(0.0, available.width - chrome.width),
                      std::max(0.0, available.height - chrome.height)});
    return {content->desired_size().width + chrome.width,
            content->desired_size().height + chrome.height};
}

Size ContentControl::ArrangeOverride(Size final_size) {
    if (!Children().empty()) {
        Element* content = Children().front();
        const Thickness border = RoundBorderThickness(*this, border_thickness());
        const Thickness inset{border.left + padding().left, border.top + padding().top,
                              border.right + padding().right, border.bottom + padding().bottom};
        const HorizontalAlignment horizontal = horizontal_content_alignment();
        const VerticalAlignment vertical = vertical_content_alignment();
        const Size client{std::max(0.0, final_size.width - inset.horizontal()),
                          std::max(0.0, final_size.height - inset.vertical())};

        // Only Stretch fills the client rect. The default is Left/Top, so a
        // ContentControl handed more room than its content asked for leaves the
        // content at its desired size -- which is what every L0-props case
        // measuring one records.
        const Size content_size{horizontal == HorizontalAlignment::Stretch
                                    ? client.width
                                    : content->desired_size().width,
                                vertical == VerticalAlignment::Stretch
                                    ? client.height
                                    : content->desired_size().height};

        content->Arrange({inset.left + AlignmentOffset(horizontal, client.width, content_size.width),
                          inset.top + AlignmentOffset(vertical, client.height, content_size.height),
                          content_size.width, content_size.height});
    }
    return final_size;
}

}  // namespace openxaml
