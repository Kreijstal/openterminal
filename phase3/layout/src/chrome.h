// The chrome a panel draws around its content.
//
// WinUI 2 gives Grid, StackPanel and the other layout panels the border and
// background properties that used to require wrapping them in a Border:
// Background, BorderBrush, BorderThickness, CornerRadius and Padding. Only two
// of those move an element -- BorderThickness and Padding -- and they move it
// the same way for every type that has them, so the arithmetic lives here
// rather than being repeated per type.
//
// The asymmetry between the two is deliberate and is in the runtime: the
// border thickness is layout-rounded because it is drawn, and a border on a
// fractional boundary is a blurred edge. Padding is not drawn by anything, so
// it is used as written.
//
// The pair is registered once against a shared owner that sits in the owner
// chain of exactly the types that have them -- the same arrangement the
// inherited text properties use, and for the same reason: one registration,
// one behaviour, and a Canvas refused a Padding because it has no content rect
// for one to deflate. Border keeps its own pair rather than joining this one:
// they are separate dependency properties in the runtime, declared on Border
// long before the panels grew theirs.

#ifndef OPENXAML_CHROME_H
#define OPENXAML_CHROME_H

#include "element.h"

namespace openxaml {

// The owner shared by the panels and the ContentPresenter. Not a runtime type.
inline constexpr const char* kChromePropertyOwner = "ChromeProperties";

const DependencyProperty& ChromeBorderThicknessProperty();
const DependencyProperty& ChromePaddingProperty();
const DependencyProperty& ChromeBorderBrushProperty();
const DependencyProperty& ChromeCornerRadiusProperty();

// Rounded the way the runtime rounds it: the border is drawn, so its edges
// land on device pixels.
Thickness RoundBorderThickness(const Element& element, const Thickness& thickness);

// What the chrome costs in each axis: subtracted from what content is offered,
// added back to what the element reports.
Size ChromeThickness(const Element& element, const Thickness& border, const Thickness& padding);

// The rect content is arranged inside, in the element's own coordinates.
Rect ChromeInnerRect(const Element& element, Size outer, const Thickness& border,
                     const Thickness& padding);

// A Panel carrying the WinUI 2 chrome. ContentPresenter carries the same two
// properties without being a Panel and declares its own accessors for them;
// its arithmetic is Border's rather than a panel's, which is why the two are
// not forced through one base class.
class ChromedPanel : public Panel {
public:
    const Thickness& border_thickness() const {
        return GetThickness(ChromeBorderThicknessProperty());
    }
    void set_border_thickness(Thickness value) {
        SetValue(ChromeBorderThicknessProperty(), value);
    }
    const Thickness& padding() const { return GetThickness(ChromePaddingProperty()); }
    void set_padding(Thickness value) { SetValue(ChromePaddingProperty(), value); }
    const CornerRadius& corner_radius() const {
        return GetCornerRadius(ChromeCornerRadiusProperty());
    }
    void set_corner_radius(CornerRadius value) {
        SetValue(ChromeCornerRadiusProperty(), value);
    }

protected:
    Size CombinedThickness() const {
        return ChromeThickness(*this, border_thickness(), padding());
    }
    Rect InnerRect(Size outer) const {
        return ChromeInnerRect(*this, outer, border_thickness(), padding());
    }
};

}  // namespace openxaml

#endif  // OPENXAML_CHROME_H
