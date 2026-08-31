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
#include "icon.h"
#include "image.h"
#include "markup.h"
#include "shape.h"
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

class FixedExternalSurfaceProvider final : public ExternalSurfaceProvider {
public:
    explicit FixedExternalSurfaceProvider(ExternalSurfaceReference reference)
        : reference_(std::move(reference)) {}
    ExternalSurfaceReference CaptureExternalSurface() const noexcept override {
        return reference_;
    }

private:
    ExternalSurfaceReference reference_;
};

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

    // FillRect remains the exact opaque overwrite primitive. Fractional alpha
    // has an explicit BlendRect path and cannot accidentally enter this one.
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
    // Explicit Width/Height constrain Stretch's ink; non-overflowing Stretch
    // centres that ink in the larger client box.
    if (top)
        CHECK(top->bounds.x == 50.0 && top->bounds.y == 70.0 &&
              top->bounds.width == 100.0 && top->bounds.height == 3.0);
    if (bottom)
        CHECK(bottom->bounds.y == 125.0 && bottom->bounds.height == 5.0 &&
              bottom->bounds.width == 100.0);
    // The sides stop at the top and bottom bars rather than overlapping them,
    // so no pixel is painted twice and the four rects recover separately.
    if (left) CHECK(left->bounds.y == 73.0 && left->bounds.height == 52.0 &&
                    left->bounds.width == 2.0);
    if (right) CHECK(right->bounds.x == 146.0 && right->bounds.width == 4.0);
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
    metrics.advances[0xe921] = 500.0;
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
        CHECK(list.texts[0].bounds.x == raw->render_origin().x);
        CHECK(list.texts[0].bounds.y == raw->render_origin().y);
        CHECK(list.texts[0].bounds.width == raw->render_size().width);
    }
}

void AZeroAreaTextBlockPaintsNothing() {
    InstallTestFont();
    auto text = std::make_unique<TextBlock>();
    text->set_font_family("RenderTestFont");
    text->set_height(20.0);
    text->Measure({0.0, 20.0});
    // Grid's track allocator may leave a tiny positive remainder for a track
    // it considers zero. This must remain a no-draw just like exact zero.
    text->Arrange({0.0, 0.0, 1e-6, 20.0});
    // Model a binding changing after the retained layout was committed. The
    // render invalidation can run before the coalesced layout invalidation.
    text->set_text("bound but hidden");
    const DisplayList list = Build(*text, Size{200.0, 100.0});

    CHECK(list.texts.empty());
    CHECK(list.refusals.empty());
    CHECK(list.scene && list.scene->nodes().size() == 1);
    if (list.scene && !list.scene->nodes().empty()) {
        CHECK(list.scene->nodes().front().content->commands.empty());
    }
}

void AFontIconBecomesAnIconFontRun() {
    InstallTestFont();
    auto icon = std::make_unique<FontIcon>();
    icon->set_glyph("\xee\xa4\xa1"); // U+E921, Terminal's minimize glyph.
    icon->set_font_family("RenderTestFont");
    icon->set_font_size(20.0);
    icon->set_width(20.0);
    icon->set_height(20.0);
    const DisplayList list = LayOut(std::move(icon), Size{20.0, 20.0});

    CHECK(list.refusals.empty());
    CHECK(list.texts.size() == 1);
    if (!list.texts.empty()) {
        CHECK(list.texts[0].text == "\xee\xa4\xa1");
        CHECK(list.texts[0].font_family == "RenderTestFont");
        CHECK(list.texts[0].font_size == 20.0);
    }
}

// --- composition --------------------------------------------------------------

