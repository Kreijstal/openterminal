// Typed Tapped/DoubleTapped ABI acceptance through a real XAML island HWND.

#include "sdk.h"

#include <roapi.h>

#include <cstdio>
#include <cwchar>
#include <functional>
#include <string>
#include <utility>

#include "com.h"
#include "openxaml_iids.h"

namespace wux = ABI::Windows::UI::Xaml;
namespace wuxc = ABI::Windows::UI::Xaml::Controls;
namespace wuxh = ABI::Windows::UI::Xaml::Hosting;
namespace wuxi = ABI::Windows::UI::Xaml::Input;

namespace {

inline constexpr GUID IID_IslandNative = {
    0x3cbcf1bf, 0x2f76, 0x4e9c,
    {0x96, 0xab, 0xe8, 0x4b, 0x37, 0x97, 0x25, 0x54}};

int failures = 0;

void Check(bool condition, const char* message) {
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", message);
    ++failures;
}

template <class Interface>
Interface* Activate(const wchar_t* name, const GUID& iid) {
    HSTRING class_name = nullptr;
    WindowsCreateString(name, static_cast<UINT32>(std::wcslen(name)),
                        &class_name);
    IInspectable* instance = nullptr;
    const HRESULT activated = RoActivateInstance(class_name, &instance);
    WindowsDeleteString(class_name);
    if (FAILED(activated) || !instance) return nullptr;
    Interface* result = nullptr;
    const HRESULT queried = instance->QueryInterface(
        iid, reinterpret_cast<void**>(&result));
    instance->Release();
    return SUCCEEDED(queried) ? result : nullptr;
}

template <class Interface, class Args>
class Handler final : public Interface {
public:
    using Callback = std::function<void(Args*)>;
    explicit Handler(Callback callback) : callback_(std::move(callback)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override {
        if (!value) return E_POINTER;
        if (IsEqualGUID(iid, IID_IUnknown)) {
            *value = static_cast<Interface*>(this);
            AddRef();
            return S_OK;
        }
        *value = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return static_cast<ULONG>(InterlockedIncrement(&references_));
    }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG left = static_cast<ULONG>(
            InterlockedDecrement(&references_));
        if (!left) delete this;
        return left;
    }
    HRESULT STDMETHODCALLTYPE Invoke(IInspectable*, Args* args) override {
        callback_(args);
        return S_OK;
    }

private:
    ~Handler() = default;
    LONG references_ = 1;
    Callback callback_;
};

using TappedHandler = Handler<wuxi::ITappedEventHandler,
                              wuxi::ITappedRoutedEventArgs>;
using DoubleTappedHandler = Handler<wuxi::IDoubleTappedEventHandler,
                                    wuxi::IDoubleTappedRoutedEventArgs>;

void Pump() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

void Click(HWND island, int x, int y) {
    SendMessageW(island, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
    SendMessageW(island, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
}

}  // namespace

int main() {
    if (FAILED(RoInitialize(RO_INIT_SINGLETHREADED))) return 2;
    HWND parent = CreateWindowExW(0, L"STATIC", L"OpenXaml tap smoke",
                                  WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                  20, 20, 340, 220, nullptr, nullptr,
                                  GetModuleHandleW(nullptr), nullptr);

    auto* source = Activate<wuxh::IDesktopWindowXamlSource>(
        L"Windows.UI.Xaml.Hosting.DesktopWindowXamlSource",
        openxaml::iid::Windows_UI_Xaml_Hosting_IDesktopWindowXamlSource);
    IDesktopWindowXamlSourceNative* native = nullptr;
    if (source) source->QueryInterface(IID_IslandNative,
                                       reinterpret_cast<void**>(&native));
    HWND island = nullptr;
    Check(parent && source && native &&
              SUCCEEDED(native->AttachToWindow(parent)) &&
              SUCCEEDED(native->get_WindowHandle(&island)),
          "create tap island");
    if (island) SetWindowPos(island, nullptr, 0, 0, 300, 160,
                             SWP_NOACTIVATE | SWP_NOZORDER | SWP_SHOWWINDOW);

    auto* root_control = Activate<wuxc::IContentControl>(
        L"Windows.UI.Xaml.Controls.ContentControl",
        openxaml::iid::Windows_UI_Xaml_Controls_IContentControl);
    auto* child = Activate<wux::IUIElement>(
        L"Windows.UI.Xaml.Controls.UserControl",
        openxaml::iid::Windows_UI_Xaml_IUIElement);
    wux::IUIElement* root = nullptr;
    wux::IFrameworkElement* root_framework = nullptr;
    wux::IFrameworkElement* child_framework = nullptr;
    if (root_control) root_control->QueryInterface(
        openxaml::iid::Windows_UI_Xaml_IUIElement,
        reinterpret_cast<void**>(&root));
    if (root) root->QueryInterface(
        openxaml::iid::Windows_UI_Xaml_IFrameworkElement,
        reinterpret_cast<void**>(&root_framework));
    if (child) child->QueryInterface(
        openxaml::iid::Windows_UI_Xaml_IFrameworkElement,
        reinterpret_cast<void**>(&child_framework));
    Check(root_control && root && root_framework && child && child_framework,
          "activate tap tree");
    if (root_framework) {
        root_framework->put_Width(300.0);
        root_framework->put_Height(160.0);
    }
    if (child_framework) {
        child_framework->put_Width(300.0);
        child_framework->put_Height(160.0);
    }
    if (root_control && child) root_control->put_Content(child);
    if (source && root) source->put_Content(root);
    Pump();

    std::string route;
    bool fields_ok = true;
    wuxi::ITappedRoutedEventArgs* retained_args = nullptr;
    EventRegistrationToken child_tapped{}, root_tapped{};
    EventRegistrationToken child_double{}, root_double{};

    auto* child_tap = new TappedHandler([&](auto* args) {
        route += 't';
        ABI::Windows::Devices::Input::PointerDeviceType device{};
        ABI::Windows::Foundation::Point position{};
        fields_ok = fields_ok && args &&
            SUCCEEDED(args->get_PointerDeviceType(&device)) &&
            device == ABI::Windows::Devices::Input::PointerDeviceType_Mouse &&
            SUCCEEDED(args->GetPosition(child, &position)) &&
            position.X == 12.0f && position.Y == 14.0f;
        if (!retained_args) {
            retained_args = args;
            retained_args->AddRef();
        }
    });
    auto* root_tap = new TappedHandler([&](auto*) { route += 'r'; });
    auto* child_double_tap = new DoubleTappedHandler([&](auto* args) {
        route += 'D';
        ABI::Windows::Foundation::Point position{};
        fields_ok = fields_ok && args &&
            SUCCEEDED(args->GetPosition(child, &position)) &&
            position.X == 12.0f && position.Y == 14.0f;
    });
    auto* root_double_tap = new DoubleTappedHandler(
        [&](auto*) { route += 'R'; });

    child->add_Tapped(child_tap, &child_tapped);
    root->add_Tapped(root_tap, &root_tapped);
    child->add_DoubleTapped(child_double_tap, &child_double);
    root->add_DoubleTapped(root_double_tap, &root_double);
    child_tap->Release();
    root_tap->Release();
    child_double_tap->Release();
    root_double_tap->Release();

    Click(island, 12, 14);
    Check(route == "tr", "Tapped bubbles child to root");
    Check(fields_ok, "Tapped args expose mouse device and relative position");

    route.clear();
    Click(island, 12, 14);
    Check(route == "trDR",
          "second click raises Tapped then typed DoubleTapped route");

    // Removing a Tapped token from the DoubleTapped registry must affect
    // neither event-specific registration.
    child->remove_DoubleTapped(child_tapped);
    route.clear();
    Click(island, 12, 14);
    Check(route == "tr", "tap event tokens remain event-specific");

    route.clear();
    SendMessageW(island, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(12, 14));
    SendMessageW(island, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(200, 120));
    SendMessageW(island, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(12, 14));
    SendMessageW(island, WM_LBUTTONUP, 0, MAKELPARAM(12, 14));
    Check(route.empty(), "drag away and back suppresses tap synthesis");

    child->remove_Tapped(child_tapped);
    root->remove_Tapped(root_tapped);
    child->remove_DoubleTapped(child_double);
    root->remove_DoubleTapped(root_double);

    if (source) source->put_Content(nullptr);
    if (root_framework) { root_framework->Release(); root_framework = nullptr; }
    if (root) { root->Release(); root = nullptr; }
    if (root_control) { root_control->Release(); root_control = nullptr; }
    ABI::Windows::Foundation::Point retained_position{};
    Check(retained_args && SUCCEEDED(
              retained_args->GetPosition(nullptr, &retained_position)) &&
              retained_position.X == 12.0f && retained_position.Y == 14.0f,
          "retained Tapped args remain safe after island detach");
    if (retained_args) retained_args->Release();

    ABI::Windows::Foundation::IClosable* closable = nullptr;
    if (source) source->QueryInterface(openxaml::iid::Windows_Foundation_IClosable,
                                       reinterpret_cast<void**>(&closable));
    if (closable) { closable->Close(); closable->Release(); }
    if (child_framework) child_framework->Release();
    if (child) child->Release();
    if (native) native->Release();
    if (source) source->Release();
    if (parent) DestroyWindow(parent);
    RoUninitialize();

    if (failures) return 1;
    std::puts("tap input ABI checks passed");
    return 0;
}
