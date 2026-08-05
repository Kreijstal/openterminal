// Image: sized by its source, and there is no source.
//
// Both passes run the same computation -- scale the source's natural bounds by
// whatever the Stretch mode makes of the constraint -- which is why an Image
// reports the size of its content rather than the slot it was given, even when
// its alignment is Stretch. With no Source the natural bounds are empty, so
// both passes return zero and the element occupies nothing.
//
// Source is deliberately not implemented. Decoding an image to find its
// natural size is a dependency this layer does not have, and every case in the
// corpus that reaches an Image has none; markup carrying a Source is rejected
// by name rather than measured as if it were empty.

#ifndef OPENXAML_IMAGE_H
#define OPENXAML_IMAGE_H

#include <string>

#include "element.h"

namespace openxaml {

class Image : public Element {
public:
    std::string TypeName() const override { return "Windows.UI.Xaml.Controls.Image"; }

protected:
    Size MeasureOverride(Size) override { return Size{}; }
    Size ArrangeOverride(Size) override { return Size{}; }
};

}  // namespace openxaml

#endif  // OPENXAML_IMAGE_H
