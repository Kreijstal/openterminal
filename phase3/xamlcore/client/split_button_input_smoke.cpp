// MUX SplitButton.Click acceptance through a real XAML island HWND.

#include "sdk.h"

#include <roapi.h>

#include <cstdio>
#include <cwchar>

#include "openxaml_iids.h"

namespace wux = ABI::Windows::UI::Xaml;
namespace wuxh = ABI::Windows::UI::Xaml::Hosting;
namespace wuxi = ABI::Windows::UI::Xaml::Input;

namespace {

inline constexpr GUID IID_IslandNative = {
    0x3cbcf1bf, 0x2f76, 0x4e9c,
    {0x96, 0xab, 0xe8, 0x4b, 0x37, 0x97, 0x25, 0x54}};
inline constexpr GUID IID_IMuxcSplitButton = {
    0x8b09006a, 0x6241, 0x594f,
    {0x93, 0xe4, 0x8b, 0xf0, 0x51, 0xd7, 0xba, 0x8f}};
inline constexpr GUID IID_IMuxcSplitButtonClickEventArgs = {
    0x6af896c2, 0xe65a, 0x5998,
    {0x9c, 0x82, 0x2a, 0xf8, 0xf3, 0xe0, 0x74, 0x1f}};

struct IMuxcSplitButtonClickEventArgs : IInspectable {};
// Deliberately not the runtime's local handler type. This is the independent
// projection shape a C++/WinRT client owns on its side of the COM boundary.
struct ProjectedSplitButtonClickHandler : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Invoke(void*, void*) = 0;
};
struct MuxcSplitButtonAbi;
struct MuxcSplitButtonVtbl {
    HRESULT (STDMETHODCALLTYPE* QueryInterface)(
        MuxcSplitButtonAbi*, REFIID, void**);
    ULONG (STDMETHODCALLTYPE* AddRef)(MuxcSplitButtonAbi*);
    ULONG (STDMETHODCALLTYPE* Release)(MuxcSplitButtonAbi*);
    HRESULT (STDMETHODCALLTYPE* GetIids)(MuxcSplitButtonAbi*, ULONG*, IID**);
    HRESULT (STDMETHODCALLTYPE* GetRuntimeClassName)(MuxcSplitButtonAbi*, HSTRING*);
    HRESULT (STDMETHODCALLTYPE* GetTrustLevel)(MuxcSplitButtonAbi*, TrustLevel*);
    HRESULT (STDMETHODCALLTYPE* get_Flyout)(MuxcSplitButtonAbi*, void**);
    HRESULT (STDMETHODCALLTYPE* put_Flyout)(MuxcSplitButtonAbi*, void*);
    HRESULT (STDMETHODCALLTYPE* get_Command)(MuxcSplitButtonAbi*, void**);
    HRESULT (STDMETHODCALLTYPE* put_Command)(MuxcSplitButtonAbi*, void*);
    HRESULT (STDMETHODCALLTYPE* get_CommandParameter)(MuxcSplitButtonAbi*, void**);
    HRESULT (STDMETHODCALLTYPE* put_CommandParameter)(MuxcSplitButtonAbi*, void*);
    HRESULT (STDMETHODCALLTYPE* add_Click)(
        MuxcSplitButtonAbi*, void*, EventRegistrationToken*);
    HRESULT (STDMETHODCALLTYPE* remove_Click)(
        MuxcSplitButtonAbi*, EventRegistrationToken);
};
struct MuxcSplitButtonAbi {
    const MuxcSplitButtonVtbl* lpVtbl;
};

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

