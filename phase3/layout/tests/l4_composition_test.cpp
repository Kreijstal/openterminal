// How a level 4 size is *composed* out of font metrics.
//
// The corpus pins the answers, but a measurement is a tree of numbers and
// cannot say which of several rules produced one. These do: each check below
// isolates one composition rule against metrics invented for the test, so a
// rule that happens to be right for Segoe UI and wrong in general fails here
// rather than surviving until the next font is harvested.

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

#include "border.h"
#include "fonts.h"
#include "icon.h"
#include "json.h"
#include "text.h"

using namespace openxaml;

namespace {

// Not assert(): a condition here may carry the side effect the next line
// depends on, and NDEBUG would erase it along with the check.
void Check(bool condition, const char* what, int line) {
    if (condition) return;
    std::cerr << "l4_composition_test.cpp:" << line << ": CHECK failed: " << what << "\n";
    std::exit(1);
}

#define CHECK(condition) Check(static_cast<bool>(condition), #condition, __LINE__)

bool Near(double value, double expected) { return std::abs(value - expected) < 1e-4; }

// A square icon font: one em per glyph, one em per line, which is what both
// harvested icon families turn out to be.
FontMetrics SquareIcons(char32_t glyph) {
    FontMetrics metrics;
    metrics.units_per_em = 2048;
    metrics.ascender = 2048;
    metrics.descender = 0;
    metrics.advances[glyph] = 2048;
    return metrics;
}

// A text font whose line box is taller than its em, which is what makes the
// fallback rule below observable at all.
FontMetrics TallText() {
    FontMetrics metrics;
    metrics.units_per_em = 2048;
    metrics.ascender = 2210;
    metrics.descender = -514;
    metrics.advances[U'M'] = 1839;
    return metrics;
}

// A FontIcon is a FrameworkElement, so a stretched one fills the slot it was
// arranged into. It used to report the glyph's own size instead, which made
// every root-level FontIcon in the corpus arrange to its glyph rather than to
// the window.
void AnIconFillsTheSlotItIsGiven() {
    FontLibrary::Default().Add("Slot Icons", SquareIcons(0xE76C));

    FontIcon icon;
    icon.set_font_family("Slot Icons");
    icon.set_font_size(14);
    icon.set_glyph(u8"");
    icon.Measure({400, 300});
    CHECK(Near(icon.desired_size().width, 14) && Near(icon.desired_size().height, 14));
    icon.Arrange({0, 0, 400, 300});
    CHECK(Near(icon.render_size().width, 400) && Near(icon.render_size().height, 300));

    // A slot of nothing is still the slot. The corpus measures this directly:
    // every L4-icon-*-a1 case is arranged into 0x0 and reports 0x0.
    FontIcon squeezed;
    squeezed.set_font_family("Slot Icons");
    squeezed.set_font_size(14);
    squeezed.set_glyph(u8"");
    squeezed.Measure({0, 0});
    squeezed.Arrange({0, 0, 0, 0});
    CHECK(Near(squeezed.render_size().width, 0) && Near(squeezed.render_size().height, 0));

    // Not stretched, so it takes what it asked for and no more.
    FontIcon near_corner;
    near_corner.set_font_family("Slot Icons");
    near_corner.set_font_size(14);
    near_corner.set_glyph(u8"");
    near_corner.set_horizontal_alignment(HorizontalAlignment::Left);
    near_corner.set_vertical_alignment(VerticalAlignment::Top);
    near_corner.Measure({400, 300});
    near_corner.Arrange({0, 0, 400, 300});
    CHECK(Near(near_corner.render_size().width, 14) && Near(near_corner.render_size().height, 14));
}

// A FontFamily may be a fallback list, and the two halves of a text size come
// from different entries in it: the line box from the family that was asked
// for first, the advance from the first family that actually has the glyph.
// Reading both off the covering family makes an icon in Terminal's list square
// when the runtime makes it as tall as Segoe UI's line.
void AFallbackListSplitsTheLineBoxFromTheGlyph() {
    FontLibrary::Default().Add("Split Text", TallText());
    FontLibrary::Default().Add("Split Icons", SquareIcons(0xE932));

    FontIcon icon;
    icon.set_font_family("Split Text, Split Icons");
    icon.set_font_size(12);
    icon.set_glyph(u8"");
    icon.Measure({400, 300});
    // Width: one em of the icon family. Height: (2210 + 514) / 2048 * 12, which
    // is 15.9609 snapped to 15.96 and then rounded up to the pixel grid.
    CHECK(Near(icon.desired_size().width, 12));
    CHECK(Near(icon.desired_size().height, 16));

    // The first family is still the line box when it is also the one with the
    // glyph -- the rule is "first in the list", not "the one without the glyph".
    FontIcon plain;
    plain.set_font_family("Split Icons, Split Text");
    plain.set_font_size(12);
    plain.set_glyph(u8"");
    plain.Measure({400, 300});
    CHECK(Near(plain.desired_size().width, 12) && Near(plain.desired_size().height, 12));
}

// A weight an icon family does not ship is simulated, and the simulation adds
// the same fraction of the em whichever heavy weight was asked for. Bold and
// Black both measure 103 at size 100 where the plain glyph measures 100, which
// is what says the amount does not track how much heavier the weight was.
void ASimulatedWeightWidensByAFixedFraction() {
    FontLibrary::Default().Add("Weight Icons", SquareIcons(0xE76C));

    FontIcon plain;
    plain.set_font_family("Weight Icons");
    plain.set_font_size(100);
    plain.set_glyph(u8"");
    plain.Measure({400, 300});
    CHECK(Near(plain.desired_size().width, 100));

    for (const char* weight : {"Bold", "Black"}) {
        FontIcon icon;
        icon.set_font_family("Weight Icons");
        icon.set_font_size(100);
        icon.set_glyph(u8"");
        icon.set_font_weight(weight);
        icon.Measure({400, 300});
        CHECK(Near(icon.desired_size().width, 103));
        CHECK(Near(icon.desired_size().height, 100));  // the line box is untouched
    }

    // Every fraction the recordings admit produces these same integers, which
    // is why the constant in text.cpp is not observable here. What is
    // observable is that the three rules the two smaller sizes once allowed are
    // all dead: they answer 201, 204 and 208 where the runtime answered 205.
    FontIcon at200;
    at200.set_font_family("Weight Icons");
    at200.set_font_size(200);
    at200.set_glyph(u8"");
    at200.set_font_weight("Black");
    at200.Measure({400, 300});
    CHECK(Near(at200.desired_size().width, 205));
}

// A weight nothing measured is still refused. The corpus records Normal, Bold
// and Black and no others, so sorting an unseen weight into "simulated" or not
// would be a threshold this repository invented.
void AnUnmeasuredWeightIsRefused() {
    FontLibrary::Default().Add("Unseen Icons", SquareIcons(0xE76C));

    FontIcon icon;
    icon.set_font_family("Unseen Icons");
    icon.set_font_size(14);
    icon.set_glyph(u8"");
    icon.set_font_weight("SemiBold");
    bool refused = false;
    try {
        icon.Measure({400, 300});
    } catch (const TextError& error) {
        refused = std::string(error.what()).find("SemiBold") != std::string::npos;
    }
    CHECK(refused);
}

// A pair adjustment is part of the first glyph's advance and is applied in
// design units, before the advance snaps to 1/300 of a DIP. Snapping the
// adjustment on its own gives a different answer -- at size 12 the two differ
// by 1/300 -- and the corpus records the first.
void KerningJoinsTheAdvanceBeforeItSnaps() {
    FontMetrics font = TallText();
    font.advances[U'T'] = 1073;
    font.advances[U'e'] = 1071;
    font.kerning[{U'T', U'e'}] = -200;
    FontLibrary::Default().Add("Kerned", font);

    TextBlock text;
    text.set_font_family("Kerned");
    text.set_font_size(12);
    text.set_text("Te");
    text.Measure({400, 300});
    text.Arrange({0, 0, 400, 300});
    // round((1073 - 200) * 12 / 2048 * 300) / 300 = 5.116667, plus e's 6.276667.
    CHECK(Near(text.render_size().width, 11.393333));

    // Unkerned pairs are untouched, so a font with a kern table does not move
    // text that does not use it.
    text.set_text("eT");
    text.Arrange({0, 0, 400, 300});
    CHECK(Near(text.render_size().width, 12.563333));
}

// Which pairs reach past the front of a run, and which do not. A pair the
// font's GPOS carries moves the run wherever it sits; a pair only the legacy
// `kern` table has moves the first pair and nothing else. This is the rule the
// L4-kern recordings forced and the one thing here no reader will believe
// without the numbers, so the test carries both halves against one font.
//
// The advances and the adjustment land on whole DIPs at this size, so the
// arithmetic is readable and no snap can hide the difference.
void OnlyGposPairsReachPastTheFrontOfARun() {
    FontMetrics font = TallText();
    font.advances[U'a'] = 2048;   // one em
    font.advances[U'b'] = 2048;
    font.advances[U'c'] = 2048;
    font.kerning[{U'a', U'b'}] = -1024;   // half an em, from the legacy table
    font.kerning[{U'c', U'a'}] = -1024;   // and the same from GPOS
    font.kerns_anywhere.insert({U'c', U'a'});
    FontLibrary::Default().Add("Positional", font);

    TextBlock text;
    text.set_font_family("Positional");
    text.set_font_size(16);   // one em is 16 DIPs and either pair is worth -8

    text.set_text("ab");                    // legacy pair, at the front
    text.Measure({4000, 300});
    text.Arrange({0, 0, 4000, 300});
    CHECK(Near(text.render_size().width, 8 + 16));

    text.set_text("bab");                   // the same pair, one glyph in
    text.Arrange({0, 0, 4000, 300});
    CHECK(Near(text.render_size().width, 3 * 16));

    text.set_text("bbbab");                 // and further in, still nothing
    text.Arrange({0, 0, 4000, 300});
    CHECK(Near(text.render_size().width, 5 * 16));

    text.set_text("ca");                    // the GPOS pair, at the front
    text.Arrange({0, 0, 4000, 300});
    CHECK(Near(text.render_size().width, 8 + 16));

    text.set_text("bca");                   // and away from it, still applied
    text.Arrange({0, 0, 4000, 300});
    CHECK(Near(text.render_size().width, 16 + 8 + 16));

    text.set_text("bbbca");
    text.Arrange({0, 0, 4000, 300});
    CHECK(Near(text.render_size().width, 3 * 16 + 8 + 16));

    // Two GPOS pairs deep in a run both count, which is what the recorded
    // "{StaticResource NotAKey}" needs: it is short by three of them.
    text.set_text("bcaca");
    text.Arrange({0, 0, 4000, 300});
    CHECK(Near(text.render_size().width, 16 + 8 + 16 + 8 + 16));
}

// Where the pair values come from. A harvest carries the font's two tables
// under "font_kerning" and the layout core reads them, because every isolated
// two-character run moved by exactly what the font says. GPOS wins where the
// two disagree, which the recordings settle rather than convention: Segoe UI's
// tables differ on five pairs and the runtime took the GPOS value every time.
void AHarvestReadsTheFontsTablesWithGposWinning() {
    const std::string evidence =
        R"({"family": "Evidence", "provenance": "harvested", "units_per_em": 2048,
            "hhea": {"ascender": 2210, "descender": -514, "line_gap": 0},
            "advances": {"77": 1839},
            "font_kerning": {"gpos": {"84,101": -200}, "kern": {"84,101": -211,
                             "111,120": -25}}})";
    const FontMetrics metrics = ParseFontMetrics(evidence, "evidence.json");
    CHECK(metrics.PairAdjustment(U'T', U'e', false) == -200);  // GPOS, not the -211
    CHECK(metrics.kerns_anywhere.count({U'T', U'e'}) == 1);
    // The legacy table's own pair reaches the front of a run and no further.
    CHECK(metrics.PairAdjustment(U'o', U'x', true) == -25);
    CHECK(metrics.PairAdjustment(U'o', U'x', false) == 0);
    CHECK(metrics.PairAdjustment(U'M', U'M', true) == 0);

    // "kerning" is the derived spelling and means something else -- the pairs
    // the recordings witnessed, one per two-character run. A harvest claiming
    // it is the shape of a run that predates the L4-kern series.
    const std::string wrong_key =
        R"({"family": "Refused", "provenance": "harvested", "units_per_em": 2048,
            "hhea": {"ascender": 2210, "descender": -514, "line_gap": 0},
            "advances": {"77": 1839}, "kerning": {"84,101": -200}})";
    bool refused = false;
    try {
        ParseFontMetrics(wrong_key, "refused.json");
    } catch (const JsonError& error) {
        refused = std::string(error.what()).find("font_kerning") != std::string::npos;
    }
    CHECK(refused);

    // A derived file states the witnessed pairs and is read as such.
    const std::string derived =
        R"({"family": "Derived", "provenance": "derived", "units_per_em": 2048,
            "hhea": {"ascender": 2724, "descender": 0, "line_gap": 0},
            "advances": {"77": 1839}, "kerning": {"84,101": -200}})";
    CHECK(ParseFontMetrics(derived, "derived.json").PairAdjustment(U'T', U'e', true) == -200);
}

