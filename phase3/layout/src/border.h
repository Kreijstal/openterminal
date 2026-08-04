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

    void SetChild(std::unique_ptr<Element> child) { child_ = std::move(child); }

    std::vector<Element*> Children() const override {
        if (!child_) return {};
        return {child_.get()};
    }

    Thickness border_thickness;
    Thickness padding;

protected:
    Size MeasureOverride(Size available) override;
    Size ArrangeOverride(Size final_size) override;

private:
    std::unique_ptr<Element> child_;
};

}  // namespace openxaml

#endif  // OPENXAML_BORDER_H
