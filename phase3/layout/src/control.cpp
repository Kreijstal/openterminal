#include "control.h"

#include <algorithm>

#include "default_styles.h"

namespace openxaml {
namespace {

const DependencyProperty* const kTemplateFocusTarget = RegisterProperty(
    "Control", "Control.IsTemplateFocusTarget", {false, false, false});
const DependencyProperty* const kControlPadding =
    RegisterProperty("Control", "Padding", {Thickness{}, false, true});

const std::vector<std::string> kOwners = {"ContentControl", "Control", kTextPropertyOwner,
                                          "FrameworkElement", "UIElement"};

}  // namespace

const std::vector<std::string>& ContentControl::Owners() { return kOwners; }
const DependencyProperty& Control::PaddingProperty() { return *kControlPadding; }

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
    // Straight through to the content, with no chrome of its own.
    //
    // A real ContentControl reaches its content through a ContentPresenter
    // that the default template supplies, and that presenter carries the
    // control's Padding and content alignment. None of it is modelled: the one
    // case in the corpus leaves Padding at zero, and its content is a
    // zero-width TextBlock arranged at the origin, which is where Left/Top and
    // Stretch both put it. Guessing between them would be a rule nothing here
    // could check.
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
        const Thickness inset = padding();
        Children().front()->Arrange({inset.left, inset.top,
                                     std::max(0.0, final_size.width - inset.horizontal()),
                                     std::max(0.0, final_size.height - inset.vertical())});
    }
    return final_size;
}

}  // namespace openxaml
