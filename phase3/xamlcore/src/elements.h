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
#include <memory>

#include "border.h"
#include "collection.h"
#include "com.h"
#include "properties.h"
#include "strings.h"
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
namespace wuxi = ABI::Windows::UI::Xaml::Input;
namespace wuxs = ABI::Windows::UI::Xaml::Shapes;

// --- SizeChangedEventArgs -----------------------------------------------------
//
// The one event-arguments class this DLL builds, because SizeChanged is one of
// the two events it raises. Not activatable and not registered: a caller never
// constructs one, it only receives one.

class SizeChangedEventArgsObject final : public ComObject,
                                         public abi::NotImpl_ISizeChangedEventArgs {
public:
    SizeChangedEventArgsObject(openxaml::Size previous, openxaml::Size current)
        : previous_(previous), current_(current) {}

    const wchar_t* RuntimeClassName() const override {
        return L"Windows.UI.Xaml.SizeChangedEventArgs";
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_ISizeChangedEventArgs,
                        wux::ISizeChangedEventArgs)
        OPENXAML_QI_ARM(IID_IUnknown, wux::ISizeChangedEventArgs)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable, wux::ISizeChangedEventArgs)
        *object = nullptr;
        return E_NOINTERFACE;
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE get_PreviousSize(wf::Size* value) override {
        if (!value) return E_POINTER;
        *value = {static_cast<FLOAT>(previous_.width), static_cast<FLOAT>(previous_.height)};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_NewSize(wf::Size* value) override {
        if (!value) return E_POINTER;
        *value = {static_cast<FLOAT>(current_.width), static_cast<FLOAT>(current_.height)};
        return S_OK;
    }

private:
    openxaml::Size previous_;
    openxaml::Size current_;
};

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

// --- the element base ---------------------------------------------------------

