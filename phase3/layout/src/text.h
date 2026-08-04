// Text measurement: TextBlock, and the font metrics it reads.
//
// The numbers here were not ported from a source; they were derived from the
// recorded oracle and each one is stated as what it is. See text.cpp for the
// three rules and the evidence for them.

#ifndef OPENXAML_TEXT_H
#define OPENXAML_TEXT_H

#include <map>
#include <stdexcept>
#include <string>

#include "element.h"

namespace openxaml {

// What phase3/scripts/harvest_font_metrics.py extracts from a font file, in
// design units. Nothing here is scaled: a metric only becomes a distance once
// a font size divides it by units_per_em.
struct FontMetrics {
    double units_per_em = 0.0;
    // As stored in `hhea`: ascender is positive up, descender negative down.
    double ascender = 0.0;
    double descender = 0.0;
    double line_gap = 0.0;
    std::map<char32_t, double> advances;

    // Baseline to baseline, which is what a line of text occupies.
    double LineSpacing() const { return ascender - descender + line_gap; }
};

// Font family name -> metrics. A family with no entry is an error rather than
// a substitution: measuring against whatever font happened to be installed
// would produce numbers that look right and are not comparable to anything.
class FontLibrary {
public:
    void Add(std::string family, FontMetrics metrics);
    const FontMetrics* Find(const std::string& family) const;
    bool empty() const { return fonts_.empty(); }

    // Process-wide, because the markup builder and the WinRT activation
    // factory both need to reach it and neither is handed one.
    static FontLibrary& Default();

private:
    std::map<std::string, FontMetrics> fonts_;
};

// The corpus exercises NoWrap and Wrap. WrapWholeWords exists in XAML and is
// deliberately absent here -- see text.cpp.
enum class TextWrapping { NoWrap, Wrap };

class TextBlock : public Element {
public:
    std::string TypeName() const override { return "Windows.UI.Xaml.Controls.TextBlock"; }

    std::string text;
    std::string font_family = "Segoe UI";
    double font_size = 14.0;
    TextWrapping text_wrapping = TextWrapping::NoWrap;

protected:
    Size MeasureOverride(Size available) override;
    Size ArrangeOverride(Size final_size) override;

private:
    // Lays the text out into `limit` DIPs of width and returns what it fills.
    Size LayoutText(double limit) const;
};

// Thrown when a family is not in the library, or the text uses a codepoint the
// harvested metrics do not cover.
class TextError : public std::runtime_error {
public:
    explicit TextError(const std::string& what) : std::runtime_error(what) {}
};

}  // namespace openxaml

#endif  // OPENXAML_TEXT_H
