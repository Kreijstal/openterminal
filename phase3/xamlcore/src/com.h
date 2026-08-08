// COM plumbing shared by every object in the DLL.
//
// Nothing here is XAML-specific. It exists because a WinRT object is a COM
// object first: refcounted, discovered through QueryInterface, and required to
// answer GetRuntimeClassName with the name it was activated under.

#ifndef OPENXAML_COM_H
#define OPENXAML_COM_H

#include "sdk.h"

#include <cstring>
#include <cstdio>
#include <memory>

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
