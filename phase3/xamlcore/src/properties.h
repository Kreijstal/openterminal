// The property system, projected.
//
// The layout core has had a real dependency-property store since wave 1 --
// identity objects, a registry, and a precedence chain of animation over local
// over style over inherited over default. What it did not have was a way to
// reach any of that through the ABI: `IDependencyObject::GetValue` answered
// E_NOTIMPL, and `IGridStatics::get_RowProperty` had nothing to hand back,
// because `Windows.UI.Xaml.DependencyProperty` is a runtime class and this DLL
// did not implement it.
//
// This file is that class, plus `Windows.UI.Xaml.PropertyMetadata`, plus the
// boxing that lets a value cross an ABI whose currency is `IInspectable*`.
// Nothing here is a second property store: every object below holds a pointer
// into the one the layout core already keeps.

#ifndef OPENXAML_PROPERTIES_H
#define OPENXAML_PROPERTIES_H

#include <map>
#include <string>

#include <roapi.h>
#include <windows.foundation.h>

#include "com.h"
#include "openxaml_abi_stubs.h"
#include "property.h"
#include "strings.h"

namespace openxaml::winrt {

namespace wf = ABI::Windows::Foundation;
namespace wux = ABI::Windows::UI::Xaml;

// The DLL's escape hatch from a projected DependencyProperty back to the
// native one, on the same principle as IOpenXamlNative: a caller may hand back
// a pointer that has been through its own code, and QueryInterface is the only
// supported way to recognise our own object in it.
// {6F70656E-7861-6D6C-9E03-70726F706479}
inline constexpr GUID IID_IOpenXamlProperty = {
    0x6f70656e, 0x7861, 0x6d6c, {0x9e, 0x03, 0x70, 0x72, 0x6f, 0x70, 0x64, 0x79}};

struct IOpenXamlProperty : ::IUnknown {
    // Never null.
    virtual const openxaml::DependencyProperty* NativeProperty() = 0;
};

// --- boxing -------------------------------------------------------------------
//
// Boxes are made by the platform's own `Windows.Foundation.PropertyValue`
// rather than by a class of ours. A box this DLL hands out is then
// indistinguishable from one the caller made: it answers to IPropertyValue and
// to the IReference<T> a projection unboxes through, and neither of those is
// something this DLL could produce correctly on its own -- IReference's IID is
// computed from a type signature that is not written down in the SDK headers
// for the scalars.

inline wf::IPropertyValueStatics* BoxingStatics() {
    static wf::IPropertyValueStatics* statics = [] {
        HSTRING name = nullptr;
        static const wchar_t kClass[] = L"Windows.Foundation.PropertyValue";
        if (FAILED(WindowsCreateString(kClass, static_cast<UINT32>(std::wcslen(kClass)), &name)))
            return static_cast<wf::IPropertyValueStatics*>(nullptr);
        wf::IPropertyValueStatics* found = nullptr;
        const HRESULT hr = RoGetActivationFactory(
            name, ::openxaml::iid::Windows_Foundation_IPropertyValueStatics,
            reinterpret_cast<void**>(&found));
        WindowsDeleteString(name);
        return SUCCEEDED(hr) ? found : nullptr;
    }();
    return statics;
}

// A value out of the store, as an IInspectable the caller owns.
//
// A Thickness is refused rather than boxed. The runtime boxes one as
// IReference<Thickness>, whose IID this DLL cannot obtain -- the SDK's headers
// declare no such specialisation -- and a box that answered only to
// IPropertyValue would unbox as nothing at all in a projection. Margin,
// Padding and BorderThickness are all reachable through their own typed
// accessors, so the refusal costs a caller nothing but a name.
inline HRESULT BoxPropertyValue(const openxaml::PropertyValue& value, IInspectable** out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    // Null boxes as a null pointer, which is how the ABI spells it -- there is
    // no object that means "no object".
    if (std::holds_alternative<std::monostate>(value)) return S_OK;
    wf::IPropertyValueStatics* statics = BoxingStatics();
    if (!statics) return E_NOINTERFACE;
    if (const double* number = std::get_if<double>(&value))
        return statics->CreateDouble(*number, out);
    if (const int* number = std::get_if<int>(&value))
        return statics->CreateInt32(*number, out);
    if (const bool* flag = std::get_if<bool>(&value))
        return statics->CreateBoolean(*flag ? 1 : 0, out);
    if (const std::string* text = std::get_if<std::string>(&value)) {
        HSTRING wide = nullptr;
        const HRESULT hr = HStringFromUtf8(*text, &wide);
        if (FAILED(hr)) return hr;
        const HRESULT boxed = statics->CreateString(wide, out);
        WindowsDeleteString(wide);
        return boxed;
    }
    // A Thickness. See the note above: refused by name rather than boxed into
    // something a projection cannot read back.
    return E_NOTIMPL;
}

// The other direction. `expected` is the property's registered default, whose
// type decides how a numeric box is read: a caller that boxes Width as an
// Int32 means the double the property holds, and storing an int under a
// property every reader asks for a double from would turn a value into a
// type error at the next read.
inline HRESULT UnboxPropertyValue(IInspectable* value, const openxaml::PropertyValue& expected,
                                  openxaml::PropertyValue* out) {
    if (!out) return E_POINTER;
    if (!value) {
        *out = std::monostate{};
        return S_OK;
    }
    wf::IPropertyValue* boxed = nullptr;
    if (FAILED(value->QueryInterface(::openxaml::iid::Windows_Foundation_IPropertyValue,
                                     reinterpret_cast<void**>(&boxed)))) {
        // Not a box at all: some other runtime class. This store holds
        // scalars, so saying so is the only honest answer.
        return E_NOTIMPL;
    }
    wf::PropertyType kind = wf::PropertyType_Empty;
    HRESULT hr = boxed->get_Type(&kind);
    if (SUCCEEDED(hr)) {
        const bool wants_double = std::holds_alternative<double>(expected);
        const bool wants_int = std::holds_alternative<int>(expected);
        switch (kind) {
            case wf::PropertyType_Empty:
                *out = std::monostate{};
                break;
            case wf::PropertyType_Double: {
                DOUBLE number = 0;
                hr = boxed->GetDouble(&number);
                if (SUCCEEDED(hr)) *out = wants_int ? openxaml::PropertyValue(static_cast<int>(number))
                                                    : openxaml::PropertyValue(number);
                break;
            }
            case wf::PropertyType_Single: {
                FLOAT number = 0;
                hr = boxed->GetSingle(&number);
                if (SUCCEEDED(hr)) *out = wants_int ? openxaml::PropertyValue(static_cast<int>(number))
                                                    : openxaml::PropertyValue(static_cast<double>(number));
                break;
            }
            case wf::PropertyType_Int32:
            case wf::PropertyType_Int64:
            case wf::PropertyType_UInt32:
            case wf::PropertyType_UInt64: {
                INT64 number = 0;
                if (kind == wf::PropertyType_Int32) {
                    INT32 narrow = 0;
                    hr = boxed->GetInt32(&narrow);
                    number = narrow;
                } else if (kind == wf::PropertyType_Int64) {
                    hr = boxed->GetInt64(&number);
                } else if (kind == wf::PropertyType_UInt32) {
                    UINT32 unsigned_narrow = 0;
                    hr = boxed->GetUInt32(&unsigned_narrow);
                    number = unsigned_narrow;
                } else {
                    UINT64 unsigned_wide = 0;
                    hr = boxed->GetUInt64(&unsigned_wide);
                    number = static_cast<INT64>(unsigned_wide);
                }
                // An integer written to a double-valued property is the double
                // it stands for: `SetValue(WidthProperty, box(5))` means five,
                // and storing an int under a property every reader asks a
                // double from would turn the value into a type error later.
                if (SUCCEEDED(hr))
                    *out = wants_double ? openxaml::PropertyValue(static_cast<double>(number))
                                        : openxaml::PropertyValue(static_cast<int>(number));
                break;
            }
            case wf::PropertyType_Boolean: {
                boolean flag = 0;
                hr = boxed->GetBoolean(&flag);
                if (SUCCEEDED(hr)) *out = static_cast<bool>(flag);
                break;
            }
            case wf::PropertyType_String: {
                HSTRING text = nullptr;
                hr = boxed->GetString(&text);
                if (SUCCEEDED(hr)) {
                    *out = Utf8FromHString(text);
                    WindowsDeleteString(text);
                }
                break;
            }
            default:
                // Every other PropertyType is a kind this store has no
                // alternative for -- a GUID, a DateTime, an array. Refused by
                // name rather than truncated into one it does have.
                hr = E_NOTIMPL;
                break;
        }
    }
    boxed->Release();
    return hr;
}

// --- DependencyProperty.UnsetValue --------------------------------------------
//
// The sentinel that means "no value here", as distinct from a value that
// happens to be null -- null is a perfectly good value for a property whose
// type is `object`, so the absence of one cannot be spelled with it. A caller
// recognises it by identity, which is the only thing it can do with it, so it
// is one object for the life of the process.
//
// An IInspectable and nothing else. It has no runtime class name of its own in
// the runtime either.
class UnsetValueObject final : public ComObject, public ::IInspectable {
public:
    ULONG Retain() override { return 2; }
    ULONG ReleaseOne() override { return 1; }

