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

#include "layout.h"

namespace openxaml {

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
