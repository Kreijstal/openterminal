#include "text.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace openxaml {
namespace {

// --- what the runtime was observed to do --------------------------------------
//
// Three rules, each derived from phase3/xaml-db's recorded measurements rather
// than ported from a source. The evidence is stated because none of them is
// guessable, and a later reader should be able to re-check them:
//
// 1. Advances and line heights land on multiples of 1/300 of a DIP. A design
//    unit advance a at font size s contributes round(a * s / upem * 300) / 300.
//    The snap is per glyph, not per run: no single integer design-unit total
//    for "Terminal" can produce the measured 44.6133 at 12, 52.04 at 14 and
//    89.2167 at 24 -- the three sizes each imply a different total, so the
//    rounding must already have happened by the time the advances are summed.
//    Where 300 comes from is not explained here; it is what the numbers say.
//
// 2. Line height is that same snap applied to the font's baseline-to-baseline
//    distance -- except for empty text, which keeps the unsnapped value. At
//    size 12 a line of text is 15.96 and an empty TextBlock is 15.9609, and
//    both appear in the corpus.
//
// 3. Heights accumulate one line at a time in float, not by multiplying. Ten
//    lines at size 24 measure 319.2334; ten times the line height is
//    319.2333, and only repeated float addition produces the recorded value.
//
// 4. A pair of adjacent glyphs can move the first one's advance, and the move
//    happens in design units, before that advance snaps. "Terminal" measures
//    200 units narrower than its advances add up to, at all three recorded
//    sizes, and the pangram beside it measures exactly what they add up to --
//    so the 200 belongs to a pair rather than to the arithmetic. Where the
//    move joins matters: snapping it on its own and adding it afterwards
//    lands 1/300 of a DIP away at size 12, and the corpus records the first.
//    Which pairs a font moves is harvested from it, not decided here.
//
// 5. Which pairs move a run depends on where the font keeps them. A pair the
//    font's GPOS carries moves the run wherever it occurs; a pair only the
//    legacy `kern` table has moves the run's *first* pair and nothing else.
//    The L4-kern series is what forced this, and the split falls exactly along
//    the two tables:
//
//      * measured on its own, every one of twelve pairs moved by what the font
//        says -- Te Ta To Wa Ya from GPOS, and ry vo yo ox rm ro ve, which
//        only the legacy table has. So the runtime ignores no pair outright.
//      * "Term" is short by Te and not by rm, and "Terminal" and the pangram
//        agree: ro, ox, ve and rm never move anything away from the front.
//        All four are legacy-only.
//      * but "{StaticResource NotAKey}" is short by 153 units, which is
//        St + Re + Ke at indices 1, 7 and 20 -- three GPOS pairs, deep in the
//        run -- while rc at index 12, the one legacy-only pair it contains,
//        does not move it.
//
//    Where the two tables disagree the GPOS value is the one measured, on all
//    five pairs that disagree. What the corpus cannot say is why the legacy
//    table is consulted at the front of a run at all: "the first pair" and
//    "a run of exactly two glyphs" fit every recording equally, because no
//    recorded run of three or more begins with a legacy-only pair. See
//    phase3/xaml-db/fonts/README.md.
//
// 6. A weight the family does not ship is simulated, and the simulation adds
//    a fixed fraction of the em to every advance. The fraction does not depend
//    on how much heavier the weight asked for was: at size 100 a plain icon
//    glyph is 100 wide and Bold and Black are both 103.
//
//    What the corpus does *not* do is pin the fraction. Black is 103 at 100,
//    205 at 200 and 15 at 14, which bounds it to (0.02, 0.025] of the em --
//    and no tighter, because a FontIcon's desired width is a ceiling, so every
//    value in that interval produces those same three integers. The three
//    rules the earlier two sizes admitted are all dead: a whole DIP gives
//    101/201, a twenty-fourth of the em 104.17/208.33, two per cent 102/204.
//    A TextBlock in an icon font at Black weight, written as Text= so the
//    recording keeps the unsnapped width, would settle it.
//
// 7. The snap in rule 1 is inline content's alone. Text set through the Text
//    property keeps the unsnapped metrics, glyph by glyph and in the line
//    height too. The L4-source twins were authored to ask whether the two
//    spellings are the same thing and the answer is that they are not:
//
//        <TextBlock Text="M"/>        12.5713 wide, 18.6211 tall
//        <TextBlock>M</TextBlock>     12.57 wide,   18.62 tall
//
//    and the same split at "Terminal" (52.042 against 52.04) and at
//    "{StaticResource NotAKey}" (156.2285 against 156.2167), which is where it
//    stops being a rounding curiosity: by then the accumulated difference is
//    wider than the corpus's tolerance. Content becomes an implicit Run in the
//    Inlines collection and the property does not, so the two are different
//    text sources in the runtime; that they are also different arithmetic is
//    the finding.
//
// Every recorded lone-TextBlock case agrees with these rules.

inline constexpr double kTextUnitsPerDip = 300.0;

double SnapText(double value) {
    return std::round(value * kTextUnitsPerDip) / kTextUnitsPerDip;
}

// A layout engine carries these as 32-bit floats, and at ten lines the
// difference reaches the fourth decimal the corpus records.
double AsFloat(double value) {
    return static_cast<double>(static_cast<float>(value));
}

// A measured extent, ceiled.
//
// The corpus has a TextBlock whose text measures 89.2167 and which reports a
// desired width of 90, which rounding cannot produce. The height looked like
// the opposite: an empty TextBlock at size 22 measures 29.2617 and reports 29,
// which ceiling cannot produce. It is not the opposite, it is a second case --
// `L5-styles-style-beats-inherited` measures the same 29.2617 with an "M" in
// it and reports 30. Across the whole corpus the split is exact: all seven
// fractional heights that round down are on a TextBlock whose recorded width
// is zero, and both that ceil are on one with text. So the height is ceiled
// when there is text; when there is none it is left alone and the framework's
// ordinary layout rounding, which every element's desired size goes through,
// produces the 29. See tests/text_measure_test.cpp.
double CeilLayout(double value, double dpi_scale) {
    const double scaled = value * dpi_scale;
    const double nearest = std::nearbyint(scaled);
    // Already whole to within float noise: ceiling it would add a whole pixel
    // for nothing.
    if (AreClose(scaled, nearest)) return nearest / dpi_scale;
    return std::ceil(scaled) / dpi_scale;
}

// --- text ---------------------------------------------------------------------

bool IsBreakSpace(char32_t code) { return code == U' ' || code == U'\t'; }

std::vector<char32_t> DecodeUtf8(const std::string& text) {
    std::vector<char32_t> out;
    size_t index = 0;
    while (index < text.size()) {
        const auto lead = static_cast<unsigned char>(text[index]);
        size_t extra = 0;
        char32_t code = 0;
        if (lead < 0x80) {
            code = lead;
        } else if ((lead & 0xE0) == 0xC0) {
            extra = 1;
            code = lead & 0x1F;
        } else if ((lead & 0xF0) == 0xE0) {
            extra = 2;
            code = lead & 0x0F;
        } else if ((lead & 0xF8) == 0xF0) {
            extra = 3;
            code = lead & 0x07;
        } else {
            throw TextError("the text is not valid UTF-8");
        }
        if (index + extra >= text.size()) throw TextError("the text is not valid UTF-8");
        for (size_t step = 1; step <= extra; ++step) {
            const auto unit = static_cast<unsigned char>(text[index + step]);
            if ((unit & 0xC0) != 0x80) throw TextError("the text is not valid UTF-8");
            code = (code << 6) | (unit & 0x3F);
        }
        out.push_back(code);
        index += extra + 1;
    }
    return out;
}

struct Glyph {
    char32_t code = 0;
    double advance = 0.0;  // already snapped, in DIPs
    bool space = false;
};

std::string Describe(char32_t code) {
    char buffer[16];
    std::snprintf(buffer, sizeof buffer, "U+%04X", static_cast<unsigned>(code));
    return buffer;
}

// What simulating a bold face adds to every advance, as a fraction of the em.
// See rule 6: the recordings bound it to (0.02, 0.025] and no further, and
// every value in that interval produces the same number for every case the
// corpus has, so which one is written here is not observable here.
inline constexpr double kBoldSimulationEm = 0.025;

// A pair adjustment joins the first glyph's advance in design units and snaps
// with it, rather than snapping on its own and being added afterwards. The
// corpus separates the two: Segoe UI kerns T before e by -200 units, and at
// size 12 "Terminal" measures 44.6133, which is the first rule. Snapping the
// -200 alone gives 44.61.
std::vector<Glyph> Shape(const std::string& text, const std::string& family,
                         const FontMetrics& font, double font_size, bool simulates_bold,
                         bool snaps) {
    const std::vector<char32_t> codes = DecodeUtf8(text);
    std::vector<Glyph> glyphs;
    for (size_t index = 0; index < codes.size(); ++index) {
        const char32_t code = codes[index];
        const auto found = font.advances.find(code);
        if (found == font.advances.end()) {
            // Which metrics are loaded decides what the reader has to do about
            // it: harvest a font that covers the character, or -- for the
            // derived set, which only carries what the corpus measures on its
            // own -- fetch the real harvest.
            if (font.provenance == FontProvenance::Derived) {
                throw TextError(
                    "the metrics derived from the recorded measurements have no advance for " +
                    Describe(code) + "; only characters the corpus measures alone are "
                    "solvable, so this case needs the harvested metrics");
            }
            // No family in the list has the character, and the runtime does not
            // stop there: L4-icon-rule-mdl2-latin-14 asks Segoe MDL2 Assets for
            // 'M' and the oracle answers 10 wide, which is neither the icon
            // font's em (14) nor Segoe UI's M at that size (12.57). So it fell
            // back past every family the markup names, to a font chosen by
            // rules this corpus records nothing about. Refusing is the only
            // honest answer until a case measures which font that is.
            throw TextError("no family in \"" + family + "\" has an advance for " +
                            Describe(code) + "; the runtime falls back past the families "
                            "a FontFamily names and this corpus harvests only those");
        }
        const double pair = index + 1 < codes.size()
                                ? font.PairAdjustment(code, codes[index + 1], index == 0)
                                : 0.0;
        // The emboldening is a pen width, so it is a fraction of the em rather
        // than of the glyph. Every recorded case is one em wide, which is where
        // the two readings coincide -- see rule 6.
        const double embolden = simulates_bold ? kBoldSimulationEm * font.units_per_em : 0.0;
        Glyph glyph;
        glyph.code = code;
        const double advance = (found->second + pair + embolden) * font_size / font.units_per_em;
        glyph.advance = snaps ? SnapText(advance) : advance;
        glyph.space = IsBreakSpace(code);
        glyphs.push_back(glyph);
    }
    return glyphs;
}

// Advances accumulate left to right across the whole line, one glyph at a
// time, because that is where rule 3 above says the float rounding happens.
// Summing a word on its own and adding the subtotal is not the same
// computation and does not always give the same number.
double Extend(double width, const std::vector<Glyph>& glyphs, size_t begin, size_t end) {
    for (size_t index = begin; index < end; ++index) width = AsFloat(width + glyphs[index].advance);
    return width;
}

// Greedy line breaking, the standard shape: break after a run of spaces, and
// only break inside a word when the word cannot fit a line of its own.
std::vector<double> BreakLines(const std::vector<Glyph>& glyphs, double limit) {
    std::vector<double> widths;
    const size_t count = glyphs.size();

    size_t position = 0;
    size_t line_begin = 0;
    // Two running totals: what the line measures, which stops at the last
    // visible glyph, and where the next word would start, which does not.
    double visible = 0.0;
    double consumed = 0.0;

    while (position < count) {
        size_t word_end = position;
        while (word_end < count && !glyphs[word_end].space) ++word_end;
        size_t segment_end = word_end;
        while (segment_end < count && glyphs[segment_end].space) ++segment_end;

        double with_word = Extend(consumed, glyphs, position, word_end);

        if (position != line_begin && GreaterThan(with_word, limit)) {
            widths.push_back(visible);
            line_begin = position;
            visible = 0.0;
            consumed = 0.0;
            with_word = Extend(0.0, glyphs, position, word_end);
        }

        if (position == line_begin && GreaterThan(with_word, limit)) {
            // No line can hold this word, so break inside it. At least one
            // glyph per line, otherwise a glyph wider than the limit loops.
            double running = 0.0;
            size_t taken = position;
            while (taken < word_end) {
                const double next = AsFloat(running + glyphs[taken].advance);
                if (taken > position && GreaterThan(next, limit)) break;
                running = next;
                ++taken;
            }
            widths.push_back(running);
            position = taken;
            line_begin = position;
            visible = 0.0;
            consumed = 0.0;
            continue;
        }

        visible = with_word;
        consumed = Extend(with_word, glyphs, word_end, segment_end);
        position = segment_end;
    }

    // A word broken exactly at the end of the text has already closed its last
    // line, so there is nothing left to push.
    if (line_begin < count || widths.empty()) widths.push_back(visible);
    return widths;
}

}  // namespace

