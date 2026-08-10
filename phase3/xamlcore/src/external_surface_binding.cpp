#include "external_surface_binding.h"

#include <new>
#include <utility>

namespace openxaml {

class ExternalSurfaceResource final {
public:
    static ExternalSurfaceResource FromHandle(HANDLE handle) noexcept {
        return ExternalSurfaceResource(handle);
    }

    static ExternalSurfaceResource FromSwapChain(IUnknown* swap_chain) noexcept {
        return ExternalSurfaceResource(swap_chain);
    }

    ~ExternalSurfaceResource() {
        if (kind_ == ExternalSurfaceKind::CompositionSurfaceHandle && handle_) {
            CloseHandle(handle_);
        } else if (kind_ == ExternalSurfaceKind::DxgiSwapChain && swap_chain_) {
            swap_chain_->Release();
        }
    }

    ExternalSurfaceResource(const ExternalSurfaceResource&) = delete;
    ExternalSurfaceResource& operator=(const ExternalSurfaceResource&) = delete;

    ExternalSurfaceResource(ExternalSurfaceResource&& other) noexcept
        : kind_(other.kind_), handle_(other.handle_), swap_chain_(other.swap_chain_) {
        other.kind_ = ExternalSurfaceKind::None;
        other.handle_ = nullptr;
        other.swap_chain_ = nullptr;
    }

    ExternalSurfaceKind kind() const noexcept { return kind_; }
    HANDLE handle() const noexcept { return handle_; }
    IUnknown* swap_chain() const noexcept { return swap_chain_; }

private:
    explicit ExternalSurfaceResource(HANDLE handle) noexcept
        : kind_(ExternalSurfaceKind::CompositionSurfaceHandle), handle_(handle) {}

    explicit ExternalSurfaceResource(IUnknown* swap_chain) noexcept
        : kind_(ExternalSurfaceKind::DxgiSwapChain), swap_chain_(swap_chain) {
        swap_chain_->AddRef();
    }

    ExternalSurfaceKind kind_ = ExternalSurfaceKind::None;
    HANDLE handle_ = nullptr;
    IUnknown* swap_chain_ = nullptr;
};

ExternalSurfaceSnapshot::ExternalSurfaceSnapshot(
    std::uint64_t generation,
    std::shared_ptr<const ExternalSurfaceResource> resource) noexcept
    : generation_(generation), resource_(std::move(resource)) {}

ExternalSurfaceKind ExternalSurfaceSnapshot::kind() const noexcept {
    return resource_ ? resource_->kind() : ExternalSurfaceKind::None;
}

HANDLE ExternalSurfaceSnapshot::composition_surface_handle() const noexcept {
    return kind() == ExternalSurfaceKind::CompositionSurfaceHandle ? resource_->handle() : nullptr;
}

IUnknown* ExternalSurfaceSnapshot::dxgi_swap_chain() const noexcept {
    return kind() == ExternalSurfaceKind::DxgiSwapChain ? resource_->swap_chain() : nullptr;
}

HRESULT ExternalSurfaceBinding::SetCompositionSurfaceHandle(HANDLE source_handle) noexcept {
    if (!source_handle) {
        Clear();
        return S_OK;
    }

    HANDLE duplicate = nullptr;
    const HANDLE process = GetCurrentProcess();
    if (!DuplicateHandle(process, source_handle, process, &duplicate, 0, FALSE,
                         DUPLICATE_SAME_ACCESS)) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    try {
        auto resource = std::make_shared<ExternalSurfaceResource>(
            ExternalSurfaceResource::FromHandle(duplicate));
        Commit(std::move(resource));
    } catch (const std::bad_alloc&) {
        // The move-only temporary owns duplicate before make_shared enters;
        // its destructor closes the handle if allocation/construction fails.
        return E_OUTOFMEMORY;
    } catch (...) {
        return E_FAIL;
    }
    return S_OK;
}

HRESULT ExternalSurfaceBinding::SetDxgiSwapChain(IUnknown* swap_chain) noexcept {
    if (!swap_chain) {
        Clear();
        return S_OK;
    }

    try {
        auto resource = std::make_shared<ExternalSurfaceResource>(
            ExternalSurfaceResource::FromSwapChain(swap_chain));
        Commit(std::move(resource));
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (...) {
        return E_FAIL;
    }
    return S_OK;
}

void ExternalSurfaceBinding::Clear() noexcept {
    Commit(nullptr);
}

ExternalSurfaceSnapshot ExternalSurfaceBinding::Snapshot() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return ExternalSurfaceSnapshot(generation_, resource_);
}

ExternalSurfaceReference
ExternalSurfaceBinding::CaptureExternalSurface() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    ExternalSurfaceReference reference;
    reference.generation = generation_;
    if (!resource_) return reference;
    reference.kind = resource_->kind();
    if (reference.kind == ExternalSurfaceKind::CompositionSurfaceHandle) {
        reference.native_value = reinterpret_cast<std::uintptr_t>(resource_->handle());
    } else if (reference.kind == ExternalSurfaceKind::DxgiSwapChain) {
        reference.native_value = reinterpret_cast<std::uintptr_t>(resource_->swap_chain());
    }
    reference.lifetime = std::static_pointer_cast<const void>(resource_);
    return reference;
}

void ExternalSurfaceBinding::Commit(
    std::shared_ptr<const ExternalSurfaceResource> resource) noexcept {
    std::shared_ptr<const ExternalSurfaceResource> previous;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++generation_;
        previous = std::move(resource_);
        resource_ = std::move(resource);
    }
    // Releasing a swap chain is an arbitrary COM call and may re-enter this
    // binding. Keep the state change atomic, but never run external teardown
    // while holding the binding mutex.
    previous.reset();
}

} // namespace openxaml
