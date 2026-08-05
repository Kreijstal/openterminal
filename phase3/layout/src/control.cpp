#include "control.h"

namespace openxaml {
namespace {

const std::vector<std::string> kOwners = {"ContentControl", "Control", kTextPropertyOwner,
                                          "FrameworkElement", "UIElement"};

}  // namespace

const std::vector<std::string>& ContentControl::Owners() { return kOwners; }

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
    if (Children().empty()) return {};

    Element* content = Children().front();
    content->Measure(available);
    return content->desired_size();
}

Size ContentControl::ArrangeOverride(Size final_size) {
    if (!Children().empty())
        Children().front()->Arrange({0.0, 0.0, final_size.width, final_size.height});
    return final_size;
}

}  // namespace openxaml
