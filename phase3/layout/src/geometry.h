// Path geometry, reduced to the only thing layout asks of it: its bounds.
//
// A Shape's desired size is the right and bottom edge of its geometry, so the
// whole of `Path.Data` -- an arbitrary sequence of lines and curves -- collapses
// to four numbers before layout sees it. Nothing here renders, fills or
// intersects anything.
//
// The bounds are tight, not the convex hull of the control points. A cubic
// segment's extreme is found by solving its derivative rather than by taking
// the largest control point, because the hull can be arbitrarily far outside
// the curve and the difference lands directly in an element's desired size.

#ifndef OPENXAML_GEOMETRY_H
#define OPENXAML_GEOMETRY_H

#include <string>
#include <utility>

#include "layout.h"

namespace openxaml {

enum class VisualClipKind { None, Rectangle, Unsupported };

enum class VisualTransformKind { None, Rotate, Scale, Unsupported };

struct VisualTransform {
    VisualTransformKind kind = VisualTransformKind::None;
    double angle_degrees = 0.0;
    double scale_x = 1.0;
    double scale_y = 1.0;
    double center_x = 0.0;
    double center_y = 0.0;
    std::string type;

    static VisualTransform Rotate(double angle) {
        VisualTransform result;
        result.kind = VisualTransformKind::Rotate;
        result.angle_degrees = angle;
        result.type = "Windows.UI.Xaml.Media.RotateTransform";
        return result;
    }
    static VisualTransform Scale(double x, double y, double center_x = 0.0,
                                 double center_y = 0.0) {
        VisualTransform result;
        result.kind = VisualTransformKind::Scale;
        result.scale_x = x;
        result.scale_y = y;
        result.center_x = center_x;
        result.center_y = center_y;
        result.type = "Windows.UI.Xaml.Media.ScaleTransform";
        return result;
    }
    static VisualTransform Unsupported(std::string runtime_type) {
        VisualTransform result;
        result.kind = VisualTransformKind::Unsupported;
        result.type = std::move(runtime_type);
        return result;
    }
};

inline bool operator==(const VisualTransform& left, const VisualTransform& right) {
    return left.kind == right.kind && left.angle_degrees == right.angle_degrees &&
           left.scale_x == right.scale_x && left.scale_y == right.scale_y &&
           left.center_x == right.center_x && left.center_y == right.center_y &&
           left.type == right.type;
}
inline bool operator!=(const VisualTransform& left, const VisualTransform& right) {
    return !(left == right);
}

// UIElement.Clip is retained visual state, independent of layout geometry.
// Rectangle is the exact supported subset. Unsupported preserves the declared
// runtime type so rendering can refuse it by name instead of dropping it.
struct VisualClip {
    VisualClipKind kind = VisualClipKind::None;
    Rect bounds;
    std::string type;

    static VisualClip Rectangle(Rect rect) {
        return {VisualClipKind::Rectangle, rect,
                "Windows.UI.Xaml.Media.RectangleGeometry"};
    }
    static VisualClip Unsupported(std::string runtime_type) {
        return {VisualClipKind::Unsupported, {}, std::move(runtime_type)};
    }
};

inline bool operator==(const VisualClip& left, const VisualClip& right) {
    return left.kind == right.kind && left.bounds.x == right.bounds.x &&
           left.bounds.y == right.bounds.y && left.bounds.width == right.bounds.width &&
           left.bounds.height == right.bounds.height && left.type == right.type;
}
inline bool operator!=(const VisualClip& left, const VisualClip& right) {
    return !(left == right);
}

struct GeometryBounds {
    // An empty geometry has no edges at all, which is not the same as one
    // whose edges are at the origin: the first contributes nothing to a
    // desired size, the second is a legitimate zero-sized figure. They agree
    // on every number layout reads, so the flag exists to keep the parse
    // honest rather than to change an answer.
    bool empty = true;
    double left = 0.0;
    double top = 0.0;
    double right = 0.0;
    double bottom = 0.0;

    void Add(double x, double y);
};

// Parses the XAML path mini-language and returns the bounds of the figure it
// describes. Throws MarkupError for syntax it does not implement, naming what
// it found -- an unimplemented command must not silently contribute nothing to
// the bounds, since the result would be a smaller element rather than a
// failure.
GeometryBounds PathGeometryBounds(const std::string& data);

}  // namespace openxaml

#endif  // OPENXAML_GEOMETRY_H
