// The scanline coverage filler and the recorded-outline text painter.
//
// The filler is held to known polygons first -- coverage of a whole-pixel
// rectangle is exactly 1 inside and exactly 0 outside, a hole is a hole under
// the rule that makes it one -- because every property the corpus check relies
// on reduces to those. Then DrawRecordedOutlineTextRun is held to the placement
// contract: the pen starts at (bounds.x, bounds.y + baseline) and moves by the
// display list's advances, and every gap in the recording is a refusal by
// name, never a .notdef box.

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include "glyph_outline_rasterizer.h"
#include "glyph_outlines.h"
#include "surface.h"

using namespace openxaml;
using namespace openxaml::render;

namespace {

int failures = 0;

void Check(bool condition, const char* expression, int line) {
    if (condition) return;
    std::cerr << "glyph_outline_rasterizer_test.cpp:" << line << ": CHECK failed: "
              << expression << "\n";
    ++failures;
}

#define CHECK(expression) Check(static_cast<bool>(expression), #expression, __LINE__)

constexpr Color kInk{0xff, 0xff, 0x00, 0xff};

Surface Blank(int width = 24, int height = 24) {
    return Surface(width, height, BackdropColor());
}

PixelRect WholeSurface(const Surface& surface) {
    return PixelRect{0, 0, surface.width(), surface.height()};
}

bool IsInk(const Surface& surface, int x, int y) {
    return surface.At(x, y) == Pack(kInk);
}

bool IsBackdrop(const Surface& surface, int x, int y) {
    return surface.At(x, y) == Pack(BackdropColor());
}

OutlinePolygon Rectangle(double left, double top, double right, double bottom) {
    return {{left, top}, {right, top}, {right, bottom}, {left, bottom}};
}

void AWholePixelRectangleFillsExactlyItself() {
    Surface surface = Blank();
    FillContours(surface, {Rectangle(4.0, 5.0, 9.0, 8.0)}, OutlineFillMode::Winding,
                 kInk, WholeSurface(surface));
    for (int y = 0; y < surface.height(); ++y) {
        for (int x = 0; x < surface.width(); ++x) {
            const bool inside = x >= 4 && x < 9 && y >= 5 && y < 8;
            if (inside)
                CHECK(IsInk(surface, x, y));
            else
                CHECK(IsBackdrop(surface, x, y));
        }
    }
}

void FractionalEdgesGetFractionalCoverageAndFullPixelsStayExact() {
    // A rectangle covering exactly half of column 4 and all of column 5.
    Surface surface = Blank();
    FillContours(surface, {Rectangle(4.5, 4.0, 6.0, 5.0)}, OutlineFillMode::Winding,
                 kInk, WholeSurface(surface));
    CHECK(IsInk(surface, 5, 4));
    CHECK(!IsInk(surface, 4, 4) && !IsBackdrop(surface, 4, 4));  // half covered
    CHECK(IsBackdrop(surface, 6, 4));
    CHECK(IsBackdrop(surface, 3, 4));
}

void TheFillRuleDecidesWhetherACounterIsAHole() {
    // An outer square and an inner square wound the same way: under the
    // nonzero (winding) rule the inner square is more ink; under the alternate
    // (even-odd) rule it is a hole.
    const std::vector<OutlinePolygon> same_direction = {
        Rectangle(2.0, 2.0, 12.0, 12.0), Rectangle(5.0, 5.0, 9.0, 9.0)};
    Surface winding = Blank();
    FillContours(winding, same_direction, OutlineFillMode::Winding, kInk,
                 WholeSurface(winding));
    CHECK(IsInk(winding, 6, 6));
    CHECK(IsInk(winding, 3, 3));

    Surface alternate = Blank();
    FillContours(alternate, same_direction, OutlineFillMode::Alternate, kInk,
                 WholeSurface(alternate));
    CHECK(IsBackdrop(alternate, 6, 6));
    CHECK(IsInk(alternate, 3, 3));

    // The inner square reversed: now the winding rule also cuts the hole,
    // which is how a font's counters actually arrive.
    const std::vector<OutlinePolygon> reversed = {
        Rectangle(2.0, 2.0, 12.0, 12.0),
        {{5.0, 5.0}, {5.0, 9.0}, {9.0, 9.0}, {9.0, 5.0}}};
    Surface counter = Blank();
    FillContours(counter, reversed, OutlineFillMode::Winding, kInk,
                 WholeSurface(counter));
    CHECK(IsBackdrop(counter, 6, 6));
    CHECK(IsInk(counter, 3, 3));
}

void TheClipRectangleIsHonoured() {
    Surface surface = Blank();
    FillContours(surface, {Rectangle(0.0, 0.0, 24.0, 24.0)}, OutlineFillMode::Winding,
                 kInk, PixelRect{4, 4, 8, 8});
    CHECK(IsInk(surface, 4, 4));
    CHECK(IsInk(surface, 7, 7));
    CHECK(IsBackdrop(surface, 3, 4));
    CHECK(IsBackdrop(surface, 8, 8));
}

void CubicFlatteningIsFixedDepthAndDeterministic() {
    OutlinePolygon first{{0.0, 0.0}};
    FlattenCubic({0.0, 0.0}, {10.0, -20.0}, {30.0, -20.0}, {40.0, 0.0}, first);
    CHECK(first.size() == 1 + static_cast<size_t>(kCubicChords));
    CHECK(first.back().x == 40.0 && first.back().y == 0.0);

    OutlinePolygon second{{0.0, 0.0}};
    FlattenCubic({0.0, 0.0}, {10.0, -20.0}, {30.0, -20.0}, {40.0, 0.0}, second);
    CHECK(first.size() == second.size());
    for (size_t i = 0; i < first.size() && i < second.size(); ++i)
        CHECK(first[i].x == second[i].x && first[i].y == second[i].y);

    // And the whole fill is deterministic: two renders of a curved shape are
    // byte-identical, which is the rule every dump in this project lives by.
    GlyphOutlineFamily family;
    family.family = "Curved";
    family.units_per_em = 100.0;
    GlyphOutline glyph;
    glyph.glyph_index = 1;
    glyph.advance = 100.0;
    OutlineContour contour;
    contour.start_x = 10.0;
    contour.start_y = -10.0;
    OutlineSegment curve;
    curve.cubic = true;
    curve.x[0] = 90.0; curve.y[0] = -90.0;
    curve.x[1] = 90.0; curve.y[1] = -10.0;
    curve.x[2] = 10.0; curve.y[2] = -90.0;
    contour.segments.push_back(curve);
    OutlineSegment close;
    close.cubic = false;
    close.x[0] = 10.0; close.y[0] = -10.0;
    contour.segments.push_back(close);
    glyph.contours.push_back(contour);
    family.outlines[U'A'] = glyph;
    GlyphOutlineLibrary library;
    library.Add(family);

    TextOp run;
    run.bounds = Rect{2.0, 2.0, 20.0, 20.0};
    run.text = "A";
    run.font_family = "Curved";
    run.font_size = 16.0;
    run.baseline = 16.0;
    run.advances = {16.0};
    run.path = "/TextBlock[0]";

    Surface once = Blank();
    Surface twice = Blank();
    std::string diagnostic;
    CHECK(DrawRecordedOutlineTextRun(library, once, run, kInk, diagnostic));
    CHECK(DrawRecordedOutlineTextRun(library, twice, run, kInk, diagnostic));
    CHECK(once.pixels() == twice.pixels());
    CHECK(once.pixels() != Blank().pixels());
}

// A synthetic family whose glyphs are full-em squares, so every painted pixel
// is predictable from the placement contract alone.
GlyphOutlineLibrary SquareLibrary() {
    GlyphOutlineFamily family;
    family.family = "Square Outline Font";
    family.units_per_em = 100.0;
    for (char32_t code : {U'A', U'B'}) {
        GlyphOutline glyph;
        glyph.glyph_index = code == U'A' ? 1 : 2;
        glyph.advance = 100.0;
        // The em square sits on the baseline and extends one em up: y is
        // negative above the baseline, exactly as the harvest records it.
        glyph.contours.push_back(OutlineContour{
            0.0, -100.0,
            {OutlineSegment{false, {100.0, 0.0, 0.0}, {-100.0, 0.0, 0.0}},
             OutlineSegment{false, {100.0, 0.0, 0.0}, {0.0, 0.0, 0.0}},
             OutlineSegment{false, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}}}});
        family.outlines[code] = glyph;
    }
    GlyphOutlineLibrary library;
    library.Add(std::move(family));
    return library;
}

