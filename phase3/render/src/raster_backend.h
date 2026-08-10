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

// A CPU-readable view of one imported external surface: premultiplied BGRA8
// as 0xAARRGGBB, top row first, `stride_pixels` elements per row. The view is
// borrowed and only has to stay valid until the Render call that asked for it
// returns, so a reader may hand back a mapped resource it unmaps afterwards.
struct ExternalSurfaceView {
    int width = 0;
    int height = 0;
    std::size_t stride_pixels = 0;
    const std::uint32_t* pixels = nullptr;

    bool readable() const {
        return pixels != nullptr && width > 0 && height > 0 &&
               stride_pixels >= static_cast<std::size_t>(width);
    }
};

// The seam producer-owned content arrives through.
//
// Everything about *where* an external surface goes -- its arranged rect, the
// retained clip above it, its place in paint order -- belongs to the scene and
// is resolved by the backend. This interface answers only *what pixels it is*,
// which is the one question a CPU backend cannot answer for itself: importing
// a DXGI swap chain or a composition surface handle needs a graphics device.
//
// A DXGI reader is the real implementation and plugs in here: it maps the
// presented back buffer and returns that view, and it is the only piece of
// this path that needs Present to work. Until it exists, a reader for
// ExternalSurfaceKind::CpuBgraImage drives the same compositing code.
//
// A reader must return false and name the reason. Fabricating pixels for a
// source it could not import would make an absent frame indistinguishable
// from a rendered one.
class ExternalSurfaceReader {
public:
    virtual ~ExternalSurfaceReader() = default;

    virtual bool ReadExternalSurface(const ExternalSurfaceReference& source,
                                     ExternalSurfaceView& view,
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
    //
    // A null `external_reader` is not "no external surfaces": it means this
    // frame has no way to import one, and every external-surface command in
    // the scene is reported as UnsupportedExternalSurface rather than skipped.
    virtual RasterResult Render(const SceneSnapshot& scene, Color clear,
                                TextRasterizer* text_rasterizer = nullptr,
                                ExternalSurfaceReader* external_reader = nullptr) const = 0;
};

}  // namespace render
}  // namespace openxaml

#endif  // OPENXAML_RENDER_RASTER_BACKEND_H
