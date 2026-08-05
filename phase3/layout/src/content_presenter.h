// ContentPresenter: a Border that aligns its content instead of filling it.
//
// It is where a templated control puts the content it was handed, so it is the
// first thing between Terminal's markup and a control set. Chrome (border
// thickness and padding) behaves exactly as it does on a Border; the part that
// is its own is the second pair of alignment properties, which position the
// content inside the presenter rather than the presenter inside its parent.

#ifndef OPENXAML_CONTENT_PRESENTER_H
#define OPENXAML_CONTENT_PRESENTER_H

#include <memory>
#include <string>
#include <vector>

#include "chrome.h"
#include "element.h"

namespace openxaml {

class ContentPresenter : public Element {
public:
    std::string TypeName() const override {
        return "Windows.UI.Xaml.Controls.ContentPresenter";
    }
    static const std::vector<std::string>& Owners();
    const std::vector<std::string>& PropertyOwners() const override { return Owners(); }

    void SetContent(std::unique_ptr<Element> content);

    std::vector<Element*> Children() const override {
        if (!content_) return {};
        return {content_.get()};
    }

    // The same two properties the WinUI 2 panels carry -- see chrome.h. A
    // ContentPresenter is not a Panel, so it declares the accessors rather
    // than inheriting them; the arithmetic they feed is shared.
    const Thickness& border_thickness() const {
        return GetThickness(ChromeBorderThicknessProperty());
    }
    void set_border_thickness(Thickness value) {
        SetValue(ChromeBorderThicknessProperty(), value);
    }
    const Thickness& padding() const { return GetThickness(ChromePaddingProperty()); }
    void set_padding(Thickness value) { SetValue(ChromePaddingProperty(), value); }

    // Not the same properties as HorizontalAlignment/VerticalAlignment: these
    // place the content within the presenter. Left/Top is the documented
    // default and no measured case gives a ContentPresenter any content, so it
    // is unconfirmed -- see the pending L2-content cases.
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
    static const DependencyProperty& BackgroundProperty();

protected:
    Size MeasureOverride(Size available) override;
    Size ArrangeOverride(Size final_size) override;

private:
    std::unique_ptr<Element> content_;
};

}  // namespace openxaml

#endif  // OPENXAML_CONTENT_PRESENTER_H