class XamlElement : public ComObject,
                    public abi::NotImpl_IDependencyObject,
                    public abi::NotImpl_IDependencyObject2,
                    public abi::NotImpl_IUIElement,
                    public abi::NotImpl_IFrameworkElement,
                    public IOpenXamlNative {
public:
    // The interface an activation factory returns this object as. Every
    // element is a UIElement; Grid's definitions are not, and say so.
    using PrimaryInterface = wux::IUIElement;

    virtual openxaml::Element* Layout() = 0;
    const openxaml::Element* Layout() const {
        return const_cast<XamlElement*>(this)->Layout();
    }

    openxaml::Element* LayoutElement() override { return Layout(); }

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

    // --- IDependencyObject ---
    //
    // The store these reach is the layout core's own, the one every accessor
    // above already reads: `put_Width` and `SetValue(WidthProperty, ...)` are
    // two spellings of one write, and the precedence chain -- animation over
    // local over style over inherited over default -- is the chain that
    // decides what the next Measure sees.

    HRESULT STDMETHODCALLTYPE GetValue(wux::IDependencyProperty* dp,
                                       IInspectable** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        const openxaml::DependencyProperty* property = NativeProperty(dp);
        if (!property) return E_INVALIDARG;
        return BoxPropertyValue(Layout()->GetValue(*property), result);
    }

    HRESULT STDMETHODCALLTYPE SetValue(wux::IDependencyProperty* dp,
                                       IInspectable* value) override {
        const openxaml::DependencyProperty* property = NativeProperty(dp);
        if (!property) return E_INVALIDARG;
        openxaml::PropertyValue unboxed;
        const HRESULT hr = UnboxPropertyValue(value, property->default_value(), &unboxed);
        if (FAILED(hr)) return hr;
        Layout()->SetValue(*property, std::move(unboxed));
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE ClearValue(wux::IDependencyProperty* dp) override {
        const openxaml::DependencyProperty* property = NativeProperty(dp);
        if (!property) return E_INVALIDARG;
        Layout()->ClearValue(*property);
        return S_OK;
    }

    // DependencyProperty.UnsetValue -- the very object
    // IDependencyPropertyStatics::get_UnsetValue hands out, so that the
    // caller's `== DependencyProperty.UnsetValue` is true -- for a property
    // whose value comes from a style, an ancestor or its default rather than
    // from this object.
    HRESULT STDMETHODCALLTYPE ReadLocalValue(wux::IDependencyProperty* dp,
                                             IInspectable** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        const openxaml::DependencyProperty* property = NativeProperty(dp);
        if (!property) return E_INVALIDARG;
        const openxaml::PropertyValue* local = Layout()->ReadLocalValue(*property);
        if (!local) {
            *result = UnsetValue();
            (*result)->AddRef();
            return S_OK;
        }
        return BoxPropertyValue(*local, result);
    }

    HRESULT STDMETHODCALLTYPE RegisterPropertyChangedCallback(
        wux::IDependencyProperty* dp, wux::IDependencyPropertyChangedCallback* callback,
        INT64* result) override {
        if (!result) return E_POINTER;
        *result = 0;
        const openxaml::DependencyProperty* property = NativeProperty(dp);
        if (!property || !callback) return E_INVALIDARG;
        callback->AddRef();
        // The projected property, not the one the caller passed: the callback
        // is handed the identity this DLL owns, which is the one it would get
        // from a *Property static.
        wux::IDependencyProperty* projected = ProjectProperty(*property);
        auto* self = static_cast<wux::IDependencyObject*>(this);
        const openxaml::DependencyObject::PropertyChangedToken token =
            Layout()->RegisterPropertyChangedCallback(
                *property,
                [callback, projected, self](openxaml::DependencyObject&,
                                            const openxaml::DependencyProperty&,
                                            const openxaml::PropertyValue&) {
                    callback->Invoke(self, projected);
                });
        callbacks_.emplace(static_cast<INT64>(token), callback);
        *result = static_cast<INT64>(token);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE UnregisterPropertyChangedCallback(wux::IDependencyProperty* dp,
                                                                INT64 token) override {
        const openxaml::DependencyProperty* property = NativeProperty(dp);
        if (!property) return E_INVALIDARG;
        Layout()->UnregisterPropertyChangedCallback(
            *property, static_cast<openxaml::DependencyObject::PropertyChangedToken>(token));
        const auto found = callbacks_.find(token);
        if (found != callbacks_.end()) {
            found->second->Release();
            callbacks_.erase(found);
        }
        return S_OK;
    }

    // --- events ---
    //
    // Every pair below stores a handler and hands back a token that takes it
    // off again. Two of them are raised -- see layout/src/events.h, which says
    // when, and from which lines of the published XAML core. The rest are
    // stored and never called, because this implementation has no live visual
    // tree and no input: a handler that would only ever be called at a moment
    // that does not exist here is better stored honestly than refused.

#define OPENXAML_EVENT(name, handler_type, event_kind)                                   \
    HRESULT STDMETHODCALLTYPE add_##name(handler_type* handler,                          \
                                         EventRegistrationToken* token) override {       \
        if (!token) return E_POINTER;                                                    \
        token->value = 0;                                                                \
        if (!handler) return E_INVALIDARG;                                               \
        return AddEventHandler(openxaml::FrameworkEvent::event_kind, handler, token);    \
    }                                                                                    \
    HRESULT STDMETHODCALLTYPE remove_##name(EventRegistrationToken token) override {     \
        return RemoveEventHandler(openxaml::FrameworkEvent::event_kind, token);          \
    }

    OPENXAML_EVENT(Loaded, wux::IRoutedEventHandler, Loaded)
    OPENXAML_EVENT(Unloaded, wux::IRoutedEventHandler, Unloaded)
    OPENXAML_EVENT(LayoutUpdated, __FIEventHandler_1_IInspectable, LayoutUpdated)
    OPENXAML_EVENT(PointerPressed, wuxi::IPointerEventHandler, PointerPressed)
    OPENXAML_EVENT(PointerReleased, wuxi::IPointerEventHandler, PointerReleased)
    OPENXAML_EVENT(PointerMoved, wuxi::IPointerEventHandler, PointerMoved)
    OPENXAML_EVENT(PointerEntered, wuxi::IPointerEventHandler, PointerEntered)
    OPENXAML_EVENT(PointerExited, wuxi::IPointerEventHandler, PointerExited)
    OPENXAML_EVENT(KeyDown, wuxi::IKeyEventHandler, KeyDown)
    OPENXAML_EVENT(KeyUp, wuxi::IKeyEventHandler, KeyUp)
    OPENXAML_EVENT(GotFocus, wux::IRoutedEventHandler, GotFocus)
    OPENXAML_EVENT(LostFocus, wux::IRoutedEventHandler, LostFocus)
    OPENXAML_EVENT(Tapped, wuxi::ITappedEventHandler, Tapped)
    OPENXAML_EVENT(DoubleTapped, wuxi::IDoubleTappedEventHandler, DoubleTapped)
    OPENXAML_EVENT(RightTapped, wuxi::IRightTappedEventHandler, RightTapped)
#undef OPENXAML_EVENT

    // SizeChanged is written out rather than macro'd, because it is the one
    // that is raised: its handler is typed, and the arguments it is given are
    // built here from the two sizes the layout pass recorded.
    HRESULT STDMETHODCALLTYPE add_SizeChanged(wux::ISizeChangedEventHandler* handler,
                                              EventRegistrationToken* token) override {
        if (!token) return E_POINTER;
        token->value = 0;
        if (!handler) return E_INVALIDARG;
        return AddEventHandler(openxaml::FrameworkEvent::SizeChanged, handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_SizeChanged(EventRegistrationToken token) override {
        return RemoveEventHandler(openxaml::FrameworkEvent::SizeChanged, token);
    }

    ~XamlElement() override {
        for (auto& [token, handler] : handlers_) {
            (void)token;
            handler->Release();
        }
        for (auto& [token, callback] : callbacks_) {
            (void)token;
            callback->Release();
        }
    }

protected:
    // What a raised event does with the delegate it stored. Only the two
    // events this implementation raises have an overload that calls anything;
    // every other handler type falls through to the one that does nothing,
    // which is the honest shape of "stored and never raised" -- the compiler
    // then cannot silently start calling a handler for an event whose firing
    // moment has not been established.
    static void InvokeHandler(::IUnknown*, wux::IDependencyObject*,
                              const openxaml::SizeChangedArgs&) {}

    static void InvokeHandler(wux::ISizeChangedEventHandler* handler,
                              wux::IDependencyObject* sender,
                              const openxaml::SizeChangedArgs& args) {
        auto* arguments = new SizeChangedEventArgsObject(args.previous, args.current);
        handler->Invoke(sender, arguments);
        static_cast<wux::ISizeChangedEventArgs*>(arguments)->Release();
    }

    static void InvokeHandler(__FIEventHandler_1_IInspectable* handler, wux::IDependencyObject*,
                              const openxaml::SizeChangedArgs&) {
        // Null sender and null arguments, as the reference raises it: the
        // layout manager passes a NULL target for LayoutUpdated, which is what
        // "every subscriber, about the pass rather than about an element"
        // looks like from the core side.
        handler->Invoke(nullptr, nullptr);
    }

    template <class Handler>
    HRESULT AddEventHandler(openxaml::FrameworkEvent event, Handler* handler,
                            EventRegistrationToken* token) {
        handler->AddRef();
        auto* sender = static_cast<wux::IDependencyObject*>(this);
        const openxaml::EventToken registered = Layout()->events().Add(
            event, [handler, sender](openxaml::Element&, openxaml::FrameworkEvent,
                                     const openxaml::SizeChangedArgs& args) {
                InvokeHandler(handler, sender, args);
            });
        if (registered == 0) {
            handler->Release();
            return E_FAIL;
        }
        handlers_.emplace(registered, handler);
        token->value = registered;
        return S_OK;
    }

    HRESULT RemoveEventHandler(openxaml::FrameworkEvent event, EventRegistrationToken token) {
        // A token that names nothing is not an error. remove_ on a stale token
        // is the caller's business, and the runtime does not fail it either.
        if (!Layout()->events().Remove(event, token.value)) return S_OK;
        const auto found = handlers_.find(token.value);
        if (found != handlers_.end()) {
            found->second->Release();
            handlers_.erase(found);
        }
        return S_OK;
    }

    // The interfaces every element carries. A concrete class tries its own
    // first, then falls through to here.
    HRESULT QueryElementInterface(REFIID iid, void** object) {
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_IUIElement, wux::IUIElement)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_IFrameworkElement, wux::IFrameworkElement)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_IDependencyObject, wux::IDependencyObject)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_IDependencyObject2, wux::IDependencyObject2)
        OPENXAML_QI_ARM(IID_IUnknown, wux::IUIElement)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable, wux::IUIElement)
        if (IsEqualGUID(iid, IID_IOpenXamlNative)) {
            auto* pointer = static_cast<IOpenXamlNative*>(this);
            static_cast<wux::IUIElement*>(this)->AddRef();
            *object = pointer;
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }

