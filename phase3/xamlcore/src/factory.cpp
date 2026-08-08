// Activation factories and the DLL's one entry point.
//
// Wine's RoGetActivationFactory resolves a class name to a DLL through
// HKLM\Software\Microsoft\WindowsRuntime\ActivatableClassId, loads it, and
// calls DllGetActivationFactory. Everything below that point is ours.

#include "sdk.h"
#include <roapi.h>

#include <algorithm>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "elements.h"
#include "fonts.h"
#include "xbf_object.h"

namespace openxaml::winrt {
HRESULT MaterializeXbf(const std::shared_ptr<xbf::Object>& graph,
                       IInspectable* component);

namespace {

namespace wuxmk = ABI::Windows::UI::Xaml::Markup;
namespace ws = ABI::Windows::System;
namespace wuxh = ABI::Windows::UI::Xaml::Hosting;
namespace wuxa = ABI::Windows::UI::Xaml::Automation;
namespace wuxap = ABI::Windows::UI::Xaml::Automation::Peers;
namespace wuit = ABI::Windows::UI::Text;
namespace wuxma = ABI::Windows::UI::Xaml::Media::Animation;
namespace warc = ABI::Windows::ApplicationModel::Resources::Core;
namespace wfc = ABI::Windows::Foundation::Collections;

bool g_xaml_manager_initialized = false;

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
        return TraceQueryInterfaceMiss(RuntimeClassName(), iid);
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

template <class T, class Interface>
HRESULT CreateComposableObject(IInspectable** inner, Interface** value) {
    if (!inner || !value) return E_POINTER;
    *inner = nullptr;
    *value = nullptr;
    auto* object = new (std::nothrow) T();
    if (!object) return E_OUTOFMEMORY;
    auto* projected = static_cast<Interface*>(object);
    projected->AddRef();
    *inner = static_cast<IInspectable*>(projected);
    *value = projected;
    return S_OK;
}

using PropertyMap = wfc::__FIMap_2_HSTRING_IInspectable_t;
using PropertyMapView = wfc::__FIMapView_2_HSTRING_IInspectable_t;
using PropertyObservableMap = wfc::__FIObservableMap_2_HSTRING_IInspectable_t;
using PropertyIterable =
    wfc::__FIIterable_1___FIKeyValuePair_2_HSTRING_IInspectable_t;
using PropertyIterator =
    wfc::__FIIterator_1___FIKeyValuePair_2_HSTRING_IInspectable_t;
using PropertyMapChangedHandler =
    wfc::__FMapChangedEventHandler_2_HSTRING_IInspectable_t;
inline constexpr GUID IID_PropertySet = {
    0x8a43ed9f, 0xf4e6, 0x4421, {0xac, 0xf9, 0x1d, 0xab, 0x29, 0x86, 0x82, 0x0c}};
inline constexpr GUID IID_PropertyMap = {
    0x1b0d3570, 0x0877, 0x5ec2, {0x8a, 0x2c, 0x3b, 0x95, 0x39, 0x50, 0x6a, 0xca}};
inline constexpr GUID IID_PropertyMapView = {
    0xbb78502a, 0xf79d, 0x54fa, {0x92, 0xc9, 0x90, 0xc5, 0x03, 0x9f, 0xdf, 0x7e}};
inline constexpr GUID IID_PropertyObservableMap = {
    0x236aac9d, 0xfb12, 0x5c4d, {0xa4, 0x1c, 0x9e, 0x44, 0x5f, 0xb4, 0xd7, 0xec}};
inline constexpr GUID IID_PropertyIterable = {
    0xfe2f3d47, 0x5d47, 0x5499, {0x83, 0x74, 0x43, 0x0c, 0x7c, 0xda, 0x02, 0x04}};

class ValueSetObject final : public ComObject,
                             public wfc::IPropertySet,
                             public PropertyMap,
                             public PropertyMapView,
                             public PropertyObservableMap,
                             public PropertyIterable {
public:
    using PrimaryInterface = wfc::IPropertySet;
    ~ValueSetObject() override {
        for (auto& [_, value] : values_) value->Release();
        for (auto& [_, handler] : changed_handlers_) handler->Release();
    }
    const wchar_t* RuntimeClassName() const override {
        return L"Windows.Foundation.Collections.ValueSet";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        if (IsEqualGUID(iid, IID_PropertySet)) {
            *object = static_cast<wfc::IPropertySet*>(this);
        } else if (IsEqualGUID(iid, IID_PropertyMap)) {
            *object = static_cast<PropertyMap*>(this);
        } else if (IsEqualGUID(iid, IID_PropertyMapView)) {
            *object = static_cast<PropertyMapView*>(this);
        } else if (IsEqualGUID(iid, IID_PropertyObservableMap)) {
            *object = static_cast<PropertyObservableMap*>(this);
        } else if (IsEqualGUID(iid, IID_PropertyIterable)) {
            *object = static_cast<PropertyIterable*>(this);
        } else if (IsEqualGUID(iid, IID_IUnknown) ||
                   IsEqualGUID(iid, ::openxaml::iid::IInspectable)) {
            *object = static_cast<wfc::IPropertySet*>(this);
        } else {
            *object = nullptr;
            return TraceQueryInterfaceMiss(RuntimeClassName(), iid);
        }
        static_cast<IUnknown*>(static_cast<wfc::IPropertySet*>(this))->AddRef();
        return S_OK;
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE Lookup(HSTRING key, IInspectable** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        const auto found = values_.find(Key(key));
        if (found == values_.end()) return E_BOUNDS;
        *value = found->second;
        (*value)->AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Size(UINT32* value) override {
        if (!value) return E_POINTER;
        *value = static_cast<UINT32>(values_.size());
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE HasKey(HSTRING key, boolean* value) override {
        if (!value) return E_POINTER;
        *value = values_.count(Key(key)) ? 1 : 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetView(PropertyMapView** value) override {
        if (!value) return E_POINTER;
        *value = static_cast<PropertyMapView*>(this);
        (*value)->AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Insert(HSTRING key, IInspectable* value,
                                     boolean* replaced) override {
        if (!value || !replaced) return E_INVALIDARG;
        const std::wstring name = Key(key);
        const auto found = values_.find(name);
        *replaced = found != values_.end();
        value->AddRef();
        if (found == values_.end()) {
            values_.emplace(name, value);
        } else {
            found->second->Release();
            found->second = value;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Remove(HSTRING key) override {
        const auto found = values_.find(Key(key));
        if (found == values_.end()) return E_BOUNDS;
        found->second->Release();
        values_.erase(found);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Clear() override {
        for (auto& [_, value] : values_) value->Release();
        values_.clear();
        return S_OK;
    }

    // IMapView shares Lookup/get_Size/HasKey with IMap.
    HRESULT STDMETHODCALLTYPE Split(PropertyMapView** first,
                                    PropertyMapView** second) override {
        if (!first || !second) return E_POINTER;
        *first = static_cast<PropertyMapView*>(this);
        (*first)->AddRef();
        *second = nullptr;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE add_MapChanged(PropertyMapChangedHandler* handler,
                                              EventRegistrationToken* token) override {
        if (!handler || !token) return E_INVALIDARG;
        token->value = ++next_token_;
        handler->AddRef();
        changed_handlers_[token->value] = handler;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE remove_MapChanged(EventRegistrationToken token) override {
        const auto found = changed_handlers_.find(token.value);
        if (found == changed_handlers_.end()) return S_OK;
        found->second->Release();
        changed_handlers_.erase(found);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE First(PropertyIterator** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return E_NOTIMPL;
    }

private:
    static std::wstring Key(HSTRING value) {
        UINT32 length = 0;
        const wchar_t* text = WindowsGetStringRawBuffer(value, &length);
        return text ? std::wstring(text, length) : std::wstring{};
    }
    std::map<std::wstring, IInspectable*> values_;
    std::map<LONGLONG, PropertyMapChangedHandler*> changed_handlers_;
    LONGLONG next_token_ = 0;
};

class ColorsFactory final : public ComObject,
                            public IActivationFactory,
                            public abi::NotImpl_IColorsStatics {
public:
    const wchar_t* RuntimeClassName() const override { return L"Windows.UI.Colors"; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_IColorsStatics,
                        ABI::Windows::UI::IColorsStatics)
        OPENXAML_QI_ARM(::openxaml::iid::IActivationFactory, IActivationFactory)
        OPENXAML_QI_ARM(IID_IUnknown, IActivationFactory)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable, IActivationFactory)
        *object = nullptr;
        return TraceQueryInterfaceMiss(RuntimeClassName(), iid);
    }
    OPENXAML_COM_BOILERPLATE()
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE get_Transparent(ABI::Windows::UI::Color* value) override {
        if (!value) return E_POINTER;
        *value = {0, 255, 255, 255};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Black(ABI::Windows::UI::Color* value) override {
        if (!value) return E_POINTER;
        *value = {255, 0, 0, 0};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_White(ABI::Windows::UI::Color* value) override {
        if (!value) return E_POINTER;
        *value = {255, 255, 255, 255};
        return S_OK;
    }
};

class VisualStateManagerFactory final
    : public ComObject,
      public IActivationFactory,
      public abi::NotImpl_IVisualStateManagerStatics {
public:
    const wchar_t* RuntimeClassName() const override {
        return L"Windows.UI.Xaml.VisualStateManager";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_IVisualStateManagerStatics,
                        wux::IVisualStateManagerStatics)
        OPENXAML_QI_ARM(::openxaml::iid::IActivationFactory, IActivationFactory)
        OPENXAML_QI_ARM(IID_IUnknown, IActivationFactory)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable, IActivationFactory)
        *object = nullptr;
        return TraceQueryInterfaceMiss(RuntimeClassName(), iid);
    }
    OPENXAML_COM_BOILERPLATE()
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GoToState(wuxc::IControl*, HSTRING, boolean,
                                        boolean* result) override {
        if (!result) return E_POINTER;
        *result = 1;
        return S_OK;
    }
};

class TimelineFactory final
    : public ComObject,
      public IActivationFactory,
      public abi::NotImpl_ITimelineStatics {
public:
    const wchar_t* RuntimeClassName() const override {
        return L"Windows.UI.Xaml.Media.Animation.Timeline";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(
            ::openxaml::iid::Windows_UI_Xaml_Media_Animation_ITimelineStatics,
            wuxma::ITimelineStatics)
        OPENXAML_QI_ARM(::openxaml::iid::IActivationFactory, IActivationFactory)
        OPENXAML_QI_ARM(IID_IUnknown, IActivationFactory)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable, IActivationFactory)
        *object = nullptr;
        return TraceQueryInterfaceMiss(RuntimeClassName(), iid);
    }
    OPENXAML_COM_BOILERPLATE()
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable**) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE get_AllowDependentAnimations(boolean* value) override {
        if (!value) return E_POINTER;
        *value = allow_dependent_animations_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_AllowDependentAnimations(boolean value) override {
        allow_dependent_animations_ = value != 0;
        return S_OK;
    }
#define OPENXAML_NULL_TIMELINE_PROPERTY(name)                                 \
    HRESULT STDMETHODCALLTYPE get_##name##Property(                            \
        wux::IDependencyProperty** value) override {                           \
        if (!value) return E_POINTER;                                          \
        *value = nullptr;                                                      \
        return S_OK;                                                           \
    }
    OPENXAML_NULL_TIMELINE_PROPERTY(AutoReverse)
    OPENXAML_NULL_TIMELINE_PROPERTY(BeginTime)
    OPENXAML_NULL_TIMELINE_PROPERTY(Duration)
    OPENXAML_NULL_TIMELINE_PROPERTY(SpeedRatio)
    OPENXAML_NULL_TIMELINE_PROPERTY(FillBehavior)
    OPENXAML_NULL_TIMELINE_PROPERTY(RepeatBehavior)
#undef OPENXAML_NULL_TIMELINE_PROPERTY
private:
    boolean allow_dependent_animations_ = 0;
};

class AutomationPropertiesFactory final
    : public ComObject,
      public IActivationFactory,
      public abi::NotImpl_IAutomationPropertiesStatics,
      public abi::NotImpl_IAutomationPropertiesStatics2 {
public:
    const wchar_t* RuntimeClassName() const override {
        return L"Windows.UI.Xaml.Automation.AutomationProperties";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(
            ::openxaml::iid::Windows_UI_Xaml_Automation_IAutomationPropertiesStatics,
            wuxa::IAutomationPropertiesStatics)
        OPENXAML_QI_ARM(
            ::openxaml::iid::Windows_UI_Xaml_Automation_IAutomationPropertiesStatics2,
            wuxa::IAutomationPropertiesStatics2)
        OPENXAML_QI_ARM(::openxaml::iid::IActivationFactory, IActivationFactory)
        OPENXAML_QI_ARM(IID_IUnknown, IActivationFactory)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable, IActivationFactory)
        *object = nullptr;
        return TraceQueryInterfaceMiss(RuntimeClassName(), iid);
    }
    OPENXAML_COM_BOILERPLATE()
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable**) override {
        return E_NOTIMPL;
    }
#define OPENXAML_AUTOMATION_STRING(name, id)                            \
    HRESULT STDMETHODCALLTYPE Get##name(                                \
        wux::IDependencyObject* element, HSTRING* value) override {     \
        return GetString(element, id, value);                           \
    }                                                                   \
    HRESULT STDMETHODCALLTYPE Set##name(                                \
        wux::IDependencyObject* element, HSTRING value) override {      \
        return SetString(element, id, value);                           \
    }
    OPENXAML_AUTOMATION_STRING(AcceleratorKey, 0)
    OPENXAML_AUTOMATION_STRING(AccessKey, 1)
    OPENXAML_AUTOMATION_STRING(AutomationId, 2)
    OPENXAML_AUTOMATION_STRING(HelpText, 3)
    OPENXAML_AUTOMATION_STRING(ItemStatus, 4)
    OPENXAML_AUTOMATION_STRING(ItemType, 5)
    OPENXAML_AUTOMATION_STRING(Name, 6)
#undef OPENXAML_AUTOMATION_STRING

#define OPENXAML_NULL_AUTOMATION_PROPERTY(name)                        \
    HRESULT STDMETHODCALLTYPE get_##name##Property(                     \
        wux::IDependencyProperty** value) override {                    \
        if (!value) return E_POINTER;                                   \
        *value = nullptr;                                               \
        return S_OK;                                                    \
    }
    OPENXAML_NULL_AUTOMATION_PROPERTY(AcceleratorKey)
    OPENXAML_NULL_AUTOMATION_PROPERTY(AccessKey)
    OPENXAML_NULL_AUTOMATION_PROPERTY(AutomationId)
    OPENXAML_NULL_AUTOMATION_PROPERTY(HelpText)
    OPENXAML_NULL_AUTOMATION_PROPERTY(IsRequiredForForm)
    OPENXAML_NULL_AUTOMATION_PROPERTY(ItemStatus)
    OPENXAML_NULL_AUTOMATION_PROPERTY(ItemType)
    OPENXAML_NULL_AUTOMATION_PROPERTY(LabeledBy)
    OPENXAML_NULL_AUTOMATION_PROPERTY(Name)
    OPENXAML_NULL_AUTOMATION_PROPERTY(LiveSetting)
    OPENXAML_NULL_AUTOMATION_PROPERTY(AccessibilityView)
    OPENXAML_NULL_AUTOMATION_PROPERTY(ControlledPeers)
#undef OPENXAML_NULL_AUTOMATION_PROPERTY
    HRESULT STDMETHODCALLTYPE GetAccessibilityView(
        wux::IDependencyObject*, wuxap::AccessibilityView* value) override {
        if (!value) return E_POINTER;
        *value = wuxap::AccessibilityView_Content;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetAccessibilityView(
        wux::IDependencyObject*, wuxap::AccessibilityView) override {
        return S_OK;
    }

private:
    static HRESULT GetString(wux::IDependencyObject* element, UINT32 property,
                             HSTRING* value) {
        if (!element || !value) return E_INVALIDARG;
        *value = nullptr;
        IOpenXamlAutomationProperties* storage = nullptr;
        const HRESULT hr = element->QueryInterface(
            IID_IOpenXamlAutomationProperties,
            reinterpret_cast<void**>(&storage));
        if (hr == E_NOINTERFACE) return CopyToHString(L"", value);
        if (FAILED(hr)) return hr;
        const HRESULT result = storage->GetAutomationString(property, value);
        storage->Release();
        return result;
    }
    static HRESULT SetString(wux::IDependencyObject* element, UINT32 property,
                             HSTRING value) {
        if (!element) return E_INVALIDARG;
        IOpenXamlAutomationProperties* storage = nullptr;
        const HRESULT hr = element->QueryInterface(
            IID_IOpenXamlAutomationProperties,
            reinterpret_cast<void**>(&storage));
        if (hr == E_NOINTERFACE) return S_OK;
        if (FAILED(hr)) return hr;
        const HRESULT result = storage->SetAutomationString(property, value);
        storage->Release();
        return result;
    }
};

class FontWeightsFactory final
    : public ComObject,
      public IActivationFactory,
      public abi::NotImpl_IFontWeightsStatics {
public:
    const wchar_t* RuntimeClassName() const override {
        return L"Windows.UI.Text.FontWeights";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Text_IFontWeightsStatics,
                        wuit::IFontWeightsStatics)
        OPENXAML_QI_ARM(::openxaml::iid::IActivationFactory, IActivationFactory)
        OPENXAML_QI_ARM(IID_IUnknown, IActivationFactory)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable, IActivationFactory)
        *object = nullptr;
        return TraceQueryInterfaceMiss(RuntimeClassName(), iid);
    }
    OPENXAML_COM_BOILERPLATE()
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable**) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE get_Bold(wuit::FontWeight* value) override {
        if (!value) return E_POINTER;
        *value = {700};
        return S_OK;
    }
};

class ToolTipServiceFactory final
    : public ComObject,
      public IActivationFactory,
      public abi::NotImpl_IToolTipServiceStatics {
public:
    const wchar_t* RuntimeClassName() const override {
        return L"Windows.UI.Xaml.Controls.ToolTipService";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IToolTipServiceStatics,
                        wuxc::IToolTipServiceStatics)
        OPENXAML_QI_ARM(::openxaml::iid::IActivationFactory, IActivationFactory)
        OPENXAML_QI_ARM(IID_IUnknown, IActivationFactory)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable, IActivationFactory)
        *object = nullptr;
        return TraceQueryInterfaceMiss(RuntimeClassName(), iid);
    }
    OPENXAML_COM_BOILERPLATE()
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable**) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE GetToolTip(wux::IDependencyObject*,
                                         IInspectable** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetToolTip(wux::IDependencyObject*,
                                         IInspectable*) override {
        return S_OK;
    }
};

class FlyoutBaseFactory final
    : public ComObject,
      public IActivationFactory,
      public abi::NotImpl_IFlyoutBaseStatics {
public:
    const wchar_t* RuntimeClassName() const override {
        return L"Windows.UI.Xaml.Controls.Primitives.FlyoutBase";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(
            ::openxaml::iid::Windows_UI_Xaml_Controls_Primitives_IFlyoutBaseStatics,
            wuxcp::IFlyoutBaseStatics)
        OPENXAML_QI_ARM(::openxaml::iid::IActivationFactory, IActivationFactory)
        OPENXAML_QI_ARM(IID_IUnknown, IActivationFactory)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable, IActivationFactory)
        *object = nullptr;
        return TraceQueryInterfaceMiss(RuntimeClassName(), iid);
    }
    OPENXAML_COM_BOILERPLATE()
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable**) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE GetAttachedFlyout(wux::IFrameworkElement*,
                                                 wuxcp::IFlyoutBase** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetAttachedFlyout(wux::IFrameworkElement*,
                                                 wuxcp::IFlyoutBase*) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ShowAttachedFlyout(wux::IFrameworkElement*) override {
        return S_OK;
    }
};

class SolidColorBrushActivationFactory final
    : public Factory<SolidColorBrushObject>,
      public abi::NotImpl_ISolidColorBrushFactory {
public:
    SolidColorBrushActivationFactory()
        : Factory(L"Windows.UI.Xaml.Media.SolidColorBrush") {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        return Factory<SolidColorBrushObject>::QueryInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()
    HRESULT STDMETHODCALLTYPE CreateInstanceWithColor(
        ABI::Windows::UI::Color color, wuxm::ISolidColorBrush** value) override {
        if (!value) return E_POINTER;
        auto* brush = new (std::nothrow) SolidColorBrushObject();
        if (!brush) return E_OUTOFMEMORY;
        brush->put_Color(color);
        *value = static_cast<wuxm::ISolidColorBrush*>(brush);
        return S_OK;
    }
protected:
    HRESULT QueryStatics(REFIID iid, void** object) override {
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Media_ISolidColorBrushFactory,
                        wuxm::ISolidColorBrushFactory)
        return E_NOINTERFACE;
    }
};

