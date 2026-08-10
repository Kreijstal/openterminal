// Brushes, to the extent that layout has an opinion about them: none.
//
// A Background or a Fill never moves anything. It is here so that markup
// carrying one can be realised at all -- Terminal's pages set Background on
// almost every Grid -- and so that a brush that is *not* understood is still
// refused by name. Accepting any string as a colour would mean a typo in a
// page loaded silently and the case passed for the wrong reason.
//
// The colour a brush attribute spells is now kept as well as checked. Layout
// still never reads it: it is carried for the render pass in phase3/render,
// which paints a background only where the markup or the theme dictionary
// said what colour it is. Parsing it here rather than there is deliberate --
// the markup parser is the one place that knows a `Background="{ThemeResource
// K}"` has already been resolved to the literal an inlined attribute would
// have written, so both routes reach one parser and cannot disagree.

#ifndef OPENXAML_BRUSH_H
#define OPENXAML_BRUSH_H

#include <string>
#include <utility>

namespace openxaml {

// Straight (non-premultiplied) sRGB, in the channel order XAML writes them.
struct Color {
    unsigned char a = 0;
    unsigned char r = 0;
    unsigned char g = 0;
    unsigned char b = 0;
};

inline bool operator==(const Color& x, const Color& y) {
    return x.a == y.a && x.r == y.r && x.g == y.g && x.b == y.b;
}
inline bool operator!=(const Color& x, const Color& y) { return !(x == y); }

enum class BrushKind {
    // A declared brush whose concrete runtime type was not retained. This is
    // a named rendering boundary, never an implicit transparent brush.
    Unknown,
    SolidColor,
    Image,
};

// What a brush-typed property was set to, as far as anything downstream can
// use it. `declared` says the property was set at all. The trailing kind and
// image source keep old aggregate construction source-compatible while
// distinguishing an empty ImageBrush (a real transparent no-op) from an
// unknown brush whose paint must be refused.
struct BrushValue {
    bool declared = false;
    bool has_color = false;
    Color color;
    BrushKind kind = BrushKind::Unknown;
    bool has_image_source = false;
    std::string image_source;

    BrushValue() = default;
    BrushValue(bool is_declared, bool retains_color, Color retained_color)
        : declared(is_declared),
          has_color(retains_color),
          color(retained_color),
          kind(retains_color ? BrushKind::SolidColor : BrushKind::Unknown) {}
    BrushValue(bool is_declared, bool retains_color, Color retained_color,
               BrushKind retained_kind, bool retains_image_source,
               std::string retained_image_source)
        : declared(is_declared),
          has_color(retains_color),
          color(retained_color),
          kind(retained_kind),
          has_image_source(retains_image_source),
          image_source(std::move(retained_image_source)) {}

    static BrushValue SolidColor(Color value) {
        return BrushValue{true, true, value, BrushKind::SolidColor, false, {}};
    }
    static BrushValue Image(bool has_source = false, std::string source = {}) {
        return BrushValue{true, false, {}, BrushKind::Image, has_source,
                          std::move(source)};
    }
};

// The full runtime name of a brush type, as the oracle spells it. Throws
// MarkupError for a type that is not implemented.
std::string FullBrushTypeName(const std::string& short_name);

// Parses the shorthand a brush-typed attribute takes -- "#ff0000",
// "#80000000", "Transparent". Throws MarkupError for anything else.
Color ParseColor(const std::string& text, const std::string& where);

// The same check with the value dropped, for callers that only want the
// refusal. Kept because most of the parser only cares that the markup is
// legal.
void ValidateColor(const std::string& text, const std::string& where);

}  // namespace openxaml

#endif  // OPENXAML_BRUSH_H
