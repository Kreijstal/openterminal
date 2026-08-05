#include "stack_panel.h"

#include <algorithm>

namespace openxaml {

Size StackPanel::MeasureOverride(Size available) {
    const bool horizontal = orientation == Orientation::Horizontal;
    const Size chrome = CombinedThickness();

    // Unbounded along the stacking axis. This is the single most consequential
    // line in the class: children are asked how big they want to be with no
    // limit in the stacking direction, so a StackPanel never compresses its
    // content to fit and its own desired size can exceed what it was offered.
    Size slot{horizontal ? kInfinity : available.width - chrome.width,
              horizontal ? available.height - chrome.height : kInfinity};

    Size desired;
    int visible_children = 0;
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
        if (child->visibility == Visibility::Visible) ++visible_children;
    }

    desired.width += chrome.width;
    desired.height += chrome.height;

    // Gaps, not padding: the count is one less than the number of children
    // that are actually there, and a panel with one visible child has none.
    if (visible_children > 1) {
        const double combined = spacing * (visible_children - 1);
        (horizontal ? desired.width : desired.height) += combined;
    }
    return desired;
}

Size StackPanel::ArrangeOverride(Size final_size) {
    const bool horizontal = orientation == Orientation::Horizontal;
    const Rect inner = InnerRect(final_size);

    Rect slot = inner;
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
            slot.height = std::max(inner.height, child_desired.height);
        } else {
            slot.y += previous_child_size;
            previous_child_size = child_desired.height;
            slot.height = previous_child_size;
            slot.width = std::max(inner.width, child_desired.width);
        }
        child->Arrange(slot);
        // A collapsed child is placed but does not advance the cursor, so it
        // costs neither its own size nor a gap.
        if (child->visibility == Visibility::Visible) {
            previous_child_size += spacing;
        } else {
            previous_child_size = 0.0;
        }
    }
    return final_size;
}

}  // namespace openxaml
