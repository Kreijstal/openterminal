// Deterministic reference implementation of the retained-scene raster
// boundary. Axis-aligned translation retains an exact rectangle fast path;
// general affine solid fills and rectangular clips use deterministic convex
// polygon area coverage. Text remains a separately negotiated raster service.

#ifndef OPENXAML_RENDER_CPU_RASTER_BACKEND_H
#define OPENXAML_RENDER_CPU_RASTER_BACKEND_H

#include "raster_backend.h"

namespace openxaml {
namespace render {

class CpuRasterBackend final : public RasterBackend {
public:
    RasterResult Render(const SceneSnapshot& scene, Color clear,
                        TextRasterizer* text_rasterizer = nullptr) const override;
};

}  // namespace render
}  // namespace openxaml

#endif  // OPENXAML_RENDER_CPU_RASTER_BACKEND_H