class PropertyChangedEventArgsActivationFactory final
    : public Factory<PropertyChangedEventArgsObject>,
      public abi::NotImpl_IPropertyChangedEventArgsFactory {
public:
    PropertyChangedEventArgsActivationFactory()
        : Factory(L"Windows.UI.Xaml.Data.PropertyChangedEventArgs") {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        return Factory<PropertyChangedEventArgsObject>::QueryInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()
    HRESULT STDMETHODCALLTYPE CreateInstance(
        HSTRING name, IInspectable*, IInspectable** inner,
        wuxdata::IPropertyChangedEventArgs** value) override {
        if (!inner || !value) return E_POINTER;
        auto* args = new (std::nothrow) PropertyChangedEventArgsObject(name);
        if (!args) return E_OUTOFMEMORY;
        auto* projected = static_cast<wuxdata::IPropertyChangedEventArgs*>(args);
        projected->AddRef();
        *inner = static_cast<IInspectable*>(projected);
        *value = projected;
        return S_OK;
    }
protected:
    HRESULT QueryStatics(REFIID iid, void** object) override {
        OPENXAML_QI_ARM(
            ::openxaml::iid::Windows_UI_Xaml_Data_IPropertyChangedEventArgsFactory,
            wuxdata::IPropertyChangedEventArgsFactory)
        return E_NOINTERFACE;
    }
};

class MenuFlyoutActivationFactory final
    : public Factory<MenuFlyoutObject>,
      public wuxc::IMenuFlyoutFactory {
public:
    MenuFlyoutActivationFactory()
        : Factory(L"Windows.UI.Xaml.Controls.MenuFlyout") {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        return Factory<MenuFlyoutObject>::QueryInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()
    HRESULT STDMETHODCALLTYPE CreateInstance(
        IInspectable*, IInspectable** inner,
        wuxc::IMenuFlyout** value) override {
        if (!inner || !value) return E_POINTER;
        *inner = nullptr;
        *value = nullptr;
        auto* flyout = new (std::nothrow) MenuFlyoutObject();
        if (!flyout) return E_OUTOFMEMORY;
        auto* projected = static_cast<wuxc::IMenuFlyout*>(flyout);
        projected->AddRef();
        *inner = static_cast<IInspectable*>(projected);
        *value = projected;
        return S_OK;
    }
protected:
    HRESULT QueryStatics(REFIID iid, void** object) override {
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IMenuFlyoutFactory,
                        wuxc::IMenuFlyoutFactory)
        return E_NOINTERFACE;
    }
};

class CommandBarFlyoutActivationFactory final
    : public Factory<CommandBarFlyoutObject>,
      public IMuxcCommandBarFlyoutFactory {
public:
    CommandBarFlyoutActivationFactory()
        : Factory(L"Microsoft.UI.Xaml.Controls.CommandBarFlyout") {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        return Factory<CommandBarFlyoutObject>::QueryInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()
    HRESULT STDMETHODCALLTYPE CreateInstance(void*, void** inner, void** value) override {
        if (!inner || !value) return E_POINTER;
        *inner = nullptr;
        *value = nullptr;
        auto* flyout = new (std::nothrow) CommandBarFlyoutObject();
        if (!flyout) return E_OUTOFMEMORY;
        auto* projected = static_cast<IMuxcCommandBarFlyout*>(flyout);
        projected->AddRef();
        *inner = static_cast<IInspectable*>(projected);
        *value = projected;
        return S_OK;
    }
protected:
    HRESULT QueryStatics(REFIID iid, void** object) override {
        OPENXAML_QI_ARM(IID_IMuxcCommandBarFlyoutFactory,
                        IMuxcCommandBarFlyoutFactory)
        return E_NOINTERFACE;
    }
};

class ProgressRingActivationFactory final
    : public Factory<ProgressRingObject>,
      public IMuxcProgressRingFactory {
public:
    ProgressRingActivationFactory()
        : Factory(L"Microsoft.UI.Xaml.Controls.ProgressRing") {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        return Factory<ProgressRingObject>::QueryInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()
    HRESULT STDMETHODCALLTYPE CreateInstance(void*, void** inner, void** value) override {
        if (!inner || !value) return E_POINTER;
        *inner = nullptr;
        *value = nullptr;
        auto* ring = new (std::nothrow) ProgressRingObject();
        if (!ring) return E_OUTOFMEMORY;
        auto* projected = static_cast<IMuxcProgressRing*>(ring);
        projected->AddRef();
        *inner = static_cast<IInspectable*>(projected);
        *value = projected;
        return S_OK;
    }
protected:
    HRESULT QueryStatics(REFIID iid, void** object) override {
        OPENXAML_QI_ARM(IID_IMuxcProgressRingFactory, IMuxcProgressRingFactory)
        return E_NOINTERFACE;
    }
};

class TabViewItemActivationFactory final
    : public Factory<TabViewItemObject>,
      public IMuxcTabViewItemFactory {
public:
    TabViewItemActivationFactory()
        : Factory(L"Microsoft.UI.Xaml.Controls.TabViewItem") {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        return Factory<TabViewItemObject>::QueryInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()
    HRESULT STDMETHODCALLTYPE CreateInstance(void*, void** inner, void** value) override {
        if (!inner || !value) return E_POINTER;
        *inner = nullptr;
        *value = nullptr;
        auto* item = new (std::nothrow) TabViewItemObject();
        if (!item) return E_OUTOFMEMORY;
        auto* projected = static_cast<IMuxcTabViewItem*>(item);
        projected->AddRef();
        *inner = static_cast<IInspectable*>(projected);
        *value = projected;
        return S_OK;
    }
protected:
    HRESULT QueryStatics(REFIID iid, void** object) override {
        OPENXAML_QI_ARM(IID_IMuxcTabViewItemFactory, IMuxcTabViewItemFactory)
        return E_NOINTERFACE;
    }
};

class AppBarButtonActivationFactory final
    : public Factory<AppBarButtonObject>,
      public wuxc::IAppBarButtonFactory {
public:
    AppBarButtonActivationFactory()
        : Factory(L"Windows.UI.Xaml.Controls.AppBarButton") {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        return Factory<AppBarButtonObject>::QueryInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()
    HRESULT STDMETHODCALLTYPE CreateInstance(
        IInspectable*, IInspectable** inner, wuxc::IAppBarButton** value) override {
        if (!inner || !value) return E_POINTER;
        *inner = nullptr;
        *value = nullptr;
        auto* button = new (std::nothrow) AppBarButtonObject();
        if (!button) return E_OUTOFMEMORY;
        auto* projected = static_cast<wuxc::IAppBarButton*>(button);
        projected->AddRef();
        *inner = static_cast<IInspectable*>(projected);
        *value = projected;
        return S_OK;
    }
protected:
    HRESULT QueryStatics(REFIID iid, void** object) override {
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IAppBarButtonFactory,
                        wuxc::IAppBarButtonFactory)
        return E_NOINTERFACE;
    }
};

class ContentDialogActivationFactory final
    : public Factory<ContentDialogObject>,
      public wuxc::IContentDialogFactory {
public:
    ContentDialogActivationFactory()
        : Factory(L"Windows.UI.Xaml.Controls.ContentDialog") {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        return Factory<ContentDialogObject>::QueryInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()
    HRESULT STDMETHODCALLTYPE CreateInstance(
        IInspectable*, IInspectable** inner, wuxc::IContentDialog** value) override {
        if (!inner || !value) return E_POINTER;
        *inner = nullptr;
        *value = nullptr;
        auto* dialog = new (std::nothrow) ContentDialogObject();
        if (!dialog) return E_OUTOFMEMORY;
        auto* projected = static_cast<wuxc::IContentDialog*>(dialog);
        projected->AddRef();
        *inner = static_cast<IInspectable*>(projected);
        *value = projected;
        return S_OK;
    }
protected:
    HRESULT QueryStatics(REFIID iid, void** object) override {
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IContentDialogFactory,
                        wuxc::IContentDialogFactory)
        return E_NOINTERFACE;
    }
};

class MenuFlyoutItemActivationFactory final
    : public Factory<MenuFlyoutItemObject>,
      public wuxc::IMenuFlyoutItemFactory {
public:
    MenuFlyoutItemActivationFactory()
        : Factory(L"Windows.UI.Xaml.Controls.MenuFlyoutItem") {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        return Factory<MenuFlyoutItemObject>::QueryInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()
    HRESULT STDMETHODCALLTYPE CreateInstance(
        IInspectable*, IInspectable** inner,
        wuxc::IMenuFlyoutItem** value) override {
        return CreateComposable<MenuFlyoutItemObject, wuxc::IMenuFlyoutItem>(inner, value);
    }
protected:
    HRESULT QueryStatics(REFIID iid, void** object) override {
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IMenuFlyoutItemFactory,
                        wuxc::IMenuFlyoutItemFactory)
        return E_NOINTERFACE;
    }
private:
    template <class T, class Interface>
    static HRESULT CreateComposable(IInspectable** inner, Interface** value) {
        if (!inner || !value) return E_POINTER;
        *inner = nullptr;
        *value = nullptr;
        auto* object = new (std::nothrow) T();
        if (!object) return E_OUTOFMEMORY;
        auto* projected = static_cast<Interface*>(object);
        projected->AddRef();
        *inner = static_cast<IInspectable*>(projected);
        *value = projected;
        return S_OK;
    }
};

class MenuFlyoutSeparatorActivationFactory final
    : public Factory<MenuFlyoutSeparatorObject>,
      public wuxc::IMenuFlyoutSeparatorFactory {
public:
    MenuFlyoutSeparatorActivationFactory()
        : Factory(L"Windows.UI.Xaml.Controls.MenuFlyoutSeparator") {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        return Factory<MenuFlyoutSeparatorObject>::QueryInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()
    HRESULT STDMETHODCALLTYPE CreateInstance(
        IInspectable*, IInspectable** inner,
        wuxc::IMenuFlyoutSeparator** value) override {
        if (!inner || !value) return E_POINTER;
        *inner = nullptr;
        *value = nullptr;
        auto* object = new (std::nothrow) MenuFlyoutSeparatorObject();
        if (!object) return E_OUTOFMEMORY;
        auto* projected = static_cast<wuxc::IMenuFlyoutSeparator*>(object);
        projected->AddRef();
        *inner = static_cast<IInspectable*>(projected);
        *value = projected;
        return S_OK;
    }
protected:
    HRESULT QueryStatics(REFIID iid, void** object) override {
        OPENXAML_QI_ARM(
            ::openxaml::iid::Windows_UI_Xaml_Controls_IMenuFlyoutSeparatorFactory,
            wuxc::IMenuFlyoutSeparatorFactory)
        return E_NOINTERFACE;
    }
};

class BitmapIconSourceActivationFactory final
    : public Factory<BitmapIconSourceObject>,
      public wuxc::IBitmapIconSourceFactory {
public:
    BitmapIconSourceActivationFactory()
        : Factory(L"Windows.UI.Xaml.Controls.BitmapIconSource") {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        return Factory<BitmapIconSourceObject>::QueryInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()
    HRESULT STDMETHODCALLTYPE CreateInstance(
        IInspectable*, IInspectable** inner,
        wuxc::IBitmapIconSource** value) override {
        return CreateComposableObject<BitmapIconSourceObject>(inner, value);
    }
protected:
    HRESULT QueryStatics(REFIID iid, void** object) override {
        OPENXAML_QI_ARM(
            ::openxaml::iid::Windows_UI_Xaml_Controls_IBitmapIconSourceFactory,
            wuxc::IBitmapIconSourceFactory)
        return E_NOINTERFACE;
    }
};

class IconSourceElementActivationFactory final
    : public Factory<IconSourceElementObject>,
      public wuxc::IIconSourceElementFactory {
public:
    IconSourceElementActivationFactory()
        : Factory(L"Windows.UI.Xaml.Controls.IconSourceElement") {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        return Factory<IconSourceElementObject>::QueryInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()
    HRESULT STDMETHODCALLTYPE CreateInstance(
        IInspectable*, IInspectable** inner,
        wuxc::IIconSourceElement** value) override {
        return CreateComposableObject<IconSourceElementObject>(inner, value);
    }
protected:
    HRESULT QueryStatics(REFIID iid, void** object) override {
        OPENXAML_QI_ARM(
            ::openxaml::iid::Windows_UI_Xaml_Controls_IIconSourceElementFactory,
            wuxc::IIconSourceElementFactory)
        return E_NOINTERFACE;
    }
};

class ToolTipActivationFactory final
    : public Factory<ToolTipObject>,
      public wuxc::IToolTipFactory {
public:
    ToolTipActivationFactory() : Factory(L"Windows.UI.Xaml.Controls.ToolTip") {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        return Factory<ToolTipObject>::QueryInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()
    HRESULT STDMETHODCALLTYPE CreateInstance(
        IInspectable*, IInspectable** inner, wuxc::IToolTip** value) override {
        return CreateComposableObject<ToolTipObject>(inner, value);
    }
protected:
    HRESULT QueryStatics(REFIID iid, void** object) override {
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IToolTipFactory,
                        wuxc::IToolTipFactory)
        return E_NOINTERFACE;
    }
};

class FontIconActivationFactory final
    : public Factory<FontIconObject>,
      public wuxc::IFontIconFactory {
public:
    FontIconActivationFactory() : Factory(L"Windows.UI.Xaml.Controls.FontIcon") {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        return Factory<FontIconObject>::QueryInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()
    HRESULT STDMETHODCALLTYPE CreateInstance(
        IInspectable*, IInspectable** inner, wuxc::IFontIcon** value) override {
        return CreateComposableObject<FontIconObject>(inner, value);
    }
protected:
    HRESULT QueryStatics(REFIID iid, void** object) override {
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IFontIconFactory,
                        wuxc::IFontIconFactory)
        return E_NOINTERFACE;
    }
};

class SymbolIconActivationFactory final
    : public Factory<SymbolIconObject>,
      public wuxc::ISymbolIconFactory,
      public wuxc::ISymbolIconStatics {
public:
    SymbolIconActivationFactory() : Factory(L"Windows.UI.Xaml.Controls.SymbolIcon") {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        return Factory<SymbolIconObject>::QueryInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()
    HRESULT STDMETHODCALLTYPE CreateInstanceWithSymbol(
        wuxc::Symbol symbol, wuxc::ISymbolIcon** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto* object = new (std::nothrow) SymbolIconObject(symbol);
        if (!object) return E_OUTOFMEMORY;
        *value = static_cast<wuxc::ISymbolIcon*>(object);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_SymbolProperty(wux::IDependencyProperty** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return S_OK;
    }
protected:
    HRESULT QueryStatics(REFIID iid, void** object) override {
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_ISymbolIconFactory,
                        wuxc::ISymbolIconFactory)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_ISymbolIconStatics,
                        wuxc::ISymbolIconStatics)
        return E_NOINTERFACE;
    }
};

// Grid's attached properties. Row, Column, RowSpan and ColumnSpan live on any
// element, not just a Grid's children, so they are stored on the element and
// read by whichever Grid happens to be the parent.
//
// The `*Property` getters stay E_NOTIMPL. There is a property system behind
// this now -- Get and Set go through it -- but its DependencyProperty is a
// plain C++ object and the ABI wants a Windows.UI.Xaml.DependencyProperty,
// which is a runtime class this DLL does not project.
class GridFactory : public Factory<GridObject>,
                    public abi::NotImpl_IGridStatics,
                    public abi::NotImpl_IGridFactory {
public:
    GridFactory() : Factory(L"Windows.UI.Xaml.Controls.Grid") {}

    HRESULT STDMETHODCALLTYPE GetRow(ABI::Windows::UI::Xaml::IFrameworkElement* element,
                                     INT32* value) override {
        return Read(element, value, openxaml::Grid::RowProperty());
    }
    HRESULT STDMETHODCALLTYPE SetRow(ABI::Windows::UI::Xaml::IFrameworkElement* element,
                                     INT32 value) override {
        return Write(element, value, openxaml::Grid::RowProperty());
    }
    HRESULT STDMETHODCALLTYPE GetColumn(ABI::Windows::UI::Xaml::IFrameworkElement* element,
                                        INT32* value) override {
        return Read(element, value, openxaml::Grid::ColumnProperty());
    }
    HRESULT STDMETHODCALLTYPE SetColumn(ABI::Windows::UI::Xaml::IFrameworkElement* element,
                                        INT32 value) override {
        return Write(element, value, openxaml::Grid::ColumnProperty());
    }
    HRESULT STDMETHODCALLTYPE GetRowSpan(ABI::Windows::UI::Xaml::IFrameworkElement* element,
                                         INT32* value) override {
        return Read(element, value, openxaml::Grid::RowSpanProperty());
    }
    HRESULT STDMETHODCALLTYPE SetRowSpan(ABI::Windows::UI::Xaml::IFrameworkElement* element,
                                         INT32 value) override {
        return Write(element, value, openxaml::Grid::RowSpanProperty());
    }
    HRESULT STDMETHODCALLTYPE GetColumnSpan(ABI::Windows::UI::Xaml::IFrameworkElement* element,
                                            INT32* value) override {
        return Read(element, value, openxaml::Grid::ColumnSpanProperty());
    }
    HRESULT STDMETHODCALLTYPE SetColumnSpan(ABI::Windows::UI::Xaml::IFrameworkElement* element,
                                            INT32 value) override {
        return Write(element, value, openxaml::Grid::ColumnSpanProperty());
    }
    HRESULT STDMETHODCALLTYPE CreateInstance(IInspectable* base_interface,
                                             IInspectable** inner_interface,
                                             wuxc::IGrid** value) override {
        if (!inner_interface || !value) return E_POINTER;
        // A non-null outer is the normal path for a generated custom control
        // deriving from Grid. C++/WinRT keeps the returned inner interface in
        // its composing base, so retaining the outer here would create a
        // cycle; the inner simply supplies Grid's implementation.
        (void)base_interface;
        auto* grid = new GridObject();
        auto* projected = static_cast<wuxc::IGrid*>(grid);
        projected->AddRef();
        *inner_interface = static_cast<IInspectable*>(projected);
        *value = projected;
        return S_OK;
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
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IGridFactory,
                        ABI::Windows::UI::Xaml::Controls::IGridFactory)
        return E_NOINTERFACE;
    }

private:
    using Attached = const openxaml::DependencyProperty&;

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
                        Attached property) {
        if (!value) return E_POINTER;
        openxaml::Element* layout = Unwrap(element);
        if (!layout) return E_INVALIDARG;
        *value = layout->GetInt(property);
        return S_OK;
    }

    static HRESULT Write(ABI::Windows::UI::Xaml::IFrameworkElement* element, INT32 value,
                         Attached property) {
        openxaml::Element* layout = Unwrap(element);
        if (!layout) return E_INVALIDARG;
        layout->SetValue(property, value);
        return S_OK;
    }
};

class DependencyPropertyObject final : public ComObject,
                                       public abi::NotImpl_IDependencyProperty {
public:
    const wchar_t* RuntimeClassName() const override {
        return L"Windows.UI.Xaml.DependencyProperty";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_IDependencyProperty,
                        wux::IDependencyProperty)
        OPENXAML_QI_ARM(IID_IUnknown, wux::IDependencyProperty)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable, wux::IDependencyProperty)
        *object = nullptr;
        return E_NOINTERFACE;
    }
    OPENXAML_COM_BOILERPLATE()
};

class PanelStaticsFactory final : public ComObject,
                                  public IActivationFactory,
                                  public abi::NotImpl_IPanelStatics {
public:
    const wchar_t* RuntimeClassName() const override {
        return L"Windows.UI.Xaml.Controls.Panel";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IPanelStatics,
                        wuxc::IPanelStatics)
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
    HRESULT STDMETHODCALLTYPE get_BackgroundProperty(wux::IDependencyProperty** value) override {
        return Null(value);
    }
    HRESULT STDMETHODCALLTYPE get_IsItemsHostProperty(wux::IDependencyProperty** value) override {
        return Null(value);
    }
    HRESULT STDMETHODCALLTYPE get_ChildrenTransitionsProperty(
        wux::IDependencyProperty** value) override {
        return Null(value);
    }

private:
    static HRESULT Null(wux::IDependencyProperty** value) {
        if (!value) return E_POINTER;
        *value = new DependencyPropertyObject();
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

// DurationHelper is a value-type helper rather than a visual element. The
// generated Terminal XAML uses FromTimeSpan for pane animations during app
// startup, so its statics must be available before any window is created.
class DurationHelperObject final : public ComObject,
                                   public abi::NotImpl_IDurationHelper {
public:
    using PrimaryInterface = wux::IDurationHelper;
    const wchar_t* RuntimeClassName() const override {
        return L"Windows.UI.Xaml.DurationHelper";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_IDurationHelper,
                        wux::IDurationHelper)
        OPENXAML_QI_ARM(IID_IUnknown, wux::IDurationHelper)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable, wux::IDurationHelper)
        *object = nullptr;
        return E_NOINTERFACE;
    }
    OPENXAML_COM_BOILERPLATE()
};

class DurationHelperFactory final : public Factory<DurationHelperObject>,
                                    public abi::NotImpl_IDurationHelperStatics {
public:
    DurationHelperFactory() : Factory(L"Windows.UI.Xaml.DurationHelper") {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        return Factory<DurationHelperObject>::QueryInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE get_Automatic(wux::Duration* value) override {
        return WriteSpecial(value, wux::DurationType_Automatic);
    }
    HRESULT STDMETHODCALLTYPE get_Forever(wux::Duration* value) override {
        return WriteSpecial(value, wux::DurationType_Forever);
    }
    HRESULT STDMETHODCALLTYPE Compare(wux::Duration left, wux::Duration right,
                                      INT32* result) override {
        if (!result) return E_POINTER;
        if (!Valid(left) || !Valid(right)) return E_INVALIDARG;
        if (left.Type == wux::DurationType_Automatic ||
            right.Type == wux::DurationType_Automatic) {
            *result = left.Type == right.Type ? 0 :
                      left.Type == wux::DurationType_Automatic ? -1 : 1;
        } else if (left.Type == wux::DurationType_Forever ||
                   right.Type == wux::DurationType_Forever) {
            *result = left.Type == right.Type ? 0 :
                      left.Type == wux::DurationType_Forever ? 1 : -1;
        } else {
            *result = left.TimeSpan.Duration < right.TimeSpan.Duration ? -1 :
                      left.TimeSpan.Duration > right.TimeSpan.Duration ? 1 : 0;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE FromTimeSpan(wf::TimeSpan time_span,
                                           wux::Duration* result) override {
        if (!result) return E_POINTER;
        if (time_span.Duration < 0) return E_INVALIDARG;
        *result = { time_span, wux::DurationType_TimeSpan };
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetHasTimeSpan(wux::Duration target,
                                             boolean* result) override {
        if (!result) return E_POINTER;
        if (!Valid(target)) return E_INVALIDARG;
        *result = target.Type == wux::DurationType_TimeSpan;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Add(wux::Duration left, wux::Duration right,
                                  wux::Duration* result) override {
        if (!result) return E_POINTER;
        if (!Valid(left) || !Valid(right)) return E_INVALIDARG;
        if (left.Type == wux::DurationType_Automatic ||
            right.Type == wux::DurationType_Automatic) {
            return WriteSpecial(result, wux::DurationType_Automatic);
        }
        if (left.Type == wux::DurationType_Forever ||
            right.Type == wux::DurationType_Forever) {
            return WriteSpecial(result, wux::DurationType_Forever);
        }
        if (right.TimeSpan.Duration >
            std::numeric_limits<INT64>::max() - left.TimeSpan.Duration) {
            return E_INVALIDARG;
        }
        result->TimeSpan.Duration = left.TimeSpan.Duration + right.TimeSpan.Duration;
        result->Type = wux::DurationType_TimeSpan;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Equals(wux::Duration left, wux::Duration right,
                                     boolean* result) override {
        if (!result) return E_POINTER;
        if (!Valid(left) || !Valid(right)) return E_INVALIDARG;
        *result = left.Type == right.Type &&
                  (left.Type != wux::DurationType_TimeSpan ||
                   left.TimeSpan.Duration == right.TimeSpan.Duration);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Subtract(wux::Duration left, wux::Duration right,
                                       wux::Duration* result) override {
        if (!result) return E_POINTER;
        if (!Valid(left) || !Valid(right)) return E_INVALIDARG;
        if (left.Type == wux::DurationType_TimeSpan &&
            right.Type == wux::DurationType_TimeSpan) {
            if (left.TimeSpan.Duration < right.TimeSpan.Duration) return E_INVALIDARG;
            result->TimeSpan.Duration = left.TimeSpan.Duration - right.TimeSpan.Duration;
            result->Type = wux::DurationType_TimeSpan;
            return S_OK;
        }
        if (left.Type == wux::DurationType_Forever &&
            right.Type == wux::DurationType_TimeSpan) {
            return WriteSpecial(result, wux::DurationType_Forever);
        }
        return WriteSpecial(result, wux::DurationType_Automatic);
    }

protected:
    HRESULT QueryStatics(REFIID iid, void** object) override {
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_IDurationHelperStatics,
                        wux::IDurationHelperStatics)
        return E_NOINTERFACE;
    }

private:
    static bool Valid(const wux::Duration& value) {
        return value.Type >= wux::DurationType_Automatic &&
               value.Type <= wux::DurationType_Forever &&
               (value.Type != wux::DurationType_TimeSpan ||
                value.TimeSpan.Duration >= 0);
    }
    static HRESULT WriteSpecial(wux::Duration* value, wux::DurationType type) {
        if (!value) return E_POINTER;
        value->TimeSpan.Duration = 0;
        value->Type = type;
        return S_OK;
    }
};

class GridLengthHelperObject final : public ComObject,
                                     public abi::NotImpl_IGridLengthHelper {
public:
    using PrimaryInterface = wux::IGridLengthHelper;
    const wchar_t* RuntimeClassName() const override {
        return L"Windows.UI.Xaml.GridLengthHelper";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_IGridLengthHelper,
                        wux::IGridLengthHelper)
        OPENXAML_QI_ARM(IID_IUnknown, wux::IGridLengthHelper)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable, wux::IGridLengthHelper)
        *object = nullptr;
        return E_NOINTERFACE;
    }
    OPENXAML_COM_BOILERPLATE()
};

class GridLengthHelperFactory final
    : public Factory<GridLengthHelperObject>,
      public abi::NotImpl_IGridLengthHelperStatics {
public:
    GridLengthHelperFactory() : Factory(L"Windows.UI.Xaml.GridLengthHelper") {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        return Factory<GridLengthHelperObject>::QueryInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE get_Auto(wux::GridLength* value) override {
        return Write(1.0, wux::GridUnitType_Auto, value);
    }
    HRESULT STDMETHODCALLTYPE FromPixels(DOUBLE pixels,
                                         wux::GridLength* result) override {
        return Write(pixels, wux::GridUnitType_Pixel, result);
    }
    HRESULT STDMETHODCALLTYPE FromValueAndType(DOUBLE value, wux::GridUnitType type,
                                                wux::GridLength* result) override {
        return Write(value, type, result);
    }
    HRESULT STDMETHODCALLTYPE GetIsAbsolute(wux::GridLength target,
                                             boolean* result) override {
        return Is(target, wux::GridUnitType_Pixel, result);
    }
    HRESULT STDMETHODCALLTYPE GetIsAuto(wux::GridLength target,
                                         boolean* result) override {
        return Is(target, wux::GridUnitType_Auto, result);
    }
    HRESULT STDMETHODCALLTYPE GetIsStar(wux::GridLength target,
                                         boolean* result) override {
        return Is(target, wux::GridUnitType_Star, result);
    }
    HRESULT STDMETHODCALLTYPE Equals(wux::GridLength target, wux::GridLength value,
                                      boolean* result) override {
        if (!result) return E_POINTER;
        if (!Valid(target.Value, target.GridUnitType) ||
            !Valid(value.Value, value.GridUnitType)) return E_INVALIDARG;
        *result = target.Value == value.Value &&
                  target.GridUnitType == value.GridUnitType;
        return S_OK;
    }

protected:
    HRESULT QueryStatics(REFIID iid, void** object) override {
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_IGridLengthHelperStatics,
                        wux::IGridLengthHelperStatics)
        return E_NOINTERFACE;
    }

private:
    static bool Valid(DOUBLE value, wux::GridUnitType type) {
        return std::isfinite(value) && value >= 0 &&
               type >= wux::GridUnitType_Auto && type <= wux::GridUnitType_Star;
    }
    static HRESULT Write(DOUBLE value, wux::GridUnitType type,
                         wux::GridLength* result) {
        if (!result) return E_POINTER;
        if (!Valid(value, type)) return E_INVALIDARG;
        *result = {value, type};
        return S_OK;
    }
    static HRESULT Is(wux::GridLength target, wux::GridUnitType type,
                      boolean* result) {
        if (!result) return E_POINTER;
        if (!Valid(target.Value, target.GridUnitType)) return E_INVALIDARG;
        *result = target.GridUnitType == type;
        return S_OK;
    }
};

// Private bridge used by the XBF adapter. ResourceDictionary's public map and
// vector interfaces have parameterized IIDs; keeping mutation behind this
// in-DLL interface makes graph construction independent of compiler UUID
// attributes while the public ABI remains available for callers.
inline constexpr GUID IID_IOpenXamlResourceDictionary = {
    0x6f70656e, 0x7861, 0x6d6c, {0x9e, 0x03, 0x72, 0x65, 0x73, 0x64, 0x69, 0x63}};
inline constexpr GUID IID_IResourceDictionary = {
    0xc1ea4f24, 0xd6de, 0x4191, {0x8e, 0x3a, 0xf4, 0x86, 0x01, 0xf7, 0x48, 0x9c}};

struct IOpenXamlResourceDictionary : ::IUnknown {
    virtual HRESULT AppendMerged(IInspectable* value) = 0;
    virtual HRESULT InsertTheme(const char* key, IInspectable* value) = 0;
    virtual HRESULT InsertResource(const char* key, IInspectable* value) = 0;
    virtual HRESULT AliasResource(const char* key, const char* target) = 0;
    virtual HRESULT SetSourceText(const char* value) = 0;
};

using InspectablePairAbi = __FIKeyValuePair_2_IInspectable_IInspectable;
using InspectablePairIteratorAbi =
    __FIIterator_1___FIKeyValuePair_2_IInspectable_IInspectable;
using InspectablePairIterableAbi =
    __FIIterable_1___FIKeyValuePair_2_IInspectable_IInspectable;
using InspectableMapAbi = __FIMap_2_IInspectable_IInspectable;
using InspectableMapViewAbi = __FIMapView_2_IInspectable_IInspectable;

class BoxedStringObject final : public ComObject,
                                public abi::NotImpl_IPropertyValue {
public:
    explicit BoxedStringObject(std::string value) : value_(std::move(value)) {}
    const wchar_t* RuntimeClassName() const override {
        return L"Windows.Foundation.IReference`1<String>";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_Foundation_IPropertyValue,
                        wf::IPropertyValue)
        OPENXAML_QI_ARM(IID_IUnknown, wf::IPropertyValue)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable, wf::IPropertyValue)
        *object = nullptr;
        return TraceQueryInterfaceMiss(RuntimeClassName(), iid);
    }
    OPENXAML_COM_BOILERPLATE()
    HRESULT STDMETHODCALLTYPE get_Type(wf::PropertyType* value) override {
        if (!value) return E_POINTER;
        *value = wf::PropertyType_String;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsNumericScalar(boolean* value) override {
        if (!value) return E_POINTER;
        *value = 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetString(HSTRING* value) override {
        return HStringFromUtf8(value_, value);
    }
private:
    std::string value_;
};

inline bool InspectableString(IInspectable* value, std::wstring* text) {
    if (!value || !text) return false;
    wf::IPropertyValue* property = nullptr;
    if (FAILED(value->QueryInterface(
            ::openxaml::iid::Windows_Foundation_IPropertyValue,
            reinterpret_cast<void**>(&property)))) return false;
    wf::PropertyType type{};
    HSTRING string = nullptr;
    const HRESULT hr = property->get_Type(&type);
    const HRESULT string_hr =
        SUCCEEDED(hr) && type == wf::PropertyType_String
            ? property->GetString(&string) : E_NOINTERFACE;
    property->Release();
    if (FAILED(string_hr)) return false;
    UINT32 length = 0;
    const wchar_t* raw = WindowsGetStringRawBuffer(string, &length);
    text->assign(raw ? raw : L"", length);
    WindowsDeleteString(string);
    return true;
}

inline bool InspectableKeyEqual(IInspectable* left, IInspectable* right) {
    if (left == right) return true;
    std::wstring left_text;
    std::wstring right_text;
    return InspectableString(left, &left_text) &&
           InspectableString(right, &right_text) && left_text == right_text;
}

class InspectableKeyValuePair final : public ComObject,
                                      public InspectablePairAbi {
public:
    InspectableKeyValuePair(IInspectable* key, IInspectable* value)
        : key_(key), value_(value) {
        key_->AddRef();
        value_->AddRef();
    }
    ~InspectableKeyValuePair() override {
        key_->Release();
        value_->Release();
    }
    const wchar_t* RuntimeClassName() const override {
        return L"Windows.Foundation.Collections.IKeyValuePair`2<Object,Object>";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::PIID_FIKeyValuePair_2_IInspectable_IInspectable,
                        InspectablePairAbi)
        OPENXAML_QI_ARM(IID_IUnknown, InspectablePairAbi)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable, InspectablePairAbi)
        *object = nullptr;
        return TraceQueryInterfaceMiss(RuntimeClassName(), iid);
    }
    OPENXAML_COM_BOILERPLATE()
    HRESULT STDMETHODCALLTYPE get_Key(IInspectable** value) override {
        if (!value) return E_POINTER;
        key_->AddRef();
        *value = key_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Value(IInspectable** value) override {
        if (!value) return E_POINTER;
        value_->AddRef();
        *value = value_;
        return S_OK;
    }
private:
    IInspectable* key_;
    IInspectable* value_;
};

class InspectableMap;

class InspectableMapIterator final : public ComObject,
                                     public InspectablePairIteratorAbi {
public:
    explicit InspectableMapIterator(InspectableMap* owner);
    ~InspectableMapIterator() override;
    const wchar_t* RuntimeClassName() const override {
        return L"Windows.Foundation.Collections.IIterator`1<IKeyValuePair<Object,Object>>";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(
            ::openxaml::iid::PIID_FIIterator_1___FIKeyValuePair_2_IInspectable_IInspectable,
            InspectablePairIteratorAbi)
        OPENXAML_QI_ARM(IID_IUnknown, InspectablePairIteratorAbi)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable, InspectablePairIteratorAbi)
        *object = nullptr;
        return TraceQueryInterfaceMiss(RuntimeClassName(), iid);
    }
    OPENXAML_COM_BOILERPLATE()
    HRESULT STDMETHODCALLTYPE get_Current(InspectablePairAbi** value) override;
    HRESULT STDMETHODCALLTYPE get_HasCurrent(boolean* value) override;
    HRESULT STDMETHODCALLTYPE MoveNext(boolean* value) override;
    HRESULT STDMETHODCALLTYPE GetMany(unsigned capacity, InspectablePairAbi** values,
                                      unsigned* actual) override;
private:
    InspectableMap* owner_;
    unsigned position_ = 0;
};

class InspectableMap final : public ComObject,
                             public InspectableMapAbi,
                             public InspectableMapViewAbi,
                             public InspectablePairIterableAbi {
    using Entry = std::pair<IInspectable*, IInspectable*>;
    using EntryIterator = std::vector<Entry>::iterator;
public:
    explicit InspectableMap(ComObject* owner = nullptr) : owner_(owner) {}
    ~InspectableMap() override { Clear(); }
    ULONG Retain() override { return owner_ ? owner_->Retain() : ComObject::Retain(); }
    ULONG ReleaseOne() override {
        return owner_ ? owner_->ReleaseOne() : ComObject::ReleaseOne();
    }
    const wchar_t* RuntimeClassName() const override {
        return L"Windows.Foundation.Collections.IMap`2<Object,Object>";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::PIID_FIMap_2_IInspectable_IInspectable,
                        InspectableMapAbi)
        OPENXAML_QI_ARM(::openxaml::iid::PIID_FIMapView_2_IInspectable_IInspectable,
                        InspectableMapViewAbi)
        OPENXAML_QI_ARM(
            ::openxaml::iid::PIID_FIIterable_1___FIKeyValuePair_2_IInspectable_IInspectable,
            InspectablePairIterableAbi)
        OPENXAML_QI_ARM(IID_IUnknown, InspectableMapAbi)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable, InspectableMapAbi)
        *object = nullptr;
        return TraceQueryInterfaceMiss(RuntimeClassName(), iid);
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE Lookup(IInspectable* key, IInspectable** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        const auto found = Find(key);
        if (found == entries_.end()) return E_BOUNDS;
        found->second->AddRef();
        *value = found->second;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Size(unsigned* value) override {
        if (!value) return E_POINTER;
        *value = static_cast<unsigned>(entries_.size());
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE HasKey(IInspectable* key, boolean* value) override {
        if (!value) return E_POINTER;
        *value = Find(key) != entries_.end();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetView(InspectableMapViewAbi** value) override {
        if (!value) return E_POINTER;
        *value = static_cast<InspectableMapViewAbi*>(this);
        Retain();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Insert(IInspectable* key, IInspectable* value,
                                     boolean* replaced) override {
        if (!key || !value || !replaced) return E_INVALIDARG;
        auto found = Find(key);
        *replaced = found != entries_.end();
        key->AddRef();
        value->AddRef();
        if (found != entries_.end()) {
            found->first->Release();
            found->second->Release();
            *found = {key, value};
        } else {
            entries_.push_back({key, value});
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Remove(IInspectable* key) override {
        auto found = Find(key);
        if (found == entries_.end()) return E_BOUNDS;
        found->first->Release();
        found->second->Release();
        entries_.erase(found);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Clear() override {
        for (auto& [key, value] : entries_) {
            key->Release();
            value->Release();
        }
        entries_.clear();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Split(InspectableMapViewAbi** first,
                                    InspectableMapViewAbi** second) override {
        if (!first || !second) return E_POINTER;
        *first = static_cast<InspectableMapViewAbi*>(this);
        Retain();
        *second = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE First(InspectablePairIteratorAbi** value) override {
        if (!value) return E_POINTER;
        *value = new (std::nothrow) InspectableMapIterator(this);
        return *value ? S_OK : E_OUTOFMEMORY;
    }

    HRESULT InsertText(const char* key, IInspectable* value) {
        auto* boxed = new (std::nothrow) BoxedStringObject(key ? key : "");
        if (!boxed) return E_OUTOFMEMORY;
        boolean replaced = 0;
        const HRESULT hr = Insert(static_cast<wf::IPropertyValue*>(boxed), value,
                                  &replaced);
        boxed->Release();
        return hr;
    }
    unsigned Count() const { return static_cast<unsigned>(entries_.size()); }
    HRESULT PairAt(unsigned index, InspectablePairAbi** value) {
        if (!value) return E_POINTER;
        if (index >= entries_.size()) return E_BOUNDS;
        *value = new (std::nothrow) InspectableKeyValuePair(
            entries_[index].first, entries_[index].second);
        return *value ? S_OK : E_OUTOFMEMORY;
    }
private:
    EntryIterator Find(IInspectable* key) {
        return std::find_if(entries_.begin(), entries_.end(),
            [key](const Entry& entry) { return InspectableKeyEqual(entry.first, key); });
    }
    ComObject* owner_;
    std::vector<Entry> entries_;
};

InspectableMapIterator::InspectableMapIterator(InspectableMap* owner) : owner_(owner) {
    owner_->Retain();
}
InspectableMapIterator::~InspectableMapIterator() { owner_->ReleaseOne(); }
HRESULT InspectableMapIterator::get_Current(InspectablePairAbi** value) {
    return owner_->PairAt(position_, value);
}
HRESULT InspectableMapIterator::get_HasCurrent(boolean* value) {
    if (!value) return E_POINTER;
    *value = position_ < owner_->Count();
    return S_OK;
}
HRESULT InspectableMapIterator::MoveNext(boolean* value) {
    if (!value) return E_POINTER;
    if (position_ < owner_->Count()) ++position_;
    *value = position_ < owner_->Count();
    return S_OK;
}
HRESULT InspectableMapIterator::GetMany(unsigned capacity, InspectablePairAbi** values,
                                        unsigned* actual) {
    if (!actual || (capacity && !values)) return E_POINTER;
    *actual = 0;
    while (*actual < capacity && position_ < owner_->Count()) {
        HRESULT hr = owner_->PairAt(position_++, &values[*actual]);
        if (FAILED(hr)) return hr;
        ++*actual;
    }
    return S_OK;
}

using ResourceDictionaryVector = Vector<
    __FIVector_1_Windows__CUI__CXaml__CResourceDictionary,
    __FIIterable_1_Windows__CUI__CXaml__CResourceDictionary,
    __FIIterator_1_Windows__CUI__CXaml__CResourceDictionary,
    wux::IResourceDictionary,
    __FIVectorView_1_Windows__CUI__CXaml__CResourceDictionary>;

class ResourceDictionaryObject : public ComObject,
                                 public abi::NotImpl_IDependencyObject,
                                 public wux::IResourceDictionary,
                                 public InspectableMapAbi,
                                 public InspectableMapViewAbi,
                                 public InspectablePairIterableAbi,
                                 public IOpenXamlResourceDictionary {
public:
    using PrimaryInterface = wux::IResourceDictionary;
    explicit ResourceDictionaryObject(
        const wchar_t* name = L"Windows.UI.Xaml.ResourceDictionary") : name_(name) {}
    ~ResourceDictionaryObject() override {
        if (source_) source_->Release();
    }

    const wchar_t* RuntimeClassName() const override { return name_; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(IID_IResourceDictionary, wux::IResourceDictionary)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_IDependencyObject,
                        wux::IDependencyObject)
        OPENXAML_QI_ARM(::openxaml::iid::PIID_FIMap_2_IInspectable_IInspectable,
                        InspectableMapAbi)
        OPENXAML_QI_ARM(::openxaml::iid::PIID_FIMapView_2_IInspectable_IInspectable,
                        InspectableMapViewAbi)
        OPENXAML_QI_ARM(
            ::openxaml::iid::PIID_FIIterable_1___FIKeyValuePair_2_IInspectable_IInspectable,
            InspectablePairIterableAbi)
        OPENXAML_QI_ARM(IID_IUnknown, wux::IResourceDictionary)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable, wux::IResourceDictionary)
        if (IsEqualGUID(iid, IID_IOpenXamlResourceDictionary)) {
            auto* pointer = static_cast<IOpenXamlResourceDictionary*>(this);
            static_cast<wux::IResourceDictionary*>(this)->AddRef();
            *object = pointer;
            return S_OK;
        }
        *object = nullptr;
        return TraceQueryInterfaceMiss(RuntimeClassName(), iid);
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE get_Source(wf::IUriRuntimeClass** value) override {
        if (!value) return E_POINTER;
        *value = source_;
        if (*value) (*value)->AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Source(wf::IUriRuntimeClass* value) override {
        if (value) value->AddRef();
        if (source_) source_->Release();
        source_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_MergedDictionaries(
        __FIVector_1_Windows__CUI__CXaml__CResourceDictionary** value) override {
        if (!value) return E_POINTER;
        merged_.AddRef();
        *value = &merged_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_ThemeDictionaries(
        __FIMap_2_IInspectable_IInspectable** value) override {
        if (!value) return E_POINTER;
        theme_dictionaries_.AddRef();
        *value = &theme_dictionaries_;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Lookup(IInspectable* key, IInspectable** value) override {
        std::wstring trace_key;
        const bool trace = GetEnvironmentVariableW(L"OPENXAML_TRACE_QI", nullptr, 0) &&
                           InspectableString(key, &trace_key);
        if (trace) {
            char line[256]{};
            std::snprintf(line, sizeof(line),
                          "OpenXaml: resource lookup key=%ls root=%u themes=%u\n",
                          trace_key.c_str(), resources_.Count(),
                          theme_dictionaries_.Count());
            OutputDebugStringA(line);
        }
        HRESULT hr = resources_.Lookup(key, value);
        if (hr != E_BOUNDS) return hr;
        // Later merged dictionaries have higher precedence, matching XAML's
        // resource lookup order.
        for (unsigned index = merged_.Count(); index > 0; --index) {
            wux::IResourceDictionary* merged = nullptr;
            if (FAILED(merged_.GetAt(index - 1, &merged))) continue;
            InspectableMapAbi* merged_map = nullptr;
            hr = merged->QueryInterface(
                ::openxaml::iid::PIID_FIMap_2_IInspectable_IInspectable,
                reinterpret_cast<void**>(&merged_map));
            merged->Release();
            if (SUCCEEDED(hr)) {
                hr = merged_map->Lookup(key, value);
                merged_map->Release();
                if (SUCCEEDED(hr)) return hr;
            }
        }
        // ResourceDictionary.Lookup applies the active theme automatically.
        // Wine does not expose UISettings' color-scheme contract yet, so use
        // Terminal's dark default, then light, before reporting a miss.
        for (const char* theme : {"Dark", "Light", "HighContrast"}) {
            auto* boxed = new (std::nothrow) BoxedStringObject(theme);
            if (!boxed) return E_OUTOFMEMORY;
            IInspectable* themed_value = nullptr;
            hr = theme_dictionaries_.Lookup(
                static_cast<wf::IPropertyValue*>(boxed), &themed_value);
            boxed->Release();
            if (FAILED(hr)) continue;
            InspectableMapAbi* themed_map = nullptr;
            hr = themed_value->QueryInterface(
                ::openxaml::iid::PIID_FIMap_2_IInspectable_IInspectable,
                reinterpret_cast<void**>(&themed_map));
            themed_value->Release();
            if (SUCCEEDED(hr)) {
                hr = themed_map->Lookup(key, value);
                themed_map->Release();
                if (SUCCEEDED(hr)) {
                    if (trace) OutputDebugStringA("OpenXaml: resource theme hit\n");
                    return hr;
                }
            }
        }
        *value = nullptr;
        return E_BOUNDS;
    }
    HRESULT STDMETHODCALLTYPE get_Size(unsigned* value) override {
        return resources_.get_Size(value);
    }
    HRESULT STDMETHODCALLTYPE HasKey(IInspectable* key, boolean* value) override {
        if (!value) return E_POINTER;
        IInspectable* found = nullptr;
        const HRESULT hr = Lookup(key, &found);
        if (SUCCEEDED(hr)) found->Release();
        *value = SUCCEEDED(hr);
        return hr == E_BOUNDS ? S_OK : hr;
    }
    HRESULT STDMETHODCALLTYPE GetView(InspectableMapViewAbi** value) override {
        return resources_.GetView(value);
    }
    HRESULT STDMETHODCALLTYPE Insert(IInspectable* key, IInspectable* value,
                                     boolean* replaced) override {
        return resources_.Insert(key, value, replaced);
    }
    HRESULT STDMETHODCALLTYPE Remove(IInspectable* key) override {
        return resources_.Remove(key);
    }
    HRESULT STDMETHODCALLTYPE Clear() override { return resources_.Clear(); }
    HRESULT STDMETHODCALLTYPE Split(InspectableMapViewAbi** first,
                                    InspectableMapViewAbi** second) override {
        return resources_.Split(first, second);
    }
    HRESULT STDMETHODCALLTYPE First(InspectablePairIteratorAbi** value) override {
        return resources_.First(value);
    }

    HRESULT AppendMerged(IInspectable* value) override {
        if (!value) return E_INVALIDARG;
        wux::IResourceDictionary* dictionary = nullptr;
        HRESULT hr = value->QueryInterface(IID_IResourceDictionary,
                                            reinterpret_cast<void**>(&dictionary));
        if (FAILED(hr)) return hr;
        const HRESULT append_hr = merged_.Append(dictionary);
        dictionary->Release();
        return append_hr;
    }
    HRESULT InsertTheme(const char* key, IInspectable* value) override {
        if (!key || !value) return E_INVALIDARG;
        return theme_dictionaries_.InsertText(key, value);
    }
    HRESULT InsertResource(const char* key, IInspectable* value) override {
        return resources_.InsertText(key, value);
    }
    HRESULT AliasResource(const char* key, const char* target) override {
        if (!key || !target) return E_INVALIDARG;
        auto* boxed = new (std::nothrow) BoxedStringObject(target);
        if (!boxed) return E_OUTOFMEMORY;
        IInspectable* value = nullptr;
        HRESULT hr = resources_.Lookup(static_cast<wf::IPropertyValue*>(boxed), &value);
        boxed->Release();
        if (SUCCEEDED(hr)) {
            hr = resources_.InsertText(key, value);
            value->Release();
        }
        return hr;
    }
    HRESULT SetSourceText(const char* value) override {
        source_text_ = value ? value : "";
        return S_OK;
    }

private:
    const wchar_t* name_;
    wf::IUriRuntimeClass* source_ = nullptr;
    std::string source_text_;
    ResourceDictionaryVector merged_{
        {::openxaml::iid::PIID_FIVector_1_Windows__CUI__CXaml__CResourceDictionary,
         ::openxaml::iid::PIID_FIIterable_1_Windows__CUI__CXaml__CResourceDictionary,
         ::openxaml::iid::PIID_FIIterator_1_Windows__CUI__CXaml__CResourceDictionary},
        L"Windows.UI.Xaml.ResourceDictionaryCollection", this};
    InspectableMap resources_{this};
    InspectableMap theme_dictionaries_{this};
};

class XamlControlsResourcesObject final : public ResourceDictionaryObject {
public:
    XamlControlsResourcesObject()
        : ResourceDictionaryObject(L"Microsoft.UI.Xaml.Controls.XamlControlsResources") {}
};

class ApplicationObject final : public ComObject,
                                public abi::NotImpl_IApplication,
                                public abi::NotImpl_IApplication2,
                                public abi::NotImpl_IApplication3 {
public:
    using PrimaryInterface = wux::IApplication;
    // Constructed only by ApplicationFactory::CreateInstance, which calls
    // Compose before the object escapes.
    ApplicationObject() = default;
    ~ApplicationObject() override {
        if (Current() == this) Current() = nullptr;
        if (resources_) resources_->Release();
    }
    const wchar_t* RuntimeClassName() const override {
        return L"Windows.UI.Xaml.Application";
    }

    // The one application the process has, as Application.Current reads it: a
    // raw process-wide pointer set at construction and cleared by the
    // destructor, so a dead application answers null rather than dangling.
    static ApplicationObject*& Current() {
        static ApplicationObject* current = nullptr;
        return current;
    }

    // --- the aggregation boundary ---------------------------------------
    //
    // A WinRT class derives from Application by composing it: the caller is
    // the controlling outer and every interface this object hands out must
    // forward AddRef, Release and QueryInterface to it, or the aggregate
    // splits into two identities. The object's own count belongs to the
    // nested non-delegating Inner alone.
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        return outer_->QueryInterface(iid, object);
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return outer_->AddRef(); }
    ULONG STDMETHODCALLTYPE Release() override { return outer_->Release(); }
    HRESULT STDMETHODCALLTYPE GetIids(ULONG* count, IID** iids) override {
        return outer_->GetIids(count, iids);
    }
    HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING* name) override {
        return outer_->GetRuntimeClassName(name);
    }
    HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel* level) override {
        return outer_->GetTrustLevel(level);
    }

    // The non-delegating identity, and the only pointer whose IUnknown counts
    // this object. It resolves the composed interfaces itself rather than
    // asking the outer to: forwarding a QueryInterface back to the aggregator
    // that just forwarded it here is how an aggregation deadlocks.
    class Inner final : public IInspectable {
    public:
        explicit Inner(ApplicationObject* owner) : owner_(owner) {}

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
            if (!object) return E_POINTER;
            if (IsEqualGUID(iid, IID_IUnknown) ||
                IsEqualGUID(iid, ::openxaml::iid::IInspectable)) {
                *object = static_cast<IInspectable*>(this);
                AddRef();
                return S_OK;
            }
            const HRESULT composed = owner_->QueryComposed(iid, object);
            if (composed == E_NOINTERFACE)
                return TraceQueryInterfaceMiss(owner_->RuntimeClassName(), iid);
            return composed;
        }
        ULONG STDMETHODCALLTYPE AddRef() override { return owner_->Retain(); }
        ULONG STDMETHODCALLTYPE Release() override { return owner_->ReleaseOne(); }
        HRESULT STDMETHODCALLTYPE GetIids(ULONG*, IID**) override { return E_NOTIMPL; }
        HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING* name) override {
            return ::openxaml::winrt::CopyToHString(owner_->RuntimeClassName(), name);
        }
        HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel* level) override {
            if (!level) return E_POINTER;
            *level = BaseTrust;
            return S_OK;
        }

    private:
        ApplicationObject* owner_;
    };

    IInspectable* InnerIdentity() { return &inner_; }
    IInspectable* OuterIdentity() { return outer_; }

    // Called once, by the factory. A null outer means nobody is aggregating
    // us, so the inner identity is what everything delegates to.
    void Compose(IInspectable* outer) {
        outer_ = outer ? outer : static_cast<IInspectable*>(&inner_);
    }

    HRESULT QueryComposed(REFIID iid, void** object) {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_IApplication, wux::IApplication)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_IApplication2, wux::IApplication2)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_IApplication3, wux::IApplication3)
        *object = nullptr;
        return E_NOINTERFACE;
    }

    HRESULT STDMETHODCALLTYPE get_Resources(wux::IResourceDictionary** value) override {
        if (!value) return E_POINTER;
        *value = resources_;
        if (*value) (*value)->AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Resources(wux::IResourceDictionary* value) override {
        if (value) value->AddRef();
        if (resources_) resources_->Release();
        resources_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_DebugSettings(wux::IDebugSettings** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_RequestedTheme(wux::ApplicationTheme* value) override {
        if (!value) return E_POINTER;
        *value = requested_theme_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_RequestedTheme(wux::ApplicationTheme value) override {
        if (value < wux::ApplicationTheme_Light || value > wux::ApplicationTheme_Dark)
            return E_INVALIDARG;
        requested_theme_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE add_UnhandledException(
        wux::IUnhandledExceptionEventHandler*, EventRegistrationToken* token) override {
        return NewToken(token);
    }
    HRESULT STDMETHODCALLTYPE remove_UnhandledException(EventRegistrationToken) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE add_Suspending(
        wux::ISuspendingEventHandler*, EventRegistrationToken* token) override {
        return NewToken(token);
    }
    HRESULT STDMETHODCALLTYPE remove_Suspending(EventRegistrationToken) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE add_Resuming(
        __FIEventHandler_1_IInspectable*, EventRegistrationToken* token) override {
        return NewToken(token);
    }
    HRESULT STDMETHODCALLTYPE remove_Resuming(EventRegistrationToken) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Exit() override {
        PostQuitMessage(0);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_FocusVisualKind(wux::FocusVisualKind* value) override {
        if (!value) return E_POINTER;
        *value = focus_visual_kind_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_FocusVisualKind(wux::FocusVisualKind value) override {
        focus_visual_kind_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_RequiresPointerMode(
        wux::ApplicationRequiresPointerMode* value) override {
        if (!value) return E_POINTER;
        *value = pointer_mode_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_RequiresPointerMode(
        wux::ApplicationRequiresPointerMode value) override {
        pointer_mode_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE add_LeavingBackground(
        wux::ILeavingBackgroundEventHandler*, EventRegistrationToken* token) override {
        return NewToken(token);
    }
    HRESULT STDMETHODCALLTYPE remove_LeavingBackground(EventRegistrationToken) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE add_EnteredBackground(
        wux::IEnteredBackgroundEventHandler*, EventRegistrationToken* token) override {
        return NewToken(token);
    }
    HRESULT STDMETHODCALLTYPE remove_EnteredBackground(EventRegistrationToken) override {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_HighContrastAdjustment(
        wux::ApplicationHighContrastAdjustment* value) override {
        if (!value) return E_POINTER;
        *value = high_contrast_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_HighContrastAdjustment(
        wux::ApplicationHighContrastAdjustment value) override {
        if (value != wux::ApplicationHighContrastAdjustment_None &&
            value != wux::ApplicationHighContrastAdjustment_Auto) return E_INVALIDARG;
        high_contrast_ = value;
        return S_OK;
    }

private:
    HRESULT NewToken(EventRegistrationToken* token) {
        if (!token) return E_POINTER;
        token->value = InterlockedIncrement64(&next_token_);
        return S_OK;
    }

    wux::IResourceDictionary* resources_ = nullptr;
    Inner inner_{this};
    // The aggregate. Never null after Compose, and deliberately not AddRef'd:
    // an inner that held a reference on its aggregator would be a cycle
    // neither could break.
    IInspectable* outer_ = nullptr;
    wux::ApplicationTheme requested_theme_ = wux::ApplicationTheme_Light;
    wux::FocusVisualKind focus_visual_kind_ = wux::FocusVisualKind_HighVisibility;
    wux::ApplicationRequiresPointerMode pointer_mode_ = wux::ApplicationRequiresPointerMode_Auto;
    wux::ApplicationHighContrastAdjustment high_contrast_ =
        wux::ApplicationHighContrastAdjustment_Auto;
    LONGLONG next_token_ = 0;
};

class ApplicationFactory final : public Factory<ApplicationObject>,
                                 public abi::NotImpl_IApplicationFactory,
                                 public abi::NotImpl_IApplicationStatics {
public:
    ApplicationFactory() : Factory(L"Windows.UI.Xaml.Application") {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        return Factory<ApplicationObject>::QueryInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()

    // Application is [composable] and not [activatable]: the IDL offers no
    // default constructor, and an instance made without Compose would have no
    // identity to delegate to. CreateInstance is the way in.
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable**) override {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE CreateInstance(IInspectable* base_interface,
                                             IInspectable** inner_interface,
                                             wux::IApplication** value) override {
        if (!inner_interface || !value) return E_POINTER;
        *inner_interface = nullptr;
        *value = nullptr;

        // One application per process. The published core enforces this in
        // FrameworkApplication::Initialize and fails the second construction;
        // a runtime that quietly allowed two would have two answers for
        // Application.Current and no way to say which was right.
        if (ApplicationObject::Current()) return E_UNEXPECTED;

        auto* application = new ApplicationObject();
        application->Compose(base_interface);
        ApplicationObject::Current() = application;

        // The inner carries the object's own single reference; the returned
        // default interface carries one on the aggregate.
        *inner_interface = application->InnerIdentity();
        const HRESULT composed = application->QueryComposed(
            ::openxaml::iid::Windows_UI_Xaml_IApplication,
            reinterpret_cast<void**>(value));
        if (FAILED(composed)) {
            *inner_interface = nullptr;
            // Releasing the inner drops the object's only reference, and its
            // destructor is what clears Application.Current again.
            application->InnerIdentity()->Release();
            return composed;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Current(wux::IApplication** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        // Null with S_OK when no Application was made: the published core
        // returns exactly that, and warns its own callers to expect it.
        ApplicationObject* current = ApplicationObject::Current();
        if (!current) return S_OK;
        return current->QueryComposed(::openxaml::iid::Windows_UI_Xaml_IApplication,
                                      reinterpret_cast<void**>(value));
    }
    HRESULT STDMETHODCALLTYPE Start(wux::IApplicationInitializationCallback* callback) override {
        return callback ? callback->Invoke(nullptr) : E_INVALIDARG;
    }
    HRESULT STDMETHODCALLTYPE LoadComponent(IInspectable* component,
                                            wf::IUriRuntimeClass* resource) override {
        if (!component || !resource) return E_INVALIDARG;
        HSTRING absolute = nullptr;
        HRESULT hr = resource->get_AbsoluteUri(&absolute);
        if (FAILED(hr)) return hr;
        UINT32 length = 0;
        const wchar_t* raw = WindowsGetStringRawBuffer(absolute, &length);
        const std::wstring uri(raw ? raw : L"", length);
        WindowsDeleteString(absolute);

        constexpr const wchar_t prefix[] = L"ms-appx:///";
        if (uri.rfind(prefix, 0) != 0) return E_INVALIDARG;
        std::filesystem::path relative(uri.substr((sizeof(prefix) / sizeof(wchar_t)) - 1));
        if (relative.extension() != L".xaml") return E_INVALIDARG;
        relative.replace_extension(L".xbf");
        for (const auto& part : relative)
            if (part == L"..") return E_INVALIDARG;

        std::vector<std::filesystem::path> roots;
        wchar_t configured[32768];
        const DWORD configured_length = GetEnvironmentVariableW(
            L"OPENXAML_XBF_ROOT", configured,
            static_cast<DWORD>(sizeof(configured) / sizeof(configured[0])));
        if (configured_length > 0 &&
            configured_length < sizeof(configured) / sizeof(configured[0])) {
            roots.emplace_back(configured);
        }
        wchar_t executable[32768];
        const DWORD executable_length = GetModuleFileNameW(
            nullptr, executable,
            static_cast<DWORD>(sizeof(executable) / sizeof(executable[0])));
        if (executable_length > 0 &&
            executable_length < sizeof(executable) / sizeof(executable[0])) {
            roots.push_back(std::filesystem::path(executable).parent_path());
        }

        try {
            for (const auto& root : roots) {
                const auto candidate = root / relative;
                if (!std::filesystem::is_regular_file(candidate)) continue;
                auto graph = xbf::WriteObjectGraph(xbf::ReadFile(candidate));
                if (!graph) return HRESULT_FROM_WIN32(ERROR_BAD_FORMAT);
                hr = MaterializeXbf(graph, component);
                if (FAILED(hr)) return hr;
                loaded_components_[component] = std::move(graph);
                return S_OK;
            }
        } catch (const std::exception& error) {
            std::string message = "OpenXaml LoadComponent: ";
            message += error.what();
            message += "\n";
            OutputDebugStringA(message.c_str());
            return HRESULT_FROM_WIN32(ERROR_BAD_FORMAT);
        }
        return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    }
    HRESULT STDMETHODCALLTYPE LoadComponentWithResourceLocation(
        IInspectable* component, wf::IUriRuntimeClass* resource,
        wuxcp::ComponentResourceLocation) override {
        return LoadComponent(component, resource);
    }

    HRESULT ActivateLocalXamlType(const std::string& type, IInspectable** result) {
        if (!result) return E_POINTER;
        *result = nullptr;
        ApplicationObject* current = ApplicationObject::Current();
        if (!current) return REGDB_E_CLASSNOTREG;
        wuxmk::IXamlMetadataProvider* provider = nullptr;
        HRESULT hr = current->OuterIdentity()->QueryInterface(
            ::openxaml::iid::Windows_UI_Xaml_Markup_IXamlMetadataProvider,
            reinterpret_cast<void**>(&provider));
        if (FAILED(hr)) return hr;
        HSTRING name = nullptr;
        hr = HStringFromUtf8(type, &name);
        wuxmk::IXamlType* xaml_type = nullptr;
        if (SUCCEEDED(hr)) hr = provider->GetXamlTypeByFullName(name, &xaml_type);
        WindowsDeleteString(name);
        provider->Release();
        if (FAILED(hr)) return hr;
        if (!xaml_type) return REGDB_E_CLASSNOTREG;
        hr = xaml_type->ActivateInstance(result);
        xaml_type->Release();
        return hr;
    }

protected:
    HRESULT QueryStatics(REFIID iid, void** object) override {
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_IApplicationFactory,
                        wux::IApplicationFactory)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_IApplicationStatics,
                        wux::IApplicationStatics)
        return E_NOINTERFACE;
    }

private:
    std::map<IInspectable*, std::shared_ptr<xbf::Object>> loaded_components_;
};

// WinUI's generated application bootstrap adds this provider to its local
// metadata chain before initializing desktop XAML. The open runtime does not
// yet project the WinUI control catalog, but it must provide a valid empty
// IXamlMetadataProvider so local Terminal types remain usable and an absent
// Microsoft.UI.Xaml.dll does not stop the process at activation.
class XamlControlsMetadataProviderObject final
    : public ComObject,
      public abi::NotImpl_IXamlMetadataProvider {
public:
    using PrimaryInterface = wuxmk::IXamlMetadataProvider;
    const wchar_t* RuntimeClassName() const override {
        return L"Microsoft.UI.Xaml.XamlTypeInfo.XamlControlsXamlMetaDataProvider";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Markup_IXamlMetadataProvider,
                        wuxmk::IXamlMetadataProvider)
        OPENXAML_QI_ARM(IID_IUnknown, wuxmk::IXamlMetadataProvider)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable, wuxmk::IXamlMetadataProvider)
        *object = nullptr;
        return E_NOINTERFACE;
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE GetXamlType(
        ABI::Windows::UI::Xaml::Interop::TypeName,
        wuxmk::IXamlType** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetXamlTypeByFullName(
        HSTRING, wuxmk::IXamlType** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetXmlnsDefinitions(
        UINT32* result_length, wuxmk::XmlnsDefinition** result) override {
        if (!result_length || !result) return E_POINTER;
        *result_length = 0;
        *result = nullptr;
        return S_OK;
    }
};

class DispatcherQueueTimerObject final
    : public ComObject,
      public abi::NotImpl_IDispatcherQueueTimer {
public:
    using TickHandler =
        __FITypedEventHandler_2_Windows__CSystem__CDispatcherQueueTimer_IInspectable;
    using PrimaryInterface = ws::IDispatcherQueueTimer;

    DispatcherQueueTimerObject(IUnknown* owner, HWND window)
        : owner_(owner), window_(window) {
        owner_->AddRef();
    }
    ~DispatcherQueueTimerObject() override {
        if (timer_id_) KillTimer(window_, timer_id_);
        for (auto& [_, handler] : handlers_) handler->Release();
        owner_->Release();
    }
    const wchar_t* RuntimeClassName() const override {
        return L"Windows.System.DispatcherQueueTimer";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_System_IDispatcherQueueTimer,
                        ws::IDispatcherQueueTimer)
        OPENXAML_QI_ARM(IID_IUnknown, ws::IDispatcherQueueTimer)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable, ws::IDispatcherQueueTimer)
        *object = nullptr;
        return TraceQueryInterfaceMiss(RuntimeClassName(), iid);
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE get_Interval(wf::TimeSpan* value) override {
        if (!value) return E_POINTER;
        *value = interval_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Interval(wf::TimeSpan value) override {
        if (value.Duration < 0) return E_INVALIDARG;
        interval_ = value;
        if (running_) {
            KillTimer(window_, timer_id_);
            timer_id_ = SetTimer(window_, reinterpret_cast<UINT_PTR>(this),
                                 IntervalMilliseconds(), nullptr);
            if (!timer_id_) return HRESULT_FROM_WIN32(GetLastError());
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsRunning(boolean* value) override {
        if (!value) return E_POINTER;
        *value = running_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsRepeating(boolean* value) override {
        if (!value) return E_POINTER;
        *value = repeating_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_IsRepeating(boolean value) override {
        repeating_ = value != 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Start() override {
        if (running_) return S_OK;
        timer_id_ = SetTimer(window_, reinterpret_cast<UINT_PTR>(this),
                             IntervalMilliseconds(), nullptr);
        if (!timer_id_) return HRESULT_FROM_WIN32(GetLastError());
        running_ = true;
        static_cast<ws::IDispatcherQueueTimer*>(this)->AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Stop() override {
        if (!running_) return S_OK;
        KillTimer(window_, timer_id_);
        timer_id_ = 0;
        running_ = false;
        static_cast<ws::IDispatcherQueueTimer*>(this)->Release();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE add_Tick(TickHandler* handler,
                                        EventRegistrationToken* token) override {
        if (!handler || !token) return E_INVALIDARG;
        token->value = ++next_token_;
        handler->AddRef();
        handlers_[token->value] = handler;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE remove_Tick(EventRegistrationToken token) override {
        const auto found = handlers_.find(token.value);
        if (found == handlers_.end()) return S_OK;
        found->second->Release();
        handlers_.erase(found);
        return S_OK;
    }

    void OnTick() {
        auto* self = static_cast<ws::IDispatcherQueueTimer*>(this);
        self->AddRef();
        const bool one_shot = !repeating_;
        if (one_shot && running_) {
            KillTimer(window_, timer_id_);
            timer_id_ = 0;
            running_ = false;
        }
        std::vector<TickHandler*> snapshot;
        snapshot.reserve(handlers_.size());
        for (const auto& [_, handler] : handlers_) {
            handler->AddRef();
            snapshot.push_back(handler);
        }
        for (auto* handler : snapshot) {
            handler->Invoke(self, nullptr);
            handler->Release();
        }
        if (one_shot) self->Release();  // release Start's running reference
        self->Release();                // release this callback's reference
    }

private:
    UINT IntervalMilliseconds() const {
        const auto milliseconds = interval_.Duration / 10000;
        if (milliseconds <= 0) return 1;
        return static_cast<UINT>(std::min<INT64>(milliseconds, USER_TIMER_MAXIMUM));
    }

    IUnknown* owner_;
    HWND window_ = nullptr;
    UINT_PTR timer_id_ = 0;
    wf::TimeSpan interval_{100000};
    boolean running_ = 0;
    boolean repeating_ = 0;
    LONGLONG next_token_ = 0;
    std::map<LONGLONG, TickHandler*> handlers_;
};

class DispatcherQueueObject final
    : public ComObject,
      public abi::NotImpl_IDispatcherQueue,
      public abi::NotImpl_IDispatcherQueue2 {
public:
    using PrimaryInterface = ws::IDispatcherQueue;
    DispatcherQueueObject() : thread_id_(GetCurrentThreadId()) {
        WNDCLASSW window_class{};
        window_class.lpfnWndProc = WindowProc;
        window_class.hInstance = GetModuleHandleW(nullptr);
        window_class.lpszClassName = L"OpenXaml.DispatcherQueueWindow";
        RegisterClassW(&window_class);
        window_ = CreateWindowExW(0, window_class.lpszClassName, L"", 0,
                                  0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                  window_class.hInstance, this);
    }
    ~DispatcherQueueObject() override {
        if (window_) DestroyWindow(window_);
        for (auto& [_, handler] : shutdown_handlers_) handler->Release();
    }
    const wchar_t* RuntimeClassName() const override {
        return L"Windows.System.DispatcherQueue";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_System_IDispatcherQueue,
                        ws::IDispatcherQueue)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_System_IDispatcherQueue2,
                        ws::IDispatcherQueue2)
        OPENXAML_QI_ARM(IID_IUnknown, ws::IDispatcherQueue)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable, ws::IDispatcherQueue)
        *object = nullptr;
        return TraceQueryInterfaceMiss(RuntimeClassName(), iid);
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE CreateTimer(ws::IDispatcherQueueTimer** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (!window_) return E_UNEXPECTED;
        auto* owner = static_cast<ws::IDispatcherQueue*>(this);
        auto* timer = new (std::nothrow) DispatcherQueueTimerObject(owner, window_);
        if (!timer) return E_OUTOFMEMORY;
        *result = static_cast<ws::IDispatcherQueueTimer*>(timer);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE TryEnqueue(ws::IDispatcherQueueHandler* callback,
                                          boolean* result) override {
        return Enqueue(callback, result);
    }
    HRESULT STDMETHODCALLTYPE TryEnqueueWithPriority(
        ws::DispatcherQueuePriority, ws::IDispatcherQueueHandler* callback,
        boolean* result) override {
        return Enqueue(callback, result);
    }
    HRESULT STDMETHODCALLTYPE add_ShutdownStarting(
        __FITypedEventHandler_2_Windows__CSystem__CDispatcherQueue_Windows__CSystem__CDispatcherQueueShutdownStartingEventArgs* handler,
        EventRegistrationToken* token) override {
        return AddShutdown(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_ShutdownStarting(
        EventRegistrationToken token) override {
        return RemoveShutdown(token);
    }
    HRESULT STDMETHODCALLTYPE add_ShutdownCompleted(
        __FITypedEventHandler_2_Windows__CSystem__CDispatcherQueue_IInspectable* handler,
        EventRegistrationToken* token) override {
        return AddShutdown(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_ShutdownCompleted(
        EventRegistrationToken token) override {
        return RemoveShutdown(token);
    }
    HRESULT STDMETHODCALLTYPE get_HasThreadAccess(boolean* value) override {
        if (!value) return E_POINTER;
        *value = GetCurrentThreadId() == thread_id_;
        return S_OK;
    }

private:
    static constexpr UINT CallbackMessage = WM_APP + 0x584;
    static LRESULT CALLBACK WindowProc(HWND window, UINT message,
                                        WPARAM wparam, LPARAM lparam) {
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
            SetWindowLongPtrW(window, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(create->lpCreateParams));
            return TRUE;
        }
        if (message == CallbackMessage) {
            auto* callback = reinterpret_cast<ws::IDispatcherQueueHandler*>(lparam);
            callback->Invoke();
            callback->Release();
            return 0;
        }
        if (message == WM_TIMER) {
            reinterpret_cast<DispatcherQueueTimerObject*>(wparam)->OnTick();
            return 0;
        }
        return DefWindowProcW(window, message, wparam, lparam);
    }
    HRESULT Enqueue(ws::IDispatcherQueueHandler* callback, boolean* result) {
        if (!callback || !result) return E_INVALIDARG;
        callback->AddRef();
        const bool posted = window_ &&
            PostMessageW(window_, CallbackMessage, 0,
                         reinterpret_cast<LPARAM>(callback));
        if (!posted) callback->Release();
        *result = posted;
        return S_OK;
    }
    HRESULT AddShutdown(IUnknown* handler, EventRegistrationToken* token) {
        if (!handler || !token) return E_INVALIDARG;
        token->value = ++next_token_;
        handler->AddRef();
        shutdown_handlers_[token->value] = handler;
        return S_OK;
    }
    HRESULT RemoveShutdown(EventRegistrationToken token) {
        const auto found = shutdown_handlers_.find(token.value);
        if (found == shutdown_handlers_.end()) return S_OK;
        found->second->Release();
        shutdown_handlers_.erase(found);
        return S_OK;
    }

    DWORD thread_id_;
    HWND window_ = nullptr;
    LONGLONG next_token_ = 0;
    std::map<LONGLONG, IUnknown*> shutdown_handlers_;
};

class DispatcherQueueFactory final
    : public ComObject,
      public IActivationFactory,
      public abi::NotImpl_IDispatcherQueueStatics {
public:
    const wchar_t* RuntimeClassName() const override {
        return L"Windows.System.DispatcherQueue";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_System_IDispatcherQueueStatics,
                        ws::IDispatcherQueueStatics)
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
    HRESULT STDMETHODCALLTYPE GetForCurrentThread(ws::IDispatcherQueue** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (GetEnvironmentVariableW(L"OPENXAML_TRACE_QI", nullptr, 0))
            OutputDebugStringA(g_xaml_manager_initialized
                ? "OpenXaml: DispatcherQueue current=ready\n"
                : "OpenXaml: DispatcherQueue current=absent\n");
        if (!g_xaml_manager_initialized) return S_OK;
        *result = static_cast<ws::IDispatcherQueue*>(&queue_);
        (*result)->AddRef();
        return S_OK;
    }
private:
    DispatcherQueueObject queue_;
};

class WindowsXamlManagerObject final
    : public ComObject,
      public abi::NotImpl_IWindowsXamlManager,
      public abi::NotImpl_IClosable {
public:
    using PrimaryInterface = wuxh::IWindowsXamlManager;
    const wchar_t* RuntimeClassName() const override {
        return L"Windows.UI.Xaml.Hosting.WindowsXamlManager";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Hosting_IWindowsXamlManager,
                        wuxh::IWindowsXamlManager)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_Foundation_IClosable, wf::IClosable)
        OPENXAML_QI_ARM(IID_IUnknown, wuxh::IWindowsXamlManager)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable, wuxh::IWindowsXamlManager)
        *object = nullptr;
        return E_NOINTERFACE;
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE Close() override {
        closed_ = true;
        return S_OK;
    }

private:
    bool closed_ = false;
};

class WindowsXamlManagerFactory final
    : public Factory<WindowsXamlManagerObject>,
      public abi::NotImpl_IWindowsXamlManagerStatics {
public:
    WindowsXamlManagerFactory()
        : Factory(L"Windows.UI.Xaml.Hosting.WindowsXamlManager") {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        return Factory<WindowsXamlManagerObject>::QueryInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE InitializeForCurrentThread(
        wuxh::IWindowsXamlManager** result) override {
        if (!result) return E_POINTER;
        g_xaml_manager_initialized = true;
        if (GetEnvironmentVariableW(L"OPENXAML_TRACE_QI", nullptr, 0))
            OutputDebugStringA("OpenXaml: WindowsXamlManager initialized\n");
        *result = static_cast<wuxh::IWindowsXamlManager*>(new WindowsXamlManagerObject());
        return S_OK;
    }

protected:
    HRESULT QueryStatics(REFIID iid, void** object) override {
        OPENXAML_QI_ARM(
            ::openxaml::iid::Windows_UI_Xaml_Hosting_IWindowsXamlManagerStatics,
            wuxh::IWindowsXamlManagerStatics)
        return E_NOINTERFACE;
    }
};

class ResourceCandidateObject final
    : public ComObject,
      public abi::NotImpl_IResourceCandidate {
public:
    using PrimaryInterface = warc::IResourceCandidate;
    explicit ResourceCandidateObject(HSTRING value) {
        WindowsDuplicateString(value, &value_);
    }
    ~ResourceCandidateObject() override { WindowsDeleteString(value_); }
    const wchar_t* RuntimeClassName() const override {
        return L"Windows.ApplicationModel.Resources.Core.ResourceCandidate";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(
            ::openxaml::iid::Windows_ApplicationModel_Resources_Core_IResourceCandidate,
            warc::IResourceCandidate)
        OPENXAML_QI_ARM(IID_IUnknown, warc::IResourceCandidate)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable, warc::IResourceCandidate)
        *object = nullptr;
        return E_NOINTERFACE;
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE get_IsMatch(boolean* value) override {
        if (!value) return E_POINTER;
        *value = true;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsMatchAsDefault(boolean* value) override {
        return get_IsMatch(value);
    }
    HRESULT STDMETHODCALLTYPE get_IsDefault(boolean* value) override {
        return get_IsMatch(value);
    }
    HRESULT STDMETHODCALLTYPE get_ValueAsString(HSTRING* result) override {
        if (!result) return E_POINTER;
        return WindowsDuplicateString(value_, result);
    }
    HRESULT STDMETHODCALLTYPE GetQualifierValue(HSTRING, HSTRING* value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return WindowsCreateString(L"", 0, value);
    }

private:
    HSTRING value_ = nullptr;
};

class ResourceMapObject final : public ComObject,
                                public abi::NotImpl_IResourceMap {
public:
    using PrimaryInterface = warc::IResourceMap;
    const wchar_t* RuntimeClassName() const override {
        return L"Windows.ApplicationModel.Resources.Core.ResourceMap";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_ApplicationModel_Resources_Core_IResourceMap,
                        warc::IResourceMap)
        OPENXAML_QI_ARM(IID_IUnknown, warc::IResourceMap)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable, warc::IResourceMap)
        *object = nullptr;
        return E_NOINTERFACE;
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE get_Uri(wf::IUriRuntimeClass** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetValue(HSTRING resource,
                                        warc::IResourceCandidate** value) override {
        return Candidate(resource, value);
    }
    HRESULT STDMETHODCALLTYPE GetValueForContext(
        HSTRING resource, warc::IResourceContext*,
        warc::IResourceCandidate** value) override {
        return Candidate(resource, value);
    }
    HRESULT STDMETHODCALLTYPE GetSubtree(HSTRING, warc::IResourceMap** map) override {
        if (!map) return E_POINTER;
        *map = static_cast<warc::IResourceMap*>(new ResourceMapObject());
        return S_OK;
    }

private:
    static HRESULT Candidate(HSTRING resource, warc::IResourceCandidate** value) {
        if (!value) return E_POINTER;
        *value = static_cast<warc::IResourceCandidate*>(new ResourceCandidateObject(resource));
        return S_OK;
    }
};

class ResourceContextObject final : public ComObject,
                                    public abi::NotImpl_IResourceContext {
public:
    using PrimaryInterface = warc::IResourceContext;
    const wchar_t* RuntimeClassName() const override {
        return L"Windows.ApplicationModel.Resources.Core.ResourceContext";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(
            ::openxaml::iid::Windows_ApplicationModel_Resources_Core_IResourceContext,
            warc::IResourceContext)
        OPENXAML_QI_ARM(IID_IUnknown, warc::IResourceContext)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable, warc::IResourceContext)
        *object = nullptr;
        return E_NOINTERFACE;
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE Reset() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ResetQualifierValues(__FIIterable_1_HSTRING*) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OverrideToMatch(
        __FIIterable_1_Windows__CApplicationModel__CResources__CCore__CResourceQualifier*) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Clone(warc::IResourceContext** clone) override {
        if (!clone) return E_POINTER;
        *clone = static_cast<warc::IResourceContext*>(new ResourceContextObject());
        return S_OK;
    }
};

class ResourceContextFactory final
    : public Factory<ResourceContextObject>,
      public abi::NotImpl_IResourceContextStatics,
      public abi::NotImpl_IResourceContextStatics2 {
public:
    ResourceContextFactory()
        : Factory(L"Windows.ApplicationModel.Resources.Core.ResourceContext") {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        return Factory<ResourceContextObject>::QueryInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE CreateMatchingContext(
        __FIIterable_1_Windows__CApplicationModel__CResources__CCore__CResourceQualifier*,
        warc::IResourceContext** value) override {
        return Create(value);
    }
    HRESULT STDMETHODCALLTYPE GetForCurrentView(warc::IResourceContext** value) override {
        return Create(value);
    }
    HRESULT STDMETHODCALLTYPE SetGlobalQualifierValue(HSTRING, HSTRING) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ResetGlobalQualifierValues() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ResetGlobalQualifierValuesForSpecifiedQualifiers(
        __FIIterable_1_HSTRING*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetForViewIndependentUse(
        warc::IResourceContext** value) override {
        return Create(value);
    }

protected:
    HRESULT QueryStatics(REFIID iid, void** object) override {
        OPENXAML_QI_ARM(
            ::openxaml::iid::Windows_ApplicationModel_Resources_Core_IResourceContextStatics,
            warc::IResourceContextStatics)
        OPENXAML_QI_ARM(
            ::openxaml::iid::Windows_ApplicationModel_Resources_Core_IResourceContextStatics2,
            warc::IResourceContextStatics2)
        return E_NOINTERFACE;
    }

private:
    static HRESULT Create(warc::IResourceContext** value) {
        if (!value) return E_POINTER;
        *value = static_cast<warc::IResourceContext*>(new ResourceContextObject());
        return S_OK;
    }
};

class ResourceManagerObject final : public ComObject,
                                    public abi::NotImpl_IResourceManager {
public:
    using PrimaryInterface = warc::IResourceManager;
    const wchar_t* RuntimeClassName() const override {
        return L"Windows.ApplicationModel.Resources.Core.ResourceManager";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(
            ::openxaml::iid::Windows_ApplicationModel_Resources_Core_IResourceManager,
            warc::IResourceManager)
        OPENXAML_QI_ARM(IID_IUnknown, warc::IResourceManager)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable, warc::IResourceManager)
        *object = nullptr;
        return E_NOINTERFACE;
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE get_MainResourceMap(warc::IResourceMap** value) override {
        if (!value) return E_POINTER;
        *value = static_cast<warc::IResourceMap*>(new ResourceMapObject());
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_DefaultContext(warc::IResourceContext** value) override {
        if (!value) return E_POINTER;
        *value = static_cast<warc::IResourceContext*>(new ResourceContextObject());
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE LoadPriFiles(
        __FIIterable_1_Windows__CStorage__CIStorageFile*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE UnloadPriFiles(
        __FIIterable_1_Windows__CStorage__CIStorageFile*) override { return S_OK; }
};

class ResourceManagerFactory final
    : public Factory<ResourceManagerObject>,
      public abi::NotImpl_IResourceManagerStatics {
public:
    ResourceManagerFactory()
        : Factory(L"Windows.ApplicationModel.Resources.Core.ResourceManager") {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        return Factory<ResourceManagerObject>::QueryInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE get_Current(warc::IResourceManager** value) override {
        if (!value) return E_POINTER;
        *value = static_cast<warc::IResourceManager*>(new ResourceManagerObject());
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE IsResourceReference(HSTRING resource,
                                                   boolean* result) override {
        if (!result) return E_POINTER;
        const wchar_t* text = WindowsGetStringRawBuffer(resource, nullptr);
        *result = text && wcsncmp(text, L"ms-resource:", 12) == 0;
        return S_OK;
    }

protected:
    HRESULT QueryStatics(REFIID iid, void** object) override {
        OPENXAML_QI_ARM(
            ::openxaml::iid::Windows_ApplicationModel_Resources_Core_IResourceManagerStatics,
            warc::IResourceManagerStatics)
        return E_NOINTERFACE;
    }
};

inline constexpr GUID IID_OpenXamlDesktopWindowXamlSourceNative = {
    0x3cbcf1bf, 0x2f76, 0x4e9c,
    {0x96, 0xab, 0xe8, 0x4b, 0x37, 0x97, 0x25, 0x54}};

class DesktopWindowXamlSourceObject final
    : public ComObject,
      public abi::NotImpl_IDesktopWindowXamlSource,
      public abi::NotImpl_IClosable,
      public IDesktopWindowXamlSourceNative {
public:
    using PrimaryInterface = wuxh::IDesktopWindowXamlSource;
    ~DesktopWindowXamlSourceObject() override { Close(); }
    const wchar_t* RuntimeClassName() const override {
        return L"Windows.UI.Xaml.Hosting.DesktopWindowXamlSource";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(
            ::openxaml::iid::Windows_UI_Xaml_Hosting_IDesktopWindowXamlSource,
            wuxh::IDesktopWindowXamlSource)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_Foundation_IClosable, wf::IClosable)
        OPENXAML_QI_ARM(IID_OpenXamlDesktopWindowXamlSourceNative,
                        IDesktopWindowXamlSourceNative)
        OPENXAML_QI_ARM(IID_IUnknown, wuxh::IDesktopWindowXamlSource)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable,
                        wuxh::IDesktopWindowXamlSource)
        *object = nullptr;
        return E_NOINTERFACE;
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE get_Content(wux::IUIElement** value) override {
        if (!value) return E_POINTER;
        *value = content_;
        if (*value) (*value)->AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Content(wux::IUIElement* value) override {
        if (value) value->AddRef();
        if (content_) content_->Release();
        content_ = value;
        LayoutContent();
        if (child_) InvalidateRect(child_, nullptr, FALSE);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_HasFocus(boolean* value) override {
        if (!value) return E_POINTER;
        *value = child_ && GetFocus() == child_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE add_TakeFocusRequested(
        __FITypedEventHandler_2_Windows__CUI__CXaml__CHosting__CDesktopWindowXamlSource_Windows__CUI__CXaml__CHosting__CDesktopWindowXamlSourceTakeFocusRequestedEventArgs*,
        EventRegistrationToken* token) override {
        return NewToken(token);
    }
    HRESULT STDMETHODCALLTYPE remove_TakeFocusRequested(EventRegistrationToken) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE add_GotFocus(
        __FITypedEventHandler_2_Windows__CUI__CXaml__CHosting__CDesktopWindowXamlSource_Windows__CUI__CXaml__CHosting__CDesktopWindowXamlSourceGotFocusEventArgs*,
        EventRegistrationToken* token) override {
        return NewToken(token);
    }
    HRESULT STDMETHODCALLTYPE remove_GotFocus(EventRegistrationToken) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Close() override {
        put_Content(nullptr);
        if (child_) {
            DestroyWindow(child_);
            child_ = nullptr;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE AttachToWindow(HWND parent) override {
        if (!parent || !IsWindow(parent)) return E_INVALIDARG;
        if (child_) return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
        const HRESULT class_hr = EnsureWindowClass();
        if (FAILED(class_hr)) return class_hr;
        child_ = CreateWindowExW(0, WindowClassName(), L"OpenXaml Island", WS_CHILD,
                                 0, 0, 1, 1, parent, nullptr,
                                 GetModuleHandleW(nullptr), this);
        return child_ ? S_OK : HRESULT_FROM_WIN32(GetLastError());
    }
    HRESULT STDMETHODCALLTYPE get_WindowHandle(HWND* value) override {
        if (!value) return E_POINTER;
        *value = child_;
        return child_ ? S_OK : E_UNEXPECTED;
    }

private:
    static const wchar_t* WindowClassName() {
        return L"OpenXaml.DesktopWindowXamlSource";
    }
    static HRESULT EnsureWindowClass() {
        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.style = CS_HREDRAW | CS_VREDRAW;
        window_class.lpfnWndProc = WindowProc;
        window_class.hInstance = GetModuleHandleW(nullptr);
        window_class.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        window_class.lpszClassName = WindowClassName();
        if (RegisterClassExW(&window_class)) return S_OK;
        const DWORD error = GetLastError();
        return error == ERROR_CLASS_ALREADY_EXISTS ? S_OK : HRESULT_FROM_WIN32(error);
    }
    static LRESULT CALLBACK WindowProc(HWND window, UINT message,
                                       WPARAM wparam, LPARAM lparam) {
        auto* self = reinterpret_cast<DesktopWindowXamlSourceObject*>(
            GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
            self = static_cast<DesktopWindowXamlSourceObject*>(create->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(self));
        }
        switch (message) {
            case WM_SIZE:
                if (self) self->LayoutContent();
                InvalidateRect(window, nullptr, FALSE);
                return 0;
            case WM_ERASEBKGND:
                return 1;
            case WM_PAINT: {
                PAINTSTRUCT paint{};
                HDC dc = BeginPaint(window, &paint);
                RECT client{};
                GetClientRect(window, &client);
                HBRUSH background = CreateSolidBrush(RGB(12, 12, 12));
                FillRect(dc, &client, background);
                DeleteObject(background);
                EndPaint(window, &paint);
                return 0;
            }
            case WM_NCDESTROY:
                SetWindowLongPtrW(window, GWLP_USERDATA, 0);
                break;
        }
        return DefWindowProcW(window, message, wparam, lparam);
    }
    void LayoutContent() {
        if (!child_ || !content_) return;
        RECT client{};
        if (!GetClientRect(child_, &client)) return;
        IOpenXamlNative* native = nullptr;
        const HRESULT hr = content_->QueryInterface(
            IID_IOpenXamlNative, reinterpret_cast<void**>(&native));
        if (SUCCEEDED(hr)) {
            native->PerformLayout(
                static_cast<double>(client.right - client.left),
                static_cast<double>(client.bottom - client.top));
            native->Release();
        }
    }

    HRESULT NewToken(EventRegistrationToken* token) {
        if (!token) return E_POINTER;
        token->value = ++next_token_;
        return S_OK;
    }

    wux::IUIElement* content_ = nullptr;
    HWND child_ = nullptr;
    LONGLONG next_token_ = 0;
};

class DesktopWindowXamlSourceFactory final
    : public Factory<DesktopWindowXamlSourceObject>,
      public abi::NotImpl_IDesktopWindowXamlSourceFactory {
public:
    DesktopWindowXamlSourceFactory()
        : Factory(L"Windows.UI.Xaml.Hosting.DesktopWindowXamlSource") {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        return Factory<DesktopWindowXamlSourceObject>::QueryInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE CreateInstance(
        IInspectable* base_interface, IInspectable** inner_interface,
        wuxh::IDesktopWindowXamlSource** value) override {
        if (!inner_interface || !value) return E_POINTER;
        if (base_interface) return E_NOTIMPL;
        auto* source = new DesktopWindowXamlSourceObject();
        auto* projected = static_cast<wuxh::IDesktopWindowXamlSource*>(source);
        projected->AddRef();
        *inner_interface = static_cast<IInspectable*>(projected);
        *value = projected;
        return S_OK;
    }

protected:
    HRESULT QueryStatics(REFIID iid, void** object) override {
        OPENXAML_QI_ARM(
            ::openxaml::iid::Windows_UI_Xaml_Hosting_IDesktopWindowXamlSourceFactory,
            wuxh::IDesktopWindowXamlSourceFactory)
        return E_NOINTERFACE;
    }
};

}  // namespace
// FontFamily is constructed with its name rather than default-constructed and
// filled in, so its factory carries IFontFamilyFactory alongside
// IActivationFactory -- the same shape as Grid's statics.
class FontFamilyFactory final : public Factory<FontFamilyObject>,
                                public abi::NotImpl_IFontFamilyFactory {
public:
    FontFamilyFactory() : Factory<FontFamilyObject>(L"Windows.UI.Xaml.Media.FontFamily") {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Media_IFontFamilyFactory,
                        wuxm::IFontFamilyFactory)
        return Factory<FontFamilyObject>::QueryInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE CreateInstanceWithName(HSTRING name, IInspectable* outer,
                                                     IInspectable** inner,
                                                     wuxm::IFontFamily** instance) override {
        if (!instance) return E_POINTER;
        // Aggregation is what `outer` is for, and nothing here supports being
        // aggregated. Saying so beats ignoring the argument.
        if (outer) return E_NOTIMPL;
        if (inner) *inner = nullptr;

        auto* family = new FontFamilyObject();
        family->source = Utf8FromHString(name);
        family->AddRef();
        *instance = family;
        return S_OK;
    }
};

}  // namespace openxaml::winrt

using namespace openxaml::winrt;

namespace {

namespace xbf = openxaml::xbf;
namespace wuvm = ABI::Windows::UI::ViewManagement;
inline constexpr GUID IID_IAccessibilitySettings = {
    0xfe0e8147, 0xc4c0, 0x4562,
    {0xb9, 0x62, 0x13, 0x27, 0xb5, 0x2a, 0xd5, 0xb9}};

class AccessibilitySettingsObject final
    : public ComObject,
      public openxaml::abi::NotImpl_IAccessibilitySettings,
      public IWeakReferenceSource {
public:
    using PrimaryInterface = wuvm::IAccessibilitySettings;
    AccessibilitySettingsObject()
        : weak_state_(std::make_shared<WeakReferenceState>(
              this, static_cast<wuvm::IAccessibilitySettings*>(this))) {}
    ~AccessibilitySettingsObject() override {
        weak_state_->Invalidate();
        for (auto& [_, handler] : handlers_) handler->Release();
    }
    const wchar_t* RuntimeClassName() const override {
        return L"Windows.UI.ViewManagement.AccessibilitySettings";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(
            IID_IAccessibilitySettings, wuvm::IAccessibilitySettings)
        OPENXAML_QI_ARM(IID_IUnknown, wuvm::IAccessibilitySettings)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable,
                        wuvm::IAccessibilitySettings)
        OPENXAML_QI_ARM(IID_OpenXamlWeakReferenceSource, IWeakReferenceSource)
        *object = nullptr;
        return TraceQueryInterfaceMiss(RuntimeClassName(), iid);
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE get_HighContrast(boolean* value) override {
        if (!value) return E_POINTER;
        *value = 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_HighContrastScheme(HSTRING* value) override {
        if (!value) return E_POINTER;
        return WindowsCreateString(L"", 0, value);
    }
    HRESULT STDMETHODCALLTYPE add_HighContrastChanged(
        __FITypedEventHandler_2_Windows__CUI__CViewManagement__CAccessibilitySettings_IInspectable* handler,
        EventRegistrationToken* token) override {
        if (!handler || !token) return E_INVALIDARG;
        token->value = InterlockedIncrement64(&next_token_);
        handler->AddRef();
        handlers_[token->value] = handler;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE remove_HighContrastChanged(
        EventRegistrationToken token) override {
        const auto found = handlers_.find(token.value);
        if (found == handlers_.end()) return S_OK;
        found->second->Release();
        handlers_.erase(found);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetWeakReference(IWeakReference** value) override {
        if (!value) return E_POINTER;
        *value = new (std::nothrow) WeakReferenceObject(weak_state_);
        return *value ? S_OK : E_OUTOFMEMORY;
    }

private:
    std::shared_ptr<WeakReferenceState> weak_state_;
    LONGLONG next_token_ = 0;
    std::map<LONGLONG,
             __FITypedEventHandler_2_Windows__CUI__CViewManagement__CAccessibilitySettings_IInspectable*>
        handlers_;
};

class DispatcherTimerObject final
    : public ComObject,
      public openxaml::abi::NotImpl_IDispatcherTimer {
public:
    using PrimaryInterface = wux::IDispatcherTimer;
    ~DispatcherTimerObject() override {
        for (auto& [_, handler] : handlers_) handler->Release();
    }
    const wchar_t* RuntimeClassName() const override {
        return L"Windows.UI.Xaml.DispatcherTimer";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_IDispatcherTimer,
                        wux::IDispatcherTimer)
        OPENXAML_QI_ARM(IID_IUnknown, wux::IDispatcherTimer)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable, wux::IDispatcherTimer)
        *object = nullptr;
        return TraceQueryInterfaceMiss(RuntimeClassName(), iid);
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE get_Interval(wf::TimeSpan* value) override {
        if (!value) return E_POINTER;
        *value = interval_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Interval(wf::TimeSpan value) override {
        if (value.Duration < 0) return E_INVALIDARG;
        interval_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsEnabled(boolean* value) override {
        if (!value) return E_POINTER;
        *value = enabled_ ? 1 : 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE add_Tick(
        __FIEventHandler_1_IInspectable* handler,
        EventRegistrationToken* token) override {
        if (!handler || !token) return E_INVALIDARG;
        token->value = InterlockedIncrement64(&next_token_);
        handler->AddRef();
        handlers_[token->value] = handler;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE remove_Tick(EventRegistrationToken token) override {
        const auto found = handlers_.find(token.value);
        if (found == handlers_.end()) return S_OK;
        found->second->Release();
        handlers_.erase(found);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Start() override {
        enabled_ = true;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Stop() override {
        enabled_ = false;
        return S_OK;
    }

private:
    wf::TimeSpan interval_{};
    bool enabled_ = false;
    LONGLONG next_token_ = 0;
    std::map<LONGLONG, __FIEventHandler_1_IInspectable*> handlers_;
};

class DispatcherTimerFactory final
    : public Factory<DispatcherTimerObject>,
      public openxaml::abi::NotImpl_IDispatcherTimerFactory {
public:
    DispatcherTimerFactory() : Factory(L"Windows.UI.Xaml.DispatcherTimer") {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        return Factory<DispatcherTimerObject>::QueryInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()
    HRESULT STDMETHODCALLTYPE CreateInstance(
        IInspectable*, IInspectable** inner, wux::IDispatcherTimer** value) override {
        if (!inner || !value) return E_POINTER;
        auto* timer = new (std::nothrow) DispatcherTimerObject();
        if (!timer) return E_OUTOFMEMORY;
        auto* projected = static_cast<wux::IDispatcherTimer*>(timer);
        projected->AddRef();
        *inner = static_cast<IInspectable*>(projected);
        *value = projected;
        return S_OK;
    }
protected:
    HRESULT QueryStatics(REFIID iid, void** object) override {
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_IDispatcherTimerFactory,
                        wux::IDispatcherTimerFactory)
        return E_NOINTERFACE;
    }
};

IActivationFactory* FactoryFor(const wchar_t* name);

template <class Object, class FactoryInterface, class ResultInterface>
class ComposableFactory final : public Factory<Object>, public FactoryInterface {
public:
    ComposableFactory(const wchar_t* name, const GUID& iid)
        : Factory<Object>(name), iid_(iid) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        return Factory<Object>::QueryInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE CreateInstance(IInspectable* base_interface,
                                             IInspectable** inner_interface,
                                             ResultInterface** value) override {
        if (!inner_interface || !value) return E_POINTER;
        (void)base_interface;
        auto* instance = new Object();
        auto* projected = static_cast<ResultInterface*>(instance);
        projected->AddRef();
        *inner_interface = static_cast<IInspectable*>(projected);
        *value = projected;
        return S_OK;
    }

protected:
    HRESULT QueryStatics(REFIID iid, void** object) override {
        if (IsEqualGUID(iid, iid_)) {
            auto* pointer = static_cast<FactoryInterface*>(this);
            pointer->AddRef();
            *object = pointer;
            return S_OK;
        }
        return E_NOINTERFACE;
    }

private:
    const GUID& iid_;
};

Factory<BorderObject>& BorderFactory() {
    static Factory<BorderObject> factory(L"Windows.UI.Xaml.Controls.Border");
    return factory;
}
Factory<ValueSetObject>& ValueSetFactory() {
    static Factory<ValueSetObject> factory(
        L"Windows.Foundation.Collections.ValueSet");
    return factory;
}
Factory<AccessibilitySettingsObject>& AccessibilitySettingsFactory() {
    static Factory<AccessibilitySettingsObject> factory(
        L"Windows.UI.ViewManagement.AccessibilitySettings");
    return factory;
}
DispatcherTimerFactory& TheDispatcherTimerFactory() {
    static DispatcherTimerFactory factory;
    return factory;
}
ColorsFactory& TheColorsFactory() {
    static ColorsFactory factory;
    return factory;
}
VisualStateManagerFactory& TheVisualStateManagerFactory() {
    static VisualStateManagerFactory factory;
    return factory;
}
TimelineFactory& TheTimelineFactory() {
    static TimelineFactory factory;
    return factory;
}
AutomationPropertiesFactory& TheAutomationPropertiesFactory() {
    static AutomationPropertiesFactory factory;
    return factory;
}
FontWeightsFactory& TheFontWeightsFactory() {
    static FontWeightsFactory factory;
    return factory;
}
ToolTipServiceFactory& TheToolTipServiceFactory() {
    static ToolTipServiceFactory factory;
    return factory;
}
FlyoutBaseFactory& TheFlyoutBaseFactory() {
    static FlyoutBaseFactory factory;
    return factory;
}
PanelStaticsFactory& PanelFactory() {
    static PanelStaticsFactory factory;
    return factory;
}
ComposableFactory<StackPanelObject, wuxc::IStackPanelFactory, wuxc::IStackPanel>&
StackPanelFactory() {
    static ComposableFactory<StackPanelObject, wuxc::IStackPanelFactory, wuxc::IStackPanel>
        factory(L"Windows.UI.Xaml.Controls.StackPanel",
                ::openxaml::iid::Windows_UI_Xaml_Controls_IStackPanelFactory);
    return factory;
}
Factory<CanvasObject>& CanvasFactory() {
    static Factory<CanvasObject> factory(L"Windows.UI.Xaml.Controls.Canvas"); return factory;
}
class SwapChainPanelActivationFactory final
    : public Factory<SwapChainPanelObject>,
      public openxaml::abi::NotImpl_ISwapChainPanelFactory,
      public openxaml::abi::NotImpl_ISwapChainPanelStatics {
public:
    SwapChainPanelActivationFactory()
        : Factory(L"Windows.UI.Xaml.Controls.SwapChainPanel") {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        return Factory<SwapChainPanelObject>::QueryInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()
    HRESULT STDMETHODCALLTYPE CreateInstance(
        IInspectable* base_interface, IInspectable** inner_interface,
        wuxc::ISwapChainPanel** value) override {
        if (base_interface) return E_NOTIMPL;
        return CreateComposableObject<SwapChainPanelObject>(inner_interface, value);
    }
    HRESULT STDMETHODCALLTYPE get_CompositionScaleXProperty(
        wux::IDependencyProperty** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_CompositionScaleYProperty(
        wux::IDependencyProperty** value) override {
        return get_CompositionScaleXProperty(value);
    }
protected:
    HRESULT QueryStatics(REFIID iid, void** object) override {
        OPENXAML_QI_ARM(
            ::openxaml::iid::Windows_UI_Xaml_Controls_ISwapChainPanelFactory,
            wuxc::ISwapChainPanelFactory)
        OPENXAML_QI_ARM(
            ::openxaml::iid::Windows_UI_Xaml_Controls_ISwapChainPanelStatics,
            wuxc::ISwapChainPanelStatics)
        return E_NOINTERFACE;
    }
};
SwapChainPanelActivationFactory& SwapChainPanelFactory() {
    static SwapChainPanelActivationFactory factory;
    return factory;
}
ComposableFactory<ContentPresenterObject, wuxc::IContentPresenterFactory,
                  wuxc::IContentPresenter>& ContentPresenterFactory() {
    static ComposableFactory<ContentPresenterObject, wuxc::IContentPresenterFactory,
                             wuxc::IContentPresenter>
        factory(L"Windows.UI.Xaml.Controls.ContentPresenter",
                ::openxaml::iid::Windows_UI_Xaml_Controls_IContentPresenterFactory);
    return factory;
}
Factory<ImageObject>& ImageFactory() {
    static Factory<ImageObject> factory(L"Windows.UI.Xaml.Controls.Image"); return factory;
}
Factory<PathObject>& PathFactory() {
    static Factory<PathObject> factory(L"Windows.UI.Xaml.Shapes.Path"); return factory;
}
Factory<PathIconObject>& PathIconFactory() {
    static Factory<PathIconObject> factory(L"Windows.UI.Xaml.Controls.PathIcon"); return factory;
}
Factory<TextBlockObject>& TextBlockFactory() {
    static Factory<TextBlockObject> factory(L"Windows.UI.Xaml.Controls.TextBlock");
    return factory;
}
Factory<RunObject>& RunFactory() {
    static Factory<RunObject> factory(L"Windows.UI.Xaml.Documents.Run");
    return factory;
}
Factory<LineBreakObject>& LineBreakFactory() {
    static Factory<LineBreakObject> factory(L"Windows.UI.Xaml.Documents.LineBreak");
    return factory;
}
MenuFlyoutActivationFactory& MenuFlyoutFactory() {
    static MenuFlyoutActivationFactory factory;
    return factory;
}
MenuFlyoutItemActivationFactory& MenuFlyoutItemFactory() {
    static MenuFlyoutItemActivationFactory factory;
    return factory;
}
MenuFlyoutSeparatorActivationFactory& MenuFlyoutSeparatorFactory() {
    static MenuFlyoutSeparatorActivationFactory factory;
    return factory;
}
Factory<MenuFlyoutSubItemObject>& MenuFlyoutSubItemFactory() {
    static Factory<MenuFlyoutSubItemObject> factory(
        L"Windows.UI.Xaml.Controls.MenuFlyoutSubItem");
    return factory;
}
Factory<MuxcBitmapIconSourceObject>& MuxcBitmapIconSourceFactory() {
    static Factory<MuxcBitmapIconSourceObject> factory(
        L"Microsoft.UI.Xaml.Controls.BitmapIconSource");
    return factory;
}
BitmapIconSourceActivationFactory& BitmapIconSourceFactory() {
    static BitmapIconSourceActivationFactory factory;
    return factory;
}
IconSourceElementActivationFactory& IconSourceElementFactory() {
    static IconSourceElementActivationFactory factory;
    return factory;
}
ToolTipActivationFactory& ToolTipFactory() {
    static ToolTipActivationFactory factory;
    return factory;
}
Factory<ImageBrushObject>& ImageBrushFactory() {
    static Factory<ImageBrushObject> factory(L"Windows.UI.Xaml.Media.ImageBrush");
    return factory;
}
SolidColorBrushActivationFactory& SolidColorBrushFactory() {
    static SolidColorBrushActivationFactory factory;
    return factory;
}
PropertyChangedEventArgsActivationFactory& PropertyChangedEventArgsFactory() {
    static PropertyChangedEventArgsActivationFactory factory;
    return factory;
}
Factory<ContentControlObject>& ContentControlFactory() {
    static Factory<ContentControlObject> factory(L"Windows.UI.Xaml.Controls.ContentControl");
    return factory;
}
Factory<ResourceDictionaryObject>& ResourceDictionaryFactory() {
    static Factory<ResourceDictionaryObject> factory(L"Windows.UI.Xaml.ResourceDictionary");
    return factory;
}
Factory<XamlControlsResourcesObject>& XamlControlsResourcesFactory() {
    static Factory<XamlControlsResourcesObject> factory(
        L"Microsoft.UI.Xaml.Controls.XamlControlsResources");
    return factory;
}
ComposableFactory<PageObject, wuxc::IPageFactory, wuxc::IPage>& PageFactory() {
    static ComposableFactory<PageObject, wuxc::IPageFactory, wuxc::IPage> factory(
        L"Windows.UI.Xaml.Controls.Page",
        ::openxaml::iid::Windows_UI_Xaml_Controls_IPageFactory);
    return factory;
}
ComposableFactory<UserControlObject, wuxc::IUserControlFactory, wuxc::IUserControl>&
UserControlFactory() {
    static ComposableFactory<UserControlObject, wuxc::IUserControlFactory, wuxc::IUserControl>
        factory(L"Windows.UI.Xaml.Controls.UserControl",
                ::openxaml::iid::Windows_UI_Xaml_Controls_IUserControlFactory);
    return factory;
}
Factory<FrameObject>& FrameFactory() {
    static Factory<FrameObject> factory(L"Windows.UI.Xaml.Controls.Frame");
    return factory;
}
Factory<ItemsControlObject>& ItemsControlFactory() {
    static Factory<ItemsControlObject> factory(L"Windows.UI.Xaml.Controls.ItemsControl");
    return factory;
}
Factory<ListViewObject>& ListViewFactory() {
    static Factory<ListViewObject> factory(L"Windows.UI.Xaml.Controls.ListView");
    return factory;
}
Factory<PopupObject>& PopupFactory() {
    static Factory<PopupObject> factory(L"Windows.UI.Xaml.Controls.Primitives.Popup");
    return factory;
}
Factory<ButtonObject>& ButtonFactory() {
    static Factory<ButtonObject> factory(L"Windows.UI.Xaml.Controls.Button"); return factory;
}
Factory<TextBoxObject>& TextBoxFactory() {
    static Factory<TextBoxObject> factory(L"Windows.UI.Xaml.Controls.TextBox"); return factory;
}
Factory<ThumbObject>& ThumbFactory() {
    static Factory<ThumbObject> factory(L"Windows.UI.Xaml.Controls.Primitives.Thumb"); return factory;
}
Factory<VisualBellLightObject>& VisualBellLightFactory() {
    static Factory<VisualBellLightObject> factory(
        L"Microsoft.Terminal.Control.VisualBellLight");
    return factory;
}
Factory<ScrollBarObject>& ScrollBarFactory() {
    static Factory<ScrollBarObject> factory(
        L"Windows.UI.Xaml.Controls.Primitives.ScrollBar");
    return factory;
}
Factory<ScrollViewerObject>& ScrollViewerFactory() {
    static Factory<ScrollViewerObject> factory(L"Windows.UI.Xaml.Controls.ScrollViewer"); return factory;
}
FontIconActivationFactory& FontIconFactory() {
    static FontIconActivationFactory factory;
    return factory;
}
SymbolIconActivationFactory& SymbolIconFactory() {
    static SymbolIconActivationFactory factory;
    return factory;
}
Factory<RectangleObject>& RectangleFactory() {
    static Factory<RectangleObject> factory(L"Windows.UI.Xaml.Shapes.Rectangle"); return factory;
}

FontFamilyFactory& TheFontFamilyFactory() {
    static FontFamilyFactory factory;
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
DurationHelperFactory& TheDurationHelperFactory() {
    static DurationHelperFactory factory;
    return factory;
}
GridLengthHelperFactory& TheGridLengthHelperFactory() {
    static GridLengthHelperFactory factory;
    return factory;
}
ApplicationFactory& TheApplicationFactory() {
    static ApplicationFactory factory;
    return factory;
}
Factory<XamlControlsMetadataProviderObject>& XamlControlsMetadataProviderFactory() {
    static Factory<XamlControlsMetadataProviderObject> factory(
        L"Microsoft.UI.Xaml.XamlTypeInfo.XamlControlsXamlMetaDataProvider");
    return factory;
}
Factory<TabViewObject>& TheTabViewFactory() {
    static Factory<TabViewObject> factory(L"Microsoft.UI.Xaml.Controls.TabView");
    return factory;
}
TabViewItemActivationFactory& TheTabViewItemFactory() {
    static TabViewItemActivationFactory factory;
    return factory;
}
Factory<SplitButtonObject>& TheSplitButtonFactory() {
    static Factory<SplitButtonObject> factory(L"Microsoft.UI.Xaml.Controls.SplitButton");
    return factory;
}
CommandBarFlyoutActivationFactory& TheCommandBarFlyoutFactory() {
    static CommandBarFlyoutActivationFactory factory;
    return factory;
}
ProgressRingActivationFactory& TheProgressRingFactory() {
    static ProgressRingActivationFactory factory;
    return factory;
}
AppBarButtonActivationFactory& TheAppBarButtonFactory() {
    static AppBarButtonActivationFactory factory;
    return factory;
}
ContentDialogActivationFactory& TheContentDialogFactory() {
    static ContentDialogActivationFactory factory;
    return factory;
}
DispatcherQueueFactory& TheDispatcherQueueFactory() {
    static DispatcherQueueFactory factory;
    return factory;
}
WindowsXamlManagerFactory& TheWindowsXamlManagerFactory() {
    static WindowsXamlManagerFactory factory;
    return factory;
}
ResourceManagerFactory& TheResourceManagerFactory() {
    static ResourceManagerFactory factory;
    return factory;
}
ResourceContextFactory& TheResourceContextFactory() {
    static ResourceContextFactory factory;
    return factory;
}
DesktopWindowXamlSourceFactory& TheDesktopWindowXamlSourceFactory() {
    static DesktopWindowXamlSourceFactory factory;
    return factory;
}

// Where the harvested font metrics are. A real implementation reads the
// system font directory; this one is told, because the metrics are a build
// artefact of phase 3 rather than something installed on the machine.
//
// Loaded once, on the first activation. A TextBlock with no metrics behind it
// fails when it measures, naming the family it could not find, which is a
// better failure than refusing to load the DLL at all.
void EnsureFontMetrics() {
    static const int loaded = [] {
        char path[4096];
        const DWORD length =
            GetEnvironmentVariableA("OPENXAML_FONT_METRICS", path, sizeof path);
        if (length == 0 || length >= sizeof path) return 0;
        return openxaml::LoadFontDirectory(openxaml::FontLibrary::Default(), path);
    }();
    (void)loaded;
}

IActivationFactory* FactoryFor(const wchar_t* name) {
    if (!name) return nullptr;
    if (wcscmp(name, L"Windows.Foundation.Collections.ValueSet") == 0)
        return &ValueSetFactory();
    if (wcscmp(name, L"Windows.UI.ViewManagement.AccessibilitySettings") == 0)
        return &AccessibilitySettingsFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.DispatcherTimer") == 0)
        return &TheDispatcherTimerFactory();
    if (wcscmp(name, L"Windows.UI.Colors") == 0) {
        if (GetEnvironmentVariableW(L"OPENXAML_TRACE_QI", nullptr, 0))
            OutputDebugStringA("OpenXaml: activating Windows.UI.Colors\n");
        return &TheColorsFactory();
    }
    if (wcscmp(name, L"Windows.UI.Xaml.VisualStateManager") == 0)
        return &TheVisualStateManagerFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Media.Animation.Timeline") == 0)
        return &TheTimelineFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Automation.AutomationProperties") == 0)
        return &TheAutomationPropertiesFactory();
    if (wcscmp(name, L"Windows.UI.Text.FontWeights") == 0)
        return &TheFontWeightsFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.ToolTipService") == 0)
        return &TheToolTipServiceFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.Primitives.FlyoutBase") == 0)
        return &TheFlyoutBaseFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.Border") == 0)
        return &BorderFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.Panel") == 0)
        return &PanelFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.Grid") == 0)
        return &TheGridFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.StackPanel") == 0)
        return &StackPanelFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.Canvas") == 0) return &CanvasFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.SwapChainPanel") == 0)
        return &SwapChainPanelFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.ContentPresenter") == 0)
        return &ContentPresenterFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.Image") == 0) return &ImageFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Shapes.Path") == 0) return &PathFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.PathIcon") == 0) return &PathIconFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.TextBlock") == 0)
        return &TextBlockFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Documents.Run") == 0)
        return &RunFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Documents.LineBreak") == 0)
        return &LineBreakFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Data.PropertyChangedEventArgs") == 0)
        return &PropertyChangedEventArgsFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.ContentControl") == 0)
        return &ContentControlFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.ContentDialog") == 0)
        return &TheContentDialogFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.ResourceDictionary") == 0)
        return &ResourceDictionaryFactory();
    if (wcscmp(name, L"Microsoft.UI.Xaml.Controls.XamlControlsResources") == 0)
        return &XamlControlsResourcesFactory();
    if (wcscmp(name, L"Microsoft.UI.Xaml.Controls.TabView") == 0)
        return &TheTabViewFactory();
    if (wcscmp(name, L"Microsoft.UI.Xaml.Controls.TabViewItem") == 0)
        return &TheTabViewItemFactory();
    if (wcscmp(name, L"Microsoft.UI.Xaml.Controls.SplitButton") == 0)
        return &TheSplitButtonFactory();
    if (wcscmp(name, L"Microsoft.UI.Xaml.Controls.CommandBarFlyout") == 0)
        return &TheCommandBarFlyoutFactory();
    if (wcscmp(name, L"Microsoft.UI.Xaml.Controls.ProgressRing") == 0)
        return &TheProgressRingFactory();
    if (wcscmp(name, L"Microsoft.UI.Xaml.Controls.BitmapIconSource") == 0)
        return &MuxcBitmapIconSourceFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.Page") == 0)
        return &PageFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.UserControl") == 0)
        return &UserControlFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.Frame") == 0)
        return &FrameFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.ItemsControl") == 0)
        return &ItemsControlFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.ListView") == 0)
        return &ListViewFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.Primitives.Popup") == 0)
        return &PopupFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.MenuFlyout") == 0)
        return &MenuFlyoutFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.MenuFlyoutItem") == 0)
        return &MenuFlyoutItemFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.MenuFlyoutSeparator") == 0)
        return &MenuFlyoutSeparatorFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.MenuFlyoutSubItem") == 0)
        return &MenuFlyoutSubItemFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.BitmapIconSource") == 0)
        return &BitmapIconSourceFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.IconSourceElement") == 0)
        return &IconSourceElementFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.Button") == 0) return &ButtonFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.AppBarButton") == 0)
        return &TheAppBarButtonFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.TextBox") == 0) return &TextBoxFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.ToolTip") == 0) return &ToolTipFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.Primitives.Thumb") == 0) return &ThumbFactory();
    if (wcscmp(name, L"Microsoft.Terminal.Control.VisualBellLight") == 0)
        return &VisualBellLightFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.Primitives.ScrollBar") == 0)
        return &ScrollBarFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.ScrollViewer") == 0) return &ScrollViewerFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.FontIcon") == 0) return &FontIconFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.SymbolIcon") == 0) return &SymbolIconFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Shapes.Rectangle") == 0) return &RectangleFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Media.FontFamily") == 0)
        return &TheFontFamilyFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Media.ImageBrush") == 0)
        return &ImageBrushFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Media.SolidColorBrush") == 0)
        return &SolidColorBrushFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.ColumnDefinition") == 0)
        return &ColumnDefinitionFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.RowDefinition") == 0)
        return &RowDefinitionFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.Primitives.LayoutInformation") == 0)
        return &TheLayoutInformationFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.DurationHelper") == 0)
        return &TheDurationHelperFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.GridLengthHelper") == 0)
        return &TheGridLengthHelperFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Application") == 0)
        return &TheApplicationFactory();
    if (wcscmp(name,
               L"Microsoft.UI.Xaml.XamlTypeInfo.XamlControlsXamlMetaDataProvider") == 0)
        return &XamlControlsMetadataProviderFactory();
    if (wcscmp(name, L"Windows.System.DispatcherQueue") == 0)
        return &TheDispatcherQueueFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Hosting.WindowsXamlManager") == 0)
        return &TheWindowsXamlManagerFactory();
    if (wcscmp(name, L"Windows.ApplicationModel.Resources.Core.ResourceManager") == 0)
        return &TheResourceManagerFactory();
    if (wcscmp(name, L"Windows.ApplicationModel.Resources.Core.ResourceContext") == 0)
        return &TheResourceContextFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Hosting.DesktopWindowXamlSource") == 0)
        return &TheDesktopWindowXamlSourceFactory();
    return nullptr;
}

// --- XBF object writer adapter ------------------------------------------------
//
// The portable reader deliberately stops at a generic graph. This adapter is
// the Windows-specific half: it activates ABI objects, applies the properties
// the runtime currently projects, populates collections, and reports every
// x:ConnectionId to the generated component connector.

constexpr GUID kComponentConnectorIid = {
    0xf6790987, 0xe6e5, 0x47f2, {0x92, 0xc6, 0xec, 0xcc, 0xe4, 0xba, 0x15, 0x9a}};

double ConstantNumber(const xbf::Constant& value) {
    switch (value.kind) {
    case xbf::ConstantKind::Float:
    case xbf::ConstantKind::GridLength:
        return value.floats.empty() ? 0.0 : value.floats.front();
    case xbf::ConstantKind::Signed:
        return value.signed_value;
    case xbf::ConstantKind::Enum:
    case xbf::ConstantKind::Color:
        return value.unsigned_value;
    case xbf::ConstantKind::True:
        return 1.0;
    default:
        return 0.0;
    }
}

INT32 ConstantInteger(const xbf::Constant& value) {
    return static_cast<INT32>(ConstantNumber(value));
}

bool EndsWith(const std::string& value, const char* suffix) {
    const std::size_t length = std::strlen(suffix);
    return value.size() >= length &&
           value.compare(value.size() - length, length, suffix) == 0;
}

std::wstring WideFromUtf8(const std::string& value) {
    if (value.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                           static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) return {};
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                        static_cast<int>(value.size()), result.data(), length);
    return result;
}

HRESULT ActivateXbfObject(const std::string& type, IInspectable** result) {
    if (!result) return E_POINTER;
    *result = nullptr;
    const std::wstring name = WideFromUtf8(type);
    if (name.empty()) return E_INVALIDARG;
    if (IActivationFactory* factory = FactoryFor(name.c_str()))
        return factory->ActivateInstance(result);

    HRESULT hr = TheApplicationFactory().ActivateLocalXamlType(type, result);
    if (SUCCEEDED(hr)) return hr;

    HSTRING class_name = nullptr;
    hr = WindowsCreateString(name.c_str(), static_cast<UINT32>(name.size()), &class_name);
    if (SUCCEEDED(hr)) hr = RoActivateInstance(class_name, result);
    WindowsDeleteString(class_name);
    return hr;
}

void TraceXbfFailure(const char* operation, const std::string& subject, HRESULT hr) {
    char code[24];
    std::snprintf(code, sizeof(code), "0x%08lx",
                  static_cast<unsigned long>(static_cast<ULONG>(hr)));
    std::string message = "OpenXaml XBF ";
    message += operation;
    message += " failed for ";
    message += subject;
    message += " (";
    message += code;
    message += ")\n";
    OutputDebugStringA(message.c_str());
}

template <class Interface>
HRESULT Query(IInspectable* object, const GUID& iid, Interface** result) {
    if (!object || !result) return E_INVALIDARG;
    *result = nullptr;
    return object->QueryInterface(iid, reinterpret_cast<void**>(result));
}

wux::Thickness ConstantThickness(const xbf::Constant& value) {
    wux::Thickness result{};
    if (value.floats.size() == 4) {
        result.Left = value.floats[0];
        result.Top = value.floats[1];
        result.Right = value.floats[2];
        result.Bottom = value.floats[3];
    } else {
        result.Left = result.Top = result.Right = result.Bottom = ConstantNumber(value);
    }
    return result;
}

wux::GridLength ConstantGridLength(const xbf::Constant& value) {
    wux::GridLength result{};
    result.Value = ConstantNumber(value);
    result.GridUnitType = static_cast<wux::GridUnitType>(value.unsigned_value);
    return result;
}

HRESULT BuildXbfObject(const std::shared_ptr<xbf::Object>& graph,
                       IInspectable* existing,
                       wuxmk::IComponentConnector* connector,
                       IInspectable** result);

HRESULT AppendPanelChild(IInspectable* parent, IInspectable* child) {
    wuxc::IPanel* panel = nullptr;
    HRESULT hr = Query(parent, ::openxaml::iid::Windows_UI_Xaml_Controls_IPanel, &panel);
    if (FAILED(hr)) return hr;
    __FIVector_1_Windows__CUI__CXaml__CUIElement* children = nullptr;
    hr = panel->get_Children(&children);
    panel->Release();
    if (FAILED(hr)) return hr;
    wux::IUIElement* element = nullptr;
    hr = Query(child, ::openxaml::iid::Windows_UI_Xaml_IUIElement, &element);
    if (SUCCEEDED(hr)) hr = children->Append(element);
    if (element) element->Release();
    children->Release();
    return hr;
}

HRESULT AppendGridDefinition(IInspectable* parent, IInspectable* child, bool column) {
    wuxc::IGrid* grid = nullptr;
    HRESULT hr = Query(parent, ::openxaml::iid::Windows_UI_Xaml_Controls_IGrid, &grid);
    if (FAILED(hr)) return hr;
    if (column) {
        __FIVector_1_Windows__CUI__CXaml__CControls__CColumnDefinition* definitions = nullptr;
        hr = grid->get_ColumnDefinitions(&definitions);
        if (SUCCEEDED(hr)) {
            wuxc::IColumnDefinition* definition = nullptr;
            hr = Query(child, ::openxaml::iid::Windows_UI_Xaml_Controls_IColumnDefinition,
                       &definition);
            if (SUCCEEDED(hr)) hr = definitions->Append(definition);
            if (definition) definition->Release();
            definitions->Release();
        }
    } else {
        __FIVector_1_Windows__CUI__CXaml__CControls__CRowDefinition* definitions = nullptr;
        hr = grid->get_RowDefinitions(&definitions);
        if (SUCCEEDED(hr)) {
            wuxc::IRowDefinition* definition = nullptr;
            hr = Query(child, ::openxaml::iid::Windows_UI_Xaml_Controls_IRowDefinition,
                       &definition);
            if (SUCCEEDED(hr)) hr = definitions->Append(definition);
            if (definition) definition->Release();
            definitions->Release();
        }
    }
    grid->Release();
    return hr;
}

HRESULT SetSingleChild(IInspectable* parent, IInspectable* child,
                       const std::string& property) {
    if (property == "Windows.UI.Xaml.Controls.ToolTipService.ToolTip") return S_OK;
    if (property == "Windows.UI.Xaml.UIElement.ContextFlyout") {
        wux::IUIElement4* element = nullptr;
        HRESULT hr = Query(parent, ::openxaml::iid::Windows_UI_Xaml_IUIElement4,
                           &element);
        if (FAILED(hr)) return hr;
        wuxcp::IFlyoutBase* flyout = nullptr;
        hr = Query(child,
                   ::openxaml::iid::Windows_UI_Xaml_Controls_Primitives_IFlyoutBase,
                   &flyout);
        if (SUCCEEDED(hr)) hr = element->put_ContextFlyout(flyout);
        if (flyout) flyout->Release();
        element->Release();
        return hr;
    }
    if (property == "Windows.UI.Xaml.Controls.Button.Flyout") {
        wuxc::IButtonWithFlyout* button = nullptr;
        HRESULT hr = Query(parent,
                           ::openxaml::iid::Windows_UI_Xaml_Controls_IButtonWithFlyout,
                           &button);
        if (FAILED(hr)) return hr;
        wuxcp::IFlyoutBase* flyout = nullptr;
        hr = Query(child,
                   ::openxaml::iid::Windows_UI_Xaml_Controls_Primitives_IFlyoutBase,
                   &flyout);
        if (SUCCEEDED(hr)) hr = button->put_Flyout(flyout);
        if (flyout) flyout->Release();
        button->Release();
        return hr;
    }
    if (property == "Windows.UI.Xaml.Controls.AppBarButton.Icon") {
        wuxc::IAppBarButton* button = nullptr;
        HRESULT hr = Query(parent,
                           ::openxaml::iid::Windows_UI_Xaml_Controls_IAppBarButton,
                           &button);
        if (FAILED(hr)) return hr;
        wuxc::IIconElement* icon = nullptr;
        hr = Query(child, ::openxaml::iid::Windows_UI_Xaml_Controls_IIconElement,
                   &icon);
        if (SUCCEEDED(hr)) hr = button->put_Icon(icon);
        if (icon) icon->Release();
        button->Release();
        return hr;
    }
    if (property == "Windows.UI.Xaml.Controls.Panel.Background") {
        wuxc::IPanel* panel = nullptr;
        HRESULT hr = Query(parent, ::openxaml::iid::Windows_UI_Xaml_Controls_IPanel,
                           &panel);
        if (FAILED(hr)) return hr;
        wuxm::IBrush* brush = nullptr;
        hr = Query(child, ::openxaml::iid::Windows_UI_Xaml_Media_IBrush, &brush);
        if (SUCCEEDED(hr)) hr = panel->put_Background(brush);
        if (brush) brush->Release();
        panel->Release();
        return hr;
    }
    if (property == "Windows.UI.Xaml.Application.Resources") {
        wux::IApplication* application = nullptr;
        HRESULT hr = Query(parent, ::openxaml::iid::Windows_UI_Xaml_IApplication,
                           &application);
        if (FAILED(hr)) return hr;
        wux::IResourceDictionary* dictionary = nullptr;
        hr = child->QueryInterface(IID_IResourceDictionary,
                                   reinterpret_cast<void**>(&dictionary));
        if (SUCCEEDED(hr)) hr = application->put_Resources(dictionary);
        if (dictionary) dictionary->Release();
        application->Release();
        return hr;
    }
    if (property == "Windows.UI.Xaml.FrameworkElement.Resources") {
        wux::IFrameworkElement* framework = nullptr;
        HRESULT hr = Query(parent, ::openxaml::iid::Windows_UI_Xaml_IFrameworkElement,
                           &framework);
        if (FAILED(hr)) return hr;
        wux::IResourceDictionary* dictionary = nullptr;
        hr = child->QueryInterface(IID_IResourceDictionary,
                                   reinterpret_cast<void**>(&dictionary));
        if (SUCCEEDED(hr)) hr = framework->put_Resources(dictionary);
        if (dictionary) dictionary->Release();
        framework->Release();
        return hr;
    }
    if (property == "Windows.UI.Xaml.Controls.Border.Child") {
        wuxc::IBorder* border = nullptr;
        HRESULT hr = Query(parent, ::openxaml::iid::Windows_UI_Xaml_Controls_IBorder, &border);
        if (FAILED(hr)) return hr;
        wux::IUIElement* element = nullptr;
        hr = Query(child, ::openxaml::iid::Windows_UI_Xaml_IUIElement, &element);
        if (SUCCEEDED(hr)) hr = border->put_Child(element);
        if (element) element->Release();
        border->Release();
        return hr;
    }
    if (property == "Windows.UI.Xaml.Controls.ContentPresenter.Content") {
        wuxc::IContentPresenter* presenter = nullptr;
        HRESULT hr = Query(parent,
                           ::openxaml::iid::Windows_UI_Xaml_Controls_IContentPresenter,
                           &presenter);
        if (FAILED(hr)) return hr;
        hr = presenter->put_Content(child);
        presenter->Release();
        return hr;
    }
    wuxc::IContentControl* control = nullptr;
    HRESULT hr = Query(parent, ::openxaml::iid::Windows_UI_Xaml_Controls_IContentControl,
                       &control);
    if (FAILED(hr)) return hr;
    hr = control->put_Content(child);
    control->Release();
    return hr;
}

HRESULT ApplyCollection(const std::string& property, const xbf::Value& value,
                        IInspectable* parent, wuxmk::IComponentConnector* connector) {
    if (!value.object) return E_INVALIDARG;
    IOpenXamlResourceDictionary* resources = nullptr;
    const bool merged =
        property == "Windows.UI.Xaml.ResourceDictionary.MergedDictionaries";
    const bool themes =
        property == "Windows.UI.Xaml.ResourceDictionary.ThemeDictionaries";
    if (merged || themes) {
        HRESULT hr = parent->QueryInterface(IID_IOpenXamlResourceDictionary,
                                             reinterpret_cast<void**>(&resources));
        if (FAILED(hr)) return hr;
    }
    for (const auto& item : value.object->items) {
        if (item.kind != xbf::Value::Kind::Object || !item.object) continue;
        // x:Load="False" is encoded as an internal DeferredElement whose
        // payload lives in a custom-runtime-data substream. It is a marker,
        // not an activatable WinRT class, and stays absent from the live
        // collection until the owning generated binding requests it.
        if (item.object->type == "Windows.UI.Xaml.Internal.DeferredElement") continue;
        IInspectable* child = nullptr;
        HRESULT hr = BuildXbfObject(item.object, nullptr, connector, &child);
        if (FAILED(hr)) {
            if (resources) resources->Release();
            return hr;
        }
        if (property == "Windows.UI.Xaml.Controls.Panel.Children") {
            hr = AppendPanelChild(parent, child);
        } else if (property == "Windows.UI.Xaml.Controls.Grid.ColumnDefinitions") {
            hr = AppendGridDefinition(parent, child, true);
        } else if (property == "Windows.UI.Xaml.Controls.Grid.RowDefinitions") {
            hr = AppendGridDefinition(parent, child, false);
        } else if (merged) {
            hr = resources->AppendMerged(child);
        } else if (property == "Windows.UI.Xaml.Controls.TextBlock.Inlines") {
            // The Run is still activated and connected above. Text rendering
            // currently stores TextBlock's flattened text rather than the
            // public InlineCollection; retaining that projection comes with
            // the documents layer.
            hr = S_OK;
        } else if (property == "Windows.UI.Xaml.UIElement.Lights") {
            // Lights are retained by the generated component connector.  The
            // GDI island backend has no composition-light renderer.
            hr = S_OK;
        } else if (property ==
                       "Microsoft.UI.Xaml.Controls.CommandBarFlyout.PrimaryCommands" ||
                   property ==
                       "Microsoft.UI.Xaml.Controls.CommandBarFlyout.SecondaryCommands") {
            IMuxcCommandBarFlyout* projected = nullptr;
            hr = parent->QueryInterface(IID_IMuxcCommandBarFlyout,
                                        reinterpret_cast<void**>(&projected));
            if (SUCCEEDED(hr)) {
                auto* flyout = static_cast<CommandBarFlyoutObject*>(projected);
                hr = flyout->AppendCommand(child, EndsWith(property, ".SecondaryCommands"));
                projected->Release();
            }
        } else {
            hr = E_NOTIMPL;
        }
        child->Release();
        if (FAILED(hr)) {
            if (resources) resources->Release();
            return hr;
        }
    }
    if (themes) {
        for (const auto& [key, item] : value.object->dictionary) {
            if (item.kind != xbf::Value::Kind::Object || !item.object) continue;
            IInspectable* child = nullptr;
            HRESULT hr = BuildXbfObject(item.object, nullptr, connector, &child);
            if (FAILED(hr)) {
                resources->Release();
                return hr;
            }
            hr = resources->InsertTheme(key.c_str(), child);
            child->Release();
            if (FAILED(hr)) {
                resources->Release();
                return hr;
            }
        }
    }
    if (resources) resources->Release();
    return S_OK;
}

HRESULT ApplyConstantProperty(const std::string& property, const xbf::Constant& value,
                              IInspectable* object) {
    if (property == "x:ConnectionId" ||
        property == "Windows.UI.Xaml.DependencyObject.Name" ||
        property == "x:Uid") return S_OK;

    if (property == "Windows.UI.Xaml.ResourceDictionary.Source") {
        IOpenXamlResourceDictionary* dictionary = nullptr;
        HRESULT hr = object->QueryInterface(IID_IOpenXamlResourceDictionary,
                                             reinterpret_cast<void**>(&dictionary));
        if (FAILED(hr)) return hr;
        hr = dictionary->SetSourceText(value.string_value.c_str());
        dictionary->Release();
        return hr;
    }

    if (property == "Windows.UI.Xaml.Media.SolidColorBrush.Color") {
        wuxm::ISolidColorBrush* brush = nullptr;
        HRESULT hr = Query(object,
                           ::openxaml::iid::Windows_UI_Xaml_Media_ISolidColorBrush,
                           &brush);
        if (FAILED(hr)) return hr;
        const std::uint32_t argb = value.unsigned_value;
        const ABI::Windows::UI::Color color{
            static_cast<BYTE>((argb >> 24) & 0xff),
            static_cast<BYTE>((argb >> 16) & 0xff),
            static_cast<BYTE>((argb >> 8) & 0xff),
            static_cast<BYTE>(argb & 0xff)};
        hr = brush->put_Color(color);
        brush->Release();
        return hr;
    }

    wux::IFrameworkElement* framework = nullptr;
    if (property.rfind("Windows.UI.Xaml.FrameworkElement.", 0) == 0) {
        HRESULT hr = Query(object, ::openxaml::iid::Windows_UI_Xaml_IFrameworkElement,
                           &framework);
        if (FAILED(hr)) return hr;
        const std::string name = property.substr(property.rfind('.') + 1);
        if (name == "Width") hr = framework->put_Width(ConstantNumber(value));
        else if (name == "Height") hr = framework->put_Height(ConstantNumber(value));
        else if (name == "MinWidth") hr = framework->put_MinWidth(ConstantNumber(value));
        else if (name == "MaxWidth") hr = framework->put_MaxWidth(ConstantNumber(value));
        else if (name == "MinHeight") hr = framework->put_MinHeight(ConstantNumber(value));
        else if (name == "MaxHeight") hr = framework->put_MaxHeight(ConstantNumber(value));
        else if (name == "Margin") hr = framework->put_Margin(ConstantThickness(value));
        else if (name == "HorizontalAlignment")
            hr = framework->put_HorizontalAlignment(
                static_cast<wux::HorizontalAlignment>(ConstantInteger(value)));
        else if (name == "VerticalAlignment")
            hr = framework->put_VerticalAlignment(
                static_cast<wux::VerticalAlignment>(ConstantInteger(value)));
        else hr = S_OK;
        framework->Release();
        return hr;
    }

    if (property == "Windows.UI.Xaml.Controls.Grid.Column" ||
        property == "Windows.UI.Xaml.Controls.Grid.Row" ||
        property == "Windows.UI.Xaml.Controls.Grid.ColumnSpan" ||
        property == "Windows.UI.Xaml.Controls.Grid.RowSpan") {
        HRESULT hr = Query(object, ::openxaml::iid::Windows_UI_Xaml_IFrameworkElement,
                           &framework);
        if (FAILED(hr)) return hr;
        const INT32 number = ConstantInteger(value);
        if (EndsWith(property, ".Column")) hr = TheGridFactory().SetColumn(framework, number);
        else if (EndsWith(property, ".Row")) hr = TheGridFactory().SetRow(framework, number);
        else if (EndsWith(property, ".ColumnSpan"))
            hr = TheGridFactory().SetColumnSpan(framework, number);
        else hr = TheGridFactory().SetRowSpan(framework, number);
        framework->Release();
        return hr;
    }

    if (property == "Windows.UI.Xaml.Controls.ColumnDefinition.Width") {
        wuxc::IColumnDefinition* definition = nullptr;
        HRESULT hr = Query(object,
                           ::openxaml::iid::Windows_UI_Xaml_Controls_IColumnDefinition,
                           &definition);
        if (SUCCEEDED(hr)) hr = definition->put_Width(ConstantGridLength(value));
        if (definition) definition->Release();
        return hr;
    }
    if (property == "Windows.UI.Xaml.Controls.RowDefinition.Height") {
        wuxc::IRowDefinition* definition = nullptr;
        HRESULT hr = Query(object, ::openxaml::iid::Windows_UI_Xaml_Controls_IRowDefinition,
                           &definition);
        if (SUCCEEDED(hr)) hr = definition->put_Height(ConstantGridLength(value));
        if (definition) definition->Release();
        return hr;
    }
    if (property == "Windows.UI.Xaml.Controls.StackPanel.Orientation") {
        wuxc::IStackPanel* panel = nullptr;
        HRESULT hr = Query(object, ::openxaml::iid::Windows_UI_Xaml_Controls_IStackPanel,
                           &panel);
        if (SUCCEEDED(hr))
            hr = panel->put_Orientation(static_cast<wuxc::Orientation>(ConstantInteger(value)));
        if (panel) panel->Release();
        return hr;
    }
    if (property == "Windows.UI.Xaml.Controls.TextBlock.Text" ||
        property == "Windows.UI.Xaml.Controls.TextBlock.FontSize") {
        wuxc::ITextBlock* text = nullptr;
        HRESULT hr = Query(object, ::openxaml::iid::Windows_UI_Xaml_Controls_ITextBlock, &text);
        if (SUCCEEDED(hr) && EndsWith(property, ".Text")) {
            HSTRING string = nullptr;
            hr = HStringFromUtf8(value.string_value, &string);
            if (SUCCEEDED(hr)) hr = text->put_Text(string);
            WindowsDeleteString(string);
        } else if (SUCCEEDED(hr)) {
            hr = text->put_FontSize(ConstantNumber(value));
        }
        if (text) text->Release();
        return hr;
    }
    if (property == "Windows.UI.Xaml.Controls.Border.Padding" ||
        property == "Windows.UI.Xaml.Controls.Border.BorderThickness") {
        wuxc::IBorder* border = nullptr;
        HRESULT hr = Query(object, ::openxaml::iid::Windows_UI_Xaml_Controls_IBorder, &border);
        if (SUCCEEDED(hr) && EndsWith(property, ".Padding"))
            hr = border->put_Padding(ConstantThickness(value));
        else if (SUCCEEDED(hr))
            hr = border->put_BorderThickness(ConstantThickness(value));
        if (border) border->Release();
        return hr;
    }
    return S_OK;
}

HRESULT ApplyXbfProperty(const std::string& property, const xbf::Value& value,
                         IInspectable* object, wuxmk::IComponentConnector* connector) {
    if (value.kind == xbf::Value::Kind::Constant)
        return ApplyConstantProperty(property, value.constant, object);
    if (value.kind != xbf::Value::Kind::Object || !value.object) return S_OK;
    if (value.object->type.size() >= 6 &&
        value.object->type.compare(value.object->type.size() - 6, 6, "#value") == 0)
        return ApplyCollection(property, value, object, connector);

    IInspectable* child = nullptr;
    HRESULT hr = BuildXbfObject(value.object, nullptr, connector, &child);
    if (FAILED(hr)) return hr;
    hr = SetSingleChild(object, child, property);
    child->Release();
    return hr;
}

HRESULT BuildXbfObject(const std::shared_ptr<xbf::Object>& graph,
                       IInspectable* existing,
                       wuxmk::IComponentConnector* connector,
                       IInspectable** result) {
    if (!graph || !result) return E_INVALIDARG;
    *result = nullptr;
    IInspectable* object = existing;
    HRESULT hr = S_OK;
    if (object) object->AddRef();
    else hr = ActivateXbfObject(graph->type, &object);
    if (FAILED(hr)) {
        TraceXbfFailure("activation", graph->type, hr);
        return hr;
    }

    for (const auto& [property, value] : graph->properties) {
        hr = ApplyXbfProperty(property, value, object, connector);
        if (FAILED(hr)) {
            TraceXbfFailure("property", property, hr);
            object->Release();
            return hr;
        }
    }

    if (!graph->dictionary.empty()) {
        IOpenXamlResourceDictionary* dictionary = nullptr;
        hr = object->QueryInterface(IID_IOpenXamlResourceDictionary,
                                    reinterpret_cast<void**>(&dictionary));
        if (SUCCEEDED(hr)) {
            for (const auto& [key, value] : graph->dictionary) {
                if (value.kind == xbf::Value::Kind::Object && value.object) {
                    // Templates/styles have context-dependent custom streams;
                    // the controls layer consumes those separately.  The
                    // resource values needed during application construction
                    // are immediately activatable dictionary and brush nodes.
                    if (value.object->type != "Windows.UI.Xaml.ResourceDictionary" &&
                        value.object->type != "Windows.UI.Xaml.Media.SolidColorBrush" &&
                        value.object->type != "Windows.UI.Xaml.Media.ImageBrush" &&
                        value.object->type !=
                            "Microsoft.UI.Xaml.Controls.CommandBarFlyout")
                        continue;
                    IInspectable* child = nullptr;
                    hr = BuildXbfObject(value.object, nullptr, connector, &child);
                    if (FAILED(hr)) break;
                    hr = dictionary->InsertResource(key.c_str(), child);
                    child->Release();
                    if (FAILED(hr)) break;
                } else if (value.kind == xbf::Value::Kind::Constant &&
                           (value.constant.kind == xbf::ConstantKind::SharedString ||
                            value.constant.kind == xbf::ConstantKind::UniqueString ||
                            value.constant.kind == xbf::ConstantKind::NullString)) {
                    auto* boxed = new (std::nothrow)
                        BoxedStringObject(value.constant.string_value);
                    if (!boxed) { hr = E_OUTOFMEMORY; break; }
                    hr = dictionary->InsertResource(
                        key.c_str(), static_cast<wf::IPropertyValue*>(boxed));
                    boxed->Release();
                    if (FAILED(hr)) break;
                }
            }
            if (SUCCEEDED(hr)) {
                for (const auto& [key, value] : graph->dictionary) {
                    if (value.kind != xbf::Value::Kind::Resource) continue;
                    hr = dictionary->AliasResource(key.c_str(), value.text.c_str());
                    if (hr == E_BOUNDS) hr = S_OK;
                    if (FAILED(hr)) break;
                }
            }
            dictionary->Release();
            if (FAILED(hr)) {
                TraceXbfFailure("dictionary", graph->type, hr);
                object->Release();
                return hr;
            }
        } else {
            hr = S_OK;
        }
    }

    const auto connection = graph->properties.find("x:ConnectionId");
    if (connector && connection != graph->properties.end() &&
        connection->second.kind == xbf::Value::Kind::Constant) {
        const INT32 connection_id = ConstantInteger(connection->second.constant);
        if (GetEnvironmentVariableW(L"OPENXAML_TRACE_QI", nullptr, 0)) {
            char message[256]{};
            std::snprintf(message, sizeof(message),
                          "OpenXaml: XBF connect id=%ld type=%s\n",
                          static_cast<long>(connection_id), graph->type.c_str());
            OutputDebugStringA(message);
        }
        hr = connector->Connect(connection_id, object);
        if (FAILED(hr)) {
            TraceXbfFailure("connection", graph->type, hr);
            object->Release();
            return hr;
        }
    }
    *result = object;
    return S_OK;
}

HRESULT MaterializeXbfImpl(const std::shared_ptr<xbf::Object>& graph,
                           IInspectable* component) {
    if (!graph || !component) return E_INVALIDARG;
    wuxmk::IComponentConnector* connector = nullptr;
    (void)component->QueryInterface(kComponentConnectorIid,
                                    reinterpret_cast<void**>(&connector));
    IInspectable* root = nullptr;
    const HRESULT hr = BuildXbfObject(graph, component, connector, &root);
    if (root) root->Release();
    if (connector) connector->Release();
    return hr;
}

}  // namespace

namespace openxaml::winrt {
HRESULT MaterializeXbf(const std::shared_ptr<xbf::Object>& graph,
                       IInspectable* component) {
    return MaterializeXbfImpl(graph, component);
}
}  // namespace openxaml::winrt

extern "C" __declspec(dllexport) HRESULT WINAPI
DllGetActivationFactory(HSTRING classid, IActivationFactory** factory) {
    if (!factory) return E_POINTER;
    *factory = nullptr;
    EnsureFontMetrics();

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
