// PathIcon: an icon element whose size is its glyph's geometry.
//
// An icon element is a one-child host. It reports whatever its content asks
// for, and it gives the whole slot back to that content when arranging. The
// content is not markup -- it is built by the icon itself -- so it never
// appears in the measured tree, which is why a PathIcon shows up as a leaf
// even though there is a Path inside it.
//
// FontIcon is the other subclass and is not here. Its content is a glyph run,
// so its desired size is a text measurement in an icon font, and the harvested
// metrics for Segoe MDL2 Assets and Segoe Fluent Icons are not in the corpus.
// Implementing it would mean inventing glyph advances, and fifteen L7 cases
// would then agree or disagree with the oracle for reasons that had nothing to
// do with layout.

#ifndef OPENXAML_ICON_H
#define OPENXAML_ICON_H

#include <memory>
#include <string>
#include <vector>

#include "element.h"
#include "shape.h"

namespace openxaml {

class PathIcon : public Element {
public:
    std::string TypeName() const override { return "Windows.UI.Xaml.Controls.PathIcon"; }
    static const std::vector<std::string>& Owners();
    const std::vector<std::string>& PropertyOwners() const override { return Owners(); }

    // Its own Data, not the Path's: they are separate dependency properties in
    // the runtime, declared on separate types. The bounds reach it the same way
    // -- see shape.h.
    GeometryBounds data;

    static const DependencyProperty& DataProperty();

protected:
    Size MeasureOverride(Size available) override;
    Size ArrangeOverride(Size final_size) override;

private:
    Path content_;
};

}  // namespace openxaml

#endif  // OPENXAML_ICON_H
