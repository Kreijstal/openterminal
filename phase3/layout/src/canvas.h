// Canvas: the panel that does not do layout.
//
// It measures every child with an infinite constraint, arranges each one at
// its own Canvas.Left/Canvas.Top at exactly the size the child asked for, and
// reports no size of its own in either pass. Nothing a Canvas contains
// influences anything a Canvas contains.

#ifndef OPENXAML_CANVAS_H
#define OPENXAML_CANVAS_H

#include <string>
#include <vector>

#include "element.h"

namespace openxaml {

class Canvas : public Panel {
public:
    std::string TypeName() const override { return "Windows.UI.Xaml.Controls.Canvas"; }
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

    static const DependencyProperty& LeftProperty();
    static const DependencyProperty& TopProperty();

protected:
    Size MeasureOverride(Size available) override;
    Size ArrangeOverride(Size final_size) override;
};

}  // namespace openxaml

#endif  // OPENXAML_CANVAS_H
