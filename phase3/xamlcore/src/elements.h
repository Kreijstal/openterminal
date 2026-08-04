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
#include "grid.h"
#include "openxaml_abi_stubs.h"
#include "stack_panel.h"

namespace openxaml::winrt {

namespace wf = ABI::Windows::Foundation;
namespace wux = ABI::Windows::UI::Xaml;
namespace wuxc = ABI::Windows::UI::Xaml::Controls;

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

// --- the element base ---------------------------------------------------------

class XamlElement : public ComObject,
                    public abi::NotImpl_IDependencyObject,
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
        *value = Layout()->use_layout_rounding ? 1 : 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_UseLayoutRounding(boolean value) override {
        Layout()->use_layout_rounding = value != 0;
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
        *value = Layout()->field;                                      \
        return S_OK;                                                   \
    }                                                                  \
    HRESULT STDMETHODCALLTYPE put_##name(DOUBLE value) override {      \
        Layout()->field = value;                                       \
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
        const openxaml::Thickness& margin = Layout()->margin;
        *value = {margin.left, margin.top, margin.right, margin.bottom};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Margin(wux::Thickness value) override {
        Layout()->margin = {value.Left, value.Top, value.Right, value.Bottom};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_HorizontalAlignment(wux::HorizontalAlignment* value) override {
        if (!value) return E_POINTER;
        switch (Layout()->horizontal_alignment) {
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
                Layout()->horizontal_alignment = openxaml::HorizontalAlignment::Left; break;
            case wux::HorizontalAlignment_Center:
                Layout()->horizontal_alignment = openxaml::HorizontalAlignment::Center; break;
            case wux::HorizontalAlignment_Right:
                Layout()->horizontal_alignment = openxaml::HorizontalAlignment::Right; break;
            case wux::HorizontalAlignment_Stretch:
                Layout()->horizontal_alignment = openxaml::HorizontalAlignment::Stretch; break;
            default:
                return E_INVALIDARG;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_VerticalAlignment(wux::VerticalAlignment* value) override {
        if (!value) return E_POINTER;
        switch (Layout()->vertical_alignment) {
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
                Layout()->vertical_alignment = openxaml::VerticalAlignment::Top; break;
            case wux::VerticalAlignment_Center:
                Layout()->vertical_alignment = openxaml::VerticalAlignment::Center; break;
            case wux::VerticalAlignment_Bottom:
                Layout()->vertical_alignment = openxaml::VerticalAlignment::Bottom; break;
            case wux::VerticalAlignment_Stretch:
                Layout()->vertical_alignment = openxaml::VerticalAlignment::Stretch; break;
            default:
                return E_INVALIDARG;
        }
        return S_OK;
    }

protected:
    // The interfaces every element carries. A concrete class tries its own
    // first, then falls through to here.
    HRESULT QueryElementInterface(REFIID iid, void** object) {
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_IUIElement, wux::IUIElement)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_IFrameworkElement, wux::IFrameworkElement)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_IDependencyObject, wux::IDependencyObject)
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
        const openxaml::Thickness& t = children_.border_thickness;
        *value = {t.left, t.top, t.right, t.bottom};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_BorderThickness(wux::Thickness value) override {
        children_.border_thickness = {value.Left, value.Top, value.Right, value.Bottom};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Padding(wux::Thickness* value) override {
        if (!value) return E_POINTER;
        const openxaml::Thickness& t = children_.padding;
        *value = {t.left, t.top, t.right, t.bottom};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Padding(wux::Thickness value) override {
        children_.padding = {value.Left, value.Top, value.Right, value.Bottom};
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
        *value = layout_.orientation == openxaml::Orientation::Horizontal
                     ? wuxc::Orientation_Horizontal
                     : wuxc::Orientation_Vertical;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Orientation(wuxc::Orientation value) override {
        layout_.orientation = value == wuxc::Orientation_Horizontal
                                  ? openxaml::Orientation::Horizontal
                                  : openxaml::Orientation::Vertical;
        return S_OK;
    }
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

}  // namespace openxaml::winrt

#endif  // OPENXAML_ELEMENTS_H
