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

inline double RoundLayoutValue(double value, double dpi_scale) {
    // nearbyint under the default rounding mode is round-half-to-even, which
    // is what the ported source's Math.Round does. The tie-break is not
    // confirmed against the runtime -- it is the ported behaviour, not a
    // measured one. L0-props-rounding-half is the case authored to settle it,
    // and has no measurement yet.
    if (!AreClose(dpi_scale, 1.0)) {
        const double scaled = std::nearbyint(value * dpi_scale) / dpi_scale;
        // Scaling can overflow a very large value into infinity; keeping the
        // original is better than propagating that into a size.
        if (std::isnan(scaled) || std::isinf(scaled)) return value;
        return scaled;
    }
    return std::nearbyint(value);
}

inline Size RoundLayoutSize(Size size, double dpi_scale_x, double dpi_scale_y) {
    return {RoundLayoutValue(size.width, dpi_scale_x),
            RoundLayoutValue(size.height, dpi_scale_y)};
}

}  // namespace openxaml

#endif  // OPENXAML_LAYOUT_H
