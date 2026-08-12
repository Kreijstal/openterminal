// Text measurement: TextBlock, and the font metrics it reads.
//
// The numbers here were not ported from a source; they were derived from the
// recorded oracle and each one is stated as what it is. See text.cpp for the
// three rules and the evidence for them.

#ifndef OPENXAML_TEXT_H
#define OPENXAML_TEXT_H

#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "element.h"

namespace openxaml {

// Where a set of metrics came from. Not decoration: the derived ones cover the
// two numbers the corpus measures directly and nothing else, so "no advance for
// U+0054" means different things for the two and a reader has to be told which
// one is loaded.
enum class FontProvenance {
    Harvested,  // read out of the font file by harvest_font_metrics.py
    Derived,    // solved out of the recorded measurements by derive_font_metrics.py
    RuntimeDirectWrite,  // resolved from the installed Windows font collection
};

// Metrics for a glyph selected by the platform's system-font fallback rather
// than by a family explicitly named in XAML. The Windows oracle harvests the
// selected face and scale through DirectWrite; layout never guesses from the
// requested family or substitutes a font installed on the current host.
struct FallbackGlyphMetrics {
    std::string family;
    double scale = 1.0;
    double units_per_em = 0.0;
    double advance = 0.0;
};

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
    // Codepoint -> the DirectWrite system-fallback face selected for it.
    // Optional because most fonts and codepoints never leave the named family
    // list, and old oracle artifacts predate this harvest.
    std::map<char32_t, FallbackGlyphMetrics> system_fallbacks;
    // What a pair of adjacent glyphs adds to the first one's advance, keyed by
    // the pair. Absent means zero: a font with no kerning at all and a pair the
    // font does not kern are the same thing to a measurement.
    std::map<std::pair<char32_t, char32_t>, double> kerning;
    // The subset the runtime applies wherever the pair occurs, which is the
    // subset the font's GPOS carries. The rest -- pairs only the legacy `kern`
    // table has -- move the run's first pair and nothing else. See rule 5 in
    // text.cpp for the recordings that split them.
    std::set<std::pair<char32_t, char32_t>> kerns_anywhere;
    FontProvenance provenance = FontProvenance::Harvested;

    // Baseline to baseline, which is what a line of text occupies.
    double LineSpacing() const { return ascender - descender + line_gap; }

    double PairAdjustment(char32_t left, char32_t right, bool first_in_run) const {
        const std::pair<char32_t, char32_t> pair{left, right};
        if (!first_in_run && !kerns_anywhere.count(pair)) return 0.0;
        const auto found = kerning.find(pair);
        return found == kerning.end() ? 0.0 : found->second;
    }
};

// Optional platform text boundary. The layout core never constructs one and
// native/oracle executables therefore remain pinned to harvested FontLibrary
// data. The Windows.UI.Xaml DLL installs a DirectWrite implementation at its
// activation boundary.
struct RuntimeTextRequest {
    std::string family;
    std::string text;
    double font_size = 0.0;
    double width = 0.0;
    bool wrap = false;
    bool bold = false;
    // Explicit and deterministic. Callers that project FrameworkElement.Language
    // can replace this; the provider never consults the machine's user locale.
    std::string language = "en-US";
};

struct RuntimeTextResult {
    Size size;
    double baseline = 0.0;
    std::vector<double> advances;
    std::string resolved_family;
};

class RuntimeTextProvider {
public:
    virtual ~RuntimeTextProvider() = default;
    virtual bool ResolveFontMetrics(const std::string& family,
                                    FontMetrics& metrics,
                                    std::string& resolved_family,
                                    std::string& diagnostic) = 0;
    virtual bool Layout(const RuntimeTextRequest& request,
                        RuntimeTextResult& result,
                        std::string& diagnostic) = 0;
};

void SetRuntimeTextProvider(std::shared_ptr<RuntimeTextProvider> provider);
std::shared_ptr<RuntimeTextProvider> GetRuntimeTextProvider();

// Font family name -> metrics. A family with no entry is an error rather than
// a substitution: measuring against whatever font happened to be installed
// would produce numbers that look right and are not comparable to anything.
class FontLibrary {
public:
    void Add(std::string family, FontMetrics metrics);
    // The family a FontFamily names, which is the first entry of a fallback
    // list that is installed at all. This is the one that owns the line box:
    // an icon in Terminal's "Segoe UI, Segoe Fluent Icons, Segoe MDL2 Assets"
    // is one em wide and one Segoe UI line tall, so the two halves of the size
    // come from different entries.
    const FontMetrics* Find(const std::string& family) const;
    // The first entry of a fallback list that covers the text, which is the one
    // that owns the advances. Falls back to Find's answer when nothing covers
    // it, so the failure is "no advance for U+xxxx" and not "no such family".
    const FontMetrics* FindForText(const std::string& family, const std::string& text) const;
    // Replaces one family's pair adjustments, leaving its advances alone.
    // False when no such family is loaded.
    bool SetKerning(const std::string& family,
                    std::map<std::pair<char32_t, char32_t>, double> pairs);
    bool empty() const { return fonts_.empty(); }

