// Platform-neutral retained identity for content produced outside XAML.
//
// Native ownership stays behind ExternalSurfaceProvider. A scene snapshot
// receives only an opaque value plus a shared lifetime token, which is enough
// for a future platform compositor to import the resource without teaching
// layout or the CPU renderer about HANDLE, IUnknown, DXGI or DirectComposition.

#ifndef OPENXAML_EXTERNAL_SURFACE_H
#define OPENXAML_EXTERNAL_SURFACE_H

#include <cstdint>
#include <memory>

namespace openxaml {

enum class ExternalSurfaceKind {
    None,
    CompositionSurfaceHandle,
    DxgiSwapChain,
    // A producer-owned, already premultiplied BGRA image resident in this
    // process. `native_value` points at a render::CpuExternalImage, declared
    // in phase3/render/src/external_surface_reader.h, and `lifetime` owns it.
    //
    // This is the one source kind a CPU compositor can read without a
    // graphics device, and it is the shape a DXGI readback produces once a
    // presented back buffer has been mapped. It is not a substitute for a
    // swap chain: a platform compositor still imports those directly, and a
    // producer that has only a swap chain must not describe it as this.
    CpuBgraImage,
};

struct ExternalSurfaceReference {
    ExternalSurfaceKind kind = ExternalSurfaceKind::None;
    std::uint64_t generation = 0;
    std::uintptr_t native_value = 0;
    std::shared_ptr<const void> lifetime;

    explicit operator bool() const noexcept {
        return kind != ExternalSurfaceKind::None && native_value != 0 &&
               lifetime != nullptr;
    }
};

class ExternalSurfaceProvider {
public:
    virtual ~ExternalSurfaceProvider() = default;
    virtual ExternalSurfaceReference CaptureExternalSurface() const noexcept = 0;
};

}  // namespace openxaml

#endif  // OPENXAML_EXTERNAL_SURFACE_H
