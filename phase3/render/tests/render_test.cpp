// What the render pass decides, held where a dump cannot hold it.
//
// The round-trip gate in phase4/scripts/check_render.py is the real check: it
// paints the corpus and recovers every rectangle back out of the pixels. What
// it cannot do is prove a *refusal*, because a case that refuses paints nothing
// and there is nothing to recover -- and refusals are half of this track. So
// the named no-draws are stated here, each one against a tree built for it, and
// so are the two arithmetic rules the dumps depend on: how a colour is parsed
// and where an edge snaps.

#include <cmath>
#include <iostream>
#include <memory>
#include <string>

#include "border.h"
#include "brush.h"
#include "case_runner.h"
#include "content_presenter.h"
#include "display_list.h"
#include "grid.h"
#include "markup.h"
#include "stack_panel.h"
#include "surface.h"
#include "text.h"

using namespace openxaml;
using namespace openxaml::render;

namespace {

int failures = 0;

// Not assert(): a condition here may carry the side effect the next line
// depends on, and NDEBUG would erase it along with the check.
void Check(bool condition, const char* what, int line) {
    if (condition) return;
    std::cerr << "render_test.cpp:" << line << ": CHECK failed: " << what << "\n";
    ++failures;
}

#define CHECK(condition) Check(static_cast<bool>(condition), #condition, __LINE__)

bool HasRefusal(const DisplayList& list, const std::string& feature) {
    for (const Refusal& refusal : list.refusals) {
        if (refusal.feature == feature) return true;
    }
    return false;
}

const RectOp* FindRect(const DisplayList& list, const std::string& what) {
    for (const RectOp& op : list.rects) {
        if (op.what == what) return &op;
    }
    return nullptr;
}

DisplayList LayOut(std::unique_ptr<Element> root, Size available) {
    root->Measure(available);
    root->Arrange({0.0, 0.0, available.width, available.height});
    return Build(*root, available);
}

// --- colours ------------------------------------------------------------------

void ColoursParseTheWayXamlSpellsThem() {
    CHECK(ParseColor("#ff0000", "t") == (Color{0xff, 0xff, 0x00, 0x00}));
    CHECK(ParseColor("#0000ff", "t") == (Color{0xff, 0x00, 0x00, 0xff}));
    CHECK(ParseColor("#80000000", "t") == (Color{0x80, 0x00, 0x00, 0x00}));
    CHECK(ParseColor("#E4000000", "t") == (Color{0xe4, 0x00, 0x00, 0x00}));
    // A missing alpha is opaque, not transparent. The other way round would
    // make every #rrggbb background invisible and the round trip would report
    // a clean pass over an empty surface.
    CHECK(ParseColor("#123456", "t").a == 0xff);
    // Each short-form digit doubles, so #fff is white and not #f0f0f0.
    CHECK(ParseColor("#fff", "t") == (Color{0xff, 0xff, 0xff, 0xff}));
    CHECK(ParseColor("#8abc", "t") == (Color{0x88, 0xaa, 0xbb, 0xcc}));

    // Transparent is alpha zero, which is what makes the seventeen corpus
    // cases that set it paint nothing rather than paint black.
    CHECK(ParseColor("Transparent", "t").a == 0x00);
    CHECK(ParseColor("Black", "t") == (Color{0xff, 0x00, 0x00, 0x00}));
    CHECK(ParseColor("White", "t") == (Color{0xff, 0xff, 0xff, 0xff}));
    // The trap in the named list: XAML's Green is the dark one. Lime is
    // #00ff00 and is deliberately not in the table at all.
    CHECK(ParseColor("Green", "t") == (Color{0xff, 0x00, 0x80, 0x00}));

    bool refused = false;
    try {
        ParseColor("Lime", "t");
    } catch (const MarkupError&) {
        refused = true;
    }
    CHECK(refused);

    // Four digits is #ARGB and is legal; five is nothing.
    refused = false;
    try {
        ParseColor("#ff000", "t");
    } catch (const MarkupError&) {
        refused = true;
    }
    CHECK(refused);
}

// --- snapping -----------------------------------------------------------------

void EdgesSnapTheWayLayoutRounds() {
    // A rounded layout's numbers are already whole, so snapping is the
    // identity -- which is what makes the round trip exact.
    const PixelRect whole = SnapRect(Rect{10.0, 20.0, 30.0, 40.0});
    CHECK(whole.left == 10 && whole.top == 20 && whole.right == 40 && whole.bottom == 60);

    // Half rounds up, the same tie-break RoundLayoutValue uses. Both edges, so
    // a half-pixel rect keeps its width.
    const PixelRect half = SnapRect(Rect{0.5, 0.5, 10.0, 10.0});
    CHECK(half.left == 1 && half.top == 1 && half.right == 11 && half.bottom == 11);
    CHECK(half.width() == 10 && half.height() == 10);

    const PixelRect nothing = SnapRect(Rect{5.0, 5.0, 0.0, 0.0});
    CHECK(nothing.empty());
}

void AnOpaqueFillIsExactlyInvertible() {
    Surface surface(20, 10, BackdropColor());
    surface.FillRect(Rect{3.0, 2.0, 5.0, 4.0}, Color{0xff, 0x12, 0x34, 0x56});
    CHECK(surface.At(3, 2) == Pack(Color{0xff, 0x12, 0x34, 0x56}));
    CHECK(surface.At(7, 5) == Pack(Color{0xff, 0x12, 0x34, 0x56}));
    // One pixel outside on every side is still the backdrop: the fill covers
    // [3,8) x [2,6) and nothing else.
    CHECK(surface.At(2, 2) == Pack(BackdropColor()));
    CHECK(surface.At(8, 2) == Pack(BackdropColor()));
    CHECK(surface.At(3, 1) == Pack(BackdropColor()));
    CHECK(surface.At(3, 6) == Pack(BackdropColor()));

    // Clipping is a clip, not a wrap: a rect running off the right edge fills
    // to the edge and does not appear on the next row.
    Surface clipped(4, 2, BackdropColor());
    clipped.FillRect(Rect{2.0, 0.0, 100.0, 1.0}, Color{0xff, 0xff, 0xff, 0xff});
    CHECK(clipped.At(3, 0) == Pack(Color{0xff, 0xff, 0xff, 0xff}));
    CHECK(clipped.At(0, 1) == Pack(BackdropColor()));

    // A partly transparent brush never reaches here -- the display list refuses
    // it by name first -- and if one ever did it must throw rather than invent
    // a blend.
    bool threw = false;
    try {
        surface.FillRect(Rect{0.0, 0.0, 1.0, 1.0}, Color{0x80, 0x00, 0x00, 0x00});
    } catch (const std::logic_error&) {
        threw = true;
    }
    CHECK(threw);
}

// --- what paints --------------------------------------------------------------

void AnOpaqueBackgroundPaintsItsArrangedRect() {
    auto grid = std::make_unique<Grid>();
    grid->set_background_brush(BrushValue{true, true, Color{0xff, 0xff, 0x00, 0x00}});
    const DisplayList list = LayOut(std::move(grid), Size{400.0, 300.0});

    CHECK(list.refusals.empty());
    CHECK(list.rects.size() == 1);
    const RectOp* background = FindRect(list, "background");
    CHECK(background != nullptr);
    if (background) {
        CHECK(background->bounds.x == 0.0 && background->bounds.y == 0.0);
        CHECK(background->bounds.width == 400.0 && background->bounds.height == 300.0);
        CHECK(background->color == (Color{0xff, 0xff, 0x00, 0x00}));
    }
    // And the geometry the checker compares against is the arranged tree's.
    CHECK(list.geometry.size() == 1);
    CHECK(list.geometry[0].actual.width == 400.0);
}

void ABorderPaintsFourInsetsAtTheThicknessLayoutUsed() {
    auto border = std::make_unique<Border>();
    border->set_border_thickness(Thickness{2.0, 3.0, 4.0, 5.0});
    border->set_border_brush(BrushValue{true, true, Color{0xff, 0x00, 0x00, 0xff}});
    border->set_width(100.0);
    border->set_height(60.0);
    const DisplayList list = LayOut(std::move(border), Size{200.0, 200.0});

    CHECK(list.refusals.empty());
    CHECK(list.rects.size() == 4);
    const RectOp* top = FindRect(list, "border-top");
    const RectOp* bottom = FindRect(list, "border-bottom");
    const RectOp* left = FindRect(list, "border-left");
    const RectOp* right = FindRect(list, "border-right");
    CHECK(top && bottom && left && right);
    if (top) CHECK(top->bounds.width == 100.0 && top->bounds.height == 3.0);
    if (bottom)
        CHECK(bottom->bounds.y == 55.0 && bottom->bounds.height == 5.0 &&
              bottom->bounds.width == 100.0);
    // The sides stop at the top and bottom bars rather than overlapping them,
    // so no pixel is painted twice and the four rects recover separately.
    if (left) CHECK(left->bounds.y == 3.0 && left->bounds.height == 52.0 &&
                    left->bounds.width == 2.0);
    if (right) CHECK(right->bounds.x == 96.0 && right->bounds.width == 4.0);
}

void ABorderThicknessWithNoBrushPaintsNothingAndRefusesNothing() {
    auto border = std::make_unique<Border>();
    border->set_border_thickness(Thickness{2.0, 2.0, 2.0, 2.0});
    border->set_width(50.0);
    border->set_height(50.0);
    const DisplayList list = LayOut(std::move(border), Size{100.0, 100.0});
    // The runtime draws nothing without a brush, so this is the answer and not
    // an omission.
    CHECK(list.rects.empty());
    CHECK(list.refusals.empty());
}

void TransparentPaintsNothingAndIsNotARefusal() {
    auto grid = std::make_unique<Grid>();
    grid->set_background_brush(BrushValue{true, true, ParseColor("Transparent", "t")});
    const DisplayList list = LayOut(std::move(grid), Size{40.0, 40.0});
    CHECK(list.rects.empty());
    CHECK(list.refusals.empty());
}

// Metrics invented for this file, the same way l4_composition_test invents
// them: what is being checked is that the run lands where the arrange put it,
// and holding that to a harvested font would make the check depend on a file
// that is not in the checkout.
void InstallTestFont() {
    FontMetrics metrics;
    metrics.units_per_em = 1000.0;
    metrics.ascender = 800.0;
    metrics.descender = -200.0;
    metrics.line_gap = 0.0;
    for (char32_t c = 32; c < 127; ++c) metrics.advances[c] = 500.0;
    FontLibrary::Default().Add("RenderTestFont", metrics);
}

void ATextBlockBecomesARunAtItsArrangedOrigin() {
    InstallTestFont();
    auto stack = std::make_unique<StackPanel>();
    auto text = std::make_unique<TextBlock>();
    text->set_text("Terminal");
    text->set_font_family("RenderTestFont");
    text->set_font_size(10.0);
    TextBlock* raw = text.get();
    stack->AddChild(std::move(text));
    const DisplayList list = LayOut(std::move(stack), Size{200.0, 100.0});

    CHECK(list.texts.size() == 1);
    if (!list.texts.empty()) {
        CHECK(list.texts[0].text == "Terminal");
        CHECK(list.texts[0].font_family == "RenderTestFont");
        CHECK(list.texts[0].font_size == 10.0);
        // The run's box is the arranged TextBlock, which is what the corpus
        // records, so the ink check has a verified box to be contained in.
        CHECK(list.texts[0].bounds.x == raw->layout_slot().x);
        CHECK(list.texts[0].bounds.y == raw->layout_slot().y);
        CHECK(list.texts[0].bounds.width == raw->render_size().width);
    }
}

// --- what refuses -------------------------------------------------------------

void APartlyTransparentBrushIsANamedNoDraw() {
    auto grid = std::make_unique<Grid>();
    grid->set_background_brush(BrushValue{true, true, Color{0x37, 0x00, 0x00, 0x00}});
    const DisplayList list = LayOut(std::move(grid), Size{40.0, 40.0});
    CHECK(list.rects.empty());
    CHECK(HasRefusal(list, "background"));
}

void ABrushWithNoColourIsANamedNoDraw() {
    auto grid = std::make_unique<Grid>();
    grid->set_background_brush(BrushValue{true, false, Color{}});
    const DisplayList list = LayOut(std::move(grid), Size{40.0, 40.0});
    CHECK(list.rects.empty());
    CHECK(HasRefusal(list, "background"));
}

void TheProbeInkColourIsReserved() {
    auto grid = std::make_unique<Grid>();
    grid->set_background_brush(BrushValue{true, true, ProbeInkColor()});
    const DisplayList list = LayOut(std::move(grid), Size{40.0, 40.0});
    CHECK(list.rects.empty());
    CHECK(HasRefusal(list, "background"));
}

void OpacityIsANamedNoDraw() {
    auto grid = std::make_unique<Grid>();
    grid->set_opacity(0.5);
    grid->set_background_brush(BrushValue{true, true, Color{0xff, 0xff, 0x00, 0x00}});
    const DisplayList list = LayOut(std::move(grid), Size{40.0, 40.0});
    CHECK(HasRefusal(list, "Opacity"));
}

void AnOffsetInsideAWiderSlotIsANamedNoDraw() {
    // A centred child of a Grid: the layout core arranges it at the slot's
    // origin and the runtime moves it, and no recording says by how much.
    auto grid = std::make_unique<Grid>();
    auto child = std::make_unique<Border>();
    child->set_width(50.0);
    child->set_horizontal_alignment(HorizontalAlignment::Center);
    child->set_background_brush(BrushValue{true, true, Color{0xff, 0xff, 0x00, 0x00}});
    grid->AddChild(std::move(child));
    const DisplayList list = LayOut(std::move(grid), Size{400.0, 300.0});
    CHECK(HasRefusal(list, "HorizontalAlignment inside a wider slot"));

    // The same child stretched fills its slot, so there is no offset to be
    // unsure about and it paints.
    auto stretched_grid = std::make_unique<Grid>();
    auto stretched = std::make_unique<Border>();
    stretched->set_background_brush(BrushValue{true, true, Color{0xff, 0xff, 0x00, 0x00}});
    stretched_grid->AddChild(std::move(stretched));
    const DisplayList ok = LayOut(std::move(stretched_grid), Size{400.0, 300.0});
    CHECK(ok.refusals.empty());
    CHECK(ok.rects.size() == 1);
}

void AnUnarrangedElementIsANamedNoDraw() {
    // A Rectangle takes no part in layout, so nothing recorded gives it a rect
    // -- see Element::IsLayoutElement. Its Fill must not become a guess.
    std::unique_ptr<Element> root =
        LoadMarkup("<Canvas xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\">"
                   "<Rectangle Width=\"20\" Height=\"20\" Fill=\"#ff0000\"/></Canvas>",
                   StringTable{});
    root->Measure(Size{100.0, 100.0});
    root->Arrange({0.0, 0.0, 100.0, 100.0});
    const DisplayList list = Build(*root, Size{100.0, 100.0});
    CHECK(list.rects.empty());
    CHECK(HasRefusal(list, "unarranged element"));
}

void ACollapsedElementPaintsNothingAndRefusesNothing() {
    auto grid = std::make_unique<Grid>();
    auto child = std::make_unique<Border>();
    child->set_visibility(Visibility::Collapsed);
    child->set_background_brush(BrushValue{true, true, Color{0xff, 0xff, 0x00, 0x00}});
    grid->AddChild(std::move(child));
    const DisplayList list = LayOut(std::move(grid), Size{100.0, 100.0});
    CHECK(list.rects.empty());
    CHECK(list.refusals.empty());
}

// --- the markup route ---------------------------------------------------------

void TheMarkupCarriesTheColourThroughToThePaint() {
    std::unique_ptr<Element> root =
        LoadMarkup("<Grid xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\" "
                   "Background=\"#0000ff\"/>",
                   StringTable{});
    root->Measure(Size{40.0, 30.0});
    root->Arrange({0.0, 0.0, 40.0, 30.0});
    const DisplayList list = Build(*root, Size{40.0, 30.0});
    CHECK(list.rects.size() == 1);
    if (!list.rects.empty()) CHECK(list.rects[0].color == (Color{0xff, 0x00, 0x00, 0xff}));

    // And a property-element brush is declared without a colour, which is the
    // no-draw the parser's own comment promises.
    std::unique_ptr<Element> element_form =
        LoadMarkup("<Grid xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\">"
                   "<Grid.Background><SolidColorBrush/></Grid.Background></Grid>",
                   StringTable{});
    element_form->Measure(Size{40.0, 30.0});
    element_form->Arrange({0.0, 0.0, 40.0, 30.0});
    const DisplayList from_element = Build(*element_form, Size{40.0, 30.0});
    CHECK(from_element.rects.empty());
    CHECK(HasRefusal(from_element, "background"));
}

// --- the harness ---------------------------------------------------------------

void ACaseThatCannotLoadIsNeitherPaintedNorRefused() {
    CaseResult result = LayOutCase(
        "{\"id\": \"bad\", \"environment\": {\"available_size\": [10, 10]}, "
        "\"markup\": \"<Nope xmlns=\\\"http://schemas.microsoft.com/winfx/2006/xaml/"
        "presentation\\\"/>\"}");
    CHECK(!result.load_error.empty());
    CHECK(!result.has_surface);
}

void TheHarnessArrangesWhatTheMeasurementPathArranges() {
    CaseResult result = LayOutCase(
        "{\"id\": \"one\", \"environment\": {\"available_size\": [400, 300]}, "
        "\"markup\": \"<Grid xmlns=\\\"http://schemas.microsoft.com/winfx/2006/xaml/"
        "presentation\\\" Background=\\\"#ff0000\\\"/>\"}");
    CHECK(result.load_error.empty());
    // The tree is the measurement path's shape, down to the fixed precision --
    // the corpus-wide diff against measure_cases is what makes that a proof,
    // and this is the one line of it that can be checked here.
    CHECK(result.tree_json.find("\"actual\": [400.0000, 300.0000]") != std::string::npos);

    Surface surface = PaintCase(result, nullptr);
    CHECK(surface.width() == 400 && surface.height() == 300);
    CHECK(surface.At(0, 0) == Pack(Color{0xff, 0xff, 0x00, 0x00}));
    CHECK(surface.At(399, 299) == Pack(Color{0xff, 0xff, 0x00, 0x00}));

    const std::string ppm = ToPpm(surface);
    CHECK(ppm.rfind("P6\n400 300\n255\n", 0) == 0);
    CHECK(ppm.size() == 15 + 400u * 300u * 3u);

    // Two renders of the same case are the same bytes. The corpus-wide version
    // of this runs in the gate; this catches an obvious regression without it.
    CaseResult again = LayOutCase(
        "{\"id\": \"one\", \"environment\": {\"available_size\": [400, 300]}, "
        "\"markup\": \"<Grid xmlns=\\\"http://schemas.microsoft.com/winfx/2006/xaml/"
        "presentation\\\" Background=\\\"#ff0000\\\"/>\"}");
    Surface second = PaintCase(again, nullptr);
    CHECK(ToPpm(second) == ppm);
    CHECK(SidecarJson(again, second, "software") == SidecarJson(result, surface, "software"));
}

}  // namespace

int main() {
    ColoursParseTheWayXamlSpellsThem();
    EdgesSnapTheWayLayoutRounds();
    AnOpaqueFillIsExactlyInvertible();
    AnOpaqueBackgroundPaintsItsArrangedRect();
    ABorderPaintsFourInsetsAtTheThicknessLayoutUsed();
    ABorderThicknessWithNoBrushPaintsNothingAndRefusesNothing();
    TransparentPaintsNothingAndIsNotARefusal();
    ATextBlockBecomesARunAtItsArrangedOrigin();
    APartlyTransparentBrushIsANamedNoDraw();
    ABrushWithNoColourIsANamedNoDraw();
    TheProbeInkColourIsReserved();
    OpacityIsANamedNoDraw();
    AnOffsetInsideAWiderSlotIsANamedNoDraw();
    AnUnarrangedElementIsANamedNoDraw();
    ACollapsedElementPaintsNothingAndRefusesNothing();
    TheMarkupCarriesTheColourThroughToThePaint();
    ACaseThatCannotLoadIsNeitherPaintedNorRefused();
    TheHarnessArrangesWhatTheMeasurementPathArranges();

    if (failures) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "render checks passed\n";
    return 0;
}
