#include "external_surface_reader.h"

namespace openxaml {
namespace render {
namespace {

const char* KindName(ExternalSurfaceKind kind) {
    switch (kind) {
        case ExternalSurfaceKind::None: return "none";
        case ExternalSurfaceKind::CompositionSurfaceHandle:
            return "a composition surface handle";
        case ExternalSurfaceKind::DxgiSwapChain: return "a DXGI swap chain";
        case ExternalSurfaceKind::CpuBgraImage: return "a CPU BGRA image";
    }
    return "an unknown external surface kind";
}

}  // namespace

bool CpuExternalSurfaceReader::ReadExternalSurface(
    const ExternalSurfaceReference& source, ExternalSurfaceView& view,
    std::string& message) {
    view = ExternalSurfaceView{};
    if (!source) {
        message = "the external surface reference names no live source";
        return false;
    }
    if (source.kind != ExternalSurfaceKind::CpuBgraImage) {
        message = std::string("this reader imports CPU BGRA images only; the bound "
                              "source is ") + KindName(source.kind) +
                  ", which needs a graphics device to import";
        return false;
    }

    const auto* image = reinterpret_cast<const CpuExternalImage*>(source.native_value);
    if (!image) {
        message = "the CPU BGRA image reference has no image";
        return false;
    }
    if (image->width <= 0 || image->height <= 0) {
        message = "the CPU BGRA image has no positive extent";
        return false;
    }
    if (!image->pixels) {
        message = "the CPU BGRA image has no pixels";
        return false;
    }
    if (image->stride_pixels < static_cast<std::size_t>(image->width)) {
        message = "the CPU BGRA image stride is narrower than its width";
        return false;
    }

    view.width = image->width;
    view.height = image->height;
    view.stride_pixels = image->stride_pixels;
    view.pixels = image->pixels;
    return true;
}

}  // namespace render
}  // namespace openxaml
