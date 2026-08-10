// Routed keyboard/character acceptance for a DesktopWindowXamlSource island.

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

namespace {

inline constexpr GUID IID_IslandNative = {
    0x3cbcf1bf, 0x2f76, 0x4e9c,
    {0x96, 0xab, 0xe8, 0x4b, 0x37, 0x97, 0x25, 0x54}};
inline constexpr GUID IID_KeyArgs2 = {
    0x1b02d57a, 0x9634, 0x4f14,
    {0x91, 0xb2, 0x13, 0x3e, 0x42, 0xfd, 0xb3, 0xcd}};

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

class KeyHandler final : public wuxi::IKeyEventHandler {
public:
    using Callback = std::function<void(wuxi::IKeyRoutedEventArgs*)>;
    explicit KeyHandler(Callback callback) : callback_(std::move(callback)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override {
        if (!value) return E_POINTER;
        if (IsEqualGUID(iid, IID_IUnknown)) {
            *value = static_cast<wuxi::IKeyEventHandler*>(this);
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
    HRESULT STDMETHODCALLTYPE Invoke(IInspectable*,
                                     wuxi::IKeyRoutedEventArgs* args) override {
        callback_(args);
        return S_OK;
    }
private:
    ~KeyHandler() = default;
    LONG references_ = 1;
    Callback callback_;
};

using CharacterHandlerAbi =
    __FITypedEventHandler_2_Windows__CUI__CXaml__CUIElement_Windows__CUI__CXaml__CInput__CCharacterReceivedRoutedEventArgs;

class CharacterHandler final : public CharacterHandlerAbi {
public:
    using Callback =
        std::function<void(wuxi::ICharacterReceivedRoutedEventArgs*)>;
    explicit CharacterHandler(Callback callback) : callback_(std::move(callback)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override {
        if (!value) return E_POINTER;
        if (IsEqualGUID(iid, IID_IUnknown)) {
            *value = static_cast<CharacterHandlerAbi*>(this);
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
        wux::IUIElement*, wuxi::ICharacterReceivedRoutedEventArgs* args) override {
        callback_(args);
        return S_OK;
    }
private:
    ~CharacterHandler() = default;
    LONG references_ = 1;
    Callback callback_;
};

LPARAM KeyBits(UINT repeat, UINT scan, bool extended, bool menu,
                bool previous, bool released) {
    std::uint32_t bits = repeat | (scan << 16);
    if (extended) bits |= 1u << 24;
    if (menu) bits |= 1u << 29;
    if (previous) bits |= 1u << 30;
    if (released) bits |= 1u << 31;
    return static_cast<LPARAM>(static_cast<std::int32_t>(bits));
}

}  // namespace

int main() {
    if (FAILED(RoInitialize(RO_INIT_SINGLETHREADED))) return 2;
    HWND parent = CreateWindowExW(0, L"STATIC", L"OpenXaml input smoke",
                                  WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                  20, 20, 320, 180, nullptr, nullptr,
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
    if (island) SetWindowPos(island, nullptr, 0, 0, 300, 140,
                             SWP_NOACTIVATE | SWP_NOZORDER | SWP_SHOWWINDOW);

    auto* root_control = Activate<wuxc::IContentControl>(
        L"Windows.UI.Xaml.Controls.ContentControl",
        openxaml::iid::Windows_UI_Xaml_Controls_IContentControl);
    auto* child = Activate<wux::IUIElement>(
        L"Windows.UI.Xaml.Controls.UserControl",
        openxaml::iid::Windows_UI_Xaml_IUIElement);
    wux::IUIElement* root = nullptr;
    wux::IUIElement7* root7 = nullptr;
    wux::IUIElement7* child7 = nullptr;
    wuxc::IControl* child_control = nullptr;
    if (root_control) root_control->QueryInterface(
        openxaml::iid::Windows_UI_Xaml_IUIElement,
        reinterpret_cast<void**>(&root));
    if (root) root->QueryInterface(openxaml::iid::Windows_UI_Xaml_IUIElement7,
                                   reinterpret_cast<void**>(&root7));
    if (child) {
        child->QueryInterface(openxaml::iid::Windows_UI_Xaml_IUIElement7,
                              reinterpret_cast<void**>(&child7));
        child->QueryInterface(openxaml::iid::Windows_UI_Xaml_Controls_IControl,
                              reinterpret_cast<void**>(&child_control));
    }
    Check(root_control && root && root7 && child && child7 && child_control,
          "activate routed tree");
    if (root_control && child) root_control->put_Content(child);
    if (source && root) source->put_Content(root);

    std::string order;
    bool fields_ok = true;
    bool original_ok = true;
    auto inspect = [&](wuxi::IKeyRoutedEventArgs* args) {
        ABI::Windows::System::VirtualKey key{};
        ABI::Windows::UI::Core::CorePhysicalKeyStatus status{};
        args->get_Key(&key);
        args->get_KeyStatus(&status);
        fields_ok = fields_ok && static_cast<UINT>(key) == 'A' &&
            status.RepeatCount == 2 && status.ScanCode == 0x1e &&
            status.IsExtendedKey && status.IsMenuKeyDown &&
            !status.WasKeyDown && !status.IsKeyReleased;
        wuxi::IKeyRoutedEventArgs2* args2 = nullptr;
        if (FAILED(args->QueryInterface(IID_KeyArgs2,
                reinterpret_cast<void**>(&args2))) || !args2) {
            original_ok = false;
        } else {
            ABI::Windows::System::VirtualKey original{};
            args2->get_OriginalKey(&original);
            original_ok = original_ok && static_cast<UINT>(original) == 'A';
            args2->Release();
        }
    };

    EventRegistrationToken root_preview{}, child_preview{}, child_down{}, root_down{};
    auto* a = new KeyHandler([&](auto* args) { order += 'A'; inspect(args); });
    auto* b = new KeyHandler([&](auto*) { order += 'B'; });
    auto* c = new KeyHandler([&](auto*) { order += 'C'; });
    auto* d = new KeyHandler([&](auto*) { order += 'D'; });
    root7->add_PreviewKeyDown(a, &root_preview);
    child7->add_PreviewKeyDown(b, &child_preview);
    child->add_KeyDown(c, &child_down);
    root->add_KeyDown(d, &root_down);
    a->Release(); b->Release(); c->Release(); d->Release();

    boolean focused = 0;
    child_control->Focus(wux::FocusState_Programmatic, &focused);
    Check(focused && GetFocus() == island, "focus child route target");
    SendMessageW(island, WM_KEYDOWN, 'A',
                 KeyBits(2, 0x1e, true, true, false, false));
    Check(order == "ABCD", "preview tunnels and keydown bubbles");
    Check(fields_ok && original_ok, "key ABI fields are exact");

    // A handled preview stops the remainder of the route and suppresses the
    // ordinary KeyDown route.
    order.clear();
    EventRegistrationToken handled_token{};
    auto* handled = new KeyHandler([&](auto* args) {
        order += 'H';
        args->put_Handled(1);
    });
    child7->add_PreviewKeyDown(handled, &handled_token);
    handled->Release();
    SendMessageW(island, WM_KEYDOWN, 'B', KeyBits(1, 0x30, false, false, false, false));
    Check(order == "ABH", "Handled stops preview and bubble routes");
    child7->remove_PreviewKeyDown(handled_token);

    // Key-up registrations are type-separated from key-down tokens.
    order.clear();
    EventRegistrationToken preview_up{}, key_up{};
    auto* u = new KeyHandler([&](auto*) { order += 'U'; });
    auto* v = new KeyHandler([&](auto*) { order += 'V'; });
    root7->add_PreviewKeyUp(u, &preview_up);
    child->add_KeyUp(v, &key_up);
    root->remove_KeyUp(root_down);
    u->Release(); v->Release();
    SendMessageW(island, WM_KEYUP, 'A',
                 KeyBits(1, 0x1e, false, false, true, true));
    Check(order == "UV", "preview/key-up route and token separation");

    // The route owns strong snapshots. Removing the current handler and
    // detaching the focused tree during the root preview callback must not
    // invalidate the remaining child/bubble callbacks already on this event.
    order.clear();
    EventRegistrationToken mutation_token{};
    auto* mutation = new KeyHandler([&](auto*) {
        order += 'M';
        root7->remove_PreviewKeyDown(mutation_token);
        source->put_Content(nullptr);
    });
    root7->add_PreviewKeyDown(mutation, &mutation_token);
    mutation->Release();
    SendMessageW(island, WM_KEYDOWN, 'C',
                 KeyBits(1, 0x2e, false, false, false, false));
    Check(order == "AMBCD", "tree mutation preserves the current route snapshot");
    order.clear();
    SendMessageW(island, WM_KEYDOWN, 'D',
                 KeyBits(1, 0x20, false, false, false, false));
    Check(order.empty(), "detached tree receives no later input");
    source->put_Content(root);
    child_control->Focus(wux::FocusState_Programmatic, &focused);
    Check(focused, "reattach and refocus after routed mutation");

    order.clear();
    WCHAR character = 0;
    ABI::Windows::UI::Core::CorePhysicalKeyStatus character_status{};
    EventRegistrationToken child_character{}, root_character{};
    auto* x = new CharacterHandler([&](auto* args) {
        order += 'X';
        args->get_Character(&character);
        args->get_KeyStatus(&character_status);
    });
    auto* y = new CharacterHandler([&](auto* args) {
        order += 'Y';
        args->put_Handled(1);
    });
    child7->add_CharacterReceived(x, &child_character);
    root7->add_CharacterReceived(y, &root_character);
    x->Release(); y->Release();
    SendMessageW(island, WM_CHAR, L'z', KeyBits(1, 0x2c, false, false, false, false));
    Check(order == "XY" && character == L'z' &&
              character_status.ScanCode == 0x2c,
          "character bubbles with WCHAR/status/Handled");

    // Close is another reentrant tree teardown boundary. It occurs at the
    // root preview stage; the retained route still completes safely.
    ABI::Windows::Foundation::IClosable* closable = nullptr;
    source->QueryInterface(openxaml::iid::Windows_Foundation_IClosable,
                           reinterpret_cast<void**>(&closable));
    order.clear();
    EventRegistrationToken close_token{};
    auto* close_handler = new KeyHandler([&](auto*) {
        order += 'K';
        if (closable) closable->Close();
    });
    root7->add_PreviewKeyDown(close_handler, &close_token);
    close_handler->Release();
    SendMessageW(island, WM_KEYDOWN, 'E',
                 KeyBits(1, 0x12, false, false, false, false));
    Check(order == "AKBCD", "Close preserves the current route snapshot");

    root7->remove_PreviewKeyDown(root_preview);
    child7->remove_PreviewKeyDown(child_preview);
    child->remove_KeyDown(child_down);
    root->remove_KeyDown(root_down);
    root7->remove_PreviewKeyUp(preview_up);
    child->remove_KeyUp(key_up);
    child7->remove_CharacterReceived(child_character);
    root7->remove_CharacterReceived(root_character);

    root7->remove_PreviewKeyDown(close_token);
    if (closable) { closable->Close(); closable->Release(); }
    if (child_control) child_control->Release();
    if (child7) child7->Release();
    if (root7) root7->Release();
    if (child) child->Release();
    if (root) root->Release();
    if (root_control) root_control->Release();
    if (native) native->Release();
    if (source) source->Release();
    if (parent) DestroyWindow(parent);
    RoUninitialize();

    if (failures) return 1;
    std::puts("keyboard input routing checks passed");
    return 0;
}
