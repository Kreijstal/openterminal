#include "border.h"

#include <algorithm>

namespace openxaml {

Size Border::MeasureOverride(Size available) {
    // Border thickness and padding are chrome: they are subtracted from what
    // the child is offered and added back to what the Border reports.
    const double chrome_width = border_thickness.horizontal() + padding.horizontal();
    const double chrome_height = border_thickness.vertical() + padding.vertical();

    if (!child_) return {chrome_width, chrome_height};

    child_->Measure({std::max(0.0, available.width - chrome_width),
                     std::max(0.0, available.height - chrome_height)});
    const Size child_size = child_->desired_size();
    return {child_size.width + chrome_width, child_size.height + chrome_height};
}

Size Border::ArrangeOverride(Size final_size) {
    if (child_) {
        // Deflate by the chrome, floored at zero: a Border narrower than its
        // own border thickness gives the child an empty rect rather than a
        // negative one.
        const double left = border_thickness.left + padding.left;
        const double top = border_thickness.top + padding.top;
        const double chrome_width = border_thickness.horizontal() + padding.horizontal();
        const double chrome_height = border_thickness.vertical() + padding.vertical();
        child_->Arrange({left, top, std::max(0.0, final_size.width - chrome_width),
                         std::max(0.0, final_size.height - chrome_height)});
    }
    // A Border always occupies the whole slot it was given, whatever the child
    // did with the inner rect.
    return final_size;
}

}  // namespace openxaml
