#include "external_surface_binding.h"

#include <atomic>
#include <cstdio>

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

class CountingUnknown final : public IUnknown {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (IsEqualIID(iid, IID_IUnknown)) {
            *value = static_cast<IUnknown*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override { return --references_; }
    ULONG references() const noexcept { return references_.load(); }

private:
    std::atomic<ULONG> references_{1};
};

class ReentrantReleaseUnknown final : public IUnknown {
public:
    explicit ReentrantReleaseUnknown(openxaml::ExternalSurfaceBinding& binding)
        : binding_(binding) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (IsEqualIID(iid, IID_IUnknown)) {
            *value = static_cast<IUnknown*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining = --references_;
        if (remaining == 0 && !reentered_.exchange(true)) {
            // This is the final Release performed by the binding. It models a
            // producer whose teardown synchronously clears its panel source.
            // Commit must have dropped its mutex before making this COM call.
            binding_.Clear();
            reentry_completed_ = true;
        }
        return remaining;
    }

    bool reentry_completed() const noexcept { return reentry_completed_.load(); }

private:
    openxaml::ExternalSurfaceBinding& binding_;
    std::atomic<ULONG> references_{1};
    std::atomic<bool> reentered_{false};
    std::atomic<bool> reentry_completed_{false};
};

void TestHandleOwnershipAndReplacement() {
    openxaml::ExternalSurfaceBinding binding;
    HANDLE first = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE second = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    Check(first && second, "test event handles were created");
    if (!first || !second) return;

    Check(SUCCEEDED(binding.SetCompositionSurfaceHandle(first)), "first handle binds");
    const auto first_snapshot = binding.Snapshot();
    const auto retained_reference = binding.CaptureExternalSurface();
    Check(first_snapshot.kind() == openxaml::ExternalSurfaceKind::CompositionSurfaceHandle,
          "snapshot records composition handle kind");
    Check(first_snapshot.generation() == 1, "first bind advances generation");
    Check(retained_reference.kind ==
              openxaml::ExternalSurfaceKind::CompositionSurfaceHandle &&
              retained_reference.generation == 1 &&
              retained_reference.native_value == reinterpret_cast<std::uintptr_t>(
                  first_snapshot.composition_surface_handle()) &&
              retained_reference.lifetime,
          "retained reference carries kind, generation, native identity and lifetime");

    CloseHandle(first);
    Check(SetEvent(first_snapshot.composition_surface_handle()) != FALSE,
          "binding owns a duplicate independent of caller handle");

    Check(SUCCEEDED(binding.SetCompositionSurfaceHandle(second)), "replacement handle binds");
    const auto second_snapshot = binding.Snapshot();
    Check(second_snapshot.generation() == 2, "replacement advances generation");
    Check(first_snapshot.generation() == 1, "retained snapshot generation is immutable");
    Check(WaitForSingleObject(first_snapshot.composition_surface_handle(), 0) == WAIT_OBJECT_0,
          "retained snapshot keeps replaced handle alive");
    Check(WaitForSingleObject(second_snapshot.composition_surface_handle(), 0) == WAIT_TIMEOUT,
          "replacement snapshot refers to new object");

    CloseHandle(second);
}

void TestFailedReplacementIsTransactional() {
    openxaml::ExternalSurfaceBinding binding;
    HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    Check(event != nullptr, "transaction event was created");
    if (!event) return;

    Check(SUCCEEDED(binding.SetCompositionSurfaceHandle(event)), "transaction source binds");
    const auto before = binding.Snapshot();
    CloseHandle(event);

    const HANDLE invalid = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(0x1234));
    Check(FAILED(binding.SetCompositionSurfaceHandle(invalid)), "invalid handle is rejected");
    const auto after = binding.Snapshot();
    Check(after.generation() == before.generation(), "failed bind preserves generation");
    Check(after.composition_surface_handle() == before.composition_surface_handle(),
          "failed bind preserves committed resource");
}

void TestComOwnershipAndClear() {
    openxaml::ExternalSurfaceBinding binding;
    CountingUnknown swap_chain;

    Check(SUCCEEDED(binding.SetDxgiSwapChain(&swap_chain)), "COM swap chain binds");
    Check(swap_chain.references() == 2, "binding AddRefs COM swap chain");
    const auto retained = binding.Snapshot();
    Check(retained.dxgi_swap_chain() == &swap_chain, "snapshot exposes bound COM object");

    binding.Clear();
    const auto cleared = binding.Snapshot();
    const auto cleared_reference = binding.CaptureExternalSurface();
    Check(cleared.kind() == openxaml::ExternalSurfaceKind::None, "clear removes current source");
    Check(cleared.generation() == 2, "clear advances generation");
    Check(!cleared_reference && cleared_reference.generation == 2,
          "cleared retained reference preserves generation without a resource");
    Check(swap_chain.references() == 2, "retained snapshot keeps COM object alive");
}

void TestFinalComReleaseMayReenterTheBinding() {
    openxaml::ExternalSurfaceBinding binding;
    ReentrantReleaseUnknown swap_chain(binding);

    Check(SUCCEEDED(binding.SetDxgiSwapChain(&swap_chain)),
          "reentrant COM source binds");
    // Transfer the caller's reference so clearing the binding performs the
    // final Release and therefore the synchronous reentry.
    Check(swap_chain.Release() == 1,
          "binding is the sole COM owner before clear");
    binding.Clear();

    const auto after = binding.Snapshot();
    Check(swap_chain.reentry_completed(),
          "final COM Release reentered without binding-mutex deadlock");
    Check(after.kind() == openxaml::ExternalSurfaceKind::None,
          "reentrant clear leaves no external source");
    Check(after.generation() == 3,
          "outer and reentrant clears are separate committed mutations");
}

} // namespace

int main() {
    TestHandleOwnershipAndReplacement();
    TestFailedReplacementIsTransactional();
    TestComOwnershipAndClear();
    TestFinalComReleaseMayReenterTheBinding();
    if (failures) return 1;
    std::puts("external surface binding checks passed");
    return 0;
}
