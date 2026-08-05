#include "stack_panel.h"

#include <algorithm>

namespace openxaml {
namespace {

const DependencyProperty* const kOrientation = RegisterProperty(
    "StackPanel", "Orientation", {static_cast<int>(Orientation::Vertical), false, true});

const std::vector<std::string> kOwners = {"StackPanel", "Panel", "FrameworkElement", "UIElement"};

}  // namespace

const DependencyProperty& StackPanel::OrientationProperty() { return *kOrientation; }

const std::vector<std::string>& StackPanel::Owners() { return kOwners; }

Size StackPanel::MeasureOverride(Size available) {
    const bool horizontal = orientation() == Orientation::Horizontal;

    // Unbounded along the stacking axis. This is the single most consequential
    // line in the class: children are asked how big they want to be with no
    // limit in the stacking direction, so a StackPanel never compresses its
    // content to fit and its own desired size can exceed what it was offered.
    Size slot = available;
    if (horizontal) {
        slot.width = kInfinity;
    } else {
        slot.height = kInfinity;
    }

    Size desired;
    for (Element* child : Children()) {
        child->Measure(slot);
        const Size child_desired = child->desired_size();
        if (horizontal) {
            desired.width += child_desired.width;
            desired.height = std::max(desired.height, child_desired.height);
        } else {
            desired.width = std::max(desired.width, child_desired.width);
            desired.height += child_desired.height;
        }
    }
    return desired;
}

Size StackPanel::ArrangeOverride(Size final_size) {
    const bool horizontal = orientation() == Orientation::Horizontal;

    Rect slot{0.0, 0.0, final_size.width, final_size.height};
    double previous_child_size = 0.0;

    for (Element* child : Children()) {
        const Size child_desired = child->desired_size();
        if (horizontal) {
            slot.x += previous_child_size;
            previous_child_size = child_desired.width;
            slot.width = previous_child_size;
            // Across the stacking axis the child gets the panel's full extent,
            // or its own desired size when that is larger -- the panel does not
            // squeeze a child that asked for more than the panel received.
            slot.height = std::max(final_size.height, child_desired.height);
        } else {
            slot.y += previous_child_size;
            previous_child_size = child_desired.height;
            slot.height = previous_child_size;
            slot.width = std::max(final_size.width, child_desired.width);
        }
        child->Arrange(slot);
    }
    return final_size;
}

}  // namespace openxaml