// The two spellings of a TextBlock's text are not the same measurement. Inline
// content snaps every advance and the line height to 1/300 of a DIP; the Text
// property keeps them unsnapped. The L4-source twins were authored to ask
// whether the two agree, and this is the answer they came back with.
void TheTextPropertyIsNotSnapped() {
    FontMetrics font = TallText();
    font.advances[U'T'] = 1073;
    font.advances[U'e'] = 1071;
    font.advances[U'r'] = 712;
    FontLibrary::Default().Add("Unsnapped", font);

    TextBlock content;
    content.set_font_family("Unsnapped");
    content.set_font_size(14);
    content.set_text("Ter");
    content.Measure({400, 300});
    content.Arrange({0, 0, 400, 300});

    TextBlock property;
    property.set_font_family("Unsnapped");
    property.set_font_size(14);
    property.set_text("Ter");
    property.set_text_from_property(true);
    property.Measure({400, 300});
    property.Arrange({0, 0, 400, 300});

    // Same font, same size, same text; the widths differ by the snap and the
    // unsnapped one is the larger here.
    CHECK(!Near(content.render_size().width, property.render_size().width));
    CHECK(Near(content.render_size().width, 7.333333 + 7.32 + 4.866667));
    CHECK(Near(property.render_size().width,
               (1073 + 1071 + 712) * 14.0 / 2048));
    // And the line box goes the same way.
    CHECK(Near(content.render_size().height, 18.62));
    CHECK(Near(property.render_size().height, 2724 * 14.0 / 2048));
}

}  // namespace

int main() {
    AnIconFillsTheSlotItIsGiven();
    AFallbackListSplitsTheLineBoxFromTheGlyph();
    ASimulatedWeightWidensByAFixedFraction();
    AnUnmeasuredWeightIsRefused();
    KerningJoinsTheAdvanceBeforeItSnaps();
    OnlyGposPairsReachPastTheFrontOfARun();
    AHarvestReadsTheFontsTablesWithGposWinning();
    TheTextPropertyIsNotSnapped();
    std::cout << "level 4 composition tests passed\n";
    return 0;
}
