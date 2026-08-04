// Activation factories and the DLL's one entry point.
//
// Wine's RoGetActivationFactory resolves a class name to a DLL through
// HKLM\Software\Microsoft\WindowsRuntime\ActivatableClassId, loads it, and
// calls DllGetActivationFactory. Everything below that point is ours.

#include "sdk.h"

#include <cstring>

#include "elements.h"

namespace openxaml::winrt {
namespace {

// A factory that answers ActivateInstance with a new T, and optionally a
// statics interface as well -- WinRT puts a class's static members on its
// activation factory rather than on a separate object.
template <class T>
class Factory : public ComObject, public IActivationFactory {
public:
    explicit Factory(const wchar_t* name) : name_(name) {}

    const wchar_t* RuntimeClassName() const override { return name_; }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        const HRESULT hr = QueryStatics(iid, object);
        if (hr != E_NOINTERFACE) return hr;
        OPENXAML_QI_ARM(::openxaml::iid::IActivationFactory, IActivationFactory)
        OPENXAML_QI_ARM(IID_IUnknown, IActivationFactory)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable, IActivationFactory)
        *object = nullptr;
        return E_NOINTERFACE;
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** instance) override {
        if (!instance) return E_POINTER;
        *instance = static_cast<typename T::PrimaryInterface*>(new T());
        return S_OK;
    }

protected:
    // Nothing by default. A class with static members overrides this.
    virtual HRESULT QueryStatics(REFIID, void**) { return E_NOINTERFACE; }

private:
    const wchar_t* name_;
};

// Grid's attached properties. Row, Column, RowSpan and ColumnSpan live on any
// element, not just a Grid's children, so they are stored on the element and
// read by whichever Grid happens to be the parent.
//
// The `*Property` getters stay E_NOTIMPL: handing back a DependencyProperty
// would imply a property system that does not exist here. Get/Set are the
// whole of what the attached properties support.
class GridFactory : public Factory<GridObject>, public abi::NotImpl_IGridStatics {
public:
    GridFactory() : Factory(L"Windows.UI.Xaml.Controls.Grid") {}

    HRESULT STDMETHODCALLTYPE GetRow(ABI::Windows::UI::Xaml::IFrameworkElement* element,
                                     INT32* value) override {
        return Read(element, value, &openxaml::Element::grid_row);
    }
    HRESULT STDMETHODCALLTYPE SetRow(ABI::Windows::UI::Xaml::IFrameworkElement* element,
                                     INT32 value) override {
        return Write(element, value, &openxaml::Element::grid_row);
    }
    HRESULT STDMETHODCALLTYPE GetColumn(ABI::Windows::UI::Xaml::IFrameworkElement* element,
                                        INT32* value) override {
        return Read(element, value, &openxaml::Element::grid_column);
    }
    HRESULT STDMETHODCALLTYPE SetColumn(ABI::Windows::UI::Xaml::IFrameworkElement* element,
                                        INT32 value) override {
        return Write(element, value, &openxaml::Element::grid_column);
    }
    HRESULT STDMETHODCALLTYPE GetRowSpan(ABI::Windows::UI::Xaml::IFrameworkElement* element,
                                         INT32* value) override {
        return Read(element, value, &openxaml::Element::grid_row_span);
    }
    HRESULT STDMETHODCALLTYPE SetRowSpan(ABI::Windows::UI::Xaml::IFrameworkElement* element,
                                         INT32 value) override {
        return Write(element, value, &openxaml::Element::grid_row_span);
    }
    HRESULT STDMETHODCALLTYPE GetColumnSpan(ABI::Windows::UI::Xaml::IFrameworkElement* element,
                                            INT32* value) override {
        return Read(element, value, &openxaml::Element::grid_column_span);
    }
    HRESULT STDMETHODCALLTYPE SetColumnSpan(ABI::Windows::UI::Xaml::IFrameworkElement* element,
                                            INT32 value) override {
        return Write(element, value, &openxaml::Element::grid_column_span);
    }

    // IGridStatics arrives through a base of its own, so its copies of the
    // IInspectable methods are still pure here and need defining again.
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        return Factory<GridObject>::QueryInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()

protected:
    HRESULT QueryStatics(REFIID iid, void** object) override {
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IGridStatics,
                        ABI::Windows::UI::Xaml::Controls::IGridStatics)
        return E_NOINTERFACE;
    }