private:
    // The delegates this element holds a reference to, by the token the
    // registration was given. The native store holds the lambda that calls
    // them; this holds the reference that keeps them alive, because a COM
    // object handed to us across the ABI is only ours while we count it.
    std::map<openxaml::EventToken, ::IUnknown*> handlers_;
    std::map<INT64, wux::IDependencyPropertyChangedCallback*> callbacks_;
};

// --- Border -------------------------------------------------------------------

class BorderObject final : public XamlElement, public abi::NotImpl_IBorder {
public:
    BorderObject() { children_.source = &child_holder_; }

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

private:
    AbiBorder children_;
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

    openxaml::Element* Layout() override { return &layout_; }

    HRESULT STDMETHODCALLTYPE get_Children(
        __FIVector_1_Windows__CUI__CXaml__CUIElement** value) override {
        if (!value) return E_POINTER;
        children_.AddRef();
        *value = &children_;
        return S_OK;
    }

protected:
    HRESULT QueryPanelInterface(REFIID iid, void** object) {
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IPanel, wuxc::IPanel)
        return QueryElementInterface(iid, object);
    }

    LayoutType layout_;
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

class ContentPresenterObject final : public XamlElement,
                                     public abi::NotImpl_IContentPresenter {
public:
    using PrimaryInterface = wuxc::IContentPresenter;
    ContentPresenterObject() { layout_.source = &content_; }
    openxaml::Element* Layout() override { return &layout_; }
    const wchar_t* RuntimeClassName() const override {
        return L"Windows.UI.Xaml.Controls.ContentPresenter";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IContentPresenter,
                        wuxc::IContentPresenter)
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
private:
    ChildSourced<openxaml::ContentPresenter> layout_;
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
    HRESULT STDMETHODCALLTYPE ApplyTemplate(boolean* result) override {
        if (!result) return E_POINTER;
        *result = layout_.ApplyTemplate() ? 1 : 0;
        return S_OK;
    }

protected:
    HRESULT QueryControlInterface(REFIID iid, void** object) {
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IContentControl,
                        wuxc::IContentControl)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IControl, wuxc::IControl)
        return QueryElementInterface(iid, object);
    }

    ChildSourced<LayoutType> layout_;
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

