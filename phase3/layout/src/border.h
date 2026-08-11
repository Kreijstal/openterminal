#ifndef OPENXAML_BORDER_H
#define OPENXAML_BORDER_H

#include <memory>
#include <string>
#include <vector>

#include "element.h"

namespace openxaml {

// A single-child decorator. Not a Panel: it has a Child, not a Children
// collection, and the measured tree reflects that.
class Border : public Element {
public:
    std::string TypeName() const override { return "Windows.UI.Xaml.Controls.Border"; }
    bool IsLayoutElement() const override { return true; }
    static const std::vector<std::string>& Owners();
    const std::vector<std::string>& PropertyOwners() const override { return Owners(); }

    void SetChild(std::unique_ptr<Element> child);

    std::vector<Element*> Children() const override {
        if (!child_) return {};
        return {child_.get()};
    }

    const Thickness& border_thickness() const { return GetThickness(BorderThicknessProperty()); }
    void set_border_thickness(Thickness value) { SetValue(BorderThicknessProperty(), value); }
    const Thickness& padding() const { return GetThickness(PaddingProperty()); }
    void set_padding(Thickness value) { SetValue(PaddingProperty(), value); }
    // Read by the render pass and by nothing in layout: a corner radius decides
    // which pixels the chrome covers, never how much room the child gets.
    const CornerRadius& corner_radius() const {
        return GetCornerRadius(CornerRadiusProperty());
    }
    void set_corner_radius(CornerRadius value) { SetValue(CornerRadiusProperty(), value); }

    static const DependencyProperty& BorderThicknessProperty();
    static const DependencyProperty& PaddingProperty();
    static const DependencyProperty& CornerRadiusProperty();
    static const DependencyProperty& BackgroundProperty();
    static const DependencyProperty& BorderBrushProperty();

protected:
    Size MeasureOverride(Size available) override;
    Size ArrangeOverride(Size final_size) override;

private:
    std::unique_ptr<Element> child_;
};

}  // namespace openxaml

#endif  // OPENXAML_BORDER_H
