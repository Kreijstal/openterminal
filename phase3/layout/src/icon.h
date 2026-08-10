// PathIcon: an icon element whose size is its glyph's geometry.
//
// An icon element is a one-child host. It reports whatever its content asks
// for, and it gives the whole slot back to that content when arranging. The
// content is not markup -- it is built by the icon itself -- so it never
// appears in the measured tree, which is why a PathIcon shows up as a leaf
// even though there is a Path inside it.
//
// FontIcon is the text-backed sibling. It uses the same deterministic glyph
// metric pipeline as TextBlock; icon-font metrics remain a harvested /tmp CI
// artifact rather than a committed binary.

#ifndef OPENXAML_ICON_H
#define OPENXAML_ICON_H

#include <memory>
#include <string>
#include <vector>

#include "element.h"
#include "shape.h"
#include "text.h"

namespace openxaml {

class PathIcon : public Element {
public:
    // The Path the icon draws with is its child, not a detached helper: a
    // Shape takes part in layout only under a parent that is a layout element,
    // and the icon is one. Without the adoption the inner Path would measure
    // nothing and the icon would report nothing.
    PathIcon() { Adopt(content_); }

    std::string TypeName() const override { return "Windows.UI.Xaml.Controls.PathIcon"; }
    // An IconElement is a layout element, unlike the Shape it draws with.
    bool IsLayoutElement() const override { return true; }
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

class FontIcon : public Element {
public:
    std::string TypeName() const override { return "Windows.UI.Xaml.Controls.FontIcon"; }
    bool IsLayoutElement() const override { return true; }
    static const std::vector<std::string>& Owners();
    const std::vector<std::string>& PropertyOwners() const override { return Owners(); }
    const std::string& glyph() const { return GetString(GlyphProperty()); }
    void set_glyph(std::string value) { SetValue(GlyphProperty(), std::move(value)); }
    double font_size() const { return GetDouble(FontSizeProperty()); }
    void set_font_size(double value) { SetValue(FontSizeProperty(), value); }
    const std::string& font_family() const { return GetString(FontFamilyProperty()); }
    void set_font_family(std::string value) { SetValue(FontFamilyProperty(), std::move(value)); }
    const std::string& font_weight() const { return GetString(FontWeightProperty()); }
    void set_font_weight(std::string value) { SetValue(FontWeightProperty(), std::move(value)); }
    static const DependencyProperty& GlyphProperty();
    static const DependencyProperty& FontSizeProperty();
    static const DependencyProperty& FontFamilyProperty();
    static const DependencyProperty& FontWeightProperty();
    const std::string& runtime_text_refusal() const {
        return content_.runtime_text_refusal();
    }
protected:
    Size MeasureOverride(Size available) override;
    Size ArrangeOverride(Size final_size) override;
private:
    bool SimulatesBold() const;

    TextBlock content_;
};

}  // namespace openxaml

#endif  // OPENXAML_ICON_H
