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

#include "element.h"

namespace openxaml {

class ContentPresenter : public Element {
public:
    std::string TypeName() const override {
        return "Windows.UI.Xaml.Controls.ContentPresenter";
    }

    void SetContent(std::unique_ptr<Element> content) { content_ = std::move(content); }

    std::vector<Element*> Children() const override {
        if (!content_) return {};
        return {content_.get()};
    }

    Thickness border_thickness;
    Thickness padding;

    // Not the same properties as HorizontalAlignment/VerticalAlignment: these
    // place the content within the presenter. Left/Top is the documented
    // default and no case in the corpus gives a ContentPresenter any content,
    // so it is unconfirmed -- see the pending L5-content cases.
    HorizontalAlignment horizontal_content_alignment = HorizontalAlignment::Left;
    VerticalAlignment vertical_content_alignment = VerticalAlignment::Top;

protected:
    Size MeasureOverride(Size available) override;
    Size ArrangeOverride(Size final_size) override;

private:
    std::unique_ptr<Element> content_;
};

}  // namespace openxaml

#endif  // OPENXAML_CONTENT_PRESENTER_H