private:
    using Field = int openxaml::Element::*;

    static openxaml::Element* Unwrap(ABI::Windows::UI::Xaml::IFrameworkElement* element) {
        if (!element) return nullptr;
        IOpenXamlNative* native = nullptr;
        if (FAILED(element->QueryInterface(IID_IOpenXamlNative,
                                           reinterpret_cast<void**>(&native)))) {
            return nullptr;
        }
        openxaml::Element* layout = native->LayoutElement();
        native->Release();
        return layout;
    }

    static HRESULT Read(ABI::Windows::UI::Xaml::IFrameworkElement* element, INT32* value,
                        Field field) {
        if (!value) return E_POINTER;
        openxaml::Element* layout = Unwrap(element);
        if (!layout) return E_INVALIDARG;
        *value = layout->*field;
        return S_OK;
    }

    static HRESULT Write(ABI::Windows::UI::Xaml::IFrameworkElement* element, INT32 value,
                         Field field) {
        openxaml::Element* layout = Unwrap(element);
        if (!layout) return E_INVALIDARG;
        layout->*field = value;
        return S_OK;
    }
};

// LayoutInformation is static-only: it has no constructor, so activating it
// fails and only the statics interface is reachable.
class LayoutInformationFactory : public ComObject,
                                 public IActivationFactory,
                                 public abi::NotImpl_ILayoutInformationStatics {
public:
    const wchar_t* RuntimeClassName() const override {
        return L"Windows.UI.Xaml.Controls.Primitives.LayoutInformation";
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_Primitives_ILayoutInformationStatics,
                        ABI::Windows::UI::Xaml::Controls::Primitives::ILayoutInformationStatics)
        OPENXAML_QI_ARM(::openxaml::iid::IActivationFactory, IActivationFactory)
        OPENXAML_QI_ARM(IID_IUnknown, IActivationFactory)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable, IActivationFactory)
        *object = nullptr;
        return E_NOINTERFACE;
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable**) override {
        return E_NOTIMPL;
    }

    // The rect the parent arranged this element into -- not where it ended up
    // rendering. Alignment moves an element inside its slot without moving the
    // slot, which is why the corpus can record this without an aligned offset.
    HRESULT STDMETHODCALLTYPE GetLayoutSlot(ABI::Windows::UI::Xaml::IFrameworkElement* element,
                                            ABI::Windows::Foundation::Rect* slot) override {
        if (!slot) return E_POINTER;
        if (!element) return E_INVALIDARG;
        IOpenXamlNative* native = nullptr;
        if (FAILED(element->QueryInterface(IID_IOpenXamlNative,
                                           reinterpret_cast<void**>(&native)))) {
            return E_INVALIDARG;
        }
        const openxaml::Rect rect = native->LayoutElement()->layout_slot();
        native->Release();
        *slot = {static_cast<FLOAT>(rect.x), static_cast<FLOAT>(rect.y),
                 static_cast<FLOAT>(rect.width), static_cast<FLOAT>(rect.height)};
        return S_OK;
    }
};

}  // namespace
}  // namespace openxaml::winrt

using namespace openxaml::winrt;

namespace {

Factory<BorderObject>& BorderFactory() {
    static Factory<BorderObject> factory(L"Windows.UI.Xaml.Controls.Border");
    return factory;
}
Factory<StackPanelObject>& StackPanelFactory() {
    static Factory<StackPanelObject> factory(L"Windows.UI.Xaml.Controls.StackPanel");
    return factory;
}
Factory<ColumnDefinitionObject>& ColumnDefinitionFactory() {
    static Factory<ColumnDefinitionObject> factory(L"Windows.UI.Xaml.Controls.ColumnDefinition");
    return factory;
}
Factory<RowDefinitionObject>& RowDefinitionFactory() {
    static Factory<RowDefinitionObject> factory(L"Windows.UI.Xaml.Controls.RowDefinition");
    return factory;
}
GridFactory& TheGridFactory() {
    static GridFactory factory;
    return factory;
}
LayoutInformationFactory& TheLayoutInformationFactory() {
    static LayoutInformationFactory factory;
    return factory;
}

IActivationFactory* FactoryFor(const wchar_t* name) {
    if (!name) return nullptr;
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.Border") == 0)
        return &BorderFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.Grid") == 0)
        return &TheGridFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.StackPanel") == 0)
        return &StackPanelFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.ColumnDefinition") == 0)
        return &ColumnDefinitionFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.RowDefinition") == 0)
        return &RowDefinitionFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.Primitives.LayoutInformation") == 0)
        return &TheLayoutInformationFactory();
    return nullptr;
}

}  // namespace

extern "C" __declspec(dllexport) HRESULT WINAPI
DllGetActivationFactory(HSTRING classid, IActivationFactory** factory) {
    if (!factory) return E_POINTER;
    *factory = nullptr;

    IActivationFactory* found = FactoryFor(WindowsGetStringRawBuffer(classid, nullptr));
    if (!found) {
        // Say so rather than returning a stub. A caller asking for a class
        // this DLL does not implement needs to see that, not an object whose
        // every method fails later for no stated reason.
        return CLASS_E_CLASSNOTAVAILABLE;
    }

    found->AddRef();
    *factory = found;
    return S_OK;
}

extern "C" __declspec(dllexport) HRESULT WINAPI DllCanUnloadNow() {
    // The factories are process-lifetime statics.
    return S_FALSE;
}
