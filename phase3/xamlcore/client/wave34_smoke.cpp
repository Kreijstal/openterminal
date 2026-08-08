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
namespace wuxmk = ABI::Windows::UI::Xaml::Markup;

inline constexpr GUID weak_reference_source_iid = {
    0x00000038, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

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

template <class Interface>
Interface* Statics(const wchar_t* name, const GUID& iid) {
    HSTRING class_name = nullptr;
    if (FAILED(WindowsCreateString(name, static_cast<UINT32>(::wcslen(name)), &class_name)))
        return nullptr;
    Interface* result = nullptr;
    const HRESULT queried = RoGetActivationFactory(
        class_name, iid, reinterpret_cast<void**>(&result));
    WindowsDeleteString(class_name);
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

    auto* page = Activate<wuxc::IPage>(L"Windows.UI.Xaml.Controls.Page",
                                       openxaml::iid::Windows_UI_Xaml_Controls_IPage);
    check(page != nullptr, "Page activation");
    if (page) {
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
            // Scrollbars overlay the content in the recorded Windows layout;
            // they do not subtract their 16-pixel tracks from the viewport.
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
    if (path) path->Release();
    auto* path_icon = Activate<wuxc::IPathIcon>(L"Windows.UI.Xaml.Controls.PathIcon",
                                                openxaml::iid::Windows_UI_Xaml_Controls_IPathIcon);
    check(path_icon != nullptr, "PathIcon activation");
    if (path_icon) path_icon->Release();

    RoUninitialize();
    if (!failures) std::puts("Wave 3/4 activation smoke passed");
    return failures ? 1 : 0;
}
