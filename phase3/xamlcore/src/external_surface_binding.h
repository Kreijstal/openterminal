#pragma once

#include <windows.h>

#include <cstdint>
#include <memory>
#include <mutex>

#include "external_surface.h"

namespace openxaml {

// A SwapChainPanel source is live: presenting to the producer's swap chain
// updates the composition surface without rebuilding the XAML scene.  This
// class owns only the source lifetime and replacement generation.  Geometry,
// clipping, opacity and z-order belong to the retained visual that references
// the snapshot; they must not be baked into or inferred by this class.
class ExternalSurfaceResource;

class ExternalSurfaceSnapshot final {
public:
    ExternalSurfaceSnapshot() noexcept = default;

    ExternalSurfaceKind kind() const noexcept;
    std::uint64_t generation() const noexcept { return generation_; }
    explicit operator bool() const noexcept { return resource_ != nullptr; }

    // Borrowed values remain valid for the lifetime of this snapshot.  A
    // consumer that retains either value beyond it must duplicate/AddRef it.
    HANDLE composition_surface_handle() const noexcept;
    IUnknown* dxgi_swap_chain() const noexcept;

private:
    friend class ExternalSurfaceBinding;
    ExternalSurfaceSnapshot(std::uint64_t generation,
                            std::shared_ptr<const ExternalSurfaceResource> resource) noexcept;

    std::uint64_t generation_ = 0;
    std::shared_ptr<const ExternalSurfaceResource> resource_;
};

// Thread-safe, transactional ownership for ISwapChainPanelNative source
// changes.  Failed replacements preserve the previously committed source and
// generation.  Successful Set/Clear operations advance generation even when
// the caller supplies the same underlying object again, making every accepted
// ABI mutation observable to a retained compositor.
class ExternalSurfaceBinding final : public ExternalSurfaceProvider {
public:
    ExternalSurfaceBinding() noexcept = default;
    ~ExternalSurfaceBinding() = default;

    ExternalSurfaceBinding(const ExternalSurfaceBinding&) = delete;
    ExternalSurfaceBinding& operator=(const ExternalSurfaceBinding&) = delete;
    ExternalSurfaceBinding(ExternalSurfaceBinding&&) = delete;
    ExternalSurfaceBinding& operator=(ExternalSurfaceBinding&&) = delete;

    // Duplicates source_handle into this process.  The caller continues to own
    // source_handle and may close it immediately after this call succeeds.
    // Passing nullptr clears the source.
    HRESULT SetCompositionSurfaceHandle(HANDLE source_handle) noexcept;

    // AddRefs swap_chain.  Passing nullptr clears the source.
    HRESULT SetDxgiSwapChain(IUnknown* swap_chain) noexcept;

    void Clear() noexcept;
    ExternalSurfaceSnapshot Snapshot() const noexcept;
    ExternalSurfaceReference CaptureExternalSurface() const noexcept override;

private:
    void Commit(std::shared_ptr<const ExternalSurfaceResource> resource) noexcept;

    mutable std::mutex mutex_;
    std::uint64_t generation_ = 0;
    std::shared_ptr<const ExternalSurfaceResource> resource_;
};

} // namespace openxaml
