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

#include <algorithm>
#include <cmath>
#include <exception>
#include <map>
#include <memory>
#include <roapi.h>
#include <set>

#include "border.h"
#include "collection.h"
#include "com.h"
#include "core_dispatcher.h"
#include "external_surface_binding.h"
#include "properties.h"
#include "strings.h"
#include "grid.h"
#include "openxaml_abi_stubs.h"
#include "stack_panel.h"
#include "text.h"
#include "xaml_focus.h"
#include "advanced_controls.h"
#include "basic_controls.h"
#include "canvas.h"
#include "content_presenter.h"
#include "icon.h"
#include "image.h"
#include "scroll_viewer.h"
#include "shape.h"

namespace openxaml::winrt {

inline void TraceLayoutException(const char* stage, const char* detail) noexcept {
    char line[768]{};
    const char* prefix = "OpenXaml: island ";
    std::size_t used = 0;
    auto append = [&](const char* source) {
        if (!source) source = "unknown C++ exception";
        while (*source && used + 2 < sizeof(line)) {
            const unsigned char value = static_cast<unsigned char>(*source++);
            line[used++] = value >= 0x20 && value != 0x7f
                ? static_cast<char>(value)
                : ' ';
        }
    };
    append(prefix);
    append(stage);
    append(" degraded: ");
    append(detail);
    line[used++] = '\n';
    line[used] = '\0';
    TraceRuntime(line);
}

namespace wf = ABI::Windows::Foundation;
namespace wux = ABI::Windows::UI::Xaml;
namespace wuxc = ABI::Windows::UI::Xaml::Controls;
namespace wuxm = ABI::Windows::UI::Xaml::Media;
namespace wuxcp = ABI::Windows::UI::Xaml::Controls::Primitives;
namespace wuxd = ABI::Windows::UI::Xaml::Documents;
namespace wuxdata = ABI::Windows::UI::Xaml::Data;
namespace wuxs = ABI::Windows::UI::Xaml::Shapes;
namespace wuxi = ABI::Windows::UI::Xaml::Input;

// Implemented beside ResourceDictionaryObject in factory.cpp. Keeping this
// construction inside OpenXaml avoids sending our own XAML types back through
// the host's WinRT registry (Wine and ReactOS do not register system XAML).
HRESULT CreateResourceDictionary(wux::IResourceDictionary** value);

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

// RangeBase raises this routed event whenever coercion changes the effective
// Value. Keep OriginalSource strongly held because a handler may retain the
// arguments after the synchronous event dispatch has returned.
class RangeBaseValueChangedEventArgsObject final
    : public ComObject,
      public abi::NotImpl_IRangeBaseValueChangedEventArgs,
      public abi::NotImpl_IRoutedEventArgs {
public:
    RangeBaseValueChangedEventArgsObject(IInspectable* source,
                                         double old_value,
                                         double new_value)
        : source_(source), old_value_(old_value), new_value_(new_value) {
        if (source_) source_->AddRef();
    }
    ~RangeBaseValueChangedEventArgsObject() override {
        if (source_) source_->Release();
    }

    const wchar_t* RuntimeClassName() const override {
        return L"Windows.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(
            ::openxaml::iid::Windows_UI_Xaml_Controls_Primitives_IRangeBaseValueChangedEventArgs,
            wuxcp::IRangeBaseValueChangedEventArgs)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_IRoutedEventArgs,
                        wux::IRoutedEventArgs)
        OPENXAML_QI_ARM(IID_IUnknown,
                        wuxcp::IRangeBaseValueChangedEventArgs)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable,
                        wuxcp::IRangeBaseValueChangedEventArgs)
        *object = nullptr;
        return E_NOINTERFACE;
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE get_OldValue(DOUBLE* value) override {
        if (!value) return E_POINTER;
        *value = old_value_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_NewValue(DOUBLE* value) override {
        if (!value) return E_POINTER;
        *value = new_value_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_OriginalSource(IInspectable** value) override {
        if (!value) return E_POINTER;
        *value = source_;
        if (*value) (*value)->AddRef();
        return S_OK;
    }

private:
    IInspectable* source_ = nullptr;
    double old_value_ = 0.0;
    double new_value_ = 0.0;
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
    const std::vector<openxaml::Element*>* supplemental = nullptr;
    bool include_source = true;
    std::vector<openxaml::Element*> Children() const override {
        std::vector<openxaml::Element*> result;
        if (include_source && source) result = source->Projected();
        if (supplemental) {
            result.insert(result.end(), supplemental->begin(), supplemental->end());
        }
        return result;
    }
};

// Small template-less controls need text and an opaque backing in one visual
// for DirectWrite ClearType. A normal TextBlock reports its glyph ink from
// ArrangeOverride even when its slot is larger; this synthetic leaf instead
// retains the fixed backing box it was assigned.
class OpaqueSyntheticTextBlock final : public openxaml::TextBlock {
public:
    void set_box_size(openxaml::Size value) {
        box_size_ = value;
        set_min_width(value.width);
        set_min_height(value.height);
    }

protected:
    openxaml::Size MeasureOverride(openxaml::Size available) override {
        const openxaml::Size measured = openxaml::TextBlock::MeasureOverride(available);
        return {std::max(measured.width, box_size_.width),
                std::max(measured.height, box_size_.height)};
    }
    openxaml::Size ArrangeOverride(openxaml::Size final_size) override {
        return {std::max(final_size.width, box_size_.width),
                std::max(final_size.height, box_size_.height)};
    }

private:
    openxaml::Size box_size_{};
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

class TransformMutationObserver {
public:
    virtual ~TransformMutationObserver() = default;
    virtual void TransformValueChanged(const openxaml::VisualTransform& value) = 0;
};

// {6F70656E-7861-6D6C-9E05-7472616E7366}
inline constexpr GUID IID_IOpenXamlTransformSource = {
    0x6f70656e, 0x7861, 0x6d6c,
    {0x9e, 0x05, 0x74, 0x72, 0x61, 0x6e, 0x73, 0x66}};

struct IOpenXamlTransformSource : IUnknown {
    virtual HRESULT Subscribe(TransformMutationObserver* observer, LONGLONG* token,
                              openxaml::VisualTransform* current) = 0;
    virtual void Unsubscribe(LONGLONG token) = 0;
};

class ScaleTransformObject final : public ComObject,
                                   public abi::NotImpl_IDependencyObject,
                                   public wuxm::IGeneralTransform,
                                   public wuxm::ITransform,
                                   public wuxm::IScaleTransform,
                                   public IOpenXamlTransformSource {
public:
    using PrimaryInterface = wuxm::IScaleTransform;
    explicit ScaleTransformObject(double scale_x = 1.0, double scale_y = 1.0,
                                  double center_x = 0.0, double center_y = 0.0)
        : center_x_(center_x), center_y_(center_y), scale_x_(scale_x), scale_y_(scale_y) {}
    const wchar_t* RuntimeClassName() const override {
        return L"Windows.UI.Xaml.Media.ScaleTransform";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Media_IScaleTransform,
                        wuxm::IScaleTransform)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Media_ITransform,
                        wuxm::ITransform)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Media_IGeneralTransform,
                        wuxm::IGeneralTransform)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_IDependencyObject,
                        wux::IDependencyObject)
        OPENXAML_QI_ARM(IID_IOpenXamlTransformSource, IOpenXamlTransformSource)
        OPENXAML_QI_ARM(IID_IUnknown, wuxm::IScaleTransform)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable, wuxm::IScaleTransform)
        *object = nullptr;
        return TraceQueryInterfaceMiss(RuntimeClassName(), iid);
    }
    OPENXAML_COM_BOILERPLATE()

#define OPENXAML_SCALE_PROPERTY(name, field)                         \
    HRESULT STDMETHODCALLTYPE get_##name(DOUBLE* value) override {  \
        if (!value) return E_POINTER;                               \
        *value = field;                                             \
        return S_OK;                                                \
    }                                                               \
    HRESULT STDMETHODCALLTYPE put_##name(DOUBLE value) override {   \
        if (std::isnan(value)) value = 0.0;                         \
        if (!std::isfinite(value)) return E_INVALIDARG;             \
        if (field == value) return S_OK;                            \
        field = value;                                              \
        NotifyObservers();                                          \
        return S_OK;                                                \
    }
    OPENXAML_SCALE_PROPERTY(CenterX, center_x_)
    OPENXAML_SCALE_PROPERTY(CenterY, center_y_)
    OPENXAML_SCALE_PROPERTY(ScaleX, scale_x_)
    OPENXAML_SCALE_PROPERTY(ScaleY, scale_y_)
