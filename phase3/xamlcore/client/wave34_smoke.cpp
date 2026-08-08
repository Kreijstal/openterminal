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

    RoUninitialize();
    if (!failures) std::puts("Wave 3/4 activation smoke passed");
    return failures ? 1 : 0;
}