    const wchar_t* RuntimeClassName() const override {
        return L"Windows.UI.Xaml.DependencyProperty+UnsetValue";
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(IID_IUnknown, ::IInspectable)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable, ::IInspectable)
        *object = nullptr;
        return E_NOINTERFACE;
    }
    OPENXAML_COM_BOILERPLATE()
};

inline ::IInspectable* UnsetValue() {
    static auto* sentinel = new UnsetValueObject();
    return static_cast<::IInspectable*>(sentinel);
}

// --- PropertyMetadata ---------------------------------------------------------

class PropertyMetadataObject final : public ComObject, public abi::NotImpl_IPropertyMetadata {
public:
    explicit PropertyMetadataObject(IInspectable* default_value)
        : default_value_(default_value) {
        if (default_value_) default_value_->AddRef();
    }
    ~PropertyMetadataObject() override {
        if (default_value_) default_value_->Release();
    }

    const wchar_t* RuntimeClassName() const override {
        return L"Windows.UI.Xaml.PropertyMetadata";
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_IPropertyMetadata, wux::IPropertyMetadata)
        OPENXAML_QI_ARM(IID_IUnknown, wux::IPropertyMetadata)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable, wux::IPropertyMetadata)
        *object = nullptr;
        return E_NOINTERFACE;
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE get_DefaultValue(IInspectable** value) override {
        if (!value) return E_POINTER;
        *value = default_value_;
        if (*value) (*value)->AddRef();
        return S_OK;
    }

    // Null, and that is the answer rather than a refusal: a metadata built
    // from a default *value* has no default-value *callback*, and the runtime
    // returns null for it too. CreateWithFactory, which is the call that would
    // produce one, is the one this DLL refuses.
    HRESULT STDMETHODCALLTYPE get_CreateDefaultValueCallback(
        wux::ICreateDefaultValueCallback** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return S_OK;
    }

