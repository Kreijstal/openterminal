// Path: a shape sized by where its geometry ends.
//
// A Shape is not a layout element, so this measure runs only under a parent
// that is one -- an IconElement, a Border, a control template's root. A Shape
// at the root of a tree is never measured at all: it reports no desired size
// and renders at whatever Width and Height say, geometry or no geometry. See
// element.h.
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

enum class ShapeStretch { None, Fill, Uniform, UniformToFill };
enum class ShapeLineCap { Flat, Square, Round, Triangle };
enum class ShapeLineJoin { Miter, Bevel, Round };

// State common to every Windows.UI.Xaml.Shapes.Shape projection.  Geometry
// rasterization is deliberately a separate capability boundary, but the
// object model must retain these values now: generated application code is
// allowed to address a Path or Rectangle through IShape and mutate its paint
// state after XBF materialization.
class Shape : public Element {
public:
    double stroke_miter_limit() const { return stroke_miter_limit_; }
    void set_stroke_miter_limit(double value) { stroke_miter_limit_ = value; }
    double stroke_thickness() const { return stroke_thickness_; }
    void set_stroke_thickness(double value) { stroke_thickness_ = value; }
    ShapeLineCap stroke_start_line_cap() const { return stroke_start_line_cap_; }
    void set_stroke_start_line_cap(ShapeLineCap value) {
        stroke_start_line_cap_ = value;
    }
    ShapeLineCap stroke_end_line_cap() const { return stroke_end_line_cap_; }
    void set_stroke_end_line_cap(ShapeLineCap value) { stroke_end_line_cap_ = value; }
    ShapeLineJoin stroke_line_join() const { return stroke_line_join_; }
    void set_stroke_line_join(ShapeLineJoin value) { stroke_line_join_ = value; }
    double stroke_dash_offset() const { return stroke_dash_offset_; }
    void set_stroke_dash_offset(double value) { stroke_dash_offset_ = value; }
    ShapeLineCap stroke_dash_cap() const { return stroke_dash_cap_; }
    void set_stroke_dash_cap(ShapeLineCap value) { stroke_dash_cap_ = value; }
    bool has_stroke_dash_array() const { return has_stroke_dash_array_; }
    void set_has_stroke_dash_array(bool value) { has_stroke_dash_array_ = value; }
    ShapeStretch shape_stretch() const { return stretch_; }
    void set_shape_stretch(ShapeStretch value) { stretch_ = value; }

private:
    double stroke_miter_limit_ = 10.0;
    double stroke_thickness_ = 0.0;
    ShapeLineCap stroke_start_line_cap_ = ShapeLineCap::Flat;
    ShapeLineCap stroke_end_line_cap_ = ShapeLineCap::Flat;
    ShapeLineJoin stroke_line_join_ = ShapeLineJoin::Miter;
    double stroke_dash_offset_ = 0.0;
    ShapeLineCap stroke_dash_cap_ = ShapeLineCap::Flat;
    bool has_stroke_dash_array_ = false;
    ShapeStretch stretch_ = ShapeStretch::None;
};

class Path : public Shape {
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

class Rectangle : public Shape {
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
