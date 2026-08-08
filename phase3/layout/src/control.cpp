#include "control.h"

#include <algorithm>

#include "default_styles.h"

namespace openxaml {
namespace {

const DependencyProperty* const kTemplateFocusTarget = RegisterProperty(
    "Control", "Control.IsTemplateFocusTarget", {false, false, false});
const DependencyProperty* const kControlPadding =
    RegisterProperty("Control", "Padding", {Thickness{}, false, true});

// Left/Top, which is what the corpus records: L0-props-content-stretch arranges
// the Border child of a 400 x 300 ContentControl at 0 x 0, and the
// L0-props-inherits-* cases arrange the child of a stretched ContentControl at
// the height its inherited font gives it. Neither affects measure -- the
// content asks for the same size wherever it is then placed.
const DependencyProperty* const kHorizontalContentAlignment =
    RegisterProperty("Control", "HorizontalContentAlignment",
                     {static_cast<int>(HorizontalAlignment::Left), false, false});
const DependencyProperty* const kVerticalContentAlignment =
    RegisterProperty("Control", "VerticalContentAlignment",
                     {static_cast<int>(VerticalAlignment::Top), false, false});

const std::vector<std::string> kOwners = {"ContentControl", "Control", kTextPropertyOwner,
                                          "FrameworkElement", "UIElement"};

}  // namespace

const std::vector<std::string>& ContentControl::Owners() { return kOwners; }
const DependencyProperty& Control::PaddingProperty() { return *kControlPadding; }
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
    // Straight through to the content, less the padding the default template's
    // ContentPresenter would apply. The presenter itself is not modelled --
    // nothing in the corpus reports one inside a ContentControl -- but its two
    // observable effects are: the padding here, and the content alignment in
    // Arrange below.
    const Thickness inset = padding();
    if (Children().empty()) return {inset.horizontal(), inset.vertical()};

    Element* content = Children().front();
    content->Measure({std::max(0.0, available.width - inset.horizontal()),
                      std::max(0.0, available.height - inset.vertical())});
    return {content->desired_size().width + inset.horizontal(),
            content->desired_size().height + inset.vertical()};
}

Size ContentControl::ArrangeOverride(Size final_size) {
    if (!Children().empty()) {
        Element* content = Children().front();
        const Thickness inset = padding();
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
