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

// A weight the harvested metrics were not read at is a refusal, not a guess.
// The corpus measures FontWeight="Black" at two sizes and both say the glyph
// grew, but two observations do not pin how much: +1 DIP, +1/24 em and +2% of
// the em all reproduce them. Measuring against any one of those would be
// inventing font data.
void AnUnharvestedWeightIsRefused() {
    FontLibrary::Default().Add("Weight Icons", SquareIcons(0xE76C));

    FontIcon icon;
    icon.set_font_family("Weight Icons");
    icon.set_font_size(14);
    icon.set_glyph(u8"");
    icon.set_font_weight("Black");
    bool refused = false;
    try {
        icon.Measure({400, 300});
    } catch (const TextError& error) {
        refused = std::string(error.what()).find("Black") != std::string::npos;
    }
    CHECK(refused);

    // Normal is the weight the metrics were read at, so it measures.
    icon.set_font_weight("Normal");
    icon.Measure({400, 300});
    CHECK(Near(icon.desired_size().width, 14));
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

// Which pairs the runtime applies is a measurement, not a reading, so a
// harvested file may not claim it. This is not tidiness: the runner's Segoe UI
// kerns "ox", "ro", "ve" and "rm", and the recorded pangram containing the
// first three measures the raw sum of its advances -- so a layout core that
// installed the font's table would measure those cases too narrow. The refusal
// is what stops a harvest written before that was understood from loading.
void AHarvestedFileMayNotClaimKerning() {
    const std::string body =
        R"({"family": "Refused", "provenance": "harvested", "units_per_em": 2048,
            "hhea": {"ascender": 2210, "descender": -514, "line_gap": 0},
            "advances": {"77": 1839}, "kerning": {"84,101": -200}})";
    bool refused = false;
    try {
        ParseFontMetrics(body, "refused.json");
    } catch (const JsonError& error) {
        refused = std::string(error.what()).find("font_kerning") != std::string::npos;
    }
    CHECK(refused);

    // The font's own table under its own key is evidence and loads fine,
    // because nothing here reads it.
    const std::string evidence =
        R"({"family": "Evidence", "provenance": "harvested", "units_per_em": 2048,
            "hhea": {"ascender": 2210, "descender": -514, "line_gap": 0},
            "advances": {"77": 1839},
            "font_kerning": {"gpos": {"84,101": -200}, "kern": {"111,120": -25}}})";
    const FontMetrics metrics = ParseFontMetrics(evidence, "evidence.json");
    CHECK(metrics.kerning.empty());
    CHECK(metrics.advances.at(U'M') == 1839);
}

// The two halves arrive separately, so they have to meet. Advances come from
// the harvest; the pairs come from the committed file the measurements imply.
void ImpliedKerningInstallsOntoAHarvestedFamily() {
    FontMetrics harvested = TallText();
    harvested.advances[U'T'] = 1073;
    harvested.advances[U'e'] = 1071;
    FontLibrary library;
    library.Add("Implied", harvested);
    CHECK(library.Find("Implied")->kerning.empty());

    CHECK(library.SetKerning("Implied", {{{U'T', U'e'}, -200}}));
    CHECK(library.Find("Implied")->PairAdjustment(U'T', U'e') == -200);
    // A pair the file does not name stays unadjusted rather than defaulting to
    // whatever the font would have said.
    CHECK(library.Find("Implied")->PairAdjustment(U'e', U'T') == 0);

    // Naming a family nothing loaded is the case where the two inputs describe
    // different runs, and silence there would measure every kerned case wrongly.
    CHECK(!library.SetKerning("Not Loaded", {{{U'T', U'e'}, -200}}));
}

}  // namespace

int main() {
    AnIconFillsTheSlotItIsGiven();
    AFallbackListSplitsTheLineBoxFromTheGlyph();
    AnUnharvestedWeightIsRefused();
    KerningJoinsTheAdvanceBeforeItSnaps();
    AHarvestedFileMayNotClaimKerning();
    ImpliedKerningInstallsOntoAHarvestedFamily();
    std::cout << "level 4 composition tests passed\n";
    return 0;
}
