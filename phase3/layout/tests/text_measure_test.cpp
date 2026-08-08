// How a TextBlock's measured text becomes a desired size.
//
// The corpus can barely say. Every text height it records has a fractional part
// above a half, where ceiling and rounding agree -- except at two places, and
// those two disagree with each other:
//
//   L0-props-inherited-fontsize            29.2617 -> 29
//   L5-styles-style-beats-inherited        29.2617 -> 30
//
// The same number, the same font, the same size, two answers. What separates
// them is that the first TextBlock is empty and the second holds an "M", and
// every other recorded row agrees: all seven fractional heights that round down
// have a recorded width of zero, and both that ceil have a width. So the height
// is ceiled when there is text, and left to the framework's ordinary layout
// rounding when there is not.
//
// A test rather than a corpus case because the corpus already has the two rows
// and cannot easily add a third: the font is Segoe UI, whose line spacing lands
// above the half at every size the corpus measures. Here the font is chosen so
// that ceiling and rounding give visibly different answers.
//
// Deliberately not a framework, like the tests beside it.

#include <cmath>
#include <iostream>
#include <string>

#include "text.h"

using namespace openxaml;

namespace {

int failures = 0;

// Not assert(): NDEBUG would erase the check and the side effect with it.
void Check(bool condition, const char* what, int line) {
    if (condition) return;
    std::cerr << "text_measure_test.cpp:" << line << ": CHECK failed: " << what << "\n";
    ++failures;
}

#define CHECK(condition) Check(static_cast<bool>(condition), #condition, __LINE__)

// The unrounded numbers are carried through a float, so they are compared the
// way the corpus compares them rather than exactly.
bool Near(double actual, double expected) { return std::fabs(actual - expected) < 1e-4; }

// Line spacing of 1010 units per em, so a size of 10 measures 10.1 -- a
// fractional part below the half, which is the whole point.
void InstallATestFont() {
    FontMetrics metrics;
    metrics.units_per_em = 1000.0;
    metrics.ascender = 810.0;
    metrics.descender = -200.0;
    metrics.advances[U'M'] = 500.0;
    FontLibrary::Default().Add("Test", metrics);
}

void Configure(TextBlock& block, const std::string& content) {
    block.set_font_family("Test");
    block.set_font_size(10.0);
    block.set_text(content);
}

void TextCeilsItsHeight() {
    TextBlock block;
    Configure(block, "M");
    block.Measure({400.0, 300.0});
    CHECK(block.desired_size().width == 5.0);
    CHECK(block.desired_size().height == 11.0);
}

void EmptyTextRoundsIt() {
    TextBlock block;
    Configure(block, "");
    block.Measure({400.0, 300.0});
    CHECK(block.desired_size().width == 0.0);
    CHECK(block.desired_size().height == 10.0);
}

// Neither rule touches the render size: the runtime records `actual` unrounded
// for the empty TextBlock and the one holding text alike.
void NeitherRuleTouchesTheRenderSize() {
    TextBlock block;
    Configure(block, "M");
    block.Measure({400.0, 300.0});
    block.Arrange({0.0, 0.0, 5.0, 11.0});
    CHECK(Near(block.render_size().height, 10.1));

    TextBlock empty;
    Configure(empty, "");
    empty.Measure({400.0, 300.0});
    empty.Arrange({0.0, 0.0, 0.0, 10.0});
    CHECK(Near(empty.render_size().height, 10.1));
}

}  // namespace

int main() {
    InstallATestFont();

    TextCeilsItsHeight();
    EmptyTextRoundsIt();
    NeitherRuleTouchesTheRenderSize();

    if (failures) return 1;
    std::cout << "text measurement checks passed\n";
    return 0;
}
