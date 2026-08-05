#include "icon.h"

namespace openxaml {
namespace {

const DependencyProperty* const kData =
    RegisterProperty("PathIcon", "Data", {std::string(), false, true});

// IconElement is where Foreground would live. It is not in the chain: the
// inherited text properties are registered against kTextPropertyOwner, which
// Control and TextBlock share, and an icon that accepted a FontSize would be
// claiming an inheritance nothing here implements.
const std::vector<std::string> kOwners = {"PathIcon", "IconElement", "FrameworkElement",
                                          "UIElement"};

}  // namespace

const DependencyProperty& PathIcon::DataProperty() { return *kData; }

const std::vector<std::string>& PathIcon::Owners() { return kOwners; }

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