TextOp SquareRun() {
    TextOp run;
    run.bounds = Rect{2.0, 3.0, 20.0, 12.0};
    run.text = "AB";
    run.font_family = "Square Outline Font";
    run.font_size = 8.0;
    run.baseline = 10.0;
    // The layout's advances, deliberately not the design advance scaled: the
    // second glyph must land where *these* say, at x = 2 + 9 = 11.
    run.advances = {9.0, 8.0};
    run.path = "/TextBlock[0]";
    return run;
}

void GlyphsLandAtThePenAndAdvanceByTheDisplayListsNumbers() {
    const GlyphOutlineLibrary library = SquareLibrary();
    Surface surface = Blank(32, 24);
    std::string diagnostic;
    CHECK(DrawRecordedOutlineTextRun(library, surface, SquareRun(), kInk, diagnostic));
    CHECK(diagnostic.empty());
    // The first em square: x in [2, 10), y in [5, 13) -- pen (2, 3 + 10 = 13),
    // one em of 8px up from the baseline... but clipped to the run's box,
    // whose TouchedRect ends at y = 15; the square's own bottom is the
    // baseline at y = 13.
    CHECK(IsInk(surface, 2, 5));
    CHECK(IsInk(surface, 9, 12));
    CHECK(IsBackdrop(surface, 2, 4));   // above the em square
    CHECK(IsBackdrop(surface, 2, 13));  // on/below the baseline
    // The gap the layout's 9.0 advance opens between the squares: x = 10 is
    // past the first glyph's em (2 + 8) and before the second's pen (11).
    CHECK(IsBackdrop(surface, 10, 8));
    // The second glyph starts at the layout's advance, not the font's.
    CHECK(IsInk(surface, 11, 8));
    CHECK(IsInk(surface, 18, 8));
    CHECK(IsBackdrop(surface, 19, 8));
}

