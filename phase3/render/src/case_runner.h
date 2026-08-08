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

#include <string>
#include <vector>

#include "display_list.h"
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
    virtual void DrawRuns(Surface& surface, const std::vector<TextOp>& runs, Color ink,
                          std::vector<std::string>& failures) = 0;
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
    bool has_surface = false;
};

// Lays out one case's markup and builds its display list. Does not paint.
CaseResult LayOutCase(const std::string& case_json);

// Paints a laid-out case. `backend` may be null, in which case the text runs
// are recorded in the dump's sidecar and no ink is drawn.
Surface PaintCase(CaseResult& result, TextBackend* backend);

// The sidecar a checker reads: the verified geometry, the rectangles that were
// painted, the text runs, and every named no-draw.
std::string SidecarJson(const CaseResult& result, const Surface& surface,
                        const std::string& backend_name);

}  // namespace render
}  // namespace openxaml

#endif  // OPENXAML_RENDER_CASE_RUNNER_H
