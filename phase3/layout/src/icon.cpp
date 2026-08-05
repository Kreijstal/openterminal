#include "icon.h"

namespace openxaml {

Size PathIcon::MeasureOverride(Size available) {
    // The icon's Data is handed to the Path it builds, and the icon's desired
    // size is the Path's answer -- there is no separate icon-level sizing.
    content_.data = data;
    content_.Measure(available);
    return content_.desired_size();
}

Size PathIcon::ArrangeOverride(Size final_size) {
    content_.Arrange({0.0, 0.0, final_size.width, final_size.height});
    return final_size;
}

}  // namespace openxaml
