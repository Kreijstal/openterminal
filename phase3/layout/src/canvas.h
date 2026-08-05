// Canvas: the panel that does not do layout.
//
// It measures every child with an infinite constraint, arranges each one at
// its own Canvas.Left/Canvas.Top at exactly the size the child asked for, and
// reports no size of its own in either pass. Nothing a Canvas contains
// influences anything a Canvas contains.

#ifndef OPENXAML_CANVAS_H
#define OPENXAML_CANVAS_H

#include <string>

#include "element.h"

namespace openxaml {

class Canvas : public Panel {
public:
    std::string TypeName() const override { return "Windows.UI.Xaml.Controls.Canvas"; }

protected:
    Size MeasureOverride(Size available) override;
    Size ArrangeOverride(Size final_size) override;
};

}  // namespace openxaml

#endif  // OPENXAML_CANVAS_H
