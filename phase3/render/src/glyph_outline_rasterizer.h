// Painting text from recorded glyph outlines.
//
// This is the consumption side of phase3/xaml-db/glyph-outlines/README.md: a
// sibling of DrawDirectWriteTextRun that needs no platform, because the shapes
// were recorded off the platform once and the positions were always the
// layout's. Placement reproduces the DirectWrite painter's single-line
// contract verbatim -- the pen starts at (bounds.x, bounds.y + baseline) and
// codepoint i moves it by advances[i], the display list's numbers, never the
// font's. The recorded advance is a harvest-time cross-check and is not read
// here at all.
//
// Rasterisation is deterministic grayscale coverage: cubics flatten at a fixed
// depth, all contours of a glyph fill in one pass under the recorded fill
// rule, and every pixel goes through Surface::BlendPixel. This is not
// ClearType and will not agree pixel-for-pixel with the DirectWrite painter;
// a run painted here is labelled "recorded-outlines" in the sidecar so that a
// comparison against native ink is never made silently.

#ifndef OPENXAML_RENDER_GLYPH_OUTLINE_RASTERIZER_H
#define OPENXAML_RENDER_GLYPH_OUTLINE_RASTERIZER_H

#include <string>
#include <vector>

#include "case_runner.h"
#include "display_list.h"
#include "glyph_outlines.h"
#include "surface.h"

namespace openxaml {
namespace render {

// Every recorded cubic becomes exactly this many chords. Fixed, not adaptive:
// an adaptive tolerance in device space would make the output depend on the
// size a run happened to be painted at, and determinism is worth more here
// than a few saved segments. 16 chords keep the worst flattening error of a
// full-em Cascadia cubic under a hundredth of a pixel at corpus sizes.
inline constexpr int kCubicChords = 16;

// Vertical coverage samples per pixel row. Horizontal coverage is exact (span
// arithmetic), so this is the only sampling in the filler; 16 sub-scanlines
// put the vertical quantisation at the same 1/16-pixel scale.
inline constexpr int kCoverageRowSamples = 16;

// A device-space contour, implicitly closed from back() to front().
using OutlinePolygon = std::vector<Point>;

// Appends kCubicChords points approximating the cubic from `from` through the
// two control points to `to`; the final appended point is exactly `to`.
void FlattenCubic(Point from, Point control1, Point control2, Point to,
                  OutlinePolygon& out);

// Deterministic scanline coverage fill of one glyph's contours, honouring the
// fill rule, blended through Surface::BlendPixel and restricted to `clip`
// (which the caller has already intersected with the surface).
void FillContours(Surface& surface, const std::vector<OutlinePolygon>& contours,
                  OutlineFillMode fill_mode, Color ink, const PixelRect& clip);

// The recorded-outline painter. False is a refusal named in `diagnostic`,
// prefixed with the run's path; the surface is untouched on refusal. A family
// the library has not got is a refusal too -- the caller decides what to try
// next -- and a codepoint gap inside a covered family refuses the whole run,
// never paints a .notdef box.
bool DrawRecordedOutlineTextRun(const GlyphOutlineLibrary& library, Surface& surface,
                                const TextOp& run, Color ink, std::string& diagnostic);

// The same painter over the process-wide default library; the signature
// DrawDirectWriteTextRun has, so the two are interchangeable per run.
bool DrawRecordedOutlineTextRun(Surface& surface, const TextOp& run, Color ink,
                                std::string& diagnostic);

// The TextBackend the Linux corpus harness installs when recorded outlines
// were loaded. It covers exactly the families the library carries, so a case
// in any other family keeps its honest missing-text-rasterizer refusal.
class RecordedOutlineTextBackend final : public TextBackend {
public:
    explicit RecordedOutlineTextBackend(const GlyphOutlineLibrary& library)
        : library_(library) {}

    bool CoversFamily(const std::string& family) const override {
        return library_.Resolve(family) != nullptr;
    }

    void DrawRuns(Surface& surface, const std::vector<TextOp>& runs, Color ink,
                  std::vector<std::string>& failures,
                  std::vector<std::string>* painters = nullptr) override;

    std::string name() const override { return "recorded-outlines"; }

private:
    const GlyphOutlineLibrary& library_;
};

}  // namespace render
}  // namespace openxaml

#endif  // OPENXAML_RENDER_GLYPH_OUTLINE_RASTERIZER_H
