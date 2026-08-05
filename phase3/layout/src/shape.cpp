#include "shape.h"

#include <algorithm>

namespace openxaml {

Size Path::MeasureOverride(Size) {
    // The constraint is not consulted. Under Stretch="None" the geometry is
    // whatever size it is; the element overflows its slot rather than being
    // scaled into it, and the layout system clips afterwards.
    if (data.empty) return Size{};
    return {std::max(0.0, data.right), std::max(0.0, data.bottom)};
}

}  // namespace openxaml
