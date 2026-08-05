#ifndef OPENXAML_STACK_PANEL_H
#define OPENXAML_STACK_PANEL_H

#include <string>
#include <vector>

#include "chrome.h"
#include "element.h"

namespace openxaml {

class StackPanel : public ChromedPanel {
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

    // Inserted between children, not around them: n children get n-1 gaps, and
    // a collapsed child gets none because it is not there.
    double spacing() const { return GetDouble(SpacingProperty()); }
    void set_spacing(double value) { SetValue(SpacingProperty(), value); }

    static const DependencyProperty& OrientationProperty();
    static const DependencyProperty& SpacingProperty();

protected:
    Size MeasureOverride(Size available) override;
    Size ArrangeOverride(Size final_size) override;
};

}  // namespace openxaml

#endif  // OPENXAML_STACK_PANEL_H
