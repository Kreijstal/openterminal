#include "shape.h"

#include <algorithm>

namespace openxaml {
namespace {

// Registered so the markup can name it; its value never reaches the store --
// see shape.h. Stroke, Fill and Stretch are deliberately absent, so markup
// setting one is refused by name rather than measured as Stretch="None".
const DependencyProperty* const kData =
    RegisterProperty("Path", "Data", {std::string(), false, true});

const std::vector<std::string> kOwners = {"Path", "Shape", "FrameworkElement", "UIElement"};

}  // namespace

const DependencyProperty& Path::DataProperty() { return *kData; }

const std::vector<std::string>& Path::Owners() { return kOwners; }

Size Path::MeasureOverride(Size) {
    // The constraint is not consulted. Under Stretch="None" the geometry is
    // whatever size it is; the element overflows its slot rather than being
    // scaled into it, and the layout system clips afterwards.
    if (data.empty) return Size{};
    return {std::max(0.0, data.right), std::max(0.0, data.bottom)};
}

}  // namespace openxaml
