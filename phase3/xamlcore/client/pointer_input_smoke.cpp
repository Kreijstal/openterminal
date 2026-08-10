// Routed mouse/capture acceptance for a DesktopWindowXamlSource island.

#include "sdk.h"

#include <roapi.h>

#include <cstdio>
#include <cwchar>
#include <functional>
#include <string>

#include "com.h"
#include "openxaml_iids.h"

namespace wux = ABI::Windows::UI::Xaml;
namespace wuxc = ABI::Windows::UI::Xaml::Controls;
namespace wuxh = ABI::Windows::UI::Xaml::Hosting;
namespace wuxi = ABI::Windows::UI::Xaml::Input;
namespace wui = ABI::Windows::UI::Input;
namespace wf = ABI::Windows::Foundation;

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

class PointerHandler final : public wuxi::IPointerEventHandler {
public:
    using Callback = std::function<void(wuxi::IPointerRoutedEventArgs*)>;
    explicit PointerHandler(Callback callback) : callback_(std::move(callback)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override {
        if (!value) return E_POINTER;
        if (IsEqualGUID(iid, IID_IUnknown)) {
            *value = static_cast<wuxi::IPointerEventHandler*>(this);
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
        const ULONG left = static_cast<ULONG>(InterlockedDecrement(&references_));
        if (!left) delete this;
        return left;
    }
    HRESULT STDMETHODCALLTYPE Invoke(
        IInspectable*, wuxi::IPointerRoutedEventArgs* args) override {
        callback_(args);
        return S_OK;
    }

private:
    ~PointerHandler() = default;
    LONG references_ = 1;
    Callback callback_;
};

void Pump() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

}  // namespace

int main() {
    if (FAILED(RoInitialize(RO_INIT_SINGLETHREADED))) return 2;
    HWND parent = CreateWindowExW(0, L"STATIC", L"OpenXaml pointer smoke",
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
          "create island");
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
          "activate pointer tree");
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

    std::string order;
    bool press_fields = true;
    bool captured = false;
    bool wheel_fields = true;
    wuxi::IPointerRoutedEventArgs* retained_args = nullptr;
    EventRegistrationToken child_pressed{}, root_pressed{}, child_moved{};
    EventRegistrationToken root_moved{}, child_released{}, capture_lost{};
    EventRegistrationToken child_wheel{}, root_wheel{};
    EventRegistrationToken entered_token{}, exited_token{}, canceled_token{};

    auto* press = new PointerHandler([&](auto* args) {
        order += 'C';
        if (!retained_args) {
            retained_args = args;
            retained_args->AddRef();
        }
        wuxi::IPointer* pointer = nullptr;
        wui::IPointerPoint* point = nullptr;
        ABI::Windows::System::VirtualKeyModifiers modifiers{};
        args->get_Pointer(&pointer);
        args->get_KeyModifiers(&modifiers);
        args->GetCurrentPoint(child, &point);
        UINT32 pointer_id = 0;
        ABI::Windows::Devices::Input::PointerDeviceType type{};
        boolean contact = 0;
        wf::Point position{};
        wui::IPointerPointProperties* properties = nullptr;
        boolean left = 0;
        wui::PointerUpdateKind update{};
        if (pointer) {
            pointer->get_PointerId(&pointer_id);
            pointer->get_PointerDeviceType(&type);
            pointer->get_IsInContact(&contact);
        }
        if (point) {
            point->get_Position(&position);
            point->get_Properties(&properties);
        }
        if (properties) {
            properties->get_IsLeftButtonPressed(&left);
            properties->get_PointerUpdateKind(&update);
        }
        const int modifier_bits = static_cast<int>(modifiers);
        press_fields = press_fields && pointer_id == 1 &&
            type == ABI::Windows::Devices::Input::PointerDeviceType_Mouse &&
            contact && left &&
            update == wui::PointerUpdateKind_LeftButtonPressed &&
            position.X == 12.0f && position.Y == 14.0f &&
            (modifier_bits & ABI::Windows::System::VirtualKeyModifiers_Shift) &&
            (modifier_bits & ABI::Windows::System::VirtualKeyModifiers_Control);
        boolean result = 0;
        if (pointer) child->CapturePointer(pointer, &result);
        captured = result != 0;
        if (properties) properties->Release();
        if (point) point->Release();
        if (pointer) pointer->Release();
    });
    auto* root_press = new PointerHandler([&](auto*) { order += 'R'; });
    auto* move = new PointerHandler([&](auto*) { order += 'M'; });
    auto* root_move = new PointerHandler([&](auto*) { order += 'N'; });
    auto* release = new PointerHandler([&](auto* args) {
        order += 'L';
        wuxi::IPointer* pointer = nullptr;
        args->get_Pointer(&pointer);
        if (pointer) {
            child->ReleasePointerCapture(pointer);
            pointer->Release();
        }
    });
    auto* lost = new PointerHandler([&](auto*) { order += 'X'; });
    auto* wheel = new PointerHandler([&](auto* args) {
        order += 'W';
        wui::IPointerPoint* point = nullptr;
        args->GetCurrentPoint(child, &point);
        wf::Point position{};
        wui::IPointerPointProperties* properties = nullptr;
        INT32 delta = 0;
        boolean horizontal = 1;
        if (point) {
            point->get_Position(&position);
            point->get_Properties(&properties);
        }
        if (properties) {
            properties->get_MouseWheelDelta(&delta);
            properties->get_IsHorizontalMouseWheel(&horizontal);
        }
        wheel_fields = wheel_fields && delta == WHEEL_DELTA && !horizontal &&
            position.X == 20.0f && position.Y == 24.0f;
        args->put_Handled(1);
        if (properties) properties->Release();
        if (point) point->Release();
    });
    auto* root_wheel_handler = new PointerHandler([&](auto*) { order += 'Q'; });
    auto* entered = new PointerHandler([&](auto*) { order += 'E'; });
    auto* exited = new PointerHandler([&](auto*) { order += 'O'; });
    auto* canceled = new PointerHandler([&](auto*) { order += 'K'; });

