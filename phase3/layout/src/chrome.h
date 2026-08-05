// The chrome a panel draws around its content.
//
// WinUI 2 gives Grid, StackPanel and the other layout panels the border and
// background properties that used to require wrapping them in a Border:
// Background, BorderBrush, BorderThickness, CornerRadius and Padding. Only two
// of those move an element -- BorderThickness and Padding -- and they move it
// the same way for every panel that has them, so the arithmetic lives here
// rather than being repeated per panel.
//
// The asymmetry between the two is deliberate and is in the runtime: the
// border thickness is layout-rounded because it is drawn, and a border on a
// fractional boundary is a blurred edge. Padding is not drawn by anything, so
// it is used as written.

#ifndef OPENXAML_CHROME_H
#define OPENXAML_CHROME_H

#include <algorithm>

#include "element.h"

namespace openxaml {

class ChromedPanel : public Panel {
public:
    Thickness border_thickness;
    Thickness padding;

protected:
    Thickness RoundedBorderThickness() const {
        if (!use_layout_rounding) return border_thickness;
        return {RoundLayoutValue(border_thickness.left, dpi_scale_x),
                RoundLayoutValue(border_thickness.top, dpi_scale_y),
                RoundLayoutValue(border_thickness.right, dpi_scale_x),
                RoundLayoutValue(border_thickness.bottom, dpi_scale_y)};
    }

    // What the chrome costs in each axis: subtracted from what children are
    // offered, added back to what the panel reports.
    Size CombinedThickness() const {
        const Thickness border = RoundedBorderThickness();
        return {border.horizontal() + padding.horizontal(),
                border.vertical() + padding.vertical()};
    }

    // The rect children are arranged inside, in the panel's own coordinates.
    Rect InnerRect(Size outer) const {
        const Thickness border = RoundedBorderThickness();
        const double width = use_layout_rounding ? RoundLayoutValue(outer.width, dpi_scale_x)
                                                 : outer.width;
        const double height = use_layout_rounding ? RoundLayoutValue(outer.height, dpi_scale_y)
                                                  : outer.height;
        return {border.left + padding.left, border.top + padding.top,
                std::max(0.0, width - border.horizontal() - padding.horizontal()),
                std::max(0.0, height - border.vertical() - padding.vertical())};
    }
};

}  // namespace openxaml

#endif  // OPENXAML_CHROME_H
