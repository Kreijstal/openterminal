// The platform-neutral boundary between a retained scene and pixels.
//
// A backend must either render a scene semantic exactly or return a named
// issue for it.  In particular, callers must never have to infer that missing
// text, opacity or transform support was the reason pixels were absent.

#ifndef OPENXAML_RENDER_RASTER_BACKEND_H
#define OPENXAML_RENDER_RASTER_BACKEND_H

#include <cstddef>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "scene.h"
#include "surface.h"

namespace openxaml {
namespace render {

enum class RenderIssueCode {
    InvalidScene,
    UnsupportedTransform,
    UnsupportedClip,
    UnsupportedOpacity,
    UnsupportedBrushAlpha,
    UnsupportedImageBrush,
    UnsupportedExternalSurface,
    MissingTextRasterizer,
    TextRasterizerFailure,
};

struct RenderIssue {
    static constexpr std::size_t kNoCommand = std::numeric_limits<std::size_t>::max();

    RenderIssueCode code = RenderIssueCode::InvalidScene;
    NodeId node{};
    std::size_t command_index = kNoCommand;
    std::string message;
};

// A fully resolved request to paint one text command.  `text` remains in the
// node's local coordinates so the rasterizer retains all shaped data, while
// `bounds` and `clip` are in surface coordinates.  The CPU backend supports
// translation only, so no hidden affine transform remains for the callback to
// guess at.
struct TextRasterRequest {
    NodeId node{};
    std::size_t command_index = 0;
    const LocalText& text;
    Rect bounds{};
    bool has_clip = false;
    Rect clip{};
};

class TextRasterizer {
public:
    virtual ~TextRasterizer() = default;

    // On failure, return false and describe the failure in `message`.  A
    // rasterizer should leave the surface unchanged when it returns false.
    virtual bool DrawText(const TextRasterRequest& request, Surface& surface,
                          std::string& message) = 0;
};

struct RasterResult {
    Surface surface;
    std::vector<RenderIssue> issues;

    explicit RasterResult(Surface rendered_surface) : surface(std::move(rendered_surface)) {}

    bool complete() const { return issues.empty(); }
};

class RasterBackend {
public:
    virtual ~RasterBackend() = default;

    // `clear` is explicit because transparent acceptance surfaces and the
    // diagnostic backdrop are both legitimate clients of the same renderer.
    virtual RasterResult Render(const SceneSnapshot& scene, Color clear,
                                TextRasterizer* text_rasterizer = nullptr) const = 0;
};

}  // namespace render
}  // namespace openxaml

#endif  // OPENXAML_RENDER_RASTER_BACKEND_H
