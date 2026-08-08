// Path: a shape sized by where its geometry ends.
//
// A Shape's desired size is the *right and bottom* of its geometry bounds, not
// the width and height. A figure that starts at (10,10) and is 5 wide asks for
// 15, because the geometry is drawn in the element's own coordinates and the
// part between the origin and the figure is space the element occupies.
// Nothing subtracts the left and top back out.
//
// This is Stretch="None" only, which is a Path's default. The other stretch
// modes scale the geometry into the constraint and shift it to the origin; no
// case in the corpus uses one, so markup that asks for one is rejected rather
// than approximated. Stroke is refused for the same reason: a stroked shape
// grows by half its thickness on every side and there is nothing here to check
// that against.

#ifndef OPENXAML_SHAPE_H
#define OPENXAML_SHAPE_H

#include <string>
#include <vector>

#include "element.h"
#include "geometry.h"

namespace openxaml {

class Path : public Element {
public:
    std::string TypeName() const override { return "Windows.UI.Xaml.Shapes.Path"; }
    static const std::vector<std::string>& Owners();
    const std::vector<std::string>& PropertyOwners() const override { return Owners(); }

    // The bounds, not the geometry. `Data` is registered so that markup can
    // name it, but its value is a Geometry rather than anything the property
    // store can hold, so the parse hands the bounds over directly -- the same
    // arrangement a Grid's definitions have.
    GeometryBounds data;

    static const DependencyProperty& DataProperty();

protected:
    Size MeasureOverride(Size) override;
    Size ArrangeOverride(Size final_size) override { return final_size; }
};

class Rectangle : public Element {
public:
    std::string TypeName() const override { return "Windows.UI.Xaml.Shapes.Rectangle"; }
    static const std::vector<std::string>& Owners();
    const std::vector<std::string>& PropertyOwners() const override { return Owners(); }
protected:
    Size MeasureOverride(Size) override { return {}; }
    Size ArrangeOverride(Size final_size) override { return final_size; }
};

}  // namespace openxaml

#endif  // OPENXAML_SHAPE_H
