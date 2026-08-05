#include "chrome.h"

#include <algorithm>

namespace openxaml {
namespace {

const DependencyProperty* const kBorderThickness =
    RegisterProperty(kChromePropertyOwner, "BorderThickness", {Thickness{}, false, true});
const DependencyProperty* const kPadding =
    RegisterProperty(kChromePropertyOwner, "Padding", {Thickness{}, false, true});

}  // namespace

const DependencyProperty& ChromeBorderThicknessProperty() { return *kBorderThickness; }
const DependencyProperty& ChromePaddingProperty() { return *kPadding; }

Thickness RoundBorderThickness(const Element& element, const Thickness& thickness) {
    if (!element.use_layout_rounding()) return thickness;
    return {RoundLayoutValue(thickness.left, element.dpi_scale_x),
            RoundLayoutValue(thickness.top, element.dpi_scale_y),
            RoundLayoutValue(thickness.right, element.dpi_scale_x),
            RoundLayoutValue(thickness.bottom, element.dpi_scale_y)};
}

Size ChromeThickness(const Element& element, const Thickness& border, const Thickness& padding) {
    const Thickness rounded = RoundBorderThickness(element, border);
    return {rounded.horizontal() + padding.horizontal(),
            rounded.vertical() + padding.vertical()};
}

Rect ChromeInnerRect(const Element& element, Size outer, const Thickness& border,
                     const Thickness& padding) {
    const Thickness rounded = RoundBorderThickness(element, border);
    const bool rounding = element.use_layout_rounding();
    const double width = rounding ? RoundLayoutValue(outer.width, element.dpi_scale_x)
                                  : outer.width;
    const double height = rounding ? RoundLayoutValue(outer.height, element.dpi_scale_y)
                                   : outer.height;
    return {rounded.left + padding.left, rounded.top + padding.top,
            std::max(0.0, width - rounded.horizontal() - padding.horizontal()),
            std::max(0.0, height - rounded.vertical() - padding.vertical())};
}

}  // namespace openxaml
