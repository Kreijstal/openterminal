// One corpus case, laid out and painted.
//
// Shared by the native harness and the GDI one under Wine, so that both arrange
// the same tree, build the same display list and snap the same rectangles. The
// only thing that differs between them is the text backend: natively there is
// none, because rasterising a glyph is not something this project has any
// authority to do; under Wine it is GDI's ExtTextOut with the real font
// selected.

#ifndef OPENXAML_RENDER_CASE_RUNNER_H
#define OPENXAML_RENDER_CASE_RUNNER_H

#include <map>
#include <string>
#include <vector>

#include "display_list.h"
#include "raster_backend.h"
#include "surface.h"

namespace openxaml {
namespace render {

// Draws the text runs of a display list. The positions are the display list's;
// the glyphs are the backend's.
class TextBackend {
public:
    virtual ~TextBackend() = default;
    // Draws every run in `ink`. `failures` collects a message per run that
    // could not be drawn -- a font the backend has not got, most likely -- so
    // that the case can be refused by name instead of silently missing its
    // text.
    //
    // `painters`, when supplied, receives one entry per run naming which
    // painter drew it ("recorded-outlines", "directwrite-cleartype"), or the
    // empty string for a run that failed. The name travels into the sidecar so
    // that ink whose provenance matters -- grayscale outline coverage is not
    // ClearType -- is never compared against native pixels silently.
    virtual void DrawRuns(Surface& surface, const std::vector<TextOp>& runs, Color ink,
                          std::vector<std::string>& failures,
                          std::vector<std::string>* painters = nullptr) = 0;
    // Whether this backend has a glyph source for the family at all. False is
    // how the CPU traversal keeps the honest missing-text-rasterizer refusal
    // for a family nothing recorded, instead of reporting a draw failure from
    // a backend that never had a chance.
    virtual bool CoversFamily(const std::string& family) const {
        (void)family;
        return true;
    }
    virtual std::string name() const = 0;
};

struct CaseResult {
    std::string id;
    // Set when the markup could not be loaded or laid out at all. Such a case
    // has no tree to paint and is neither painted nor refused: it is exactly
    // what the measurement path already reports, and the round-trip gate skips
    // it for the same reason check_layout does.
    std::string load_error;
    DisplayList list;
    // The measure_cases-shaped tree, so that a run of this harness can be
    // diffed byte-for-byte against the measurement path's own output.
    std::string tree_json;
    std::vector<std::string> text_failures;
    // Backend failures are structured and never inferred from absent pixels.
    // Refusals above describe scene construction; these describe replay.
    std::vector<RenderIssue> render_issues;
    // Which painter drew each run, keyed by the run's element path. A run that
    // was refused, or never reached a backend at all, has no entry; the
    // sidecar spells that as null.
    std::map<std::string, std::string> run_painters;
    bool has_surface = false;
};

// Lays out one case's markup and builds its display list. Does not paint.
CaseResult LayOutCase(const std::string& case_json);

// Replays the retained scene through the platform-neutral raster boundary.
// This is shared by offscreen cases and platform presenters so no host can
// accidentally keep painting the deprecated flat compatibility view.
//
// `external_reader` imports producer-owned content -- a SwapChainPanel's swap
// chain or composition surface. The corpus harnesses pass none, because a
// corpus case has no producer; a live island passes the one its host bound.
// `run_painters`, when supplied, collects which painter drew each successful
// run, keyed by the run's element path; see TextBackend::DrawRuns.
Surface RasterizeDisplayList(const DisplayList& list, TextBackend* backend, Color clear,
                             Color text_ink, std::vector<std::string>& text_failures,
                             std::vector<RenderIssue>& render_issues,
                             ExternalSurfaceReader* external_reader = nullptr,
                             std::map<std::string, std::string>* run_painters = nullptr);

// Paints a laid-out case. `backend` may be null, in which case the text runs
// are recorded in the dump's sidecar and no ink is drawn. The ordinary
// round-trip uses its reserved backdrop; the native acceptance harness passes
// transparent so its BGRA surface has the same initial state as the oracle.
Surface PaintCase(CaseResult& result, TextBackend* backend,
                  Color clear = BackdropColor());

// The shape of every sidecar this project writes, including the short one a
// case that did not load gets. One constant, because a checker refuses a
// version it does not know and a load-error sidecar left behind at 1 would
// refuse a dump root that is in fact current.
//
//   1  the original: slot, actual, abs, clip, z-index.
//   2  adds the parent-local visual origin and the margin, alignments and
//      rounding a checker re-derives it from.
//   3  adds a per-run "painter" naming which glyph painter drew each text run,
//      or null for a run that refused or never reached a backend. Grayscale
//      recorded-outline coverage is not ClearType, and a comparison against
//      native ink must be able to see the difference by name.
//
// Kept in step with check_render.REQUIRED_SIDECAR_SCHEMA and with
// build_render.SIDECAR_SCHEMA, which records it in the dump root's provenance.
inline constexpr int kSidecarSchemaVersion = 3;

// The sidecar a checker reads: the verified geometry, the rectangles that were
// painted, the text runs, and every named no-draw.
std::string SidecarJson(const CaseResult& result, const Surface& surface,
                        const std::string& backend_name,
                        Color clear = BackdropColor());

}  // namespace render
}  // namespace openxaml

#endif  // OPENXAML_RENDER_CASE_RUNNER_H
