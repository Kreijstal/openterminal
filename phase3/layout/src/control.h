// Control and ContentControl.
//
// The common control contract: inherited text properties, padding, template
// construction/application and a single-content specialization.

#ifndef OPENXAML_CONTROL_H
#define OPENXAML_CONTROL_H

#include <memory>
#include <functional>
#include <string>
#include <vector>

#include "element.h"

namespace openxaml {

class ControlTemplate;

// Abstract in the runtime and abstract here: every case names a concrete
// subclass, and a bare Control has no layout of its own to model.
class Control : public Element {
public:
    // Inherited, and shared with TextBlock -- see element.h.
    double font_size() const { return GetDouble(FontSizeProperty()); }
    void set_font_size(double value) { SetValue(FontSizeProperty(), value); }
    const std::string& font_family() const { return GetString(FontFamilyProperty()); }
    void set_font_family(std::string value) { SetValue(FontFamilyProperty(), std::move(value)); }
    const Thickness& padding() const { return GetThickness(PaddingProperty()); }
    void set_padding(Thickness value) { SetValue(PaddingProperty(), value); }
    static const DependencyProperty& PaddingProperty();

    // Where the content sits inside the control, which is not where the
    // control sits inside its parent. Control's own pair, distinct from the
    // ContentPresenter properties of the same name -- the template hands one
    // to the other, and a case can set either.
    HorizontalAlignment horizontal_content_alignment() const {
        return static_cast<HorizontalAlignment>(GetInt(HorizontalContentAlignmentProperty()));
    }
    void set_horizontal_content_alignment(HorizontalAlignment value) {
        SetValue(HorizontalContentAlignmentProperty(), static_cast<int>(value));
    }
    VerticalAlignment vertical_content_alignment() const {
        return static_cast<VerticalAlignment>(GetInt(VerticalContentAlignmentProperty()));
    }
    void set_vertical_content_alignment(VerticalAlignment value) {
        SetValue(VerticalContentAlignmentProperty(), static_cast<int>(value));
    }
    static const DependencyProperty& HorizontalContentAlignmentProperty();
    static const DependencyProperty& VerticalContentAlignmentProperty();

    void SetTemplate(std::shared_ptr<const ControlTemplate> value);
    const std::shared_ptr<const ControlTemplate>& templ() const { return template_; }
    bool ApplyTemplate();
    Element* TemplateRoot() const { return template_root_.get(); }

    std::vector<Element*> Children() const override {
        return template_root_ ? std::vector<Element*>{template_root_.get()} : std::vector<Element*>{};
    }

protected:
    Size MeasureOverride(Size available) override;
    Size ArrangeOverride(Size final_size) override;

private:
    std::shared_ptr<const ControlTemplate> template_;
    std::unique_ptr<Element> template_root_;
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