private:
    IInspectable* default_value_ = nullptr;
};

// --- DependencyProperty -------------------------------------------------------

class DependencyPropertyObject final : public ComObject,
                                       public abi::NotImpl_IDependencyProperty,
                                       public IOpenXamlProperty {
public:
    explicit DependencyPropertyObject(const openxaml::DependencyProperty* property)
        : property_(property) {}

    // A projected property is a process-lifetime singleton, exactly as the
    // native identity it names is: `Grid.RowProperty` is the same object every
    // time it is asked for, and a caller comparing two of them by pointer --
    // which is the only way to compare dependency properties -- has to get the
    // same answer as the runtime gives. So it is never destroyed, and its
    // reference count is nominal.
    ULONG Retain() override { return 2; }
    ULONG ReleaseOne() override { return 1; }

    const wchar_t* RuntimeClassName() const override {
        return L"Windows.UI.Xaml.DependencyProperty";
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_IDependencyProperty,
                        wux::IDependencyProperty)
        OPENXAML_QI_ARM(IID_IUnknown, wux::IDependencyProperty)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable, wux::IDependencyProperty)
        if (IsEqualGUID(iid, IID_IOpenXamlProperty)) {
            auto* pointer = static_cast<IOpenXamlProperty*>(this);
            static_cast<wux::IDependencyProperty*>(this)->AddRef();
            *object = pointer;
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    OPENXAML_COM_BOILERPLATE()

    const openxaml::DependencyProperty* NativeProperty() override { return property_; }

    // The metadata the property was registered with. `forType` is ignored, and
    // deliberately: type-specific metadata is what OverrideMetadata produces,
    // this store has none, and answering a per-type question with the
    // registration's own metadata is the same answer the runtime gives when no
    // override exists.
    HRESULT STDMETHODCALLTYPE GetMetadata(ABI::Windows::UI::Xaml::Interop::TypeName,
                                          wux::IPropertyMetadata** result) override;

private:
    const openxaml::DependencyProperty* property_ = nullptr;
};

// The projection of a native property, created once and returned for ever
// after. Never null.
//
// Keyed by the property's dense store index, which is the identity the layout
// core assigns it -- so two calls naming the same property return the same
// object, which is what makes `dp == Grid.RowProperty` work on the caller's
// side.
inline wux::IDependencyProperty* ProjectProperty(const openxaml::DependencyProperty& property) {
    static std::map<size_t, DependencyPropertyObject*> projected;
    auto& slot = projected[property.index()];
    if (!slot) slot = new DependencyPropertyObject(&property);
    return static_cast<wux::IDependencyProperty*>(slot);
}

// The native property behind a projected one, or nullptr if the object did not
// come from this DLL.
inline const openxaml::DependencyProperty* NativeProperty(wux::IDependencyProperty* property) {
    if (!property) return nullptr;
    IOpenXamlProperty* ours = nullptr;
    if (FAILED(property->QueryInterface(IID_IOpenXamlProperty,
                                        reinterpret_cast<void**>(&ours)))) {
        return nullptr;
    }
    const openxaml::DependencyProperty* native = ours->NativeProperty();
    ours->Release();
    return native;
}

inline HRESULT DependencyPropertyObject::GetMetadata(ABI::Windows::UI::Xaml::Interop::TypeName,
                                                     wux::IPropertyMetadata** result) {
    if (!result) return E_POINTER;
    *result = nullptr;
    IInspectable* boxed = nullptr;
    const HRESULT hr = BoxPropertyValue(property_->default_value(), &boxed);
    if (FAILED(hr)) return hr;
    auto* metadata = new PropertyMetadataObject(boxed);
    if (boxed) boxed->Release();
    *result = metadata;
    return S_OK;
}

}  // namespace openxaml::winrt

#endif  // OPENXAML_PROPERTIES_H
