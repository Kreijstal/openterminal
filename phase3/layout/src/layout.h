// Primitive layout types and the floating-point comparisons the algorithms
// depend on.
//
// The comparisons are not incidental. WPF's layout compares sizes through
// DoubleUtil rather than with bare operators, and the difference is
// observable: `LessThan` treats near-equal values as equal, so an
// accumulated rounding error of a few ulps does not push an element into the
// "does not fit" branch and change its arranged size. Porting the algorithms
// without porting the comparisons produces a layout that is right most of the
// time and inexplicably wrong at particular sizes.

#ifndef OPENXAML_LAYOUT_H
#define OPENXAML_LAYOUT_H

#include <cmath>
#include <limits>

namespace openxaml {

inline constexpr double kInfinity = std::numeric_limits<double>::infinity();

// Width and Height are NaN when unset, which is what "Auto" means for a
// FrameworkElement: no explicit size, take what the content needs.
inline double Auto() { return std::numeric_limits<double>::quiet_NaN(); }
inline bool IsAuto(double v) { return std::isnan(v); }

struct Size {
    double width = 0.0;
    double height = 0.0;
};

struct Rect {
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;

    Size size() const { return {width, height}; }
};

struct Thickness {
    double left = 0.0;
    double top = 0.0;
    double right = 0.0;
    double bottom = 0.0;

    double horizontal() const { return left + right; }
    double vertical() const { return top + bottom; }
};

// Exact, not AreClose: this answers "is the stored value the same value", which
// the property store asks before deciding that anything changed. Layout's own
// comparisons are the tolerant ones further down.
inline bool operator==(const Thickness& a, const Thickness& b) {
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}
inline bool operator!=(const Thickness& a, const Thickness& b) { return !(a == b); }

enum class HorizontalAlignment { Left, Center, Right, Stretch };
enum class VerticalAlignment { Top, Center, Bottom, Stretch };
enum class Orientation { Horizontal, Vertical };

// Collapsed is not "invisible": it suspends the element from layout entirely,
// which is a different thing from Hidden (not implemented here, because no
// case in the corpus uses it and the two would be indistinguishable without
// one).
enum class Visibility { Visible, Collapsed };

// Where an element is positioned inside a slot larger than the size it
// arranged at. The layout slot is what the probe records, so this only becomes
// observable through a parent that passes an aligned rect down -- a
// ContentPresenter, for instance -- rather than through the element itself.
inline double AlignmentOffset(HorizontalAlignment alignment, double client, double ink) {
    // Stretch degenerates to Left when the content is about to be clipped;
    // centring something that does not fit would hide both of its edges
    // instead of one.
    if (alignment == HorizontalAlignment::Stretch && ink > client)
        alignment = HorizontalAlignment::Left;
    switch (alignment) {
        case HorizontalAlignment::Center:
        case HorizontalAlignment::Stretch:
            return (client - ink) / 2.0;
        case HorizontalAlignment::Right:
            return client - ink;
        case HorizontalAlignment::Left:
            break;
    }
    return 0.0;
}

inline double AlignmentOffset(VerticalAlignment alignment, double client, double ink) {
    if (alignment == VerticalAlignment::Stretch && ink > client)
        alignment = VerticalAlignment::Top;
    switch (alignment) {
        case VerticalAlignment::Center:
        case VerticalAlignment::Stretch:
            return (client - ink) / 2.0;
        case VerticalAlignment::Bottom:
            return client - ink;
        case VerticalAlignment::Top:
            break;
    }
    return 0.0;
}

// --- DoubleUtil ---------------------------------------------------------------
// Ported from WindowsBase's DoubleUtil, which the layout code uses in place of
// bare comparisons.

inline constexpr double kDoubleEpsilon = 2.2204460492503131e-016;

inline bool AreClose(double a, double b) {
    if (a == b) return true;
    // The tolerance scales with the magnitudes, plus a floor of 10 so that
    // values near zero still compare sensibly.
    const double eps = (std::abs(a) + std::abs(b) + 10.0) * kDoubleEpsilon;
    const double delta = a - b;
    return -eps < delta && eps > delta;
}

inline bool LessThan(double a, double b) { return a < b && !AreClose(a, b); }
inline bool GreaterThan(double a, double b) { return a > b && !AreClose(a, b); }
inline bool GreaterThanOrClose(double a, double b) { return a > b || AreClose(a, b); }
inline bool IsZero(double v) { return std::abs(v) < 10.0 * kDoubleEpsilon; }

// --- layout rounding ----------------------------------------------------------
// Sizes are snapped to whole device pixels so that edges land on pixel
// boundaries instead of being anti-aliased across two of them.
//
// This is on by default, which is where WinUI parts company with WPF: WPF
// leaves UseLayoutRounding off unless asked. The difference is visible in the
// corpus -- a 2*/1* split of 100 arranges as 67 and 33 rather than 66.6667 and
// 33.3333 -- so an implementation without rounding disagrees with the runtime
// on every layout that does not divide evenly.

// Half rounds up, not to even. The corpus settled it: L0-props-rounding-half
// records Width="120.5" as 121, and L5-xprimitives-boolean-rounding records a
// 60.5 x 30.5 child as 61 x 31 through a rounding parent. Round-half-to-even --
// what nearbyint and the ported Math.Round do -- answers 120, 60 and 30 for
// those three, so it is not the runtime's rule.
//
// floor(v + 0.5) rather than a symmetric round-half-away: only positive ties
// are recorded, and this is the form the runtime's own rounding helper takes.
inline double RoundHalfUp(double value) { return std::floor(value + 0.5); }

inline double RoundLayoutValue(double value, double dpi_scale) {
    if (!AreClose(dpi_scale, 1.0)) {
        const double scaled = RoundHalfUp(value * dpi_scale) / dpi_scale;
        // Scaling can overflow a very large value into infinity; keeping the
        // original is better than propagating that into a size.
        if (std::isnan(scaled) || std::isinf(scaled)) return value;
        return scaled;
    }
    return RoundHalfUp(value);
}

inline Size RoundLayoutSize(Size size, double dpi_scale_x, double dpi_scale_y) {
    return {RoundLayoutValue(size.width, dpi_scale_x),
            RoundLayoutValue(size.height, dpi_scale_y)};
}

}  // namespace openxaml

#endif  // OPENXAML_LAYOUT_H
