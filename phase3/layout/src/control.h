// Control and ContentControl.
//
// Only as much of either as the property system needs. A real Control has a
// template, and a real ContentControl realises its Content through a
// ContentPresenter inside that template -- neither of which is here, because
// templates are level 5 and nothing below it can see them. What is here is the
// part L0 measures: a Control is where the inherited text properties are
// declared, and a ContentControl is the element that carries a single piece of
// content for them to reach.

#ifndef OPENXAML_CONTROL_H
#define OPENXAML_CONTROL_H

#include <memory>
#include <string>
#include <vector>

#include "element.h"

namespace openxaml {

// Abstract in the runtime and abstract here: every case names a concrete
// subclass, and a bare Control has no layout of its own to model.
class Control : public Element {
public:
    // Inherited, and shared with TextBlock -- see element.h.
    double font_size() const { return GetDouble(FontSizeProperty()); }
    void set_font_size(double value) { SetValue(FontSizeProperty(), value); }
    const std::string& font_family() const { return GetString(FontFamilyProperty()); }
    void set_font_family(std::string value) { SetValue(FontFamilyProperty(), std::move(value)); }
};

class ContentControl : public Control {
public:
    std::string TypeName() const override { return "Windows.UI.Xaml.Controls.ContentControl"; }
    static const std::vector<std::string>& Owners();
    const std::vector<std::string>& PropertyOwners() const override { return Owners(); }

    void SetContent(std::unique_ptr<Element> content);

    std::vector<Element*> Children() const override {
        if (!content_) return {};
        return {content_.get()};
    }

protected:
    Size MeasureOverride(Size available) override;
    Size ArrangeOverride(Size final_size) override;

private:
    std::unique_ptr<Element> content_;
};

}  // namespace openxaml

#endif  // OPENXAML_CONTROL_H
