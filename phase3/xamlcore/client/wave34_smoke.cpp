// Activation and QueryInterface smoke test for the Wave-3/4 DLL surface.

#include "sdk.h"

#include <roapi.h>

#include <cstdio>
#include <cwchar>

#include "openxaml_iids.h"

namespace wuxc = ABI::Windows::UI::Xaml::Controls;
namespace wuxcp = ABI::Windows::UI::Xaml::Controls::Primitives;
namespace wuxs = ABI::Windows::UI::Xaml::Shapes;
namespace wux = ABI::Windows::UI::Xaml;
namespace wf = ABI::Windows::Foundation;

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

int main() {
    if (FAILED(RoInitialize(RO_INIT_SINGLETHREADED))) return 1;
    int failures = 0;
    const auto check = [&](bool condition, const char* what) {
        if (!condition) {
            std::fprintf(stderr, "FAIL %s\n", what);
            ++failures;
        }
    };

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

    auto* page = Activate<wuxc::IPage>(L"Windows.UI.Xaml.Controls.Page",
                                       openxaml::iid::Windows_UI_Xaml_Controls_IPage);
    check(page != nullptr, "Page activation");
    if (page) page->Release();

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

    auto* rectangle = Activate<wuxs::IRectangle>(L"Windows.UI.Xaml.Shapes.Rectangle",
        openxaml::iid::Windows_UI_Xaml_Shapes_IRectangle);
    check(rectangle != nullptr, "Rectangle activation");
    if (rectangle) {
        check(SUCCEEDED(rectangle->put_RadiusX(4)), "Rectangle radius setter");
        DOUBLE radius = 0;
        check(SUCCEEDED(rectangle->get_RadiusX(&radius)) && radius == 4,
              "Rectangle radius round-trip");
        rectangle->Release();
    }

    auto* text_box = Activate<wuxc::ITextBox>(L"Windows.UI.Xaml.Controls.TextBox",
                                               openxaml::iid::Windows_UI_Xaml_Controls_ITextBox);
    check(text_box != nullptr, "TextBox activation");
    if (text_box) text_box->Release();
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
    if (path) path->Release();
    auto* path_icon = Activate<wuxc::IPathIcon>(L"Windows.UI.Xaml.Controls.PathIcon",
                                                openxaml::iid::Windows_UI_Xaml_Controls_IPathIcon);
    check(path_icon != nullptr, "PathIcon activation");
    if (path_icon) path_icon->Release();

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
