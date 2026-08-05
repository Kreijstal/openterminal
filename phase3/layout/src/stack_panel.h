#ifndef OPENXAML_STACK_PANEL_H
#define OPENXAML_STACK_PANEL_H

#include <string>

#include "chrome.h"
#include "element.h"

namespace openxaml {

class StackPanel : public ChromedPanel {
public:
    std::string TypeName() const override { return "Windows.UI.Xaml.Controls.StackPanel"; }

    Orientation orientation = Orientation::Vertical;

    // Inserted between children, not around them: n children get n-1 gaps, and
    // a collapsed child gets none because it is not there.
    double spacing = 0.0;

protected:
    Size MeasureOverride(Size available) override;
    Size ArrangeOverride(Size final_size) override;
};

}  // namespace openxaml

#endif  // OPENXAML_STACK_PANEL_H
