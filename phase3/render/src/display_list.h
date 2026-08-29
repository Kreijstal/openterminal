// What an arranged tree paints -- and, just as importantly, what it refuses to.
//
// This is the whole of the render pass that has an opinion. It walks a tree the
// layout core has already measured and arranged, and turns it into retained
// local display lists plus affine visual nodes. It draws nothing itself: a
// backend consumes the immutable scene. The flat rectangles and text below
// are acceptance sidecars only; transformed bounds there are root-space AABBs.
//
// The rule the whole file is built around: **paint only values derived from
// recorded truth.**
//
// Layout retains, per element, a desired size, render size, layout slot and the
// FrameworkElement render origin computed by Arrange. Scene compilation adds
// declared visual transforms around their retained origin without feeding any
// of that state back into layout. Colours likewise come only from markup or a
// theme dictionary. Everything else is a *named no-draw*: an entry in
// `refusals` saying which element and which feature, never an approximation.
//
// The named no-draws are the work list the native acceptance report exposes;
// they are never counted as passes merely because the surface stayed blank.

#ifndef OPENXAML_RENDER_DISPLAY_LIST_H
#define OPENXAML_RENDER_DISPLAY_LIST_H

#include <cstdint>
#include <string>
#include <memory>
#include <vector>

#include "brush.h"
#include "element.h"
#include "layout.h"
#include "scene.h"

namespace openxaml {
namespace render {

// A solid rectangle's root-space axis-aligned bounding box. The authoritative
// geometry remains the LocalFillRect plus its VisualNode transform. `what`
// says which part of the element it is ("background", "border-left", ...).
struct RectOp {
    Rect bounds;
    Color color;
    std::string path;
    std::string what;
};

// A text run at a position this pass owns, in a font it does not.
//
// The origin and the box are ours: they come out of the same arrange the corpus
// verifies, and the advances the measurement path sums are the same advances a
// backend must lay the glyphs on. This display list does not invent glyph
// imagery: the backend hands the string to platform text output, while the
// focused native acceptance test independently checks pixels and glyph runs.
struct TextOp {
    Rect bounds;  // the arranged TextBlock, which is the run's box
    // The CPU scene traversal resolves an axis-aligned retained clip before it
    // delegates glyph imagery. Platform text backends must constrain every
    // touched glyph pixel to this rectangle as well as to `bounds`.
    bool has_clip = false;
    Rect clip;
    std::string text;
    std::string font_family;
    double font_size = 0.0;
    // Where the baseline sits below the top of the box, from the same harvested
    // metrics the line box was measured with. A backend that aligns the glyph
    // cell's top instead lands the baseline wherever *its* notion of ascent
    // says, which for a font whose win metrics exceed its hhea metrics drops
    // the descenders out of the measured box. Zero means no metrics were
    // available and the backend must say so rather than guess.
    double baseline = 0.0;
    // One advance per character, from the measurement path's own shaping, so a
    // backend can lay the glyphs on the widths the corpus verified instead of
    // on whatever its rasteriser rounds them to.
    std::vector<double> advances;
    bool wrap = false;
    bool bold = false;
    std::string language = "en-US";
    std::string path;
};

// A live, producer-owned surface's root-space axis-aligned bounding box, plus
// the identity of the source that was bound to it at compile time.
//
// The authoritative geometry remains the LocalExternalSurface command plus its
// VisualNode transform; this is the acceptance sidecar, and it is what lets a
// frame record say which element a SwapChainPanel's content belongs to. The
// native value is deliberately absent: nothing outside the compositor may
// dereference it, and the kind plus generation are enough to say which source
// a frame was built from and when it last changed.
struct ExternalSurfaceOp {
    Rect bounds;
    std::string path;
    ExternalSurfaceKind kind = ExternalSurfaceKind::None;
    std::uint64_t generation = 0;
};

// A feature that was found and deliberately not drawn.
struct Refusal {
    std::string path;
    std::string feature;
    std::string reason;
};

// One node's verified geometry, carried out of the pass so that a checker can
// compare painted pixels against it without re-running layout.
//
// `slot` and `actual` are the layout measurements. `abs` is the accumulated
// retained render origin and is checked against the native transform-to-root
// basis points rather than inferred from the slot.
//
// `origin` is the step between the two: the parent-local visual origin the
// retained scene translates this node by. It is *not* the slot origin, because
// FrameworkElement::ArrangeCore positions the arranged ink inside the
// margin-reduced slot according to the alignments -- a 120-wide Border in a
// 400-wide Stretch slot sits at x=140 while its slot origin stays 0. The
// alignment inputs travel beside it (`margin`, `horizontal_alignment`,
// `vertical_alignment`, `layout_rounding`, `dpi_scale_*`) so that a checker can
// re-derive `origin` from the declared markup and the measured slot instead of
// believing the number the render pass wrote.
struct NodeGeometry {
    NodeId id;
    std::string path;
    std::string type;
    Rect slot;        // in the parent's coordinates
    Size actual;      // the render size
    Point origin;     // the parent-local visual origin the scene translates by
    double abs_x = 0.0;
    double abs_y = 0.0;
    Matrix3x2 transform_to_root = Matrix3x2::Identity();
    double opacity = 1.0;
    Clip clip = Clip::None();
    Thickness margin;
    HorizontalAlignment horizontal_alignment = HorizontalAlignment::Stretch;
    VerticalAlignment vertical_alignment = VerticalAlignment::Stretch;
    bool layout_rounding = false;
    double dpi_scale_x = 1.0;
    double dpi_scale_y = 1.0;
    std::int32_t z_index = 0;
    bool has_layout_storage = false;
    bool visible = true;
};

struct DisplayList {
    Size surface;
    std::vector<RectOp> rects;
    std::vector<TextOp> texts;
    std::vector<ExternalSurfaceOp> externals;
    std::vector<Refusal> refusals;
    std::vector<NodeGeometry> geometry;

    // The retained, local-coordinate scene is the authoritative paint input.
    // The flat vectors above remain temporarily as an acceptance-sidecar view:
    // they let the existing oracle prove that migrating to retained nodes did
    // not change the already-supported pixels or public geometry.
    std::shared_ptr<const SceneSnapshot> scene;

    bool painted() const { return refusals.empty(); }
};

// Walks an arranged tree. `root` must already have been measured and arranged;
// nothing here calls either.
//
// The walk is the *recorded* one -- Element::RecordedChildren -- for the same
// reason measure_cases uses it: those are the nodes the oracle was asked about,
// so those are the nodes whose geometry is verified. A node the probe never
// reached has no recorded rect, and painting it would be painting something
// unverified.
DisplayList Build(const Element& root, Size surface);

// The colour reserved for text ink in the offscreen dumps.
//
// The gate on text is containment and origin, not colour: no oracle records
// what colour the runtime paints a glyph, and the corpus's TextBlocks mostly
// set no Foreground at all. So the dumps paint every run in one reserved
// colour, declared here as a probe marker rather than as a claim -- a checker
// can then separate ink from background without knowing anything about glyphs.
// A case whose own markup paints this exact colour is refused by name rather
// than silently confusing the two.
inline Color ProbeInkColor() { return Color{0xff, 0xff, 0x00, 0xff}; }

}  // namespace render
}  // namespace openxaml

#endif  // OPENXAML_RENDER_DISPLAY_LIST_H
