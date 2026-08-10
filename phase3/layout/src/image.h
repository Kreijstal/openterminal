// Image: sized by its source, and there is no source.
//
// An Image is not a layout element, so a root one is never measured and never
// arranged: it reports no desired size and renders at whatever Width and
// Height say. Everything below describes the pass it does run -- under a
// parent that is a layout element.
//
// Both passes run the same computation -- scale the source's natural bounds by
// whatever the Stretch mode makes of the constraint -- which is why an Image
// reports the size of its content rather than the slot it was given, even when
// its alignment is Stretch. That is what makes it different from every other
// element here: an Image is the size of its picture and nothing else.
//
// With no source there are no natural bounds, and the fallback is the size the
// markup asked for, resolved without reference to the constraint. For an Image
// that asked for nothing that is zero, in both passes, which is why an empty
// Image occupies no space however large the slot around it.
//
// The ABI retains Source, Stretch and NineGrid below. Decoding remains a
// renderer capability boundary: a non-null source is carried as a declared
// fact and becomes a named no-draw, never silently measured as an empty image.

#ifndef OPENXAML_IMAGE_H
#define OPENXAML_IMAGE_H

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "element.h"

namespace openxaml {

enum class ImageStretch { None, Fill, Uniform, UniformToFill };

class Image : public Element {
public:
    std::string TypeName() const override { return "Windows.UI.Xaml.Controls.Image"; }
    static const std::vector<std::string>& Owners();
    const std::vector<std::string>& PropertyOwners() const override { return Owners(); }

    bool has_source() const { return has_source_; }
    const std::string& source_type() const { return source_type_; }
    void set_source(bool present, std::string type) {
        if (has_source_ == present && source_type_ == type) return;
        has_source_ = present;
        source_type_ = present ? std::move(type) : std::string{};
        // Source can change the natural size once decoded.
        InvalidateRender(true);
    }

    ImageStretch stretch() const { return stretch_; }
    void set_stretch(ImageStretch value) {
        if (stretch_ == value) return;
        stretch_ = value;
        InvalidateRender(true);
    }

    const Thickness& nine_grid() const { return nine_grid_; }
    void set_nine_grid(Thickness value) {
        if (nine_grid_ == value) return;
        nine_grid_ = value;
        InvalidateRender(false);
    }

protected:
    // Both passes answer with the size the markup asked for -- see
    // Element::specified_size, which is also what an Image reports when it
    // takes no part in layout and has no storage to read.
    Size MeasureOverride(Size) override { return specified_size(); }
    Size ArrangeOverride(Size) override { return specified_size(); }

private:
    bool has_source_ = false;
    std::string source_type_;
    ImageStretch stretch_ = ImageStretch::Uniform;
    Thickness nine_grid_{};
};

}  // namespace openxaml

#endif  // OPENXAML_IMAGE_H