    // Process-wide, because the markup builder and the WinRT activation
    // factory both need to reach it and neither is handed one.
    static FontLibrary& Default();

private:
    std::map<std::string, FontMetrics> fonts_;
    mutable std::mutex runtime_fonts_mutex_;
    mutable std::map<std::string, FontMetrics> runtime_fonts_;
};

// The corpus exercises NoWrap and Wrap. WrapWholeWords exists in XAML and is
// deliberately absent here -- see text.cpp.
enum class TextWrapping { NoWrap, Wrap };

class TextBlock : public Element {
public:
    std::string TypeName() const override { return "Windows.UI.Xaml.Controls.TextBlock"; }
    bool IsLayoutElement() const override { return true; }
    static const std::vector<std::string>& Owners();
    const std::vector<std::string>& PropertyOwners() const override { return Owners(); }

    const std::string& text() const { return GetString(TextProperty()); }
    void set_text(std::string value) { SetValue(TextProperty(), std::move(value)); }
    TextWrapping text_wrapping() const {
        return static_cast<TextWrapping>(GetInt(TextWrappingProperty()));
    }
    void set_text_wrapping(TextWrapping value) {
        SetValue(TextWrappingProperty(), static_cast<int>(value));
    }

    // Inherited, and shared with Control -- see element.h. A TextBlock with no
    // FontSize of its own reads the one set on the ContentControl above it,
    // which is the whole of what L0-props-inherited-fontsize measures.
    double font_size() const { return GetDouble(FontSizeProperty()); }
    void set_font_size(double value) { SetValue(FontSizeProperty(), value); }
    const std::string& font_family() const { return GetString(FontFamilyProperty()); }
    void set_font_family(std::string value) {
        SetValue(FontFamilyProperty(), std::move(value));
    }

    // Whether the face the text asked for has to be simulated because the
    // family only ships one weight. Not a dependency property: FontWeight is
    // declared on the elements that have one, and what reaches the shaper is
    // the decision they made about it. See icon.cpp.
    void set_simulates_bold(bool value) { simulates_bold_ = value; }
    bool simulates_bold() const { return simulates_bold_; }

    // Whether the text arrived through the Text property rather than as inline
    // content. The two are not the same measurement -- see rule 7 in text.cpp,
    // which is what the L4-source twins were authored to find out. Inline
    // content is the default because that is what everything building a
    // TextBlock from a Content value is doing.
    void set_text_from_property(bool value) { text_from_property_ = value; }

    static const DependencyProperty& TextProperty();
    static const DependencyProperty& TextWrappingProperty();

    // One advance per character of Text, in DIPs, from the same shaping the
    // measurement uses -- same kerning, same bold simulation, same snapping.
    // A backend that lays glyphs on its own advances instead drifts away from
    // the width the corpus verified: GDI rounds an advance to whole pixels, so
    // six of Cascadia Mono's 11.71875 at size 20 cover 72 pixels where the
    // measurement says 70.32. Empty when the family has no metrics, which the
    // caller must report rather than paper over.
    std::vector<double> ShapedAdvances() const;

    // A platform runtime provider can honestly refuse a requested system font
    // (for example when Wine has no Segoe MDL2 Assets). Such a refusal is
    // contained at this leaf so unrelated ancestors still lay out, and is
    // retained here for the display-list boundary to emit as a named no-draw.
    const std::string& runtime_text_refusal() const { return runtime_text_refusal_; }

protected:
    Size MeasureOverride(Size available) override;
    Size ArrangeOverride(Size final_size) override;

private:
    // Lays the text out into `limit` DIPs of width and returns what it fills.
    Size LayoutText(double limit) const;

    bool simulates_bold_ = false;
    bool text_from_property_ = false;
    mutable std::string runtime_text_refusal_;
};

// Thrown when a family is not in the library, or the text uses a codepoint the
// harvested metrics do not cover.
class TextError : public std::runtime_error {
public:
    explicit TextError(const std::string& what) : std::runtime_error(what) {}
};

// The height one line of nothing occupies in `family` at `size` -- the empty
// TextBlock's unsnapped line box, pulled out to where a ContentControl can ask
// for it: a string Content never becomes an element in this host, but the
// recorded Button says it still holds one line open. One arithmetic reached
// twice, not two that agree. Throws TextError when the family has no harvested
// metrics; a runtime text provider's refusal is a zero-height line instead,
// the same containment TextBlock applies at its leaf.
double EmptyLineHeight(const std::string& family, double size);

}  // namespace openxaml

#endif  // OPENXAML_TEXT_H
