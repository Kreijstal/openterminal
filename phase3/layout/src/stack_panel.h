#ifndef OPENXAML_STACK_PANEL_H
#define OPENXAML_STACK_PANEL_H

#include <string>

#include "element.h"

namespace openxaml {

class StackPanel : public Panel {
public:
    std::string TypeName() const override { return "Windows.UI.Xaml.Controls.StackPanel"; }

    Orientation orientation = Orientation::Vertical;

protected:
    Size MeasureOverride(Size available) override;
    Size ArrangeOverride(Size final_size) override;
};

}  // namespace openxaml

#endif  // OPENXAML_STACK_PANEL_H
