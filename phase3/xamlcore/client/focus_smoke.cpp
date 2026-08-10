// Focus ABI acceptance for one DesktopWindowXamlSource island.

#include "sdk.h"

#include <roapi.h>

#include <cstdio>
#include <cwchar>
#include <functional>
#include <string>

#include "openxaml_iids.h"

namespace wux = ABI::Windows::UI::Xaml;
namespace wuxc = ABI::Windows::UI::Xaml::Controls;
namespace wuxh = ABI::Windows::UI::Xaml::Hosting;
namespace wuxi = ABI::Windows::UI::Xaml::Input;
namespace wf = ABI::Windows::Foundation;

namespace {

inline constexpr GUID IID_OpenXamlDesktopWindowXamlSourceNative = {
    0x3cbcf1bf, 0x2f76, 0x4e9c,
    {0x96, 0xab, 0xe8, 0x4b, 0x37, 0x97, 0x25, 0x54}};
inline constexpr GUID IID_FocusAsyncInfo = {
    0x00000036, 0x0000, 0x0000,
    {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

int failures = 0;

void Check(bool condition, const char* message) {
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", message);
    ++failures;
}

template <class Interface>
Interface* Activate(const wchar_t* name, const GUID& iid) {
    HSTRING class_name = nullptr;
    if (FAILED(WindowsCreateString(name, static_cast<UINT32>(std::wcslen(name)),
                                   &class_name))) return nullptr;
    IInspectable* instance = nullptr;
    const HRESULT hr = RoActivateInstance(class_name, &instance);
    WindowsDeleteString(class_name);
    if (FAILED(hr) || !instance) return nullptr;
    Interface* result = nullptr;
    const HRESULT queried = instance->QueryInterface(
        iid, reinterpret_cast<void**>(&result));
    instance->Release();
    return SUCCEEDED(queried) ? result : nullptr;
}

template <class Interface>
Interface* GetStatics(const wchar_t* name, const GUID& iid) {
    HSTRING class_name = nullptr;
    if (FAILED(WindowsCreateString(name, static_cast<UINT32>(std::wcslen(name)),
                                   &class_name))) return nullptr;
    Interface* result = nullptr;
    const HRESULT hr = RoGetActivationFactory(
        class_name, iid, reinterpret_cast<void**>(&result));
    WindowsDeleteString(class_name);
    return SUCCEEDED(hr) ? result : nullptr;
}

class RoutedHandler final : public wux::IRoutedEventHandler {
public:
    explicit RoutedHandler(std::function<void()> callback)
        : callback_(std::move(callback)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        if (IsEqualGUID(iid, IID_IUnknown)) {
            *object = static_cast<wux::IRoutedEventHandler*>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
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
    HRESULT STDMETHODCALLTYPE Invoke(IInspectable*, wux::IRoutedEventArgs*) override {
        callback_();
        return S_OK;
    }

private:
    ~RoutedHandler() = default;
    LONG references_ = 1;
    std::function<void()> callback_;
};

template <class Handler, class Args>
class FocusChangingHandler final : public Handler {
public:
    explicit FocusChangingHandler(std::function<void(Args*)> callback)
        : callback_(std::move(callback)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        if (IsEqualGUID(iid, IID_IUnknown)) {
            *object = static_cast<Handler*>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
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
    HRESULT STDMETHODCALLTYPE Invoke(wux::IUIElement*, Args* args) override {
        callback_(args);
        return S_OK;
    }
private:
    ~FocusChangingHandler() = default;
    LONG references_ = 1;
    std::function<void(Args*)> callback_;
};

void Pump() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

struct OffThreadFocus {
    wuxc::IControl* control = nullptr;
    HRESULT result = E_FAIL;
    boolean focused = 1;
};

DWORD WINAPI TryFocusOffThread(void* context) {
    auto* attempt = static_cast<OffThreadFocus*>(context);
    attempt->result = attempt->control->Focus(
        wux::FocusState_Programmatic, &attempt->focused);
    return 0;
}

}  // namespace

int main() {
    if (FAILED(RoInitialize(RO_INIT_SINGLETHREADED))) return 2;

    HWND parent = CreateWindowExW(0, L"STATIC", L"OpenXaml focus smoke",
                                  WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                  20, 20, 320, 180, nullptr, nullptr,
                                  GetModuleHandleW(nullptr), nullptr);
    Check(parent != nullptr, "create parent HWND");

    auto* source = Activate<wuxh::IDesktopWindowXamlSource>(
        L"Windows.UI.Xaml.Hosting.DesktopWindowXamlSource",
        openxaml::iid::Windows_UI_Xaml_Hosting_IDesktopWindowXamlSource);
    IDesktopWindowXamlSourceNative* native = nullptr;
    wf::IClosable* closable = nullptr;
    if (source) {
        source->QueryInterface(IID_OpenXamlDesktopWindowXamlSourceNative,
                               reinterpret_cast<void**>(&native));
        source->QueryInterface(openxaml::iid::Windows_Foundation_IClosable,
                               reinterpret_cast<void**>(&closable));
    }
    Check(source && native && closable, "activate island interfaces");

    HWND island_window = nullptr;
    if (native) {
        Check(SUCCEEDED(native->AttachToWindow(parent)), "attach island HWND");
        Check(SUCCEEDED(native->get_WindowHandle(&island_window)) && island_window,
              "get island HWND");
        SetWindowPos(island_window, nullptr, 0, 0, 300, 140,
                     SWP_NOACTIVATE | SWP_NOZORDER | SWP_SHOWWINDOW);
    }

    auto* element = Activate<wux::IUIElement>(
        L"Windows.UI.Xaml.Controls.UserControl",
        openxaml::iid::Windows_UI_Xaml_IUIElement);
    wuxc::IControl* control = nullptr;
    wux::IUIElement5* element5 = nullptr;
    if (element) element->QueryInterface(
        openxaml::iid::Windows_UI_Xaml_Controls_IControl,
        reinterpret_cast<void**>(&control));
    if (element) element->QueryInterface(
        openxaml::iid::Windows_UI_Xaml_IUIElement5,
        reinterpret_cast<void**>(&element5));
    Check(element && control, "activate UserControl IControl");
    auto* root_control = Activate<wuxc::IContentControl>(
        L"Windows.UI.Xaml.Controls.ContentControl",
        openxaml::iid::Windows_UI_Xaml_Controls_IContentControl);
    wux::IUIElement* root_element = nullptr;
    if (root_control) root_control->QueryInterface(
        openxaml::iid::Windows_UI_Xaml_IUIElement,
        reinterpret_cast<void**>(&root_element));
    Check(root_control && root_element, "activate ContentControl focus parent");
    if (root_control && element)
        Check(SUCCEEDED(root_control->put_Content(element)),
              "make UserControl a visual child");
    if (source && root_element)
        Check(SUCCEEDED(source->put_Content(root_element)),
              "host ContentControl root");

    auto* focus5 = GetStatics<wuxi::IFocusManagerStatics5>(
        L"Windows.UI.Xaml.Input.FocusManager",
        openxaml::iid::Windows_UI_Xaml_Input_IFocusManagerStatics5);
    auto* focus7 = GetStatics<wuxi::IFocusManagerStatics7>(
        L"Windows.UI.Xaml.Input.FocusManager",
        openxaml::iid::Windows_UI_Xaml_Input_IFocusManagerStatics7);
    Check(focus5 && focus7, "activate FocusManager statics5/statics7");

    wux::IUIElement10* child_element10 = nullptr;
    if (element) element->QueryInterface(
        openxaml::iid::Windows_UI_Xaml_IUIElement10,
        reinterpret_cast<void**>(&child_element10));
    wux::IXamlRoot* xaml_root = nullptr;
    if (child_element10) child_element10->get_XamlRoot(&xaml_root);
    Check(xaml_root != nullptr, "visual child inherits hosted XamlRoot identity");
    wux::IUIElement* xaml_root_content = nullptr;
    if (xaml_root) xaml_root->get_Content(&xaml_root_content);
    Check(xaml_root_content == root_element,
          "XamlRoot Content is the island root");
    if (xaml_root_content) xaml_root_content->Release();

    int got = 0;
    int lost = 0;
    int getting = 0;
    int losing = 0;
    std::string focus_order;
    EventRegistrationToken got_token{};
    EventRegistrationToken lost_token{};
    EventRegistrationToken self_token{};
    EventRegistrationToken bubbled_token{};
    EventRegistrationToken getting_token{};
    EventRegistrationToken losing_token{};
    auto* got_handler = new RoutedHandler([&]() { ++got; focus_order += 'G'; });
    auto* lost_handler = new RoutedHandler([&]() { ++lost; focus_order += 'L'; });
    using GettingHandler =
        __FITypedEventHandler_2_Windows__CUI__CXaml__CUIElement_Windows__CUI__CXaml__CInput__CGettingFocusEventArgs;
    using LosingHandler =
        __FITypedEventHandler_2_Windows__CUI__CXaml__CUIElement_Windows__CUI__CXaml__CInput__CLosingFocusEventArgs;
    auto* getting_handler = new FocusChangingHandler<
        GettingHandler, wuxi::IGettingFocusEventArgs>([&](auto* args) {
        ++getting;
        focus_order += 'g';
        wux::FocusState requested = wux::FocusState_Unfocused;
        args->get_FocusState(&requested);
        Check(requested != wux::FocusState_Unfocused,
              "GettingFocus exposes requested FocusState");
    });
    auto* losing_handler = new FocusChangingHandler<
        LosingHandler, wuxi::ILosingFocusEventArgs>([&](auto* args) {
        ++losing;
        focus_order += 'l';
        boolean canceled = 1;
        args->get_Cancel(&canceled);
        Check(!canceled, "LosingFocus defaults Cancel=false");
    });
    auto* self_removing = new RoutedHandler([&]() {
        if (element) element->remove_GotFocus(self_token);
    });
    int bubbled = 0;
    auto* bubbled_handler = new RoutedHandler([&]() { ++bubbled; });
    if (element) {
        Check(SUCCEEDED(element->add_GotFocus(got_handler, &got_token)),
              "register GotFocus");
        Check(SUCCEEDED(element->add_LostFocus(lost_handler, &lost_token)),
              "register LostFocus");
        Check(element5 && SUCCEEDED(element5->add_GettingFocus(
                  getting_handler, &getting_token)),
              "register typed GettingFocus");
        Check(element5 && SUCCEEDED(element5->add_LosingFocus(
                  losing_handler, &losing_token)),
              "register typed LosingFocus");
        Check(SUCCEEDED(element->add_GotFocus(self_removing, &self_token)),
              "register self-removing GotFocus");
        // Tokens are event-specific: removing this GotFocus token from the
        // LostFocus event must not remove either subscription.
        Check(SUCCEEDED(element->remove_LostFocus(got_token)),
              "cross-event removal is harmless");
    }
    if (root_element) {
        Check(SUCCEEDED(root_element->add_GotFocus(
                  bubbled_handler, &bubbled_token)),
              "register parent GotFocus route");
    }
    got_handler->Release();
    lost_handler->Release();
    getting_handler->Release();
    losing_handler->Release();
    self_removing->Release();
    bubbled_handler->Release();

    OffThreadFocus off_thread{control};
    HANDLE worker = control
        ? CreateThread(nullptr, 0, TryFocusOffThread, &off_thread, 0, nullptr)
        : nullptr;
    Check(worker != nullptr, "create off-thread Focus caller");
    if (worker) {
        WaitForSingleObject(worker, INFINITE);
        CloseHandle(worker);
    }
    Check(SUCCEEDED(off_thread.result) && !off_thread.focused,
          "off-thread Focus is rejected without changing state");

    SetFocus(parent);
    Pump();
    boolean focused = 0;
    wux::FocusState state = wux::FocusState_Unfocused;
    wux::IDependencyObject* dependency = nullptr;
    if (element) element->QueryInterface(
        openxaml::iid::Windows_UI_Xaml_IDependencyObject,
        reinterpret_cast<void**>(&dependency));
    using FocusOperation =
        __FIAsyncOperation_1_Windows__CUI__CXaml__CInput__CFocusMovementResult;
    FocusOperation* focus_operation = nullptr;
    if (focus5 && dependency) {
        Check(SUCCEEDED(focus5->TryFocusAsync(
                  dependency, wux::FocusState_Programmatic, &focus_operation)) &&
                  focus_operation,
              "FocusManager.TryFocusAsync returns an operation");
    }
    wf::IAsyncInfo* async_info = nullptr;
    if (focus_operation) focus_operation->QueryInterface(
        IID_FocusAsyncInfo, reinterpret_cast<void**>(&async_info));
    wf::AsyncStatus async_status = wf::AsyncStatus::Started;
    if (async_info) async_info->get_Status(&async_status);
    Check(async_info && async_status == wf::AsyncStatus::Completed,
          "TryFocusAsync operation is synchronously completed");
    wuxi::IFocusMovementResult* movement = nullptr;
    if (focus_operation) focus_operation->GetResults(&movement);
    boolean succeeded = 0;
    if (movement) movement->get_Succeeded(&succeeded);
    Check(movement && succeeded,
          "TryFocusAsync returns a successful FocusMovementResult");

    IInspectable* focused_element = nullptr;
    if (focus7 && xaml_root)
        focus7->GetFocusedElement(xaml_root, &focused_element);
    IUnknown* focused_identity = nullptr;
    IUnknown* element_identity = nullptr;
    if (focused_element) focused_element->QueryInterface(
        IID_IUnknown, reinterpret_cast<void**>(&focused_identity));
    if (element) element->QueryInterface(
        IID_IUnknown, reinterpret_cast<void**>(&element_identity));
    Check(focused_identity && focused_identity == element_identity,
          "GetFocusedElement(XamlRoot) returns the island focus target");
    if (focused_element) {
        focused_element->Release();
        focused_element = nullptr;
    }

    if (control) {
        Check(SUCCEEDED(control->get_FocusState(&state)) &&
                  state == wux::FocusState_Programmatic,
              "FocusState is Programmatic");
    }
    Check(GetFocus() == island_window, "island child owns HWND focus");
    Check(got == 1 && lost == 0, "one GotFocus transition");
    Check(getting == 1 && losing == 0 && focus_order == "gG",
          "GettingFocus precedes GotFocus exactly once");
    Check(bubbled == 1, "GotFocus bubbles to registered visual parent");

    if (control) {
        focused = 0;
        control->Focus(wux::FocusState_Programmatic, &focused);
        Check(focused && got == 1, "repeat Focus is idempotent");
    }

    auto* replacement = Activate<wux::IUIElement>(
        L"Windows.UI.Xaml.Controls.Border",
        openxaml::iid::Windows_UI_Xaml_IUIElement);
    auto* reentrant_replacement = Activate<wux::IUIElement>(
        L"Windows.UI.Xaml.Controls.Grid",
        openxaml::iid::Windows_UI_Xaml_IUIElement);
    Check(replacement != nullptr, "activate replacement root");
    Check(reentrant_replacement != nullptr,
          "activate reentrant replacement root");
    EventRegistrationToken reentrant_lost_token{};
    bool reentered_content = false;
    HRESULT nested_content_result = E_FAIL;
    auto* reentrant_lost = new RoutedHandler([&]() {
        if (!reentered_content && source && reentrant_replacement) {
            reentered_content = true;
            nested_content_result = source->put_Content(reentrant_replacement);
        }
    });
    if (element) element->add_LostFocus(
        reentrant_lost, &reentrant_lost_token);
    reentrant_lost->Release();
    if (source && replacement)
        Check(SUCCEEDED(source->put_Content(replacement)),
              "outer focused-root replacement survives reentrant LostFocus");
    wux::IUIElement* committed_content = nullptr;
    if (source) source->get_Content(&committed_content);
    IUnknown* committed_identity = nullptr;
    IUnknown* reentrant_identity = nullptr;
    if (committed_content) committed_content->QueryInterface(
        IID_IUnknown, reinterpret_cast<void**>(&committed_identity));
    if (reentrant_replacement) reentrant_replacement->QueryInterface(
        IID_IUnknown, reinterpret_cast<void**>(&reentrant_identity));
    Check(reentered_content && SUCCEEDED(nested_content_result) &&
              committed_identity && committed_identity == reentrant_identity,
          "inner content transaction wins without outer UAF/finalization");
    if (element) element->remove_LostFocus(reentrant_lost_token);
    if (committed_identity) committed_identity->Release();
    if (reentrant_identity) reentrant_identity->Release();
    if (committed_content) committed_content->Release();
    if (focus7 && xaml_root)
        Check(SUCCEEDED(focus7->GetFocusedElement(xaml_root, &focused_element)) &&
                  focused_element == nullptr,
              "detached XamlRoot cannot resolve another island's focus");
    if (control) control->get_FocusState(&state);
    Check(lost == 1 && state == wux::FocusState_Unfocused,
          "root replacement balances focus loss");
    Check(losing == 1 && focus_order.find("lL") != std::string::npos,
          "LosingFocus precedes LostFocus");
    focused = 1;
    if (control) control->Focus(wux::FocusState_Programmatic, &focused);
    Check(!focused, "replaced control cannot focus");

    if (source && root_element) source->put_Content(root_element);
    if (control) control->Focus(wux::FocusState_Keyboard, &focused);
    Check(focused && got == 2, "replacement root can be restored and focused");

    SetFocus(parent);
    Pump();
    state = wux::FocusState_Programmatic;
    if (control) control->get_FocusState(&state);
    Check(lost == 2 && state == wux::FocusState_Unfocused,
          "WM_KILLFOCUS raises LostFocus and clears state");

    if (source) source->put_Content(nullptr);
    focused = 1;
    if (control) control->Focus(wux::FocusState_Programmatic, &focused);
    Check(!focused && got == 2, "detached control cannot focus");

    if (source && root_element) source->put_Content(root_element);
    if (control) control->Focus(wux::FocusState_Keyboard, &focused);
    Check(focused && got == 3, "reattached root can focus again");
    if (closable) closable->Close();
    if (control) control->get_FocusState(&state);
    Check(lost == 3 && state == wux::FocusState_Unfocused,
          "Close balances focus loss");
    Check(!IsWindow(island_window), "Close destroys island HWND");

    if (element) {
        element->remove_GotFocus(got_token);
        element->remove_LostFocus(lost_token);
        if (element5) {
            element5->remove_GettingFocus(getting_token);
            element5->remove_LosingFocus(losing_token);
        }
    }
    if (root_element) root_element->remove_GotFocus(bubbled_token);
    if (control) control->Release();
    if (element5) element5->Release();
    if (element_identity) element_identity->Release();
    if (focused_identity) focused_identity->Release();
    if (focused_element) focused_element->Release();
    if (movement) movement->Release();
    if (async_info) async_info->Release();
    if (focus_operation) focus_operation->Release();
    if (dependency) dependency->Release();
    if (xaml_root) xaml_root->Release();
    if (child_element10) child_element10->Release();
    if (focus7) focus7->Release();
    if (focus5) focus5->Release();
    if (element) element->Release();
    if (root_element) root_element->Release();
    if (root_control) root_control->Release();
    if (replacement) replacement->Release();
    if (reentrant_replacement) reentrant_replacement->Release();
    if (closable) closable->Release();
    if (native) native->Release();
    if (source) source->Release();
    if (parent) DestroyWindow(parent);
    RoUninitialize();

    if (failures) return 1;
    std::puts("focus ABI checks passed");
    return 0;
}