// --- remaining Terminal-facing Windows.UI.Xaml controls ----------------------

class ButtonObject final : public ContentControlObjectBase<openxaml::Button>,
                           public abi::NotImpl_IButton {
public:
    using PrimaryInterface = wuxc::IButton;
    const wchar_t* RuntimeClassName() const override { return L"Windows.UI.Xaml.Controls.Button"; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IButton, wuxc::IButton)
        return QueryControlInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()
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

class FontIconObject final : public XamlElement, public abi::NotImpl_IFontIcon {
public:
    using PrimaryInterface = wuxc::IFontIcon;
    openxaml::Element* Layout() override { return &layout_; }
    const wchar_t* RuntimeClassName() const override { return L"Windows.UI.Xaml.Controls.FontIcon"; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IFontIcon, wuxc::IFontIcon)
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
private:
    openxaml::FontIcon layout_;
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

class TextBlockObject final : public XamlElement, public abi::NotImpl_ITextBlock {
public:
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
        family->AddRef();
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
                // WrapWholeWords. Refused rather than approximated, for the
                // reason given in layout/src/markup.cpp.
                return E_NOTIMPL;
        }
    }

private:
    openxaml::TextBlock text_;
};

}  // namespace openxaml::winrt

#endif  // OPENXAML_ELEMENTS_H