void EveryGapAndUnplaceableRunIsARefusalByName() {
    const GlyphOutlineLibrary library = SquareLibrary();
    std::string diagnostic;

    // A codepoint the recording does not cover refuses the whole run by name
    // and paints nothing -- not even the covered glyphs around the gap.
    Surface surface = Blank(32, 24);
    TextOp gap = SquareRun();
    gap.text = "AZ";
    CHECK(!DrawRecordedOutlineTextRun(library, surface, gap, kInk, diagnostic));
    CHECK(diagnostic.find("U+005A") != std::string::npos);
    CHECK(diagnostic.find("Square Outline Font") != std::string::npos);
    CHECK(diagnostic.find(gap.path) == 0);
    CHECK(surface.pixels() == Blank(32, 24).pixels());

    // A family with no recording at all is the caller's cue to try the next
    // painter; the message says which family was asked for.
    diagnostic.clear();
    TextOp other = SquareRun();
    other.font_family = "Segoe UI";
    CHECK(!DrawRecordedOutlineTextRun(library, surface, other, kInk, diagnostic));
    CHECK(diagnostic.find("Segoe UI") != std::string::npos);

    // A run the layout broke into lines is not placeable from advances alone.
    diagnostic.clear();
    TextOp broken = SquareRun();
    broken.text = "A\nB";
    broken.advances = {9.0, 0.0, 8.0};
    CHECK(!DrawRecordedOutlineTextRun(library, surface, broken, kInk, diagnostic));
    CHECK(diagnostic.find("line-broken") != std::string::npos);
    CHECK(diagnostic.find(broken.path) == 0);

    // A wrapped run whose retained advances overflow the box wrapped when it
    // was measured, so its glyph positions are unknown here too.
    diagnostic.clear();
    TextOp wrapped = SquareRun();
    wrapped.wrap = true;
    wrapped.advances = {15.0, 15.0};
    CHECK(!DrawRecordedOutlineTextRun(library, surface, wrapped, kInk, diagnostic));
    CHECK(diagnostic.find("line-broken") != std::string::npos);

    // One advance per codepoint or nothing, the same rule the DirectWrite
    // painter holds runs to.
    diagnostic.clear();
    TextOp uneven = SquareRun();
    uneven.advances = {9.0};
    CHECK(!DrawRecordedOutlineTextRun(library, surface, uneven, kInk, diagnostic));
    CHECK(diagnostic.find("advance") != std::string::npos);

    // A baseline of zero means no metrics were available; placing the run at
    // the box top would be a guess, and the contract is to say so.
    diagnostic.clear();
    TextOp unmeasured = SquareRun();
    unmeasured.baseline = 0.0;
    CHECK(!DrawRecordedOutlineTextRun(library, surface, unmeasured, kInk, diagnostic));
    CHECK(diagnostic.find("baseline") != std::string::npos);

    // A fractional clip would need edge-coverage masking; refuse, exactly as
    // the DirectWrite painter does.
    diagnostic.clear();
    TextOp clipped = SquareRun();
    clipped.has_clip = true;
    clipped.clip = Rect{2.0, 3.0, 10.5, 8.0};
    CHECK(!DrawRecordedOutlineTextRun(library, surface, clipped, kInk, diagnostic));
    CHECK(diagnostic.find("fractional") != std::string::npos);
}

