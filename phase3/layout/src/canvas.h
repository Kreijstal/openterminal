// Canvas: the panel that does not do layout.
//
// It is not a layout element -- the only Panel that is not -- and that decides
// far more than its own two zeros. An element takes part in layout only if it
// is a layout element or its parent is one, so a Canvas at the root of a tree
// is never measured or arranged, its MeasureOverride and ArrangeOverride never
// run, and its children are therefore never reached at all. Their recorded
// sizes are all zero however large and explicitly sized they are. See
// element.h for where that rule lives.
//
// When a Canvas does take part -- because the element above it is a layout
// element -- it measures every child with an infinite constraint, arranges
// each one at its own Canvas.Left/Canvas.Top at exactly the size the child
// asked for, and reports no size of its own in either pass.

#ifndef OPENXAML_CANVAS_H
#define OPENXAML_CANVAS_H

#include <string>
#include <vector>

#include "element.h"

namespace openxaml {

class Canvas : public Panel {
public:
    std::string TypeName() const override { return "Windows.UI.Xaml.Controls.Canvas"; }
    // The one Panel that is not a layout element, and the reason a Canvas is
    // the only container here whose children never get measured. See canvas.cpp.
    bool IsLayoutElement() const override { return false; }
    static const std::vector<std::string>& Owners();
    const std::vector<std::string>& PropertyOwners() const override { return Owners(); }

    // Attached, and genuinely attached, as Grid.Row and Grid.Column are: they
    // are registered against Canvas and stored on whichever element carries
    // them, so a Border knows nothing about them and a Canvas parent can still
    // read them off it.
    static double GetLeft(const Element& element);
    static void SetLeft(Element& element, double value);
    static double GetTop(const Element& element);
    static void SetTop(Element& element, double value);
    static int GetZIndex(const Element& element);
    static void SetZIndex(Element& element, int value);

    static const DependencyProperty& LeftProperty();
    static const DependencyProperty& TopProperty();
    static const DependencyProperty& ZIndexProperty();

protected:
    Size MeasureOverride(Size available) override;
    Size ArrangeOverride(Size final_size) override;
};

}  // namespace openxaml

#endif  // OPENXAML_CANVAS_H