// --- FontLibrary --------------------------------------------------------------

void FontLibrary::Add(std::string family, FontMetrics metrics) {
    fonts_[std::move(family)] = std::move(metrics);
}

bool FontLibrary::SetKerning(const std::string& family,
                             std::map<std::pair<char32_t, char32_t>, double> pairs) {
    const auto found = fonts_.find(family);
    if (found == fonts_.end()) return false;
    found->second.kerning = std::move(pairs);
    return true;
}

const FontMetrics* FontLibrary::Find(const std::string& family) const {
    const auto found = fonts_.find(family);
    if (found != fonts_.end()) return &found->second;
    // XAML accepts a comma-separated fallback list. Use the first harvested
    // family in that list, matching the deterministic subset this core can
    // shape rather than treating the whole list as one family name.
    size_t start = 0;
    while (start < family.size()) {
        size_t end = family.find(',', start);
        std::string candidate = family.substr(start, end == std::string::npos ? end : end - start);
        const size_t first = candidate.find_first_not_of(" \t");
        const size_t last = candidate.find_last_not_of(" \t");
        if (first != std::string::npos) {
            candidate = candidate.substr(first, last - first + 1);
            const auto fallback = fonts_.find(candidate);
            if (fallback != fonts_.end()) return &fallback->second;
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return nullptr;
}

const FontMetrics* FontLibrary::FindForText(const std::string& family,
                                            const std::string& text) const {
    if (family.find(',') == std::string::npos) return Find(family);
    const std::vector<char32_t> codes = DecodeUtf8(text);
    const FontMetrics* first_available = nullptr;
    size_t start = 0;
    while (start < family.size()) {
        const size_t end = family.find(',', start);
        std::string candidate = family.substr(start, end == std::string::npos ? end : end - start);
        const size_t first = candidate.find_first_not_of(" \t");
        const size_t last = candidate.find_last_not_of(" \t");
        if (first != std::string::npos) {
            candidate = candidate.substr(first, last - first + 1);
            const auto found = fonts_.find(candidate);
            if (found != fonts_.end()) {
                if (!first_available) first_available = &found->second;
                bool covers = true;
                for (char32_t code : codes) {
                    if (!found->second.advances.count(code)) {
                        covers = false;
                        break;
                    }
                }
                if (covers) return &found->second;
            }
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return first_available;
}

FontLibrary& FontLibrary::Default() {
    static FontLibrary library;
    return library;
}

// --- TextBlock ----------------------------------------------------------------

namespace {

const DependencyProperty* const kText =
    RegisterProperty("TextBlock", "Text", {std::string(), false, true});
const DependencyProperty* const kTextWrapping = RegisterProperty(
    "TextBlock", "TextWrapping", {static_cast<int>(TextWrapping::NoWrap), false, true});

// TextProperties is where FontSize, FontFamily and Foreground are registered:
// a TextBlock carries them without being a Control. See element.h.
const std::vector<std::string> kOwners = {"TextBlock", kTextPropertyOwner, "FrameworkElement",
                                          "UIElement"};

}  // namespace

const DependencyProperty& TextBlock::TextProperty() { return *kText; }
const DependencyProperty& TextBlock::TextWrappingProperty() { return *kTextWrapping; }

const std::vector<std::string>& TextBlock::Owners() { return kOwners; }

Size TextBlock::LayoutText(double limit) const {
    const std::string& family = font_family();
    const double size = font_size();
    const std::string& content = text();

    // Two families, because a fallback list splits the answer. The line box
    // belongs to the family that was named first -- a FontIcon in Terminal's
    // "Segoe UI, Segoe Fluent Icons, Segoe MDL2 Assets" is 12 wide and 16 tall
    // at size 12, which is one em of an icon font inside one line of Segoe UI --
    // while the advances belong to whichever family has the glyph.
    const FontMetrics* line_font = FontLibrary::Default().Find(family);
    const FontMetrics* font = FontLibrary::Default().FindForText(family, content);
    if (!line_font || !font) {
        throw TextError("no harvested metrics for the font family \"" + family +
                        "\"; see phase3/xaml-db/fonts");
    }
    if (font->units_per_em <= 0.0 || line_font->units_per_em <= 0.0)
        throw TextError("the font metrics have no units per em");

    const double spacing = line_font->LineSpacing() * size / line_font->units_per_em;

    // An empty TextBlock still occupies a line, and that line keeps the
    // unsnapped height. This is the one place the two differ, and the corpus
    // records both: 15.9609 empty against 15.96 with text, at size 12.
    if (content.empty()) return {0.0, AsFloat(spacing)};

    // Rule 7: text set through the Text property keeps the unsnapped
    // metrics, inline content gets the snapped ones.
    const bool snaps = !text_from_property_;
    const std::vector<Glyph> glyphs =
        Shape(content, family, *font, size, simulates_bold_, snaps);
    const double effective_limit = text_wrapping() == TextWrapping::Wrap ? limit : kInfinity;
    const std::vector<double> lines = BreakLines(glyphs, effective_limit);

    const double line_height = snaps ? SnapText(spacing) : AsFloat(spacing);
    double width = 0.0;
    double height = 0.0;
    for (double line : lines) {
        width = std::max(width, line);
        height = AsFloat(height + line_height);
    }
    return {width, height};
}

Size TextBlock::MeasureOverride(Size available) {
    const Size text_size = LayoutText(available.width);
    if (text().empty()) return text_size;
    return {CeilLayout(text_size.width, dpi_scale_x),
            CeilLayout(text_size.height, dpi_scale_y)};
}

Size TextBlock::ArrangeOverride(Size final_size) {
    // Re-laid out, because the arranged width is not always the measured one --
    // a stretched TextBlock is arranged into the whole slot, and wrapped text
    // has to be broken against that width to match.
    //
    // The result is returned unclipped, which is why a TextBlock in a slot too
    // narrow for it reports a render size wider than its slot. The corpus has
    // that case and the runtime does the same.
    return LayoutText(final_size.width);
}

}  // namespace openxaml