void InkStaysInsideTheRunsBoxAndTheClip() {
    // A glyph whose em square is far taller than the measured box: everything
    // outside TouchedRect(bounds) must stay backdrop, because the corpus
    // checker compares those pixels byte-for-byte against the solid pass.
    const GlyphOutlineLibrary library = SquareLibrary();
    Surface surface = Blank(32, 24);
    TextOp run = SquareRun();
    run.font_size = 40.0;  // em square of 40px in a 12px-tall box
    run.advances = {40.0, 40.0};
    std::string diagnostic;
    CHECK(DrawRecordedOutlineTextRun(library, surface, run, kInk, diagnostic));
    const PixelRect box = TouchedRect(run.bounds);
    for (int y = 0; y < surface.height(); ++y) {
        for (int x = 0; x < surface.width(); ++x) {
            const bool inside = x >= box.left && x < box.right && y >= box.top &&
                                y < box.bottom;
            if (!inside) CHECK(IsBackdrop(surface, x, y));
        }
    }
    CHECK(IsInk(surface, box.left, box.top + 3));

    // An integral clip narrows the box further.
    Surface clipped_surface = Blank(32, 24);
    TextOp clipped = run;
    clipped.has_clip = true;
    clipped.clip = Rect{4.0, 3.0, 6.0, 8.0};
    CHECK(DrawRecordedOutlineTextRun(library, clipped_surface, clipped, kInk, diagnostic));
    for (int y = 0; y < clipped_surface.height(); ++y) {
        for (int x = 0; x < clipped_surface.width(); ++x) {
            const bool inside = x >= 4 && x < 10 && y >= 3 && y < 11;
            if (!inside) CHECK(IsBackdrop(clipped_surface, x, y));
        }
    }
}

void TheOutlineBackendLabelsWhatItPaintsAndNamesWhatItCannot() {
    const GlyphOutlineLibrary library = SquareLibrary();
    RecordedOutlineTextBackend backend(library);
    CHECK(backend.CoversFamily("Square Outline Font"));
    CHECK(backend.CoversFamily("Nonexistent, Square Outline Font"));
    CHECK(!backend.CoversFamily("Segoe UI"));

    Surface surface = Blank(32, 24);
    std::vector<std::string> backend_failures;
    std::vector<std::string> painters;
    backend.DrawRuns(surface, {SquareRun()}, kInk, backend_failures, &painters);
    CHECK(backend_failures.empty());
    CHECK(painters.size() == 1 && painters[0] == "recorded-outlines");

    TextOp gap = SquareRun();
    gap.text = "AZ";
    painters.clear();
    backend.DrawRuns(surface, {gap}, kInk, backend_failures, &painters);
    CHECK(backend_failures.size() == 1);
    CHECK(backend_failures[0].find("U+005A") != std::string::npos);
    CHECK(painters.size() == 1 && painters[0].empty());
}

}  // namespace

int main() {
    AWholePixelRectangleFillsExactlyItself();
    FractionalEdgesGetFractionalCoverageAndFullPixelsStayExact();
    TheFillRuleDecidesWhetherACounterIsAHole();
    TheClipRectangleIsHonoured();
    CubicFlatteningIsFixedDepthAndDeterministic();
    GlyphsLandAtThePenAndAdvanceByTheDisplayListsNumbers();
    EveryGapAndUnplaceableRunIsARefusalByName();
    InkStaysInsideTheRunsBoxAndTheClip();
    TheOutlineBackendLabelsWhatItPaintsAndNamesWhatItCannot();

    if (failures != 0) {
        std::cerr << failures << " glyph outline rasterizer check(s) failed\n";
        return 1;
    }
    std::cout << "glyph outline rasterizer checks passed\n";
    return 0;
}