void APartlyTransparentBrushUsesPremultipliedSourceOver() {
    auto grid = std::make_unique<Grid>();
    grid->set_background_brush(BrushValue{true, true, Color{0x37, 0x00, 0x00, 0x00}});
    CaseResult result;
    result.list = LayOut(std::move(grid), Size{40.0, 40.0});
    CHECK(result.list.refusals.empty());
    Surface surface = PaintCase(result, nullptr, Color{0, 0, 0, 0});
    CHECK(result.render_issues.empty());
    CHECK(surface.At(0, 0) == PackPremultiplied(0x37, 0x00, 0x00, 0x00));
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

void OpacityCompositesACompleteNodeLayer() {
    auto grid = std::make_unique<Grid>();
    grid->set_opacity(0.5);
    grid->set_background_brush(BrushValue{true, true, Color{0xff, 0xff, 0x00, 0x00}});
    CaseResult result;
    result.list = LayOut(std::move(grid), Size{40.0, 40.0});
    CHECK(result.list.refusals.empty());
    Surface surface = PaintCase(result, nullptr, Color{0, 0, 0, 0});
    CHECK(result.render_issues.empty());
    CHECK(surface.At(0, 0) == PackPremultiplied(0x80, 0x80, 0x00, 0x00));
}

void AlignedOriginsComposeThroughTheRetainedScene() {
    auto grid = std::make_unique<Grid>();
    grid->set_background_brush(BrushValue{true, true, Color{0xff, 0xff, 0xff, 0xff}});
    auto outer = std::make_unique<Border>();
    Border* outer_raw = outer.get();
    outer->set_width(100.0);
    outer->set_height(72.0);
    outer->set_horizontal_alignment(HorizontalAlignment::Center);
    outer->set_vertical_alignment(VerticalAlignment::Center);
    outer->set_padding({11.0, 7.0, 11.0, 7.0});
    outer->set_background_brush(BrushValue{true, true, Color{0xff, 0x20, 0x30, 0x40}});
    auto inner = std::make_unique<Border>();
    Border* inner_raw = inner.get();
    inner->set_background_brush(BrushValue{true, true, Color{0xff, 0xff, 0xb0, 0x00}});
    outer->SetChild(std::move(inner));
    grid->AddChild(std::move(outer));

    grid->Measure({160.0, 112.0});
    grid->Arrange({0.0, 0.0, 160.0, 112.0});
    CHECK(outer_raw->layout_slot().x == 0.0);
    CHECK(outer_raw->layout_slot().y == 0.0);
    CHECK(outer_raw->render_origin().x == 30.0);
    CHECK(outer_raw->render_origin().y == 20.0);
    CHECK(inner_raw->render_origin().x == 11.0);
    CHECK(inner_raw->render_origin().y == 7.0);

    const DisplayList list = Build(*grid, Size{160.0, 112.0});
    CHECK(list.refusals.empty());
    CHECK(list.rects.size() == 3);
    if (list.rects.size() == 3) {
        CHECK(list.rects[1].bounds.x == 30.0);
        CHECK(list.rects[1].bounds.y == 20.0);
        CHECK(list.rects[2].bounds.x == 41.0);
        CHECK(list.rects[2].bounds.y == 27.0);
        CHECK(list.rects[2].bounds.width == 78.0);
        CHECK(list.rects[2].bounds.height == 58.0);
    }
    CHECK(list.scene != nullptr);
    if (list.scene) {
        const auto outer_transform =
            list.scene->TransformToRoot(NodeId{outer_raw->render_node_id()});
        const auto inner_transform =
            list.scene->TransformToRoot(NodeId{inner_raw->render_node_id()});
        CHECK(outer_transform.has_value());
        CHECK(inner_transform.has_value());
        if (outer_transform) {
            CHECK(outer_transform->dx == 30.0);
            CHECK(outer_transform->dy == 20.0);
        }
        if (inner_transform) {
            CHECK(inner_transform->dx == 41.0);
            CHECK(inner_transform->dy == 27.0);
        }
    }
}

void AnUnarrangedElementIsANamedNoDraw() {
    // Canvas geometry needs an explicit extent. An Auto Rectangle has neither
    // layout storage nor a complete visual size, so it remains a named refusal.
    std::unique_ptr<Element> root =
        LoadMarkup("<Canvas xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\">"
                   "<Rectangle Fill=\"#ff0000\"/></Canvas>",
                   StringTable{});
    root->Measure(Size{100.0, 100.0});
    root->Arrange({0.0, 0.0, 100.0, 100.0});
    const DisplayList list = Build(*root, Size{100.0, 100.0});
    CHECK(list.rects.empty());
    CHECK(HasRefusal(list, "unarranged element"));
}

// "No layout storage" is not the same as "no rect".
//
// A Shape at the root of a tree is not a layout element, so nothing ever
// measures or arranges it and it never gets layout storage -- but the runtime
// still answers ActualWidth and ActualHeight out of the specified size once the
// element has been measured, and the corpus records exactly that. The recorded
// tree for L7-terminal-65dec6afa8 is one Rectangle with desired [0, 0], actual
// [12, 12] and offset [0, 0]: the oracle gives this element a rect. Refusing it
// as unarranged contradicts the measurement it is supposed to be checked
// against.
//
// Element::render_size already reproduces that number (see
// terminal_subtree_test.cpp). Only the render compiler disagreed.
void AMeasuredRootWithAnExplicitExtentHasTheRecordedRect() {
    std::unique_ptr<Element> root = LoadMarkup(
        "<Rectangle xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\" "
        "Width=\"12\" Height=\"12\" Fill=\"#ff0000\"/>",
        StringTable{});
    root->Measure(Size{400.0, 300.0});
    root->Arrange({0.0, 0.0, 400.0, 300.0});
    CHECK(!root->has_layout_storage());
    CHECK(root->render_size().width == 12.0);

    const DisplayList list = Build(*root, Size{400.0, 300.0});
    CHECK(!HasRefusal(list, "unarranged element"));
    const RectOp* fill = FindRect(list, "fill");
    CHECK(fill != nullptr);
    if (fill) {
        CHECK(fill->bounds.x == 0.0);
        CHECK(fill->bounds.y == 0.0);
        CHECK(fill->bounds.width == 12.0);
        CHECK(fill->bounds.height == 12.0);
    }

    // Measured is load-bearing. An element that never had Measure called on it
    // is still measure-dirty, still answers zero from ActualWidth, and still
    // has no rect any recorded measurement gives it.
    std::unique_ptr<Element> unmeasured = LoadMarkup(
        "<Rectangle xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\" "
        "Width=\"12\" Height=\"12\" Fill=\"#ff0000\"/>",
        StringTable{});
    const DisplayList never = Build(*unmeasured, Size{400.0, 300.0});
    CHECK(never.rects.empty());
    CHECK(HasRefusal(never, "unarranged element"));

    // And so is the explicit extent: an Auto Rectangle has no size to take
    // from the markup, so it keeps the refusal the Canvas case above gets.
    std::unique_ptr<Element> automatic = LoadMarkup(
        "<Rectangle xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\" "
        "Fill=\"#ff0000\"/>",
        StringTable{});
    automatic->Measure(Size{400.0, 300.0});
    automatic->Arrange({0.0, 0.0, 400.0, 300.0});
    const DisplayList auto_list = Build(*automatic, Size{400.0, 300.0});
    CHECK(auto_list.rects.empty());
    CHECK(HasRefusal(auto_list, "unarranged element"));
}

// A rounded Rectangle is named, for the reason a rounded Border is.
//
// RadiusX and RadiusY round a Rectangle's corners with the same antialiased
// arc a Border's CornerRadius draws, and phase4/scripts/check_render.py
// recovers axis-aligned rectangles out of the pixels and nothing else. Painting
// the fill as a sharp rectangle would put pixels in the four corners that the
// runtime leaves empty, and the round trip would confirm the rectangle this
// project drew rather than the shape the markup asked for.
//
// Both corpus cases that reach here are rounded: L7-terminal-4302b18781 is a
// 40 x 20 Rectangle with RadiusX and RadiusY 10, and L7-terminal-65dec6afa8 a
// 12 x 12 one with 7.
void ARoundedRectangleIsANamedNoDraw() {
    const auto rounded = [](const char* radii) {
        std::unique_ptr<Element> root = LoadMarkup(
            std::string("<Rectangle xmlns=\"http://schemas.microsoft.com/winfx/2006/"
                        "xaml/presentation\" Width=\"40\" Height=\"20\" Fill=\"#ff0000\" ") +
                radii + "/>",
            StringTable{});
        root->Measure(Size{400.0, 300.0});
        root->Arrange({0.0, 0.0, 400.0, 300.0});
        return Build(*root, Size{400.0, 300.0});
    };

    CHECK(HasRefusal(rounded("RadiusX=\"10\" RadiusY=\"10\""), "RadiusX"));
    // One axis is enough to round the corners.
    CHECK(HasRefusal(rounded("RadiusX=\"10\""), "RadiusX"));
    // Named for the property that is actually set: a Rectangle with only a
    // RadiusY is not refused for a RadiusX its markup never wrote.
    CHECK(HasRefusal(rounded("RadiusY=\"10\""), "RadiusY"));
    CHECK(!HasRefusal(rounded("RadiusY=\"10\""), "RadiusX"));
    // Zero is a square corner and paints as one.
    CHECK(!HasRefusal(rounded("RadiusX=\"0\" RadiusY=\"0\""), "RadiusX"));
    CHECK(!HasRefusal(rounded(""), "RadiusX"));

    // A radius that draws nothing is not a gap. Refusing a Rectangle whose
    // Fill and Stroke both paint nothing would report a no-draw as a feature
    // this project cannot do -- the same rule the CornerRadius refusal keeps.
    std::unique_ptr<Element> unpainted = LoadMarkup(
        "<Rectangle xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\" "
        "Width=\"40\" Height=\"20\" RadiusX=\"10\" RadiusY=\"10\"/>",
        StringTable{});
    unpainted->Measure(Size{400.0, 300.0});
    unpainted->Arrange({0.0, 0.0, 400.0, 300.0});
    CHECK(!HasRefusal(Build(*unpainted, Size{400.0, 300.0}), "RadiusX"));
}

void ShapeStrokeStateIsRetainedAsANamedRendererBoundary() {
    auto root = std::make_unique<Grid>();
    auto rectangle = std::make_unique<Rectangle>();
    rectangle->set_width(24.0);
    rectangle->set_height(16.0);
    rectangle->set_stroke_thickness(2.0);
    rectangle->set_stroke_brush(
        BrushValue{true, true, Color{0xff, 0x20, 0x40, 0x60}});
    rectangle->set_stroke_line_join(ShapeLineJoin::Round);
    rectangle->set_stroke_dash_offset(1.5);

    root->AddChild(std::move(rectangle));
    DisplayList list = LayOut(std::move(root), Size{24.0, 16.0});
    CHECK(HasRefusal(list, "Stroke"));
    CHECK(list.rects.empty());

    root = std::make_unique<Grid>();
    auto collapsed = std::make_unique<Path>();
    collapsed->set_width(24.0);
    collapsed->set_height(16.0);
    collapsed->set_visibility(Visibility::Collapsed);
    collapsed->set_stroke_thickness(2.0);
    collapsed->set_stroke_brush(
        BrushValue{true, true, Color{0xff, 0x20, 0x40, 0x60}});
    root->AddChild(std::move(collapsed));
    list = LayOut(std::move(root), Size{24.0, 16.0});
    CHECK(!HasRefusal(list, "Stroke"));
}

void CanvasRectanglesUseExplicitVisualGeometryWithoutPublicLayoutStorage() {
    CaseResult result = LayOutCase(
        "{\"id\":\"R3\",\"environment\":{\"available_size\":[128,96]},"
        "\"markup\":\"<Canvas xmlns=\\\"http://schemas.microsoft.com/winfx/2006/xaml/"
        "presentation\\\" Width=\\\"128\\\" Height=\\\"96\\\" "
        "Background=\\\"Transparent\\\"><Rectangle Width=\\\"72\\\" "
        "Height=\\\"56\\\" Fill=\\\"#C0FF4000\\\" Canvas.Left=\\\"12\\\" "
        "Canvas.Top=\\\"10\\\"/><Rectangle Width=\\\"72\\\" Height=\\\"56\\\" "
        "Fill=\\\"#8000A0FF\\\" Canvas.Left=\\\"42\\\" Canvas.Top=\\\"28\\\" "
        "Opacity=\\\"0.75\\\"/></Canvas>\"}");
    CHECK(result.load_error.empty());
    CHECK(result.list.refusals.empty());
    CHECK(result.list.geometry.size() == 3);
    if (result.list.geometry.size() == 3) {
        CHECK(result.list.geometry[1].actual.width == 0.0);
        CHECK(result.list.geometry[1].actual.height == 0.0);
        CHECK(result.list.geometry[1].abs_x == 12.0);
        CHECK(result.list.geometry[1].abs_y == 10.0);
        CHECK(result.list.geometry[2].actual.width == 0.0);
        CHECK(result.list.geometry[2].actual.height == 0.0);
        CHECK(result.list.geometry[2].abs_x == 42.0);
        CHECK(result.list.geometry[2].abs_y == 28.0);
    }
    Surface surface = PaintCase(result, nullptr, Color{0, 0, 0, 0});
    CHECK(result.render_issues.empty());
    CHECK(surface.At(12, 10) == PackPremultiplied(0xc0, 0xc0, 0x30, 0x00));
    CHECK(surface.At(50, 30) == PackPremultiplied(0xd8, 0x78, 0x5a, 0x60));
    CHECK(surface.At(100, 30) == PackPremultiplied(0x60, 0x00, 0x3c, 0x60));
}

void CanvasGeometryCompilesRotateTransformAndRetainsUnsupportedTransforms() {
    std::unique_ptr<Element> root = LoadMarkup(
        "<Canvas xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\" "
        "Width=\"40\" Height=\"30\"><Rectangle Width=\"20\" Height=\"10\" "
        "Fill=\"#ff0000\" RenderTransformOrigin=\"0.5,0.5\">"
        "<Rectangle.RenderTransform><RotateTransform Angle=\"17\">"
        "</RotateTransform>"
        "</Rectangle.RenderTransform></Rectangle></Canvas>",
        StringTable{});
    root->Measure({40.0, 30.0});
    root->Arrange({0.0, 0.0, 40.0, 30.0});
    const DisplayList list = Build(*root, Size{40.0, 30.0});
    CHECK(!HasRefusal(list, "RenderTransform"));
    CHECK(list.scene != nullptr);
    if (list.scene && list.geometry.size() == 2) {
        const Matrix3x2& transform = list.geometry[1].transform_to_root;
        const Point pivot = transform.TransformPoint({10.0, 5.0});
        CHECK(std::abs(pivot.x - 10.0) < 1e-9);
        CHECK(std::abs(pivot.y - 5.0) < 1e-9);
        CHECK(std::abs(transform.m12 - std::sin(17.0 * 3.14159265358979323846 / 180.0)) <
              1e-9);
    }

    // An attribute-free <CompositeTransform/> is the identity matrix -- every
    // one of its properties is a no-op at its default -- so there is nothing
    // here to decline to lower. It compiles, it paints the untransformed
    // rectangle, and the scene carries no unsupported transform for the
    // backend to issue on. (Previously refused as "RenderTransform"; the
    // refusal named a transform that does not move a pixel.)
    root = LoadMarkup(
        "<Canvas xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\" "
        "Width=\"40\" Height=\"30\"><Rectangle Width=\"20\" Height=\"10\" "
        "Canvas.Left=\"6\" Canvas.Top=\"4\" "
        "Fill=\"#ff0000\" RenderTransformOrigin=\"0.5,0.5\">"
        "<Rectangle.RenderTransform><CompositeTransform/>"
        "</Rectangle.RenderTransform></Rectangle></Canvas>",
        StringTable{});
    root->Measure({40.0, 30.0});
    root->Arrange({0.0, 0.0, 40.0, 30.0});
    const DisplayList identity = Build(*root, Size{40.0, 30.0});
    CHECK(!HasRefusal(identity, "RenderTransform"));
    const RectOp* placed = FindRect(identity, "fill");
    CHECK(placed != nullptr);
    if (placed) {
        CHECK(placed->bounds.x == 6.0);
        CHECK(placed->bounds.y == 4.0);
        CHECK(placed->bounds.width == 20.0);
        CHECK(placed->bounds.height == 10.0);
    }
    CHECK(identity.scene != nullptr);
    if (identity.scene) {
        for (const VisualNode& node : identity.scene->nodes())
            CHECK(node.unsupported_transform.empty());
    }

    // A CompositeTransform that authors any of its properties is a general
    // composite and keeps its name.
    root = LoadMarkup(
        "<Canvas xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\" "
        "Width=\"40\" Height=\"30\"><Rectangle Width=\"20\" Height=\"10\" "
        "Fill=\"#ff0000\"><Rectangle.RenderTransform>"
        "<CompositeTransform SkewX=\"12\"/>"
        "</Rectangle.RenderTransform></Rectangle></Canvas>",
        StringTable{});
    root->Measure({40.0, 30.0});
    root->Arrange({0.0, 0.0, 40.0, 30.0});
    CHECK(HasRefusal(Build(*root, Size{40.0, 30.0}), "RenderTransform"));
}

void RectangleGeometryClipIsRetainedLocallyAndRasterizedAfterTranslation() {
    CaseResult result = LayOutCase(
        "{\"id\":\"R4\",\"environment\":{\"available_size\":[128,96]},"
        "\"markup\":\"<Canvas xmlns=\\\"http://schemas.microsoft.com/winfx/2006/xaml/"
        "presentation\\\" Width=\\\"128\\\" Height=\\\"96\\\" "
        "Background=\\\"#FFFFFFFF\\\"><Rectangle Width=\\\"92\\\" Height=\\\"68\\\" "
        "Fill=\\\"#FF6A2CA0\\\" Canvas.Left=\\\"18\\\" Canvas.Top=\\\"14\\\">"
        "<Rectangle.Clip><RectangleGeometry Rect=\\\"9,8,54,37\\\"/>"
        "</Rectangle.Clip></Rectangle></Canvas>\"}");
    CHECK(result.load_error.empty());
    CHECK(result.list.refusals.empty());
    CHECK(result.list.scene != nullptr);
    const VisualNode* clipped = nullptr;
    if (result.list.scene) {
        for (const VisualNode& node : result.list.scene->nodes()) {
            if (node.clip.kind == Clip::Kind::Rect) clipped = &node;
        }
    }
    CHECK(clipped != nullptr);
    if (clipped) {
        CHECK(clipped->local_transform.dx == 18.0);
        CHECK(clipped->local_transform.dy == 14.0);
        CHECK(clipped->clip.bounds.x == 9.0);
        CHECK(clipped->clip.bounds.y == 8.0);
        CHECK(clipped->clip.bounds.width == 54.0);
        CHECK(clipped->clip.bounds.height == 37.0);
    }

    Surface surface = PaintCase(result, nullptr, Color{0, 0, 0, 0});
    CHECK(result.render_issues.empty());
    CHECK(surface.At(18, 14) == Pack(Color{0xff, 0xff, 0xff, 0xff}));
    CHECK(surface.At(27, 22) == Pack(Color{0xff, 0x6a, 0x2c, 0xa0}));
    CHECK(surface.At(80, 58) == Pack(Color{0xff, 0x6a, 0x2c, 0xa0}));
    CHECK(surface.At(81, 58) == Pack(Color{0xff, 0xff, 0xff, 0xff}));

    const std::string sidecar = SidecarJson(result, surface, "test", Color{0, 0, 0, 0});
    CHECK(sidecar.find("\"type\": \"Windows.UI.Xaml.Media.RectangleGeometry\", "
                       "\"bounds\": [9.0000, 8.0000, 54.0000, 37.0000]") !=
          std::string::npos);
}

void UnsupportedClipGeometryRemainsANamedRefusal() {
    std::unique_ptr<Element> root = LoadMarkup(
        "<Canvas xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\" "
        "Width=\"40\" Height=\"30\"><Rectangle Width=\"20\" Height=\"10\" "
        "Fill=\"#ff0000\"><Rectangle.Clip><EllipseGeometry Center=\"5,5\" "
        "RadiusX=\"4\" RadiusY=\"3\"/></Rectangle.Clip></Rectangle></Canvas>",
        StringTable{});
    root->Measure({40.0, 30.0});
    root->Arrange({0.0, 0.0, 40.0, 30.0});
    const DisplayList list = Build(*root, Size{40.0, 30.0});
    CHECK(HasRefusal(list, "Clip"));

    bool malformed = false;
    try {
        LoadMarkup(
            "<Rectangle xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\" "
            "Width=\"20\" Height=\"10\"><Rectangle.Clip>"
            "<RectangleGeometry Rect=\"0,0,-1,2\"/></Rectangle.Clip></Rectangle>",
            StringTable{});
    } catch (const MarkupError&) {
        malformed = true;
    }
    CHECK(malformed);
}

void CanvasZIndexOrdersSiblingsAndSurvivesTheSidecar() {
    CaseResult result = LayOutCase(
        "{\"id\":\"R6\",\"environment\":{\"available_size\":[128,96]},"
        "\"markup\":\"<Canvas xmlns=\\\"http://schemas.microsoft.com/winfx/2006/xaml/"
        "presentation\\\" Width=\\\"128\\\" Height=\\\"96\\\" "
        "Background=\\\"#FFFFFFFF\\\"><Rectangle Width=\\\"70\\\" Height=\\\"54\\\" "
        "Fill=\\\"#FFE84855\\\" Canvas.Left=\\\"10\\\" Canvas.Top=\\\"10\\\" "
        "Canvas.ZIndex=\\\"1\\\"/><Rectangle Width=\\\"70\\\" Height=\\\"54\\\" "
        "Fill=\\\"#FF3D72C9\\\" Canvas.Left=\\\"30\\\" Canvas.Top=\\\"24\\\" "
        "Canvas.ZIndex=\\\"0\\\"/><Rectangle Width=\\\"54\\\" Height=\\\"42\\\" "
        "Fill=\\\"#FFFFC857\\\" Canvas.Left=\\\"54\\\" Canvas.Top=\\\"40\\\" "
        "Canvas.ZIndex=\\\"2\\\"/></Canvas>\"}");
    CHECK(result.load_error.empty());
    CHECK(result.list.refusals.empty());
    CHECK(result.list.geometry.size() == 4);
    if (result.list.geometry.size() == 4) {
        CHECK(result.list.geometry[1].z_index == 1);
        CHECK(result.list.geometry[2].z_index == 0);
        CHECK(result.list.geometry[3].z_index == 2);
    }

    Surface surface = PaintCase(result, nullptr, Color{0, 0, 0, 0});
    CHECK(result.render_issues.empty());
    CHECK(surface.At(35, 30) == Pack(Color{0xff, 0xe8, 0x48, 0x55}));
    CHECK(surface.At(90, 30) == Pack(Color{0xff, 0x3d, 0x72, 0xc9}));
    CHECK(surface.At(60, 45) == Pack(Color{0xff, 0xff, 0xc8, 0x57}));

    const std::string sidecar = SidecarJson(result, surface, "test", Color{0, 0, 0, 0});
    CHECK(sidecar.find("\"z_index\": 2") != std::string::npos);
}

void EqualZIndexPreservesSourceOrder() {
    CaseResult result = LayOutCase(
        "{\"id\":\"equal-z\",\"environment\":{\"available_size\":[20,20]},"
        "\"markup\":\"<Canvas xmlns=\\\"http://schemas.microsoft.com/winfx/2006/xaml/"
        "presentation\\\" Width=\\\"20\\\" Height=\\\"20\\\"><Rectangle "
        "Width=\\\"12\\\" Height=\\\"12\\\" Fill=\\\"#FFFF0000\\\" "
        "Canvas.ZIndex=\\\"-3\\\"/><Rectangle Width=\\\"12\\\" Height=\\\"12\\\" "
        "Fill=\\\"#FF0000FF\\\" Canvas.ZIndex=\\\"-3\\\"/></Canvas>\"}");
    CHECK(result.load_error.empty());
    Surface surface = PaintCase(result, nullptr, Color{0, 0, 0, 0});
    CHECK(result.render_issues.empty());
    CHECK(surface.At(5, 5) == Pack(Color{0xff, 0x00, 0x00, 0xff}));
}

void BorderAndPanelChromePropertiesLoadAndPaintGenerically() {
    std::unique_ptr<Element> root = LoadMarkup(
        "<Grid xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\" "
        "Width=\"100\" Height=\"80\" Background=\"#FF20252B\" Padding=\"10\">"
        "<StackPanel Spacing=\"8\"><Border Height=\"30\" Background=\"#FF303841\" "
        "BorderBrush=\"#FF5B6672\" BorderThickness=\"1\" Padding=\"4\">"
        "<Grid/></Border><Border Height=\"10\"/></StackPanel></Grid>",
        StringTable{});
    root->Measure({100.0, 80.0});
    root->Arrange({0.0, 0.0, 100.0, 80.0});
    const DisplayList list = Build(*root, Size{100.0, 80.0});
    CHECK(list.refusals.empty());
    const RectOp* top = FindRect(list, "border-top");
    CHECK(top != nullptr);
    if (top) {
        CHECK(top->bounds.x == 10.0);
        CHECK(top->bounds.y == 10.0);
        CHECK(top->bounds.width == 80.0);
        CHECK(top->bounds.height == 1.0);
        CHECK(top->color == (Color{0xff, 0x5b, 0x66, 0x72}));
    }
}

// A drawn rounded corner is a named no-draw, and an undrawn one is not.
//
// This is the half of the CornerRadius rule that no dump can show. A refused
// case paints nothing, so its pixels cannot distinguish "the pass refused,
// correctly" from "the pass quietly painted the square version" -- and the
// square version is exactly what this pass would otherwise emit, four
// rectangles with mitred corners that look right everywhere except the four
// places the property is about.
//
// The refusal is conditional on something actually being drawn with the radius,
// because a radius on chrome with no brush changes no pixel. Reporting that as
// a gap would put a case in the refused column for a property that made no
// difference to it.
void ARoundedCornerIsANamedNoDrawOnlyWhereItWouldBeDrawn() {
    const std::string ns =
        "xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\" ";
    const auto compile = [&](const std::string& attributes) {
        std::unique_ptr<Element> root =
            LoadMarkup("<Border " + ns + "Width=\"40\" Height=\"20\" " + attributes + "/>",
                       StringTable{});
        root->Measure({40.0, 20.0});
        root->Arrange({0.0, 0.0, 40.0, 20.0});
        return Build(*root, Size{40.0, 20.0});
    };

    // Drawn: a border brush over a non-zero radius.
    DisplayList list = compile("BorderBrush=\"#FF5B6672\" BorderThickness=\"1\" "
                               "CornerRadius=\"4\"");
    CHECK(HasRefusal(list, "CornerRadius"));
    // Drawn: a background over a non-zero radius, with no border at all.
    list = compile("Background=\"#FF303841\" CornerRadius=\"4\"");
    CHECK(HasRefusal(list, "CornerRadius"));

    // Not drawn: the radius is zero, so the rectangles this pass paints are the
    // rectangles the runtime paints.
    list = compile("BorderBrush=\"#FF5B6672\" BorderThickness=\"1\" CornerRadius=\"0\"");
    CHECK(list.refusals.empty());
    // Not drawn: a radius on chrome that has no brush covers no pixel.
    list = compile("BorderThickness=\"1\" CornerRadius=\"4\"");
    CHECK(list.refusals.empty());
    // Not drawn: a fully transparent brush is a no-draw the pass already knows
    // about, and a radius does not turn it into one.
    list = compile("Background=\"#00303841\" CornerRadius=\"4\"");
    CHECK(list.refusals.empty());

    // The same rule reaches the panels, which grew the property in WinUI 2.6.
    std::unique_ptr<Element> panel = LoadMarkup(
        "<Grid " + ns + "Width=\"40\" Height=\"20\" Background=\"#FF303841\" "
        "CornerRadius=\"4\"/>", StringTable{});
    panel->Measure({40.0, 20.0});
    panel->Arrange({0.0, 0.0, 40.0, 20.0});
    CHECK(HasRefusal(Build(*panel, Size{40.0, 20.0}), "CornerRadius"));
}

void ForegroundRetainsItsSolidColorBrushWithoutMeasuringText() {
    std::unique_ptr<Element> text = LoadMarkup(
        "<TextBlock xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\" "
        "Text=\"Open Terminal\" Foreground=\"#FFF4F4F4\"/>",
        StringTable{});
    CHECK(text->foreground_brush().declared);
    CHECK(text->foreground_brush().has_color);
    CHECK(text->foreground_brush().color == (Color{0xff, 0xf4, 0xf4, 0xf4}));
    CHECK(text->GetString(ForegroundProperty()) == "SolidColorBrush");
}

void R8StopsAtTheNamedFontMetricsBoundary() {
    CaseResult result = LayOutCase(
        "{\"id\":\"R8\",\"environment\":{\"available_size\":[240,144]},"
        "\"markup\":\"<Grid xmlns=\\\"http://schemas.microsoft.com/winfx/2006/xaml/"
        "presentation\\\" Width=\\\"240\\\" Height=\\\"144\\\" "
        "Background=\\\"#FF20252B\\\" Padding=\\\"16\\\"><StackPanel "
        "Spacing=\\\"8\\\"><TextBlock Text=\\\"Open Terminal\\\" FontFamily=\\\"Segoe UI\\\" "
        "FontSize=\\\"22\\\" Foreground=\\\"#FFF4F4F4\\\"/><Border Height=\\\"54\\\" "
        "Background=\\\"#FF303841\\\" BorderBrush=\\\"#FF5B6672\\\" "
        "BorderThickness=\\\"1\\\" Padding=\\\"10,7\\\"><StackPanel>"
        "<TextBlock Text=\\\"Renderer boundary\\\" FontFamily=\\\"Segoe UI\\\" "
        "FontSize=\\\"14\\\" Foreground=\\\"#FFFFFFFF\\\"/><TextBlock "
        "Text=\\\"pixels + visual geometry\\\" FontFamily=\\\"Segoe UI\\\" "
        "FontSize=\\\"12\\\" Foreground=\\\"#FFB8C2CC\\\"/></StackPanel>"
        "</Border></StackPanel></Grid>\"}");
    CHECK(result.load_error.find("no harvested metrics for the font family \"Segoe UI\"") !=
          std::string::npos);
    CHECK(result.load_error.find("BorderBrush") == std::string::npos);
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

    // Property-element brushes retain their concrete semantics. A default
    // SolidColorBrush is Transparent, not an unknown brush.
    std::unique_ptr<Element> element_form =
        LoadMarkup("<Grid xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\">"
                   "<Grid.Background><SolidColorBrush/></Grid.Background></Grid>",
                   StringTable{});
    CHECK(element_form->background_brush().kind == BrushKind::SolidColor);
    CHECK(element_form->background_brush().has_color);
    CHECK(element_form->background_brush().color.a == 0);
    element_form->Measure(Size{40.0, 30.0});
    element_form->Arrange({0.0, 0.0, 40.0, 30.0});
    const DisplayList from_element = Build(*element_form, Size{40.0, 30.0});
    CHECK(from_element.rects.empty());
    CHECK(from_element.refusals.empty());

    std::unique_ptr<Element> coloured_element =
        LoadMarkup("<Grid xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\">"
                   "<Grid.Background><SolidColorBrush Color=\"#FF123456\"/>"
                   "</Grid.Background></Grid>",
                   StringTable{});
    coloured_element->Measure(Size{40.0, 30.0});
    coloured_element->Arrange({0.0, 0.0, 40.0, 30.0});
    const DisplayList coloured = Build(*coloured_element, Size{40.0, 30.0});
    CHECK(coloured.refusals.empty());
    CHECK(coloured.rects.size() == 1);
    if (!coloured.rects.empty())
        CHECK(coloured.rects[0].color == (Color{0xff, 0x12, 0x34, 0x56}));
}

void ImageBrushRetainsNullAndSourcedSemantics() {
    std::unique_ptr<Element> empty =
        LoadMarkup("<Grid xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\">"
                   "<Grid.Background><ImageBrush/></Grid.Background></Grid>",
                   StringTable{});
    CHECK(empty->background_brush().declared);
    CHECK(empty->background_brush().kind == BrushKind::Image);
    CHECK(!empty->background_brush().has_image_source);
    CHECK(empty->background_brush().image_source.empty());
    empty->Measure(Size{40.0, 30.0});
    empty->Arrange({0.0, 0.0, 40.0, 30.0});
    const DisplayList no_source = Build(*empty, Size{40.0, 30.0});
    CHECK(no_source.refusals.empty());
    CHECK(no_source.rects.empty());
    CHECK(no_source.scene != nullptr);
    if (no_source.scene) {
        const VisualNode* root = no_source.scene->Find(no_source.scene->root());
        CHECK(root != nullptr);
        CHECK(root && root->content && root->content->commands.empty());
    }

    std::unique_ptr<Element> sourced =
        LoadMarkup("<Grid xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\">"
                   "<Grid.Background><ImageBrush ImageSource=\"Assets/Backdrop.png\"/>"
                   "</Grid.Background></Grid>",
                   StringTable{});
    CHECK(sourced->background_brush().kind == BrushKind::Image);
    CHECK(sourced->background_brush().has_image_source);
    CHECK(sourced->background_brush().image_source == "Assets/Backdrop.png");
    sourced->Measure(Size{40.0, 30.0});
    sourced->Arrange({0.0, 0.0, 40.0, 30.0});
    const DisplayList with_source = Build(*sourced, Size{40.0, 30.0});
    CHECK(with_source.refusals.empty());
    CHECK(with_source.scene != nullptr);
    if (with_source.scene) {
        const VisualNode* root = with_source.scene->Find(with_source.scene->root());
        CHECK(root != nullptr);
        CHECK(root && root->content && root->content->commands.size() == 1);
        if (root && root->content && root->content->commands.size() == 1) {
            const auto* image =
                std::get_if<LocalImageBrushFill>(&root->content->commands[0]);
            CHECK(image != nullptr);
            CHECK(image && image->source == "Assets/Backdrop.png");
        }
    }
}

void LiveImageSourceStateIsRetainedAsANamedDecodeBoundary() {
    auto empty = std::make_unique<Image>();
    empty->set_width(32.0);
    empty->set_height(20.0);
    const DisplayList no_source = LayOut(std::move(empty), Size{32.0, 20.0});
    CHECK(no_source.refusals.empty());

    auto root = std::make_unique<Grid>();
    auto sourced = std::make_unique<Image>();
    Image* retained_image = sourced.get();
    sourced->set_width(32.0);
    sourced->set_height(20.0);
    sourced->set_source(true, "Windows.UI.Xaml.Media.Imaging.BitmapImage");
    sourced->set_stretch(ImageStretch::UniformToFill);
    sourced->set_nine_grid({1.0, 2.0, 3.0, 4.0});
    root->AddChild(std::move(sourced));
    CHECK(retained_image->has_source());
    CHECK(retained_image->source_type() ==
          "Windows.UI.Xaml.Media.Imaging.BitmapImage");
    CHECK(retained_image->stretch() == ImageStretch::UniformToFill);
    CHECK(retained_image->nine_grid() == (Thickness{1.0, 2.0, 3.0, 4.0}));
    const DisplayList retained = LayOut(std::move(root), Size{32.0, 20.0});
    CHECK(HasRefusal(retained, "Source"));
    CHECK(retained.refusals.size() == 1);
    if (!retained.refusals.empty()) {
        CHECK(retained.refusals.front().reason.find("BitmapImage") !=
              std::string::npos);
        CHECK(retained.refusals.front().reason.find("decoding and sampling") !=
              std::string::npos);
    }
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

void RetainedNodeIdsBelongToElementsRatherThanBuildOrder() {
    Border first;
    Border second;
    CHECK(first.render_node_id() != 0);
    CHECK(second.render_node_id() != 0);
    CHECK(first.render_node_id() != second.render_node_id());

    first.Measure({40.0, 30.0});
    first.Arrange({0.0, 0.0, 40.0, 30.0});
    const DisplayList before = Build(first, {40.0, 30.0});
    const DisplayList after = Build(first, {40.0, 30.0});
    CHECK(before.scene != nullptr);
    CHECK(after.scene != nullptr);
    CHECK(before.scene->root() == NodeId{first.render_node_id()});
    CHECK(after.scene->root() == before.scene->root());
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
    const std::string bgra = ToBgra(surface);
    CHECK(bgra.size() == 400u * 300u * 4u);
    CHECK(static_cast<unsigned char>(bgra[0]) == 0x00);  // B
    CHECK(static_cast<unsigned char>(bgra[1]) == 0x00);  // G
    CHECK(static_cast<unsigned char>(bgra[2]) == 0xff);  // R
    CHECK(static_cast<unsigned char>(bgra[3]) == 0xff);  // A

    // Two renders of the same case are the same bytes. The corpus-wide version
    // of this runs in the gate; this catches an obvious regression without it.
    CaseResult again = LayOutCase(
        "{\"id\": \"one\", \"environment\": {\"available_size\": [400, 300]}, "
        "\"markup\": \"<Grid xmlns=\\\"http://schemas.microsoft.com/winfx/2006/xaml/"
        "presentation\\\" Background=\\\"#ff0000\\\"/>\"}");
    Surface second = PaintCase(again, nullptr);
    CHECK(ToPpm(second) == ppm);
    CHECK(ToBgra(second) == bgra);
    CHECK(SidecarJson(again, second, "software") == SidecarJson(result, surface, "software"));

    Surface transparent(1, 1, Color{0, 0, 0, 0});
    const std::string transparent_bgra = ToBgra(transparent);
    CHECK(transparent_bgra.size() == 4);
    CHECK(transparent_bgra == std::string(4, '\0'));
}

void TheRetainedSceneMatchesTheFlatCompatibilityView() {
    auto root = std::make_unique<Grid>();
    root->set_background_brush(
        BrushValue{true, true, Color{0xff, 0x10, 0x20, 0x30}});
    auto child = std::make_unique<Border>();
    child->set_width(20.0);
    child->set_height(10.0);
    child->set_border_thickness(Thickness{1.0, 2.0, 3.0, 1.0});
    child->set_border_brush(
        BrushValue{true, true, Color{0xff, 0xa0, 0xb0, 0xc0}});
    root->AddChild(std::move(child));

    DisplayList list = LayOut(std::move(root), Size{40.0, 30.0});
    CHECK(list.scene != nullptr);
    CHECK(list.scene && list.scene->Validate());
    CHECK(list.scene && list.scene->nodes().size() == list.geometry.size());

    Surface flat(40, 30, BackdropColor());
    for (const RectOp& op : list.rects) flat.FillRect(op.bounds, op.color);

    CaseResult retained;
    retained.list = std::move(list);
    Surface replayed = PaintCase(retained, nullptr);
    CHECK(retained.render_issues.empty());
    CHECK(ToBgra(replayed) == ToBgra(flat));
}

void ExternalSurfaceIsRetainedAsLocalLiveContent() {
    auto lifetime = std::make_shared<int>(7);
    std::weak_ptr<int> weak_lifetime = lifetime;
    ExternalSurfaceReference reference;
    reference.kind = ExternalSurfaceKind::DxgiSwapChain;
    reference.generation = 42;
    reference.native_value = 0x1234;
    reference.lifetime = lifetime;
    auto provider =
        std::make_shared<FixedExternalSurfaceProvider>(std::move(reference));

    auto root = std::make_unique<Grid>();
    root->SetExternalSurfaceProvider(provider);
    int invalidations = 0;
    bool invalidated_layout = true;
    auto sink = std::make_shared<RenderInvalidationSink>([&](bool layout) {
        ++invalidations;
        invalidated_layout = layout;
    });
    CHECK(root->AttachRenderInvalidationSink(sink));
    root->NotifyExternalSurfaceChanged();
    CHECK(invalidations == 1 && !invalidated_layout);
    DisplayList list = LayOut(std::move(root), Size{40.0, 30.0});
    provider.reset();
    lifetime.reset();

    CHECK(list.refusals.empty());
    CHECK(list.scene && list.scene->nodes().size() == 1);
    const VisualNode* visual = list.scene ? list.scene->Find(list.scene->root()) : nullptr;
    CHECK(visual && visual->content && visual->content->commands.size() == 1);
    const auto* external = visual && visual->content
        ? std::get_if<LocalExternalSurface>(&visual->content->commands.front())
        : nullptr;
    CHECK(external != nullptr);
    CHECK(external && external->bounds.width == 40.0 &&
          external->bounds.height == 30.0);
    CHECK(external && external->source.kind == ExternalSurfaceKind::DxgiSwapChain);
    CHECK(external && external->source.generation == 42);
    CHECK(!weak_lifetime.expired());

    list = DisplayList{};
    CHECK(weak_lifetime.expired());
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
    AZeroAreaTextBlockPaintsNothing();
    AFontIconBecomesAnIconFontRun();
    APartlyTransparentBrushUsesPremultipliedSourceOver();
    ABrushWithNoColourIsANamedNoDraw();
    TheProbeInkColourIsReserved();
    OpacityCompositesACompleteNodeLayer();
    AlignedOriginsComposeThroughTheRetainedScene();
    AnUnarrangedElementIsANamedNoDraw();
    AMeasuredRootWithAnExplicitExtentHasTheRecordedRect();
    ARoundedRectangleIsANamedNoDraw();
    ShapeStrokeStateIsRetainedAsANamedRendererBoundary();
    CanvasRectanglesUseExplicitVisualGeometryWithoutPublicLayoutStorage();
    CanvasGeometryCompilesRotateTransformAndRetainsUnsupportedTransforms();
    RectangleGeometryClipIsRetainedLocallyAndRasterizedAfterTranslation();
    UnsupportedClipGeometryRemainsANamedRefusal();
    CanvasZIndexOrdersSiblingsAndSurvivesTheSidecar();
    EqualZIndexPreservesSourceOrder();
    BorderAndPanelChromePropertiesLoadAndPaintGenerically();
    ARoundedCornerIsANamedNoDrawOnlyWhereItWouldBeDrawn();
    ForegroundRetainsItsSolidColorBrushWithoutMeasuringText();
    R8StopsAtTheNamedFontMetricsBoundary();
    ACollapsedElementPaintsNothingAndRefusesNothing();
    TheMarkupCarriesTheColourThroughToThePaint();
    ImageBrushRetainsNullAndSourcedSemantics();
    LiveImageSourceStateIsRetainedAsANamedDecodeBoundary();
    ACaseThatCannotLoadIsNeitherPaintedNorRefused();
    RetainedNodeIdsBelongToElementsRatherThanBuildOrder();
    TheHarnessArrangesWhatTheMeasurementPathArranges();
    TheRetainedSceneMatchesTheFlatCompatibilityView();
    ExternalSurfaceIsRetainedAsLocalLiveContent();

    if (failures) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "render checks passed\n";
    return 0;
}