#undef OPENXAML_SCALE_PROPERTY

    HRESULT STDMETHODCALLTYPE get_Inverse(wuxm::IGeneralTransform** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (scale_x_ == 0.0 || scale_y_ == 0.0) return S_OK;
        auto* inverse = new (std::nothrow) ScaleTransformObject(
            1.0 / scale_x_, 1.0 / scale_y_, center_x_, center_y_);
        if (!inverse) return E_OUTOFMEMORY;
        *value = static_cast<wuxm::IGeneralTransform*>(inverse);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE TransformPoint(wf::Point point, wf::Point* value) override {
        if (!value) return E_POINTER;
        value->X = static_cast<FLOAT>(center_x_ + (point.X - center_x_) * scale_x_);
        value->Y = static_cast<FLOAT>(center_y_ + (point.Y - center_y_) * scale_y_);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE TryTransform(wf::Point point, wf::Point* value,
                                           boolean* transformed) override {
        if (!value || !transformed) return E_POINTER;
        *transformed = 1;
        return TransformPoint(point, value);
    }
    HRESULT STDMETHODCALLTYPE TransformBounds(wf::Rect rect, wf::Rect* value) override {
        if (!value) return E_POINTER;
        wf::Point first{rect.X, rect.Y};
        wf::Point second{rect.X + rect.Width, rect.Y + rect.Height};
        TransformPoint(first, &first);
        TransformPoint(second, &second);
        value->X = std::min(first.X, second.X);
        value->Y = std::min(first.Y, second.Y);
        value->Width = std::abs(second.X - first.X);
        value->Height = std::abs(second.Y - first.Y);
        return S_OK;
    }
    HRESULT Subscribe(TransformMutationObserver* observer, LONGLONG* token,
                      openxaml::VisualTransform* current) override {
        if (!observer || !token || !current) return E_INVALIDARG;
        const LONGLONG next = ++next_observer_token_;
        observers_[next] = observer;
        *token = next;
        *current = Snapshot();
        return S_OK;
    }
    void Unsubscribe(LONGLONG token) override { observers_.erase(token); }

private:
    openxaml::VisualTransform Snapshot() const {
        return openxaml::VisualTransform::Scale(scale_x_, scale_y_, center_x_, center_y_);
    }
    void NotifyObservers() {
        const openxaml::VisualTransform value = Snapshot();
        std::vector<LONGLONG> tokens;
        tokens.reserve(observers_.size());
        for (const auto& [token, _] : observers_) tokens.push_back(token);
        for (LONGLONG token : tokens) {
            const auto found = observers_.find(token);
            if (found != observers_.end()) found->second->TransformValueChanged(value);
        }
    }
    DOUBLE center_x_ = 0.0;
    DOUBLE center_y_ = 0.0;
    DOUBLE scale_x_ = 1.0;
    DOUBLE scale_y_ = 1.0;
    LONGLONG next_observer_token_ = 0;
    std::map<LONGLONG, TransformMutationObserver*> observers_;
};

// Private live-brush boundary. A projected element keeps the public IBrush
// alive and subscribes to mutable brush state through this interface. The
// observer is deliberately non-owning: the projection unsubscribes before it
// releases the brush, preventing a brush -> element -> brush cycle.
class BrushMutationObserver {
public:
    virtual ~BrushMutationObserver() = default;
    virtual void BrushValueChanged(const openxaml::BrushValue& value) = 0;
};

// {6F70656E-7861-6D6C-9E03-627275736873}
inline constexpr GUID IID_IOpenXamlBrushSource = {
    0x6f70656e, 0x7861, 0x6d6c, {0x9e, 0x03, 0x62, 0x72, 0x75, 0x73, 0x68, 0x73}};

struct IOpenXamlBrushSource : IUnknown {
    virtual HRESULT Subscribe(BrushMutationObserver* observer, LONGLONG* token,
                              openxaml::BrushValue* current) = 0;
    virtual void Unsubscribe(LONGLONG token) = 0;
};

class ImageBrushObject final : public ComObject,
                               public abi::NotImpl_IDependencyObject,
                               public abi::NotImpl_IBrush,
                               public abi::NotImpl_IImageBrush,
                               public IOpenXamlBrushSource {
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
        OPENXAML_QI_ARM(IID_IOpenXamlBrushSource, IOpenXamlBrushSource)
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
        if (!std::isfinite(value) || value < 0.0 || value > 1.0) return E_INVALIDARG;
        opacity_ = value;
        NotifyObservers();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Transform(wuxm::ITransform** value) override {
        return Get(transform_, value);
    }
    HRESULT STDMETHODCALLTYPE put_Transform(wuxm::ITransform* value) override {
        const HRESULT hr = Put(transform_, value);
        if (SUCCEEDED(hr)) NotifyObservers();
        return hr;
    }
    HRESULT STDMETHODCALLTYPE get_RelativeTransform(wuxm::ITransform** value) override {
        return Get(relative_transform_, value);
    }
    HRESULT STDMETHODCALLTYPE put_RelativeTransform(wuxm::ITransform* value) override {
        const HRESULT hr = Put(relative_transform_, value);
        if (SUCCEEDED(hr)) NotifyObservers();
        return hr;
    }
    HRESULT STDMETHODCALLTYPE get_ImageSource(wuxm::IImageSource** value) override {
        return Get(source_, value);
    }
    HRESULT STDMETHODCALLTYPE put_ImageSource(wuxm::IImageSource* value) override {
        const HRESULT hr = Put(source_, value);
        if (SUCCEEDED(hr)) NotifyObservers();
        return hr;
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
    HRESULT Subscribe(BrushMutationObserver* observer, LONGLONG* token,
                      openxaml::BrushValue* current) override {
        if (!observer || !token || !current) return E_INVALIDARG;
        *token = 0;
        const LONGLONG next = ++next_observer_token_;
        observers_[next] = observer;
        *token = next;
        *current = ProjectedValue();
        return S_OK;
    }
    void Unsubscribe(LONGLONG token) override { observers_.erase(token); }

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
    openxaml::BrushValue ProjectedValue() const {
        // Null ImageSource is the ImageBrush default and paints nothing,
        // independent of opacity, stretch or transforms. A non-null source is
        // retained as present, but resource identity/decoding remains a named
        // renderer boundary until the scene owns the IImageSource snapshot.
        return openxaml::BrushValue::Image(source_ != nullptr);
    }
    void NotifyObservers() {
        const openxaml::BrushValue value = ProjectedValue();
        std::vector<LONGLONG> snapshot;
        snapshot.reserve(observers_.size());
        for (const auto& [token, _] : observers_) snapshot.push_back(token);
        for (LONGLONG token : snapshot) {
            const auto current = observers_.find(token);
            if (current != observers_.end())
                current->second->BrushValueChanged(value);
        }
    }

    DOUBLE opacity_ = 1.0;
    wuxm::IImageSource* source_ = nullptr;
    wuxm::ITransform* transform_ = nullptr;
    wuxm::ITransform* relative_transform_ = nullptr;
    LONGLONG next_token_ = 0;
    std::map<LONGLONG, IUnknown*> handlers_;
    LONGLONG next_observer_token_ = 0;
    std::map<LONGLONG, BrushMutationObserver*> observers_;
};

class SolidColorBrushObject final : public ComObject,
                                    public abi::NotImpl_IDependencyObject,
                                    public abi::NotImpl_IBrush,
                                    public abi::NotImpl_ISolidColorBrush,
                                    public IOpenXamlBrushSource {
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
        OPENXAML_QI_ARM(IID_IOpenXamlBrushSource, IOpenXamlBrushSource)
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
        NotifyObservers();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Opacity(DOUBLE* value) override {
        if (!value) return E_POINTER;
        *value = opacity_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Opacity(DOUBLE value) override {
        if (!std::isfinite(value) || value < 0.0 || value > 1.0) return E_INVALIDARG;
        opacity_ = value;
        NotifyObservers();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Transform(wuxm::ITransform** value) override {
        return Get(transform_, value);
    }
    HRESULT STDMETHODCALLTYPE put_Transform(wuxm::ITransform* value) override {
        const HRESULT hr = Put(transform_, value);
        if (SUCCEEDED(hr)) NotifyObservers();
        return hr;
    }
    HRESULT STDMETHODCALLTYPE get_RelativeTransform(wuxm::ITransform** value) override {
        return Get(relative_transform_, value);
    }
    HRESULT STDMETHODCALLTYPE put_RelativeTransform(wuxm::ITransform* value) override {
        const HRESULT hr = Put(relative_transform_, value);
        if (SUCCEEDED(hr)) NotifyObservers();
        return hr;
    }
    HRESULT Subscribe(BrushMutationObserver* observer, LONGLONG* token,
                      openxaml::BrushValue* current) override {
        if (!observer || !token || !current) return E_INVALIDARG;
        *token = 0;
        const LONGLONG next = ++next_observer_token_;
        observers_[next] = observer;
        *token = next;
        *current = ProjectedValue();
        return S_OK;
    }
    void Unsubscribe(LONGLONG token) override {
        observers_.erase(token);
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
    openxaml::BrushValue ProjectedValue() const {
        // A transformed solid brush is not an axis-aligned solid fill. Keep it
        // declared but colourless so the display-list builder emits its named
        // unsupported-brush refusal instead of discarding the transform.
        if (transform_ || relative_transform_)
            return openxaml::BrushValue{true, false, {}};
        const auto alpha = static_cast<unsigned char>(
            std::floor(static_cast<double>(color_.A) * opacity_ + 0.5));
        return openxaml::BrushValue{
            true, true, {alpha, color_.R, color_.G, color_.B}};
    }
    void NotifyObservers() {
        const openxaml::BrushValue value = ProjectedValue();
        std::vector<LONGLONG> snapshot;
        snapshot.reserve(observers_.size());
        for (const auto& [token, _] : observers_) snapshot.push_back(token);
        for (LONGLONG token : snapshot) {
            const auto current = observers_.find(token);
            if (current != observers_.end())
                current->second->BrushValueChanged(value);
        }
    }
    ABI::Windows::UI::Color color_{255, 0, 0, 0};
    DOUBLE opacity_ = 1.0;
    wuxm::ITransform* transform_ = nullptr;
    wuxm::ITransform* relative_transform_ = nullptr;
    LONGLONG next_observer_token_ = 0;
    std::map<LONGLONG, BrushMutationObserver*> observers_;
};

enum class ProjectedBrushSlot { Background, Border, Fill, Stroke };

// Owns one element brush assignment and its optional live subscription.
// Member declaration order is important at call sites: declare the layout
// element before this projection so this destructor detaches while the target
// still exists.
class BrushProjection final : private BrushMutationObserver {
public:
    BrushProjection(openxaml::Element& element, ProjectedBrushSlot slot)
        : element_(element), slot_(slot) {}
    ~BrushProjection() { Reset(); }

    BrushProjection(const BrushProjection&) = delete;
    BrushProjection& operator=(const BrushProjection&) = delete;

    HRESULT Assign(wuxm::IBrush* value) {
        // Retain first: assigning the same interface again must not release the
        // object's final reference while replacing its subscription.
        if (value) value->AddRef();
        Reset();
        brush_ = value;

        if (!brush_) {
            Publish(openxaml::BrushValue{});
            return S_OK;
        }

        HRESULT hr = brush_->QueryInterface(
            IID_IOpenXamlBrushSource, reinterpret_cast<void**>(&source_));
        if (FAILED(hr)) {
            // Any other Brush remains present but unsupported. This is the
            // distinction DisplayList uses for a named refusal.
            Publish(openxaml::BrushValue{true, false, {}});
            return S_OK;
        }

        openxaml::BrushValue current;
        hr = source_->Subscribe(this, &token_, &current);
        if (FAILED(hr)) {
            source_->Release();
            source_ = nullptr;
            Publish(openxaml::BrushValue{true, false, {}});
            return hr;
        }
        Publish(current);
        return S_OK;
    }

    HRESULT Get(wuxm::IBrush** value) const {
        if (!value) return E_POINTER;
        *value = brush_;
        if (*value) (*value)->AddRef();
        return S_OK;
    }

private:
    void Reset() {
        if (source_) {
            source_->Unsubscribe(token_);
            source_->Release();
            source_ = nullptr;
            token_ = 0;
        }
        if (brush_) {
            brush_->Release();
            brush_ = nullptr;
        }
    }

    void BrushValueChanged(const openxaml::BrushValue& value) override {
        Publish(value);
    }

    void Publish(openxaml::BrushValue value) {
        switch (slot_) {
            case ProjectedBrushSlot::Background:
                element_.set_background_brush(value);
                break;
            case ProjectedBrushSlot::Border:
                element_.set_border_brush(value);
                break;
            case ProjectedBrushSlot::Fill:
                element_.set_fill_brush(value);
                break;
            case ProjectedBrushSlot::Stroke:
                element_.set_stroke_brush(value);
                break;
        }
        element_.InvalidateRender(false);
    }

    openxaml::Element& element_;
    ProjectedBrushSlot slot_;
    wuxm::IBrush* brush_ = nullptr;
    IOpenXamlBrushSource* source_ = nullptr;
    LONGLONG token_ = 0;
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
                    public IOpenXamlNative,
                    public IOpenXamlNameScopeOwner,
                    public XamlFocusTarget,
                    private TransformMutationObserver {
public:
    // The interface an activation factory returns this object as. Every
    // element is a UIElement; Grid's definitions are not, and say so.
    using PrimaryInterface = wux::IUIElement;
    using GettingFocusHandler =
        __FITypedEventHandler_2_Windows__CUI__CXaml__CUIElement_Windows__CUI__CXaml__CInput__CGettingFocusEventArgs;
    using LosingFocusHandler =
        __FITypedEventHandler_2_Windows__CUI__CXaml__CUIElement_Windows__CUI__CXaml__CInput__CLosingFocusEventArgs;

    XamlElement()
        : weak_state_(std::make_shared<WeakReferenceState>(
              this, static_cast<wux::IUIElement*>(this))),
          owner_thread_id_(GetCurrentThreadId()) {
        // DependencyObject.Dispatcher is the dispatcher of the creating UI
        // thread. Capture it now so a background getter never manufactures a
        // dispatcher for the wrong thread.
        (void)GetCoreDispatcherForCurrentThread(&owner_dispatcher_);
    }

    ~XamlElement() override {
        ResetRenderTransform();
        UnregisterXamlFocusTarget(*this);
        if (name_scope_ && name_) {
            (void)name_scope_->Unregister(
                name_, static_cast<IInspectable*>(static_cast<wux::IUIElement*>(this)));
        }
        if (name_scope_) name_scope_->Release();
        WindowsDeleteString(name_);
        weak_state_->Invalidate();
        if (owner_dispatcher_) owner_dispatcher_->Release();
        if (resources_) resources_->Release();
        if (context_flyout_) context_flyout_->Release();
        if (xaml_root_) xaml_root_->Release();
        if (shadow_) shadow_->Release();
        for (auto& [_, value] : automation_strings_)
            WindowsDeleteString(value);
        for (auto& [_, handler] : event_handlers_) handler->Release();
        for (auto& [_, handler] : got_focus_handlers_) handler->Release();
        for (auto& [_, handler] : lost_focus_handlers_) handler->Release();
        for (auto& [_, handler] : getting_focus_handlers_) handler->Release();
        for (auto& [_, handler] : losing_focus_handlers_) handler->Release();
        for (auto& [_, handler] : preview_key_down_handlers_) handler->Release();
        for (auto& [_, handler] : key_down_handlers_) handler->Release();
        for (auto& [_, handler] : preview_key_up_handlers_) handler->Release();
        for (auto& [_, handler] : key_up_handlers_) handler->Release();
        for (auto& [_, handler] : character_handlers_) handler->Release();
        for (auto& [_, handler] : pointer_pressed_handlers_) handler->Release();
        for (auto& [_, handler] : pointer_moved_handlers_) handler->Release();
        for (auto& [_, handler] : pointer_released_handlers_) handler->Release();
        for (auto& [_, handler] : pointer_entered_handlers_) handler->Release();
        for (auto& [_, handler] : pointer_exited_handlers_) handler->Release();
        for (auto& [_, handler] : pointer_capture_lost_handlers_) handler->Release();
        for (auto& [_, handler] : pointer_canceled_handlers_) handler->Release();
        for (auto& [_, handler] : pointer_wheel_handlers_) handler->Release();
        for (auto& [_, handler] : tapped_handlers_) handler->Release();
        for (auto& [_, handler] : double_tapped_handlers_) handler->Release();
        for (auto& [_, handler] : loaded_handlers_) handler->Release();
        for (auto& [_, handler] : handlers_) handler->Release();
        for (auto& [_, callback] : callbacks_) callback->Release();
    }

    virtual openxaml::Element* Layout() = 0;
    const openxaml::Element* Layout() const {
        return const_cast<XamlElement*>(this)->Layout();
    }

    HRESULT STDMETHODCALLTYPE get_RenderTransform(wuxm::ITransform** value) override {
        if (!value) return E_POINTER;
        *value = render_transform_;
        if (*value) (*value)->AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_RenderTransform(wuxm::ITransform* value) override {
        if (value) value->AddRef();
        ResetRenderTransform();
        render_transform_ = value;
        if (!value) {
            Layout()->set_visual_transform({});
            return S_OK;
        }

        HRESULT hr = value->QueryInterface(
            IID_IOpenXamlTransformSource,
            reinterpret_cast<void**>(&render_transform_source_));
        if (SUCCEEDED(hr)) {
            openxaml::VisualTransform current;
            hr = render_transform_source_->Subscribe(
                this, &render_transform_token_, &current);
            if (FAILED(hr)) {
                render_transform_source_->Release();
                render_transform_source_ = nullptr;
                return hr;
            }
            Layout()->set_visual_transform(std::move(current));
            return S_OK;
        }

        HSTRING runtime_name = nullptr;
        std::string type = "Windows.UI.Xaml.Media.Transform";
        if (SUCCEEDED(value->GetRuntimeClassName(&runtime_name)) && runtime_name) {
            UINT32 length = 0;
            const wchar_t* wide = WindowsGetStringRawBuffer(runtime_name, &length);
            type.clear();
            type.reserve(length);
            for (UINT32 index = 0; index < length; ++index)
                type.push_back(wide[index] <= 0x7f ? static_cast<char>(wide[index]) : '?');
            WindowsDeleteString(runtime_name);
        }
        Layout()->set_visual_transform(openxaml::VisualTransform::Unsupported(std::move(type)));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_RenderTransformOrigin(wf::Point* value) override {
        if (!value) return E_POINTER;
        const openxaml::Point origin = Layout()->render_transform_origin();
        *value = {static_cast<FLOAT>(origin.x), static_cast<FLOAT>(origin.y)};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_RenderTransformOrigin(wf::Point value) override {
        if (!std::isfinite(value.X) || !std::isfinite(value.Y)) return E_INVALIDARG;
        Layout()->set_render_transform_origin({value.X, value.Y});
        return S_OK;
    }

    openxaml::Element* LayoutElement() override {
        // Native ABI consumers such as FocusManager may request focus before
        // this element has any event registrations. Publishing the projection
        // here gives Element*-based focus lookup a lifetime-bounded identity.
        RegisterXamlFocusTarget(*this);
        return Layout();
    }
    openxaml::Element* FocusLayoutElement() noexcept override { return Layout(); }
    bool HasFocusThreadAccess() const noexcept override {
        return GetCurrentThreadId() == owner_thread_id_;
    }
    void RetainFocusTarget() noexcept override {
        static_cast<wux::IUIElement*>(this)->AddRef();
    }
    void ReleaseFocusTarget() noexcept override {
        static_cast<wux::IUIElement*>(this)->Release();
    }
    HRESULT CopyFocusInspectable(IInspectable** value) noexcept override {
        if (!value) return E_POINTER;
        *value = static_cast<IInspectable*>(static_cast<wux::IUIElement*>(this));
        (*value)->AddRef();
        return S_OK;
    }
    HRESULT CopyOwnXamlRoot(wux::IXamlRoot** value) noexcept override {
        if (!value) return E_POINTER;
        *value = xaml_root_;
        if (*value) (*value)->AddRef();
        return S_OK;
    }
    void SetIslandFocusState(wux::FocusState state) noexcept override {
        focus_state_ = state;
    }
    void InvokeIslandFocusEvent(bool gained) noexcept override {
        try {
        auto& source = gained ? got_focus_handlers_ : lost_focus_handlers_;
        std::vector<wux::IRoutedEventHandler*> snapshot;
        snapshot.reserve(source.size());
        for (const auto& [_, handler] : source) {
            handler->AddRef();
            snapshot.push_back(handler);
        }
        IInspectable* const sender = static_cast<IInspectable*>(
            static_cast<wux::IUIElement*>(this));
        for (wux::IRoutedEventHandler* handler : snapshot) {
            (void)handler->Invoke(sender, nullptr);
            handler->Release();
        }
        } catch (...) {
            // Routed input/focus callbacks are HWND-bound noexcept seams.
        }
    }
    void InvokeIslandLosingFocus(
        wuxi::ILosingFocusEventArgs* args) noexcept override {
        InvokeFocusChanging(losing_focus_handlers_, args);
    }
    void InvokeIslandGettingFocus(
        wuxi::IGettingFocusEventArgs* args) noexcept override {
        InvokeFocusChanging(getting_focus_handlers_, args);
    }
    void InvokeIslandKeyEvent(
        bool preview, bool key_down,
        wuxi::IKeyRoutedEventArgs* args) noexcept override {
        try {
        auto& source = preview
            ? (key_down ? preview_key_down_handlers_ : preview_key_up_handlers_)
            : (key_down ? key_down_handlers_ : key_up_handlers_);
        std::vector<wuxi::IKeyEventHandler*> snapshot;
        snapshot.reserve(source.size());
        for (const auto& [_, handler] : source) {
            handler->AddRef();
            snapshot.push_back(handler);
        }
        IInspectable* const sender = static_cast<IInspectable*>(
            static_cast<wux::IUIElement*>(this));
        for (wuxi::IKeyEventHandler* handler : snapshot) {
            (void)handler->Invoke(sender, args);
            boolean handled = 0;
            if (SUCCEEDED(args->get_Handled(&handled)) && handled) break;
        }
        for (wuxi::IKeyEventHandler* handler : snapshot) handler->Release();
        } catch (...) {
        }
    }
    void InvokeIslandCharacterEvent(
        wuxi::ICharacterReceivedRoutedEventArgs* args) noexcept override {
        try {
        using CharacterHandler =
            __FITypedEventHandler_2_Windows__CUI__CXaml__CUIElement_Windows__CUI__CXaml__CInput__CCharacterReceivedRoutedEventArgs;
        std::vector<CharacterHandler*> snapshot;
        snapshot.reserve(character_handlers_.size());
        for (const auto& [_, handler] : character_handlers_) {
            handler->AddRef();
            snapshot.push_back(handler);
        }
        wux::IUIElement* const sender = static_cast<wux::IUIElement*>(this);
        for (CharacterHandler* handler : snapshot) {
            (void)handler->Invoke(sender, args);
            boolean handled = 0;
            if (SUCCEEDED(args->get_Handled(&handled)) && handled) break;
        }
        for (CharacterHandler* handler : snapshot) handler->Release();
        } catch (...) {
        }
    }
    void InvokeIslandPointerEvent(
        IslandPointerEventKind kind,
        wuxi::IPointerRoutedEventArgs* args) noexcept override {
        try {
        std::map<LONGLONG, wuxi::IPointerEventHandler*>* source = nullptr;
        switch (kind) {
            case IslandPointerEventKind::Pressed:
                source = &pointer_pressed_handlers_;
                break;
            case IslandPointerEventKind::Moved:
                source = &pointer_moved_handlers_;
                break;
            case IslandPointerEventKind::Released:
                source = &pointer_released_handlers_;
                break;
            case IslandPointerEventKind::Wheel:
                source = &pointer_wheel_handlers_;
                break;
            case IslandPointerEventKind::Entered:
                source = &pointer_entered_handlers_;
                break;
            case IslandPointerEventKind::Exited:
                source = &pointer_exited_handlers_;
                break;
            case IslandPointerEventKind::Canceled:
                source = &pointer_canceled_handlers_;
                break;
            case IslandPointerEventKind::CaptureLost:
                source = &pointer_capture_lost_handlers_;
                break;
        }
        if (!source || !args) return;
        if (kind == IslandPointerEventKind::CaptureLost) {
            wuxi::IPointer* pointer = nullptr;
            if (SUCCEEDED(args->get_Pointer(&pointer)) && pointer) {
                UINT32 pointer_id = 0;
                if (SUCCEEDED(pointer->get_PointerId(&pointer_id)))
                    captured_pointer_ids_.erase(pointer_id);
                pointer->Release();
            }
        }
        std::vector<wuxi::IPointerEventHandler*> snapshot;
        snapshot.reserve(source->size());
        for (const auto& [_, handler] : *source) {
            handler->AddRef();
            snapshot.push_back(handler);
        }
        IInspectable* const sender = static_cast<IInspectable*>(
            static_cast<wux::IUIElement*>(this));
        for (wuxi::IPointerEventHandler* handler : snapshot) {
            (void)handler->Invoke(sender, args);
            boolean handled = 0;
            if (SUCCEEDED(args->get_Handled(&handled)) && handled) break;
        }
        for (wuxi::IPointerEventHandler* handler : snapshot) handler->Release();
        } catch (...) {
        }
    }
    void InvokeIslandTapEvent(
        IslandTapEventKind kind,
        wuxi::ITappedRoutedEventArgs* args) noexcept override {
        if (!args) return;
        try {
        IInspectable* const sender = static_cast<IInspectable*>(
            static_cast<wux::IUIElement*>(this));
        if (kind == IslandTapEventKind::Tapped) {
            std::vector<wuxi::ITappedEventHandler*> snapshot;
            snapshot.reserve(tapped_handlers_.size());
            for (const auto& [_, handler] : tapped_handlers_) {
                handler->AddRef();
                snapshot.push_back(handler);
            }
            for (wuxi::ITappedEventHandler* handler : snapshot) {
                (void)handler->Invoke(sender, args);
                boolean handled = 0;
                if (SUCCEEDED(args->get_Handled(&handled)) && handled) break;
            }
            for (wuxi::ITappedEventHandler* handler : snapshot)
                handler->Release();
            return;
        }

        constexpr GUID double_args_iid = {
            0xaf404424, 0x26df, 0x44f4,
            {0x87, 0x14, 0x93, 0x59, 0x24, 0x9b, 0x62, 0xd3}};
        std::vector<wuxi::IDoubleTappedEventHandler*> snapshot;
        snapshot.reserve(double_tapped_handlers_.size());
        for (const auto& [_, handler] : double_tapped_handlers_) {
            handler->AddRef();
            snapshot.push_back(handler);
        }
        wuxi::IDoubleTappedRoutedEventArgs* double_args = nullptr;
        if (FAILED(args->QueryInterface(
                double_args_iid,
                reinterpret_cast<void**>(&double_args))) || !double_args) {
            for (wuxi::IDoubleTappedEventHandler* handler : snapshot)
                handler->Release();
            return;
        }
        for (wuxi::IDoubleTappedEventHandler* handler : snapshot) {
            (void)handler->Invoke(sender, double_args);
            boolean handled = 0;
            if (SUCCEEDED(double_args->get_Handled(&handled)) && handled) break;
        }
        for (wuxi::IDoubleTappedEventHandler* handler : snapshot)
            handler->Release();
        double_args->Release();
        } catch (...) {
        }
    }
    wux::FocusState IslandFocusState() const noexcept { return focus_state_; }
    HRESULT ValidateOwnedCollectionChange(IUnknown*, IUnknown* added) override {
        if (!added) return S_OK;
        IOpenXamlNative* native = nullptr;
        const HRESULT hr = added->QueryInterface(
            IID_IOpenXamlNative, reinterpret_cast<void**>(&native));
        // Row/column definitions and other non-visual member collections have
        // no native element to attach.
        if (FAILED(hr)) return S_OK;
        openxaml::Element* child = native->LayoutElement();
        const bool allowed = Layout()->CanAttachVisualChild(*child);
        native->Release();
        return allowed ? S_OK : E_INVALIDARG;
    }
    void OnOwnedCollectionRemoving(IUnknown* removed) override {
        IOpenXamlNative* native = nullptr;
        if (removed && SUCCEEDED(removed->QueryInterface(
                           IID_IOpenXamlNative,
                           reinterpret_cast<void**>(&native)))) {
            openxaml::Element* const child = native->LayoutElement();
            pending_detached_roots_[removed] =
                XamlFocusScope::VisualRoot(child);
            PrepareXamlVisualSubtreeDetached(
                child, pending_detached_roots_[removed]);
            Layout()->DetachVisualChild(*child);
            native->Release();
        }
    }
    void OnOwnedCollectionRemoved(IUnknown* removed) override {
        const auto pending = pending_detached_roots_.find(removed);
        if (pending == pending_detached_roots_.end()) return;
        IOpenXamlNative* native = nullptr;
        if (removed && SUCCEEDED(removed->QueryInterface(
                           IID_IOpenXamlNative,
                           reinterpret_cast<void**>(&native)))) {
            NotifyXamlVisualSubtreeDetached(native->LayoutElement(),
                                            pending->second);
            native->Release();
        }
        pending_detached_roots_.erase(pending);
    }
    void OnOwnedCollectionChanged(IUnknown* added) override {
        IOpenXamlNative* native = nullptr;
        if (added && SUCCEEDED(added->QueryInterface(
                         IID_IOpenXamlNative,
                         reinterpret_cast<void**>(&native)))) {
            // Validation ran before the collection changed. Failure here can
            // only mean an internal ordering bug; leave the tree detached and
            // make the structural invalidation observable.
            openxaml::Element* const parent = Layout();
            openxaml::Element* const child = native->LayoutElement();
            const bool attached = parent->AttachVisualChild(*child);
            if (GetEnvironmentVariableW(L"OPENXAML_TRACE_VISUAL_TREE", nullptr, 0)) {
                char message[384]{};
                std::snprintf(
                    message, sizeof(message),
                    "OpenXaml visual attach parent=%s parent_id=%llu child=%s "
                    "child_id=%llu attached=%s\n",
                    parent->TypeName().c_str(),
                    static_cast<unsigned long long>(parent->render_node_id()),
                    child->TypeName().c_str(),
                    static_cast<unsigned long long>(child->render_node_id()),
                    attached ? "true" : "false");
                OutputDebugStringA(message);
            }
            native->Release();
        }
        Layout()->NotifyVisualStructureChanged();
    }
    HRESULT PerformLayout(double width, double height) override {
        if (width < 0.0 || height < 0.0) return E_INVALIDARG;
        TraceRuntime("OpenXaml: island layout begin\n");
        EnsureLayoutPassCallback();
        HRESULT result = S_OK;
        try {
            Layout()->Measure({width, height});
            TraceRuntime("OpenXaml: island measure complete\n");
        } catch (const std::exception& error) {
            // Leaf-level feature refusals are contained by their owning
            // element. Reaching this boundary means the tree layout itself
            // failed; arrange remains a last-resort attempt so diagnostics and
            // the first layout event are not lost with the original failure.
            TraceLayoutException("measure", error.what());
            result = S_FALSE;
        } catch (...) {
            TraceLayoutException("measure", nullptr);
            result = S_FALSE;
        }
        try {
            Layout()->Arrange({0.0, 0.0, width, height});
            TraceRuntime("OpenXaml: island arrange complete\n");
        } catch (const std::exception& error) {
            TraceLayoutException("arrange", error.what());
            result = S_FALSE;
        } catch (...) {
            TraceLayoutException("arrange", nullptr);
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
    HRESULT STDMETHODCALLTYPE get_Dispatcher(
        ABI::Windows::UI::Core::ICoreDispatcher** value) override {
        if (!value) return E_POINTER;
        *value = owner_dispatcher_;
        if (!*value) return E_UNEXPECTED;
        (*value)->AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CapturePointer(
        wuxi::IPointer* pointer, boolean* result) override {
        if (!result) return E_POINTER;
        *result = 0;
        if (!pointer) return E_INVALIDARG;
        UINT32 pointer_id = 0;
        HRESULT hr = pointer->get_PointerId(&pointer_id);
        if (FAILED(hr)) return hr;
        if (CaptureXamlPointer(*this, pointer_id)) {
            captured_pointer_ids_.insert(pointer_id);
            *result = 1;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ReleasePointerCapture(
        wuxi::IPointer* pointer) override {
        if (!pointer) return E_INVALIDARG;
        UINT32 pointer_id = 0;
        HRESULT hr = pointer->get_PointerId(&pointer_id);
        if (FAILED(hr)) return hr;
        (void)ReleaseXamlPointer(*this, pointer_id);
        captured_pointer_ids_.erase(pointer_id);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ReleasePointerCaptures() override {
        const std::set<UINT32> captured = captured_pointer_ids_;
        for (UINT32 pointer_id : captured)
            (void)ReleaseXamlPointer(*this, pointer_id);
        captured_pointer_ids_.clear();
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
        RegisterXamlFocusTarget(*this);
        return AddTypedEvent(handler, token, double_tapped_handlers_);
    }
    HRESULT STDMETHODCALLTYPE remove_DoubleTapped(EventRegistrationToken token) override {
        return RemoveTypedEvent(token, double_tapped_handlers_);
    }
    HRESULT STDMETHODCALLTYPE add_Tapped(
        wuxi::ITappedEventHandler* handler, EventRegistrationToken* token) override {
        RegisterXamlFocusTarget(*this);
        return AddTypedEvent(handler, token, tapped_handlers_);
    }
    HRESULT STDMETHODCALLTYPE remove_Tapped(EventRegistrationToken token) override {
        return RemoveTypedEvent(token, tapped_handlers_);
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
        RegisterXamlFocusTarget(*this);
        return AddTypedEvent(handler, token, key_up_handlers_);
    }
    HRESULT STDMETHODCALLTYPE remove_KeyUp(EventRegistrationToken token) override {
        return RemoveTypedEvent(token, key_up_handlers_);
    }
    HRESULT STDMETHODCALLTYPE add_KeyDown(
        wuxi::IKeyEventHandler* handler, EventRegistrationToken* token) override {
        RegisterXamlFocusTarget(*this);
        return AddTypedEvent(handler, token, key_down_handlers_);
    }
    HRESULT STDMETHODCALLTYPE remove_KeyDown(EventRegistrationToken token) override {
        return RemoveTypedEvent(token, key_down_handlers_);
    }
    HRESULT STDMETHODCALLTYPE add_GotFocus(
        wux::IRoutedEventHandler* handler, EventRegistrationToken* token) override {
        RegisterXamlFocusTarget(*this);
        return AddTypedEvent(handler, token, got_focus_handlers_);
    }
    HRESULT STDMETHODCALLTYPE remove_GotFocus(EventRegistrationToken token) override {
        return RemoveTypedEvent(token, got_focus_handlers_);
    }
    HRESULT STDMETHODCALLTYPE add_LostFocus(
        wux::IRoutedEventHandler* handler, EventRegistrationToken* token) override {
        RegisterXamlFocusTarget(*this);
        return AddTypedEvent(handler, token, lost_focus_handlers_);
    }
    HRESULT STDMETHODCALLTYPE remove_LostFocus(EventRegistrationToken token) override {
        return RemoveTypedEvent(token, lost_focus_handlers_);
    }
#define OPENXAML_POINTER_EVENT(name, storage)                             \
    HRESULT STDMETHODCALLTYPE add_##name(                                \
        wuxi::IPointerEventHandler* handler, EventRegistrationToken* token) override { \
        RegisterXamlFocusTarget(*this);                                   \
        return AddTypedEvent(handler, token, storage);                    \
    }                                                                     \
    HRESULT STDMETHODCALLTYPE remove_##name(EventRegistrationToken token) override { \
        return RemoveTypedEvent(token, storage);                          \
    }
    OPENXAML_POINTER_EVENT(PointerPressed, pointer_pressed_handlers_)
    OPENXAML_POINTER_EVENT(PointerMoved, pointer_moved_handlers_)
    OPENXAML_POINTER_EVENT(PointerReleased, pointer_released_handlers_)
    OPENXAML_POINTER_EVENT(PointerEntered, pointer_entered_handlers_)
    OPENXAML_POINTER_EVENT(PointerExited, pointer_exited_handlers_)
    OPENXAML_POINTER_EVENT(PointerCaptureLost, pointer_capture_lost_handlers_)
    OPENXAML_POINTER_EVENT(PointerCanceled, pointer_canceled_handlers_)
    OPENXAML_POINTER_EVENT(PointerWheelChanged, pointer_wheel_handlers_)
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
        RegisterXamlFocusTarget(*this);
        return AddTypedEvent(handler, token, character_handlers_);
    }
    HRESULT STDMETHODCALLTYPE remove_CharacterReceived(
        EventRegistrationToken token) override {
        return RemoveTypedEvent(token, character_handlers_);
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
        RegisterXamlFocusTarget(*this);
        return AddTypedEvent(handler, token, preview_key_down_handlers_);
    }
    HRESULT STDMETHODCALLTYPE remove_PreviewKeyDown(
        EventRegistrationToken token) override {
        return RemoveTypedEvent(token, preview_key_down_handlers_);
    }
    HRESULT STDMETHODCALLTYPE add_PreviewKeyUp(
        wuxi::IKeyEventHandler* handler, EventRegistrationToken* token) override {
        RegisterXamlFocusTarget(*this);
        return AddTypedEvent(handler, token, preview_key_up_handlers_);
    }
    HRESULT STDMETHODCALLTYPE remove_PreviewKeyUp(
        EventRegistrationToken token) override {
        return RemoveTypedEvent(token, preview_key_up_handlers_);
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
    HRESULT STDMETHODCALLTYPE get_Name(HSTRING* value) override {
        if (!value) return E_POINTER;
        if (!HasFocusThreadAccess()) return RPC_E_WRONG_THREAD;
        return WindowsDuplicateString(name_, value);
    }
    HRESULT STDMETHODCALLTYPE put_Name(HSTRING value) override {
        if (!HasFocusThreadAccess()) return RPC_E_WRONG_THREAD;
        if (SameHString(name_, value)) return S_OK;

        HSTRING next = nullptr;
        HRESULT hr = WindowsDuplicateString(value, &next);
        if (FAILED(hr)) return hr;
        IInspectable* const self = static_cast<IInspectable*>(
            static_cast<wux::IUIElement*>(this));
        if (name_scope_ && !HStringEmpty(next)) {
            hr = name_scope_->Register(next, self);
            if (FAILED(hr)) {
                WindowsDeleteString(next);
                return hr;
            }
        }
        if (name_scope_ && !HStringEmpty(name_))
            (void)name_scope_->Unregister(name_, self);
        WindowsDeleteString(name_);
        name_ = next;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE FindName(HSTRING name, IInspectable** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (!HasFocusThreadAccess()) return RPC_E_WRONG_THREAD;
        return name_scope_ ? name_scope_->Find(name, value) : S_OK;
    }
    HRESULT STDMETHODCALLTYPE AttachNameScope(
        IOpenXamlNameScope* value) override {
        if (!HasFocusThreadAccess()) return RPC_E_WRONG_THREAD;
        if (name_scope_ == value) return S_OK;

        IInspectable* const self = static_cast<IInspectable*>(
            static_cast<wux::IUIElement*>(this));
        if (value) value->AddRef();
        if (value && !HStringEmpty(name_)) {
            const HRESULT hr = value->Register(name_, self);
            if (FAILED(hr)) {
                value->Release();
                return hr;
            }
        }
        if (name_scope_ && !HStringEmpty(name_))
            (void)name_scope_->Unregister(name_, self);
        if (name_scope_) name_scope_->Release();
        name_scope_ = value;
        return S_OK;
    }
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
            const HRESULT hr = CreateResourceDictionary(&resources_);
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
    HRESULT STDMETHODCALLTYPE add_Loaded(
        wux::IRoutedEventHandler* handler, EventRegistrationToken* token) override {
        EnsureLayoutPassCallback();
        return AddTypedEvent(handler, token, loaded_handlers_);
    }
    HRESULT STDMETHODCALLTYPE remove_Loaded(EventRegistrationToken token) override {
        return RemoveTypedEvent(token, loaded_handlers_);
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
        GettingFocusHandler* handler,
        EventRegistrationToken* token) override {
        RegisterXamlFocusTarget(*this);
        return AddTypedEvent(handler, token, getting_focus_handlers_);
    }
    HRESULT STDMETHODCALLTYPE remove_GettingFocus(EventRegistrationToken token) override {
        return RemoveTypedEvent(token, getting_focus_handlers_);
    }
    HRESULT STDMETHODCALLTYPE add_LosingFocus(
        LosingFocusHandler* handler,
        EventRegistrationToken* token) override {
        RegisterXamlFocusTarget(*this);
        return AddTypedEvent(handler, token, losing_focus_handlers_);
    }
    HRESULT STDMETHODCALLTYPE remove_LosingFocus(EventRegistrationToken token) override {
        return RemoveTypedEvent(token, losing_focus_handlers_);
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
        if (xaml_root_) return CopyOwnXamlRoot(value);
        // XamlRoot is inherited from the hosted visual root; descendants do
        // not manufacture independent root identities.
        return CopyInheritedXamlRoot(Layout(), value);
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

    // --- IDependencyObject ---
    //
    // The store these reach is the layout core's own, the one every accessor
    // above already reads: `put_Width` and `SetValue(WidthProperty, ...)` are
    // two spellings of one write, and the precedence chain -- animation over
    // local over style over inherited over default -- is the chain that
    // decides what the next Measure sees.

    // The distinction every property entry point below makes: null is the
    // caller's mistake, but an identity this DLL handed out that names no
    // native slot (a *Property placeholder) is this runtime's own gap, and it
    // answers the way the pre-store runtime did -- E_NOTIMPL, by name.
    static HRESULT NoNativeIdentity(const char* method) {
        TraceRuntime("OpenXaml: E_NOTIMPL IDependencyObject.");
        TraceRuntime(method);
        TraceRuntime(" (the property names no native slot)\n");
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE GetValue(wux::IDependencyProperty* dp,
                                       IInspectable** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        const openxaml::DependencyProperty* property = NativeProperty(dp);
        if (!property) return dp ? NoNativeIdentity("GetValue") : E_INVALIDARG;
        return BoxPropertyValue(Layout()->GetValue(*property), result);
    }

    HRESULT STDMETHODCALLTYPE SetValue(wux::IDependencyProperty* dp,
                                       IInspectable* value) override {
        const openxaml::DependencyProperty* property = NativeProperty(dp);
        if (!property) return dp ? NoNativeIdentity("SetValue") : E_INVALIDARG;
        openxaml::PropertyValue unboxed;
        const HRESULT hr = UnboxPropertyValue(value, property->default_value(), &unboxed);
        if (FAILED(hr)) return hr;
        Layout()->SetValue(*property, std::move(unboxed));
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE ClearValue(wux::IDependencyProperty* dp) override {
        const openxaml::DependencyProperty* property = NativeProperty(dp);
        if (!property) return dp ? NoNativeIdentity("ClearValue") : E_INVALIDARG;
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
        if (!property) return dp ? NoNativeIdentity("ReadLocalValue") : E_INVALIDARG;
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
        if (!dp || !callback) return E_INVALIDARG;
        const openxaml::DependencyProperty* property = NativeProperty(dp);
        if (!property) {
            // An identity this DLL handed out (a *Property placeholder) that
            // names no native slot. The value behind it never moves through
            // this runtime, so the observer is stored and never fired -- the
            // same honesty as a stored event, and what the placeholder did
            // before the store was reachable at all. Refusing instead would
            // fail WinUI's control constructors, which register these
            // observers unconditionally.
            EventRegistrationToken stored{};
            const HRESULT hr = AddEvent(callback, &stored);
            if (FAILED(hr)) return hr;
            *result = stored.value;
            return S_OK;
        }
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
        if (!dp) return E_INVALIDARG;
        const openxaml::DependencyProperty* property = NativeProperty(dp);
        if (!property) return RemoveEvent({token});
        Layout()->UnregisterPropertyChangedCallback(
            *property, static_cast<openxaml::DependencyObject::PropertyChangedToken>(token));
        const auto found = callbacks_.find(token);
        if (found != callbacks_.end()) {
            found->second->Release();
            callbacks_.erase(found);
        }
        return S_OK;
    }

    // --- events through the layout core's own registry ---
    //
    // Unloaded is stored and never called -- there is no live visual tree to
    // leave, so a handler that would only ever be called at a moment that does
    // not exist is better stored honestly than refused. LayoutUpdated is
    // raised: the core's arrange pass drains it after the size-changed queue,
    // exactly where the reference's layout manager does. Every other stored
    // event (pointers, keys, focus, taps) already lives on the AddEvent path
    // above, and Loaded is raised by the island's layout-pass callback.

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

    OPENXAML_EVENT(Unloaded, wux::IRoutedEventHandler, Unloaded)
    OPENXAML_EVENT(LayoutUpdated, __FIEventHandler_1_IInspectable, LayoutUpdated)
#undef OPENXAML_EVENT

    // SizeChanged goes through the layout core's registry because it is
    // raised: layout/src/events.h says when, and from which lines of the
    // published XAML core, and the arguments it is given are built here from
    // the two sizes the layout pass recorded.
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

protected:
    template <class Handler>
    HRESULT AddTypedEvent(Handler* handler, EventRegistrationToken* token,
                          std::map<LONGLONG, Handler*>& handlers) {
        if (!handler || !token) return E_INVALIDARG;
        token->value = InterlockedIncrement64(&next_event_token_);
        handler->AddRef();
        try {
            handlers.emplace(token->value, handler);
        } catch (...) {
            handler->Release();
            token->value = 0;
            return E_OUTOFMEMORY;
        }
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
        const HRESULT result = handler->Invoke(nullptr, nullptr);
        if (FAILED(result) &&
            GetEnvironmentVariableW(L"OPENXAML_TRACE_VISUAL_TREE", nullptr, 0)) {
            char message[160]{};
            std::snprintf(message, sizeof(message),
                          "OpenXaml event action=invoke-failed kind=LayoutUpdated error=0x%08lx\n",
                          static_cast<unsigned long>(result));
            OutputDebugStringA(message);
        }
    }

    template <class Handler>
    HRESULT AddEventHandler(openxaml::FrameworkEvent event, Handler* handler,
                            EventRegistrationToken* token) {
        handler->AddRef();
        auto* sender = static_cast<wux::IDependencyObject*>(this);
        const openxaml::EventToken registered = Layout()->events().Add(
            event, [this, handler, sender](openxaml::Element&, openxaml::FrameworkEvent raised,
                                           const openxaml::SizeChangedArgs& args) {
                TraceFrameworkEvent("raise", raised);
                InvokeHandler(handler, sender, args);
            });
        if (registered == 0) {
            handler->Release();
            return E_FAIL;
        }
        handlers_.emplace(registered, handler);
        token->value = registered;
        TraceFrameworkEvent("add", event);
        return S_OK;
    }

    void TraceFrameworkEvent(const char* action,
                             openxaml::FrameworkEvent event) const noexcept {
        if (!GetEnvironmentVariableW(L"OPENXAML_TRACE_VISUAL_TREE", nullptr, 0)) return;
        char message[256]{};
        std::snprintf(
            message, sizeof(message),
            "OpenXaml event action=%s kind=%s type=%s node_id=%llu\n",
            action, openxaml::NameOf(event), Layout()->TypeName().c_str(),
            static_cast<unsigned long long>(Layout()->render_node_id()));
        OutputDebugStringA(message);
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
        OPENXAML_QI_ARM(IID_IOpenXamlNameScopeOwner, IOpenXamlNameScopeOwner)
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
    static bool HStringEmpty(HSTRING value) noexcept {
        UINT32 length = 0;
        (void)WindowsGetStringRawBuffer(value, &length);
        return length == 0;
    }
    static bool SameHString(HSTRING left, HSTRING right) noexcept {
        UINT32 left_length = 0;
        UINT32 right_length = 0;
        const wchar_t* left_text = WindowsGetStringRawBuffer(left, &left_length);
        const wchar_t* right_text = WindowsGetStringRawBuffer(right, &right_length);
        if (left_length != right_length) return false;
        if (left_length == 0) return true;
        return std::wmemcmp(left_text, right_text, left_length) == 0;
    }

    void TransformValueChanged(const openxaml::VisualTransform& value) override {
        Layout()->set_visual_transform(value);
    }
    void ResetRenderTransform() {
        if (render_transform_source_) {
            render_transform_source_->Unsubscribe(render_transform_token_);
            render_transform_source_->Release();
            render_transform_source_ = nullptr;
            render_transform_token_ = 0;
        }
        if (render_transform_) {
            render_transform_->Release();
            render_transform_ = nullptr;
        }
    }

    template <class Handler, class Args>
    void InvokeFocusChanging(std::map<LONGLONG, Handler*>& source,
                             Args* args) noexcept {
        try {
            std::vector<Handler*> snapshot;
            snapshot.reserve(source.size());
            for (const auto& [_, handler] : source) {
                handler->AddRef();
                snapshot.push_back(handler);
            }
            wux::IUIElement* const sender =
                static_cast<wux::IUIElement*>(this);
            for (Handler* handler : snapshot) {
                (void)handler->Invoke(sender, args);
                boolean cancel = 0;
                boolean handled = 0;
                (void)args->get_Cancel(&cancel);
                (void)args->get_Handled(&handled);
                if (cancel || handled) break;
            }
            for (Handler* handler : snapshot) handler->Release();
        } catch (...) {
        }
    }

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
    }

    std::shared_ptr<WeakReferenceState> weak_state_;
    IOpenXamlNameScope* name_scope_ = nullptr;
    HSTRING name_ = nullptr;
    DWORD owner_thread_id_ = 0;
    ABI::Windows::UI::Core::ICoreDispatcher* owner_dispatcher_ = nullptr;
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
    wux::FocusState focus_state_ = wux::FocusState_Unfocused;
    wuxm::ITransform* render_transform_ = nullptr;
    IOpenXamlTransformSource* render_transform_source_ = nullptr;
    LONGLONG render_transform_token_ = 0;
    std::map<IUnknown*, openxaml::Element*> pending_detached_roots_;
    std::map<LONGLONG, IUnknown*> event_handlers_;
    std::map<LONGLONG, wux::IRoutedEventHandler*> got_focus_handlers_;
    std::map<LONGLONG, wux::IRoutedEventHandler*> lost_focus_handlers_;
    std::map<LONGLONG, GettingFocusHandler*> getting_focus_handlers_;
    std::map<LONGLONG, LosingFocusHandler*> losing_focus_handlers_;
    std::map<LONGLONG, wuxi::IKeyEventHandler*> preview_key_down_handlers_;
    std::map<LONGLONG, wuxi::IKeyEventHandler*> key_down_handlers_;
    std::map<LONGLONG, wuxi::IKeyEventHandler*> preview_key_up_handlers_;
    std::map<LONGLONG, wuxi::IKeyEventHandler*> key_up_handlers_;
    using CharacterHandler =
        __FITypedEventHandler_2_Windows__CUI__CXaml__CUIElement_Windows__CUI__CXaml__CInput__CCharacterReceivedRoutedEventArgs;
    std::map<LONGLONG, CharacterHandler*> character_handlers_;
    std::map<LONGLONG, wuxi::IPointerEventHandler*> pointer_pressed_handlers_;
    std::map<LONGLONG, wuxi::IPointerEventHandler*> pointer_moved_handlers_;
    std::map<LONGLONG, wuxi::IPointerEventHandler*> pointer_released_handlers_;
    std::map<LONGLONG, wuxi::IPointerEventHandler*> pointer_entered_handlers_;
    std::map<LONGLONG, wuxi::IPointerEventHandler*> pointer_exited_handlers_;
    std::map<LONGLONG, wuxi::IPointerEventHandler*> pointer_capture_lost_handlers_;
    std::map<LONGLONG, wuxi::IPointerEventHandler*> pointer_canceled_handlers_;
    std::map<LONGLONG, wuxi::IPointerEventHandler*> pointer_wheel_handlers_;
    std::map<LONGLONG, wuxi::ITappedEventHandler*> tapped_handlers_;
    std::map<LONGLONG, wuxi::IDoubleTappedEventHandler*> double_tapped_handlers_;
    std::set<UINT32> captured_pointer_ids_;
    std::map<LONGLONG, wux::IRoutedEventHandler*> loaded_handlers_;
    std::map<UINT32, HSTRING> automation_strings_;
    bool layout_callback_installed_ = false;
    bool loaded_raised_ = false;
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
    HRESULT STDMETHODCALLTYPE get_BorderBrush(wuxm::IBrush** value) override {
        return border_brush_.Get(value);
    }
    HRESULT STDMETHODCALLTYPE put_BorderBrush(wuxm::IBrush* value) override {
        return border_brush_.Assign(value);
    }
    HRESULT STDMETHODCALLTYPE get_Background(wuxm::IBrush** value) override {
        return background_.Get(value);
    }
    HRESULT STDMETHODCALLTYPE put_Background(wuxm::IBrush* value) override {
        return background_.Assign(value);
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
    AbiBorder children_;
    BrushProjection border_brush_{children_, ProjectedBrushSlot::Border};
    BrushProjection background_{children_, ProjectedBrushSlot::Background};
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

    openxaml::Element* Layout() override { return &layout_; }

    HRESULT STDMETHODCALLTYPE get_Children(
        __FIVector_1_Windows__CUI__CXaml__CUIElement** value) override {
        if (!value) return E_POINTER;
        children_.AddRef();
        *value = &children_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Background(wuxm::IBrush** value) override {
        return background_.Get(value);
    }
    HRESULT STDMETHODCALLTYPE put_Background(wuxm::IBrush* value) override {
        return background_.Assign(value);
    }

protected:
    HRESULT QueryPanelInterface(REFIID iid, void** object) {
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IPanel, wuxc::IPanel)
        return QueryElementInterface(iid, object);
    }

    LayoutType layout_;
    BrushProjection background_{layout_, ProjectedBrushSlot::Background};
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
    SwapChainPanelObject()
        : external_surface_(std::make_shared<openxaml::ExternalSurfaceBinding>()) {
        layout_.SetExternalSurfaceProvider(external_surface_);
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
        const HRESULT hr = external_surface_->SetDxgiSwapChain(value);
        if (SUCCEEDED(hr)) layout_.NotifyExternalSurfaceChanged();
        TraceExternalSurfaceMutation("dxgi-swap-chain", hr);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE SetSwapChainHandle(HANDLE value) override {
        const HRESULT hr = external_surface_->SetCompositionSurfaceHandle(value);
        if (SUCCEEDED(hr)) layout_.NotifyExternalSurfaceChanged();
        TraceExternalSurfaceMutation("composition-surface-handle", hr);
        return hr;
    }

private:
    void TraceExternalSurfaceMutation(const char* source, HRESULT result) const noexcept {
        if (!GetEnvironmentVariableW(L"OPENXAML_TRACE_VISUAL_TREE", nullptr, 0)) return;
        const openxaml::ExternalSurfaceSnapshot snapshot = external_surface_->Snapshot();
        char message[320]{};
        std::snprintf(
            message, sizeof(message),
            "OpenXaml external source=%s result=0x%08lx generation=%llu "
            "kind=%d bound=%s node_id=%llu\n",
            source, static_cast<unsigned long>(result),
            static_cast<unsigned long long>(snapshot.generation()),
            static_cast<int>(snapshot.kind()), snapshot ? "true" : "false",
            static_cast<unsigned long long>(layout_.render_node_id()));
        OutputDebugStringA(message);
    }

    std::shared_ptr<openxaml::ExternalSurfaceBinding> external_surface_;
};

class ContentPresenterObject final : public XamlElement,
                                     public abi::NotImpl_IContentPresenter,
                                     public abi::NotImpl_IContentPresenter4 {
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
        return background_.Get(value);
    }
    HRESULT STDMETHODCALLTYPE put_Background(wuxm::IBrush* value) override {
        return background_.Assign(value);
    }
private:
    ChildSourced<openxaml::ContentPresenter> layout_;
    BrushProjection background_{layout_, ProjectedBrushSlot::Background};
    ChildCollection content_{{::openxaml::iid::PIID_FIVector_1_Windows__CUI__CXaml__CUIElement,
                              ::openxaml::iid::PIID_FIIterable_1_Windows__CUI__CXaml__CUIElement,
                              ::openxaml::iid::PIID_FIIterator_1_Windows__CUI__CXaml__CUIElement},
                             L"Windows.UI.Xaml.Controls.ContentPresenterContent", this};
};

class ImageObject final : public XamlElement, public abi::NotImpl_IImage {
public:
    using PrimaryInterface = wuxc::IImage;
    ~ImageObject() override {
        if (source_) source_->Release();
    }
    openxaml::Element* Layout() override { return &layout_; }
    const wchar_t* RuntimeClassName() const override { return L"Windows.UI.Xaml.Controls.Image"; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IImage, wuxc::IImage)
        return QueryElementInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE get_Source(wuxm::IImageSource** value) override {
        if (!value) return E_POINTER;
        *value = source_;
        if (*value) (*value)->AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Source(wuxm::IImageSource* value) override {
        if (value == source_) return S_OK;
        if (value) value->AddRef();

        std::string type;
        if (value) {
            type = "Windows.UI.Xaml.Media.ImageSource";
            HSTRING runtime_name = nullptr;
            if (SUCCEEDED(value->GetRuntimeClassName(&runtime_name)) && runtime_name) {
                type = Utf8FromHString(runtime_name);
                WindowsDeleteString(runtime_name);
            }
        }

        wuxm::IImageSource* previous = source_;
        source_ = value;
        layout_.set_source(value != nullptr, std::move(type));
        if (previous) previous->Release();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Stretch(wuxm::Stretch* value) override {
        if (!value) return E_POINTER;
        *value = static_cast<wuxm::Stretch>(layout_.stretch());
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Stretch(wuxm::Stretch value) override {
        if (value < wuxm::Stretch_None || value > wuxm::Stretch_UniformToFill)
            return E_INVALIDARG;
        layout_.set_stretch(static_cast<openxaml::ImageStretch>(value));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_NineGrid(wux::Thickness* value) override {
        if (!value) return E_POINTER;
        const openxaml::Thickness& retained = layout_.nine_grid();
        *value = {retained.left, retained.top, retained.right, retained.bottom};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_NineGrid(wux::Thickness value) override {
        if (!ValidNineGrid(value.Left) || !ValidNineGrid(value.Top) ||
            !ValidNineGrid(value.Right) || !ValidNineGrid(value.Bottom))
            return E_INVALIDARG;
        layout_.set_nine_grid(
            {value.Left, value.Top, value.Right, value.Bottom});
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE add_ImageFailed(
        wux::IExceptionRoutedEventHandler* handler,
        EventRegistrationToken* token) override {
        return AddEvent(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_ImageFailed(
        EventRegistrationToken token) override {
        return RemoveEvent(token);
    }
    HRESULT STDMETHODCALLTYPE add_ImageOpened(
        wux::IRoutedEventHandler* handler,
        EventRegistrationToken* token) override {
        return AddEvent(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_ImageOpened(
        EventRegistrationToken token) override {
        return RemoveEvent(token);
    }

private:
    static bool ValidNineGrid(double value) {
        return std::isfinite(value) && value >= 0.0;
    }

    openxaml::Image layout_;
    wuxm::IImageSource* source_ = nullptr;
};

template <class LayoutType>
class ShapeObjectBase : public XamlElement, public wuxs::IShape {
public:
    ShapeObjectBase()
        : fill_(layout_, ProjectedBrushSlot::Fill),
          stroke_(layout_, ProjectedBrushSlot::Stroke) {}
    ~ShapeObjectBase() override {
        if (stroke_dash_array_) stroke_dash_array_->Release();
    }

    openxaml::Element* Layout() override { return &layout_; }

    HRESULT QueryShapeInterface(REFIID iid, void** object) {
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Shapes_IShape,
                        wuxs::IShape)
        return QueryElementInterface(iid, object);
    }

    HRESULT STDMETHODCALLTYPE get_Fill(wuxm::IBrush** value) override {
        return fill_.Get(value);
    }
    HRESULT STDMETHODCALLTYPE put_Fill(wuxm::IBrush* value) override {
        return fill_.Assign(value);
    }
    HRESULT STDMETHODCALLTYPE get_Stroke(wuxm::IBrush** value) override {
        return stroke_.Get(value);
    }
    HRESULT STDMETHODCALLTYPE put_Stroke(wuxm::IBrush* value) override {
        return stroke_.Assign(value);
    }
    HRESULT STDMETHODCALLTYPE get_StrokeMiterLimit(DOUBLE* value) override {
        return CopyDouble(layout_.stroke_miter_limit(), value);
    }
    HRESULT STDMETHODCALLTYPE put_StrokeMiterLimit(DOUBLE value) override {
        if (!std::isfinite(value) || value < 1.0) return E_INVALIDARG;
        if (layout_.stroke_miter_limit() == value) return S_OK;
        layout_.set_stroke_miter_limit(value);
        layout_.InvalidateRender(false);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_StrokeThickness(DOUBLE* value) override {
        return CopyDouble(layout_.stroke_thickness(), value);
    }
    HRESULT STDMETHODCALLTYPE put_StrokeThickness(DOUBLE value) override {
        if (!std::isfinite(value) || value < 0.0) return E_INVALIDARG;
        if (layout_.stroke_thickness() == value) return S_OK;
        layout_.set_stroke_thickness(value);
        layout_.InvalidateRender(true);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_StrokeStartLineCap(
        wuxm::PenLineCap* value) override {
        if (!value) return E_POINTER;
        *value = static_cast<wuxm::PenLineCap>(layout_.stroke_start_line_cap());
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_StrokeStartLineCap(
        wuxm::PenLineCap value) override {
        if (!ValidLineCap(value)) return E_INVALIDARG;
        layout_.set_stroke_start_line_cap(
            static_cast<openxaml::ShapeLineCap>(value));
        layout_.InvalidateRender(false);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_StrokeEndLineCap(
        wuxm::PenLineCap* value) override {
        if (!value) return E_POINTER;
        *value = static_cast<wuxm::PenLineCap>(layout_.stroke_end_line_cap());
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_StrokeEndLineCap(
        wuxm::PenLineCap value) override {
        if (!ValidLineCap(value)) return E_INVALIDARG;
        layout_.set_stroke_end_line_cap(
            static_cast<openxaml::ShapeLineCap>(value));
        layout_.InvalidateRender(false);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_StrokeLineJoin(
        wuxm::PenLineJoin* value) override {
        if (!value) return E_POINTER;
        *value = static_cast<wuxm::PenLineJoin>(layout_.stroke_line_join());
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_StrokeLineJoin(
        wuxm::PenLineJoin value) override {
        if (value < wuxm::PenLineJoin_Miter || value > wuxm::PenLineJoin_Round)
            return E_INVALIDARG;
        layout_.set_stroke_line_join(
            static_cast<openxaml::ShapeLineJoin>(value));
        layout_.InvalidateRender(false);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_StrokeDashOffset(DOUBLE* value) override {
        return CopyDouble(layout_.stroke_dash_offset(), value);
    }
    HRESULT STDMETHODCALLTYPE put_StrokeDashOffset(DOUBLE value) override {
        if (!std::isfinite(value)) return E_INVALIDARG;
        if (layout_.stroke_dash_offset() == value) return S_OK;
        layout_.set_stroke_dash_offset(value);
        layout_.InvalidateRender(false);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_StrokeDashCap(
        wuxm::PenLineCap* value) override {
        if (!value) return E_POINTER;
        *value = static_cast<wuxm::PenLineCap>(layout_.stroke_dash_cap());
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_StrokeDashCap(
        wuxm::PenLineCap value) override {
        if (!ValidLineCap(value)) return E_INVALIDARG;
        layout_.set_stroke_dash_cap(static_cast<openxaml::ShapeLineCap>(value));
        layout_.InvalidateRender(false);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_StrokeDashArray(
        __FIVector_1_double** value) override {
        if (!value) return E_POINTER;
        *value = stroke_dash_array_;
        if (*value) (*value)->AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_StrokeDashArray(
        __FIVector_1_double* value) override {
        if (value) value->AddRef();
        if (stroke_dash_array_) stroke_dash_array_->Release();
        stroke_dash_array_ = value;
        layout_.set_has_stroke_dash_array(value != nullptr);
        layout_.InvalidateRender(false);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Stretch(wuxm::Stretch* value) override {
        if (!value) return E_POINTER;
        *value = static_cast<wuxm::Stretch>(layout_.shape_stretch());
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Stretch(wuxm::Stretch value) override {
        if (value < wuxm::Stretch_None || value > wuxm::Stretch_UniformToFill)
            return E_INVALIDARG;
        if (layout_.shape_stretch() ==
            static_cast<openxaml::ShapeStretch>(value)) return S_OK;
        layout_.set_shape_stretch(static_cast<openxaml::ShapeStretch>(value));
        layout_.InvalidateRender(true);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_GeometryTransform(
        wuxm::ITransform** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (layout_.shape_stretch() != openxaml::ShapeStretch::None) {
            TraceRuntime(
                "OpenXaml: E_NOTIMPL IShape.get_GeometryTransform for stretched geometry\n");
            return E_NOTIMPL;
        }
        auto* identity = new (std::nothrow) ScaleTransformObject();
        if (!identity) return E_OUTOFMEMORY;
        *value = static_cast<wuxm::ITransform*>(identity);
        return S_OK;
    }

protected:
    LayoutType layout_;

private:
    static HRESULT CopyDouble(double source, DOUBLE* value) noexcept {
        if (!value) return E_POINTER;
        *value = source;
        return S_OK;
    }
    static bool ValidLineCap(wuxm::PenLineCap value) noexcept {
        return value >= wuxm::PenLineCap_Flat &&
               value <= wuxm::PenLineCap_Triangle;
    }

    BrushProjection fill_;
    BrushProjection stroke_;
    __FIVector_1_double* stroke_dash_array_ = nullptr;
};

class PathObject final : public ShapeObjectBase<openxaml::Path>,
                         public abi::NotImpl_IPath {
public:
    using PrimaryInterface = wuxs::IPath;
    const wchar_t* RuntimeClassName() const override { return L"Windows.UI.Xaml.Shapes.Path"; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Shapes_IPath, wuxs::IPath)
        return QueryShapeInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()
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
    void OnOwnedCollectionChanged(IUnknown* added) override {
        // Island layout enters the native Element tree directly, so it does
        // not pass through this object's ABI Measure/Arrange methods. Keep the
        // native definition snapshots current when the WinRT collections are
        // edited; otherwise a programmatic Auto + Star titlebar grid silently
        // becomes a one-cell overlay and its client content covers the tabs.
        SyncDefinitions();
        XamlElement::OnOwnedCollectionChanged(added);
    }

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
        return background_.Get(value);
    }
    HRESULT STDMETHODCALLTYPE put_Background(wuxm::IBrush* value) override {
        return background_.Assign(value);
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
    HRESULT STDMETHODCALLTYPE get_FocusState(wux::FocusState* value) override {
        if (!value) return E_POINTER;
        *value = IslandFocusState();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Focus(wux::FocusState state,
                                    boolean* value) override {
        if (!value) return E_POINTER;
        *value = 0;
        if (state < wux::FocusState_Unfocused ||
            state > wux::FocusState_Programmatic) {
            return E_INVALIDARG;
        }
        *value = RequestXamlFocus(*this, state) ? 1 : 0;
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
    BrushProjection background_{layout_, ProjectedBrushSlot::Background};
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
    virtual HRESULT STDMETHODCALLTYPE add_SelectionChanged(
        wuxc::ISelectionChangedEventHandler*, EventRegistrationToken*) = 0;
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

inline constexpr GUID IID_OpenXamlSelectionChangedEventArgs = {
    0xc972d2dc, 0xb609, 0x4758,
    {0x85, 0x1e, 0xa7, 0x99, 0xc2, 0x1d, 0xe9, 0x7d}};

// SelectionChanged is the semantic bridge TabView uses to publish a newly
// selected tab. The argument collections own stable references to the old and
// new items so a handler may retain either collection after dispatch.
class SelectionChangedEventArgsObject final
    : public ComObject,
      public wuxc::ISelectionChangedEventArgs {
public:
    SelectionChangedEventArgsObject(IInspectable* removed,
                                    IInspectable* added)
        : removed_({::openxaml::iid::PIID_FIVector_1_IInspectable,
                    ::openxaml::iid::PIID_FIIterable_1_IInspectable,
                    ::openxaml::iid::PIID_FIIterator_1_IInspectable},
                   L"Windows.UI.Xaml.Controls.SelectionChangedEventArgs.RemovedItems",
                   this),
          added_({::openxaml::iid::PIID_FIVector_1_IInspectable,
                  ::openxaml::iid::PIID_FIIterable_1_IInspectable,
                  ::openxaml::iid::PIID_FIIterator_1_IInspectable},
                 L"Windows.UI.Xaml.Controls.SelectionChangedEventArgs.AddedItems",
                 this) {
        if (removed) (void)removed_.Append(removed);
        if (added) (void)added_.Append(added);
    }

    const wchar_t* RuntimeClassName() const override {
        return L"Windows.UI.Xaml.Controls.SelectionChangedEventArgs";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(IID_OpenXamlSelectionChangedEventArgs,
                        wuxc::ISelectionChangedEventArgs)
        OPENXAML_QI_ARM(IID_IUnknown, wuxc::ISelectionChangedEventArgs)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable,
                        wuxc::ISelectionChangedEventArgs)
        *object = nullptr;
        return E_NOINTERFACE;
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE get_AddedItems(
        __FIVector_1_IInspectable** value) override {
        if (!value) return E_POINTER;
        *value = static_cast<__FIVector_1_IInspectable*>(&added_);
        (*value)->AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_RemovedItems(
        __FIVector_1_IInspectable** value) override {
        if (!value) return E_POINTER;
        *value = static_cast<__FIVector_1_IInspectable*>(&removed_);
        (*value)->AddRef();
        return S_OK;
    }

private:
    InspectableCollection removed_;
    InspectableCollection added_;
};

class TabViewObject final : public ContentControlObjectBase<openxaml::TabView>,
                            public IMuxcTabView {
public:
    using PrimaryInterface = IMuxcTabView;
    TabViewObject()
        : tab_items_({::openxaml::iid::PIID_FIVector_1_IInspectable,
                      ::openxaml::iid::PIID_FIIterable_1_IInspectable,
                      ::openxaml::iid::PIID_FIIterator_1_IInspectable},
                     L"Microsoft.UI.Xaml.Controls.TabView.TabItems", this) {
        layout_.supplemental = &visual_children_;
        layout_.set_background_brush(openxaml::BrushValue::SolidColor(
            {0xff, 0x20, 0x20, 0x20}));
    }
    ~TabViewObject() override {
        for (auto& [_, handler] : selection_changed_handlers_)
            handler->Release();
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
    OPENXAML_MUXC_OBJECT(TabStripHeaderTemplate, tab_strip_header_template_)
    OPENXAML_MUXC_OBJECT(TabStripFooterTemplate, tab_strip_footer_template_)
    OPENXAML_MUXC_OBJECT(AddTabButtonCommand, add_tab_command_)
    OPENXAML_MUXC_OBJECT(AddTabButtonCommandParameter, add_tab_parameter_)
    OPENXAML_MUXC_OBJECT(TabItemsSource, tab_items_source_)
    OPENXAML_MUXC_OBJECT(TabItemTemplate, tab_item_template_)
    OPENXAML_MUXC_OBJECT(TabItemTemplateSelector, tab_item_template_selector_)
#undef OPENXAML_MUXC_OBJECT

    HRESULT STDMETHODCALLTYPE get_TabStripHeader(void** value) override {
        return GetObject(tab_strip_header_, value);
    }
    HRESULT STDMETHODCALLTYPE put_TabStripHeader(void* value) override {
        return PutVisualObject(
            tab_strip_header_, static_cast<IInspectable*>(value));
    }
    HRESULT STDMETHODCALLTYPE get_TabStripFooter(void** value) override {
        return GetObject(tab_strip_footer_, value);
    }
    HRESULT STDMETHODCALLTYPE put_TabStripFooter(void* value) override {
        return PutVisualObject(
            tab_strip_footer_, static_cast<IInspectable*>(value));
    }

    HRESULT STDMETHODCALLTYPE get_SelectedIndex(INT32* value) override {
        if (!value) return E_POINTER;
        *value = selected_index_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_SelectedIndex(INT32 value) override {
        if (value < -1) return E_INVALIDARG;
        IInspectable* item = nullptr;
        if (value >= 0) {
            const HRESULT hr = tab_items_.GetAt(static_cast<UINT32>(value), &item);
            if (FAILED(hr)) return hr;
        }
        const HRESULT hr = Select(item, value);
        if (item) item->Release();
        return hr;
    }
    HRESULT STDMETHODCALLTYPE get_SelectedItem(void** value) override {
        return GetObject(selected_item_, value);
    }
    HRESULT STDMETHODCALLTYPE put_SelectedItem(void* value) override {
        auto* const item = static_cast<IInspectable*>(value);
        INT32 index = -1;
        if (item) {
            const UINT32 count = tab_items_.Count();
            for (UINT32 candidate = 0; candidate < count; ++candidate) {
                IInspectable* existing = nullptr;
                if (FAILED(tab_items_.GetAt(candidate, &existing)) || !existing)
                    continue;
                const bool match = SameIdentity(existing, item);
                existing->Release();
                if (match) {
                    index = static_cast<INT32>(candidate);
                    break;
                }
            }
        }
        return Select(item, index);
    }

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
    OPENXAML_MUXC_EVENT(TabDragStarting)
    OPENXAML_MUXC_EVENT(TabDragCompleted)
    OPENXAML_MUXC_EVENT(TabStripDragOver)
    OPENXAML_MUXC_EVENT(TabStripDrop)
#undef OPENXAML_MUXC_EVENT

    HRESULT STDMETHODCALLTYPE add_SelectionChanged(
        wuxc::ISelectionChangedEventHandler* handler,
        EventRegistrationToken* token) override {
        return AddTypedEvent(handler, token, selection_changed_handlers_);
    }
    HRESULT STDMETHODCALLTYPE remove_SelectionChanged(
        EventRegistrationToken token) override {
        return RemoveTypedEvent(token, selection_changed_handlers_);
    }

private:
    void RebuildVisualChildren() {
        visual_children_.clear();
        AppendVisual(tab_strip_header_);
        for (IInspectable* item : tab_items_.Projected()) AppendVisual(item);
        AppendVisual(tab_strip_footer_);
    }

    void AppendVisual(IInspectable* value) {
        if (!value) return;
        IOpenXamlNative* native = nullptr;
        if (SUCCEEDED(value->QueryInterface(
                IID_IOpenXamlNative, reinterpret_cast<void**>(&native)))) {
            visual_children_.push_back(native->LayoutElement());
            native->Release();
        }
    }

    HRESULT PutVisualObject(IInspectable*& target, IInspectable* value) {
        if (SameIdentity(target, value)) return S_OK;
        const HRESULT validation = ValidateOwnedCollectionChange(target, value);
        if (FAILED(validation)) return validation;
        IInspectable* previous = target;
        if (previous) OnOwnedCollectionRemoving(previous);
        if (value) value->AddRef();
        target = value;
        RebuildVisualChildren();
        if (previous) {
            OnOwnedCollectionRemoved(previous);
            previous->Release();
        }
        if (value) OnOwnedCollectionChanged(value);
        else Layout()->NotifyVisualStructureChanged();
        return S_OK;
    }

    void OnOwnedCollectionChanged(IUnknown* added) override {
        RebuildVisualChildren();
        XamlElement::OnOwnedCollectionChanged(added);
    }

    static bool SameIdentity(IUnknown* left, IUnknown* right) {
        if (left == right) return true;
        if (!left || !right) return false;
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

    HRESULT Select(IInspectable* item, INT32 index) {
        if (SameIdentity(selected_item_, item)) {
            selected_index_ = index;
            return S_OK;
        }

        SelectionChangedEventArgsObject* arguments = nullptr;
        std::vector<wuxc::ISelectionChangedEventHandler*> handlers;
        try {
            arguments = new SelectionChangedEventArgsObject(selected_item_, item);
            handlers.reserve(selection_changed_handlers_.size());
            for (const auto& [_, handler] : selection_changed_handlers_) {
                handler->AddRef();
                handlers.push_back(handler);
            }
        } catch (...) {
            if (arguments) arguments->Release();
            for (auto* handler : handlers) handler->Release();
            return E_OUTOFMEMORY;
        }

        if (item) item->AddRef();
        IInspectable* const removed = selected_item_;
        selected_item_ = item;
        selected_index_ = index;

        HRESULT result = S_OK;
        auto* const sender = static_cast<IInspectable*>(
            static_cast<IMuxcTabView*>(this));
        auto* const args = static_cast<wuxc::ISelectionChangedEventArgs*>(arguments);
        for (auto* handler : handlers) {
            const HRESULT invoked = handler->Invoke(sender, args);
            if (FAILED(invoked) && SUCCEEDED(result)) result = invoked;
        }
        for (auto* handler : handlers) handler->Release();
        arguments->Release();
        if (removed) removed->Release();
        return result;
    }

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
    std::map<LONGLONG, wuxc::ISelectionChangedEventHandler*>
        selection_changed_handlers_;
    InspectableCollection tab_items_;
    std::vector<openxaml::Element*> visual_children_;
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
    : public ContentControlObjectBase<openxaml::TabViewItem>,
      public abi::NotImpl_ISelectorItem,
      public IMuxcTabViewItem {
public:
    using PrimaryInterface = IMuxcTabViewItem;
    TabViewItemObject() {
        label_.set_text("Terminal");
        label_.set_font_size(12.0);
        label_.set_foreground_brush(openxaml::BrushValue::SolidColor(
            {0xff, 0xf2, 0xf2, 0xf2}));
        SetSelectedVisual(false);
        layout_.include_source = false;
        visual_children_.push_back(&label_);
        layout_.supplemental = &visual_children_;
        (void)layout_.AttachVisualChild(label_);
    }
    ~TabViewItemObject() override {
        layout_.DetachVisualChild(label_);
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
        SetSelectedVisual(is_selected_);
        return S_OK;
    }

    HRESULT SetAutomationString(UINT32 property, HSTRING value) override {
        const HRESULT hr = XamlElement::SetAutomationString(property, value);
        if (SUCCEEDED(hr) && property == 6) {
            const std::string name = Utf8FromHString(value);
            label_.set_text(name.empty() ? "Terminal" : name);
            label_.InvalidateRender(true);
        }
        return hr;
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
    void SetSelectedVisual(bool selected) {
        layout_.set_selected(selected);
        // DirectComposition rasterizes each visual node into its own surface.
        // Text on an otherwise transparent node cannot use ClearType, so give
        // the synthetic header label the same opaque backing as its tab. This
        // keeps the glyph run and its backing in one CPU stratum and matches
        // WinUI's opaque tab-strip rendering contract.
        label_.set_background_brush(openxaml::BrushValue::SolidColor(
            selected ? openxaml::Color{0xff, 0x3a, 0x3a, 0x3a}
                     : openxaml::Color{0xff, 0x24, 0x24, 0x24}));
    }

    IInspectable* header_ = nullptr;
    IInspectable* header_template_ = nullptr;
    IInspectable* icon_source_ = nullptr;
    boolean is_closable_ = 1;
    boolean is_selected_ = 0;
    openxaml::TextBlock label_;
    std::vector<openxaml::Element*> visual_children_;
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
    SplitButtonObject() {
        label_.set_text("+  v");
        label_.set_font_size(14.0);
        label_.set_foreground_brush(openxaml::BrushValue::SolidColor(
            {0xff, 0xf2, 0xf2, 0xf2}));
        const auto backing = openxaml::BrushValue::SolidColor(
            {0xff, 0x2b, 0x2b, 0x2b});
        label_.set_background_brush(backing);
        label_.set_box_size({52.0, 24.0});
        layout_.set_background_brush(backing);
        layout_.set_min_width(52.0);
        layout_.set_min_height(24.0);
        layout_.set_horizontal_content_alignment(
            openxaml::HorizontalAlignment::Stretch);
        layout_.set_vertical_content_alignment(
            openxaml::VerticalAlignment::Stretch);
        visual_children_.push_back(&label_);
        layout_.supplemental = &visual_children_;
        (void)layout_.AttachVisualChild(label_);
    }
    ~SplitButtonObject() override {
        layout_.DetachVisualChild(label_);
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
    OpaqueSyntheticTextBlock label_;
    std::vector<openxaml::Element*> visual_children_;
};

inline constexpr GUID IID_IMuxcInfoBar = {
    0x273ffde8, 0x9324, 0x55b7,
    {0x9f, 0xfe, 0x7d, 0x99, 0x5a, 0x8a, 0xf5, 0x6b}};
inline constexpr GUID IID_IMuxcInfoBarFactory = {
    0x60618a60, 0x9be7, 0x5df5,
    {0xbe, 0x0d, 0x93, 0x3d, 0x34, 0xdd, 0xb4, 0x4c}};

// Handwritten from the pinned Microsoft.UI.Xaml WinMD projection. WinUI's
// interfaces are not part of the Windows SDK headers used to build this DLL,
// but Terminal consumes this exact default-interface vtable.
struct IMuxcInfoBar : IInspectable {
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
    virtual HRESULT STDMETHODCALLTYPE add_CloseButtonClick(
        void*, EventRegistrationToken*) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_CloseButtonClick(
        EventRegistrationToken) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_Closing(
        void*, EventRegistrationToken*) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_Closing(EventRegistrationToken) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_Closed(
        void*, EventRegistrationToken*) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_Closed(EventRegistrationToken) = 0;
};

struct IMuxcInfoBarFactory : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE CreateInstance(void*, void**, void**) = 0;
};

class InfoBarObject final
    : public ContentControlObjectBase<openxaml::InfoBar>,
      public IMuxcInfoBar {
public:
    using Base = ContentControlObjectBase<openxaml::InfoBar>;
    using PrimaryInterface = IMuxcInfoBar;

    InfoBarObject() {
        // A closed InfoBar is absent from layout. x:Load controls whether the
        // object exists at all; IsOpen controls the realized element once it
        // has been materialized.
        layout_.set_visibility(openxaml::Visibility::Collapsed);
    }
    ~InfoBarObject() override {
        WindowsDeleteString(title_);
        WindowsDeleteString(message_);
        ReleaseObject(icon_source_);
        ReleaseObject(close_button_style_);
        ReleaseObject(close_button_command_);
        ReleaseObject(close_button_command_parameter_);
        ReleaseObject(action_button_);
        ReleaseObject(content_template_);
    }

    const wchar_t* RuntimeClassName() const override {
        return L"Microsoft.UI.Xaml.Controls.InfoBar";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(IID_IMuxcInfoBar, IMuxcInfoBar)
        return QueryControlInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE get_IsOpen(boolean* value) override {
        if (!value) return E_POINTER;
        *value = is_open_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_IsOpen(boolean value) override {
        const boolean next = value != 0;
        if (is_open_ == next) return S_OK;
        is_open_ = next;
        layout_.set_visibility(next ? openxaml::Visibility::Visible
                                    : openxaml::Visibility::Collapsed);
        return S_OK;
    }

#define OPENXAML_INFOBAR_STRING(name, field)                               \
    HRESULT STDMETHODCALLTYPE get_##name(HSTRING* value) override {        \
        if (!value) return E_POINTER;                                       \
        return WindowsDuplicateString(field, value);                        \
    }                                                                       \
    HRESULT STDMETHODCALLTYPE put_##name(HSTRING value) override {          \
        HSTRING next = nullptr;                                             \
        HRESULT hr = WindowsDuplicateString(value, &next);                  \
        if (FAILED(hr)) return hr;                                          \
        WindowsDeleteString(field);                                         \
        field = next;                                                       \
        layout_.InvalidateRender(true);                                     \
        return S_OK;                                                        \
    }
    OPENXAML_INFOBAR_STRING(Title, title_)
    OPENXAML_INFOBAR_STRING(Message, message_)
#undef OPENXAML_INFOBAR_STRING

    HRESULT STDMETHODCALLTYPE get_Severity(INT32* value) override {
        if (!value) return E_POINTER;
        *value = severity_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Severity(INT32 value) override {
        if (value < 0 || value > 3) return E_INVALIDARG;
        severity_ = value;
        layout_.InvalidateRender(false);
        return S_OK;
    }

#define OPENXAML_INFOBAR_BOOL(name, field, layout_change)                  \
    HRESULT STDMETHODCALLTYPE get_##name(boolean* value) override {        \
        if (!value) return E_POINTER;                                       \
        *value = field;                                                     \
        return S_OK;                                                        \
    }                                                                       \
    HRESULT STDMETHODCALLTYPE put_##name(boolean value) override {         \
        field = value != 0;                                                 \
        layout_.InvalidateRender(layout_change);                            \
        return S_OK;                                                        \
    }
    OPENXAML_INFOBAR_BOOL(IsIconVisible, is_icon_visible_, true)
    OPENXAML_INFOBAR_BOOL(IsClosable, is_closable_, true)
#undef OPENXAML_INFOBAR_BOOL

#define OPENXAML_INFOBAR_OBJECT(name, field, layout_change)                \
    HRESULT STDMETHODCALLTYPE get_##name(void** value) override {          \
        if (!value) return E_POINTER;                                       \
        *value = field;                                                     \
        if (field) field->AddRef();                                         \
        return S_OK;                                                        \
    }                                                                       \
    HRESULT STDMETHODCALLTYPE put_##name(void* value) override {           \
        auto* next = static_cast<IInspectable*>(value);                     \
        if (next) next->AddRef();                                           \
        ReleaseObject(field);                                               \
        field = next;                                                       \
        layout_.InvalidateRender(layout_change);                            \
        return S_OK;                                                        \
    }
    OPENXAML_INFOBAR_OBJECT(IconSource, icon_source_, true)
    OPENXAML_INFOBAR_OBJECT(CloseButtonStyle, close_button_style_, true)
    OPENXAML_INFOBAR_OBJECT(CloseButtonCommand, close_button_command_, false)
    OPENXAML_INFOBAR_OBJECT(CloseButtonCommandParameter,
                            close_button_command_parameter_, false)
    OPENXAML_INFOBAR_OBJECT(ActionButton, action_button_, true)
    OPENXAML_INFOBAR_OBJECT(ContentTemplate, content_template_, true)
#undef OPENXAML_INFOBAR_OBJECT

    HRESULT STDMETHODCALLTYPE get_Content(void** value) override {
        return Base::get_Content(reinterpret_cast<IInspectable**>(value));
    }
    HRESULT STDMETHODCALLTYPE put_Content(void* value) override {
        return Base::put_Content(static_cast<IInspectable*>(value));
    }
    HRESULT STDMETHODCALLTYPE get_TemplateSettings(void** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return S_OK;
    }

#define OPENXAML_INFOBAR_EVENT(name)                                      \
    HRESULT STDMETHODCALLTYPE add_##name(                                 \
        void* handler, EventRegistrationToken* token) override {          \
        return AddEvent(static_cast<IUnknown*>(handler), token);           \
    }                                                                      \
    HRESULT STDMETHODCALLTYPE remove_##name(                              \
        EventRegistrationToken token) override {                          \
        return RemoveEvent(token);                                         \
    }
    OPENXAML_INFOBAR_EVENT(CloseButtonClick)
    OPENXAML_INFOBAR_EVENT(Closing)
    OPENXAML_INFOBAR_EVENT(Closed)
#undef OPENXAML_INFOBAR_EVENT

private:
    static void ReleaseObject(IInspectable*& value) {
        if (value) value->Release();
        value = nullptr;
    }

    HSTRING title_ = nullptr;
    HSTRING message_ = nullptr;
    IInspectable* icon_source_ = nullptr;
    IInspectable* close_button_style_ = nullptr;
    IInspectable* close_button_command_ = nullptr;
    IInspectable* close_button_command_parameter_ = nullptr;
    IInspectable* action_button_ = nullptr;
    IInspectable* content_template_ = nullptr;
    boolean is_open_ = 0;
    boolean is_icon_visible_ = 1;
    boolean is_closable_ = 1;
    INT32 severity_ = 0;
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

class UserControlObject final : public ContentControlObjectBase<openxaml::UserControl>,
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
inline constexpr GUID IID_IMuxcBitmapIconSourceFactory = {
    0x7d484c14, 0xf5f6, 0x5e39,
    {0xb4, 0xe4, 0xb6, 0x10, 0x8d, 0x2e, 0xe0, 0x95}};
inline constexpr GUID IID_IMuxcIconSource = {
    0x6e3501ed, 0xdd31, 0x51e9,
    {0x8f, 0x14, 0x25, 0x61, 0xf9, 0x9c, 0x8a, 0x8f}};
inline constexpr GUID IID_IMuxcImageIconSource = {
    0xc789d80c, 0x0494, 0x54be,
    {0xb9, 0x41, 0x75, 0x7d, 0x3f, 0x72, 0x30, 0x03}};
inline constexpr GUID IID_IMuxcImageIconSourceFactory = {
    0x24f76321, 0x71bd, 0x530a,
    {0x8c, 0xc8, 0x3f, 0x61, 0x5c, 0xd1, 0x43, 0x7a}};

struct IMuxcBitmapIconSource : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_UriSource(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_UriSource(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ShowAsMonochrome(boolean*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_ShowAsMonochrome(boolean) = 0;
};
struct IMuxcBitmapIconSourceFactory : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE CreateInstance(
        IInspectable* base, IInspectable** inner,
        IMuxcBitmapIconSource** value) = 0;
};
struct IMuxcIconSource : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE CreateIconElement(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Foreground(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Foreground(void*) = 0;
};
struct IMuxcImageIconSource : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_ImageSource(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_ImageSource(void*) = 0;
};
struct IMuxcImageIconSourceFactory : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE CreateInstance(
        IInspectable* base, IInspectable** inner,
        IMuxcImageIconSource** value) = 0;
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

class MuxcImageIconSourceObject final
    : public ComObject,
      public abi::NotImpl_IDependencyObject,
      public IMuxcImageIconSource,
      public IMuxcIconSource {
public:
    using PrimaryInterface = IMuxcImageIconSource;
    ~MuxcImageIconSourceObject() override {
        if (image_source_) image_source_->Release();
        if (foreground_) foreground_->Release();
    }
    const wchar_t* RuntimeClassName() const override {
        return L"Microsoft.UI.Xaml.Controls.ImageIconSource";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(IID_IMuxcImageIconSource, IMuxcImageIconSource)
        OPENXAML_QI_ARM(IID_IMuxcIconSource, IMuxcIconSource)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_IDependencyObject,
                        wux::IDependencyObject)
        OPENXAML_QI_ARM(IID_IUnknown, IMuxcImageIconSource)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable, IMuxcImageIconSource)
        *object = nullptr;
        return TraceQueryInterfaceMiss(RuntimeClassName(), iid);
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE get_ImageSource(void** value) override {
        return Get(image_source_, value);
    }
    HRESULT STDMETHODCALLTYPE put_ImageSource(void* value) override {
        return Put(image_source_, static_cast<IInspectable*>(value));
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
    IInspectable* image_source_ = nullptr;
    IInspectable* foreground_ = nullptr;
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

inline constexpr GUID IID_IMuxcImageIcon = {
    0xe30c7c6c, 0x2026, 0x5e76, {0xa2, 0x71, 0x1b, 0x9f, 0xab, 0x3f, 0x84, 0x9d}};
inline constexpr GUID IID_IMuxcImageIconFactory = {
    0x235e0279, 0xa7d0, 0x5fda, {0xa3, 0x08, 0x9b, 0x7c, 0xb9, 0xc4, 0xc9, 0x12}};

struct IMuxcImageIcon : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Source(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Source(void*) = 0;
};
struct IMuxcImageIconFactory : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE CreateInstance(void*, void**, void**) = 0;
};

class MuxcImageIconObject final : public XamlElement,
                                  public abi::NotImpl_IIconElement,
                                  public IMuxcImageIcon {
public:
    using PrimaryInterface = IMuxcImageIcon;
    ~MuxcImageIconObject() override {
        if (source_) source_->Release();
    }
    openxaml::Element* Layout() override { return &layout_; }
    const wchar_t* RuntimeClassName() const override {
        return L"Microsoft.UI.Xaml.Controls.ImageIcon";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(IID_IMuxcImageIcon, IMuxcImageIcon)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IIconElement,
                        wuxc::IIconElement)
        return QueryElementInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()
    HRESULT STDMETHODCALLTYPE get_Source(void** value) override {
        if (!value) return E_POINTER;
        *value = source_;
        if (source_) source_->AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Source(void* value) override {
        auto* source = static_cast<IInspectable*>(value);
        if (source) source->AddRef();
        if (source_) source_->Release();
        source_ = source;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Foreground(wuxm::IBrush** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Foreground(wuxm::IBrush*) override { return S_OK; }
private:
    openxaml::FontIcon layout_;
    IInspectable* source_ = nullptr;
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
    ButtonObject() { layout_.supplemental = &visual_children_; }
    ~ButtonObject() override {
        if (label_attached_) layout_.DetachVisualChild(label_);
        if (flyout_) flyout_->Release();
    }
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

    HRESULT STDMETHODCALLTYPE put_Name(HSTRING value) override {
        const HRESULT hr = XamlElement::put_Name(value);
        if (SUCCEEDED(hr)) ConfigureNamedVisual(Utf8FromHString(value));
        return hr;
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
    void ConfigureNamedVisual(const std::string& name) {
        std::string glyph;
        if (name == "MinimizeButton") glyph = "-";
        else if (name == "MaximizeButton") glyph = "[]";
        else if (name == "CloseButton") glyph = "x";
        else return;

        label_.set_text(std::move(glyph));
        label_.set_font_size(14.0);
        label_.set_foreground_brush(openxaml::BrushValue::SolidColor(
            {0xff, 0xf2, 0xf2, 0xf2}));
        const auto backing = openxaml::BrushValue::SolidColor(
            {0xff, 0x2b, 0x2b, 0x2b});
        label_.set_background_brush(backing);
        // Keep ClearType on a fully opaque integer-sized stratum. A glyph-sized
        // TextBlock has fractional edges, leaving antialiased backing pixels
        // that cannot legally carry ClearType coverage in DirectComposition.
        label_.set_box_size({46.0, 40.0});
        layout_.set_background_brush(backing);
        layout_.set_horizontal_content_alignment(
            openxaml::HorizontalAlignment::Stretch);
        layout_.set_vertical_content_alignment(
            openxaml::VerticalAlignment::Stretch);
        if (!label_attached_) {
            label_attached_ = layout_.AttachVisualChild(label_);
            if (label_attached_) visual_children_.push_back(&label_);
        }
        label_.InvalidateRender(true);
    }

    wuxcp::IFlyoutBase* flyout_ = nullptr;
    OpaqueSyntheticTextBlock label_;
    std::vector<openxaml::Element*> visual_children_;
    bool label_attached_ = false;
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
    using ValueChangedHandler = wuxcp::IRangeBaseValueChangedEventHandler;
    ~ScrollBarObject() override {
        for (auto& [_, handler] : value_changed_handlers_) handler->Release();
    }
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

    HRESULT STDMETHODCALLTYPE get_Minimum(DOUBLE* value) override {
        return CopyDouble(minimum_, value);
    }
    HRESULT STDMETHODCALLTYPE put_Minimum(DOUBLE value) override {
        if (!ValidDouble(value)) return E_INVALIDARG;
        const double old_minimum = minimum_;
        const double old_maximum = maximum_;
        if (value <= old_minimum) {
            // Expanding the range publishes Minimum before an uncoerced Value
            // is allowed to return to the newly available interval.
            minimum_ = value;
            return CoerceValue(value, old_maximum);
        }

        // Contracting the range publishes a required Maximum adjustment
        // first, then Value, then Minimum. This ordering is observable from a
        // reentrant ValueChanged handler and follows RangeBase::SetValue.
        if (value > maximum_) maximum_ = value;
        const HRESULT changed = CoerceValue(value, maximum_);
        if (FAILED(changed)) return changed;
        minimum_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Maximum(DOUBLE* value) override {
        return CopyDouble(maximum_, value);
    }
    HRESULT STDMETHODCALLTYPE put_Maximum(DOUBLE value) override {
        if (!ValidDouble(value)) return E_INVALIDARG;
        const double old_maximum = maximum_;
        if (value >= old_maximum) {
            // Expanding the range publishes Maximum before restoring the
            // caller's uncoerced Value.
            maximum_ = value;
            return CoerceValue(minimum_, value);
        }

        // Contracting publishes the coerced Value while handlers can still
        // observe the old Maximum, then commits the new bound.
        const double coerced_maximum = std::max(value, minimum_);
        const HRESULT changed = CoerceValue(minimum_, coerced_maximum);
        if (FAILED(changed)) return changed;
        maximum_ = coerced_maximum;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_SmallChange(DOUBLE* value) override {
        return CopyDouble(small_change_, value);
    }
    HRESULT STDMETHODCALLTYPE put_SmallChange(DOUBLE value) override {
        if (!ValidDouble(value)) return E_INVALIDARG;
        small_change_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_LargeChange(DOUBLE* value) override {
        return CopyDouble(large_change_, value);
    }
    HRESULT STDMETHODCALLTYPE put_LargeChange(DOUBLE value) override {
        if (!ValidDouble(value)) return E_INVALIDARG;
        large_change_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Value(DOUBLE* value) override {
        return CopyDouble(value_, value);
    }
    HRESULT STDMETHODCALLTYPE put_Value(DOUBLE value) override {
        if (!ValidDouble(value)) return E_INVALIDARG;
        uncoerced_value_ = value;
        return CoerceValue(minimum_, maximum_);
    }
    HRESULT STDMETHODCALLTYPE get_ViewportSize(DOUBLE* value) override {
        return CopyDouble(viewport_size_, value);
    }
    HRESULT STDMETHODCALLTYPE put_ViewportSize(DOUBLE value) override {
        viewport_size_ = value;
        return S_OK;
    }

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
        return AddTypedEvent(handler, token, value_changed_handlers_);
    }
    HRESULT STDMETHODCALLTYPE remove_ValueChanged(EventRegistrationToken token) override {
        return RemoveTypedEvent(token, value_changed_handlers_);
    }
    HRESULT STDMETHODCALLTYPE add_Scroll(
        wuxcp::IScrollEventHandler* handler, EventRegistrationToken* token) override {
        return AddEvent(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_Scroll(EventRegistrationToken token) override {
        return RemoveEvent(token);
    }

private:
    static bool ValidDouble(double value) noexcept {
        return std::isfinite(value);
    }
    static HRESULT CopyDouble(double source, DOUBLE* value) noexcept {
        if (!value) return E_POINTER;
        *value = source;
        return S_OK;
    }

    HRESULT CoerceValue(double minimum, double maximum) {
        const double coerced = std::max(std::min(uncoerced_value_, maximum),
                                        minimum);
        if (coerced == value_) return S_OK;
        const double old_value = value_;
        value_ = coerced;
        return RaiseValueChanged(old_value, coerced);
    }

    HRESULT RaiseValueChanged(double old_value, double new_value) {
        // Range values are projection state until a retained ScrollBar
        // template owns a thumb. Do not fabricate a layout/render invalidation
        // for a scene property that the current Element model does not read.
        if (value_changed_handlers_.empty()) return S_OK;
        std::vector<ValueChangedHandler*> snapshot;
        try {
            snapshot.reserve(value_changed_handlers_.size());
        } catch (...) {
            return E_OUTOFMEMORY;
        }
        for (const auto& [_, handler] : value_changed_handlers_) {
            handler->AddRef();
            snapshot.push_back(handler);
        }

        IInspectable* const sender = static_cast<IInspectable*>(
            static_cast<wuxcp::IScrollBar*>(this));
        auto* args = new (std::nothrow) RangeBaseValueChangedEventArgsObject(
            sender, old_value, new_value);
        if (!args) {
            for (ValueChangedHandler* handler : snapshot) handler->Release();
            return E_OUTOFMEMORY;
        }
        auto* const event_args =
            static_cast<wuxcp::IRangeBaseValueChangedEventArgs*>(args);
        HRESULT result = S_OK;
        try {
            for (ValueChangedHandler* handler : snapshot) {
                result = handler->Invoke(sender, event_args);
                if (FAILED(result)) break;
            }
        } catch (...) {
            result = E_FAIL;
        }
        event_args->Release();
        for (ValueChangedHandler* handler : snapshot) handler->Release();
        return result;
    }

    DOUBLE minimum_ = 0.0;
    DOUBLE maximum_ = 1.0;
    DOUBLE small_change_ = 0.1;
    DOUBLE large_change_ = 1.0;
    DOUBLE value_ = 0.0;
    DOUBLE uncoerced_value_ = 0.0;
    DOUBLE viewport_size_ = 0.0;
    wuxc::Orientation orientation_ = wuxc::Orientation_Vertical;
    wuxcp::ScrollingIndicatorMode indicator_mode_ =
        wuxcp::ScrollingIndicatorMode_None;
    std::map<LONGLONG, ValueChangedHandler*> value_changed_handlers_;
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

class RectangleObject final : public ShapeObjectBase<openxaml::Rectangle>,
                              public abi::NotImpl_IRectangle {
public:
    using PrimaryInterface = wuxs::IRectangle;
    const wchar_t* RuntimeClassName() const override { return L"Windows.UI.Xaml.Shapes.Rectangle"; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Shapes_IRectangle, wuxs::IRectangle)
        return QueryShapeInterface(iid, object);
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
