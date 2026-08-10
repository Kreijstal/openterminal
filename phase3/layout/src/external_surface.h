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
