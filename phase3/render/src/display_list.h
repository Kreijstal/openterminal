// What an arranged tree paints -- and, just as importantly, what it refuses to.
//
// This is the whole of the render pass that has an opinion. It walks a tree the
// layout core has already measured and arranged, and turns it into a flat list
// of axis-aligned solid rectangles and positioned text runs. It draws nothing
// itself: a backend (software here, GDI under Wine) consumes the list.
//
// The rule the whole file is built around: **paint only values derived from
// recorded truth.**
//
// The corpus records, per element, a desired size, a render size and a layout
// slot -- and nothing else. So a rectangle's geometry is derivable (slot origin
// plus render size, accumulated down the tree) and a rectangle's colour is
// derivable when the markup or the theme dictionary spelled one. Everything
// else is a *named no-draw*: an entry in `refusals` saying which element and
// which feature, never an approximation. There is no recorded pixel truth yet
// -- rendered-output probes are wave 6 in phase3/ROADMAP.md -- so a plausible
// gradient or a plausible rounded corner would be exactly the "wrong number"
// the project's standing rules forbid, with no measurement able to catch it.
//
// The named no-draws are not a to-do list for this file. They are the work list
// for the next oracle cycle: each one names a capability a rendered-output probe
// would have to record before anything here could honestly paint it.

#ifndef OPENXAML_RENDER_DISPLAY_LIST_H
#define OPENXAML_RENDER_DISPLAY_LIST_H

#include <string>
#include <vector>

#include "brush.h"
#include "element.h"
#include "layout.h"

namespace openxaml {
namespace render {

// A solid, axis-aligned rectangle in root coordinates. `what` says which part
// of the element it is ("background", "border-left", ...), so a dump can be
// read without re-deriving it.
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
// backend must lay the glyphs on. What a glyph *looks like* has no oracle at
// all, so the backend hands the string to the platform's own text output with
// the real font selected and the imagery is the platform's. See the README.
struct TextOp {
    Rect bounds;  // the arranged TextBlock, which is the run's box
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
    std::string path;
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
// The columns are exactly the ones the measurement path reports -- `slot` is
// what the recorded `offset` column holds and `actual` is the recorded `actual`
// column -- plus the absolute origin this pass accumulated from them. A checker
// re-adds the chain itself and refuses to take the absolute on trust.
struct NodeGeometry {
    std::string path;
    std::string type;
    Rect slot;        // in the parent's coordinates
    Size actual;      // the render size
    double abs_x = 0.0;
    double abs_y = 0.0;
    bool has_layout_storage = false;
    bool visible = true;
};

struct DisplayList {
    Size surface;
    std::vector<RectOp> rects;
    std::vector<TextOp> texts;
    std::vector<Refusal> refusals;
    std::vector<NodeGeometry> geometry;

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
