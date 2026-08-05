#ifndef OPENXAML_STACK_PANEL_H
#define OPENXAML_STACK_PANEL_H

#include <string>
#include <vector>

#include "element.h"

namespace openxaml {

class StackPanel : public Panel {
public:
    std::string TypeName() const override { return "Windows.UI.Xaml.Controls.StackPanel"; }
    static const std::vector<std::string>& Owners();
    const std::vector<std::string>& PropertyOwners() const override { return Owners(); }

    Orientation orientation() const {
        return static_cast<Orientation>(GetInt(OrientationProperty()));
    }
    void set_orientation(Orientation value) {
        SetValue(OrientationProperty(), static_cast<int>(value));
    }

    static const DependencyProperty& OrientationProperty();

protected:
    Size MeasureOverride(Size available) override;
    Size ArrangeOverride(Size final_size) override;
};

}  // namespace openxaml

#endif  // OPENXAML_STACK_PANEL_H
