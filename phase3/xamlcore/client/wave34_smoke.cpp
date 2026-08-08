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

// A static-only class has no instance to activate: its members live on the
// activation factory, which is what RoGetActivationFactory hands back.
template <class Interface>
Interface* Statics(const wchar_t* name, const GUID& iid) {
    HSTRING class_name = nullptr;
    if (FAILED(WindowsCreateString(name, static_cast<UINT32>(::wcslen(name)), &class_name)))
        return nullptr;
    Interface* result = nullptr;
    const HRESULT got = RoGetActivationFactory(class_name, iid,
                                               reinterpret_cast<void**>(&result));
    WindowsDeleteString(class_name);
    return SUCCEEDED(got) ? result : nullptr;
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
            // The whole padded client, with no padding here: the bars are
            // overlaid on the content rather than reserved out of it, which is
            // what the L3-scroll recordings say and what the layout core does.
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

    RoUninitialize();
    if (!failures) std::puts("Wave 3/4 activation smoke passed");
    return failures ? 1 : 0;
}
