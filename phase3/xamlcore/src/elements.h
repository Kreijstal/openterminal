// The WinRT objects: Border, Grid, StackPanel and Grid's definitions.
//
// Each one is a COM object implementing the SDK's interfaces, wrapping an
// instance of the layout core. Nothing here reimplements layout -- the whole
// point is that the arithmetic already verified against the real runtime is
// the same arithmetic reached through the ABI.
//
// Every method not implemented here answers E_NOTIMPL through the generated
// bases. That is the honest answer: this DLL has no brushes, no transforms,
// no events and no property system, and a caller that needs one should find
// out by being told so rather than by receiving a plausible zero.

#ifndef OPENXAML_ELEMENTS_H
#define OPENXAML_ELEMENTS_H

#include <cmath>
#include <map>
#include <memory>
#include <roapi.h>

#include "border.h"
#include "collection.h"
#include "com.h"
#include "grid.h"
#include "openxaml_abi_stubs.h"
#include "stack_panel.h"
#include "text.h"
#include "advanced_controls.h"
#include "basic_controls.h"
#include "canvas.h"
#include "content_presenter.h"
#include "icon.h"
#include "image.h"
#include "scroll_viewer.h"
#include "shape.h"

namespace openxaml::winrt {

namespace wf = ABI::Windows::Foundation;
namespace wux = ABI::Windows::UI::Xaml;
namespace wuxc = ABI::Windows::UI::Xaml::Controls;
namespace wuxm = ABI::Windows::UI::Xaml::Media;
namespace wuxcp = ABI::Windows::UI::Xaml::Controls::Primitives;
namespace wuxd = ABI::Windows::UI::Xaml::Documents;
namespace wuxdata = ABI::Windows::UI::Xaml::Data;
namespace wuxs = ABI::Windows::UI::Xaml::Shapes;
namespace wuxi = ABI::Windows::UI::Xaml::Input;

// --- strings ------------------------------------------------------------------
//
// The layout core carries text as UTF-8 and the ABI carries it as UTF-16, so
// something has to convert. Doing it here rather than through
// WideCharToMultiByte keeps the DLL's imports to what it already had.

inline std::string Utf8FromHString(HSTRING text) {
    UINT32 length = 0;
    const wchar_t* buffer = WindowsGetStringRawBuffer(text, &length);
    std::string out;
    for (UINT32 index = 0; index < length; ++index) {
        char32_t code = buffer[index];
        // A surrogate pair is one codepoint written as two UTF-16 units.
        if (code >= 0xD800 && code <= 0xDBFF && index + 1 < length &&
            buffer[index + 1] >= 0xDC00 && buffer[index + 1] <= 0xDFFF) {
            code = 0x10000 + ((code - 0xD800) << 10) + (buffer[++index] - 0xDC00);
        }
        if (code < 0x80) {
            out += static_cast<char>(code);
        } else if (code < 0x800) {
            out += static_cast<char>(0xC0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else if (code < 0x10000) {
            out += static_cast<char>(0xE0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (code >> 18));
            out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        }
    }
    return out;
}

inline std::string Utf8FromCodePoint(char32_t code) {
    std::string out;
    if (code < 0x80) {
        out += static_cast<char>(code);
    } else if (code < 0x800) {
        out += static_cast<char>(0xC0 | (code >> 6));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else if (code < 0x10000) {
        out += static_cast<char>(0xE0 | (code >> 12));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (code >> 18));
        out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    }
    return out;
}

inline HRESULT HStringFromUtf8(const std::string& text, HSTRING* out) {
    if (!out) return E_POINTER;
    std::wstring wide;
    size_t index = 0;
    while (index < text.size()) {
        const auto lead = static_cast<unsigned char>(text[index]);
        size_t extra = lead < 0x80 ? 0 : (lead & 0xE0) == 0xC0 ? 1 : (lead & 0xF0) == 0xE0 ? 2 : 3;
        char32_t code = lead < 0x80 ? lead : lead & (0x3F >> extra);
        if (index + extra >= text.size()) return E_INVALIDARG;
        for (size_t step = 1; step <= extra; ++step)
            code = (code << 6) | (static_cast<unsigned char>(text[index + step]) & 0x3F);
        index += extra + 1;
        if (code < 0x10000) {
            wide += static_cast<wchar_t>(code);
        } else {
            code -= 0x10000;
            wide += static_cast<wchar_t>(0xD800 + (code >> 10));
            wide += static_cast<wchar_t>(0xDC00 + (code & 0x3FF));
        }
    }
    return WindowsCreateString(wide.c_str(), static_cast<UINT32>(wide.size()), out);
}

// --- collection traits --------------------------------------------------------

template <>
struct CollectionTraits<wux::IUIElement> {
    using Projected = openxaml::Element*;
    static HRESULT Project(wux::IUIElement* item, Projected* out) {
        IOpenXamlNative* native = nullptr;
        const HRESULT hr = item->QueryInterface(IID_IOpenXamlNative,
                                                reinterpret_cast<void**>(&native));
        if (FAILED(hr)) return E_INVALIDARG;
        *out = native->LayoutElement();
        native->Release();
        return S_OK;
    }
};

template <>
struct CollectionTraits<IInspectable> {
    using Projected = IInspectable*;
    static HRESULT Project(IInspectable* item, Projected* out) {
        if (!item || !out) return E_INVALIDARG;
        *out = item;
        return S_OK;
    }
};

template <>
struct CollectionTraits<wux::IResourceDictionary> {
    using Projected = wux::IResourceDictionary*;
    static HRESULT Project(wux::IResourceDictionary* item, Projected* out) {
        if (!item || !out) return E_INVALIDARG;
        *out = item;
        return S_OK;
    }
};

template <>
struct CollectionTraits<wuxc::IMenuFlyoutItemBase> {
    using Projected = wuxc::IMenuFlyoutItemBase*;
    static HRESULT Project(wuxc::IMenuFlyoutItemBase* item, Projected* out) {
        if (!item || !out) return E_INVALIDARG;
        *out = item;
        return S_OK;
    }
};

template <>
struct CollectionTraits<wuxc::ICommandBarElement> {
    using Projected = wuxc::ICommandBarElement*;
    static HRESULT Project(wuxc::ICommandBarElement* item, Projected* out) {
        if (!item || !out) return E_INVALIDARG;
        *out = item;
        return S_OK;
    }
};

template <>
struct CollectionTraits<wuxd::IInline> {
    using Projected = wuxd::IInline*;
    static HRESULT Project(wuxd::IInline* item, Projected* out) {
        if (!item || !out) return E_INVALIDARG;
        *out = item;
        return S_OK;
    }
};

template <>
struct CollectionTraits<wuxc::IColumnDefinition> {
    using Projected = const openxaml::Definition*;
    static HRESULT Project(wuxc::IColumnDefinition* item, Projected* out) {
        IOpenXamlDefinition* native = nullptr;
        const HRESULT hr = item->QueryInterface(IID_IOpenXamlDefinition,
                                                reinterpret_cast<void**>(&native));
        if (FAILED(hr)) return E_INVALIDARG;
        *out = native->LayoutDefinition();
        native->Release();
        return S_OK;
    }
};

template <>
struct CollectionTraits<wuxc::IRowDefinition> {
    using Projected = const openxaml::Definition*;
    static HRESULT Project(wuxc::IRowDefinition* item, Projected* out) {
        IOpenXamlDefinition* native = nullptr;
        const HRESULT hr = item->QueryInterface(IID_IOpenXamlDefinition,
                                                reinterpret_cast<void**>(&native));
        if (FAILED(hr)) return E_INVALIDARG;
        *out = native->LayoutDefinition();
        native->Release();
        return S_OK;
    }
};

namespace collections = ABI::Windows::Foundation::Collections;

using ChildCollection = Vector<__FIVector_1_Windows__CUI__CXaml__CUIElement,
                               __FIIterable_1_Windows__CUI__CXaml__CUIElement,
                               __FIIterator_1_Windows__CUI__CXaml__CUIElement,
                               wux::IUIElement,
                               collections::IVectorView<wux::UIElement*>>;
using ColumnCollection =
    Vector<__FIVector_1_Windows__CUI__CXaml__CControls__CColumnDefinition,
           __FIIterable_1_Windows__CUI__CXaml__CControls__CColumnDefinition,
           __FIIterator_1_Windows__CUI__CXaml__CControls__CColumnDefinition,
           wuxc::IColumnDefinition,
           collections::IVectorView<wuxc::ColumnDefinition*>>;
using RowCollection = Vector<__FIVector_1_Windows__CUI__CXaml__CControls__CRowDefinition,
                             __FIIterable_1_Windows__CUI__CXaml__CControls__CRowDefinition,
                             __FIIterator_1_Windows__CUI__CXaml__CControls__CRowDefinition,
                             wuxc::IRowDefinition,
                             collections::IVectorView<wuxc::RowDefinition*>>;
using MenuFlyoutItemCollection = Vector<
    __FIVector_1_Windows__CUI__CXaml__CControls__CMenuFlyoutItemBase,
    __FIIterable_1_Windows__CUI__CXaml__CControls__CMenuFlyoutItemBase,
    __FIIterator_1_Windows__CUI__CXaml__CControls__CMenuFlyoutItemBase,
    wuxc::IMenuFlyoutItemBase,
    __FIVectorView_1_Windows__CUI__CXaml__CControls__CMenuFlyoutItemBase>;
using InlineCollection = Vector<
    __FIVector_1_Windows__CUI__CXaml__CDocuments__CInline,
    __FIIterable_1_Windows__CUI__CXaml__CDocuments__CInline,
    __FIIterator_1_Windows__CUI__CXaml__CDocuments__CInline,
    wuxd::IInline,
    __FIVectorView_1_Windows__CUI__CXaml__CDocuments__CInline>;
using CommandBarElementCollection = ObservableVector<
    __FIVector_1_Windows__CUI__CXaml__CControls__CICommandBarElement,
    __FIIterable_1_Windows__CUI__CXaml__CControls__CICommandBarElement,
    __FIIterator_1_Windows__CUI__CXaml__CControls__CICommandBarElement,
    wuxc::ICommandBarElement,
    __FIVectorView_1_Windows__CUI__CXaml__CControls__CICommandBarElement,
    __FIObservableVector_1_Windows__CUI__CXaml__CControls__CICommandBarElement,
    __FVectorChangedEventHandler_1_Windows__CUI__CXaml__CControls__CICommandBarElement>;

// --- layout subclasses --------------------------------------------------------
//
// The layout core owns its children through unique_ptr, which a refcounted COM
// object cannot be. Every layout class reads its children through the virtual
// Children(), so overriding just that lets the WinRT collection own the
// references while the algorithm stays untouched.

template <class Base>
class ChildSourced : public Base {
public:
    const ChildCollection* source = nullptr;
    std::vector<openxaml::Element*> Children() const override {
        return source ? source->Projected() : std::vector<openxaml::Element*>{};
    }
};

using AbiBorder = ChildSourced<openxaml::Border>;
using AbiGrid = ChildSourced<openxaml::Grid>;
using AbiStackPanel = ChildSourced<openxaml::StackPanel>;
using AbiCanvas = ChildSourced<openxaml::Canvas>;

// TransformToVisual is used by the desktop host to place its native title-bar
// input window over the XAML tree. Until the renderer grows a separate visual
// transform stack, layout coordinates are already expressed in the island's
// coordinate space, so this object is the identity transform.
class GeneralTransformObject final : public ComObject,
                                     public wuxm::IGeneralTransform {
public:
    const wchar_t* RuntimeClassName() const override {
        return L"Windows.UI.Xaml.Media.GeneralTransform";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Media_IGeneralTransform,
                        wuxm::IGeneralTransform)
        OPENXAML_QI_ARM(IID_IUnknown, wuxm::IGeneralTransform)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable, wuxm::IGeneralTransform)
        *object = nullptr;
        return E_NOINTERFACE;
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE get_Inverse(wuxm::IGeneralTransform** value) override {
        if (!value) return E_POINTER;
        *value = this;
        AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE TransformPoint(wf::Point point, wf::Point* value) override {
        if (!value) return E_POINTER;
        *value = point;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE TryTransform(wf::Point point, wf::Point* value,
                                           boolean* transformed) override {
        if (!value || !transformed) return E_POINTER;
        *value = point;
        *transformed = 1;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE TransformBounds(wf::Rect rect, wf::Rect* value) override {
        if (!value) return E_POINTER;
        *value = rect;
        return S_OK;
    }
};

class ImageBrushObject final : public ComObject,
                               public abi::NotImpl_IDependencyObject,
                               public abi::NotImpl_IBrush,
                               public abi::NotImpl_IImageBrush {
public:
    using PrimaryInterface = wuxm::IImageBrush;
    ~ImageBrushObject() override {
        if (source_) source_->Release();
        if (transform_) transform_->Release();
        if (relative_transform_) relative_transform_->Release();
        for (auto& [_, handler] : handlers_) handler->Release();
    }
    const wchar_t* RuntimeClassName() const override {
        return L"Windows.UI.Xaml.Media.ImageBrush";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Media_IImageBrush,
                        wuxm::IImageBrush)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Media_IBrush, wuxm::IBrush)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_IDependencyObject,
                        wux::IDependencyObject)
        OPENXAML_QI_ARM(IID_IUnknown, wuxm::IImageBrush)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable, wuxm::IImageBrush)
        *object = nullptr;
        return E_NOINTERFACE;
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE get_Opacity(DOUBLE* value) override {
        if (!value) return E_POINTER;
        *value = opacity_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Opacity(DOUBLE value) override {
        opacity_ = value; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Transform(wuxm::ITransform** value) override {
        return Get(transform_, value);
    }
    HRESULT STDMETHODCALLTYPE put_Transform(wuxm::ITransform* value) override {
        return Put(transform_, value);
    }
    HRESULT STDMETHODCALLTYPE get_RelativeTransform(wuxm::ITransform** value) override {
        return Get(relative_transform_, value);
    }
    HRESULT STDMETHODCALLTYPE put_RelativeTransform(wuxm::ITransform* value) override {
        return Put(relative_transform_, value);
    }
    HRESULT STDMETHODCALLTYPE get_ImageSource(wuxm::IImageSource** value) override {
        return Get(source_, value);
    }
    HRESULT STDMETHODCALLTYPE put_ImageSource(wuxm::IImageSource* value) override {
        return Put(source_, value);
    }
    HRESULT STDMETHODCALLTYPE add_ImageFailed(wux::IExceptionRoutedEventHandler* handler,
                                               EventRegistrationToken* token) override {
        return Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_ImageFailed(EventRegistrationToken token) override {
        return Remove(token);
    }
    HRESULT STDMETHODCALLTYPE add_ImageOpened(wux::IRoutedEventHandler* handler,
                                               EventRegistrationToken* token) override {
        return Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_ImageOpened(EventRegistrationToken token) override {
        return Remove(token);
    }

private:
    template <class T> static HRESULT Get(T* source, T** value) {
        if (!value) return E_POINTER;
        *value = source;
        if (*value) (*value)->AddRef();
        return S_OK;
    }
    template <class T> static HRESULT Put(T*& target, T* value) {
        if (value) value->AddRef();
        if (target) target->Release();
        target = value;
        return S_OK;
    }
    HRESULT Add(IUnknown* handler, EventRegistrationToken* token) {
        if (!handler || !token) return E_INVALIDARG;
        token->value = ++next_token_; handler->AddRef(); handlers_[token->value] = handler;
        return S_OK;
    }
    HRESULT Remove(EventRegistrationToken token) {
        const auto found = handlers_.find(token.value);
        if (found == handlers_.end()) return S_OK;
        found->second->Release(); handlers_.erase(found); return S_OK;
    }

    DOUBLE opacity_ = 1.0;
    wuxm::IImageSource* source_ = nullptr;
    wuxm::ITransform* transform_ = nullptr;
    wuxm::ITransform* relative_transform_ = nullptr;
    LONGLONG next_token_ = 0;
    std::map<LONGLONG, IUnknown*> handlers_;
};

class SolidColorBrushObject final : public ComObject,
                                    public abi::NotImpl_IDependencyObject,
                                    public abi::NotImpl_IBrush,
                                    public abi::NotImpl_ISolidColorBrush {
public:
    using PrimaryInterface = wuxm::ISolidColorBrush;
    ~SolidColorBrushObject() override {
        if (transform_) transform_->Release();
        if (relative_transform_) relative_transform_->Release();
    }
    const wchar_t* RuntimeClassName() const override {
        return L"Windows.UI.Xaml.Media.SolidColorBrush";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Media_ISolidColorBrush,
                        wuxm::ISolidColorBrush)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Media_IBrush, wuxm::IBrush)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_IDependencyObject,
                        wux::IDependencyObject)
        OPENXAML_QI_ARM(IID_IUnknown, wuxm::ISolidColorBrush)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable, wuxm::ISolidColorBrush)
        *object = nullptr;
        return TraceQueryInterfaceMiss(RuntimeClassName(), iid);
    }
    OPENXAML_COM_BOILERPLATE()
    HRESULT STDMETHODCALLTYPE get_Color(ABI::Windows::UI::Color* value) override {
        if (!value) return E_POINTER;
        *value = color_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Color(ABI::Windows::UI::Color value) override {
        color_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Opacity(DOUBLE* value) override {
        if (!value) return E_POINTER;
        *value = opacity_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Opacity(DOUBLE value) override {
        opacity_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Transform(wuxm::ITransform** value) override {
        return Get(transform_, value);
    }
    HRESULT STDMETHODCALLTYPE put_Transform(wuxm::ITransform* value) override {
        return Put(transform_, value);
    }
    HRESULT STDMETHODCALLTYPE get_RelativeTransform(wuxm::ITransform** value) override {
        return Get(relative_transform_, value);
    }
    HRESULT STDMETHODCALLTYPE put_RelativeTransform(wuxm::ITransform* value) override {
        return Put(relative_transform_, value);
    }
private:
    template <class T> static HRESULT Get(T* source, T** value) {
        if (!value) return E_POINTER;
        *value = source;
        if (*value) (*value)->AddRef();
        return S_OK;
    }
    template <class T> static HRESULT Put(T*& target, T* value) {
        if (value) value->AddRef();
        if (target) target->Release();
        target = value;
        return S_OK;
    }
    ABI::Windows::UI::Color color_{255, 0, 0, 0};
    DOUBLE opacity_ = 1.0;
    wuxm::ITransform* transform_ = nullptr;
    wuxm::ITransform* relative_transform_ = nullptr;
};

// Terminal's VisualBellLight is a nonvisual XamlLight placed in UIElement.Lights.
// Composition lights are not rendered by the compatibility backend yet, but
// the generated component connector still needs the local runtime interface
// so it can retain the named light.
inline constexpr GUID IID_ITerminalVisualBellLight = {
    0x8eba68bd, 0xc33a, 0x5572,
    {0xaf, 0xa9, 0xcc, 0x71, 0x3d, 0x3b, 0xe2, 0x29}};

struct ITerminalVisualBellLight : IInspectable {};

// Private attached-property storage used by AutomationProperties.  Keeping it
// on the element avoids a process-wide table that would either dangle when an
// element is destroyed or retain the entire visual tree forever.
inline constexpr GUID IID_IOpenXamlAutomationProperties = {
    0x6f70656e, 0x7861, 0x6d6c,
    {0x9e, 0x03, 0x61, 0x75, 0x74, 0x6f, 0x6d, 0x61}};

struct IOpenXamlAutomationProperties : IUnknown {
    virtual HRESULT SetAutomationString(UINT32 property, HSTRING value) = 0;
    virtual HRESULT GetAutomationString(UINT32 property, HSTRING* value) = 0;
};

class VisualBellLightObject final : public ComObject,
                                    public abi::NotImpl_IDependencyObject,
                                    public ITerminalVisualBellLight {
public:
    using PrimaryInterface = ITerminalVisualBellLight;
    const wchar_t* RuntimeClassName() const override {
        return L"Microsoft.Terminal.Control.VisualBellLight";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(IID_ITerminalVisualBellLight, ITerminalVisualBellLight)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_IDependencyObject,
                        wux::IDependencyObject)
        OPENXAML_QI_ARM(IID_IUnknown, ITerminalVisualBellLight)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable, ITerminalVisualBellLight)
        *object = nullptr;
        return TraceQueryInterfaceMiss(RuntimeClassName(), iid);
    }
    OPENXAML_COM_BOILERPLATE()
};

// --- the element base ---------------------------------------------------------

class XamlElement : public ComObject,
                    public abi::NotImpl_IDependencyObject,
                    public abi::NotImpl_IDependencyObject2,
                    public abi::NotImpl_IUIElement,
                    public abi::NotImpl_IUIElement4,
                    public abi::NotImpl_IUIElement5,
                    public abi::NotImpl_IUIElement7,
                    public abi::NotImpl_IUIElement10,
                    public abi::NotImpl_IFrameworkElement,
                    public abi::NotImpl_IFrameworkElement2,
                    public IWeakReferenceSource,
                    public IOpenXamlAutomationProperties,
                    public IOpenXamlNative {
public:
    // The interface an activation factory returns this object as. Every
    // element is a UIElement; Grid's definitions are not, and say so.
    using PrimaryInterface = wux::IUIElement;

    XamlElement()
        : weak_state_(std::make_shared<WeakReferenceState>(
              this, static_cast<wux::IUIElement*>(this))) {}

    ~XamlElement() override {
        weak_state_->Invalidate();
        if (resources_) resources_->Release();
        if (context_flyout_) context_flyout_->Release();
        if (xaml_root_) xaml_root_->Release();
        if (shadow_) shadow_->Release();
        for (auto& [_, value] : automation_strings_)
            WindowsDeleteString(value);
        for (auto& [_, handler] : event_handlers_) handler->Release();
        for (auto& [_, handler] : loaded_handlers_) handler->Release();
        for (auto& [_, handler] : layout_updated_handlers_) handler->Release();
    }

    virtual openxaml::Element* Layout() = 0;
    const openxaml::Element* Layout() const {
        return const_cast<XamlElement*>(this)->Layout();
    }

    openxaml::Element* LayoutElement() override { return Layout(); }
    HRESULT PerformLayout(double width, double height) override {
        if (width < 0.0 || height < 0.0) return E_INVALIDARG;
        TraceRuntime("OpenXaml: island layout begin\n");
        EnsureLayoutPassCallback();
        HRESULT result = S_OK;
        try {
            Layout()->Measure({width, height});
            TraceRuntime("OpenXaml: island measure complete\n");
        } catch (...) {
            // A leaf renderer (most commonly text when no harvested metrics
            // are installed) must not suppress layout for every ancestor or
            // prevent the application from observing its first layout pass.
            // Arrange still gives stretch panels their actual island size.
            TraceRuntime("OpenXaml: island measure degraded\n");
            result = S_FALSE;
        }
        try {
            Layout()->Arrange({0.0, 0.0, width, height});
            TraceRuntime("OpenXaml: island arrange complete\n");
        } catch (...) {
            TraceRuntime("OpenXaml: island arrange degraded\n");
            result = S_FALSE;
        }
        try {
            Layout()->NotifyLayoutPass();
            TraceRuntime("OpenXaml: island events complete\n");
        } catch (...) {
            TraceRuntime("OpenXaml: island events failed\n");
            return E_FAIL;
        }
        return result;
    }

    // --- IUIElement ---
    HRESULT STDMETHODCALLTYPE Measure(wf::Size available) override {
        Layout()->Measure({available.Width, available.Height});
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Arrange(wf::Rect final_rect) override {
        Layout()->Arrange({final_rect.X, final_rect.Y, final_rect.Width, final_rect.Height});
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_DesiredSize(wf::Size* value) override {
        if (!value) return E_POINTER;
        const openxaml::Size desired = Layout()->desired_size();
        *value = {static_cast<FLOAT>(desired.width), static_cast<FLOAT>(desired.height)};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_UseLayoutRounding(boolean* value) override {
        if (!value) return E_POINTER;
        *value = Layout()->use_layout_rounding() ? 1 : 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_UseLayoutRounding(boolean value) override {
        Layout()->set_use_layout_rounding(value != 0);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Visibility(wux::Visibility* value) override {
        if (!value) return E_POINTER;
        *value = static_cast<wux::Visibility>(Layout()->visibility());
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Visibility(wux::Visibility value) override {
        if (value != wux::Visibility_Visible && value != wux::Visibility_Collapsed)
            return E_INVALIDARG;
        Layout()->set_visibility(static_cast<openxaml::Visibility>(value));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE TransformToVisual(
        wux::IUIElement*, wuxm::IGeneralTransform** value) override {
        if (!value) return E_POINTER;
        *value = new (std::nothrow) GeneralTransformObject();
        return *value ? S_OK : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE RegisterPropertyChangedCallback(
        wux::IDependencyProperty*, wux::IDependencyPropertyChangedCallback* handler,
        INT64* token) override {
        if (!token) return E_POINTER;
        EventRegistrationToken event_token{};
        const HRESULT hr = AddEvent(handler, &event_token);
        *token = event_token.value;
        return hr;
    }
    HRESULT STDMETHODCALLTYPE UnregisterPropertyChangedCallback(
        wux::IDependencyProperty*, INT64 token) override {
        return RemoveEvent({token});
    }
    HRESULT STDMETHODCALLTYPE get_Dispatcher(
        ABI::Windows::UI::Core::ICoreDispatcher** value) override {
        if (!value) return E_POINTER;
        // A CoreDispatcher projection is supplied next to the message-loop
        // backend.  Returning an absent dispatcher is the WinRT-compatible
        // representation while no island dispatcher has been attached.
        *value = nullptr;
        return S_OK;
    }
    HRESULT SetAutomationString(UINT32 property, HSTRING value) override {
        HSTRING next = nullptr;
        HRESULT hr = WindowsDuplicateString(value, &next);
        if (FAILED(hr)) return hr;
        const auto found = automation_strings_.find(property);
        if (found != automation_strings_.end()) {
            WindowsDeleteString(found->second);
            found->second = next;
        } else {
            automation_strings_.emplace(property, next);
        }
        return S_OK;
    }
    HRESULT GetAutomationString(UINT32 property, HSTRING* value) override {
        if (!value) return E_POINTER;
        const auto found = automation_strings_.find(property);
        if (found == automation_strings_.end()) return CopyToHString(L"", value);
        return WindowsDuplicateString(found->second, value);
    }
    HRESULT STDMETHODCALLTYPE add_DoubleTapped(
        wuxi::IDoubleTappedEventHandler* handler, EventRegistrationToken* token) override {
        return AddEvent(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_DoubleTapped(EventRegistrationToken token) override {
        return RemoveEvent(token);
    }
    HRESULT STDMETHODCALLTYPE add_Tapped(
        wuxi::ITappedEventHandler* handler, EventRegistrationToken* token) override {
        return AddEvent(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_Tapped(EventRegistrationToken token) override {
        return RemoveEvent(token);
    }
    HRESULT STDMETHODCALLTYPE add_Holding(
        wuxi::IHoldingEventHandler* handler, EventRegistrationToken* token) override {
        return AddEvent(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_Holding(EventRegistrationToken token) override {
        return RemoveEvent(token);
    }
    HRESULT STDMETHODCALLTYPE add_RightTapped(
        wuxi::IRightTappedEventHandler* handler, EventRegistrationToken* token) override {
        return AddEvent(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_RightTapped(EventRegistrationToken token) override {
        return RemoveEvent(token);
    }
    HRESULT STDMETHODCALLTYPE add_DragEnter(
        wux::IDragEventHandler* handler, EventRegistrationToken* token) override {
        return AddEvent(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_DragEnter(EventRegistrationToken token) override {
        return RemoveEvent(token);
    }
    HRESULT STDMETHODCALLTYPE add_DragLeave(
        wux::IDragEventHandler* handler, EventRegistrationToken* token) override {
        return AddEvent(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_DragLeave(EventRegistrationToken token) override {
        return RemoveEvent(token);
    }
    HRESULT STDMETHODCALLTYPE add_DragOver(
        wux::IDragEventHandler* handler, EventRegistrationToken* token) override {
        return AddEvent(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_DragOver(EventRegistrationToken token) override {
        return RemoveEvent(token);
    }
    HRESULT STDMETHODCALLTYPE add_Drop(
        wux::IDragEventHandler* handler, EventRegistrationToken* token) override {
        return AddEvent(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_Drop(EventRegistrationToken token) override {
        return RemoveEvent(token);
    }
    HRESULT STDMETHODCALLTYPE add_KeyUp(
        wuxi::IKeyEventHandler* handler, EventRegistrationToken* token) override {
        return AddEvent(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_KeyUp(EventRegistrationToken token) override {
        return RemoveEvent(token);
    }
    HRESULT STDMETHODCALLTYPE add_KeyDown(
        wuxi::IKeyEventHandler* handler, EventRegistrationToken* token) override {
        return AddEvent(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_KeyDown(EventRegistrationToken token) override {
        return RemoveEvent(token);
    }
    HRESULT STDMETHODCALLTYPE add_GotFocus(
        wux::IRoutedEventHandler* handler, EventRegistrationToken* token) override {
        return AddEvent(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_GotFocus(EventRegistrationToken token) override {
        return RemoveEvent(token);
    }
    HRESULT STDMETHODCALLTYPE add_LostFocus(
        wux::IRoutedEventHandler* handler, EventRegistrationToken* token) override {
        return AddEvent(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_LostFocus(EventRegistrationToken token) override {
        return RemoveEvent(token);
    }
#define OPENXAML_POINTER_EVENT(name)                                      \
    HRESULT STDMETHODCALLTYPE add_##name(                                \
        wuxi::IPointerEventHandler* handler, EventRegistrationToken* token) override { \
        return AddEvent(handler, token);                                  \
    }                                                                     \
    HRESULT STDMETHODCALLTYPE remove_##name(EventRegistrationToken token) override { \
        return RemoveEvent(token);                                        \
    }
    OPENXAML_POINTER_EVENT(PointerPressed)
    OPENXAML_POINTER_EVENT(PointerMoved)
    OPENXAML_POINTER_EVENT(PointerReleased)
    OPENXAML_POINTER_EVENT(PointerEntered)
    OPENXAML_POINTER_EVENT(PointerExited)
    OPENXAML_POINTER_EVENT(PointerCaptureLost)
    OPENXAML_POINTER_EVENT(PointerCanceled)
    OPENXAML_POINTER_EVENT(PointerWheelChanged)
#undef OPENXAML_POINTER_EVENT
    HRESULT STDMETHODCALLTYPE add_ContextRequested(
        __FITypedEventHandler_2_Windows__CUI__CXaml__CUIElement_Windows__CUI__CXaml__CInput__CContextRequestedEventArgs* handler,
        EventRegistrationToken* token) override {
        return AddEvent(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_ContextRequested(
        EventRegistrationToken token) override {
        return RemoveEvent(token);
    }
    HRESULT STDMETHODCALLTYPE get_ContextFlyout(wuxcp::IFlyoutBase** value) override {
        if (!value) return E_POINTER;
        *value = context_flyout_;
        if (*value) (*value)->AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_ContextFlyout(wuxcp::IFlyoutBase* value) override {
        if (value) value->AddRef();
        if (context_flyout_) context_flyout_->Release();
        context_flyout_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE add_CharacterReceived(
        __FITypedEventHandler_2_Windows__CUI__CXaml__CUIElement_Windows__CUI__CXaml__CInput__CCharacterReceivedRoutedEventArgs* handler,
        EventRegistrationToken* token) override {
        return AddEvent(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_CharacterReceived(
        EventRegistrationToken token) override {
        return RemoveEvent(token);
    }
    HRESULT STDMETHODCALLTYPE add_ProcessKeyboardAccelerators(
        __FITypedEventHandler_2_Windows__CUI__CXaml__CUIElement_Windows__CUI__CXaml__CInput__CProcessKeyboardAcceleratorEventArgs* handler,
        EventRegistrationToken* token) override {
        return AddEvent(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_ProcessKeyboardAccelerators(
        EventRegistrationToken token) override {
        return RemoveEvent(token);
    }
    HRESULT STDMETHODCALLTYPE add_PreviewKeyDown(
        wuxi::IKeyEventHandler* handler, EventRegistrationToken* token) override {
        return AddEvent(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_PreviewKeyDown(
        EventRegistrationToken token) override {
        return RemoveEvent(token);
    }
    HRESULT STDMETHODCALLTYPE add_PreviewKeyUp(
        wuxi::IKeyEventHandler* handler, EventRegistrationToken* token) override {
        return AddEvent(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_PreviewKeyUp(
        EventRegistrationToken token) override {
        return RemoveEvent(token);
    }

    // C++/WinRT's auto_revoke event overload stores a weak reference to the
    // event source. Real XAML elements support this contract; without it the
    // generated projection dereferences a null IWeakReferenceSource pointer.
    HRESULT STDMETHODCALLTYPE GetWeakReference(IWeakReference** value) override {
        if (!value) return E_POINTER;
        *value = new (std::nothrow) WeakReferenceObject(weak_state_);
        return *value ? S_OK : E_OUTOFMEMORY;
    }

    // --- IFrameworkElement ---
    HRESULT STDMETHODCALLTYPE get_ActualWidth(DOUBLE* value) override {
        if (!value) return E_POINTER;
        *value = Layout()->render_size().width;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_ActualHeight(DOUBLE* value) override {
        if (!value) return E_POINTER;
        *value = Layout()->render_size().height;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Resources(wux::IResourceDictionary** value) override {
        if (!value) return E_POINTER;
        if (!resources_) {
            HSTRING name = nullptr;
            HRESULT hr = WindowsCreateString(
                L"Windows.UI.Xaml.ResourceDictionary", 34, &name);
            IInspectable* instance = nullptr;
            if (SUCCEEDED(hr)) hr = RoActivateInstance(name, &instance);
            WindowsDeleteString(name);
            if (SUCCEEDED(hr)) {
                hr = instance->QueryInterface(
                    GUID{0xc1ea4f24, 0xd6de, 0x4191,
                         {0x8e, 0x3a, 0xf4, 0x86, 0x01, 0xf7, 0x48, 0x9c}},
                    reinterpret_cast<void**>(&resources_));
            }
            if (instance) instance->Release();
            if (FAILED(hr)) return hr;
        }
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
    HRESULT STDMETHODCALLTYPE add_SizeChanged(
        wux::ISizeChangedEventHandler* handler, EventRegistrationToken* token) override {
        return AddEvent(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_SizeChanged(EventRegistrationToken token) override {
        return RemoveEvent(token);
    }
    HRESULT STDMETHODCALLTYPE add_Loaded(
        wux::IRoutedEventHandler* handler, EventRegistrationToken* token) override {
        EnsureLayoutPassCallback();
        return AddTypedEvent(handler, token, loaded_handlers_);
    }
    HRESULT STDMETHODCALLTYPE remove_Loaded(EventRegistrationToken token) override {
        return RemoveTypedEvent(token, loaded_handlers_);
    }
    HRESULT STDMETHODCALLTYPE add_LayoutUpdated(
        __FIEventHandler_1_IInspectable* handler,
        EventRegistrationToken* token) override {
        EnsureLayoutPassCallback();
        return AddTypedEvent(handler, token, layout_updated_handlers_);
    }
    HRESULT STDMETHODCALLTYPE remove_LayoutUpdated(
        EventRegistrationToken token) override {
        return RemoveTypedEvent(token, layout_updated_handlers_);
    }
    HRESULT STDMETHODCALLTYPE get_RequestedTheme(wux::ElementTheme* value) override {
        if (!value) return E_POINTER;
        *value = requested_theme_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_RequestedTheme(wux::ElementTheme value) override {
        if (value < wux::ElementTheme_Default || value > wux::ElementTheme_Dark)
            return E_INVALIDARG;
        requested_theme_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Parent(wux::IDependencyObject** value) override {
        if (!value) return E_POINTER;
        // Island roots and popup roots are detached by definition. Child
        // collection parent projection will replace this null with a retained
        // parent when the ABI visual-tree layer starts exposing ancestry.
        *value = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE add_DataContextChanged(
        __FITypedEventHandler_2_Windows__CUI__CXaml__CFrameworkElement_Windows__CUI__CXaml__CDataContextChangedEventArgs* handler,
        EventRegistrationToken* token) override {
        return AddEvent(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_DataContextChanged(EventRegistrationToken token) override {
        return RemoveEvent(token);
    }

    // --- IUIElement5 ---
    // These properties guide focus navigation and key-tip presentation in the
    // native XAML runtime.  The compatibility renderer does not draw key tips
    // yet, but retaining the values and subscriptions is important: WinUI's
    // TabView wires the focus events while constructing every TabViewItem.
    HRESULT STDMETHODCALLTYPE get_KeyTipPlacementMode(
        wuxi::KeyTipPlacementMode* value) override {
        if (!value) return E_POINTER;
        *value = key_tip_placement_mode_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_KeyTipPlacementMode(
        wuxi::KeyTipPlacementMode value) override {
        key_tip_placement_mode_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_KeyTipHorizontalOffset(DOUBLE* value) override {
        if (!value) return E_POINTER;
        *value = key_tip_horizontal_offset_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_KeyTipHorizontalOffset(DOUBLE value) override {
        key_tip_horizontal_offset_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_KeyTipVerticalOffset(DOUBLE* value) override {
        if (!value) return E_POINTER;
        *value = key_tip_vertical_offset_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_KeyTipVerticalOffset(DOUBLE value) override {
        key_tip_vertical_offset_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_XYFocusKeyboardNavigation(
        wuxi::XYFocusKeyboardNavigationMode* value) override {
        if (!value) return E_POINTER;
        *value = xy_focus_keyboard_navigation_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_XYFocusKeyboardNavigation(
        wuxi::XYFocusKeyboardNavigationMode value) override {
        xy_focus_keyboard_navigation_ = value;
        return S_OK;
    }
#define OPENXAML_XY_FOCUS_STRATEGY(name, field)                         \
    HRESULT STDMETHODCALLTYPE get_##name(                               \
        wuxi::XYFocusNavigationStrategy* value) override {              \
        if (!value) return E_POINTER;                                   \
        *value = field;                                                 \
        return S_OK;                                                    \
    }                                                                   \
    HRESULT STDMETHODCALLTYPE put_##name(                               \
        wuxi::XYFocusNavigationStrategy value) override {               \
        field = value;                                                  \
        return S_OK;                                                    \
    }
    OPENXAML_XY_FOCUS_STRATEGY(XYFocusUpNavigationStrategy,
                               xy_focus_up_navigation_strategy_)
    OPENXAML_XY_FOCUS_STRATEGY(XYFocusDownNavigationStrategy,
                               xy_focus_down_navigation_strategy_)
    OPENXAML_XY_FOCUS_STRATEGY(XYFocusLeftNavigationStrategy,
                               xy_focus_left_navigation_strategy_)
    OPENXAML_XY_FOCUS_STRATEGY(XYFocusRightNavigationStrategy,
                               xy_focus_right_navigation_strategy_)
#undef OPENXAML_XY_FOCUS_STRATEGY
    HRESULT STDMETHODCALLTYPE get_HighContrastAdjustment(
        wux::ElementHighContrastAdjustment* value) override {
        if (!value) return E_POINTER;
        *value = high_contrast_adjustment_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_HighContrastAdjustment(
        wux::ElementHighContrastAdjustment value) override {
        high_contrast_adjustment_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_TabFocusNavigation(
        wuxi::KeyboardNavigationMode* value) override {
        if (!value) return E_POINTER;
        *value = tab_focus_navigation_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_TabFocusNavigation(
        wuxi::KeyboardNavigationMode value) override {
        tab_focus_navigation_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE add_GettingFocus(
        __FITypedEventHandler_2_Windows__CUI__CXaml__CUIElement_Windows__CUI__CXaml__CInput__CGettingFocusEventArgs* handler,
        EventRegistrationToken* token) override {
        return AddEvent(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_GettingFocus(EventRegistrationToken token) override {
        return RemoveEvent(token);
    }
    HRESULT STDMETHODCALLTYPE add_LosingFocus(
        __FITypedEventHandler_2_Windows__CUI__CXaml__CUIElement_Windows__CUI__CXaml__CInput__CLosingFocusEventArgs* handler,
        EventRegistrationToken* token) override {
        return AddEvent(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_LosingFocus(EventRegistrationToken token) override {
        return RemoveEvent(token);
    }
    HRESULT STDMETHODCALLTYPE add_NoFocusCandidateFound(
        __FITypedEventHandler_2_Windows__CUI__CXaml__CUIElement_Windows__CUI__CXaml__CInput__CNoFocusCandidateFoundEventArgs* handler,
        EventRegistrationToken* token) override {
        return AddEvent(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_NoFocusCandidateFound(
        EventRegistrationToken token) override {
        return RemoveEvent(token);
    }
    HRESULT STDMETHODCALLTYPE StartBringIntoView() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE StartBringIntoViewWithOptions(
        wux::IBringIntoViewOptions*) override {
        return S_OK;
    }

    // --- IUIElement10 ---
    HRESULT STDMETHODCALLTYPE get_ActualOffset(
        ABI::Windows::Foundation::Numerics::Vector3* value) override {
        if (!value) return E_POINTER;
        *value = {0.0f, 0.0f, 0.0f};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_ActualSize(
        ABI::Windows::Foundation::Numerics::Vector2* value) override {
        if (!value) return E_POINTER;
        const auto size = Layout()->render_size();
        *value = {static_cast<FLOAT>(size.width),
                  static_cast<FLOAT>(size.height)};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_XamlRoot(wux::IXamlRoot** value) override {
        if (!value) return E_POINTER;
        *value = xaml_root_;
        if (*value) (*value)->AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_XamlRoot(wux::IXamlRoot* value) override {
        if (value) value->AddRef();
        if (xaml_root_) xaml_root_->Release();
        xaml_root_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_UIContext(
        ABI::Windows::UI::IUIContext** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Shadow(wuxm::IShadow** value) override {
        if (!value) return E_POINTER;
        *value = shadow_;
        if (*value) (*value)->AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Shadow(wuxm::IShadow* value) override {
        if (value) value->AddRef();
        if (shadow_) shadow_->Release();
        shadow_ = value;
        return S_OK;
    }

#define OPENXAML_DOUBLE_PROPERTY(name, field)                          \
    HRESULT STDMETHODCALLTYPE get_##name(DOUBLE* value) override {     \
        if (!value) return E_POINTER;                                  \
        *value = Layout()->field();                                    \
        return S_OK;                                                   \
    }                                                                  \
    HRESULT STDMETHODCALLTYPE put_##name(DOUBLE value) override {      \
        Layout()->set_##field(value);                                  \
        return S_OK;                                                   \
    }

    OPENXAML_DOUBLE_PROPERTY(Width, width)
    OPENXAML_DOUBLE_PROPERTY(Height, height)
    OPENXAML_DOUBLE_PROPERTY(MinWidth, min_width)
    OPENXAML_DOUBLE_PROPERTY(MaxWidth, max_width)
    OPENXAML_DOUBLE_PROPERTY(MinHeight, min_height)
    OPENXAML_DOUBLE_PROPERTY(MaxHeight, max_height)
#undef OPENXAML_DOUBLE_PROPERTY

    HRESULT STDMETHODCALLTYPE get_Margin(wux::Thickness* value) override {
        if (!value) return E_POINTER;
        const openxaml::Thickness& margin = Layout()->margin();
        *value = {margin.left, margin.top, margin.right, margin.bottom};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Margin(wux::Thickness value) override {
        Layout()->set_margin({value.Left, value.Top, value.Right, value.Bottom});
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_HorizontalAlignment(wux::HorizontalAlignment* value) override {
        if (!value) return E_POINTER;
        switch (Layout()->horizontal_alignment()) {
            case openxaml::HorizontalAlignment::Left: *value = wux::HorizontalAlignment_Left; break;
            case openxaml::HorizontalAlignment::Center: *value = wux::HorizontalAlignment_Center; break;
            case openxaml::HorizontalAlignment::Right: *value = wux::HorizontalAlignment_Right; break;
            default: *value = wux::HorizontalAlignment_Stretch; break;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_HorizontalAlignment(wux::HorizontalAlignment value) override {
        switch (value) {
            case wux::HorizontalAlignment_Left:
                Layout()->set_horizontal_alignment(openxaml::HorizontalAlignment::Left); break;
            case wux::HorizontalAlignment_Center:
                Layout()->set_horizontal_alignment(openxaml::HorizontalAlignment::Center); break;
            case wux::HorizontalAlignment_Right:
                Layout()->set_horizontal_alignment(openxaml::HorizontalAlignment::Right); break;
            case wux::HorizontalAlignment_Stretch:
                Layout()->set_horizontal_alignment(openxaml::HorizontalAlignment::Stretch); break;
            default:
                return E_INVALIDARG;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_VerticalAlignment(wux::VerticalAlignment* value) override {
        if (!value) return E_POINTER;
        switch (Layout()->vertical_alignment()) {
            case openxaml::VerticalAlignment::Top: *value = wux::VerticalAlignment_Top; break;
            case openxaml::VerticalAlignment::Center: *value = wux::VerticalAlignment_Center; break;
            case openxaml::VerticalAlignment::Bottom: *value = wux::VerticalAlignment_Bottom; break;
            default: *value = wux::VerticalAlignment_Stretch; break;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_VerticalAlignment(wux::VerticalAlignment value) override {
        switch (value) {
            case wux::VerticalAlignment_Top:
                Layout()->set_vertical_alignment(openxaml::VerticalAlignment::Top); break;
            case wux::VerticalAlignment_Center:
                Layout()->set_vertical_alignment(openxaml::VerticalAlignment::Center); break;
            case wux::VerticalAlignment_Bottom:
                Layout()->set_vertical_alignment(openxaml::VerticalAlignment::Bottom); break;
            case wux::VerticalAlignment_Stretch:
                Layout()->set_vertical_alignment(openxaml::VerticalAlignment::Stretch); break;
            default:
                return E_INVALIDARG;
        }
        return S_OK;
    }

protected:
    template <class Handler>
    HRESULT AddTypedEvent(Handler* handler, EventRegistrationToken* token,
                          std::map<LONGLONG, Handler*>& handlers) {
        if (!handler || !token) return E_INVALIDARG;
        token->value = InterlockedIncrement64(&next_event_token_);
        handler->AddRef();
        handlers[token->value] = handler;
        return S_OK;
    }
    template <class Handler>
    HRESULT RemoveTypedEvent(EventRegistrationToken token,
                             std::map<LONGLONG, Handler*>& handlers) {
        const auto found = handlers.find(token.value);
        if (found == handlers.end()) return S_OK;
        found->second->Release();
        handlers.erase(found);
        return S_OK;
    }

    HRESULT AddEvent(IUnknown* handler, EventRegistrationToken* token) {
        if (!handler || !token) return E_INVALIDARG;
        token->value = InterlockedIncrement64(&next_event_token_);
        handler->AddRef();
        event_handlers_[token->value] = handler;
        return S_OK;
    }
    HRESULT RemoveEvent(EventRegistrationToken token) {
        const auto found = event_handlers_.find(token.value);
        if (found == event_handlers_.end()) return S_OK;
        found->second->Release();
        event_handlers_.erase(found);
        return S_OK;
    }

    // The interfaces every element carries. A concrete class tries its own
    // first, then falls through to here.
    HRESULT QueryElementInterface(REFIID iid, void** object) {
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_IUIElement, wux::IUIElement)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_IUIElement4, wux::IUIElement4)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_IUIElement5, wux::IUIElement5)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_IUIElement7, wux::IUIElement7)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_IUIElement10,
                        wux::IUIElement10)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_IFrameworkElement, wux::IFrameworkElement)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_IFrameworkElement2,
                        wux::IFrameworkElement2)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_IDependencyObject, wux::IDependencyObject)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_IDependencyObject2,
                        wux::IDependencyObject2)
        OPENXAML_QI_ARM(IID_OpenXamlWeakReferenceSource, IWeakReferenceSource)
        OPENXAML_QI_ARM(IID_IOpenXamlAutomationProperties,
                        IOpenXamlAutomationProperties)
        OPENXAML_QI_ARM(IID_IUnknown, wux::IUIElement)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable, wux::IUIElement)
        if (IsEqualGUID(iid, IID_IOpenXamlNative)) {
            auto* pointer = static_cast<IOpenXamlNative*>(this);
            static_cast<wux::IUIElement*>(this)->AddRef();
            *object = pointer;
            return S_OK;
        }
        *object = nullptr;
        return TraceQueryInterfaceMiss(RuntimeClassName(), iid);
    }

private:
    void EnsureLayoutPassCallback() {
        if (layout_callback_installed_) return;
        Layout()->SetLayoutPassCallback([this]() { RaiseFrameworkEvents(); });
        layout_callback_installed_ = true;
    }
    void RaiseFrameworkEvents() {
        IInspectable* sender = static_cast<IInspectable*>(
            static_cast<wux::IUIElement*>(this));

        if (!loaded_raised_) {
            loaded_raised_ = true;
            std::vector<wux::IRoutedEventHandler*> handlers;
            handlers.reserve(loaded_handlers_.size());
            for (const auto& [_, handler] : loaded_handlers_) {
                handler->AddRef();
                handlers.push_back(handler);
            }
            for (auto* handler : handlers) {
                handler->Invoke(sender, nullptr);
                handler->Release();
            }
        }

        std::vector<__FIEventHandler_1_IInspectable*> handlers;
        handlers.reserve(layout_updated_handlers_.size());
        for (const auto& [_, handler] : layout_updated_handlers_) {
            handler->AddRef();
            handlers.push_back(handler);
        }
        for (auto* handler : handlers) {
            TraceRuntime("OpenXaml: raising LayoutUpdated\n");
            handler->Invoke(sender, nullptr);
            handler->Release();
        }
    }

    std::shared_ptr<WeakReferenceState> weak_state_;
    wux::IResourceDictionary* resources_ = nullptr;
    wuxcp::IFlyoutBase* context_flyout_ = nullptr;
    wux::IXamlRoot* xaml_root_ = nullptr;
    wuxm::IShadow* shadow_ = nullptr;
    wux::ElementTheme requested_theme_ = wux::ElementTheme_Default;
    wuxi::KeyTipPlacementMode key_tip_placement_mode_ =
        static_cast<wuxi::KeyTipPlacementMode>(0);
    DOUBLE key_tip_horizontal_offset_ = 0.0;
    DOUBLE key_tip_vertical_offset_ = 0.0;
    wuxi::XYFocusKeyboardNavigationMode xy_focus_keyboard_navigation_ =
        static_cast<wuxi::XYFocusKeyboardNavigationMode>(0);
    wuxi::XYFocusNavigationStrategy xy_focus_up_navigation_strategy_ =
        static_cast<wuxi::XYFocusNavigationStrategy>(0);
    wuxi::XYFocusNavigationStrategy xy_focus_down_navigation_strategy_ =
        static_cast<wuxi::XYFocusNavigationStrategy>(0);
    wuxi::XYFocusNavigationStrategy xy_focus_left_navigation_strategy_ =
        static_cast<wuxi::XYFocusNavigationStrategy>(0);
    wuxi::XYFocusNavigationStrategy xy_focus_right_navigation_strategy_ =
        static_cast<wuxi::XYFocusNavigationStrategy>(0);
    wux::ElementHighContrastAdjustment high_contrast_adjustment_ =
        static_cast<wux::ElementHighContrastAdjustment>(0);
    wuxi::KeyboardNavigationMode tab_focus_navigation_ =
        static_cast<wuxi::KeyboardNavigationMode>(0);
    LONGLONG next_event_token_ = 0;
    std::map<LONGLONG, IUnknown*> event_handlers_;
    std::map<LONGLONG, wux::IRoutedEventHandler*> loaded_handlers_;
    std::map<LONGLONG, __FIEventHandler_1_IInspectable*> layout_updated_handlers_;
    std::map<UINT32, HSTRING> automation_strings_;
    bool layout_callback_installed_ = false;
    bool loaded_raised_ = false;
};

// --- Border -------------------------------------------------------------------

class BorderObject final : public XamlElement, public abi::NotImpl_IBorder {
public:
    BorderObject() { children_.source = &child_holder_; }
    ~BorderObject() override {
        if (border_brush_) border_brush_->Release();
        if (background_) background_->Release();
    }

    openxaml::Element* Layout() override { return &children_; }
    const wchar_t* RuntimeClassName() const override {
        return L"Windows.UI.Xaml.Controls.Border";
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IBorder, wuxc::IBorder)
        return QueryElementInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()

    // Border has a Child, not a Children collection. It is still backed by the
    // same collection type, capped at one entry, so that child ownership and
    // projection work identically everywhere.
    HRESULT STDMETHODCALLTYPE put_Child(wux::IUIElement* child) override {
        const HRESULT hr = child_holder_.Clear();
        if (FAILED(hr)) return hr;
        if (!child) return S_OK;
        return child_holder_.Append(child);
    }
    HRESULT STDMETHODCALLTYPE get_Child(wux::IUIElement** child) override {
        if (!child) return E_POINTER;
        if (child_holder_.Count() == 0) {
            *child = nullptr;
            return S_OK;
        }
        return child_holder_.GetAt(0, child);
    }

    HRESULT STDMETHODCALLTYPE get_BorderThickness(wux::Thickness* value) override {
        if (!value) return E_POINTER;
        const openxaml::Thickness& t = children_.border_thickness();
        *value = {t.left, t.top, t.right, t.bottom};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_BorderThickness(wux::Thickness value) override {
        children_.set_border_thickness({value.Left, value.Top, value.Right, value.Bottom});
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Padding(wux::Thickness* value) override {
        if (!value) return E_POINTER;
        const openxaml::Thickness& t = children_.padding();
        *value = {t.left, t.top, t.right, t.bottom};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Padding(wux::Thickness value) override {
        children_.set_padding({value.Left, value.Top, value.Right, value.Bottom});
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_BorderBrush(wuxm::IBrush** value) override {
        return GetBrush(border_brush_, value);
    }
    HRESULT STDMETHODCALLTYPE put_BorderBrush(wuxm::IBrush* value) override {
        return PutBrush(border_brush_, value);
    }
    HRESULT STDMETHODCALLTYPE get_Background(wuxm::IBrush** value) override {
        return GetBrush(background_, value);
    }
    HRESULT STDMETHODCALLTYPE put_Background(wuxm::IBrush* value) override {
        return PutBrush(background_, value);
    }
    HRESULT STDMETHODCALLTYPE get_CornerRadius(wux::CornerRadius* value) override {
        if (!value) return E_POINTER;
        *value = corner_radius_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_CornerRadius(wux::CornerRadius value) override {
        corner_radius_ = value;
        return S_OK;
    }

private:
    static HRESULT GetBrush(wuxm::IBrush* source, wuxm::IBrush** value) {
        if (!value) return E_POINTER;
        *value = source;
        if (*value) (*value)->AddRef();
        return S_OK;
    }
    static HRESULT PutBrush(wuxm::IBrush*& target, wuxm::IBrush* value) {
        if (value) value->AddRef();
        if (target) target->Release();
        target = value;
        return S_OK;
    }
    AbiBorder children_;
    wuxm::IBrush* border_brush_ = nullptr;
    wuxm::IBrush* background_ = nullptr;
    wux::CornerRadius corner_radius_{};
    ChildCollection child_holder_{
        {::openxaml::iid::PIID_FIVector_1_Windows__CUI__CXaml__CUIElement,
         ::openxaml::iid::PIID_FIIterable_1_Windows__CUI__CXaml__CUIElement,
         ::openxaml::iid::PIID_FIIterator_1_Windows__CUI__CXaml__CUIElement},
        L"Windows.UI.Xaml.Controls.UIElementCollection", this};
};

// --- Panel-derived ------------------------------------------------------------

template <class LayoutType>
class PanelObject : public XamlElement, public abi::NotImpl_IPanel {
public:
    PanelObject() { layout_.source = &children_; }
    ~PanelObject() override { if (background_) background_->Release(); }

    openxaml::Element* Layout() override { return &layout_; }

    HRESULT STDMETHODCALLTYPE get_Children(
        __FIVector_1_Windows__CUI__CXaml__CUIElement** value) override {
        if (!value) return E_POINTER;
        children_.AddRef();
        *value = &children_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Background(wuxm::IBrush** value) override {
        if (!value) return E_POINTER;
        *value = background_;
        if (*value) (*value)->AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Background(wuxm::IBrush* value) override {
        if (value) value->AddRef();
        if (background_) background_->Release();
        background_ = value;
        return S_OK;
    }

protected:
    HRESULT QueryPanelInterface(REFIID iid, void** object) {
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IPanel, wuxc::IPanel)
        return QueryElementInterface(iid, object);
    }

    LayoutType layout_;
    wuxm::IBrush* background_ = nullptr;
    ChildCollection children_{{::openxaml::iid::PIID_FIVector_1_Windows__CUI__CXaml__CUIElement,
                               ::openxaml::iid::PIID_FIIterable_1_Windows__CUI__CXaml__CUIElement,
                               ::openxaml::iid::PIID_FIIterator_1_Windows__CUI__CXaml__CUIElement},
                              L"Windows.UI.Xaml.Controls.UIElementCollection", this};
};

class StackPanelObject final : public PanelObject<AbiStackPanel>,
                               public abi::NotImpl_IStackPanel {
public:
    const wchar_t* RuntimeClassName() const override {
        return L"Windows.UI.Xaml.Controls.StackPanel";
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IStackPanel, wuxc::IStackPanel)
        return QueryPanelInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE get_Orientation(wuxc::Orientation* value) override {
        if (!value) return E_POINTER;
        *value = layout_.orientation() == openxaml::Orientation::Horizontal
                     ? wuxc::Orientation_Horizontal
                     : wuxc::Orientation_Vertical;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Orientation(wuxc::Orientation value) override {
        layout_.set_orientation(value == wuxc::Orientation_Horizontal
                                    ? openxaml::Orientation::Horizontal
                                    : openxaml::Orientation::Vertical);
        return S_OK;
    }
};

class CanvasObject final : public PanelObject<AbiCanvas>, public abi::NotImpl_ICanvas {
public:
    const wchar_t* RuntimeClassName() const override { return L"Windows.UI.Xaml.Controls.Canvas"; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_ICanvas, wuxc::ICanvas)
        return QueryPanelInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()
};

// The native interop interfaces are not part of the WinRT metadata. They are
// declared in windows.ui.xaml.media.dxinterop.h in Microsoft's desktop SDK;
// keep their exact COM shape private so this DLL does not depend on that
// optional header.
inline constexpr GUID IID_OpenXamlSwapChainPanelNative = {
    0xf92f19d2, 0x3ade, 0x45a6, {0xa2, 0x0c, 0xf6, 0xf1, 0xea, 0x90, 0x55, 0x4b}};
inline constexpr GUID IID_OpenXamlSwapChainPanelNative2 = {
    0xd5a2f60c, 0x37b2, 0x44a2, {0x93, 0x7b, 0x8d, 0x8e, 0xb9, 0x72, 0x68, 0x21}};
struct IOpenXamlSwapChainPanelNative : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE SetSwapChain(IUnknown* swap_chain) = 0;
};
struct IOpenXamlSwapChainPanelNative2 : IOpenXamlSwapChainPanelNative {
    virtual HRESULT STDMETHODCALLTYPE SetSwapChainHandle(HANDLE handle) = 0;
};

class SwapChainPanelObject final : public PanelObject<AbiGrid>,
                                   public abi::NotImpl_ISwapChainPanel,
                                   public IOpenXamlSwapChainPanelNative2 {
public:
    using PrimaryInterface = wuxc::ISwapChainPanel;
    ~SwapChainPanelObject() override {
        if (swap_chain_) swap_chain_->Release();
    }
    const wchar_t* RuntimeClassName() const override {
        return L"Windows.UI.Xaml.Controls.SwapChainPanel";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_ISwapChainPanel,
                        wuxc::ISwapChainPanel)
        if (IsEqualGUID(iid, IID_OpenXamlSwapChainPanelNative) ||
            IsEqualGUID(iid, IID_OpenXamlSwapChainPanelNative2)) {
            auto* pointer = static_cast<IOpenXamlSwapChainPanelNative2*>(this);
            static_cast<wuxc::ISwapChainPanel*>(this)->AddRef();
            *object = IsEqualGUID(iid, IID_OpenXamlSwapChainPanelNative2)
                          ? static_cast<void*>(pointer)
                          : static_cast<void*>(
                                static_cast<IOpenXamlSwapChainPanelNative*>(pointer));
            return S_OK;
        }
        return QueryPanelInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE get_CompositionScaleX(FLOAT* value) override {
        if (!value) return E_POINTER;
        *value = 1.0f;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_CompositionScaleY(FLOAT* value) override {
        return get_CompositionScaleX(value);
    }
    HRESULT STDMETHODCALLTYPE add_CompositionScaleChanged(
        __FITypedEventHandler_2_Windows__CUI__CXaml__CControls__CSwapChainPanel_IInspectable*
            handler,
        EventRegistrationToken* token) override {
        return AddEvent(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_CompositionScaleChanged(
        EventRegistrationToken token) override {
        return RemoveEvent(token);
    }
    HRESULT STDMETHODCALLTYPE CreateCoreIndependentInputSource(
        ABI::Windows::UI::Core::CoreInputDeviceTypes,
        ABI::Windows::UI::Core::ICoreInputSourceBase** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE SetSwapChain(IUnknown* value) override {
        if (value) value->AddRef();
        if (swap_chain_) swap_chain_->Release();
        swap_chain_ = value;
        swap_chain_handle_ = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetSwapChainHandle(HANDLE value) override {
        if (swap_chain_) {
            swap_chain_->Release();
            swap_chain_ = nullptr;
        }
        swap_chain_handle_ = value;
        return S_OK;
    }

private:
    IUnknown* swap_chain_ = nullptr;
    HANDLE swap_chain_handle_ = nullptr;
};

class ContentPresenterObject final : public XamlElement,
                                     public abi::NotImpl_IContentPresenter,
                                     public abi::NotImpl_IContentPresenter4 {
public:
    using PrimaryInterface = wuxc::IContentPresenter;
    ContentPresenterObject() { layout_.source = &content_; }
    ~ContentPresenterObject() override {
        if (background_) background_->Release();
    }
    openxaml::Element* Layout() override { return &layout_; }
    const wchar_t* RuntimeClassName() const override {
        return L"Windows.UI.Xaml.Controls.ContentPresenter";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IContentPresenter,
                        wuxc::IContentPresenter)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IContentPresenter4,
                        wuxc::IContentPresenter4)
        return QueryElementInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()
    HRESULT STDMETHODCALLTYPE get_Content(IInspectable** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (!content_.Count()) return S_OK;
        wux::IUIElement* child = nullptr;
        HRESULT hr = content_.GetAt(0, &child);
        if (FAILED(hr)) return hr;
        hr = child->QueryInterface(::openxaml::iid::IInspectable,
                                   reinterpret_cast<void**>(value));
        child->Release();
        return hr;
    }
    HRESULT STDMETHODCALLTYPE put_Content(IInspectable* value) override {
        HRESULT hr = content_.Clear();
        if (FAILED(hr) || !value) return hr;
        wux::IUIElement* child = nullptr;
        hr = value->QueryInterface(::openxaml::iid::Windows_UI_Xaml_IUIElement,
                                   reinterpret_cast<void**>(&child));
        if (FAILED(hr)) return E_INVALIDARG;
        hr = content_.Append(child);
        child->Release();
        return hr;
    }
    HRESULT STDMETHODCALLTYPE get_Background(wuxm::IBrush** value) override {
        if (!value) return E_POINTER;
        *value = background_;
        if (*value) (*value)->AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Background(wuxm::IBrush* value) override {
        if (value) value->AddRef();
        if (background_) background_->Release();
        background_ = value;
        return S_OK;
    }
private:
    ChildSourced<openxaml::ContentPresenter> layout_;
    wuxm::IBrush* background_ = nullptr;
    ChildCollection content_{{::openxaml::iid::PIID_FIVector_1_Windows__CUI__CXaml__CUIElement,
                              ::openxaml::iid::PIID_FIIterable_1_Windows__CUI__CXaml__CUIElement,
                              ::openxaml::iid::PIID_FIIterator_1_Windows__CUI__CXaml__CUIElement},
                             L"Windows.UI.Xaml.Controls.ContentPresenterContent", this};
};

class ImageObject final : public XamlElement, public abi::NotImpl_IImage {
public:
    using PrimaryInterface = wuxc::IImage;
    openxaml::Element* Layout() override { return &layout_; }
    const wchar_t* RuntimeClassName() const override { return L"Windows.UI.Xaml.Controls.Image"; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IImage, wuxc::IImage)
        return QueryElementInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()
private:
    openxaml::Image layout_;
};

class PathObject final : public XamlElement, public abi::NotImpl_IPath {
public:
    using PrimaryInterface = wuxs::IPath;
    openxaml::Element* Layout() override { return &layout_; }
    const wchar_t* RuntimeClassName() const override { return L"Windows.UI.Xaml.Shapes.Path"; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Shapes_IPath, wuxs::IPath)
        return QueryElementInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()
private:
    openxaml::Path layout_;
};

class PathIconObject final : public XamlElement, public abi::NotImpl_IPathIcon {
public:
    using PrimaryInterface = wuxc::IPathIcon;
    openxaml::Element* Layout() override { return &layout_; }
    const wchar_t* RuntimeClassName() const override { return L"Windows.UI.Xaml.Controls.PathIcon"; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IPathIcon, wuxc::IPathIcon)
        return QueryElementInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()
private:
    openxaml::PathIcon layout_;
};

// --- Grid definitions ---------------------------------------------------------

// One implementation for rows and columns. They differ only in which
// interface they answer to and which axis their names refer to; the stored
// state is the same Definition either way.
template <class InterfaceAbi, class StubBase>
class DefinitionObject : public ComObject,
                               public abi::NotImpl_IDependencyObject,
                               public StubBase,
                               public IOpenXamlDefinition {
public:
    using PrimaryInterface = InterfaceAbi;

    DefinitionObject(const GUID& iid, const wchar_t* name) : iid_(iid), name_(name) {}

    const wchar_t* RuntimeClassName() const override { return name_; }
    const openxaml::Definition* LayoutDefinition() override { return &definition_; }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(iid_, InterfaceAbi)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_IDependencyObject, wux::IDependencyObject)
        OPENXAML_QI_ARM(IID_IUnknown, InterfaceAbi)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable, InterfaceAbi)
        if (IsEqualGUID(iid, IID_IOpenXamlDefinition)) {
            auto* pointer = static_cast<IOpenXamlDefinition*>(this);
            static_cast<InterfaceAbi*>(this)->AddRef();
            *object = pointer;
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    OPENXAML_COM_BOILERPLATE()

    // A ColumnDefinition calls this Width and a RowDefinition calls it Height,
    // but it is the same value on the definition's own axis.
    HRESULT GetLength(wux::GridLength* value) {
        if (!value) return E_POINTER;
        const openxaml::GridLength& length = definition_.user_size;
        value->Value = length.value;
        switch (length.type) {
            case openxaml::GridUnitType::Auto: value->GridUnitType = wux::GridUnitType_Auto; break;
            case openxaml::GridUnitType::Pixel: value->GridUnitType = wux::GridUnitType_Pixel; break;
            default: value->GridUnitType = wux::GridUnitType_Star; break;
        }
        return S_OK;
    }
    HRESULT PutLength(wux::GridLength value) {
        switch (value.GridUnitType) {
            case wux::GridUnitType_Auto:
                definition_.user_size = {openxaml::GridUnitType::Auto, value.Value}; break;
            case wux::GridUnitType_Pixel:
                definition_.user_size = {openxaml::GridUnitType::Pixel, value.Value}; break;
            case wux::GridUnitType_Star:
                definition_.user_size = {openxaml::GridUnitType::Star, value.Value}; break;
            default:
                return E_INVALIDARG;
        }
        return S_OK;
    }
    HRESULT GetMin(DOUBLE* value) {
        if (!value) return E_POINTER;
        *value = definition_.user_min_size;
        return S_OK;
    }
    HRESULT PutMin(DOUBLE value) {
        definition_.user_min_size = value;
        return S_OK;
    }
    HRESULT GetMax(DOUBLE* value) {
        if (!value) return E_POINTER;
        *value = definition_.user_max_size;
        return S_OK;
    }
    HRESULT PutMax(DOUBLE value) {
        definition_.user_max_size = value;
        return S_OK;
    }

private:
    const GUID& iid_;
    const wchar_t* name_;
    openxaml::Definition definition_;
};

class ColumnDefinitionObject final
    : public DefinitionObject<wuxc::IColumnDefinition, abi::NotImpl_IColumnDefinition> {
public:
    ColumnDefinitionObject()
        : DefinitionObject(::openxaml::iid::Windows_UI_Xaml_Controls_IColumnDefinition,
                           L"Windows.UI.Xaml.Controls.ColumnDefinition") {}

    HRESULT STDMETHODCALLTYPE get_Width(wux::GridLength* value) override { return GetLength(value); }
    HRESULT STDMETHODCALLTYPE put_Width(wux::GridLength value) override { return PutLength(value); }
    HRESULT STDMETHODCALLTYPE get_MinWidth(DOUBLE* value) override { return GetMin(value); }
    HRESULT STDMETHODCALLTYPE put_MinWidth(DOUBLE value) override { return PutMin(value); }
    HRESULT STDMETHODCALLTYPE get_MaxWidth(DOUBLE* value) override { return GetMax(value); }
    HRESULT STDMETHODCALLTYPE put_MaxWidth(DOUBLE value) override { return PutMax(value); }
};

class RowDefinitionObject final
    : public DefinitionObject<wuxc::IRowDefinition, abi::NotImpl_IRowDefinition> {
public:
    RowDefinitionObject()
        : DefinitionObject(::openxaml::iid::Windows_UI_Xaml_Controls_IRowDefinition,
                           L"Windows.UI.Xaml.Controls.RowDefinition") {}

    HRESULT STDMETHODCALLTYPE get_Height(wux::GridLength* value) override { return GetLength(value); }
    HRESULT STDMETHODCALLTYPE put_Height(wux::GridLength value) override { return PutLength(value); }
    HRESULT STDMETHODCALLTYPE get_MinHeight(DOUBLE* value) override { return GetMin(value); }
    HRESULT STDMETHODCALLTYPE put_MinHeight(DOUBLE value) override { return PutMin(value); }
    HRESULT STDMETHODCALLTYPE get_MaxHeight(DOUBLE* value) override { return GetMax(value); }
    HRESULT STDMETHODCALLTYPE put_MaxHeight(DOUBLE value) override { return PutMax(value); }
};

// --- Grid ---------------------------------------------------------------------

class GridObject final : public PanelObject<AbiGrid>, public abi::NotImpl_IGrid {
public:
    const wchar_t* RuntimeClassName() const override {
        return L"Windows.UI.Xaml.Controls.Grid";
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IGrid, wuxc::IGrid)
        return QueryPanelInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE get_ColumnDefinitions(
        __FIVector_1_Windows__CUI__CXaml__CControls__CColumnDefinition** value) override {
        if (!value) return E_POINTER;
        columns_.AddRef();
        *value = &columns_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_RowDefinitions(
        __FIVector_1_Windows__CUI__CXaml__CControls__CRowDefinition** value) override {
        if (!value) return E_POINTER;
        rows_.AddRef();
        *value = &rows_;
        return S_OK;
    }

    // The definitions live in their own COM objects, which a caller can go on
    // mutating after adding them. Copying them in at the start of each pass is
    // what makes a late `put_Width` take effect, instead of the Grid holding a
    // snapshot from whenever the definition happened to be appended.
    HRESULT STDMETHODCALLTYPE Measure(wf::Size available) override {
        SyncDefinitions();
        return XamlElement::Measure(available);
    }
    HRESULT STDMETHODCALLTYPE Arrange(wf::Rect final_rect) override {
        SyncDefinitions();
        return XamlElement::Arrange(final_rect);
    }

private:
    void SyncDefinitions() {
        layout_.column_definitions.clear();
        for (const openxaml::Definition* definition : columns_.Projected())
            layout_.column_definitions.push_back(*definition);
        layout_.row_definitions.clear();
        for (const openxaml::Definition* definition : rows_.Projected())
            layout_.row_definitions.push_back(*definition);
    }

    ColumnCollection columns_{
        {::openxaml::iid::PIID_FIVector_1_Windows__CUI__CXaml__CControls__CColumnDefinition,
         ::openxaml::iid::PIID_FIIterable_1_Windows__CUI__CXaml__CControls__CColumnDefinition,
         ::openxaml::iid::PIID_FIIterator_1_Windows__CUI__CXaml__CControls__CColumnDefinition},
        L"Windows.UI.Xaml.Controls.ColumnDefinitionCollection", this};
    RowCollection rows_{
        {::openxaml::iid::PIID_FIVector_1_Windows__CUI__CXaml__CControls__CRowDefinition,
         ::openxaml::iid::PIID_FIIterable_1_Windows__CUI__CXaml__CControls__CRowDefinition,
         ::openxaml::iid::PIID_FIIterator_1_Windows__CUI__CXaml__CControls__CRowDefinition},
        L"Windows.UI.Xaml.Controls.RowDefinitionCollection", this};
};

// --- Wave-4 controls ----------------------------------------------------------

template <class LayoutType>
class ContentControlObjectBase : public XamlElement,
                                 public abi::NotImpl_IControl,
                                 public abi::NotImpl_IContentControl {
public:
    using PrimaryInterface = wuxc::IContentControl;

    ContentControlObjectBase() { layout_.source = &content_; }
    ~ContentControlObjectBase() override {
        if (background_) background_->Release();
        if (foreground_) foreground_->Release();
    }
    openxaml::Element* Layout() override { return &layout_; }

    HRESULT STDMETHODCALLTYPE get_Content(IInspectable** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (!content_.Count()) return S_OK;
        wux::IUIElement* child = nullptr;
        HRESULT hr = content_.GetAt(0, &child);
        if (FAILED(hr)) return hr;
        hr = child->QueryInterface(::openxaml::iid::IInspectable,
                                   reinterpret_cast<void**>(value));
        child->Release();
        return hr;
    }
    HRESULT STDMETHODCALLTYPE put_Content(IInspectable* value) override {
        HRESULT hr = content_.Clear();
        if (FAILED(hr) || !value) return hr;
        wux::IUIElement* child = nullptr;
        hr = value->QueryInterface(::openxaml::iid::Windows_UI_Xaml_IUIElement,
                                   reinterpret_cast<void**>(&child));
        if (FAILED(hr)) return E_INVALIDARG;
        hr = content_.Append(child);
        child->Release();
        return hr;
    }

    HRESULT STDMETHODCALLTYPE get_FontSize(DOUBLE* value) override {
        if (!value) return E_POINTER;
        *value = layout_.font_size();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_FontSize(DOUBLE value) override {
        layout_.set_font_size(value);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_FontFamily(wuxm::IFontFamily** value) override;
    HRESULT STDMETHODCALLTYPE put_FontFamily(wuxm::IFontFamily* value) override;
    HRESULT STDMETHODCALLTYPE get_Padding(wux::Thickness* value) override {
        if (!value) return E_POINTER;
        const openxaml::Thickness& padding = layout_.padding();
        *value = {padding.left, padding.top, padding.right, padding.bottom};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Padding(wux::Thickness value) override {
        layout_.set_padding({value.Left, value.Top, value.Right, value.Bottom});
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Background(wuxm::IBrush** value) override {
        return GetBrush(background_, value);
    }
    HRESULT STDMETHODCALLTYPE put_Background(wuxm::IBrush* value) override {
        return PutBrush(background_, value);
    }
    HRESULT STDMETHODCALLTYPE get_Foreground(wuxm::IBrush** value) override {
        return GetBrush(foreground_, value);
    }
    HRESULT STDMETHODCALLTYPE put_Foreground(wuxm::IBrush* value) override {
        return PutBrush(foreground_, value);
    }
    HRESULT STDMETHODCALLTYPE ApplyTemplate(boolean* result) override {
        if (!result) return E_POINTER;
        *result = layout_.ApplyTemplate() ? 1 : 0;
        return S_OK;
    }

protected:
    static HRESULT GetBrush(wuxm::IBrush* source, wuxm::IBrush** value) {
        if (!value) return E_POINTER;
        *value = source;
        if (*value) (*value)->AddRef();
        return S_OK;
    }
    static HRESULT PutBrush(wuxm::IBrush*& target, wuxm::IBrush* value) {
        if (value) value->AddRef();
        if (target) target->Release();
        target = value;
        return S_OK;
    }
    HRESULT QueryControlInterface(REFIID iid, void** object) {
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IContentControl,
                        wuxc::IContentControl)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IControl, wuxc::IControl)
        return QueryElementInterface(iid, object);
    }

    ChildSourced<LayoutType> layout_;
    wuxm::IBrush* background_ = nullptr;
    wuxm::IBrush* foreground_ = nullptr;
    ChildCollection content_{
        {::openxaml::iid::PIID_FIVector_1_Windows__CUI__CXaml__CUIElement,
         ::openxaml::iid::PIID_FIIterable_1_Windows__CUI__CXaml__CUIElement,
         ::openxaml::iid::PIID_FIIterator_1_Windows__CUI__CXaml__CUIElement},
        L"Windows.UI.Xaml.Controls.ContentControlContent", this};
};

class ContentControlObject final : public ContentControlObjectBase<openxaml::ContentControl> {
public:
    const wchar_t* RuntimeClassName() const override {
        return L"Windows.UI.Xaml.Controls.ContentControl";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        return QueryControlInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()
};

inline constexpr GUID IID_ContentDialogAsyncOperation = {
    0x1f23bdd1, 0x06dc, 0x5be9,
    {0x9a, 0x60, 0x0b, 0x4d, 0x94, 0xd4, 0xd7, 0x2c}};
inline constexpr GUID IID_OpenXamlAsyncInfo = {
    0x00000036, 0x0000, 0x0000,
    {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

// ContentDialog.ShowAsync is consumed directly by C++/WinRT's await adapter.
// A compatibility dialog has no interactive popup renderer yet, so it
// completes deterministically with None while still implementing both the
// typed operation and the IAsyncInfo contract expected of every WinRT async
// operation.
class ContentDialogAsyncOperation final
    : public ComObject,
      public __FIAsyncOperation_1_Windows__CUI__CXaml__CControls__CContentDialogResult,
      public IAsyncInfo {
public:
    using Handler =
        __FIAsyncOperationCompletedHandler_1_Windows__CUI__CXaml__CControls__CContentDialogResult;
    using Operation =
        __FIAsyncOperation_1_Windows__CUI__CXaml__CControls__CContentDialogResult;

    ~ContentDialogAsyncOperation() override {
        if (completed_) completed_->Release();
    }
    const wchar_t* RuntimeClassName() const override {
        return L"Windows.Foundation.IAsyncOperation`1<Windows.UI.Xaml.Controls.ContentDialogResult>";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(IID_ContentDialogAsyncOperation, Operation)
        OPENXAML_QI_ARM(IID_OpenXamlAsyncInfo, IAsyncInfo)
        OPENXAML_QI_ARM(IID_IUnknown, Operation)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable, Operation)
        *object = nullptr;
        return TraceQueryInterfaceMiss(RuntimeClassName(), iid);
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE put_Completed(Handler* handler) override {
        if (handler) handler->AddRef();
        if (completed_) completed_->Release();
        completed_ = handler;
        if (!handler) return S_OK;
        // Keep both participants alive while application code runs.  The
        // callback may release its last external reference to the operation.
        AddRef();
        handler->AddRef();
        const HRESULT hr = handler->Invoke(
            static_cast<Operation*>(this),
            ABI::Windows::Foundation::AsyncStatus::Completed);
        handler->Release();
        Release();
        return hr;
    }
    HRESULT STDMETHODCALLTYPE get_Completed(Handler** handler) override {
        if (!handler) return E_POINTER;
        *handler = completed_;
        if (*handler) (*handler)->AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetResults(wuxc::ContentDialogResult* result) override {
        if (!result) return E_POINTER;
        *result = wuxc::ContentDialogResult_None;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_Id(UINT32* id) override {
        if (!id) return E_POINTER;
        *id = 1;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Status(
        ABI::Windows::Foundation::AsyncStatus* status) override {
        if (!status) return E_POINTER;
        *status = ABI::Windows::Foundation::AsyncStatus::Completed;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_ErrorCode(HRESULT* error) override {
        if (!error) return E_POINTER;
        *error = S_OK;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Cancel() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Close() override {
        closed_ = true;
        return S_OK;
    }

private:
    Handler* completed_ = nullptr;
    bool closed_ = false;
};

class ContentDialogObject final
    : public ContentControlObjectBase<openxaml::ContentControl>,
      public abi::NotImpl_IContentDialog,
      public abi::NotImpl_IContentDialog2,
      public abi::NotImpl_IContentDialog3 {
public:
    using PrimaryInterface = wuxc::IContentDialog;
    ~ContentDialogObject() override {
        auto release = [](auto*& value) {
            if (value) value->Release();
            value = nullptr;
        };
        release(title_);
        release(title_template_);
        release(primary_command_);
        release(secondary_command_);
        release(primary_command_parameter_);
        release(secondary_command_parameter_);
        release(close_command_);
        release(close_command_parameter_);
        release(primary_button_style_);
        release(secondary_button_style_);
        release(close_button_style_);
        WindowsDeleteString(primary_button_text_);
        WindowsDeleteString(secondary_button_text_);
        WindowsDeleteString(close_button_text_);
    }
    const wchar_t* RuntimeClassName() const override {
        return L"Windows.UI.Xaml.Controls.ContentDialog";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IContentDialog,
                        wuxc::IContentDialog)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IContentDialog2,
                        wuxc::IContentDialog2)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IContentDialog3,
                        wuxc::IContentDialog3)
        return QueryControlInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE get_Title(IInspectable** value) override {
        return GetObject(title_, value);
    }
    HRESULT STDMETHODCALLTYPE put_Title(IInspectable* value) override {
        return PutObject(title_, value);
    }
    HRESULT STDMETHODCALLTYPE get_TitleTemplate(wux::IDataTemplate** value) override {
        return GetObject(title_template_, value);
    }
    HRESULT STDMETHODCALLTYPE put_TitleTemplate(wux::IDataTemplate* value) override {
        return PutObject(title_template_, value);
    }
    HRESULT STDMETHODCALLTYPE get_FullSizeDesired(boolean* value) override {
        if (!value) return E_POINTER;
        *value = full_size_desired_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_FullSizeDesired(boolean value) override {
        full_size_desired_ = value;
        return S_OK;
    }
#define OPENXAML_DIALOG_STRING_PROPERTY(name, field)                    \
    HRESULT STDMETHODCALLTYPE get_##name(HSTRING* value) override {     \
        if (!value) return E_POINTER;                                   \
        return WindowsDuplicateString(field, value);                    \
    }                                                                   \
    HRESULT STDMETHODCALLTYPE put_##name(HSTRING value) override {      \
        HSTRING next = nullptr;                                         \
        HRESULT hr = WindowsDuplicateString(value, &next);              \
        if (FAILED(hr)) return hr;                                      \
        WindowsDeleteString(field);                                     \
        field = next;                                                   \
        return S_OK;                                                    \
    }
    OPENXAML_DIALOG_STRING_PROPERTY(PrimaryButtonText, primary_button_text_)
    OPENXAML_DIALOG_STRING_PROPERTY(SecondaryButtonText, secondary_button_text_)
    OPENXAML_DIALOG_STRING_PROPERTY(CloseButtonText, close_button_text_)
#undef OPENXAML_DIALOG_STRING_PROPERTY

#define OPENXAML_DIALOG_OBJECT_PROPERTY(type, name, field)              \
    HRESULT STDMETHODCALLTYPE get_##name(type** value) override {       \
        return GetObject(field, value);                                 \
    }                                                                   \
    HRESULT STDMETHODCALLTYPE put_##name(type* value) override {        \
        return PutObject(field, value);                                 \
    }
    OPENXAML_DIALOG_OBJECT_PROPERTY(wuxi::ICommand, PrimaryButtonCommand,
                                    primary_command_)
    OPENXAML_DIALOG_OBJECT_PROPERTY(wuxi::ICommand, SecondaryButtonCommand,
                                    secondary_command_)
    OPENXAML_DIALOG_OBJECT_PROPERTY(IInspectable, PrimaryButtonCommandParameter,
                                    primary_command_parameter_)
    OPENXAML_DIALOG_OBJECT_PROPERTY(IInspectable, SecondaryButtonCommandParameter,
                                    secondary_command_parameter_)
    OPENXAML_DIALOG_OBJECT_PROPERTY(wuxi::ICommand, CloseButtonCommand,
                                    close_command_)
    OPENXAML_DIALOG_OBJECT_PROPERTY(IInspectable, CloseButtonCommandParameter,
                                    close_command_parameter_)
    OPENXAML_DIALOG_OBJECT_PROPERTY(wux::IStyle, PrimaryButtonStyle,
                                    primary_button_style_)
    OPENXAML_DIALOG_OBJECT_PROPERTY(wux::IStyle, SecondaryButtonStyle,
                                    secondary_button_style_)
    OPENXAML_DIALOG_OBJECT_PROPERTY(wux::IStyle, CloseButtonStyle,
                                    close_button_style_)
#undef OPENXAML_DIALOG_OBJECT_PROPERTY

    HRESULT STDMETHODCALLTYPE get_IsPrimaryButtonEnabled(boolean* value) override {
        if (!value) return E_POINTER;
        *value = primary_button_enabled_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_IsPrimaryButtonEnabled(boolean value) override {
        primary_button_enabled_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsSecondaryButtonEnabled(boolean* value) override {
        if (!value) return E_POINTER;
        *value = secondary_button_enabled_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_IsSecondaryButtonEnabled(boolean value) override {
        secondary_button_enabled_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_DefaultButton(wuxc::ContentDialogButton* value) override {
        if (!value) return E_POINTER;
        *value = default_button_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_DefaultButton(wuxc::ContentDialogButton value) override {
        default_button_ = value;
        return S_OK;
    }

#define OPENXAML_DIALOG_EVENT(name, handler_type)                       \
    HRESULT STDMETHODCALLTYPE add_##name(                               \
        handler_type* handler, EventRegistrationToken* token) override { \
        return AddEvent(handler, token);                                \
    }                                                                   \
    HRESULT STDMETHODCALLTYPE remove_##name(                            \
        EventRegistrationToken token) override {                        \
        return RemoveEvent(token);                                      \
    }
    OPENXAML_DIALOG_EVENT(
        Closing,
        __FITypedEventHandler_2_Windows__CUI__CXaml__CControls__CContentDialog_Windows__CUI__CXaml__CControls__CContentDialogClosingEventArgs)
    OPENXAML_DIALOG_EVENT(
        Closed,
        __FITypedEventHandler_2_Windows__CUI__CXaml__CControls__CContentDialog_Windows__CUI__CXaml__CControls__CContentDialogClosedEventArgs)
    OPENXAML_DIALOG_EVENT(
        Opened,
        __FITypedEventHandler_2_Windows__CUI__CXaml__CControls__CContentDialog_Windows__CUI__CXaml__CControls__CContentDialogOpenedEventArgs)
    OPENXAML_DIALOG_EVENT(
        PrimaryButtonClick,
        __FITypedEventHandler_2_Windows__CUI__CXaml__CControls__CContentDialog_Windows__CUI__CXaml__CControls__CContentDialogButtonClickEventArgs)
    OPENXAML_DIALOG_EVENT(
        SecondaryButtonClick,
        __FITypedEventHandler_2_Windows__CUI__CXaml__CControls__CContentDialog_Windows__CUI__CXaml__CControls__CContentDialogButtonClickEventArgs)
    OPENXAML_DIALOG_EVENT(
        CloseButtonClick,
        __FITypedEventHandler_2_Windows__CUI__CXaml__CControls__CContentDialog_Windows__CUI__CXaml__CControls__CContentDialogButtonClickEventArgs)
#undef OPENXAML_DIALOG_EVENT

    HRESULT STDMETHODCALLTYPE Hide() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ShowAsync(
        __FIAsyncOperation_1_Windows__CUI__CXaml__CControls__CContentDialogResult**
            operation) override {
        return CreateCompletedOperation(operation);
    }
    HRESULT STDMETHODCALLTYPE ShowAsyncWithPlacement(
        wuxc::ContentDialogPlacement,
        __FIAsyncOperation_1_Windows__CUI__CXaml__CControls__CContentDialogResult**
            operation) override {
        return CreateCompletedOperation(operation);
    }

private:
    template <class T> static HRESULT GetObject(T* source, T** value) {
        if (!value) return E_POINTER;
        *value = source;
        if (*value) (*value)->AddRef();
        return S_OK;
    }
    template <class T> static HRESULT PutObject(T*& target, T* value) {
        if (value) value->AddRef();
        if (target) target->Release();
        target = value;
        return S_OK;
    }
    static HRESULT CreateCompletedOperation(
        __FIAsyncOperation_1_Windows__CUI__CXaml__CControls__CContentDialogResult**
            operation) {
        if (!operation) return E_POINTER;
        *operation = new (std::nothrow) ContentDialogAsyncOperation();
        return *operation ? S_OK : E_OUTOFMEMORY;
    }

    IInspectable* title_ = nullptr;
    wux::IDataTemplate* title_template_ = nullptr;
    HSTRING primary_button_text_ = nullptr;
    HSTRING secondary_button_text_ = nullptr;
    HSTRING close_button_text_ = nullptr;
    wuxi::ICommand* primary_command_ = nullptr;
    wuxi::ICommand* secondary_command_ = nullptr;
    IInspectable* primary_command_parameter_ = nullptr;
    IInspectable* secondary_command_parameter_ = nullptr;
    wuxi::ICommand* close_command_ = nullptr;
    IInspectable* close_command_parameter_ = nullptr;
    wux::IStyle* primary_button_style_ = nullptr;
    wux::IStyle* secondary_button_style_ = nullptr;
    wux::IStyle* close_button_style_ = nullptr;
    boolean full_size_desired_ = 0;
    boolean primary_button_enabled_ = 1;
    boolean secondary_button_enabled_ = 1;
    wuxc::ContentDialogButton default_button_ = wuxc::ContentDialogButton_None;
};

// Microsoft.UI.Xaml's ABI is supplied by its WinMD rather than the Windows
// SDK headers used by this DLL. Keep the exact ITabView vtable here so the
// Terminal projection can use its default interface without importing the
// closed Microsoft.UI.Xaml implementation.
inline constexpr GUID IID_IMuxcTabView = {
    0x6aa787ab, 0x5a30, 0x5ea2, {0xbe, 0x5b, 0xae, 0xd8, 0x68, 0x38, 0x17, 0x56}};

struct IMuxcTabView : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_TabWidthMode(INT32*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_TabWidthMode(INT32) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_CloseButtonOverlayMode(INT32*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_CloseButtonOverlayMode(INT32) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_TabStripHeader(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_TabStripHeader(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_TabStripHeaderTemplate(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_TabStripHeaderTemplate(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_TabStripFooter(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_TabStripFooter(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_TabStripFooterTemplate(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_TabStripFooterTemplate(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsAddTabButtonVisible(boolean*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsAddTabButtonVisible(boolean) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AddTabButtonCommand(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_AddTabButtonCommand(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AddTabButtonCommandParameter(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_AddTabButtonCommandParameter(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_TabCloseRequested(void*, EventRegistrationToken*) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_TabCloseRequested(EventRegistrationToken) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_TabDroppedOutside(void*, EventRegistrationToken*) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_TabDroppedOutside(EventRegistrationToken) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_AddTabButtonClick(void*, EventRegistrationToken*) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_AddTabButtonClick(EventRegistrationToken) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_TabItemsChanged(void*, EventRegistrationToken*) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_TabItemsChanged(EventRegistrationToken) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_TabItemsSource(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_TabItemsSource(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_TabItems(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_TabItemTemplate(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_TabItemTemplate(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_TabItemTemplateSelector(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_TabItemTemplateSelector(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_CanDragTabs(boolean*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_CanDragTabs(boolean) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_CanReorderTabs(boolean*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_CanReorderTabs(boolean) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AllowDropTabs(boolean*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_AllowDropTabs(boolean) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_SelectedIndex(INT32*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_SelectedIndex(INT32) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_SelectedItem(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_SelectedItem(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE ContainerFromItem(void*, void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE ContainerFromIndex(INT32, void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_SelectionChanged(void*, EventRegistrationToken*) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_SelectionChanged(EventRegistrationToken) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_TabDragStarting(void*, EventRegistrationToken*) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_TabDragStarting(EventRegistrationToken) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_TabDragCompleted(void*, EventRegistrationToken*) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_TabDragCompleted(EventRegistrationToken) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_TabStripDragOver(void*, EventRegistrationToken*) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_TabStripDragOver(EventRegistrationToken) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_TabStripDrop(void*, EventRegistrationToken*) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_TabStripDrop(EventRegistrationToken) = 0;
};

using InspectableCollection = Vector<
    __FIVector_1_IInspectable, __FIIterable_1_IInspectable,
    __FIIterator_1_IInspectable, IInspectable, __FIVectorView_1_IInspectable>;

class TabViewObject final : public ContentControlObjectBase<openxaml::ContentControl>,
                            public IMuxcTabView {
public:
    using PrimaryInterface = IMuxcTabView;
    TabViewObject()
        : tab_items_({::openxaml::iid::PIID_FIVector_1_IInspectable,
                      ::openxaml::iid::PIID_FIIterable_1_IInspectable,
                      ::openxaml::iid::PIID_FIIterator_1_IInspectable},
                     L"Microsoft.UI.Xaml.Controls.TabView.TabItems", this) {}
    ~TabViewObject() override {
        for (auto* value : {tab_strip_header_, tab_strip_header_template_,
                            tab_strip_footer_, tab_strip_footer_template_,
                            add_tab_command_, add_tab_parameter_, tab_items_source_,
                            tab_item_template_, tab_item_template_selector_, selected_item_})
            if (value) value->Release();
    }
    const wchar_t* RuntimeClassName() const override {
        return L"Microsoft.UI.Xaml.Controls.TabView";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(IID_IMuxcTabView, IMuxcTabView)
        return QueryControlInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()

#define OPENXAML_MUXC_INT(name, field)                                      \
    HRESULT STDMETHODCALLTYPE get_##name(INT32* value) override {           \
        if (!value) return E_POINTER;                                        \
        *value = field;                                                      \
        return S_OK;                                                         \
    }                                                                        \
    HRESULT STDMETHODCALLTYPE put_##name(INT32 value) override {             \
        field = value; return S_OK;                                          \
    }
    OPENXAML_MUXC_INT(TabWidthMode, tab_width_mode_)
    OPENXAML_MUXC_INT(CloseButtonOverlayMode, close_button_overlay_mode_)
    OPENXAML_MUXC_INT(SelectedIndex, selected_index_)
#undef OPENXAML_MUXC_INT

#define OPENXAML_MUXC_BOOL(name, field)                                     \
    HRESULT STDMETHODCALLTYPE get_##name(boolean* value) override {         \
        if (!value) return E_POINTER;                                        \
        *value = field;                                                      \
        return S_OK;                                                         \
    }                                                                        \
    HRESULT STDMETHODCALLTYPE put_##name(boolean value) override {           \
        field = value != 0; return S_OK;                                     \
    }
    OPENXAML_MUXC_BOOL(IsAddTabButtonVisible, add_button_visible_)
    OPENXAML_MUXC_BOOL(CanDragTabs, can_drag_tabs_)
    OPENXAML_MUXC_BOOL(CanReorderTabs, can_reorder_tabs_)
    OPENXAML_MUXC_BOOL(AllowDropTabs, allow_drop_tabs_)
#undef OPENXAML_MUXC_BOOL

#define OPENXAML_MUXC_OBJECT(name, field)                                   \
    HRESULT STDMETHODCALLTYPE get_##name(void** value) override {           \
        return GetObject(field, value);                                      \
    }                                                                        \
    HRESULT STDMETHODCALLTYPE put_##name(void* value) override {             \
        return PutObject(field, static_cast<IInspectable*>(value));          \
    }
    OPENXAML_MUXC_OBJECT(TabStripHeader, tab_strip_header_)
    OPENXAML_MUXC_OBJECT(TabStripHeaderTemplate, tab_strip_header_template_)
    OPENXAML_MUXC_OBJECT(TabStripFooter, tab_strip_footer_)
    OPENXAML_MUXC_OBJECT(TabStripFooterTemplate, tab_strip_footer_template_)
    OPENXAML_MUXC_OBJECT(AddTabButtonCommand, add_tab_command_)
    OPENXAML_MUXC_OBJECT(AddTabButtonCommandParameter, add_tab_parameter_)
    OPENXAML_MUXC_OBJECT(TabItemsSource, tab_items_source_)
    OPENXAML_MUXC_OBJECT(TabItemTemplate, tab_item_template_)
    OPENXAML_MUXC_OBJECT(TabItemTemplateSelector, tab_item_template_selector_)
    OPENXAML_MUXC_OBJECT(SelectedItem, selected_item_)
#undef OPENXAML_MUXC_OBJECT

    HRESULT STDMETHODCALLTYPE get_TabItems(void** value) override {
        if (!value) return E_POINTER;
        tab_items_.AddRef();
        *value = static_cast<__FIVector_1_IInspectable*>(&tab_items_);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ContainerFromItem(void* item, void** value) override {
        if (!value) return E_POINTER;
        *value = item;
        if (item) static_cast<IUnknown*>(static_cast<IInspectable*>(item))->AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ContainerFromIndex(INT32 index, void** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (index < 0) return S_OK;
        IInspectable* item = nullptr;
        const HRESULT hr = tab_items_.GetAt(static_cast<UINT32>(index), &item);
        if (hr == E_BOUNDS) return S_OK;
        *value = item;
        return hr;
    }

#define OPENXAML_MUXC_EVENT(name)                                           \
    HRESULT STDMETHODCALLTYPE add_##name(void* handler,                     \
                                          EventRegistrationToken* token) override { \
        return AddEvent(static_cast<IUnknown*>(handler), token);             \
    }                                                                        \
    HRESULT STDMETHODCALLTYPE remove_##name(EventRegistrationToken token) override { \
        return RemoveEvent(token);                                           \
    }
    OPENXAML_MUXC_EVENT(TabCloseRequested)
    OPENXAML_MUXC_EVENT(TabDroppedOutside)
    OPENXAML_MUXC_EVENT(AddTabButtonClick)
    OPENXAML_MUXC_EVENT(TabItemsChanged)
    OPENXAML_MUXC_EVENT(SelectionChanged)
    OPENXAML_MUXC_EVENT(TabDragStarting)
    OPENXAML_MUXC_EVENT(TabDragCompleted)
    OPENXAML_MUXC_EVENT(TabStripDragOver)
    OPENXAML_MUXC_EVENT(TabStripDrop)
#undef OPENXAML_MUXC_EVENT

private:
    static HRESULT GetObject(IInspectable* object, void** value) {
        if (!value) return E_POINTER;
        *value = object;
        if (object) object->AddRef();
        return S_OK;
    }
    static HRESULT PutObject(IInspectable*& target, IInspectable* value) {
        if (value) value->AddRef();
        if (target) target->Release();
        target = value;
        return S_OK;
    }

    INT32 tab_width_mode_ = 0;
    INT32 close_button_overlay_mode_ = 0;
    INT32 selected_index_ = -1;
    boolean add_button_visible_ = 1;
    boolean can_drag_tabs_ = 0;
    boolean can_reorder_tabs_ = 0;
    boolean allow_drop_tabs_ = 0;
    IInspectable* tab_strip_header_ = nullptr;
    IInspectable* tab_strip_header_template_ = nullptr;
    IInspectable* tab_strip_footer_ = nullptr;
    IInspectable* tab_strip_footer_template_ = nullptr;
    IInspectable* add_tab_command_ = nullptr;
    IInspectable* add_tab_parameter_ = nullptr;
    IInspectable* tab_items_source_ = nullptr;
    IInspectable* tab_item_template_ = nullptr;
    IInspectable* tab_item_template_selector_ = nullptr;
    IInspectable* selected_item_ = nullptr;
    InspectableCollection tab_items_;
};

inline constexpr GUID IID_IMuxcTabViewItem = {
    0x291f3e98, 0x4f17, 0x5021, {0x94, 0xf0, 0x6a, 0x5b, 0x30, 0x43, 0x12, 0xb6}};
inline constexpr GUID IID_IMuxcTabViewItemFactory = {
    0xb64c2423, 0x7e56, 0x5d41, {0x8a, 0x84, 0x1e, 0xe2, 0x8f, 0x98, 0x26, 0xa4}};

struct IMuxcTabViewItem : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Header(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Header(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_HeaderTemplate(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_HeaderTemplate(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IconSource(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IconSource(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsClosable(boolean*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsClosable(boolean) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_TabViewTemplateSettings(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_CloseRequested(
        void*, EventRegistrationToken*) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_CloseRequested(
        EventRegistrationToken) = 0;
};

struct IMuxcTabViewItemFactory : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE CreateInstance(void*, void**, void**) = 0;
};

class TabViewItemObject final
    : public ContentControlObjectBase<openxaml::ContentControl>,
      public abi::NotImpl_ISelectorItem,
      public IMuxcTabViewItem {
public:
    using PrimaryInterface = IMuxcTabViewItem;
    ~TabViewItemObject() override {
        if (header_) header_->Release();
        if (header_template_) header_template_->Release();
        if (icon_source_) icon_source_->Release();
    }
    const wchar_t* RuntimeClassName() const override {
        return L"Microsoft.UI.Xaml.Controls.TabViewItem";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(IID_IMuxcTabViewItem, IMuxcTabViewItem)
        OPENXAML_QI_ARM(
            ::openxaml::iid::Windows_UI_Xaml_Controls_Primitives_ISelectorItem,
            wuxcp::ISelectorItem)
        return QueryControlInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()

#define OPENXAML_TAB_ITEM_OBJECT(name, field)                               \
    HRESULT STDMETHODCALLTYPE get_##name(void** value) override {           \
        if (!value) return E_POINTER;                                        \
        *value = field;                                                      \
        if (field) field->AddRef();                                          \
        return S_OK;                                                         \
    }                                                                        \
    HRESULT STDMETHODCALLTYPE put_##name(void* value) override {             \
        auto* next = static_cast<IInspectable*>(value);                      \
        if (next) next->AddRef();                                            \
        if (field) field->Release();                                         \
        field = next;                                                        \
        return S_OK;                                                         \
    }
    OPENXAML_TAB_ITEM_OBJECT(Header, header_)
    OPENXAML_TAB_ITEM_OBJECT(HeaderTemplate, header_template_)
    OPENXAML_TAB_ITEM_OBJECT(IconSource, icon_source_)
#undef OPENXAML_TAB_ITEM_OBJECT

    HRESULT STDMETHODCALLTYPE get_IsClosable(boolean* value) override {
        if (!value) return E_POINTER;
        *value = is_closable_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_IsClosable(boolean value) override {
        is_closable_ = value != 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsSelected(boolean* value) override {
        if (!value) return E_POINTER;
        *value = is_selected_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_IsSelected(boolean value) override {
        is_selected_ = value != 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_TabViewTemplateSettings(void** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE add_CloseRequested(
        void* handler, EventRegistrationToken* token) override {
        return AddEvent(static_cast<IUnknown*>(handler), token);
    }
    HRESULT STDMETHODCALLTYPE remove_CloseRequested(
        EventRegistrationToken token) override {
        return RemoveEvent(token);
    }

private:
    IInspectable* header_ = nullptr;
    IInspectable* header_template_ = nullptr;
    IInspectable* icon_source_ = nullptr;
    boolean is_closable_ = 1;
    boolean is_selected_ = 0;
};

inline constexpr GUID IID_IMuxcSplitButton = {
    0x8b09006a, 0x6241, 0x594f, {0x93, 0xe4, 0x8b, 0xf0, 0x51, 0xd7, 0xba, 0x8f}};

struct IMuxcSplitButton : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Flyout(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Flyout(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Command(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Command(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_CommandParameter(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_CommandParameter(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_Click(void*, EventRegistrationToken*) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_Click(EventRegistrationToken) = 0;
};

class SplitButtonObject final
    : public ContentControlObjectBase<openxaml::ContentControl>,
      public IMuxcSplitButton {
public:
    using PrimaryInterface = IMuxcSplitButton;
    ~SplitButtonObject() override {
        if (flyout_) flyout_->Release();
        if (command_) command_->Release();
        if (command_parameter_) command_parameter_->Release();
    }
    const wchar_t* RuntimeClassName() const override {
        return L"Microsoft.UI.Xaml.Controls.SplitButton";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(IID_IMuxcSplitButton, IMuxcSplitButton)
        return QueryControlInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()

#define OPENXAML_SPLIT_OBJECT(name, field)                                  \
    HRESULT STDMETHODCALLTYPE get_##name(void** value) override {           \
        if (!value) return E_POINTER;                                        \
        *value = field;                                                      \
        if (field) field->AddRef();                                          \
        return S_OK;                                                         \
    }                                                                        \
    HRESULT STDMETHODCALLTYPE put_##name(void* value) override {             \
        auto* next = static_cast<IInspectable*>(value);                      \
        if (next) next->AddRef();                                            \
        if (field) field->Release();                                         \
        field = next;                                                        \
        return S_OK;                                                         \
    }
    OPENXAML_SPLIT_OBJECT(Flyout, flyout_)
    OPENXAML_SPLIT_OBJECT(Command, command_)
    OPENXAML_SPLIT_OBJECT(CommandParameter, command_parameter_)
#undef OPENXAML_SPLIT_OBJECT

    HRESULT STDMETHODCALLTYPE add_Click(void* handler,
                                         EventRegistrationToken* token) override {
        return AddEvent(static_cast<IUnknown*>(handler), token);
    }
    HRESULT STDMETHODCALLTYPE remove_Click(EventRegistrationToken token) override {
        return RemoveEvent(token);
    }

private:
    IInspectable* flyout_ = nullptr;
    IInspectable* command_ = nullptr;
    IInspectable* command_parameter_ = nullptr;
};

inline constexpr GUID IID_IMuxcProgressRing = {
    0x2670d03f, 0xe28c, 0x5652, {0xbe, 0xe2, 0xb5, 0x21, 0x2e, 0xbd, 0xf7, 0xff}};
inline constexpr GUID IID_IMuxcProgressRingFactory = {
    0x092fa98c, 0x62a7, 0x5dbc, {0x9a, 0x85, 0x3e, 0x55, 0x6b, 0xa8, 0x1f, 0x79}};

struct IMuxcProgressRing : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_IsActive(boolean*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsActive(boolean) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsIndeterminate(boolean*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsIndeterminate(boolean) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_TemplateSettings(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Value(DOUBLE*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Value(DOUBLE) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Minimum(DOUBLE*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Minimum(DOUBLE) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Maximum(DOUBLE*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Maximum(DOUBLE) = 0;
};
struct IMuxcProgressRingFactory : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE CreateInstance(void*, void**, void**) = 0;
};

class ProgressRingObject final
    : public ContentControlObjectBase<openxaml::ContentControl>,
      public IMuxcProgressRing {
public:
    using PrimaryInterface = IMuxcProgressRing;
    const wchar_t* RuntimeClassName() const override {
        return L"Microsoft.UI.Xaml.Controls.ProgressRing";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(IID_IMuxcProgressRing, IMuxcProgressRing)
        return QueryControlInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()

#define OPENXAML_PROGRESS_BOOL(name, field)                                  \
    HRESULT STDMETHODCALLTYPE get_##name(boolean* value) override {          \
        if (!value) return E_POINTER;                                         \
        *value = field;                                                       \
        return S_OK;                                                          \
    }                                                                         \
    HRESULT STDMETHODCALLTYPE put_##name(boolean value) override {            \
        field = value != 0; return S_OK;                                       \
    }
    OPENXAML_PROGRESS_BOOL(IsActive, active_)
    OPENXAML_PROGRESS_BOOL(IsIndeterminate, indeterminate_)
#undef OPENXAML_PROGRESS_BOOL

#define OPENXAML_PROGRESS_DOUBLE(name, field)                                \
    HRESULT STDMETHODCALLTYPE get_##name(DOUBLE* value) override {           \
        if (!value) return E_POINTER;                                         \
        *value = field;                                                       \
        return S_OK;                                                          \
    }                                                                         \
    HRESULT STDMETHODCALLTYPE put_##name(DOUBLE value) override {             \
        field = value; return S_OK;                                            \
    }
    OPENXAML_PROGRESS_DOUBLE(Value, value_)
    OPENXAML_PROGRESS_DOUBLE(Minimum, minimum_)
    OPENXAML_PROGRESS_DOUBLE(Maximum, maximum_)
#undef OPENXAML_PROGRESS_DOUBLE

    HRESULT STDMETHODCALLTYPE get_TemplateSettings(void** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return S_OK;
    }

private:
    boolean active_ = 0;
    boolean indeterminate_ = 1;
    DOUBLE value_ = 0.0;
    DOUBLE minimum_ = 0.0;
    DOUBLE maximum_ = 100.0;
};

class UserControlObject final : public ContentControlObjectBase<openxaml::ContentControl>,
                                public abi::NotImpl_IUserControl {
public:
    using PrimaryInterface = wuxc::IUserControl;
    const wchar_t* RuntimeClassName() const override {
        return L"Windows.UI.Xaml.Controls.UserControl";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IUserControl,
                        wuxc::IUserControl)
        return QueryControlInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()
};

class PageObject final : public ContentControlObjectBase<openxaml::Page>,
                         public abi::NotImpl_IPage {
public:
    using PrimaryInterface = wuxc::IPage;
    const wchar_t* RuntimeClassName() const override { return L"Windows.UI.Xaml.Controls.Page"; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IPage, wuxc::IPage)
        return QueryControlInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()
};

class FrameObject final : public ContentControlObjectBase<openxaml::Frame>,
                          public abi::NotImpl_IFrame {
public:
    using PrimaryInterface = wuxc::IFrame;
    const wchar_t* RuntimeClassName() const override { return L"Windows.UI.Xaml.Controls.Frame"; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IFrame, wuxc::IFrame)
        return QueryControlInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE get_CanGoBack(boolean* value) override {
        if (!value) return E_POINTER;
        *value = layout_.CanGoBack() ? 1 : 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_CanGoForward(boolean* value) override {
        if (!value) return E_POINTER;
        *value = layout_.CanGoForward() ? 1 : 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_BackStackDepth(INT32* value) override {
        if (!value) return E_POINTER;
        *value = static_cast<INT32>(layout_.BackStackDepth());
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GoBack() override {
        return layout_.GoBack() ? S_OK : E_BOUNDS;
    }
    HRESULT STDMETHODCALLTYPE GoForward() override {
        return layout_.GoForward() ? S_OK : E_BOUNDS;
    }
};

struct NoAdditionalInterface {};

template <class LayoutType, class InterfaceType, class StubType>
class ItemsControlObjectBase : public XamlElement,
                               public abi::NotImpl_IControl,
                               public abi::NotImpl_IItemsControl,
                               public StubType {
public:
    using PrimaryInterface = InterfaceType;
    openxaml::Element* Layout() override { return &layout_; }

protected:
    HRESULT QueryItemsInterface(REFIID iid, void** object, const GUID& own_iid) {
        OPENXAML_QI_ARM(own_iid, InterfaceType)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IItemsControl,
                        wuxc::IItemsControl)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IControl, wuxc::IControl)
        return QueryElementInterface(iid, object);
    }
    LayoutType layout_;
};

class ItemsControlObject final
    : public ItemsControlObjectBase<openxaml::ItemsControl, wuxc::IItemsControl,
                                    NoAdditionalInterface> {
public:
    const wchar_t* RuntimeClassName() const override {
        return L"Windows.UI.Xaml.Controls.ItemsControl";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        return QueryItemsInterface(iid, object,
            ::openxaml::iid::Windows_UI_Xaml_Controls_IItemsControl);
    }
    OPENXAML_COM_BOILERPLATE()
};

class ListViewObject final
    : public ItemsControlObjectBase<openxaml::ListView, wuxc::IListView,
                                    abi::NotImpl_IListView>,
      public abi::NotImpl_IListViewBase {
public:
    const wchar_t* RuntimeClassName() const override { return L"Windows.UI.Xaml.Controls.ListView"; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IListViewBase,
                        wuxc::IListViewBase)
        return QueryItemsInterface(iid, object,
            ::openxaml::iid::Windows_UI_Xaml_Controls_IListView);
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE get_SelectionMode(wuxc::ListViewSelectionMode* value) override {
        if (!value) return E_POINTER;
        switch (layout_.selection_mode()) {
            case openxaml::SelectionMode::None: *value = wuxc::ListViewSelectionMode_None; break;
            case openxaml::SelectionMode::Multiple: *value = wuxc::ListViewSelectionMode_Multiple; break;
            case openxaml::SelectionMode::Extended: *value = wuxc::ListViewSelectionMode_Extended; break;
            default: *value = wuxc::ListViewSelectionMode_Single; break;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_SelectionMode(wuxc::ListViewSelectionMode value) override {
        switch (value) {
            case wuxc::ListViewSelectionMode_None:
                layout_.set_selection_mode(openxaml::SelectionMode::None); break;
            case wuxc::ListViewSelectionMode_Single:
                layout_.set_selection_mode(openxaml::SelectionMode::Single); break;
            case wuxc::ListViewSelectionMode_Multiple:
                layout_.set_selection_mode(openxaml::SelectionMode::Multiple); break;
            case wuxc::ListViewSelectionMode_Extended:
                layout_.set_selection_mode(openxaml::SelectionMode::Extended); break;
            default: return E_INVALIDARG;
        }
        return S_OK;
    }
};

class PopupObject final : public XamlElement, public abi::NotImpl_IPopup {
public:
    using PrimaryInterface = wuxcp::IPopup;
    PopupObject() { layout_.source = &child_; }
    openxaml::Element* Layout() override { return &layout_; }
    const wchar_t* RuntimeClassName() const override {
        return L"Windows.UI.Xaml.Controls.Primitives.Popup";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_Primitives_IPopup,
                        wuxcp::IPopup)
        return QueryElementInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE get_Child(wux::IUIElement** value) override {
        if (!value) return E_POINTER;
        if (!child_.Count()) { *value = nullptr; return S_OK; }
        return child_.GetAt(0, value);
    }
    HRESULT STDMETHODCALLTYPE put_Child(wux::IUIElement* value) override {
        HRESULT hr = child_.Clear();
        if (FAILED(hr) || !value) return hr;
        return child_.Append(value);
    }
    HRESULT STDMETHODCALLTYPE get_IsOpen(boolean* value) override {
        if (!value) return E_POINTER;
        *value = layout_.is_open() ? 1 : 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_IsOpen(boolean value) override {
        layout_.set_is_open(value != 0);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_HorizontalOffset(DOUBLE* value) override {
        if (!value) return E_POINTER;
        *value = horizontal_offset_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_HorizontalOffset(DOUBLE value) override {
        horizontal_offset_ = value; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_VerticalOffset(DOUBLE* value) override {
        if (!value) return E_POINTER;
        *value = vertical_offset_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_VerticalOffset(DOUBLE value) override {
        vertical_offset_ = value; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsLightDismissEnabled(boolean* value) override {
        if (!value) return E_POINTER;
        *value = layout_.is_light_dismiss_enabled() ? 1 : 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_IsLightDismissEnabled(boolean value) override {
        layout_.set_is_light_dismiss_enabled(value != 0);
        return S_OK;
    }

private:
    ChildSourced<openxaml::Popup> layout_;
    ChildCollection child_{
        {::openxaml::iid::PIID_FIVector_1_Windows__CUI__CXaml__CUIElement,
         ::openxaml::iid::PIID_FIIterable_1_Windows__CUI__CXaml__CUIElement,
         ::openxaml::iid::PIID_FIIterator_1_Windows__CUI__CXaml__CUIElement},
        L"Windows.UI.Xaml.Controls.Primitives.PopupChild", this};
    double horizontal_offset_ = 0.0;
    double vertical_offset_ = 0.0;
};

class MenuFlyoutObject : public ComObject,
                               public abi::NotImpl_IDependencyObject,
                               public abi::NotImpl_IMenuFlyout,
                               public abi::NotImpl_IFlyoutBase,
                               public abi::NotImpl_IFlyoutBase2,
                               public abi::NotImpl_IFlyoutBase3,
                               public abi::NotImpl_IFlyoutBase4,
                               public abi::NotImpl_IFlyoutBase5,
                               public abi::NotImpl_IFlyoutBase6 {
public:
    using PrimaryInterface = wuxc::IMenuFlyout;
    ~MenuFlyoutObject() override {
        if (presenter_style_) presenter_style_->Release();
        if (target_) target_->Release();
        if (overlay_input_pass_through_) overlay_input_pass_through_->Release();
        if (xaml_root_) xaml_root_->Release();
        for (auto& [_, handler] : handlers_) handler->Release();
    }
    const wchar_t* RuntimeClassName() const override {
        return L"Windows.UI.Xaml.Controls.MenuFlyout";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IMenuFlyout,
                        wuxc::IMenuFlyout)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_Primitives_IFlyoutBase,
                        wuxcp::IFlyoutBase)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_Primitives_IFlyoutBase2,
                        wuxcp::IFlyoutBase2)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_Primitives_IFlyoutBase3,
                        wuxcp::IFlyoutBase3)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_Primitives_IFlyoutBase4,
                        wuxcp::IFlyoutBase4)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_Primitives_IFlyoutBase5,
                        wuxcp::IFlyoutBase5)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_Primitives_IFlyoutBase6,
                        wuxcp::IFlyoutBase6)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_IDependencyObject,
                        wux::IDependencyObject)
        OPENXAML_QI_ARM(IID_IUnknown, wuxc::IMenuFlyout)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable, wuxc::IMenuFlyout)
        *object = nullptr;
        return TraceQueryInterfaceMiss(RuntimeClassName(), iid);
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE get_Items(
        __FIVector_1_Windows__CUI__CXaml__CControls__CMenuFlyoutItemBase** value) override {
        if (!value) return E_POINTER;
        *value = static_cast<
            __FIVector_1_Windows__CUI__CXaml__CControls__CMenuFlyoutItemBase*>(&items_);
        (*value)->AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_MenuFlyoutPresenterStyle(wux::IStyle** value) override {
        if (!value) return E_POINTER;
        *value = presenter_style_;
        if (*value) (*value)->AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_MenuFlyoutPresenterStyle(wux::IStyle* value) override {
        if (value) value->AddRef();
        if (presenter_style_) presenter_style_->Release();
        presenter_style_ = value;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_Placement(wuxcp::FlyoutPlacementMode* value) override {
        if (!value) return E_POINTER;
        *value = placement_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Placement(wuxcp::FlyoutPlacementMode value) override {
        placement_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE add_Opened(__FIEventHandler_1_IInspectable* handler,
                                          EventRegistrationToken* token) override {
        return Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_Opened(EventRegistrationToken token) override {
        return Remove(token);
    }
    HRESULT STDMETHODCALLTYPE add_Closed(__FIEventHandler_1_IInspectable* handler,
                                          EventRegistrationToken* token) override {
        return Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_Closed(EventRegistrationToken token) override {
        return Remove(token);
    }
    HRESULT STDMETHODCALLTYPE add_Opening(__FIEventHandler_1_IInspectable* handler,
                                           EventRegistrationToken* token) override {
        return Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_Opening(EventRegistrationToken token) override {
        return Remove(token);
    }
    HRESULT STDMETHODCALLTYPE ShowAt(wux::IFrameworkElement* target) override {
        if (target) target->AddRef();
        if (target_) target_->Release();
        target_ = target;
        open_ = true;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Hide() override {
        open_ = false;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_Target(wux::IFrameworkElement** value) override {
        if (!value) return E_POINTER;
        *value = target_;
        if (*value) (*value)->AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_AllowFocusOnInteraction(boolean* value) override {
        if (!value) return E_POINTER;
        *value = allow_focus_on_interaction_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_AllowFocusOnInteraction(boolean value) override {
        allow_focus_on_interaction_ = value != 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_LightDismissOverlayMode(
        wuxc::LightDismissOverlayMode* value) override {
        if (!value) return E_POINTER;
        *value = light_dismiss_overlay_mode_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_LightDismissOverlayMode(
        wuxc::LightDismissOverlayMode value) override {
        light_dismiss_overlay_mode_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_AllowFocusWhenDisabled(boolean* value) override {
        if (!value) return E_POINTER;
        *value = allow_focus_when_disabled_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_AllowFocusWhenDisabled(boolean value) override {
        allow_focus_when_disabled_ = value != 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_ElementSoundMode(wux::ElementSoundMode* value) override {
        if (!value) return E_POINTER;
        *value = element_sound_mode_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_ElementSoundMode(wux::ElementSoundMode value) override {
        element_sound_mode_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE add_Closing(
        __FITypedEventHandler_2_Windows__CUI__CXaml__CControls__CPrimitives__CFlyoutBase_Windows__CUI__CXaml__CControls__CPrimitives__CFlyoutBaseClosingEventArgs* handler,
        EventRegistrationToken* token) override {
        return Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_Closing(EventRegistrationToken token) override {
        return Remove(token);
    }

    HRESULT STDMETHODCALLTYPE get_OverlayInputPassThroughElement(
        wux::IDependencyObject** value) override {
        if (!value) return E_POINTER;
        *value = overlay_input_pass_through_;
        if (*value) (*value)->AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_OverlayInputPassThroughElement(
        wux::IDependencyObject* value) override {
        if (value) value->AddRef();
        if (overlay_input_pass_through_) overlay_input_pass_through_->Release();
        overlay_input_pass_through_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE TryInvokeKeyboardAccelerator(
        wuxi::IProcessKeyboardAcceleratorEventArgs*) override {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_ShowMode(wuxcp::FlyoutShowMode* value) override {
        if (!value) return E_POINTER;
        *value = show_mode_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_ShowMode(wuxcp::FlyoutShowMode value) override {
        show_mode_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_InputDevicePrefersPrimaryCommands(boolean* value) override {
        if (!value) return E_POINTER;
        *value = 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_AreOpenCloseAnimationsEnabled(boolean* value) override {
        if (!value) return E_POINTER;
        *value = animations_enabled_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_AreOpenCloseAnimationsEnabled(boolean value) override {
        animations_enabled_ = value != 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsOpen(boolean* value) override {
        if (!value) return E_POINTER;
        *value = open_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ShowAt(wux::IDependencyObject*,
                                      wuxcp::IFlyoutShowOptions*) override {
        open_ = true;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_ShouldConstrainToRootBounds(boolean* value) override {
        if (!value) return E_POINTER;
        *value = should_constrain_to_root_bounds_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_ShouldConstrainToRootBounds(boolean value) override {
        should_constrain_to_root_bounds_ = value != 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsConstrainedToRootBounds(boolean* value) override {
        if (!value) return E_POINTER;
        *value = should_constrain_to_root_bounds_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_XamlRoot(wux::IXamlRoot** value) override {
        if (!value) return E_POINTER;
        *value = xaml_root_;
        if (*value) (*value)->AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_XamlRoot(wux::IXamlRoot* value) override {
        if (value) value->AddRef();
        if (xaml_root_) xaml_root_->Release();
        xaml_root_ = value;
        return S_OK;
    }

private:
    MenuFlyoutItemCollection items_{
        {::openxaml::iid::PIID_FIVector_1_Windows__CUI__CXaml__CControls__CMenuFlyoutItemBase,
         ::openxaml::iid::PIID_FIIterable_1_Windows__CUI__CXaml__CControls__CMenuFlyoutItemBase,
         ::openxaml::iid::PIID_FIIterator_1_Windows__CUI__CXaml__CControls__CMenuFlyoutItemBase},
        L"Windows.UI.Xaml.Controls.MenuFlyoutItemCollection", this};
    HRESULT Add(IUnknown* handler, EventRegistrationToken* token) {
        if (!handler || !token) return E_INVALIDARG;
        token->value = ++next_token_;
        handler->AddRef();
        handlers_[token->value] = handler;
        return S_OK;
    }
    HRESULT Remove(EventRegistrationToken token) {
        const auto found = handlers_.find(token.value);
        if (found == handlers_.end()) return S_OK;
        found->second->Release();
        handlers_.erase(found);
        return S_OK;
    }

    wux::IStyle* presenter_style_ = nullptr;
    wux::IFrameworkElement* target_ = nullptr;
    wux::IDependencyObject* overlay_input_pass_through_ = nullptr;
    wux::IXamlRoot* xaml_root_ = nullptr;
    wuxcp::FlyoutPlacementMode placement_ = wuxcp::FlyoutPlacementMode_Top;
    wuxcp::FlyoutShowMode show_mode_ = wuxcp::FlyoutShowMode_Auto;
    wuxc::LightDismissOverlayMode light_dismiss_overlay_mode_ =
        wuxc::LightDismissOverlayMode_Auto;
    wux::ElementSoundMode element_sound_mode_ = wux::ElementSoundMode_Default;
    boolean allow_focus_on_interaction_ = 1;
    boolean allow_focus_when_disabled_ = 0;
    boolean should_constrain_to_root_bounds_ = 1;
    boolean animations_enabled_ = 1;
    boolean open_ = 0;
    LONGLONG next_token_ = 0;
    std::map<LONGLONG, IUnknown*> handlers_;
};

// CommandBarFlyout is shipped by Microsoft.UI.Xaml and therefore is absent
// from the Windows SDK ABI namespace used to build this DLL. These are the
// exact WinUI 2.8 interface layouts and IIDs consumed by Terminal.
inline constexpr GUID IID_IMuxcCommandBarFlyout = {
    0xf8f5b8bc, 0x8d67, 0x5fa9, {0x8f, 0xb0, 0xc2, 0xc3, 0x31, 0x1e, 0x1b, 0x7c}};
inline constexpr GUID IID_IMuxcCommandBarFlyout2 = {
    0x5f81ec9e, 0xa9d2, 0x5f04, {0xb5, 0xb1, 0x01, 0x3d, 0xae, 0xf0, 0x26, 0xdd}};
inline constexpr GUID IID_IMuxcCommandBarFlyoutFactory = {
    0xa194dbe6, 0x4311, 0x5bd2, {0xa8, 0xeb, 0xb4, 0x9c, 0x47, 0x33, 0xa3, 0x3c}};

struct IMuxcCommandBarFlyout : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_PrimaryCommands(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_SecondaryCommands(void**) = 0;
};
struct IMuxcCommandBarFlyout2 : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_AlwaysExpanded(boolean*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_AlwaysExpanded(boolean) = 0;
};
struct IMuxcCommandBarFlyoutFactory : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE CreateInstance(void*, void**, void**) = 0;
};

class CommandBarFlyoutObject final : public MenuFlyoutObject,
                                      public IMuxcCommandBarFlyout,
                                      public IMuxcCommandBarFlyout2 {
public:
    using PrimaryInterface = IMuxcCommandBarFlyout;
    CommandBarFlyoutObject()
        : primary_({::openxaml::iid::PIID_FIVector_1_Windows__CUI__CXaml__CControls__CICommandBarElement,
                    ::openxaml::iid::PIID_FIIterable_1_Windows__CUI__CXaml__CControls__CICommandBarElement,
                    ::openxaml::iid::PIID_FIIterator_1_Windows__CUI__CXaml__CControls__CICommandBarElement},
                   ::openxaml::iid::PIID_FIObservableVector_1_Windows__CUI__CXaml__CControls__CICommandBarElement,
                   L"Microsoft.UI.Xaml.Controls.CommandBarFlyout.PrimaryCommands", this),
          secondary_({::openxaml::iid::PIID_FIVector_1_Windows__CUI__CXaml__CControls__CICommandBarElement,
                      ::openxaml::iid::PIID_FIIterable_1_Windows__CUI__CXaml__CControls__CICommandBarElement,
                      ::openxaml::iid::PIID_FIIterator_1_Windows__CUI__CXaml__CControls__CICommandBarElement},
                     ::openxaml::iid::PIID_FIObservableVector_1_Windows__CUI__CXaml__CControls__CICommandBarElement,
                     L"Microsoft.UI.Xaml.Controls.CommandBarFlyout.SecondaryCommands", this) {}

    const wchar_t* RuntimeClassName() const override {
        return L"Microsoft.UI.Xaml.Controls.CommandBarFlyout";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(IID_IMuxcCommandBarFlyout, IMuxcCommandBarFlyout)
        OPENXAML_QI_ARM(IID_IMuxcCommandBarFlyout2, IMuxcCommandBarFlyout2)
        if (IsEqualGUID(iid, IID_IUnknown) ||
            IsEqualGUID(iid, ::openxaml::iid::IInspectable)) {
            auto* pointer = static_cast<IMuxcCommandBarFlyout*>(this);
            pointer->AddRef();
            *object = pointer;
            return S_OK;
        }
        return MenuFlyoutObject::QueryInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE get_PrimaryCommands(void** value) override {
        return GetCommands(primary_, value);
    }
    HRESULT STDMETHODCALLTYPE get_SecondaryCommands(void** value) override {
        return GetCommands(secondary_, value);
    }
    HRESULT STDMETHODCALLTYPE get_AlwaysExpanded(boolean* value) override {
        if (!value) return E_POINTER;
        *value = always_expanded_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_AlwaysExpanded(boolean value) override {
        always_expanded_ = value != 0;
        return S_OK;
    }
    HRESULT AppendCommand(IInspectable* child, bool secondary) {
        if (!child) return E_INVALIDARG;
        wuxc::ICommandBarElement* element = nullptr;
        HRESULT hr = child->QueryInterface(
            ::openxaml::iid::Windows_UI_Xaml_Controls_ICommandBarElement,
            reinterpret_cast<void**>(&element));
        if (SUCCEEDED(hr)) hr = (secondary ? secondary_ : primary_).Append(element);
        if (element) element->Release();
        return hr;
    }

private:
    static HRESULT GetCommands(CommandBarElementCollection& commands, void** value) {
        if (!value) return E_POINTER;
        auto* projected = static_cast<
            __FIObservableVector_1_Windows__CUI__CXaml__CControls__CICommandBarElement*>(
                &commands);
        projected->AddRef();
        *value = projected;
        return S_OK;
    }

    CommandBarElementCollection primary_;
    CommandBarElementCollection secondary_;
    boolean always_expanded_ = 0;
};

// --- remaining Terminal-facing Windows.UI.Xaml controls ----------------------

class BitmapIconSourceObject final
    : public ComObject,
      public abi::NotImpl_IDependencyObject,
      public abi::NotImpl_IIconSource,
      public abi::NotImpl_IBitmapIconSource {
public:
    using PrimaryInterface = wuxc::IBitmapIconSource;
    ~BitmapIconSourceObject() override {
        if (uri_) uri_->Release();
        if (foreground_) foreground_->Release();
    }
    const wchar_t* RuntimeClassName() const override {
        return L"Windows.UI.Xaml.Controls.BitmapIconSource";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IBitmapIconSource,
                        wuxc::IBitmapIconSource)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IIconSource,
                        wuxc::IIconSource)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_IDependencyObject,
                        wux::IDependencyObject)
        OPENXAML_QI_ARM(IID_IUnknown, wuxc::IBitmapIconSource)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable, wuxc::IBitmapIconSource)
        *object = nullptr;
        return TraceQueryInterfaceMiss(RuntimeClassName(), iid);
    }
    OPENXAML_COM_BOILERPLATE()
    HRESULT STDMETHODCALLTYPE get_UriSource(wf::IUriRuntimeClass** value) override {
        return Get(uri_, value);
    }
    HRESULT STDMETHODCALLTYPE put_UriSource(wf::IUriRuntimeClass* value) override {
        return Put(uri_, value);
    }
    HRESULT STDMETHODCALLTYPE get_ShowAsMonochrome(boolean* value) override {
        if (!value) return E_POINTER;
        *value = monochrome_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_ShowAsMonochrome(boolean value) override {
        monochrome_ = value != 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Foreground(wuxm::IBrush** value) override {
        return Get(foreground_, value);
    }
    HRESULT STDMETHODCALLTYPE put_Foreground(wuxm::IBrush* value) override {
        return Put(foreground_, value);
    }
private:
    template <class T> static HRESULT Get(T* source, T** value) {
        if (!value) return E_POINTER;
        *value = source;
        if (*value) (*value)->AddRef();
        return S_OK;
    }
    template <class T> static HRESULT Put(T*& target, T* value) {
        if (value) value->AddRef();
        if (target) target->Release();
        target = value;
        return S_OK;
    }
    wf::IUriRuntimeClass* uri_ = nullptr;
    wuxm::IBrush* foreground_ = nullptr;
    boolean monochrome_ = 0;
};

inline constexpr GUID IID_IMuxcBitmapIconSource = {
    0xa6b6cccc, 0xea8f, 0x53ca,
    {0x83, 0x1f, 0x2a, 0xbe, 0x85, 0xcd, 0x6d, 0x8c}};
inline constexpr GUID IID_IMuxcIconSource = {
    0x6e3501ed, 0xdd31, 0x51e9,
    {0x8f, 0x14, 0x25, 0x61, 0xf9, 0x9c, 0x8a, 0x8f}};

struct IMuxcBitmapIconSource : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_UriSource(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_UriSource(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ShowAsMonochrome(boolean*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_ShowAsMonochrome(boolean) = 0;
};
struct IMuxcIconSource : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE CreateIconElement(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Foreground(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Foreground(void*) = 0;
};

class MuxcBitmapIconSourceObject final
    : public ComObject,
      public abi::NotImpl_IDependencyObject,
      public IMuxcBitmapIconSource,
      public IMuxcIconSource {
public:
    using PrimaryInterface = IMuxcBitmapIconSource;
    ~MuxcBitmapIconSourceObject() override {
        if (uri_) uri_->Release();
        if (foreground_) foreground_->Release();
    }
    const wchar_t* RuntimeClassName() const override {
        return L"Microsoft.UI.Xaml.Controls.BitmapIconSource";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(IID_IMuxcBitmapIconSource, IMuxcBitmapIconSource)
        OPENXAML_QI_ARM(IID_IMuxcIconSource, IMuxcIconSource)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_IDependencyObject,
                        wux::IDependencyObject)
        OPENXAML_QI_ARM(IID_IUnknown, IMuxcBitmapIconSource)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable, IMuxcBitmapIconSource)
        *object = nullptr;
        return TraceQueryInterfaceMiss(RuntimeClassName(), iid);
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE get_UriSource(void** value) override {
        return Get(uri_, value);
    }
    HRESULT STDMETHODCALLTYPE put_UriSource(void* value) override {
        return Put(uri_, static_cast<IInspectable*>(value));
    }
    HRESULT STDMETHODCALLTYPE get_ShowAsMonochrome(boolean* value) override {
        if (!value) return E_POINTER;
        *value = monochrome_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_ShowAsMonochrome(boolean value) override {
        monochrome_ = value != 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateIconElement(void** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Foreground(void** value) override {
        return Get(foreground_, value);
    }
    HRESULT STDMETHODCALLTYPE put_Foreground(void* value) override {
        return Put(foreground_, static_cast<IInspectable*>(value));
    }

private:
    static HRESULT Get(IInspectable* source, void** value) {
        if (!value) return E_POINTER;
        *value = source;
        if (source) source->AddRef();
        return S_OK;
    }
    static HRESULT Put(IInspectable*& target, IInspectable* value) {
        if (value) value->AddRef();
        if (target) target->Release();
        target = value;
        return S_OK;
    }
    IInspectable* uri_ = nullptr;
    IInspectable* foreground_ = nullptr;
    boolean monochrome_ = 0;
};

class IconSourceElementObject final
    : public XamlElement,
      public abi::NotImpl_IIconElement,
      public abi::NotImpl_IIconSourceElement {
public:
    using PrimaryInterface = wuxc::IIconSourceElement;
    ~IconSourceElementObject() override {
        if (source_) source_->Release();
        if (foreground_) foreground_->Release();
    }
    openxaml::Element* Layout() override { return &layout_; }
    const wchar_t* RuntimeClassName() const override {
        return L"Windows.UI.Xaml.Controls.IconSourceElement";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IIconSourceElement,
                        wuxc::IIconSourceElement)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IIconElement,
                        wuxc::IIconElement)
        return QueryElementInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()
    HRESULT STDMETHODCALLTYPE get_IconSource(wuxc::IIconSource** value) override {
        return Get(source_, value);
    }
    HRESULT STDMETHODCALLTYPE put_IconSource(wuxc::IIconSource* value) override {
        return Put(source_, value);
    }
    HRESULT STDMETHODCALLTYPE get_Foreground(wuxm::IBrush** value) override {
        return Get(foreground_, value);
    }
    HRESULT STDMETHODCALLTYPE put_Foreground(wuxm::IBrush* value) override {
        return Put(foreground_, value);
    }
private:
    template <class T> static HRESULT Get(T* source, T** value) {
        if (!value) return E_POINTER;
        *value = source;
        if (*value) (*value)->AddRef();
        return S_OK;
    }
    template <class T> static HRESULT Put(T*& target, T* value) {
        if (value) value->AddRef();
        if (target) target->Release();
        target = value;
        return S_OK;
    }
    openxaml::FontIcon layout_;
    wuxc::IIconSource* source_ = nullptr;
    wuxm::IBrush* foreground_ = nullptr;
};

class MenuFlyoutItemObject final
    : public ContentControlObjectBase<openxaml::Button>,
      public abi::NotImpl_IMenuFlyoutItem,
      public abi::NotImpl_IMenuFlyoutItem2,
      public abi::NotImpl_IMenuFlyoutItem3,
      public abi::NotImpl_IMenuFlyoutItemBase {
public:
    using PrimaryInterface = wuxc::IMenuFlyoutItem;
    ~MenuFlyoutItemObject() override {
        if (command_) command_->Release();
        if (command_parameter_) command_parameter_->Release();
        if (icon_) icon_->Release();
    }
    const wchar_t* RuntimeClassName() const override {
        return L"Windows.UI.Xaml.Controls.MenuFlyoutItem";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IMenuFlyoutItem,
                        wuxc::IMenuFlyoutItem)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IMenuFlyoutItem2,
                        wuxc::IMenuFlyoutItem2)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IMenuFlyoutItem3,
                        wuxc::IMenuFlyoutItem3)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IMenuFlyoutItemBase,
                        wuxc::IMenuFlyoutItemBase)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IControl,
                        wuxc::IControl)
        return QueryElementInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE get_Text(HSTRING* value) override {
        return HStringFromUtf8(text_, value);
    }
    HRESULT STDMETHODCALLTYPE put_Text(HSTRING value) override {
        text_ = Utf8FromHString(value);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Command(wuxi::ICommand** value) override {
        return Get(command_, value);
    }
    HRESULT STDMETHODCALLTYPE put_Command(wuxi::ICommand* value) override {
        return Put(command_, value);
    }
    HRESULT STDMETHODCALLTYPE get_CommandParameter(IInspectable** value) override {
        return Get(command_parameter_, value);
    }
    HRESULT STDMETHODCALLTYPE put_CommandParameter(IInspectable* value) override {
        return Put(command_parameter_, value);
    }
    HRESULT STDMETHODCALLTYPE add_Click(wux::IRoutedEventHandler* handler,
                                        EventRegistrationToken* token) override {
        return AddEvent(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_Click(EventRegistrationToken token) override {
        return RemoveEvent(token);
    }
    HRESULT STDMETHODCALLTYPE get_Icon(wuxc::IIconElement** value) override {
        return Get(icon_, value);
    }
    HRESULT STDMETHODCALLTYPE put_Icon(wuxc::IIconElement* value) override {
        return Put(icon_, value);
    }
    HRESULT STDMETHODCALLTYPE get_KeyboardAcceleratorTextOverride(HSTRING* value) override {
        return HStringFromUtf8(keyboard_text_, value);
    }
    HRESULT STDMETHODCALLTYPE put_KeyboardAcceleratorTextOverride(HSTRING value) override {
        keyboard_text_ = Utf8FromHString(value);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_TemplateSettings(
        wuxcp::IMenuFlyoutItemTemplateSettings** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsEnabled(boolean* value) override {
        if (!value) return E_POINTER;
        *value = enabled_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_IsEnabled(boolean value) override {
        enabled_ = value != 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_FontWeight(
        ABI::Windows::UI::Text::FontWeight* value) override {
        if (!value) return E_POINTER;
        *value = font_weight_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_FontWeight(
        ABI::Windows::UI::Text::FontWeight value) override {
        font_weight_ = value;
        return S_OK;
    }

private:
    template <class T> static HRESULT Get(T* source, T** value) {
        if (!value) return E_POINTER;
        *value = source;
        if (*value) (*value)->AddRef();
        return S_OK;
    }
    template <class T> static HRESULT Put(T*& target, T* value) {
        if (value) value->AddRef();
        if (target) target->Release();
        target = value;
        return S_OK;
    }
    std::string text_;
    std::string keyboard_text_;
    wuxi::ICommand* command_ = nullptr;
    IInspectable* command_parameter_ = nullptr;
    wuxc::IIconElement* icon_ = nullptr;
    boolean enabled_ = 1;
    ABI::Windows::UI::Text::FontWeight font_weight_{400};
};

class MenuFlyoutSeparatorObject final
    : public ContentControlObjectBase<openxaml::Button>,
      public abi::NotImpl_IMenuFlyoutSeparator,
      public abi::NotImpl_IMenuFlyoutItemBase {
public:
    using PrimaryInterface = wuxc::IMenuFlyoutSeparator;
    const wchar_t* RuntimeClassName() const override {
        return L"Windows.UI.Xaml.Controls.MenuFlyoutSeparator";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IMenuFlyoutSeparator,
                        wuxc::IMenuFlyoutSeparator)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IMenuFlyoutItemBase,
                        wuxc::IMenuFlyoutItemBase)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IControl,
                        wuxc::IControl)
        return QueryElementInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()
};

class MenuFlyoutSubItemObject final
    : public ContentControlObjectBase<openxaml::Button>,
      public abi::NotImpl_IMenuFlyoutSubItem,
      public abi::NotImpl_IMenuFlyoutSubItem2,
      public abi::NotImpl_IMenuFlyoutItemBase {
public:
    using PrimaryInterface = wuxc::IMenuFlyoutSubItem;
    ~MenuFlyoutSubItemObject() override {
        if (icon_) icon_->Release();
    }
    const wchar_t* RuntimeClassName() const override {
        return L"Windows.UI.Xaml.Controls.MenuFlyoutSubItem";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(
            ::openxaml::iid::Windows_UI_Xaml_Controls_IMenuFlyoutSubItem,
            wuxc::IMenuFlyoutSubItem)
        OPENXAML_QI_ARM(
            ::openxaml::iid::Windows_UI_Xaml_Controls_IMenuFlyoutSubItem2,
            wuxc::IMenuFlyoutSubItem2)
        OPENXAML_QI_ARM(
            ::openxaml::iid::Windows_UI_Xaml_Controls_IMenuFlyoutItemBase,
            wuxc::IMenuFlyoutItemBase)
        return QueryControlInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE get_Items(
        __FIVector_1_Windows__CUI__CXaml__CControls__CMenuFlyoutItemBase**
            value) override {
        if (!value) return E_POINTER;
        *value = static_cast<
            __FIVector_1_Windows__CUI__CXaml__CControls__CMenuFlyoutItemBase*>(
                &items_);
        (*value)->AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Text(HSTRING* value) override {
        return HStringFromUtf8(text_, value);
    }
    HRESULT STDMETHODCALLTYPE put_Text(HSTRING value) override {
        text_ = Utf8FromHString(value);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Icon(wuxc::IIconElement** value) override {
        if (!value) return E_POINTER;
        *value = icon_;
        if (*value) (*value)->AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Icon(wuxc::IIconElement* value) override {
        if (value) value->AddRef();
        if (icon_) icon_->Release();
        icon_ = value;
        return S_OK;
    }

private:
    std::string text_;
    wuxc::IIconElement* icon_ = nullptr;
    MenuFlyoutItemCollection items_{
        {::openxaml::iid::PIID_FIVector_1_Windows__CUI__CXaml__CControls__CMenuFlyoutItemBase,
         ::openxaml::iid::PIID_FIIterable_1_Windows__CUI__CXaml__CControls__CMenuFlyoutItemBase,
         ::openxaml::iid::PIID_FIIterator_1_Windows__CUI__CXaml__CControls__CMenuFlyoutItemBase},
        L"Windows.UI.Xaml.Controls.MenuFlyoutSubItem.Items", this};
};

class ButtonObject final : public ContentControlObjectBase<openxaml::Button>,
                           public abi::NotImpl_IButton,
                           public abi::NotImpl_IButtonWithFlyout,
                           public abi::NotImpl_IButtonBase {
public:
    using PrimaryInterface = wuxc::IButton;
    ~ButtonObject() override { if (flyout_) flyout_->Release(); }
    const wchar_t* RuntimeClassName() const override { return L"Windows.UI.Xaml.Controls.Button"; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IButton, wuxc::IButton)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IButtonWithFlyout,
                        wuxc::IButtonWithFlyout)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_Primitives_IButtonBase,
                        wuxcp::IButtonBase)
        return QueryControlInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE add_Click(wux::IRoutedEventHandler* handler,
                                        EventRegistrationToken* token) override {
        return AddEvent(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_Click(EventRegistrationToken token) override {
        return RemoveEvent(token);
    }
    HRESULT STDMETHODCALLTYPE get_Flyout(wuxcp::IFlyoutBase** value) override {
        if (!value) return E_POINTER;
        *value = flyout_;
        if (*value) (*value)->AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Flyout(wuxcp::IFlyoutBase* value) override {
        if (value) value->AddRef();
        if (flyout_) flyout_->Release();
        flyout_ = value;
        return S_OK;
    }

private:
    wuxcp::IFlyoutBase* flyout_ = nullptr;
};

class AppBarButtonObject final
    : public ContentControlObjectBase<openxaml::Button>,
      public abi::NotImpl_IAppBarButton,
      public abi::NotImpl_ICommandBarElement,
      public abi::NotImpl_IButtonWithFlyout,
      public abi::NotImpl_IButtonBase {
public:
    using PrimaryInterface = wuxc::IAppBarButton;
    ~AppBarButtonObject() override {
        WindowsDeleteString(label_);
        if (icon_) icon_->Release();
        if (flyout_) flyout_->Release();
    }
    const wchar_t* RuntimeClassName() const override {
        return L"Windows.UI.Xaml.Controls.AppBarButton";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IAppBarButton,
                        wuxc::IAppBarButton)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_ICommandBarElement,
                        wuxc::ICommandBarElement)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IButtonWithFlyout,
                        wuxc::IButtonWithFlyout)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_Primitives_IButtonBase,
                        wuxcp::IButtonBase)
        return QueryControlInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE get_Label(HSTRING* value) override {
        if (!value) return E_POINTER;
        return WindowsDuplicateString(label_, value);
    }
    HRESULT STDMETHODCALLTYPE put_Label(HSTRING value) override {
        HSTRING copy = nullptr;
        HRESULT hr = WindowsDuplicateString(value, &copy);
        if (FAILED(hr)) return hr;
        WindowsDeleteString(label_);
        label_ = copy;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Icon(wuxc::IIconElement** value) override {
        if (!value) return E_POINTER;
        *value = icon_;
        if (*value) (*value)->AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Icon(wuxc::IIconElement* value) override {
        if (value) value->AddRef();
        if (icon_) icon_->Release();
        icon_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsCompact(boolean* value) override {
        if (!value) return E_POINTER;
        *value = compact_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_IsCompact(boolean value) override {
        compact_ = value != 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE add_Click(wux::IRoutedEventHandler* handler,
                                        EventRegistrationToken* token) override {
        return AddEvent(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_Click(EventRegistrationToken token) override {
        return RemoveEvent(token);
    }
    HRESULT STDMETHODCALLTYPE get_Flyout(wuxcp::IFlyoutBase** value) override {
        if (!value) return E_POINTER;
        *value = flyout_;
        if (*value) (*value)->AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Flyout(wuxcp::IFlyoutBase* value) override {
        if (value) value->AddRef();
        if (flyout_) flyout_->Release();
        flyout_ = value;
        return S_OK;
    }

private:
    HSTRING label_ = nullptr;
    wuxc::IIconElement* icon_ = nullptr;
    wuxcp::IFlyoutBase* flyout_ = nullptr;
    boolean compact_ = 0;
};

class ToolTipObject final : public ContentControlObjectBase<openxaml::ToolTip>,
                            public abi::NotImpl_IToolTip {
public:
    using PrimaryInterface = wuxc::IToolTip;
    const wchar_t* RuntimeClassName() const override { return L"Windows.UI.Xaml.Controls.ToolTip"; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IToolTip, wuxc::IToolTip)
        return QueryControlInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()
};

class ScrollViewerObject final
    : public ContentControlObjectBase<openxaml::ScrollViewer>,
      public abi::NotImpl_IScrollViewer {
public:
    using PrimaryInterface = wuxc::IScrollViewer;
    const wchar_t* RuntimeClassName() const override { return L"Windows.UI.Xaml.Controls.ScrollViewer"; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IScrollViewer, wuxc::IScrollViewer)
        return QueryControlInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE get_HorizontalScrollBarVisibility(wuxc::ScrollBarVisibility* value) override {
        if (!value) return E_POINTER;
        *value = static_cast<wuxc::ScrollBarVisibility>(layout_.horizontal_scroll_bar_visibility());
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_HorizontalScrollBarVisibility(wuxc::ScrollBarVisibility value) override {
        if (value < wuxc::ScrollBarVisibility_Disabled || value > wuxc::ScrollBarVisibility_Visible)
            return E_INVALIDARG;
        layout_.set_horizontal_scroll_bar_visibility(static_cast<openxaml::ScrollBarVisibility>(value));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_VerticalScrollBarVisibility(wuxc::ScrollBarVisibility* value) override {
        if (!value) return E_POINTER;
        *value = static_cast<wuxc::ScrollBarVisibility>(layout_.vertical_scroll_bar_visibility());
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_VerticalScrollBarVisibility(wuxc::ScrollBarVisibility value) override {
        if (value < wuxc::ScrollBarVisibility_Disabled || value > wuxc::ScrollBarVisibility_Visible)
            return E_INVALIDARG;
        layout_.set_vertical_scroll_bar_visibility(static_cast<openxaml::ScrollBarVisibility>(value));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_HorizontalOffset(DOUBLE* value) override {
        if (!value) return E_POINTER;
        *value = layout_.horizontal_offset();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_VerticalOffset(DOUBLE* value) override {
        if (!value) return E_POINTER;
        *value = layout_.vertical_offset();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_ViewportWidth(DOUBLE* value) override {
        if (!value) return E_POINTER;
        *value = layout_.viewport().width;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_ViewportHeight(DOUBLE* value) override {
        if (!value) return E_POINTER;
        *value = layout_.viewport().height;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_ExtentWidth(DOUBLE* value) override {
        if (!value) return E_POINTER;
        *value = layout_.extent().width;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_ExtentHeight(DOUBLE* value) override {
        if (!value) return E_POINTER;
        *value = layout_.extent().height;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ScrollToHorizontalOffset(DOUBLE value) override {
        layout_.ScrollTo(value, layout_.vertical_offset()); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ScrollToVerticalOffset(DOUBLE value) override {
        layout_.ScrollTo(layout_.horizontal_offset(), value); return S_OK;
    }
};

class TextBoxObject final : public XamlElement, public abi::NotImpl_IControl,
                            public abi::NotImpl_ITextBox {
public:
    using PrimaryInterface = wuxc::ITextBox;
    openxaml::Element* Layout() override { return &layout_; }
    const wchar_t* RuntimeClassName() const override { return L"Windows.UI.Xaml.Controls.TextBox"; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_ITextBox, wuxc::ITextBox)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IControl, wuxc::IControl)
        return QueryElementInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()
    HRESULT STDMETHODCALLTYPE get_Text(HSTRING* value) override { return HStringFromUtf8(layout_.text(), value); }
    HRESULT STDMETHODCALLTYPE put_Text(HSTRING value) override { layout_.set_text(Utf8FromHString(value)); return S_OK; }
    HRESULT STDMETHODCALLTYPE ApplyTemplate(boolean* value) override {
        if (!value) return E_POINTER;
        *value = layout_.ApplyTemplate() ? 1 : 0;
        return S_OK;
    }
private:
    openxaml::TextBox layout_;
};

class ThumbObject final : public XamlElement, public abi::NotImpl_IControl,
                          public abi::NotImpl_IThumb {
public:
    using PrimaryInterface = wuxcp::IThumb;
    openxaml::Element* Layout() override { return &layout_; }
    const wchar_t* RuntimeClassName() const override { return L"Windows.UI.Xaml.Controls.Primitives.Thumb"; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_Primitives_IThumb, wuxcp::IThumb)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IControl, wuxc::IControl)
        return QueryElementInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()
    HRESULT STDMETHODCALLTYPE ApplyTemplate(boolean* value) override {
        if (!value) return E_POINTER;
        *value = layout_.ApplyTemplate() ? 1 : 0;
        return S_OK;
    }
private:
    openxaml::Thumb layout_;
};

class ScrollBarObject final
    : public ContentControlObjectBase<openxaml::ContentControl>,
      public abi::NotImpl_IScrollBar,
      public abi::NotImpl_IRangeBase {
public:
    using PrimaryInterface = wuxcp::IScrollBar;
    const wchar_t* RuntimeClassName() const override {
        return L"Windows.UI.Xaml.Controls.Primitives.ScrollBar";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(
            ::openxaml::iid::Windows_UI_Xaml_Controls_Primitives_IScrollBar,
            wuxcp::IScrollBar)
        OPENXAML_QI_ARM(
            ::openxaml::iid::Windows_UI_Xaml_Controls_Primitives_IRangeBase,
            wuxcp::IRangeBase)
        return QueryControlInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()

#define OPENXAML_SCROLLBAR_DOUBLE(name, field)                         \
    HRESULT STDMETHODCALLTYPE get_##name(DOUBLE* value) override {     \
        if (!value) return E_POINTER;                                  \
        *value = field;                                                \
        return S_OK;                                                   \
    }                                                                  \
    HRESULT STDMETHODCALLTYPE put_##name(DOUBLE value) override {      \
        field = value;                                                 \
        return S_OK;                                                   \
    }
    OPENXAML_SCROLLBAR_DOUBLE(Minimum, minimum_)
    OPENXAML_SCROLLBAR_DOUBLE(Maximum, maximum_)
    OPENXAML_SCROLLBAR_DOUBLE(SmallChange, small_change_)
    OPENXAML_SCROLLBAR_DOUBLE(LargeChange, large_change_)
    OPENXAML_SCROLLBAR_DOUBLE(Value, value_)
    OPENXAML_SCROLLBAR_DOUBLE(ViewportSize, viewport_size_)
#undef OPENXAML_SCROLLBAR_DOUBLE

    HRESULT STDMETHODCALLTYPE get_Orientation(wuxc::Orientation* value) override {
        if (!value) return E_POINTER;
        *value = orientation_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Orientation(wuxc::Orientation value) override {
        orientation_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IndicatorMode(
        wuxcp::ScrollingIndicatorMode* value) override {
        if (!value) return E_POINTER;
        *value = indicator_mode_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_IndicatorMode(
        wuxcp::ScrollingIndicatorMode value) override {
        indicator_mode_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE add_ValueChanged(
        wuxcp::IRangeBaseValueChangedEventHandler* handler,
        EventRegistrationToken* token) override {
        return AddEvent(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_ValueChanged(EventRegistrationToken token) override {
        return RemoveEvent(token);
    }
    HRESULT STDMETHODCALLTYPE add_Scroll(
        wuxcp::IScrollEventHandler* handler, EventRegistrationToken* token) override {
        return AddEvent(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_Scroll(EventRegistrationToken token) override {
        return RemoveEvent(token);
    }

private:
    DOUBLE minimum_ = 0.0;
    DOUBLE maximum_ = 1.0;
    DOUBLE small_change_ = 0.1;
    DOUBLE large_change_ = 1.0;
    DOUBLE value_ = 0.0;
    DOUBLE viewport_size_ = 0.0;
    wuxc::Orientation orientation_ = wuxc::Orientation_Vertical;
    wuxcp::ScrollingIndicatorMode indicator_mode_ =
        wuxcp::ScrollingIndicatorMode_None;
};

class FontIconObject final : public XamlElement,
                             public abi::NotImpl_IFontIcon,
                             public abi::NotImpl_IIconElement {
public:
    using PrimaryInterface = wuxc::IFontIcon;
    ~FontIconObject() override { if (foreground_) foreground_->Release(); }
    openxaml::Element* Layout() override { return &layout_; }
    const wchar_t* RuntimeClassName() const override { return L"Windows.UI.Xaml.Controls.FontIcon"; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IFontIcon, wuxc::IFontIcon)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IIconElement,
                        wuxc::IIconElement)
        return QueryElementInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()
    HRESULT STDMETHODCALLTYPE get_Glyph(HSTRING* value) override { return HStringFromUtf8(layout_.glyph(), value); }
    HRESULT STDMETHODCALLTYPE put_Glyph(HSTRING value) override { layout_.set_glyph(Utf8FromHString(value)); return S_OK; }
    HRESULT STDMETHODCALLTYPE get_FontSize(DOUBLE* value) override {
        if (!value) return E_POINTER;
        *value = layout_.font_size();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_FontSize(DOUBLE value) override { layout_.set_font_size(value); return S_OK; }
    HRESULT STDMETHODCALLTYPE get_FontFamily(wuxm::IFontFamily** value) override;
    HRESULT STDMETHODCALLTYPE put_FontFamily(wuxm::IFontFamily* value) override;
    HRESULT STDMETHODCALLTYPE get_Foreground(wuxm::IBrush** value) override {
        if (!value) return E_POINTER;
        *value = foreground_;
        if (*value) (*value)->AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Foreground(wuxm::IBrush* value) override {
        if (value) value->AddRef();
        if (foreground_) foreground_->Release();
        foreground_ = value;
        return S_OK;
    }
private:
    openxaml::FontIcon layout_;
    wuxm::IBrush* foreground_ = nullptr;
};

class SymbolIconObject final : public XamlElement,
                               public abi::NotImpl_ISymbolIcon,
                               public abi::NotImpl_IIconElement {
public:
    using PrimaryInterface = wuxc::ISymbolIcon;
    explicit SymbolIconObject(
        wuxc::Symbol symbol = wuxc::Symbol_Previous) : symbol_(symbol) {
        layout_.set_glyph(Utf8FromCodePoint(static_cast<char32_t>(symbol_)));
    }
    ~SymbolIconObject() override { if (foreground_) foreground_->Release(); }
    openxaml::Element* Layout() override { return &layout_; }
    const wchar_t* RuntimeClassName() const override {
        return L"Windows.UI.Xaml.Controls.SymbolIcon";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_ISymbolIcon,
                        wuxc::ISymbolIcon)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IIconElement,
                        wuxc::IIconElement)
        return QueryElementInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()
    HRESULT STDMETHODCALLTYPE get_Symbol(wuxc::Symbol* value) override {
        if (!value) return E_POINTER;
        *value = symbol_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Symbol(wuxc::Symbol value) override {
        symbol_ = value;
        layout_.set_glyph(Utf8FromCodePoint(static_cast<char32_t>(symbol_)));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Foreground(wuxm::IBrush** value) override {
        if (!value) return E_POINTER;
        *value = foreground_;
        if (*value) (*value)->AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Foreground(wuxm::IBrush* value) override {
        if (value) value->AddRef();
        if (foreground_) foreground_->Release();
        foreground_ = value;
        return S_OK;
    }
private:
    openxaml::FontIcon layout_;
    wuxc::Symbol symbol_;
    wuxm::IBrush* foreground_ = nullptr;
};

class RectangleObject final : public XamlElement, public abi::NotImpl_IRectangle {
public:
    using PrimaryInterface = wuxs::IRectangle;
    openxaml::Element* Layout() override { return &layout_; }
    const wchar_t* RuntimeClassName() const override { return L"Windows.UI.Xaml.Shapes.Rectangle"; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Shapes_IRectangle, wuxs::IRectangle)
        return QueryElementInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()
    HRESULT STDMETHODCALLTYPE get_RadiusX(DOUBLE* value) override {
        if (!value) return E_POINTER;
        *value = radius_x_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_RadiusX(DOUBLE value) override { radius_x_ = value; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_RadiusY(DOUBLE* value) override {
        if (!value) return E_POINTER;
        *value = radius_y_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_RadiusY(DOUBLE value) override { radius_y_ = value; return S_OK; }
private:
    openxaml::Rectangle layout_;
    double radius_x_ = 0;
    double radius_y_ = 0;
};

// --- FontFamily ---------------------------------------------------------------
//
// A name, and nothing else. It exists because ITextBlock::put_FontFamily takes
// an IFontFamily rather than a string, so there is no way to say "Segoe UI"
// through this ABI without an object to say it with. It is a DependencyObject
// in XAML but carries no layout, so it stands on its own here.

class FontFamilyObject final : public ComObject, public abi::NotImpl_IFontFamily {
public:
    using PrimaryInterface = wuxm::IFontFamily;

    const wchar_t* RuntimeClassName() const override { return L"Windows.UI.Xaml.Media.FontFamily"; }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Media_IFontFamily, wuxm::IFontFamily)
        OPENXAML_QI_ARM(IID_IUnknown, wuxm::IFontFamily)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable, wuxm::IFontFamily)
        *object = nullptr;
        return E_NOINTERFACE;
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE get_Source(HSTRING* value) override {
        return HStringFromUtf8(source, value);
    }

    std::string source;
};

template <class LayoutType>
HRESULT ContentControlObjectBase<LayoutType>::get_FontFamily(wuxm::IFontFamily** value) {
    if (!value) return E_POINTER;
    auto* family = new FontFamilyObject();
    family->source = layout_.font_family();
    family->AddRef();
    *value = family;
    return S_OK;
}

template <class LayoutType>
HRESULT ContentControlObjectBase<LayoutType>::put_FontFamily(wuxm::IFontFamily* value) {
    if (!value) return E_INVALIDARG;
    HSTRING source = nullptr;
    const HRESULT hr = value->get_Source(&source);
    if (FAILED(hr)) return hr;
    layout_.set_font_family(Utf8FromHString(source));
    WindowsDeleteString(source);
    return S_OK;
}

inline HRESULT FontIconObject::get_FontFamily(wuxm::IFontFamily** value) {
    if (!value) return E_POINTER;
    auto* family = new FontFamilyObject();
    family->source = layout_.font_family();
    family->AddRef();
    *value = family;
    return S_OK;
}

inline HRESULT FontIconObject::put_FontFamily(wuxm::IFontFamily* value) {
    if (!value) return E_INVALIDARG;
    HSTRING source = nullptr;
    const HRESULT hr = value->get_Source(&source);
    if (FAILED(hr)) return hr;
    layout_.set_font_family(Utf8FromHString(source));
    WindowsDeleteString(source);
    return S_OK;
}

// --- TextBlock ----------------------------------------------------------------

class RunObject final : public ComObject,
                        public abi::NotImpl_IDependencyObject,
                        public abi::NotImpl_ITextElement,
                        public abi::NotImpl_IInline,
                        public abi::NotImpl_IRun {
public:
    using PrimaryInterface = wuxd::IRun;
    ~RunObject() override {
        if (font_family_) font_family_->Release();
        if (foreground_) foreground_->Release();
        WindowsDeleteString(language_);
    }
    const wchar_t* RuntimeClassName() const override {
        return L"Windows.UI.Xaml.Documents.Run";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Documents_IRun, wuxd::IRun)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Documents_IInline,
                        wuxd::IInline)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Documents_ITextElement,
                        wuxd::ITextElement)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_IDependencyObject,
                        wux::IDependencyObject)
        OPENXAML_QI_ARM(IID_IUnknown, wuxd::IRun)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable, wuxd::IRun)
        *object = nullptr;
        return TraceQueryInterfaceMiss(RuntimeClassName(), iid);
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE get_Text(HSTRING* value) override {
        return HStringFromUtf8(text_, value);
    }
    HRESULT STDMETHODCALLTYPE put_Text(HSTRING value) override {
        text_ = Utf8FromHString(value);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Name(HSTRING* value) override {
        if (!value) return E_POINTER;
        return WindowsCreateString(L"", 0, value);
    }
    HRESULT STDMETHODCALLTYPE get_FontSize(DOUBLE* value) override {
        if (!value) return E_POINTER;
        *value = font_size_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_FontSize(DOUBLE value) override {
        if (!std::isfinite(value) || value < 0.0) return E_INVALIDARG;
        font_size_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_FontFamily(wuxm::IFontFamily** value) override {
        if (!value) return E_POINTER;
        *value = font_family_;
        if (*value) (*value)->AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_FontFamily(wuxm::IFontFamily* value) override {
        if (value) value->AddRef();
        if (font_family_) font_family_->Release();
        font_family_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_FontWeight(
        ABI::Windows::UI::Text::FontWeight* value) override {
        if (!value) return E_POINTER;
        *value = font_weight_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_FontWeight(
        ABI::Windows::UI::Text::FontWeight value) override {
        font_weight_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_FontStyle(
        ABI::Windows::UI::Text::FontStyle* value) override {
        if (!value) return E_POINTER;
        *value = font_style_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_FontStyle(
        ABI::Windows::UI::Text::FontStyle value) override {
        font_style_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_FontStretch(
        ABI::Windows::UI::Text::FontStretch* value) override {
        if (!value) return E_POINTER;
        *value = font_stretch_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_FontStretch(
        ABI::Windows::UI::Text::FontStretch value) override {
        font_stretch_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_CharacterSpacing(INT32* value) override {
        if (!value) return E_POINTER;
        *value = character_spacing_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_CharacterSpacing(INT32 value) override {
        character_spacing_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Foreground(wuxm::IBrush** value) override {
        if (!value) return E_POINTER;
        *value = foreground_;
        if (*value) (*value)->AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Foreground(wuxm::IBrush* value) override {
        if (value) value->AddRef();
        if (foreground_) foreground_->Release();
        foreground_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Language(HSTRING* value) override {
        if (!value) return E_POINTER;
        if (!language_) return WindowsCreateString(L"", 0, value);
        return WindowsDuplicateString(language_, value);
    }
    HRESULT STDMETHODCALLTYPE put_Language(HSTRING value) override {
        HSTRING next = nullptr;
        HRESULT hr = value ? WindowsDuplicateString(value, &next) : S_OK;
        if (FAILED(hr)) return hr;
        WindowsDeleteString(language_);
        language_ = next;
        return S_OK;
    }

private:
    std::string text_;
    DOUBLE font_size_ = 14.0;
    wuxm::IFontFamily* font_family_ = nullptr;
    ABI::Windows::UI::Text::FontWeight font_weight_{400};
    ABI::Windows::UI::Text::FontStyle font_style_ =
        ABI::Windows::UI::Text::FontStyle_Normal;
    ABI::Windows::UI::Text::FontStretch font_stretch_ =
        ABI::Windows::UI::Text::FontStretch_Normal;
    INT32 character_spacing_ = 0;
    wuxm::IBrush* foreground_ = nullptr;
    HSTRING language_ = nullptr;
};

class LineBreakObject final : public ComObject,
                              public abi::NotImpl_IDependencyObject,
                              public abi::NotImpl_ITextElement,
                              public abi::NotImpl_IInline,
                              public abi::NotImpl_ILineBreak {
public:
    using PrimaryInterface = wuxd::ILineBreak;
    const wchar_t* RuntimeClassName() const override {
        return L"Windows.UI.Xaml.Documents.LineBreak";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Documents_ILineBreak,
                        wuxd::ILineBreak)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Documents_IInline,
                        wuxd::IInline)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Documents_ITextElement,
                        wuxd::ITextElement)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_IDependencyObject,
                        wux::IDependencyObject)
        OPENXAML_QI_ARM(IID_IUnknown, wuxd::ILineBreak)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable, wuxd::ILineBreak)
        *object = nullptr;
        return TraceQueryInterfaceMiss(RuntimeClassName(), iid);
    }
    OPENXAML_COM_BOILERPLATE()
};

class PropertyChangedEventArgsObject final
    : public ComObject,
      public abi::NotImpl_IPropertyChangedEventArgs {
public:
    using PrimaryInterface = wuxdata::IPropertyChangedEventArgs;
    PropertyChangedEventArgsObject() = default;
    explicit PropertyChangedEventArgsObject(HSTRING value) {
        if (value) WindowsDuplicateString(value, &name_);
    }
    ~PropertyChangedEventArgsObject() override { WindowsDeleteString(name_); }
    const wchar_t* RuntimeClassName() const override {
        return L"Windows.UI.Xaml.Data.PropertyChangedEventArgs";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Data_IPropertyChangedEventArgs,
                        wuxdata::IPropertyChangedEventArgs)
        OPENXAML_QI_ARM(IID_IUnknown, wuxdata::IPropertyChangedEventArgs)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable,
                        wuxdata::IPropertyChangedEventArgs)
        *object = nullptr;
        return TraceQueryInterfaceMiss(RuntimeClassName(), iid);
    }
    OPENXAML_COM_BOILERPLATE()
    HRESULT STDMETHODCALLTYPE get_PropertyName(HSTRING* value) override {
        if (!value) return E_POINTER;
        if (!name_) return WindowsCreateString(L"", 0, value);
        return WindowsDuplicateString(name_, value);
    }
private:
    HSTRING name_ = nullptr;
};

class TextBlockObject final : public XamlElement, public abi::NotImpl_ITextBlock {
public:
    ~TextBlockObject() override {
        if (foreground_) foreground_->Release();
    }
    openxaml::Element* Layout() override { return &text_; }
    const wchar_t* RuntimeClassName() const override {
        return L"Windows.UI.Xaml.Controls.TextBlock";
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_ITextBlock, wuxc::ITextBlock)
        return QueryElementInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE get_Text(HSTRING* value) override {
        return HStringFromUtf8(text_.text(), value);
    }
    HRESULT STDMETHODCALLTYPE put_Text(HSTRING value) override {
        text_.set_text(Utf8FromHString(value));
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_FontSize(DOUBLE* value) override {
        if (!value) return E_POINTER;
        *value = text_.font_size();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_FontSize(DOUBLE value) override {
        text_.set_font_size(value);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_FontFamily(wuxm::IFontFamily** value) override {
        if (!value) return E_POINTER;
        // Handed back as a fresh object rather than a retained one: nothing
        // here keeps the instance that was put, only the name it carried.
        auto* family = new FontFamilyObject();
        family->source = text_.font_family();
        *value = family;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_FontFamily(wuxm::IFontFamily* value) override {
        if (!value) return E_INVALIDARG;
        HSTRING source = nullptr;
        const HRESULT hr = value->get_Source(&source);
        if (FAILED(hr)) return hr;
        text_.set_font_family(Utf8FromHString(source));
        WindowsDeleteString(source);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_TextWrapping(wux::TextWrapping* value) override {
        if (!value) return E_POINTER;
        *value = text_.text_wrapping() == openxaml::TextWrapping::Wrap
                     ? wux::TextWrapping_Wrap
                     : wux::TextWrapping_NoWrap;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_TextWrapping(wux::TextWrapping value) override {
        switch (value) {
            case wux::TextWrapping_NoWrap:
                text_.set_text_wrapping(openxaml::TextWrapping::NoWrap);
                return S_OK;
            case wux::TextWrapping_Wrap:
                text_.set_text_wrapping(openxaml::TextWrapping::Wrap);
                return S_OK;
            default:
                // The layout core currently uses the same word boundary
                // algorithm for Wrap and WrapWholeWords.
                text_.set_text_wrapping(openxaml::TextWrapping::Wrap);
                return S_OK;
        }
    }
    HRESULT STDMETHODCALLTYPE get_FontWeight(
        ABI::Windows::UI::Text::FontWeight* value) override {
        if (!value) return E_POINTER;
        *value = font_weight_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_FontWeight(
        ABI::Windows::UI::Text::FontWeight value) override {
        font_weight_ = value;
        text_.set_simulates_bold(value.Weight >= 600);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_FontStyle(
        ABI::Windows::UI::Text::FontStyle* value) override {
        if (!value) return E_POINTER;
        *value = font_style_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_FontStyle(
        ABI::Windows::UI::Text::FontStyle value) override {
        font_style_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_FontStretch(
        ABI::Windows::UI::Text::FontStretch* value) override {
        if (!value) return E_POINTER;
        *value = font_stretch_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_FontStretch(
        ABI::Windows::UI::Text::FontStretch value) override {
        font_stretch_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_CharacterSpacing(INT32* value) override {
        if (!value) return E_POINTER;
        *value = character_spacing_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_CharacterSpacing(INT32 value) override {
        character_spacing_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Foreground(wuxm::IBrush** value) override {
        if (!value) return E_POINTER;
        *value = foreground_;
        if (*value) (*value)->AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Foreground(wuxm::IBrush* value) override {
        if (value) value->AddRef();
        if (foreground_) foreground_->Release();
        foreground_ = value;
        return S_OK;
    }
#define OPENXAML_TEXT_ENUM_PROPERTY(type, name, field)                  \
    HRESULT STDMETHODCALLTYPE get_##name(type* value) override {       \
        if (!value) return E_POINTER;                                   \
        *value = field;                                                 \
        return S_OK;                                                    \
    }                                                                   \
    HRESULT STDMETHODCALLTYPE put_##name(type value) override {        \
        field = value;                                                  \
        return S_OK;                                                    \
    }
    OPENXAML_TEXT_ENUM_PROPERTY(wux::TextTrimming, TextTrimming,
                                text_trimming_)
    OPENXAML_TEXT_ENUM_PROPERTY(wux::TextAlignment, TextAlignment,
                                text_alignment_)
    OPENXAML_TEXT_ENUM_PROPERTY(wux::LineStackingStrategy,
                                LineStackingStrategy,
                                line_stacking_strategy_)
#undef OPENXAML_TEXT_ENUM_PROPERTY
    HRESULT STDMETHODCALLTYPE get_Padding(wux::Thickness* value) override {
        if (!value) return E_POINTER;
        *value = padding_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Padding(wux::Thickness value) override {
        padding_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_LineHeight(DOUBLE* value) override {
        if (!value) return E_POINTER;
        *value = line_height_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_LineHeight(DOUBLE value) override {
        if (value < 0.0) return E_INVALIDARG;
        line_height_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsTextSelectionEnabled(boolean* value) override {
        if (!value) return E_POINTER;
        *value = is_text_selection_enabled_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_IsTextSelectionEnabled(boolean value) override {
        is_text_selection_enabled_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Inlines(
        __FIVector_1_Windows__CUI__CXaml__CDocuments__CInline** value) override {
        if (!value) return E_POINTER;
        *value = static_cast<
            __FIVector_1_Windows__CUI__CXaml__CDocuments__CInline*>(&inlines_);
        (*value)->AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_SelectedText(HSTRING* value) override {
        return CopyToHString(L"", value);
    }
    HRESULT STDMETHODCALLTYPE get_ContentStart(wuxd::ITextPointer** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_ContentEnd(wuxd::ITextPointer** value) override {
        return get_ContentStart(value);
    }
    HRESULT STDMETHODCALLTYPE get_SelectionStart(wuxd::ITextPointer** value) override {
        return get_ContentStart(value);
    }
    HRESULT STDMETHODCALLTYPE get_SelectionEnd(wuxd::ITextPointer** value) override {
        return get_ContentStart(value);
    }
    HRESULT STDMETHODCALLTYPE get_BaselineOffset(DOUBLE* value) override {
        if (!value) return E_POINTER;
        *value = text_.font_size() * 0.8;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE add_SelectionChanged(
        wux::IRoutedEventHandler* handler, EventRegistrationToken* token) override {
        return AddEvent(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_SelectionChanged(
        EventRegistrationToken token) override {
        return RemoveEvent(token);
    }
    HRESULT STDMETHODCALLTYPE add_ContextMenuOpening(
        wuxc::IContextMenuOpeningEventHandler* handler,
        EventRegistrationToken* token) override {
        return AddEvent(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_ContextMenuOpening(
        EventRegistrationToken token) override {
        return RemoveEvent(token);
    }
    HRESULT STDMETHODCALLTYPE SelectAll() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Select(wuxd::ITextPointer*,
                                     wuxd::ITextPointer*) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Focus(wux::FocusState, boolean* value) override {
        if (!value) return E_POINTER;
        *value = 1;
        return S_OK;
    }

private:
    openxaml::TextBlock text_;
    boolean is_text_selection_enabled_ = false;
    ABI::Windows::UI::Text::FontWeight font_weight_{400};
    ABI::Windows::UI::Text::FontStyle font_style_ =
        ABI::Windows::UI::Text::FontStyle_Normal;
    ABI::Windows::UI::Text::FontStretch font_stretch_ =
        ABI::Windows::UI::Text::FontStretch_Normal;
    INT32 character_spacing_ = 0;
    wuxm::IBrush* foreground_ = nullptr;
    wux::TextTrimming text_trimming_ = wux::TextTrimming_None;
    wux::TextAlignment text_alignment_ = wux::TextAlignment_Left;
    wux::Thickness padding_{};
    DOUBLE line_height_ = 0.0;
    wux::LineStackingStrategy line_stacking_strategy_ =
        wux::LineStackingStrategy_MaxHeight;
    InlineCollection inlines_{
        {::openxaml::iid::PIID_FIVector_1_Windows__CUI__CXaml__CDocuments__CInline,
         ::openxaml::iid::PIID_FIIterable_1_Windows__CUI__CXaml__CDocuments__CInline,
         ::openxaml::iid::PIID_FIIterator_1_Windows__CUI__CXaml__CDocuments__CInline},
        L"Windows.UI.Xaml.Documents.InlineCollection", this};
};

}  // namespace openxaml::winrt

#endif  // OPENXAML_ELEMENTS_H
