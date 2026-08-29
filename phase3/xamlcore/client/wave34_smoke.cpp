// Activation and QueryInterface smoke test for the Wave-3/4 DLL surface.

#include "sdk.h"

#include <roapi.h>

#include <cstdio>
#include <cwchar>
#include <memory>

#include "com.h"
#include "openxaml_iids.h"

namespace wuxc = ABI::Windows::UI::Xaml::Controls;
namespace wuxcp = ABI::Windows::UI::Xaml::Controls::Primitives;
namespace wuxs = ABI::Windows::UI::Xaml::Shapes;
namespace wux = ABI::Windows::UI::Xaml;
namespace wuxmk = ABI::Windows::UI::Xaml::Markup;
namespace wuxm = ABI::Windows::UI::Xaml::Media;
namespace wf = ABI::Windows::Foundation;

inline constexpr GUID weak_reference_source_iid = {
    0x00000038, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

inline constexpr GUID muxc_info_bar_iid = {
    0x273ffde8, 0x9324, 0x55b7,
    {0x9f, 0xfe, 0x7d, 0x99, 0x5a, 0x8a, 0xf5, 0x6b}};
inline constexpr GUID muxc_info_bar_factory_iid = {
    0x60618a60, 0x9be7, 0x5df5,
    {0xbe, 0x0d, 0x93, 0x3d, 0x34, 0xdd, 0xb4, 0x4c}};

struct SmokeInfoBar : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_IsOpen(boolean*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsOpen(boolean) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Title(HSTRING*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Title(HSTRING) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Message(HSTRING*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Message(HSTRING) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Severity(INT32*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Severity(INT32) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IconSource(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IconSource(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsIconVisible(boolean*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsIconVisible(boolean) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsClosable(boolean*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsClosable(boolean) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_CloseButtonStyle(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_CloseButtonStyle(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_CloseButtonCommand(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_CloseButtonCommand(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_CloseButtonCommandParameter(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_CloseButtonCommandParameter(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ActionButton(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_ActionButton(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Content(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Content(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ContentTemplate(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_ContentTemplate(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_TemplateSettings(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_CloseButtonClick(void*, EventRegistrationToken*) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_CloseButtonClick(EventRegistrationToken) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_Closing(void*, EventRegistrationToken*) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_Closing(EventRegistrationToken) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_Closed(void*, EventRegistrationToken*) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_Closed(EventRegistrationToken) = 0;
};

struct SmokeInfoBarFactory : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE CreateInstance(void*, void**, void**) = 0;
};

// The factory a class name resolves to, which is where WinRT puts a class's
// static members. DependencyProperty and PropertyMetadata are reached only
// this way: neither has a constructor.
template <class Interface>
Interface* Statics(const wchar_t* name, const GUID& iid) {
    HSTRING class_name = nullptr;
    if (FAILED(WindowsCreateString(name, static_cast<UINT32>(::wcslen(name)), &class_name)))
        return nullptr;
    Interface* factory = nullptr;
    const HRESULT hr = RoGetActivationFactory(class_name, iid,
                                              reinterpret_cast<void**>(&factory));
    WindowsDeleteString(class_name);
    return SUCCEEDED(hr) ? factory : nullptr;
}

// Boxing, from the platform. The DLL uses the same statics, so a value that
// crosses this ABI is boxed by the same code on both sides -- which is the
// point: a box is the platform's, not either party's.
wf::IPropertyValueStatics* Boxes() {
    static wf::IPropertyValueStatics* statics = Statics<wf::IPropertyValueStatics>(
        L"Windows.Foundation.PropertyValue",
        openxaml::iid::Windows_Foundation_IPropertyValueStatics);
    return statics;
}

IInspectable* BoxDouble(double value) {
    IInspectable* boxed = nullptr;
    if (Boxes()) Boxes()->CreateDouble(value, &boxed);
    return boxed;
}

IInspectable* BoxInt32(INT32 value) {
    IInspectable* boxed = nullptr;
    if (Boxes()) Boxes()->CreateInt32(value, &boxed);
    return boxed;
}

double UnboxDouble(IInspectable* value) {
    if (!value) return -1;
    wf::IPropertyValue* boxed = nullptr;
    if (FAILED(value->QueryInterface(openxaml::iid::Windows_Foundation_IPropertyValue,
                                     reinterpret_cast<void**>(&boxed)))) {
        return -2;
    }
    DOUBLE out = -3;
    boxed->GetDouble(&out);
    boxed->Release();
    return out;
}

INT32 UnboxInt32(IInspectable* value) {
    if (!value) return -1;
    wf::IPropertyValue* boxed = nullptr;
    if (FAILED(value->QueryInterface(openxaml::iid::Windows_Foundation_IPropertyValue,
                                     reinterpret_cast<void**>(&boxed)))) {
        return -2;
    }
    INT32 out = -3;
    boxed->GetInt32(&out);
    boxed->Release();
    return out;
}

bool SameIdentity(IInspectable* left, IInspectable* right) {
    if (!left || !right) return left == right;
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

// A caller's delegates. The DLL never asks these for an interface -- a
// delegate arrives typed and is only invoked -- so QueryInterface answers for
// IUnknown and nothing else, which is what a caller-side handler really is.
template <class Delegate>
class Handler : public Delegate {
public:
    int calls = 0;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        if (IsEqualGUID(iid, IID_IUnknown)) {
            ++references_;
            *object = static_cast<Delegate*>(this);
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override { return --references_; }

    ULONG references() const { return references_; }

private:
    ULONG references_ = 1;
};

class PropertyChangedHandler final : public Handler<wux::IDependencyPropertyChangedCallback> {
public:
    wux::IDependencyProperty* last_property = nullptr;

    HRESULT STDMETHODCALLTYPE Invoke(wux::IDependencyObject*,
                                     wux::IDependencyProperty* property) override {
        ++calls;
        last_property = property;
        return S_OK;
    }
};

class SizeChangedHandler final : public Handler<wux::ISizeChangedEventHandler> {
public:
    wf::Size previous{};
    wf::Size current{};

    HRESULT STDMETHODCALLTYPE Invoke(IInspectable*, wux::ISizeChangedEventArgs* args) override {
        ++calls;
        if (args) {
            args->get_PreviousSize(&previous);
            args->get_NewSize(&current);
        }
        return S_OK;
    }
};

class LayoutUpdatedHandler final : public Handler<__FIEventHandler_1_IInspectable> {
public:
    HRESULT STDMETHODCALLTYPE Invoke(IInspectable*, IInspectable*) override {
        ++calls;
        return S_OK;
    }
};

class RoutedHandler final : public Handler<wux::IRoutedEventHandler> {
public:
    HRESULT STDMETHODCALLTYPE Invoke(IInspectable*, wux::IRoutedEventArgs*) override {
        ++calls;
        return S_OK;
    }
};

class FakeImageSource final : public wuxm::IImageSource {
public:
    explicit FakeImageSource(int* destroyed) : destroyed_(destroyed) {}
    ~FakeImageSource() {
        if (destroyed_) ++*destroyed_;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        *object = nullptr;
        static constexpr GUID image_source_iid = {
            0x737ef309, 0xea41, 0x4d96,
            {0xa7, 0x1c, 0x98, 0xe9, 0x8e, 0xfc, 0xab, 0x07}};
        if (IsEqualGUID(iid, IID_IUnknown) ||
            IsEqualGUID(iid, openxaml::iid::IInspectable) ||
            IsEqualGUID(iid, image_source_iid)) {
            *object = static_cast<wuxm::IImageSource*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining = --references_;
        if (!remaining) delete this;
        return remaining;
    }
    HRESULT STDMETHODCALLTYPE GetIids(ULONG*, IID**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING* value) override {
        if (!value) return E_POINTER;
        constexpr wchar_t name[] = L"OpenXaml.Smoke.ImageSource";
        return WindowsCreateString(name, ARRAYSIZE(name) - 1, value);
    }
    HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel* value) override {
        if (!value) return E_POINTER;
        *value = BaseTrust;
        return S_OK;
    }

private:
    ULONG references_ = 1;
    int* destroyed_ = nullptr;
};

class DeferredNameMaterializer final
    : public openxaml::winrt::IOpenXamlDeferredMaterializer {
public:
    DeferredNameMaterializer(wux::IFrameworkElement* element, HSTRING name)
        : element_(element) {
        element_->AddRef();
        WindowsDuplicateString(name, &name_);
    }
    ~DeferredNameMaterializer() {
        WindowsDeleteString(name_);
        element_->Release();
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (IsEqualGUID(iid, IID_IUnknown) ||
            IsEqualGUID(iid,
                        openxaml::winrt::IID_IOpenXamlDeferredMaterializer)) {
            *object = static_cast<
                openxaml::winrt::IOpenXamlDeferredMaterializer*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining = --references_;
        if (!remaining) delete this;
        return remaining;
    }
    HRESULT STDMETHODCALLTYPE Materialize(
        openxaml::winrt::IOpenXamlNameScope* scope,
        IInspectable** value) override {
        if (!scope || !value) return E_INVALIDARG;
        *value = nullptr;
        ++calls;
        openxaml::winrt::IOpenXamlNameScopeOwner* owner = nullptr;
        HRESULT hr = element_->QueryInterface(
            openxaml::winrt::IID_IOpenXamlNameScopeOwner,
            reinterpret_cast<void**>(&owner));
        if (SUCCEEDED(hr)) hr = owner->AttachNameScope(scope);
        if (owner) owner->Release();
        if (SUCCEEDED(hr)) hr = element_->put_Name(name_);
        if (SUCCEEDED(hr))
            hr = element_->QueryInterface(
                openxaml::iid::IInspectable,
                reinterpret_cast<void**>(value));
        return hr;
    }

    int calls = 0;

private:
    ULONG references_ = 1;
    wux::IFrameworkElement* element_ = nullptr;
    HSTRING name_ = nullptr;
};

template <class Interface>
Interface* Activate(const wchar_t* name, const GUID& iid) {
    HSTRING class_name = nullptr;
    if (FAILED(WindowsCreateString(name, static_cast<UINT32>(::wcslen(name)), &class_name)))
        return nullptr;
    IInspectable* instance = nullptr;
    const HRESULT activated = RoActivateInstance(class_name, &instance);
    WindowsDeleteString(class_name);
    if (FAILED(activated)) return nullptr;
    Interface* result = nullptr;
    const HRESULT queried = instance->QueryInterface(iid, reinterpret_cast<void**>(&result));
    instance->Release();
    return SUCCEEDED(queried) ? result : nullptr;
}

// A stand-in for a class that derives from Windows.UI.Xaml.Application, doing
// what C++/WinRT's Application base does: hand itself to the factory as the
// controlling outer, keep the non-delegating inner, and resolve anything it
// does not implement through that inner.
//
// It exists so the composition is exercised the way the real host exercises
// it. Composition cannot be checked by activating something -- there is
// nothing to activate -- and getting it wrong shows up as a refcount that
// never reaches zero or a QueryInterface that recurses, neither of which a
// single activation call would reveal.
class DerivedApp final : public IInspectable {
public:
    IInspectable* inner = nullptr;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        if (IsEqualGUID(iid, IID_IUnknown) || IsEqualGUID(iid, openxaml::iid::IInspectable)) {
            *object = static_cast<IInspectable*>(this);
            AddRef();
            return S_OK;
        }
        // Everything this fake derived class does not implement itself is the
        // base class's, which is exactly what the inner resolves.
        return inner ? inner->QueryInterface(iid, object) : E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining = --references;
        if (remaining == 0 && inner) {
            IInspectable* dying = inner;
            inner = nullptr;
            dying->Release();
        }
        return remaining;
    }
    HRESULT STDMETHODCALLTYPE GetIids(ULONG*, IID**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING* name) override {
        return WindowsCreateString(L"OpenXaml.Smoke.DerivedApp", 25, name);
    }
    HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel* level) override {
        if (!level) return E_POINTER;
        *level = BaseTrust;
        return S_OK;
    }

    ULONG references = 1;
};

int main() {
    if (FAILED(RoInitialize(RO_INIT_SINGLETHREADED))) return 1;
    int failures = 0;
    const auto check = [&](bool condition, const char* what) {
        if (!condition) {
            std::fprintf(stderr, "FAIL %s\n", what);
            ++failures;
        }
    };

    auto* duration_helper = Statics<wux::IDurationHelperStatics>(
        L"Windows.UI.Xaml.DurationHelper",
        openxaml::iid::Windows_UI_Xaml_IDurationHelperStatics);
    check(duration_helper != nullptr, "DurationHelper statics");
    if (duration_helper) {
        ABI::Windows::UI::Xaml::Duration duration{};
        check(SUCCEEDED(duration_helper->FromTimeSpan({2000000}, &duration)) &&
                  duration.Type == wux::DurationType_TimeSpan &&
                  duration.TimeSpan.Duration == 2000000,
              "DurationHelper FromTimeSpan");
        duration_helper->Release();
    }

    auto* grid_length_helper = Statics<wux::IGridLengthHelperStatics>(
        L"Windows.UI.Xaml.GridLengthHelper",
        openxaml::iid::Windows_UI_Xaml_IGridLengthHelperStatics);
    check(grid_length_helper != nullptr, "GridLengthHelper statics");
    if (grid_length_helper) {
        wux::GridLength automatic{};
        check(SUCCEEDED(grid_length_helper->get_Auto(&automatic)) &&
                  automatic.GridUnitType == wux::GridUnitType_Auto,
              "GridLengthHelper Auto");
        grid_length_helper->Release();
    }

    auto* grid_factory = Statics<wuxc::IGridFactory>(
        L"Windows.UI.Xaml.Controls.Grid",
        openxaml::iid::Windows_UI_Xaml_Controls_IGridFactory);
    check(grid_factory != nullptr, "Grid composable factory");
    if (grid_factory) {
        IInspectable* inner = nullptr;
        wuxc::IGrid* grid = nullptr;
        check(SUCCEEDED(grid_factory->CreateInstance(nullptr, &inner, &grid)) &&
                  inner != nullptr && grid != nullptr,
              "Grid factory construction");
        if (grid) grid->Release();
        if (inner) inner->Release();
        grid_factory->Release();
    }

    auto* panel_statics = Statics<wuxc::IPanelStatics>(
        L"Windows.UI.Xaml.Controls.Panel",
        openxaml::iid::Windows_UI_Xaml_Controls_IPanelStatics);
    check(panel_statics != nullptr, "Panel statics");
    if (panel_statics) {
        wux::IDependencyProperty* background = reinterpret_cast<wux::IDependencyProperty*>(1);
        check(SUCCEEDED(panel_statics->get_BackgroundProperty(&background)),
              "Panel BackgroundProperty");
        if (background && background != reinterpret_cast<wux::IDependencyProperty*>(1))
            background->Release();
        panel_statics->Release();
    }

    auto* application_factory = Statics<wux::IApplicationFactory>(
        L"Windows.UI.Xaml.Application",
        openxaml::iid::Windows_UI_Xaml_IApplicationFactory);
    check(application_factory != nullptr, "Application factory");
    if (application_factory) {
        IInspectable* inner = nullptr;
        wux::IApplication* application = nullptr;
        check(SUCCEEDED(application_factory->CreateInstance(nullptr, &inner, &application)) &&
                  inner != nullptr && application != nullptr,
              "Application construction");
        if (application) {
            wux::IApplication3* application3 = nullptr;
            check(SUCCEEDED(application->QueryInterface(
                      openxaml::iid::Windows_UI_Xaml_IApplication3,
                      reinterpret_cast<void**>(&application3))),
                  "Application3 projection");
            if (application3) {
                check(SUCCEEDED(application3->put_HighContrastAdjustment(
                          wux::ApplicationHighContrastAdjustment_None)),
                      "Application high-contrast setter");
                wux::ApplicationHighContrastAdjustment adjustment =
                    wux::ApplicationHighContrastAdjustment_Auto;
                check(SUCCEEDED(application3->get_HighContrastAdjustment(&adjustment)) &&
                          adjustment == wux::ApplicationHighContrastAdjustment_None,
                      "Application high-contrast round-trip");
                application3->Release();
            }
        }

        auto* application_statics = Statics<wux::IApplicationStatics>(
            L"Windows.UI.Xaml.Application",
            openxaml::iid::Windows_UI_Xaml_IApplicationStatics);
        check(application_statics != nullptr, "Application statics");
        if (application_statics) {
            wux::IApplication* current = nullptr;
            check(SUCCEEDED(application_statics->get_Current(&current)) && current != nullptr,
                  "Application.Current");
            if (current) current->Release();
            application_statics->Release();
        }
        if (application) application->Release();
        if (inner) inner->Release();
        application_factory->Release();
    }

    auto* winui_metadata = Activate<wuxmk::IXamlMetadataProvider>(
        L"Microsoft.UI.Xaml.XamlTypeInfo.XamlControlsXamlMetaDataProvider",
        openxaml::iid::Windows_UI_Xaml_Markup_IXamlMetadataProvider);
    check(winui_metadata != nullptr, "WinUI metadata-provider activation");
    if (winui_metadata) {
        UINT32 definitions_length = 1;
        wuxmk::XmlnsDefinition* definitions = reinterpret_cast<wuxmk::XmlnsDefinition*>(1);
        check(SUCCEEDED(winui_metadata->GetXmlnsDefinitions(
                  &definitions_length, &definitions)) &&
                  definitions_length == 0 && definitions == nullptr,
              "WinUI metadata-provider empty namespace catalog");
        winui_metadata->Release();
    }

    auto* content = Activate<wuxc::IContentControl>(
        L"Windows.UI.Xaml.Controls.ContentControl",
        openxaml::iid::Windows_UI_Xaml_Controls_IContentControl);
    check(content != nullptr, "ContentControl activation");
    if (content) {
        auto* border = Activate<ABI::Windows::UI::Xaml::IUIElement>(
            L"Windows.UI.Xaml.Controls.Border", openxaml::iid::Windows_UI_Xaml_IUIElement);
        check(border != nullptr, "Border activation for Content");
        if (border) {
            IInspectable* inspectable = nullptr;
            check(SUCCEEDED(border->QueryInterface(openxaml::iid::IInspectable,
                                                   reinterpret_cast<void**>(&inspectable))),
                  "Border IInspectable");
            if (inspectable) {
                check(SUCCEEDED(content->put_Content(inspectable)), "ContentControl.put_Content");
                inspectable->Release();
            }
            border->Release();
        }
        content->Release();
    }

    auto* info_bar = Activate<SmokeInfoBar>(
        L"Microsoft.UI.Xaml.Controls.InfoBar",
        muxc_info_bar_iid);
    check(info_bar != nullptr, "WinUI InfoBar activation");
    if (info_bar) {
        HSTRING title = nullptr;
        HSTRING message = nullptr;
        WindowsCreateString(L"Warning", 7, &title);
        WindowsCreateString(L"Keyboard service unavailable", 28, &message);
        check(SUCCEEDED(info_bar->put_Title(title)) &&
                  SUCCEEDED(info_bar->put_Message(message)) &&
                  SUCCEEDED(info_bar->put_Severity(2)) &&
                  SUCCEEDED(info_bar->put_IsClosable(1)) &&
                  SUCCEEDED(info_bar->put_IsIconVisible(1)) &&
                  SUCCEEDED(info_bar->put_IsOpen(1)),
              "InfoBar state setters");
        HSTRING read_message = nullptr;
        INT32 severity = -1;
        INT32 comparison = -1;
        boolean is_open = 0;
        check(SUCCEEDED(info_bar->get_Message(&read_message)) &&
                  SUCCEEDED(WindowsCompareStringOrdinal(
                      message, read_message, &comparison)) && comparison == 0 &&
                  SUCCEEDED(info_bar->get_Severity(&severity)) && severity == 2 &&
                  SUCCEEDED(info_bar->get_IsOpen(&is_open)) && is_open,
              "InfoBar state round-trip");
        WindowsDeleteString(read_message);

        openxaml::winrt::IOpenXamlNative* native = nullptr;
        check(SUCCEEDED(info_bar->QueryInterface(
                  openxaml::winrt::IID_IOpenXamlNative,
                  reinterpret_cast<void**>(&native))) && native &&
                  native->LayoutElement()->visibility() ==
                      openxaml::Visibility::Visible,
              "InfoBar IsOpen participates in layout visibility");
        if (native) native->Release();
        WindowsDeleteString(message);
        WindowsDeleteString(title);
        info_bar->Release();
    }

    auto* info_bar_factory =
        Statics<SmokeInfoBarFactory>(
            L"Microsoft.UI.Xaml.Controls.InfoBar",
            muxc_info_bar_factory_iid);
    check(info_bar_factory != nullptr, "WinUI InfoBar composable factory");
    if (info_bar_factory) {
        void* inner = nullptr;
        void* projected = nullptr;
        check(SUCCEEDED(info_bar_factory->CreateInstance(
                  nullptr, &inner, &projected)) && inner && projected,
              "InfoBar factory CreateInstance");
        if (projected)
            static_cast<SmokeInfoBar*>(projected)->Release();
        if (inner)
            static_cast<IInspectable*>(inner)->Release();
        info_bar_factory->Release();
    }

    // One materialized XAML tree owns one namescope. The scope stores weak
    // references: lookup must track rename/duplicates without retaining an
    // element after the visual owner releases it.
    auto* name_root = Activate<wux::IFrameworkElement>(
        L"Windows.UI.Xaml.Controls.Grid",
        openxaml::iid::Windows_UI_Xaml_IFrameworkElement);
    auto* named_child = Activate<wux::IFrameworkElement>(
        L"Windows.UI.Xaml.Controls.Border",
        openxaml::iid::Windows_UI_Xaml_IFrameworkElement);
    auto* transient_child = Activate<wux::IFrameworkElement>(
        L"Windows.UI.Xaml.Controls.Border",
        openxaml::iid::Windows_UI_Xaml_IFrameworkElement);
    auto* deferred_child = Activate<wux::IFrameworkElement>(
        L"Windows.UI.Xaml.Controls.Border",
        openxaml::iid::Windows_UI_Xaml_IFrameworkElement);
    check(name_root && named_child && transient_child && deferred_child,
          "namescope element activation");
    if (name_root && named_child && transient_child && deferred_child) {
        auto* scope = new (std::nothrow) openxaml::winrt::XamlNameScope();
        openxaml::winrt::IOpenXamlNameScopeOwner* root_owner = nullptr;
        openxaml::winrt::IOpenXamlNameScopeOwner* child_owner = nullptr;
        openxaml::winrt::IOpenXamlNameScopeOwner* transient_owner = nullptr;
        check(scope && SUCCEEDED(name_root->QueryInterface(
                  openxaml::winrt::IID_IOpenXamlNameScopeOwner,
                  reinterpret_cast<void**>(&root_owner))) && root_owner &&
                  SUCCEEDED(named_child->QueryInterface(
                  openxaml::winrt::IID_IOpenXamlNameScopeOwner,
                  reinterpret_cast<void**>(&child_owner))) && child_owner &&
                  SUCCEEDED(transient_child->QueryInterface(
                  openxaml::winrt::IID_IOpenXamlNameScopeOwner,
                  reinterpret_cast<void**>(&transient_owner))) && transient_owner,
              "private namescope attachment ABI");
        if (scope && root_owner && child_owner && transient_owner) {
            check(SUCCEEDED(root_owner->AttachNameScope(scope)) &&
                      SUCCEEDED(child_owner->AttachNameScope(scope)) &&
                      SUCCEEDED(transient_owner->AttachNameScope(scope)),
                  "attach one scope to materialized tree");

            HSTRING root_name = nullptr;
            HSTRING child_name = nullptr;
            HSTRING renamed = nullptr;
            HSTRING transient = nullptr;
            HSTRING deferred = nullptr;
            WindowsCreateString(L"Root", 4, &root_name);
            WindowsCreateString(L"Child", 5, &child_name);
            WindowsCreateString(L"RenamedChild", 12, &renamed);
            WindowsCreateString(L"Transient", 9, &transient);
            WindowsCreateString(L"Deferred", 8, &deferred);
            check(SUCCEEDED(name_root->put_Name(root_name)) &&
                      SUCCEEDED(named_child->put_Name(child_name)),
                  "FrameworkElement Name registration");

            IInspectable* expected_child = nullptr;
            IInspectable* found = nullptr;
            named_child->QueryInterface(
                openxaml::iid::IInspectable,
                reinterpret_cast<void**>(&expected_child));
            check(SUCCEEDED(name_root->FindName(child_name, &found)) && found &&
                      SameIdentity(found, expected_child),
                  "FindName resolves a named child by COM identity");
            if (found) found->Release();

            check(transient_child->put_Name(child_name) == E_INVALIDARG,
                  "duplicate live name is rejected");
            check(SUCCEEDED(named_child->put_Name(renamed)),
                  "named element rename");
            found = reinterpret_cast<IInspectable*>(1);
            check(SUCCEEDED(name_root->FindName(child_name, &found)) && !found,
                  "rename removes old name");
            check(SUCCEEDED(name_root->FindName(renamed, &found)) && found &&
                      SameIdentity(found, expected_child),
                  "rename publishes new name");
            if (found) found->Release();

            check(SUCCEEDED(transient_child->put_Name(transient)),
                  "transient name registration");
            transient_owner->Release();
            transient_owner = nullptr;
            transient_child->Release();
            transient_child = nullptr;
            found = reinterpret_cast<IInspectable*>(1);
            check(SUCCEEDED(name_root->FindName(transient, &found)) && !found,
                  "namescope does not retain destroyed element");

            auto* materializer = new (std::nothrow)
                DeferredNameMaterializer(deferred_child, deferred);
            check(materializer && SUCCEEDED(scope->RegisterDeferred(
                      deferred, materializer)),
                  "register deferred namescope entry");
            IInspectable* expected_deferred = nullptr;
            deferred_child->QueryInterface(
                openxaml::iid::IInspectable,
                reinterpret_cast<void**>(&expected_deferred));
            found = nullptr;
            check(materializer &&
                      SUCCEEDED(name_root->FindName(deferred, &found)) && found &&
                      SameIdentity(found, expected_deferred) &&
                      materializer->calls == 1,
                  "FindName materializes a deferred entry once");
            if (found) found->Release();
            found = nullptr;
            check(materializer &&
                      SUCCEEDED(name_root->FindName(deferred, &found)) && found &&
                      SameIdentity(found, expected_deferred) &&
                      materializer->calls == 1,
                  "repeated FindName reuses the materialized identity");
            if (found) found->Release();
            if (expected_deferred) expected_deferred->Release();
            if (materializer) materializer->Release();

            if (expected_child) expected_child->Release();
            WindowsDeleteString(deferred);
            WindowsDeleteString(transient);
            WindowsDeleteString(renamed);
            WindowsDeleteString(child_name);
            WindowsDeleteString(root_name);
        }
        if (transient_owner) transient_owner->Release();
        if (child_owner) child_owner->Release();
        if (root_owner) root_owner->Release();
        if (scope) scope->Release();
    }
    if (transient_child) transient_child->Release();
    if (deferred_child) deferred_child->Release();
    if (named_child) named_child->Release();
    if (name_root) name_root->Release();

    auto* source_image = Activate<wuxc::IImage>(
        L"Windows.UI.Xaml.Controls.Image",
        openxaml::iid::Windows_UI_Xaml_Controls_IImage);
    check(source_image != nullptr, "Image activation");
    if (source_image) {
        openxaml::winrt::IOpenXamlNative* native = nullptr;
        check(SUCCEEDED(source_image->QueryInterface(
                  openxaml::winrt::IID_IOpenXamlNative,
                  reinterpret_cast<void**>(&native))) && native,
              "Image private layout projection");
        int invalidations = 0;
        bool last_layout = false;
        std::shared_ptr<openxaml::RenderInvalidationSink> sink;
        if (native) {
            sink = std::make_shared<openxaml::RenderInvalidationSink>(
                [&](bool layout) {
                    ++invalidations;
                    last_layout = layout;
                });
            check(native->LayoutElement()->AttachRenderInvalidationSink(sink),
                  "attach Image invalidation sink");
        }

        wuxm::Stretch stretch = wuxm::Stretch_None;
        check(SUCCEEDED(source_image->get_Stretch(&stretch)) &&
                  stretch == wuxm::Stretch_Uniform,
              "Image default Stretch is Uniform");
        int destroyed = 0;
        auto* source = new FakeImageSource(&destroyed);
        check(SUCCEEDED(source_image->put_Source(source)) && invalidations == 1 &&
                  last_layout,
              "Image source retained with layout invalidation");
        wuxm::IImageSource* retained = nullptr;
        check(SUCCEEDED(source_image->get_Source(&retained)) && retained &&
                  SameIdentity(static_cast<IInspectable*>(retained),
                               static_cast<IInspectable*>(source)),
              "Image source round-trip identity");
        source->Release();
        source = nullptr;
        check(destroyed == 0, "Image owns assigned source");
        if (retained) retained->Release();

        check(SUCCEEDED(source_image->put_Stretch(wuxm::Stretch_Fill)) &&
                  invalidations == 2 && last_layout,
              "Image Stretch mutation invalidates layout");
        check(source_image->put_Stretch(static_cast<wuxm::Stretch>(99)) == E_INVALIDARG &&
                  invalidations == 2,
              "Image rejects invalid Stretch");
        const wux::Thickness nine_grid{1.0, 2.0, 3.0, 4.0};
        check(SUCCEEDED(source_image->put_NineGrid(nine_grid)) &&
                  invalidations == 3 && !last_layout,
              "Image NineGrid is render-only invalidation");
        wux::Thickness retained_grid{};
        check(SUCCEEDED(source_image->get_NineGrid(&retained_grid)) &&
                  retained_grid.Left == 1.0 && retained_grid.Top == 2.0 &&
                  retained_grid.Right == 3.0 && retained_grid.Bottom == 4.0,
              "Image NineGrid round-trip");
        check(source_image->put_NineGrid({-1.0, 0.0, 0.0, 0.0}) == E_INVALIDARG,
              "Image rejects negative NineGrid");

        check(SUCCEEDED(source_image->put_Source(nullptr)) && invalidations == 4 &&
                  last_layout && destroyed == 1,
              "clearing Image source releases it and invalidates layout");
        retained = reinterpret_cast<wuxm::IImageSource*>(1);
        check(SUCCEEDED(source_image->get_Source(&retained)) && !retained,
              "cleared Image source reads null");

        if (native) {
            native->LayoutElement()->DetachRenderInvalidationSink(sink);
            native->Release();
        }
        source_image->Release();
    }

    // A brush is a live DependencyObject, not a colour copied at assignment.
    // Exercise the private layout projection here because the public ABI can
    // only return the same IBrush; it cannot prove what the renderer sees.
    auto* painted_border = Activate<wuxc::IBorder>(
        L"Windows.UI.Xaml.Controls.Border",
        openxaml::iid::Windows_UI_Xaml_Controls_IBorder);
    auto* shared_brush = Activate<wuxm::ISolidColorBrush>(
        L"Windows.UI.Xaml.Media.SolidColorBrush",
        openxaml::iid::Windows_UI_Xaml_Media_ISolidColorBrush);
    check(painted_border != nullptr && shared_brush != nullptr,
          "live brush projection activation");
    if (painted_border && shared_brush) {
        openxaml::winrt::IOpenXamlNative* native = nullptr;
        check(SUCCEEDED(painted_border->QueryInterface(
                  openxaml::winrt::IID_IOpenXamlNative,
                  reinterpret_cast<void**>(&native))) && native != nullptr,
              "Border private layout projection");
        wuxm::IBrush* brush = nullptr;
        check(SUCCEEDED(shared_brush->QueryInterface(
                  openxaml::iid::Windows_UI_Xaml_Media_IBrush,
                  reinterpret_cast<void**>(&brush))) && brush != nullptr,
              "SolidColorBrush IBrush projection");
        if (native && brush) {
            int invalidations = 0;
            auto sink = std::make_shared<openxaml::RenderInvalidationSink>(
                [&](bool layout) {
                    check(!layout, "brush mutation is render-only invalidation");
                    ++invalidations;
                });
            openxaml::Element* layout = native->LayoutElement();
            check(layout->AttachRenderInvalidationSink(sink),
                  "attach brush invalidation sink");
            const auto brush_equals = [](const openxaml::BrushValue& value,
                                         openxaml::Color color) {
                return value.declared && value.has_color && value.color == color;
            };

            const ABI::Windows::UI::Color red{255, 255, 0, 0};
            const ABI::Windows::UI::Color blue{255, 0, 0, 255};
            check(SUCCEEDED(shared_brush->put_Color(red)) &&
                      SUCCEEDED(painted_border->put_Background(brush)),
                  "assign live red background");
            check(brush_equals(layout->background_brush(), {255, 255, 0, 0}) &&
                      invalidations == 1,
                  "red projected into renderer brush");

            check(SUCCEEDED(shared_brush->put_Color(blue)) &&
                      brush_equals(layout->background_brush(), {255, 0, 0, 255}) &&
                      invalidations == 2,
                  "live red-to-blue brush mutation");
            check(SUCCEEDED(brush->put_Opacity(0.5)) &&
                      brush_equals(layout->background_brush(), {128, 0, 0, 255}) &&
                      invalidations == 3,
                  "brush opacity projected into alpha");

            auto* second_border = Activate<wuxc::IBorder>(
                L"Windows.UI.Xaml.Controls.Border",
                openxaml::iid::Windows_UI_Xaml_Controls_IBorder);
            openxaml::winrt::IOpenXamlNative* second_native = nullptr;
            if (second_border) {
                second_border->QueryInterface(
                    openxaml::winrt::IID_IOpenXamlNative,
                    reinterpret_cast<void**>(&second_native));
            }
            check(second_border != nullptr && second_native != nullptr &&
                      SUCCEEDED(second_border->put_Background(brush)),
                  "shared brush assigned to second element");

            // Drop both caller references. The two projections retain the
            // brush, and a brush obtained back through either property must
            // still update both renderer values.
            brush->Release();
            brush = nullptr;
            shared_brush->Release();
            shared_brush = nullptr;
            wuxm::IBrush* retained = nullptr;
            wuxm::ISolidColorBrush* retained_solid = nullptr;
            check(SUCCEEDED(painted_border->get_Background(&retained)) && retained &&
                      SUCCEEDED(retained->QueryInterface(
                          openxaml::iid::Windows_UI_Xaml_Media_ISolidColorBrush,
                          reinterpret_cast<void**>(&retained_solid))) && retained_solid,
                  "element retains assigned shared brush");
            if (retained_solid && second_native) {
                const ABI::Windows::UI::Color green{255, 0, 255, 0};
                check(SUCCEEDED(retained_solid->put_Color(green)) &&
                          layout->background_brush().color.g == 255 &&
                          second_native->LayoutElement()->background_brush().color.g == 255,
                      "retained shared brush updates every subscriber");
            }

            auto* replacement = Activate<wuxm::ISolidColorBrush>(
                L"Windows.UI.Xaml.Media.SolidColorBrush",
                openxaml::iid::Windows_UI_Xaml_Media_ISolidColorBrush);
            wuxm::IBrush* replacement_brush = nullptr;
            if (replacement) {
                replacement->put_Color({255, 255, 255, 0});
                replacement->QueryInterface(
                    openxaml::iid::Windows_UI_Xaml_Media_IBrush,
                    reinterpret_cast<void**>(&replacement_brush));
            }
            check(replacement_brush &&
                      SUCCEEDED(painted_border->put_Background(replacement_brush)) &&
                      layout->background_brush().color.r == 255,
                  "brush replacement projects new value");
            const int before_old_mutation = invalidations;
            if (retained_solid) retained_solid->put_Color(red);
            check(layout->background_brush().color.r == 255 &&
                      invalidations == before_old_mutation,
                  "replacement detaches old brush observer");

            check(SUCCEEDED(painted_border->put_BorderBrush(replacement_brush)) &&
                      layout->border_brush().has_color &&
                      layout->border_brush().color.r == 255,
                  "BorderBrush projects into renderer border slot");

            auto verify_background = [&](auto* object, const char* what) {
                openxaml::winrt::IOpenXamlNative* projected = nullptr;
                const bool queried = object && SUCCEEDED(object->QueryInterface(
                    openxaml::winrt::IID_IOpenXamlNative,
                    reinterpret_cast<void**>(&projected))) && projected;
                check(queried && brush_equals(
                          projected->LayoutElement()->background_brush(),
                          {255, 255, 255, 0}),
                      what);
                if (projected) projected->Release();
            };
            auto* panel = Activate<wuxc::IPanel>(
                L"Windows.UI.Xaml.Controls.Grid",
                openxaml::iid::Windows_UI_Xaml_Controls_IPanel);
            check(panel && SUCCEEDED(panel->put_Background(replacement_brush)),
                  "Panel background assignment");
            verify_background(panel, "Panel background renderer projection");
            if (panel) panel->Release();

            auto* presenter = Activate<wuxc::IContentPresenter4>(
                L"Windows.UI.Xaml.Controls.ContentPresenter",
                openxaml::iid::Windows_UI_Xaml_Controls_IContentPresenter4);
            check(presenter && SUCCEEDED(presenter->put_Background(replacement_brush)),
                  "ContentPresenter background assignment");
            verify_background(presenter, "ContentPresenter background renderer projection");
            if (presenter) presenter->Release();

            auto* projected_control = Activate<wuxc::IControl>(
                L"Windows.UI.Xaml.Controls.ContentControl",
                openxaml::iid::Windows_UI_Xaml_Controls_IControl);
            check(projected_control &&
                      SUCCEEDED(projected_control->put_Background(replacement_brush)),
                  "ContentControl background assignment");
            verify_background(projected_control,
                              "ContentControl background renderer projection");
            if (projected_control) projected_control->Release();

            check(SUCCEEDED(painted_border->put_Background(nullptr)) &&
                      !layout->background_brush().declared,
                  "null brush becomes undeclared");

            auto* image = Activate<wuxm::IImageBrush>(
                L"Windows.UI.Xaml.Media.ImageBrush",
                openxaml::iid::Windows_UI_Xaml_Media_IImageBrush);
            wuxm::IBrush* image_brush = nullptr;
            if (image) image->QueryInterface(
                openxaml::iid::Windows_UI_Xaml_Media_IBrush,
                reinterpret_cast<void**>(&image_brush));
            check(image_brush && SUCCEEDED(painted_border->put_Background(image_brush)) &&
                      layout->background_brush().declared &&
                      layout->background_brush().kind == openxaml::BrushKind::Image &&
                      !layout->background_brush().has_image_source,
                  "empty ImageBrush projects as a typed transparent no-op");
            const int before_image_mutation = invalidations;
            check(image_brush && SUCCEEDED(image_brush->put_Opacity(0.5)) &&
                      layout->background_brush().kind == openxaml::BrushKind::Image &&
                      !layout->background_brush().has_image_source &&
                      invalidations == before_image_mutation + 1,
                  "ImageBrush mutations retain typed state and invalidate rendering");

            sink->Close();
            layout->DetachRenderInvalidationSink(sink);
            if (image_brush) image_brush->Release();
            if (image) image->Release();
            if (replacement_brush) replacement_brush->Release();
            if (replacement) replacement->Release();
            if (retained_solid) retained_solid->Release();
            if (retained) retained->Release();
            if (second_native) second_native->Release();
            if (second_border) second_border->Release();
        }
        if (brush) brush->Release();
        if (native) native->Release();
    }
    if (shared_brush) shared_brush->Release();
    if (painted_border) painted_border->Release();

    auto* weak_border = Activate<wux::IUIElement>(
        L"Windows.UI.Xaml.Controls.Border", openxaml::iid::Windows_UI_Xaml_IUIElement);
    check(weak_border != nullptr, "Border activation for weak reference");
    if (weak_border) {
        IWeakReferenceSource* source = nullptr;
        check(SUCCEEDED(weak_border->QueryInterface(
                  weak_reference_source_iid, reinterpret_cast<void**>(&source))) &&
                  source != nullptr,
              "UIElement weak-reference source");
        IWeakReference* weak = nullptr;
        if (source) {
            check(SUCCEEDED(source->GetWeakReference(&weak)) && weak != nullptr,
                  "UIElement GetWeakReference");
            source->Release();
        }
        if (weak) {
            IInspectable* resolved = nullptr;
            check(SUCCEEDED(weak->Resolve(
                      openxaml::iid::Windows_UI_Xaml_IFrameworkElement, &resolved)) &&
                      resolved != nullptr,
                  "live weak-reference resolution");
            if (resolved) resolved->Release();
            weak_border->Release();
            weak_border = nullptr;
            resolved = reinterpret_cast<IInspectable*>(1);
            check(SUCCEEDED(weak->Resolve(
                      openxaml::iid::Windows_UI_Xaml_IFrameworkElement, &resolved)) &&
                      resolved == nullptr,
                  "expired weak-reference resolution");
            weak->Release();
        }
        if (weak_border) weak_border->Release();
    }

    auto* scale = Activate<wuxm::IScaleTransform>(
        L"Windows.UI.Xaml.Media.ScaleTransform",
        openxaml::iid::Windows_UI_Xaml_Media_IScaleTransform);
    check(scale != nullptr, "ScaleTransform activation");
    if (scale) {
        wuxm::ITransform* transform = nullptr;
        wuxm::IGeneralTransform* general = nullptr;
        auto* target = Activate<wux::IUIElement>(
            L"Windows.UI.Xaml.Controls.Grid",
            openxaml::iid::Windows_UI_Xaml_IUIElement);
        openxaml::winrt::IOpenXamlNative* native = nullptr;
        if (target) {
            target->QueryInterface(openxaml::winrt::IID_IOpenXamlNative,
                                   reinterpret_cast<void**>(&native));
        }
        check(SUCCEEDED(scale->put_ScaleX(2.0)) &&
                  SUCCEEDED(scale->put_ScaleY(3.0)) &&
                  SUCCEEDED(scale->put_CenterX(4.0)) &&
                  SUCCEEDED(scale->put_CenterY(5.0)) &&
                  SUCCEEDED(scale->QueryInterface(
                      openxaml::iid::Windows_UI_Xaml_Media_ITransform,
                      reinterpret_cast<void**>(&transform))) && transform &&
                  SUCCEEDED(scale->QueryInterface(
                      openxaml::iid::Windows_UI_Xaml_Media_IGeneralTransform,
                      reinterpret_cast<void**>(&general))) && general,
              "ScaleTransform base projections and values");
        check(target && native && transform &&
                  SUCCEEDED(target->put_RenderTransform(transform)) &&
                  SUCCEEDED(target->put_RenderTransformOrigin({0.5f, 0.25f})),
              "UIElement live ScaleTransform assignment");
        if (native) {
            const openxaml::VisualTransform current =
                native->LayoutElement()->visual_transform();
            check(current.kind == openxaml::VisualTransformKind::Scale &&
                      current.scale_x == 2.0 && current.scale_y == 3.0 &&
                      current.center_x == 4.0 && current.center_y == 5.0,
                  "ScaleTransform projects into retained visual state");
            check(SUCCEEDED(scale->put_ScaleX(4.0)) &&
                      native->LayoutElement()->visual_transform().scale_x == 4.0,
                  "assigned ScaleTransform mutation remains live");
        }
        if (general) {
            wf::Point transformed{};
            check(SUCCEEDED(general->TransformPoint({5.0f, 6.0f}, &transformed)) &&
                      transformed.X == 8.0f && transformed.Y == 8.0f,
                  "ScaleTransform point mapping");
            general->Release();
        }
        if (transform) transform->Release();
        if (native) native->Release();
        if (target) target->Release();
        scale->Release();
    }

    // Terminal positions its native title-bar input sink with
    // dragBar.TransformToVisual(rootGrid). An identity result moves the sink to
    // x=0, covering the new-tab SplitButton and stealing its clicks.
    auto* transform_root = Activate<wux::IUIElement>(
        L"Windows.UI.Xaml.Controls.Grid",
        openxaml::iid::Windows_UI_Xaml_IUIElement);
    auto* transform_child = Activate<wux::IUIElement>(
        L"Windows.UI.Xaml.Controls.Grid",
        openxaml::iid::Windows_UI_Xaml_IUIElement);
    openxaml::winrt::IOpenXamlNative* transform_root_native = nullptr;
    openxaml::winrt::IOpenXamlNative* transform_child_native = nullptr;
    if (transform_root) {
        transform_root->QueryInterface(
            openxaml::winrt::IID_IOpenXamlNative,
            reinterpret_cast<void**>(&transform_root_native));
    }
    if (transform_child) {
        transform_child->QueryInterface(
            openxaml::winrt::IID_IOpenXamlNative,
            reinterpret_cast<void**>(&transform_child_native));
    }
    bool transform_attached = false;
    if (transform_root_native && transform_child_native) {
        auto* root_layout = transform_root_native->LayoutElement();
        auto* child_layout = transform_child_native->LayoutElement();
        transform_attached = root_layout->AttachVisualChild(*child_layout);
        root_layout->Arrange({0.0, 0.0, 320.0, 100.0});
        child_layout->Arrange({73.0, 11.0, 80.0, 24.0});
    }
    wuxm::IGeneralTransform* child_to_root = nullptr;
    wf::Rect translated{};
    check(transform_attached &&
              SUCCEEDED(transform_child->TransformToVisual(
                  transform_root, &child_to_root)) && child_to_root &&
              SUCCEEDED(child_to_root->TransformBounds(
                  {0.0f, 0.0f, 80.0f, 24.0f}, &translated)) &&
              translated.X == 73.0f && translated.Y == 11.0f &&
              translated.Width == 80.0f && translated.Height == 24.0f,
          "TransformToVisual preserves nested retained offset");
    wuxm::IGeneralTransform* root_to_child = nullptr;
    wf::Point inverse_point{};
    check(child_to_root &&
              SUCCEEDED(child_to_root->get_Inverse(&root_to_child)) &&
              root_to_child &&
              SUCCEEDED(root_to_child->TransformPoint(
                  {73.0f, 11.0f}, &inverse_point)) &&
              inverse_point.X == 0.0f && inverse_point.Y == 0.0f,
          "TransformToVisual inverse restores local coordinates");
    if (transform_root_native && transform_child_native && transform_attached) {
        transform_root_native->LayoutElement()->DetachVisualChild(
            *transform_child_native->LayoutElement());
    }
    if (root_to_child) root_to_child->Release();
    if (child_to_root) child_to_root->Release();
    if (transform_child_native) transform_child_native->Release();
    if (transform_root_native) transform_root_native->Release();
    if (transform_child) transform_child->Release();
    if (transform_root) transform_root->Release();

    auto* user_control = Activate<wuxc::IUserControl>(
        L"Windows.UI.Xaml.Controls.UserControl",
        openxaml::iid::Windows_UI_Xaml_Controls_IUserControl);
    check(user_control != nullptr, "UserControl activation");
    if (user_control) {
        auto* user_content = static_cast<wuxc::IContentControl*>(nullptr);
        auto* user_native = static_cast<openxaml::winrt::IOpenXamlNative*>(nullptr);
        auto* user_child = Activate<wux::IUIElement>(
            L"Windows.UI.Xaml.Controls.Grid",
            openxaml::iid::Windows_UI_Xaml_IUIElement);
        auto* child_native = static_cast<openxaml::winrt::IOpenXamlNative*>(nullptr);
        check(SUCCEEDED(user_control->QueryInterface(
                  openxaml::iid::Windows_UI_Xaml_Controls_IContentControl,
                  reinterpret_cast<void**>(&user_content))) && user_content,
              "UserControl IContentControl projection");
        check(SUCCEEDED(user_control->QueryInterface(
                  openxaml::winrt::IID_IOpenXamlNative,
                  reinterpret_cast<void**>(&user_native))) && user_native,
              "UserControl private layout projection");
        if (user_child) {
            user_child->QueryInterface(
                openxaml::winrt::IID_IOpenXamlNative,
                reinterpret_cast<void**>(&child_native));
        }
        IInspectable* child_inspectable = nullptr;
        if (user_child) {
            user_child->QueryInterface(openxaml::iid::IInspectable,
                                       reinterpret_cast<void**>(&child_inspectable));
        }
        check(user_content && user_native && child_native && child_inspectable &&
                  SUCCEEDED(user_content->put_Content(child_inspectable)) &&
                  SUCCEEDED(user_native->PerformLayout(320.0, 200.0)),
              "UserControl full-bounds content layout");
        if (child_native) {
            const openxaml::Rect slot = child_native->LayoutElement()->layout_slot();
            check(slot.x == 0.0 && slot.y == 0.0 && slot.width == 320.0 &&
                      slot.height == 200.0,
                  "UserControl arranges zero-desired content to its complete bounds");
        }
        if (child_inspectable) child_inspectable->Release();
        if (child_native) child_native->Release();
        if (user_child) user_child->Release();
        if (user_native) user_native->Release();
        if (user_content) user_content->Release();
        user_control->Release();
    }

    auto* page = Activate<wuxc::IPage>(L"Windows.UI.Xaml.Controls.Page",
                                       openxaml::iid::Windows_UI_Xaml_Controls_IPage);
    check(page != nullptr, "Page activation");
    if (page) {
        auto* page_content = static_cast<wuxc::IContentControl*>(nullptr);
        auto* page_native = static_cast<openxaml::winrt::IOpenXamlNative*>(nullptr);
        auto* page_child = Activate<wux::IUIElement>(
            L"Windows.UI.Xaml.Controls.Grid",
            openxaml::iid::Windows_UI_Xaml_IUIElement);
        auto* child_native = static_cast<openxaml::winrt::IOpenXamlNative*>(nullptr);
        check(SUCCEEDED(page->QueryInterface(
                  openxaml::iid::Windows_UI_Xaml_Controls_IContentControl,
                  reinterpret_cast<void**>(&page_content))) && page_content,
              "Page IContentControl projection");
        check(SUCCEEDED(page->QueryInterface(
                  openxaml::winrt::IID_IOpenXamlNative,
                  reinterpret_cast<void**>(&page_native))) && page_native,
              "Page private layout projection");
        if (page_child) {
            page_child->QueryInterface(
                openxaml::winrt::IID_IOpenXamlNative,
                reinterpret_cast<void**>(&child_native));
        }
        IInspectable* child_inspectable = nullptr;
        if (page_child) {
            page_child->QueryInterface(openxaml::iid::IInspectable,
                                       reinterpret_cast<void**>(&child_inspectable));
        }
        check(page_content && page_native && child_native && child_inspectable &&
                  SUCCEEDED(page_content->put_Content(child_inspectable)) &&
                  SUCCEEDED(page_native->PerformLayout(320.0, 200.0)),
              "Page full-bounds content layout");
        if (child_native) {
            const openxaml::Rect slot = child_native->LayoutElement()->layout_slot();
            check(slot.x == 0.0 && slot.y == 0.0 && slot.width == 320.0 &&
                      slot.height == 200.0,
                  "Page arranges zero-desired content to its complete bounds");
        }
        if (child_inspectable) child_inspectable->Release();
        if (child_native) child_native->Release();
        if (page_child) page_child->Release();
        if (page_native) page_native->Release();
        if (page_content) page_content->Release();

        wux::IUIElement10* element10 = nullptr;
        check(SUCCEEDED(page->QueryInterface(
                  openxaml::iid::Windows_UI_Xaml_IUIElement10,
                  reinterpret_cast<void**>(&element10))),
              "Page IUIElement10 projection");
        if (element10) {
            wux::IXamlRoot* root = reinterpret_cast<wux::IXamlRoot*>(1);
            check(SUCCEEDED(element10->get_XamlRoot(&root)) && root == nullptr,
                  "detached Page XamlRoot");
            element10->Release();
        }
        page->Release();
    }

    auto* frame = Activate<wuxc::IFrame>(L"Windows.UI.Xaml.Controls.Frame",
                                         openxaml::iid::Windows_UI_Xaml_Controls_IFrame);
    check(frame != nullptr, "Frame activation");
    if (frame) {
        boolean can_go_back = 1;
        check(SUCCEEDED(frame->get_CanGoBack(&can_go_back)) && !can_go_back,
              "Frame initial journal");
        frame->Release();
    }

    auto* items = Activate<wuxc::IItemsControl>(
        L"Windows.UI.Xaml.Controls.ItemsControl",
        openxaml::iid::Windows_UI_Xaml_Controls_IItemsControl);
    check(items != nullptr, "ItemsControl activation");
    if (items) items->Release();

    auto* list = Activate<wuxc::IListViewBase>(
        L"Windows.UI.Xaml.Controls.ListView",
        openxaml::iid::Windows_UI_Xaml_Controls_IListViewBase);
    check(list != nullptr, "ListViewBase projection");
    if (list) {
        check(SUCCEEDED(list->put_SelectionMode(wuxc::ListViewSelectionMode_Multiple)),
              "ListView selection setter");
        wuxc::ListViewSelectionMode mode = wuxc::ListViewSelectionMode_None;
        check(SUCCEEDED(list->get_SelectionMode(&mode)) &&
                  mode == wuxc::ListViewSelectionMode_Multiple,
              "ListView selection round-trip");
        list->Release();
    }

    auto* popup = Activate<wuxcp::IPopup>(
        L"Windows.UI.Xaml.Controls.Primitives.Popup",
        openxaml::iid::Windows_UI_Xaml_Controls_Primitives_IPopup);
    check(popup != nullptr, "Popup activation");
    if (popup) {
        check(SUCCEEDED(popup->put_IsOpen(1)), "Popup open");
        boolean open = 0;
        check(SUCCEEDED(popup->get_IsOpen(&open)) && open, "Popup open round-trip");
        popup->Release();
    }

    auto* sub_item = Activate<wuxc::IMenuFlyoutSubItem>(
        L"Windows.UI.Xaml.Controls.MenuFlyoutSubItem",
        openxaml::iid::Windows_UI_Xaml_Controls_IMenuFlyoutSubItem);
    check(sub_item != nullptr, "MenuFlyoutSubItem activation");
    if (sub_item) {
        HSTRING text = nullptr;
        WindowsCreateString(L"Profiles", 8, &text);
        check(SUCCEEDED(sub_item->put_Text(text)),
              "MenuFlyoutSubItem text setter");
        WindowsDeleteString(text);
        text = nullptr;
        check(SUCCEEDED(sub_item->get_Text(&text)) &&
                  WindowsGetStringLen(text) == 8,
              "MenuFlyoutSubItem text round-trip");
        WindowsDeleteString(text);
        __FIVector_1_Windows__CUI__CXaml__CControls__CMenuFlyoutItemBase* children =
            nullptr;
        check(SUCCEEDED(sub_item->get_Items(&children)) && children != nullptr,
              "MenuFlyoutSubItem Items");
        if (children) {
            UINT32 size = 1;
            check(SUCCEEDED(children->get_Size(&size)) && size == 0,
                  "MenuFlyoutSubItem initial Items size");
            children->Release();
        }
        sub_item->Release();
    }

    auto* dialog = Activate<wuxc::IContentDialog>(
        L"Windows.UI.Xaml.Controls.ContentDialog",
        openxaml::iid::Windows_UI_Xaml_Controls_IContentDialog);
    check(dialog != nullptr, "ContentDialog activation");
    if (dialog) {
        HSTRING primary = nullptr;
        WindowsCreateString(L"OK", 2, &primary);
        check(SUCCEEDED(dialog->put_PrimaryButtonText(primary)),
              "ContentDialog primary text setter");
        WindowsDeleteString(primary);
        primary = nullptr;
        check(SUCCEEDED(dialog->get_PrimaryButtonText(&primary)) &&
                  WindowsGetStringLen(primary) == 2,
              "ContentDialog primary text round-trip");
        WindowsDeleteString(primary);
        __FIAsyncOperation_1_Windows__CUI__CXaml__CControls__CContentDialogResult*
            operation = nullptr;
        check(SUCCEEDED(dialog->ShowAsync(&operation)) && operation != nullptr,
              "ContentDialog completed ShowAsync");
        if (operation) {
            wuxc::ContentDialogResult result = wuxc::ContentDialogResult_Primary;
            check(SUCCEEDED(operation->GetResults(&result)) &&
                      result == wuxc::ContentDialogResult_None,
                  "ContentDialog completed result");
            operation->Release();
        }
        dialog->Release();
    }

    auto* viewer = Activate<wuxc::IScrollViewer>(
        L"Windows.UI.Xaml.Controls.ScrollViewer",
        openxaml::iid::Windows_UI_Xaml_Controls_IScrollViewer);
    check(viewer != nullptr, "ScrollViewer activation");
    if (viewer) {
        check(SUCCEEDED(viewer->put_HorizontalScrollBarVisibility(
                  wuxc::ScrollBarVisibility_Visible)), "ScrollViewer visibility setter");
        check(SUCCEEDED(viewer->put_VerticalScrollBarVisibility(
                  wuxc::ScrollBarVisibility_Visible)), "ScrollViewer vertical visibility setter");
        wuxc::ScrollBarVisibility visibility = wuxc::ScrollBarVisibility_Disabled;
        check(SUCCEEDED(viewer->get_HorizontalScrollBarVisibility(&visibility)) &&
                  visibility == wuxc::ScrollBarVisibility_Visible,
              "ScrollViewer visibility round-trip");
        auto* child = Activate<wux::IUIElement>(L"Windows.UI.Xaml.Controls.Border",
                                                 openxaml::iid::Windows_UI_Xaml_IUIElement);
        check(child != nullptr, "ScrollViewer content activation");
        if (child) {
            wux::IFrameworkElement* framework = nullptr;
            check(SUCCEEDED(child->QueryInterface(openxaml::iid::Windows_UI_Xaml_IFrameworkElement,
                                                   reinterpret_cast<void**>(&framework))),
                  "ScrollViewer content framework projection");
            if (framework) {
                framework->put_Width(300);
                framework->put_Height(260);
                framework->Release();
            }
            wuxc::IContentControl* content_control = nullptr;
            check(SUCCEEDED(viewer->QueryInterface(
                      openxaml::iid::Windows_UI_Xaml_Controls_IContentControl,
                      reinterpret_cast<void**>(&content_control))),
                  "ScrollViewer content-control projection");
            IInspectable* inspectable = nullptr;
            child->QueryInterface(openxaml::iid::IInspectable,
                                  reinterpret_cast<void**>(&inspectable));
            if (content_control && inspectable)
                check(SUCCEEDED(content_control->put_Content(inspectable)),
                      "ScrollViewer put_Content");
            if (inspectable) inspectable->Release();
            if (content_control) content_control->Release();
            child->Release();
        }
        wux::IUIElement* viewer_element = nullptr;
        check(SUCCEEDED(viewer->QueryInterface(openxaml::iid::Windows_UI_Xaml_IUIElement,
                                               reinterpret_cast<void**>(&viewer_element))),
              "ScrollViewer UIElement projection");
        if (viewer_element) {
            viewer_element->Measure({200, 150});
            viewer_element->Arrange({0, 0, 200, 150});
            DOUBLE viewport_width = 0;
            DOUBLE extent_width = 0;
            // The padded client, not the client minus a scrollbar. A visible
            // bar is overlaid on the content rather than subtracted from it --
            // see ScrollViewer::ArrangeOverride and the L3-scroll cases that
            // pin it -- so with no padding the viewport is the whole arranged
            // size. This check read 184 while the core still subtracted the
            // bar, and was left behind when the corpus settled the rule.
            check(SUCCEEDED(viewer->get_ViewportWidth(&viewport_width)) && viewport_width == 200,
                  "ScrollViewer ABI viewport");
            check(SUCCEEDED(viewer->get_ExtentWidth(&extent_width)) && extent_width == 300,
                  "ScrollViewer ABI extent");
            viewer_element->Release();
        }
        viewer->Release();
    }

    auto* icon = Activate<wuxc::IFontIcon>(L"Windows.UI.Xaml.Controls.FontIcon",
                                            openxaml::iid::Windows_UI_Xaml_Controls_IFontIcon);
    check(icon != nullptr, "FontIcon activation");
    if (icon) {
        HSTRING glyph = nullptr;
        WindowsCreateString(L"\ue932", 1, &glyph);
        check(SUCCEEDED(icon->put_Glyph(glyph)), "FontIcon glyph setter");
        WindowsDeleteString(glyph);
        icon->Release();
    }
    auto* mux_bitmap = Activate<IInspectable>(
        L"Microsoft.UI.Xaml.Controls.BitmapIconSource",
        openxaml::iid::IInspectable);
    check(mux_bitmap != nullptr, "WinUI BitmapIconSource activation");
    if (mux_bitmap) mux_bitmap->Release();

    auto* rectangle = Activate<wuxs::IRectangle>(L"Windows.UI.Xaml.Shapes.Rectangle",
        openxaml::iid::Windows_UI_Xaml_Shapes_IRectangle);
    check(rectangle != nullptr, "Rectangle activation");
    if (rectangle) {
        check(SUCCEEDED(rectangle->put_RadiusX(4)), "Rectangle radius setter");
        DOUBLE radius = 0;
        check(SUCCEEDED(rectangle->get_RadiusX(&radius)) && radius == 4,
              "Rectangle radius round-trip");
        wuxs::IShape* rectangle_shape = nullptr;
        check(SUCCEEDED(rectangle->QueryInterface(
                  openxaml::iid::Windows_UI_Xaml_Shapes_IShape,
                  reinterpret_cast<void**>(&rectangle_shape))) &&
                  rectangle_shape,
              "Rectangle IShape projection");
        if (rectangle_shape) rectangle_shape->Release();
        rectangle->Release();
    }

    auto* text_box = Activate<wuxc::ITextBox>(L"Windows.UI.Xaml.Controls.TextBox",
                                               openxaml::iid::Windows_UI_Xaml_Controls_ITextBox);
    check(text_box != nullptr, "TextBox activation");
    if (text_box) text_box->Release();
    auto* text_block = Activate<wuxc::ITextBlock>(
        L"Windows.UI.Xaml.Controls.TextBlock",
        openxaml::iid::Windows_UI_Xaml_Controls_ITextBlock);
    check(text_block != nullptr, "TextBlock activation");
    if (text_block) {
        check(SUCCEEDED(text_block->put_TextAlignment(wux::TextAlignment_Center)),
              "TextBlock alignment setter");
        wux::TextAlignment alignment = wux::TextAlignment_Left;
        check(SUCCEEDED(text_block->get_TextAlignment(&alignment)) &&
                  alignment == wux::TextAlignment_Center,
              "TextBlock alignment round-trip");
        text_block->Release();
    }
    auto* button = Activate<wuxc::IButton>(L"Windows.UI.Xaml.Controls.Button",
                                           openxaml::iid::Windows_UI_Xaml_Controls_IButton);
    check(button != nullptr, "Button activation");
    if (button) button->Release();
    auto* tooltip = Activate<wuxc::IToolTip>(L"Windows.UI.Xaml.Controls.ToolTip",
                                             openxaml::iid::Windows_UI_Xaml_Controls_IToolTip);
    check(tooltip != nullptr, "ToolTip activation");
    if (tooltip) tooltip->Release();
    auto* thumb = Activate<wuxcp::IThumb>(L"Windows.UI.Xaml.Controls.Primitives.Thumb",
                                          openxaml::iid::Windows_UI_Xaml_Controls_Primitives_IThumb);
    check(thumb != nullptr, "Thumb activation");
    if (thumb) thumb->Release();

    auto* canvas = Activate<wuxc::ICanvas>(L"Windows.UI.Xaml.Controls.Canvas",
                                            openxaml::iid::Windows_UI_Xaml_Controls_ICanvas);
    check(canvas != nullptr, "Canvas activation");
    if (canvas) canvas->Release();
    auto* presenter = Activate<wuxc::IContentPresenter>(
        L"Windows.UI.Xaml.Controls.ContentPresenter",
        openxaml::iid::Windows_UI_Xaml_Controls_IContentPresenter);
    check(presenter != nullptr, "ContentPresenter activation");
    if (presenter) presenter->Release();
    auto* image = Activate<wuxc::IImage>(L"Windows.UI.Xaml.Controls.Image",
                                         openxaml::iid::Windows_UI_Xaml_Controls_IImage);
    check(image != nullptr, "Image activation");
    if (image) image->Release();
    auto* path = Activate<wuxs::IPath>(L"Windows.UI.Xaml.Shapes.Path",
                                       openxaml::iid::Windows_UI_Xaml_Shapes_IPath);
    check(path != nullptr, "Path activation");
    if (path) {
        wuxs::IShape* shape = nullptr;
        check(SUCCEEDED(path->QueryInterface(
                  openxaml::iid::Windows_UI_Xaml_Shapes_IShape,
                  reinterpret_cast<void**>(&shape))) && shape,
              "Path IShape projection");
        if (shape) {
            DOUBLE number = -1;
            wuxm::Stretch stretch = wuxm::Stretch_Fill;
            check(SUCCEEDED(shape->get_StrokeThickness(&number)) && number == 0.0,
                  "Shape default StrokeThickness");
            check(SUCCEEDED(shape->get_StrokeMiterLimit(&number)) && number == 10.0,
                  "Shape default StrokeMiterLimit");
            check(SUCCEEDED(shape->get_Stretch(&stretch)) &&
                      stretch == wuxm::Stretch_None,
                  "Shape default Stretch");
            check(shape->put_StrokeThickness(-1.0) == E_INVALIDARG &&
                      shape->put_StrokeMiterLimit(0.5) == E_INVALIDARG &&
                      shape->put_Stretch(static_cast<wuxm::Stretch>(99)) == E_INVALIDARG,
                  "Shape validation");

            auto* solid = Activate<wuxm::ISolidColorBrush>(
                L"Windows.UI.Xaml.Media.SolidColorBrush",
                openxaml::iid::Windows_UI_Xaml_Media_ISolidColorBrush);
            wuxm::IBrush* brush = nullptr;
            if (solid) {
                const ABI::Windows::UI::Color red{255, 0xf0, 0x20, 0x10};
                check(SUCCEEDED(solid->put_Color(red)), "Shape brush color");
                check(SUCCEEDED(solid->QueryInterface(
                          openxaml::iid::Windows_UI_Xaml_Media_IBrush,
                          reinterpret_cast<void**>(&brush))) && brush,
                      "Shape brush projection");
            }
            check(brush && SUCCEEDED(shape->put_Fill(brush)) &&
                      SUCCEEDED(shape->put_Stroke(brush)) &&
                      SUCCEEDED(shape->put_StrokeThickness(2.0)),
                  "Shape retained paint setters");
            wuxm::IBrush* retained = nullptr;
            check(SUCCEEDED(shape->get_Fill(&retained)) &&
                      SameIdentity(static_cast<IInspectable*>(brush),
                                   static_cast<IInspectable*>(retained)),
                  "Shape Fill identity round-trip");
            if (retained) retained->Release();

            openxaml::winrt::IOpenXamlNative* native = nullptr;
            check(SUCCEEDED(path->QueryInterface(
                      openxaml::winrt::IID_IOpenXamlNative,
                      reinterpret_cast<void**>(&native))) && native,
                  "Shape native projection");
            if (native) {
                const auto& fill = native->LayoutElement()->fill_brush();
                const auto& stroke = native->LayoutElement()->stroke_brush();
                check(fill.declared && fill.has_color && fill.color.r == 0xf0 &&
                          stroke.declared && stroke.has_color && stroke.color.r == 0xf0,
                      "Shape live brush retention");
                native->Release();
            }

            wuxm::ITransform* geometry_transform = nullptr;
            check(SUCCEEDED(shape->get_GeometryTransform(&geometry_transform)) &&
                      geometry_transform,
                  "Shape identity GeometryTransform");
            if (geometry_transform) geometry_transform->Release();
            if (brush) brush->Release();
            if (solid) solid->Release();
            shape->Release();
        }
        path->Release();
    }
    auto* path_icon = Activate<wuxc::IPathIcon>(L"Windows.UI.Xaml.Controls.PathIcon",
                                                openxaml::iid::Windows_UI_Xaml_Controls_IPathIcon);
    check(path_icon != nullptr, "PathIcon activation");
    if (path_icon) path_icon->Release();

    // DurationHelper. Terminal reaches this one before main, so it is the
    // first thing the real host asks the runtime for; the values below are the
    // three-way case analysis the published XAML core does.
    auto* durations = Statics<wux::IDurationHelperStatics>(
        L"Windows.UI.Xaml.DurationHelper",
        openxaml::iid::Windows_UI_Xaml_IDurationHelperStatics);
    check(durations != nullptr, "DurationHelper statics");
    if (durations) {
        // 200ms in 100-nanosecond ticks, the way Pane.cpp builds it.
        wux::Duration span{};
        check(SUCCEEDED(durations->FromTimeSpan({2000000}, &span)) &&
                  span.Type == wux::DurationType_TimeSpan &&
                  span.TimeSpan.Duration == 2000000,
              "DurationHelper.FromTimeSpan");
        check(durations->FromTimeSpan({-1}, &span) == E_INVALIDARG,
              "DurationHelper.FromTimeSpan rejects a negative span");
        boolean has_span = 0;
        check(SUCCEEDED(durations->GetHasTimeSpan(span, &has_span)) && has_span,
              "DurationHelper.GetHasTimeSpan");
        wux::Duration automatic{};
        wux::Duration forever{};
        check(SUCCEEDED(durations->get_Automatic(&automatic)) &&
                  automatic.Type == wux::DurationType_Automatic,
              "DurationHelper.Automatic");
        check(SUCCEEDED(durations->get_Forever(&forever)) &&
                  forever.Type == wux::DurationType_Forever,
              "DurationHelper.Forever");
        check(SUCCEEDED(durations->GetHasTimeSpan(forever, &has_span)) && !has_span,
              "DurationHelper.GetHasTimeSpan says Forever has none");
        INT32 order = 0;
        check(SUCCEEDED(durations->Compare(automatic, span, &order)) && order == -1,
              "DurationHelper.Compare puts Automatic first");
        check(SUCCEEDED(durations->Compare(forever, span, &order)) && order == 1,
              "DurationHelper.Compare puts Forever last");
        check(SUCCEEDED(durations->Compare(forever, forever, &order)) && order == 0,
              "DurationHelper.Compare makes Forever equal to itself");
        wux::Duration sum{};
        check(SUCCEEDED(durations->Add(span, span, &sum)) &&
                  sum.Type == wux::DurationType_TimeSpan &&
                  sum.TimeSpan.Duration == 4000000,
              "DurationHelper.Add sums two spans");
        check(SUCCEEDED(durations->Add(forever, span, &sum)) &&
                  sum.Type == wux::DurationType_Forever,
              "DurationHelper.Add keeps Forever");
        check(SUCCEEDED(durations->Subtract(forever, span, &sum)) &&
                  sum.Type == wux::DurationType_Forever,
              "DurationHelper.Subtract keeps Forever");
        check(SUCCEEDED(durations->Subtract(span, forever, &sum)) &&
                  sum.Type == wux::DurationType_Automatic,
              "DurationHelper.Subtract of Forever is Automatic");
        boolean same = 0;
        check(SUCCEEDED(durations->Equals(span, span, &same)) && same,
              "DurationHelper.Equals");
        check(SUCCEEDED(durations->Equals(automatic, forever, &same)) && !same,
              "DurationHelper.Equals separates Automatic from Forever");
        durations->Release();
    }

    // Application, composed the way a derived App composes it.
    auto* applications = Statics<wux::IApplicationStatics>(
        L"Windows.UI.Xaml.Application",
        openxaml::iid::Windows_UI_Xaml_IApplicationStatics);
    check(applications != nullptr, "Application statics");
    if (applications) {
        wux::IApplication* before = reinterpret_cast<wux::IApplication*>(1);
        check(SUCCEEDED(applications->get_Current(&before)) && before == nullptr,
              "Application.Current is null before one is made");

        wux::IApplicationFactory* composer = nullptr;
        check(SUCCEEDED(applications->QueryInterface(
                  openxaml::iid::Windows_UI_Xaml_IApplicationFactory,
                  reinterpret_cast<void**>(&composer))),
              "Application composable factory");
        if (composer) {
            DerivedApp app;
            wux::IApplication* base = nullptr;
            check(SUCCEEDED(composer->CreateInstance(&app, &app.inner, &base)) &&
                      app.inner != nullptr && base != nullptr,
                  "Application composition");
            // CreateInstance handed back one reference on the aggregate, so
            // the derived object's count is two: its own and that one.
            check(app.references == 2, "Application composition counts the aggregate");

            if (base) {
                // The composed base is the same COM identity as the derived
                // object: asking the base for the derived object's own
                // interface has to come back to the derived object.
                IInspectable* identity = nullptr;
                check(SUCCEEDED(base->QueryInterface(openxaml::iid::IInspectable,
                                                     reinterpret_cast<void**>(&identity))) &&
                          identity == static_cast<IInspectable*>(&app),
                      "Application composition is one identity");
                if (identity) identity->Release();

                // A base interface reached through the derived object, which
                // is the path C++/WinRT's app.as<IApplication3>() takes.
                wux::IApplication3* three = nullptr;
                check(SUCCEEDED(app.QueryInterface(
                          openxaml::iid::Windows_UI_Xaml_IApplication3,
                          reinterpret_cast<void**>(&three))),
                      "Application3 through the aggregate");
                if (three) {
                    wux::ApplicationHighContrastAdjustment adjustment =
                        wux::ApplicationHighContrastAdjustment_None;
                    check(SUCCEEDED(three->get_HighContrastAdjustment(&adjustment)) &&
                              adjustment == wux::ApplicationHighContrastAdjustment_Auto,
                          "HighContrastAdjustment defaults to Auto");
                    check(SUCCEEDED(three->put_HighContrastAdjustment(
                              wux::ApplicationHighContrastAdjustment_None)),
                          "HighContrastAdjustment setter");
                    check(SUCCEEDED(three->get_HighContrastAdjustment(&adjustment)) &&
                              adjustment == wux::ApplicationHighContrastAdjustment_None,
                          "HighContrastAdjustment round-trip");
                    three->Release();
                }

                wux::IApplication* current = nullptr;
                check(SUCCEEDED(applications->get_Current(&current)) && current != nullptr,
                      "Application.Current after composition");
                if (current) {
                    IInspectable* same = nullptr;
                    check(SUCCEEDED(current->QueryInterface(
                              openxaml::iid::IInspectable, reinterpret_cast<void**>(&same))) &&
                              same == static_cast<IInspectable*>(&app),
                          "Application.Current is the composed application");
                    if (same) same->Release();
                    current->Release();
                }

                // Only one application per process, as the published core
                // enforces.
                DerivedApp second;
                wux::IApplication* refused = nullptr;
                check(composer->CreateInstance(&second, &second.inner, &refused) ==
                              E_UNEXPECTED &&
                          second.inner == nullptr,
                      "a second Application is refused");

                base->Release();
            }
            check(app.references == 1, "Application composition released the aggregate");
            composer->Release();
            // Letting `app` die releases the inner, which is the whole
            // aggregate; Application.Current goes back to null with it.
            app.Release();
            wux::IApplication* after = reinterpret_cast<wux::IApplication*>(1);
            check(SUCCEEDED(applications->get_Current(&after)) && after == nullptr,
                  "Application.Current is null again once the application dies");
        }
        applications->Release();
    }

    // --- the property system, through the ABI ---------------------------------
    //
    // Everything below reaches the same store the typed accessors above do.
    // That is the claim worth checking: not that GetValue returns something,
    // but that it returns what put_Width wrote, and that put_Width sees what
    // SetValue wrote.

    auto* property_statics = Statics<wux::IDependencyPropertyStatics>(
        L"Windows.UI.Xaml.DependencyProperty",
        openxaml::iid::Windows_UI_Xaml_IDependencyPropertyStatics);
    check(property_statics != nullptr, "DependencyProperty statics");

    auto* metadata_statics = Statics<wux::IPropertyMetadataStatics>(
        L"Windows.UI.Xaml.PropertyMetadata",
        openxaml::iid::Windows_UI_Xaml_IPropertyMetadataStatics);
    check(metadata_statics != nullptr, "PropertyMetadata statics");

    auto* grid_statics = Statics<wuxc::IGridStatics>(
        L"Windows.UI.Xaml.Controls.Grid",
        openxaml::iid::Windows_UI_Xaml_Controls_IGridStatics);
    check(grid_statics != nullptr, "Grid statics");

    IInspectable* unset = nullptr;
    if (property_statics) {
        check(SUCCEEDED(property_statics->get_UnsetValue(&unset)) && unset != nullptr,
              "DependencyProperty.UnsetValue");
    }

    // A *Property static is an identity, and the same one every time. A caller
    // comparing two dependency properties has nothing but the pointer to
    // compare, so two reads that disagreed would make every such comparison
    // false.
    wux::IDependencyProperty* row_property = nullptr;
    wux::IDependencyProperty* row_again = nullptr;
    if (grid_statics) {
        check(SUCCEEDED(grid_statics->get_RowProperty(&row_property)) && row_property,
              "Grid.RowProperty");
        check(SUCCEEDED(grid_statics->get_RowProperty(&row_again)) && row_again == row_property,
              "Grid.RowProperty identity");
    }

    // Its metadata is the registration's: Grid.Row defaults to 0.
    if (row_property) {
        wux::IPropertyMetadata* metadata = nullptr;
        ABI::Windows::UI::Xaml::Interop::TypeName any{};
        check(SUCCEEDED(row_property->GetMetadata(any, &metadata)) && metadata,
              "Grid.RowProperty metadata");
        if (metadata) {
            IInspectable* value = nullptr;
            check(SUCCEEDED(metadata->get_DefaultValue(&value)) && UnboxInt32(value) == 0,
                  "Grid.RowProperty default");
            if (value) value->Release();
            metadata->Release();
        }
    }

    auto* subject = Activate<wux::IFrameworkElement>(
        L"Windows.UI.Xaml.Controls.Border",
        openxaml::iid::Windows_UI_Xaml_IFrameworkElement);
    check(subject != nullptr, "property subject activation");

    wux::IDependencyObject* store = nullptr;
    if (subject) {
        check(SUCCEEDED(subject->QueryInterface(
                  openxaml::iid::Windows_UI_Xaml_IDependencyObject,
                  reinterpret_cast<void**>(&store))) && store,
              "IDependencyObject projection");
    }

    // An attached property, by identity, through the general store rather than
    // through IGridStatics' typed Get/Set pair -- and the two agree, because
    // there is one store behind both.
    if (store && row_property && grid_statics && subject) {
        IInspectable* local = nullptr;
        check(SUCCEEDED(store->ReadLocalValue(row_property, &local)) && local == unset,
              "attached ReadLocalValue is UnsetValue");
        if (local) local->Release();

        IInspectable* three = BoxInt32(3);
        check(SUCCEEDED(store->SetValue(row_property, three)), "attached SetValue");
        if (three) three->Release();

        INT32 typed = -1;
        check(SUCCEEDED(grid_statics->GetRow(subject, &typed)) && typed == 3,
              "attached SetValue reaches the typed getter");

        IInspectable* read = nullptr;
        check(SUCCEEDED(store->GetValue(row_property, &read)) && UnboxInt32(read) == 3,
              "attached GetValue");
        if (read) read->Release();

        check(SUCCEEDED(store->ReadLocalValue(row_property, &read)) && UnboxInt32(read) == 3,
              "attached ReadLocalValue");
        if (read) read->Release();

        check(SUCCEEDED(store->ClearValue(row_property)), "attached ClearValue");
        check(SUCCEEDED(grid_statics->GetRow(subject, &typed)) && typed == 0,
              "attached ClearValue restores the default");
        check(SUCCEEDED(store->ReadLocalValue(row_property, &read)) && read == unset,
              "attached ClearValue removes the local value");
        if (read) read->Release();
    }

    // A framework property both ways round. put_Width and SetValue are two
    // spellings of one write.
    wux::IDependencyProperty* width_property = nullptr;
    if (store && property_statics && metadata_statics && subject) {
        // There is no WidthProperty static on IFrameworkElement in this ABI,
        // so the identity is reached the way a caller reaches any other: by
        // registering nothing and asking the object. Instead the check goes
        // the other way -- a property this test registers itself, which is the
        // path Terminal's own DEPENDENCY_PROPERTY declarations take.
        IInspectable* default_value = BoxDouble(12.5);
        wux::IPropertyMetadata* metadata = nullptr;
        check(SUCCEEDED(metadata_statics->CreateWithDefaultValue(default_value, &metadata)) &&
                  metadata,
              "PropertyMetadata.CreateWithDefaultValue");
        if (default_value) default_value->Release();

        // A metadata carrying a property-changed callback is refused by name
        // rather than accepted and dropped: this DLL has no way back from a
        // layout element to the WinRT object that would have to be the
        // callback's sender. RegisterPropertyChangedCallback is the observer
        // that does work, and is checked below.
        wux::IPropertyMetadata* with_callback = nullptr;
        check(metadata_statics->CreateWithDefaultValueAndCallback(nullptr, nullptr,
                                                                  &with_callback) == E_NOTIMPL,
              "PropertyMetadata with a callback is refused by name");

        HSTRING name = nullptr;
        WindowsCreateString(L"SmokeScalar", 11, &name);
        ABI::Windows::UI::Xaml::Interop::TypeName owner{};
        WindowsCreateString(L"OpenXaml.Smoke", 14, &owner.Name);
        ABI::Windows::UI::Xaml::Interop::TypeName kind{};

        check(SUCCEEDED(property_statics->Register(name, kind, owner, metadata,
                                                   &width_property)) && width_property,
              "DependencyProperty.Register");

        // The same name on the same owner twice is a mistake, not a second
        // property.
        wux::IDependencyProperty* twin = nullptr;
        check(property_statics->Register(name, kind, owner, metadata, &twin) == E_INVALIDARG,
              "DependencyProperty.Register refuses a duplicate");

        // An attached property, registered the same way and readable on an
        // element of any type.
        HSTRING attached_name = nullptr;
        WindowsCreateString(L"SmokeSlot", 9, &attached_name);
        wux::IDependencyProperty* attached = nullptr;
        check(SUCCEEDED(property_statics->RegisterAttached(attached_name, kind, owner, metadata,
                                                           &attached)) && attached,
              "DependencyProperty.RegisterAttached");

        if (width_property) {
            IInspectable* read = nullptr;
            check(SUCCEEDED(store->GetValue(width_property, &read)) &&
                      UnboxDouble(read) == 12.5,
                  "registered property reads its default");
            if (read) read->Release();

            IInspectable* forty = BoxDouble(40.0);
            check(SUCCEEDED(store->SetValue(width_property, forty)), "registered SetValue");
            if (forty) forty->Release();
            check(SUCCEEDED(store->GetValue(width_property, &read)) && UnboxDouble(read) == 40.0,
                  "registered GetValue");
            if (read) read->Release();

            // An integer written to a property whose default is a double is
            // the double it stands for. Storing an int would turn the value
            // into a type error at the next read.
            IInspectable* seven = BoxInt32(7);
            check(SUCCEEDED(store->SetValue(width_property, seven)), "registered SetValue int");
            if (seven) seven->Release();
            check(SUCCEEDED(store->GetValue(width_property, &read)) && UnboxDouble(read) == 7.0,
                  "an int written to a double property is a double");
            if (read) read->Release();
        }
        if (attached) {
            IInspectable* read = nullptr;
            check(SUCCEEDED(store->GetValue(attached, &read)) && UnboxDouble(read) == 12.5,
                  "attached registration reads its default");
            if (read) read->Release();
            attached->Release();
        }
        if (metadata) metadata->Release();
        WindowsDeleteString(name);
        WindowsDeleteString(attached_name);
        WindowsDeleteString(owner.Name);
    }

    // RegisterPropertyChangedCallback: a real token, a callback that runs on a
    // change and not on a write that changes nothing, and an unregister that
    // stops it.
    if (subject && width_property) {
        wux::IDependencyObject2* observable = nullptr;
        check(SUCCEEDED(subject->QueryInterface(
                  openxaml::iid::Windows_UI_Xaml_IDependencyObject2,
                  reinterpret_cast<void**>(&observable))) && observable,
              "IDependencyObject2 projection");
        if (observable) {
            PropertyChangedHandler handler;
            INT64 token = 0;
            check(SUCCEEDED(observable->RegisterPropertyChangedCallback(
                      width_property, &handler, &token)) && token != 0,
                  "RegisterPropertyChangedCallback");

            IInspectable* value = BoxDouble(99.0);
            store->SetValue(width_property, value);
            if (value) value->Release();
            check(handler.calls == 1, "the callback ran once");
            check(handler.last_property == width_property,
                  "the callback was told which property");

            // The same value again is not a change.
            value = BoxDouble(99.0);
            store->SetValue(width_property, value);
            if (value) value->Release();
            check(handler.calls == 1, "an unchanged write raises nothing");

            check(SUCCEEDED(observable->UnregisterPropertyChangedCallback(width_property, token)),
                  "UnregisterPropertyChangedCallback");
            value = BoxDouble(1.0);
            store->SetValue(width_property, value);
            if (value) value->Release();
            check(handler.calls == 1, "an unregistered callback does not run");
            observable->Release();
        }
    }

    // --- events ---------------------------------------------------------------

    if (subject) {
        // Loaded is stored and never raised -- there is no live visual tree
        // here. What is checked is the registration contract: two handlers get
        // two different non-zero tokens, and a token takes its own handler off.
        RoutedHandler first;
        RoutedHandler second;
        EventRegistrationToken first_token{};
        EventRegistrationToken second_token{};
        check(SUCCEEDED(subject->add_Loaded(&first, &first_token)) && first_token.value != 0,
              "add_Loaded");
        check(SUCCEEDED(subject->add_Loaded(&second, &second_token)) &&
                  second_token.value != 0 && second_token.value != first_token.value,
              "add_Loaded twice gives two tokens");
        check(first.references() == 2, "the DLL holds a reference to the handler");
        check(SUCCEEDED(subject->remove_Loaded(first_token)), "remove_Loaded");
        check(first.references() == 1, "removing releases the handler");
        // A token already removed, and one belonging to nothing, are both
        // accepted rather than failed -- the runtime does not fail them either.
        check(SUCCEEDED(subject->remove_Loaded(first_token)), "remove_Loaded is idempotent");
        check(SUCCEEDED(subject->remove_Loaded(second_token)), "remove_Loaded the second");
        check(second.references() == 1, "the second handler was released too");
        check(subject->add_Loaded(nullptr, &first_token) == E_INVALIDARG,
              "add_Loaded refuses a null handler");
    }

    // SizeChanged and LayoutUpdated are the two events a layout pass raises,
    // at the moment the published core raises them: queued during arrange,
    // delivered after the pass.
    {
        auto* panel = Activate<wux::IFrameworkElement>(
            L"Windows.UI.Xaml.Controls.StackPanel",
            openxaml::iid::Windows_UI_Xaml_IFrameworkElement);
        check(panel != nullptr, "event subject activation");
        if (panel) {
            SizeChangedHandler sized;
            LayoutUpdatedHandler updated;
            EventRegistrationToken size_token{};
            EventRegistrationToken layout_token{};
            check(SUCCEEDED(panel->add_SizeChanged(&sized, &size_token)) && size_token.value != 0,
                  "add_SizeChanged");
            check(SUCCEEDED(panel->add_LayoutUpdated(&updated, &layout_token)) &&
                      layout_token.value != 0,
                  "add_LayoutUpdated");

            wux::IUIElement* element = nullptr;
            check(SUCCEEDED(panel->QueryInterface(openxaml::iid::Windows_UI_Xaml_IUIElement,
                                                   reinterpret_cast<void**>(&element))) && element,
                  "event subject UIElement projection");
            if (element) {
                element->Measure({120, 90});
                element->Arrange({0, 0, 120, 90});
                check(sized.calls == 1, "SizeChanged was raised once");
                check(sized.previous.Width == 0 && sized.previous.Height == 0,
                      "SizeChanged reports the size before the pass");
                check(sized.current.Width == 120 && sized.current.Height == 90,
                      "SizeChanged reports the size after the pass");
                check(updated.calls == 1, "LayoutUpdated was raised once");

                // A pass that changes no size raises no SizeChanged, and a
                // LayoutUpdated anyway: one is about a size, the other about
                // the pass.
                element->Measure({120, 90});
                element->Arrange({0, 0, 120, 90});
                check(sized.calls == 1, "an unchanged pass raises no SizeChanged");
                check(updated.calls == 2, "LayoutUpdated is raised every pass");

                check(SUCCEEDED(panel->remove_SizeChanged(size_token)), "remove_SizeChanged");
                check(SUCCEEDED(panel->remove_LayoutUpdated(layout_token)),
                      "remove_LayoutUpdated");
                element->Measure({60, 40});
                element->Arrange({0, 0, 60, 40});
                check(sized.calls == 1, "a removed SizeChanged handler does not run");
                check(updated.calls == 2, "a removed LayoutUpdated handler does not run");
                element->Release();
            }
            panel->Release();
        }
    }

    if (unset) unset->Release();
    if (row_property) row_property->Release();
    if (row_again) row_again->Release();
    if (width_property) width_property->Release();
    if (store) store->Release();
    if (subject) subject->Release();
    if (grid_statics) grid_statics->Release();
    if (metadata_statics) metadata_statics->Release();
    if (property_statics) property_statics->Release();

    RoUninitialize();
    if (!failures) std::puts("Wave 3/4 activation smoke passed");
    return failures ? 1 : 0;
}