class ClickHandler final : public ProjectedSplitButtonClickHandler {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override {
        if (!value) return E_POINTER;
        if (IsEqualGUID(iid, IID_IUnknown)) {
            *value = static_cast<ProjectedSplitButtonClickHandler*>(this);
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
    HRESULT STDMETHODCALLTYPE Invoke(void* sender, void* args) override {
        ++calls;
        sender_ok = sender != nullptr;
        args_ok = args != nullptr;
        return S_OK;
    }

    int calls = 0;
    bool sender_ok = false;
    bool args_ok = false;

private:
    ~ClickHandler() = default;
    LONG references_ = 1;
};

class TappedHandler final : public wuxi::ITappedEventHandler {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override {
        if (!value) return E_POINTER;
        if (IsEqualGUID(iid, IID_IUnknown)) {
            *value = static_cast<wuxi::ITappedEventHandler*>(this);
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
    HRESULT STDMETHODCALLTYPE Invoke(
        IInspectable*, wuxi::ITappedRoutedEventArgs*) override {
        ++calls;
        return S_OK;
    }
    int calls = 0;
private:
    ~TappedHandler() = default;
    LONG references_ = 1;
};

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
    HWND parent = CreateWindowExW(0, L"STATIC", L"OpenXaml SplitButton smoke",
                                  WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                  20, 20, 180, 100, nullptr, nullptr,
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
          "create SplitButton island");
    if (island) SetWindowPos(island, nullptr, 0, 0, 62, 24,
                             SWP_NOACTIVATE | SWP_NOZORDER | SWP_SHOWWINDOW);

    HSTRING split_name = nullptr;
    WindowsCreateString(L"Microsoft.UI.Xaml.Controls.SplitButton", 38,
                        &split_name);
    IInspectable* split_instance = nullptr;
    const HRESULT split_activated =
        RoActivateInstance(split_name, &split_instance);
    WindowsDeleteString(split_name);
    MuxcSplitButtonAbi* split = nullptr;
    if (SUCCEEDED(split_activated) && split_instance) {
        (void)split_instance->QueryInterface(
            IID_IMuxcSplitButton, reinterpret_cast<void**>(&split));
        split_instance->Release();
    }
    wux::IUIElement* element = nullptr;
    wux::IFrameworkElement* framework = nullptr;
    if (split) split->lpVtbl->QueryInterface(split,
        openxaml::iid::Windows_UI_Xaml_IUIElement,
        reinterpret_cast<void**>(&element));
    if (element) element->QueryInterface(
        openxaml::iid::Windows_UI_Xaml_IFrameworkElement,
        reinterpret_cast<void**>(&framework));
    Check(split && element && framework, "activate SplitButton interfaces");
    if (framework) {
        framework->put_Width(62.0);
        framework->put_Height(24.0);
    }

    auto* handler = new ClickHandler;
    auto* tapped_handler = new TappedHandler;
    EventRegistrationToken token{};
    EventRegistrationToken tapped_token{};
    Check(split && SUCCEEDED(split->lpVtbl->add_Click(
              split, static_cast<ProjectedSplitButtonClickHandler*>(handler),
              &token)) && token.value,
          "register SplitButton.Click");
    Check(element && SUCCEEDED(element->add_Tapped(
              tapped_handler, &tapped_token)) && tapped_token.value,
          "register SplitButton.Tapped observation");
    Check(source && element && SUCCEEDED(source->put_Content(element)),
          "attach SplitButton as island content");
    Pump();

    if (island) Click(island, 10, 12);
    Check(handler->calls == 1, "primary half raises SplitButton.Click");
    Check(tapped_handler->calls == 1,
          "primary half reaches SplitButton routed tap target");
    Check(handler->sender_ok && handler->args_ok,
          "SplitButton.Click supplies projected sender and args");

    if (island) Click(island, 50, 12);
    Check(handler->calls == 1,
          "dropdown half does not raise the primary Click event");

    if (split) split->lpVtbl->remove_Click(split, token);
    if (element) element->remove_Tapped(tapped_token);
    if (source) source->put_Content(nullptr);
    handler->Release();
    tapped_handler->Release();
    if (framework) framework->Release();
    if (element) element->Release();
    if (split) split->lpVtbl->Release(split);
    ABI::Windows::Foundation::IClosable* closable = nullptr;
    if (source) source->QueryInterface(
        openxaml::iid::Windows_Foundation_IClosable,
        reinterpret_cast<void**>(&closable));
    if (closable) { closable->Close(); closable->Release(); }
    if (native) native->Release();
    if (source) source->Release();
    if (parent) DestroyWindow(parent);
    RoUninitialize();

    if (failures) return 1;
    std::puts("SplitButton input checks passed");
    return 0;
}
