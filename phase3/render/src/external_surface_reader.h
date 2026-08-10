// The one external-surface source a CPU backend can import by itself.
//
// ExternalSurfaceReader (raster_backend.h) is the seam; this is its first
// implementation. It reads ExternalSurfaceKind::CpuBgraImage -- a producer's
// premultiplied BGRA image already resident in this process -- and refuses
// every other kind by name, because importing a DXGI swap chain or a
// composition surface handle needs a graphics device this file has not got.
//
// Where the real swap chain joins: a DXGI reader implements the same
// interface, takes ExternalSurfaceKind::DxgiSwapChain, calls GetBuffer on the
// presented back buffer, copies it to a staging texture, maps it, and returns
// that mapping as an ExternalSurfaceView. Everything downstream of the view --
// placement at the arranged rect, the retained clip, paint order, the frame
// record -- is already written and does not change. Nothing else in the
// compositing path needs to know which reader produced the pixels.

#ifndef OPENXAML_RENDER_EXTERNAL_SURFACE_READER_H
#define OPENXAML_RENDER_EXTERNAL_SURFACE_READER_H

#include <cstddef>
#include <cstdint>
#include <string>

#include "raster_backend.h"

namespace openxaml {
namespace render {

// What an ExternalSurfaceKind::CpuBgraImage reference's native_value points
// at. The producer owns the storage and keeps it alive through the
// reference's lifetime token; nothing here copies or frees it.
struct CpuExternalImage {
    int width = 0;
    int height = 0;
    // Elements, not bytes, per row. Must be at least `width`.
    std::size_t stride_pixels = 0;
    // Premultiplied 0xAARRGGBB, top row first -- the same order Surface holds
    // and the same order a bottom-up Windows DIB holds once flipped.
    const std::uint32_t* pixels = nullptr;
};

class CpuExternalSurfaceReader final : public ExternalSurfaceReader {
public:
    bool ReadExternalSurface(const ExternalSurfaceReference& source,
                             ExternalSurfaceView& view,
                             std::string& message) override;
};

}  // namespace render
}  // namespace openxaml

#endif  // OPENXAML_RENDER_EXTERNAL_SURFACE_READER_H