    child->add_PointerPressed(press, &child_pressed);
    root->add_PointerPressed(root_press, &root_pressed);
    child->add_PointerMoved(move, &child_moved);
    root->add_PointerMoved(root_move, &root_moved);
    child->add_PointerReleased(release, &child_released);
    child->add_PointerCaptureLost(lost, &capture_lost);
    child->add_PointerWheelChanged(wheel, &child_wheel);
    root->add_PointerWheelChanged(root_wheel_handler, &root_wheel);
    child->add_PointerEntered(entered, &entered_token);
    child->add_PointerExited(exited, &exited_token);
    child->add_PointerCanceled(canceled, &canceled_token);
    press->Release();
    root_press->Release();
    move->Release();
    root_move->Release();
    release->Release();
    lost->Release();
    wheel->Release();
    root_wheel_handler->Release();
    entered->Release();
    exited->Release();
    canceled->Release();

    SendMessageW(island, WM_LBUTTONDOWN,
                 MK_LBUTTON | MK_SHIFT | MK_CONTROL, MAKELPARAM(12, 14));
    Check(order == "CR", "pressed bubbles child to root");
    Check(press_fields, "pointer/point/properties press ABI fields");
    Check(captured && GetCapture() == island, "element captures island mouse");

    order.clear();
    SendMessageW(island, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(420, 260));
    Check(order == "MN", "captured move routes outside bounds");

    order.clear();
    SendMessageW(island, WM_LBUTTONUP, 0, MAKELPARAM(420, 260));
    Check(order == "LX", "release balances capture and CaptureLost");
    Check(GetCapture() != island, "User32 capture released");
    order.clear();
    SendMessageW(island, WM_MOUSEMOVE, 0, MAKELPARAM(420, 260));
    Check(order.empty(), "uncaptured move outside tree is not routed");

    order.clear();
    SendMessageW(island, WM_MOUSEMOVE, 0, MAKELPARAM(18, 19));
    Check(order == "EMN", "first retained hit raises Entered before Moved");
    order.clear();
    SendMessageW(island, WM_MOUSEMOVE, 0, MAKELPARAM(420, 260));
    Check(order == "O", "leaving retained bounds raises PointerExited once");

    POINT wheel_point{20, 24};
    ClientToScreen(island, &wheel_point);
    order.clear();
    SendMessageW(island, WM_MOUSEWHEEL,
                 MAKEWPARAM(MK_CONTROL, WHEEL_DELTA),
                 MAKELPARAM(wheel_point.x, wheel_point.y));
    Check(order == "W", "handled wheel suppresses ancestor");
    Check(wheel_fields, "wheel screen/client conversion and delta fields");

    // Tokens are scoped to their event registry. Removing a pressed token
    // from PointerMoved must not remove either registration.
    child->remove_PointerMoved(child_pressed);
    order.clear();
    SendMessageW(island, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(12, 14));
    Check(order == "CR", "pointer event token registries are separated");
    order.clear();
    SendMessageW(island, WM_CANCELMODE, 0, 0);
    Check(order == "KX", "cancel raises Canceled then CaptureLost");
    Check(GetCapture() != island, "cancel releases User32 capture");

    child->remove_PointerPressed(child_pressed);
    root->remove_PointerPressed(root_pressed);
    child->remove_PointerMoved(child_moved);
    root->remove_PointerMoved(root_moved);
    child->remove_PointerReleased(child_released);
    child->remove_PointerCaptureLost(capture_lost);
    child->remove_PointerWheelChanged(child_wheel);
    root->remove_PointerWheelChanged(root_wheel);
    child->remove_PointerEntered(entered_token);
    child->remove_PointerExited(exited_token);
    child->remove_PointerCanceled(canceled_token);

    // A handler may retain routed args. Detaching and releasing the island
    // root must not leave GetCurrentPoint(relativeTo) with a raw Element.
    if (source) source->put_Content(nullptr);
    if (root_framework) { root_framework->Release(); root_framework = nullptr; }
    if (root) { root->Release(); root = nullptr; }
    if (root_control) { root_control->Release(); root_control = nullptr; }
    wui::IPointerPoint* retained_point = nullptr;
    Check(retained_args && child &&
              SUCCEEDED(retained_args->GetCurrentPoint(child, &retained_point)) &&
              retained_point,
          "retained pointer args keep detached root transform owner alive");
    if (retained_point) retained_point->Release();
    if (retained_args) { retained_args->Release(); retained_args = nullptr; }

    ABI::Windows::Foundation::IClosable* closable = nullptr;
    if (source) source->QueryInterface(openxaml::iid::Windows_Foundation_IClosable,
                                       reinterpret_cast<void**>(&closable));
    if (closable) { closable->Close(); closable->Release(); }
    if (child_framework) child_framework->Release();
    if (root_framework) root_framework->Release();
    if (child) child->Release();
    if (root) root->Release();
    if (root_control) root_control->Release();
    if (native) native->Release();
    if (source) source->Release();
    if (parent) DestroyWindow(parent);
    RoUninitialize();

    if (failures) return 1;
    std::puts("pointer input routing checks passed");
    return 0;
}
