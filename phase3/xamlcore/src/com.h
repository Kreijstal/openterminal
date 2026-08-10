// COM plumbing shared by every object in the DLL.
//
// Nothing here is XAML-specific. It exists because a WinRT object is a COM
// object first: refcounted, discovered through QueryInterface, and required to
// answer GetRuntimeClassName with the name it was activated under.

#ifndef OPENXAML_COM_H
#define OPENXAML_COM_H

#include "sdk.h"

#include <cstring>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <string>

#include "element.h"
#include "grid.h"
#include "openxaml_iids.h"

namespace openxaml::winrt {

inline constexpr GUID IID_OpenXamlWeakReference = {
    0x00000037, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
inline constexpr GUID IID_OpenXamlWeakReferenceSource = {
    0x00000038, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

// A private interface, so the DLL can recover its own C++ object from an ABI
// pointer that has been round-tripped through a caller. QueryInterface is the
// only supported way to do that -- a static_cast from an interface the caller
// handed back would assume a vtable layout we do not control.
//
// The IID is ours, not the SDK's: it names an interface that exists only
// inside this DLL and is deliberately absent from any WinMD.
// {6F70656E-7861-6D6C-9E01-6C61796F7574}
inline constexpr GUID IID_IOpenXamlNative = {
    0x6f70656e, 0x7861, 0x6d6c, {0x9e, 0x01, 0x6c, 0x61, 0x79, 0x6f, 0x75, 0x74}};

struct IOpenXamlNative : ::IUnknown {
    // The layout object behind this WinRT object. Never null.
    virtual openxaml::Element* LayoutElement() = 0;
    // Run an island-sized layout pass and publish the resulting framework
    // events throughout the projected visual tree.
    virtual HRESULT PerformLayout(double width, double height) = 0;
};

// Read-only diagnostics for the in-process desktop island. This remains a
// private ABI (and therefore does not appear in the WinMD): renderer smoke
// tests use it to prove that replaying WM_PAINT does not rebuild a frame.
// {6F70656E-7861-6D6C-9E03-69736C616E64}
inline constexpr GUID IID_IOpenXamlIslandDiagnostics = {
    0x6f70656e, 0x7861, 0x6d6c, {0x9e, 0x03, 0x69, 0x73, 0x6c, 0x61, 0x6e, 0x64}};

struct IOpenXamlIslandDiagnostics : ::IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetFrameGeneration(std::uint64_t* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetFrameExtent(INT32* width, INT32* height) = 0;
};

// The same trick for Grid's row and column definitions. They are
// DependencyObjects rather than UIElements -- they have no layout of their
// own, and a Grid reads their declared sizes directly -- so they need a
// separate escape hatch.
// {6F70656E-7861-6D6C-9E02-6465666E736E}
inline constexpr GUID IID_IOpenXamlDefinition = {
    0x6f70656e, 0x7861, 0x6d6c, {0x9e, 0x02, 0x64, 0x65, 0x66, 0x6e, 0x73, 0x6e}};

struct IOpenXamlDefinition : ::IUnknown {
    virtual const openxaml::Definition* LayoutDefinition() = 0;
};

// Refcounting, kept off the interface classes so that the many inherited
// IInspectable declarations all resolve to one implementation.
class ComObject {
public:
    virtual ~ComObject() = default;

    // Member collections call these around a structural mutation. The old
    // item is reported while its COM reference is still held, so an owning
    // element can detach native visual state before Release destroys it.
    // Non-visual collections keep the default no-op behavior.
    virtual HRESULT ValidateOwnedCollectionChange(IUnknown* removed,
                                                   IUnknown* added) {
        (void)removed;
        (void)added;
        return S_OK;
    }
    virtual void OnOwnedCollectionRemoving(IUnknown* removed) {
        (void)removed;
    }
    // Called after the collection no longer exposes the removed item, but
    // before its collection-owned COM reference is released. Visual owners
    // use this post-mutation seam to reconcile focus/capture without letting
    // callbacks observe the child in both the old and new tree state.
    virtual void OnOwnedCollectionRemoved(IUnknown* removed) {
        (void)removed;
    }
    virtual void OnOwnedCollectionChanged(IUnknown* added) {
        (void)added;
    }

    // Virtual so that an object which is physically a member of another --
    // Panel.Children, Grid.ColumnDefinitions -- can delegate its lifetime to
    // its owner. Such an object is handed out through the ABI and can outlive
    // the caller's reference to its parent, so counting it separately would
    // let the parent be destroyed underneath it.
    virtual ULONG Retain() { return static_cast<ULONG>(InterlockedIncrement(&references_)); }
    // Weak-reference resolution must not resurrect an object after its final
    // Release has committed. InterlockedIncrement alone would turn 0 back into
    // 1 in that race, so weak references use this compare/exchange loop first.
    bool TryRetain() {
        LONG current = InterlockedCompareExchange(&references_, 0, 0);
        while (current != 0) {
            const LONG observed = InterlockedCompareExchange(
                &references_, current + 1, current);
            if (observed == current) return true;
            current = observed;
        }
        return false;
    }
    virtual ULONG ReleaseOne() {
        const ULONG remaining = static_cast<ULONG>(InterlockedDecrement(&references_));
        if (remaining == 0) delete this;
        return remaining;
    }

    // The name the object was activated under. The corpus records it as the
    // node's type, so it has to be the real runtime class name rather than
    // anything descriptive.
    virtual const wchar_t* RuntimeClassName() const = 0;

private:
    LONG references_ = 1;
};

// Shared by an element and every IWeakReference handed out for it. The state
// deliberately does not own the element. Resolve takes a temporary strong
// reference with TryRetain before it calls QueryInterface; the element clears
// both pointers as its destructor begins.
struct WeakReferenceState {
    WeakReferenceState(ComObject* owner_value, IUnknown* identity_value)
        : owner(owner_value), identity(identity_value) {}

    void Invalidate() {
        AcquireSRWLockExclusive(&lock);
        owner = nullptr;
        identity = nullptr;
        ReleaseSRWLockExclusive(&lock);
    }

    SRWLOCK lock = SRWLOCK_INIT;
    ComObject* owner = nullptr;
    IUnknown* identity = nullptr;
};

class WeakReferenceObject final : public IWeakReference {
public:
    explicit WeakReferenceObject(std::shared_ptr<WeakReferenceState> state)
        : state_(std::move(state)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (IsEqualGUID(iid, IID_IUnknown) ||
            IsEqualGUID(iid, IID_OpenXamlWeakReference)) {
            *object = static_cast<IWeakReference*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return static_cast<ULONG>(InterlockedIncrement(&references_));
    }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining = static_cast<ULONG>(InterlockedDecrement(&references_));
        if (remaining == 0) delete this;
        return remaining;
    }
    HRESULT STDMETHODCALLTYPE Resolve(REFIID iid, IInspectable** object) override {
        if (!object) return E_POINTER;
        *object = nullptr;

        AcquireSRWLockShared(&state_->lock);
        ComObject* owner = state_->owner;
        IUnknown* identity = state_->identity;
        const bool retained = owner && identity && owner->TryRetain();
        ReleaseSRWLockShared(&state_->lock);
        if (!retained) return S_OK;

        const HRESULT hr = identity->QueryInterface(iid, reinterpret_cast<void**>(object));
        owner->ReleaseOne();
        return hr == E_NOINTERFACE ? S_OK : hr;
    }

private:
    ~WeakReferenceObject() = default;
    LONG references_ = 1;
    std::shared_ptr<WeakReferenceState> state_;
};

// Private namescope storage and materializer seams. They are COM interfaces,
// rather than C++ shared_ptrs crossing a vtable, so a focused client can test
// the exact ABI and the DLL never depends on another module's C++ runtime.
// {6F70656E-7861-6D6C-9E05-6E616D657363}
inline constexpr GUID IID_IOpenXamlNameScope = {
    0x6f70656e, 0x7861, 0x6d6c, {0x9e, 0x05, 0x6e, 0x61, 0x6d, 0x65, 0x73, 0x63}};
// {6F70656E-7861-6D6C-9E06-6E616D656F77}
inline constexpr GUID IID_IOpenXamlNameScopeOwner = {
    0x6f70656e, 0x7861, 0x6d6c, {0x9e, 0x06, 0x6e, 0x61, 0x6d, 0x65, 0x6f, 0x77}};
// {6F70656E-7861-6D6C-9E07-646566657272}
inline constexpr GUID IID_IOpenXamlDeferredMaterializer = {
    0x6f70656e, 0x7861, 0x6d6c, {0x9e, 0x07, 0x64, 0x65, 0x66, 0x65, 0x72, 0x72}};

struct IOpenXamlNameScope;
struct IOpenXamlDeferredMaterializer : ::IUnknown {
    // Returns a strong reference to the materialized root, or null when the
    // weak owning collection/component has already expired. The namescope is
    // supplied by the caller so the materializer never owns it and cannot
    // form namescope -> materializer -> element -> namescope cycles.
    virtual HRESULT STDMETHODCALLTYPE Materialize(
        IOpenXamlNameScope* name_scope, IInspectable** value) = 0;
};

struct IOpenXamlNameScope : ::IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Register(HSTRING name,
                                               IInspectable* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE Unregister(HSTRING name,
                                                 IInspectable* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE Find(HSTRING name,
                                           IInspectable** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE RegisterDeferred(
        HSTRING name, IOpenXamlDeferredMaterializer* materializer) = 0;
};

struct IOpenXamlNameScopeOwner : ::IUnknown {
    virtual HRESULT STDMETHODCALLTYPE AttachNameScope(
        IOpenXamlNameScope* value) = 0;
};

// A XAML namescope must not own the objects registered in it: generated
// components own their visual tree, while FindName is only a lookup service.
// Store WinRT weak references so removing or destroying a named element makes
// the entry resolve to null instead of retaining an otherwise dead subtree.
//
// Access is deliberately apartment-bound by XamlElement. That matches XAML's
// DependencyObject threading contract and lets registration remain
// transactional without holding a lock across arbitrary COM QueryInterface
// calls.
class XamlNameScope final : public IOpenXamlNameScope {
public:
    XamlNameScope() = default;
    XamlNameScope(const XamlNameScope&) = delete;
    XamlNameScope& operator=(const XamlNameScope&) = delete;

    ~XamlNameScope() {
        for (auto& [_, reference] : entries_) reference->Release();
        for (auto& [_, entry] : deferred_) entry.materializer->Release();
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (IsEqualGUID(iid, IID_IUnknown) ||
            IsEqualGUID(iid, IID_IOpenXamlNameScope)) {
            *value = static_cast<IOpenXamlNameScope*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return static_cast<ULONG>(InterlockedIncrement(&references_));
    }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining =
            static_cast<ULONG>(InterlockedDecrement(&references_));
        if (!remaining) delete this;
        return remaining;
    }

    HRESULT STDMETHODCALLTYPE Register(HSTRING name,
                                       IInspectable* value) noexcept override {
        if (!value) return E_INVALIDARG;
        try {
            const std::wstring key = Key(name);
            if (key.empty()) return S_OK;

            const auto pending = deferred_.find(key);
            if (pending != deferred_.end() && !pending->second.materializing)
                return E_INVALIDARG;

            auto found = entries_.find(key);
            if (found != entries_.end()) {
                IInspectable* current = nullptr;
                const HRESULT resolved = found->second->Resolve(
                    ::openxaml::iid::IInspectable, &current);
                if (FAILED(resolved)) return resolved;
                if (current) {
                    const bool same = SameIdentity(current, value);
                    current->Release();
                    return same ? S_OK : E_INVALIDARG;
                }
            }

            IWeakReferenceSource* source = nullptr;
            HRESULT hr = value->QueryInterface(
                IID_OpenXamlWeakReferenceSource,
                reinterpret_cast<void**>(&source));
            if (FAILED(hr)) return hr;
            IWeakReference* reference = nullptr;
            hr = source->GetWeakReference(&reference);
            source->Release();
            if (FAILED(hr)) return hr;

            if (found == entries_.end()) {
                try {
                    entries_.emplace(key, reference);
                } catch (...) {
                    reference->Release();
                    return E_OUTOFMEMORY;
                }
            } else {
                IWeakReference* stale = found->second;
                found->second = reference;
                stale->Release();
            }
            return S_OK;
        } catch (...) {
            return E_OUTOFMEMORY;
        }
    }

    HRESULT STDMETHODCALLTYPE Unregister(HSTRING name,
                                         IInspectable* value) noexcept override {
        if (!value) return E_INVALIDARG;
        try {
            const std::wstring key = Key(name);
            const auto found = entries_.find(key);
            if (found == entries_.end()) return S_OK;

            IInspectable* current = nullptr;
            const HRESULT resolved = found->second->Resolve(
                ::openxaml::iid::IInspectable, &current);
            const bool remove = SUCCEEDED(resolved) &&
                (!current || SameIdentity(current, value));
            if (current) current->Release();
            if (!remove) return S_OK;
            found->second->Release();
            entries_.erase(found);
            return S_OK;
        } catch (...) {
            // Destruction/rename cleanup is best-effort and must never unwind
            // through a COM boundary. A surviving entry is weak and therefore
            // cannot retain or later resurrect the element.
            return E_OUTOFMEMORY;
        }
    }

    HRESULT STDMETHODCALLTYPE Find(HSTRING name,
                                   IInspectable** value) noexcept override {
        if (!value) return E_POINTER;
        *value = nullptr;
        try {
            const std::wstring key = Key(name);
            auto pending = deferred_.find(key);
            if (pending != deferred_.end() && !pending->second.materializing) {
                IOpenXamlDeferredMaterializer* materializer =
                    pending->second.materializer;
                materializer->AddRef();
                pending->second.materializing = true;

                IInspectable* materialized = nullptr;
                const HRESULT hr = materializer->Materialize(this, &materialized);
                materializer->Release();

                pending = deferred_.find(key);
                if (pending != deferred_.end() &&
                    pending->second.materializer == materializer) {
                    pending->second.materializing = false;
                    if (SUCCEEDED(hr)) {
                        IOpenXamlDeferredMaterializer* owned =
                            pending->second.materializer;
                        deferred_.erase(pending);
                        owned->Release();
                    }
                }
                if (FAILED(hr)) {
                    if (materialized) materialized->Release();
                    return hr;
                }
                *value = materialized;
                return S_OK;
            }
            const auto found = entries_.find(key);
            if (found == entries_.end()) return S_OK;
            return found->second->Resolve(
                ::openxaml::iid::IInspectable, value);
        } catch (...) {
            return E_OUTOFMEMORY;
        }
    }

    HRESULT STDMETHODCALLTYPE RegisterDeferred(
        HSTRING name,
        IOpenXamlDeferredMaterializer* materializer) noexcept override {
        if (!materializer) return E_INVALIDARG;
        try {
            const std::wstring key = Key(name);
            if (key.empty()) return E_INVALIDARG;
            if (deferred_.find(key) != deferred_.end()) return E_INVALIDARG;

            const auto found = entries_.find(key);
            if (found != entries_.end()) {
                IInspectable* current = nullptr;
                const HRESULT resolved = found->second->Resolve(
                    ::openxaml::iid::IInspectable, &current);
                if (FAILED(resolved)) return resolved;
                if (current) {
                    current->Release();
                    return E_INVALIDARG;
                }
            }

            materializer->AddRef();
            try {
                deferred_.emplace(key, DeferredEntry{materializer, false});
            } catch (...) {
                materializer->Release();
                return E_OUTOFMEMORY;
            }
            return S_OK;
        } catch (...) {
            return E_OUTOFMEMORY;
        }
    }

private:
    static std::wstring Key(HSTRING value) {
        UINT32 length = 0;
        const wchar_t* text = WindowsGetStringRawBuffer(value, &length);
        return std::wstring(text ? text : L"", length);
    }

    static bool SameIdentity(IInspectable* left, IInspectable* right) noexcept {
        IUnknown* left_identity = nullptr;
        IUnknown* right_identity = nullptr;
        const HRESULT left_hr = left->QueryInterface(
            IID_IUnknown, reinterpret_cast<void**>(&left_identity));
        const HRESULT right_hr = right->QueryInterface(
            IID_IUnknown, reinterpret_cast<void**>(&right_identity));
        const bool same = SUCCEEDED(left_hr) && SUCCEEDED(right_hr) &&
            left_identity == right_identity;
        if (left_identity) left_identity->Release();
        if (right_identity) right_identity->Release();
        return same;
    }

    struct DeferredEntry {
        IOpenXamlDeferredMaterializer* materializer = nullptr;
        bool materializing = false;
    };

    std::map<std::wstring, IWeakReference*> entries_;
    std::map<std::wstring, DeferredEntry> deferred_;
    LONG references_ = 1;
};

inline HRESULT CopyToHString(const wchar_t* text, HSTRING* out) {
    if (!out) return E_POINTER;
    return WindowsCreateString(text, static_cast<UINT32>(std::wcslen(text)), out);
}

inline HRESULT TraceQueryInterfaceMiss(const wchar_t* runtime_class, REFIID iid) {
    // QueryInterface is hot enough that diagnostics must remain opt-in.  The
    // trace is intentionally deterministic: it contains only the runtime
    // class and requested contract, never addresses or machine paths.
    wchar_t enabled[2]{};
    if (!GetEnvironmentVariableW(L"OPENXAML_TRACE_QI", enabled, 2)) {
        return E_NOINTERFACE;
    }
    char line[320]{};
    std::snprintf(
        line, sizeof(line),
        "OpenXaml: QI miss class=%ls iid={%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}\n",
        runtime_class ? runtime_class : L"<unknown>",
        static_cast<unsigned long>(iid.Data1), iid.Data2, iid.Data3,
        iid.Data4[0], iid.Data4[1], iid.Data4[2], iid.Data4[3],
        iid.Data4[4], iid.Data4[5], iid.Data4[6], iid.Data4[7]);
    OutputDebugStringA(line);
    return E_NOINTERFACE;
}

inline void TraceRuntime(const char* message) {
    if (GetEnvironmentVariableW(L"OPENXAML_TRACE_QI", nullptr, 0)) {
        OutputDebugStringA(message);
    }
}

}  // namespace openxaml::winrt

// The six IInspectable methods, identical for every object. QueryInterface is
// left to each class, since only the class knows which interfaces it offers.
//
// Defining these in the most-derived class overrides the pure declarations
// inherited from every interface base at once, which is what makes multiple
// inheritance from several IInspectable-derived interfaces work.
#define OPENXAML_COM_BOILERPLATE()                                                    \
    ULONG STDMETHODCALLTYPE AddRef() override { return this->Retain(); }              \
    ULONG STDMETHODCALLTYPE Release() override { return this->ReleaseOne(); }         \
    HRESULT STDMETHODCALLTYPE GetIids(ULONG*, IID**) override { return E_NOTIMPL; }   \
    HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING* name) override {           \
        return ::openxaml::winrt::CopyToHString(this->RuntimeClassName(), name);      \
    }                                                                                 \
    HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel* level) override {             \
        if (!level) return E_POINTER;                                                 \
        *level = BaseTrust;                                                           \
        return S_OK;                                                                  \
    }

// One QueryInterface arm. The static_cast is what picks the right vtable out
// of the object's several bases, so the caller receives a pointer that is
// valid for exactly the interface it asked for.
#define OPENXAML_QI_ARM(iid_constant, type)                    \
    if (IsEqualGUID(iid, iid_constant)) {                      \
        auto* pointer = static_cast<type*>(this);              \
        pointer->AddRef();                                     \
        *object = pointer;                                     \
        return S_OK;                                           \
    }

#endif  // OPENXAML_COM_H
