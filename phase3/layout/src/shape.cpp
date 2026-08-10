#include "shape.h"

#include <algorithm>

namespace openxaml {
namespace {

// Registered so markup, styles and animations resolve the real Shape owner
// chain. Rendering retains the values separately from the dependency store;
// unsupported geometry/stroke work is named by the display-list compiler.
const DependencyProperty* const kData =
    RegisterProperty("Path", "Data", {std::string(), false, true});

const std::vector<std::string> kOwners = {"Path", "Shape", "FrameworkElement", "UIElement"};
const std::vector<std::string> kRectangleOwners = {
    "Rectangle", "Shape", "FrameworkElement", "UIElement"};
const DependencyProperty* const kFill =
    RegisterProperty("Shape", "Fill", {std::string(), false, false});
const DependencyProperty* const kStroke =
    RegisterProperty("Shape", "Stroke", {std::string(), false, false});
const DependencyProperty* const kStrokeMiterLimit =
    RegisterProperty("Shape", "StrokeMiterLimit", {10.0, false, false});
const DependencyProperty* const kStrokeThickness =
    RegisterProperty("Shape", "StrokeThickness", {0.0, false, true});
const DependencyProperty* const kStrokeStartLineCap =
    RegisterProperty("Shape", "StrokeStartLineCap",
                     {static_cast<int>(ShapeLineCap::Flat), false, false});
const DependencyProperty* const kStrokeEndLineCap =
    RegisterProperty("Shape", "StrokeEndLineCap",
                     {static_cast<int>(ShapeLineCap::Flat), false, false});
const DependencyProperty* const kStrokeLineJoin =
    RegisterProperty("Shape", "StrokeLineJoin",
                     {static_cast<int>(ShapeLineJoin::Miter), false, false});
const DependencyProperty* const kStrokeDashOffset =
    RegisterProperty("Shape", "StrokeDashOffset", {0.0, false, false});
const DependencyProperty* const kStrokeDashCap =
    RegisterProperty("Shape", "StrokeDashCap",
                     {static_cast<int>(ShapeLineCap::Flat), false, false});
const DependencyProperty* const kStrokeDashArray =
    RegisterProperty("Shape", "StrokeDashArray", {std::string(), false, false});
const DependencyProperty* const kStretch =
    RegisterProperty("Shape", "Stretch",
                     {static_cast<int>(ShapeStretch::None), false, true});
const DependencyProperty* const kRadiusX =
    RegisterProperty("Rectangle", "RadiusX", {0.0, false, false});
const DependencyProperty* const kRadiusY =
    RegisterProperty("Rectangle", "RadiusY", {0.0, false, false});

}  // namespace

const DependencyProperty& Path::DataProperty() { return *kData; }

const std::vector<std::string>& Path::Owners() { return kOwners; }
const std::vector<std::string>& Rectangle::Owners() { return kRectangleOwners; }

Size Path::MeasureOverride(Size) {
    // The constraint is not consulted. Under Stretch="None" the geometry is
    // whatever size it is; the element overflows its slot rather than being
    // scaled into it, and the layout system clips afterwards.
    if (data.empty) return Size{};
    return {std::max(0.0, data.right), std::max(0.0, data.bottom)};
}

}  // namespace openxaml
