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
// Source is deliberately not implemented. Decoding an image to find its
// natural size is a dependency this layer does not have, and every case in the
// corpus that reaches an Image has none; markup carrying a Source is rejected
// by name rather than measured as if it were empty.

#ifndef OPENXAML_IMAGE_H
#define OPENXAML_IMAGE_H

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "element.h"

namespace openxaml {

class Image : public Element {
public:
    std::string TypeName() const override { return "Windows.UI.Xaml.Controls.Image"; }
    static const std::vector<std::string>& Owners();
    const std::vector<std::string>& PropertyOwners() const override { return Owners(); }

protected:
    // Both passes answer with the size the markup asked for -- see
    // Element::specified_size, which is also what an Image reports when it
    // takes no part in layout and has no storage to read.
    Size MeasureOverride(Size) override { return specified_size(); }
    Size ArrangeOverride(Size) override { return specified_size(); }
};

}  // namespace openxaml

#endif  // OPENXAML_IMAGE_H
