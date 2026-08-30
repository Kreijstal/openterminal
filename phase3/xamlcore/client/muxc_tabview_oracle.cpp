// Differential Microsoft.UI.Xaml TabView oracle.
//
// The same executable explicitly loads either the official WinUI 2 DLL or
// OpenXaml and records only public ABI observations.  A fresh process is used
// for each side because WinRT activation factories and XAML thread state are
// cached.  No Terminal source or private WinUI implementation detail is part
// of the contract measured here.

#include "sdk.h"

#include <roapi.h>
#include <oleauto.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "openxaml_iids.h"

namespace wf = ABI::Windows::Foundation;
namespace wux = ABI::Windows::UI::Xaml;
namespace wuxc = ABI::Windows::UI::Xaml::Controls;
namespace wuxh = ABI::Windows::UI::Xaml::Hosting;
namespace wuxmk = ABI::Windows::UI::Xaml::Markup;

namespace {

inline constexpr GUID IID_IMuxcTabView = {
    0x6aa787ab, 0x5a30, 0x5ea2,
    {0xbe, 0x5b, 0xae, 0xd8, 0x68, 0x38, 0x17, 0x56}};
inline constexpr GUID IID_IMuxcTabViewItem = {
    0x291f3e98, 0x4f17, 0x5021,
    {0x94, 0xf0, 0x6a, 0x5b, 0x30, 0x43, 0x12, 0xb6}};
inline constexpr GUID IID_IMuxcTabViewTabCloseRequestedEventArgs = {
    0xd56ab9b2, 0xe264, 0x5c7e,
    {0xa1, 0xcb, 0xe4, 0x1a, 0x16, 0xa6, 0xc6, 0xc6}};
inline constexpr GUID IID_IslandNative = {
    0x3cbcf1bf, 0x2f76, 0x4e9c,
    {0x96, 0xab, 0xe8, 0x4b, 0x37, 0x97, 0x25, 0x54}};
inline constexpr GUID IID_IMuxcTabViewFactory = {
    0xe7e83685, 0xeedf, 0x5106,
    {0x94, 0x29, 0x88, 0x44, 0x35, 0xab, 0x16, 0x6b}};
inline constexpr GUID IID_IMuxcTabViewItemFactory = {
    0xb64c2423, 0x7e56, 0x5d41,
    {0x8a, 0x84, 0x1e, 0xe2, 0x8f, 0x98, 0x26, 0xa4}};
inline constexpr GUID IID_IResourceDictionary = {
    0xc1ea4f24, 0xd6de, 0x4191,
    {0x8e, 0x3a, 0xf4, 0x86, 0x01, 0xf7, 0x48, 0x9c}};
inline constexpr GUID IID_IXamlReaderStatics = {
    0x9891c6bd, 0x534f, 0x4955,
    {0xb8, 0x5a, 0x8a, 0x8d, 0xc0, 0xdc, 0xa6, 0x02}};

// mingw-w64 does not currently ship restrictederrorinfo.h. Keep the two-method
// public ABI local to the oracle so a XamlReader rejection includes the parser
// description instead of only the generic E_XAMLPARSEFAILED value.
struct IRestrictedErrorInfoAbi : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetErrorDetails(
        BSTR*, HRESULT*, BSTR*, BSTR*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetReference(BSTR*) = 0;
};

struct IMuxcTabViewTabCloseRequestedEventArgs : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Item(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Tab(void**) = 0;
};

struct IMuxcTabView : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_TabWidthMode(INT32*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_TabWidthMode(INT32) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_CloseButtonOverlayMode(INT32*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_CloseButtonOverlayMode(INT32) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_TabStripHeader(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_TabStripHeader(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_TabStripHeaderTemplate(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_TabStripHeaderTemplate(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_TabStripFooter(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_TabStripFooter(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_TabStripFooterTemplate(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_TabStripFooterTemplate(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsAddTabButtonVisible(boolean*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsAddTabButtonVisible(boolean) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AddTabButtonCommand(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_AddTabButtonCommand(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AddTabButtonCommandParameter(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_AddTabButtonCommandParameter(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_TabCloseRequested(
        void*, EventRegistrationToken*) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_TabCloseRequested(
        EventRegistrationToken) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_TabDroppedOutside(
        void*, EventRegistrationToken*) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_TabDroppedOutside(
        EventRegistrationToken) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_AddTabButtonClick(
        void*, EventRegistrationToken*) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_AddTabButtonClick(
        EventRegistrationToken) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_TabItemsChanged(
        void*, EventRegistrationToken*) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_TabItemsChanged(
        EventRegistrationToken) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_TabItemsSource(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_TabItemsSource(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_TabItems(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_TabItemTemplate(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_TabItemTemplate(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_TabItemTemplateSelector(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_TabItemTemplateSelector(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_CanDragTabs(boolean*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_CanDragTabs(boolean) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_CanReorderTabs(boolean*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_CanReorderTabs(boolean) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AllowDropTabs(boolean*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_AllowDropTabs(boolean) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_SelectedIndex(INT32*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_SelectedIndex(INT32) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_SelectedItem(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_SelectedItem(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE ContainerFromItem(void*, void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE ContainerFromIndex(INT32, void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_SelectionChanged(
        wuxc::ISelectionChangedEventHandler*, EventRegistrationToken*) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_SelectionChanged(
        EventRegistrationToken) = 0;
};

struct IMuxcTabViewItem : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Header(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Header(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_HeaderTemplate(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_HeaderTemplate(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IconSource(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IconSource(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsClosable(boolean*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsClosable(boolean) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_TabViewTemplateSettings(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_CloseRequested(
        void*, EventRegistrationToken*) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_CloseRequested(
        EventRegistrationToken) = 0;
};

struct ProjectedTabCloseHandler : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Invoke(
        IMuxcTabView*, IMuxcTabViewTabCloseRequestedEventArgs*) = 0;
};

struct ProjectedAddClickHandler : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Invoke(IMuxcTabView*, IInspectable*) = 0;
};

struct IMuxcComposableFactory : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE CreateInstance(
        void*, void**, void**) = 0;
};

template <class T>
class Ref {
public:
    Ref() = default;
    explicit Ref(T* value) : value_(value) {}
    Ref(const Ref&) = delete;
    Ref& operator=(const Ref&) = delete;
    Ref(Ref&& other) noexcept : value_(other.value_) { other.value_ = nullptr; }
    Ref& operator=(Ref&& other) noexcept {
        if (this != &other) {
            reset();
            value_ = other.value_;
            other.value_ = nullptr;
        }
        return *this;
    }
    ~Ref() { reset(); }
    void reset() {
        if (value_) value_->Release();
        value_ = nullptr;
    }
    T* get() const { return value_; }
    T* operator->() const { return value_; }
    T** put() {
        reset();
        return &value_;
    }
    explicit operator bool() const { return value_ != nullptr; }

private:
    T* value_ = nullptr;
};

class HString {
public:
    explicit HString(const wchar_t* value) {
        hr_ = WindowsCreateString(
            value, static_cast<UINT32>(std::wcslen(value)), &value_);
    }
    ~HString() { WindowsDeleteString(value_); }
    HSTRING get() const { return value_; }
    HRESULT result() const { return hr_; }

private:
    HSTRING value_ = nullptr;
    HRESULT hr_ = E_FAIL;
};

std::string Narrow(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                         static_cast<int>(value.size()),
                                         nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(std::max(size, 0)), '\0');
    if (size > 0)
        WideCharToMultiByte(CP_UTF8, 0, value.data(),
                            static_cast<int>(value.size()), result.data(), size,
                            nullptr, nullptr);
    return result;
}

std::string RestrictedErrorText() {
    using GetRestrictedErrorInfoFn = HRESULT (WINAPI*)(IRestrictedErrorInfoAbi**);
    const HMODULE combase = GetModuleHandleW(L"combase.dll");
    const auto get_info = combase
        ? reinterpret_cast<GetRestrictedErrorInfoFn>(
              GetProcAddress(combase, "GetRestrictedErrorInfo"))
        : nullptr;
    if (!get_info) return {};

    Ref<IRestrictedErrorInfoAbi> info;
    if (FAILED(get_info(info.put())) || !info) return {};
    BSTR description = nullptr;
    BSTR restricted = nullptr;
    BSTR capability = nullptr;
    HRESULT error = S_OK;
    const HRESULT hr = info->GetErrorDetails(
        &description, &error, &restricted, &capability);
    const std::wstring selected = restricted && *restricted
        ? std::wstring(restricted, SysStringLen(restricted))
        : description && *description
            ? std::wstring(description, SysStringLen(description))
            : std::wstring();
    SysFreeString(description);
    SysFreeString(restricted);
    SysFreeString(capability);
    return SUCCEEDED(hr) ? Narrow(selected) : std::string();
}

std::string Escape(const std::string& value) {
    std::ostringstream out;
    for (unsigned char unit : value) {
        switch (unit) {
            case '\\': out << "\\\\"; break;
            case '"': out << "\\\""; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (unit < 0x20) {
                    out << "\\u" << std::hex << std::setw(4)
                        << std::setfill('0') << static_cast<int>(unit);
                } else {
                    out << static_cast<char>(unit);
                }
        }
    }
    return out.str();
}

std::string Hr(HRESULT value) {
    std::ostringstream out;
    out << "0x" << std::hex << std::setw(8) << std::setfill('0')
        << static_cast<uint32_t>(value);
    return out.str();
}

bool SameIdentity(IUnknown* left, IUnknown* right) {
    if (!left || !right) return left == right;
    Ref<IUnknown> left_identity;
    Ref<IUnknown> right_identity;
    return SUCCEEDED(left->QueryInterface(
               IID_IUnknown, reinterpret_cast<void**>(left_identity.put()))) &&
           SUCCEEDED(right->QueryInterface(
               IID_IUnknown, reinterpret_cast<void**>(right_identity.put()))) &&
           left_identity.get() == right_identity.get();
}

std::string RuntimeClass(IInspectable* value) {
    if (!value) return {};
    HSTRING name = nullptr;
    if (FAILED(value->GetRuntimeClassName(&name)) || !name) return {};
    UINT32 size = 0;
    const wchar_t* text = WindowsGetStringRawBuffer(name, &size);
    std::string result = Narrow(std::wstring(text, text + size));
    WindowsDeleteString(name);
    return result;
}

using DllGetActivationFactoryFn = HRESULT (WINAPI*)(HSTRING, IActivationFactory**);

class Runtime {
public:
    HRESULT Load(const wchar_t* path, bool custom_host) {
        custom_host_ = custom_host;
        module_ = LoadLibraryExW(path, nullptr,
                                 LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                                 LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!module_) return HRESULT_FROM_WIN32(GetLastError());
        get_factory_ = reinterpret_cast<DllGetActivationFactoryFn>(
            GetProcAddress(module_, "DllGetActivationFactory"));
        if (!get_factory_) return HRESULT_FROM_WIN32(GetLastError());
        wchar_t resolved[32768]{};
        const DWORD length = GetModuleFileNameW(module_, resolved, ARRAYSIZE(resolved));
        if (length) path_ = Narrow(std::wstring(resolved, resolved + length));
        return S_OK;
    }

    ~Runtime() {
        // XAML objects and factories are process-lifetime in both runtimes.
        // Deliberately do not FreeLibrary underneath cached factory state.
    }

    HRESULT Factory(const wchar_t* name, bool host,
                    IActivationFactory** result) const {
        if (!result) return E_POINTER;
        *result = nullptr;
        HString class_name(name);
        if (FAILED(class_name.result())) return class_name.result();
        if (host && !custom_host_)
            return RoGetActivationFactory(
                class_name.get(), openxaml::iid::IActivationFactory,
                reinterpret_cast<void**>(result));
        return get_factory_(class_name.get(), result);
    }

    HRESULT Activate(const wchar_t* name, bool host,
                     IInspectable** result) const {
        if (!result) return E_POINTER;
        *result = nullptr;
        Ref<IActivationFactory> factory;
        const HRESULT hr = Factory(name, host, factory.put());
        if (FAILED(hr)) return hr;
        return factory->ActivateInstance(result);
    }

    HRESULT ActivateComposable(const wchar_t* name, REFIID factory_iid,
                               IInspectable** result) const {
        if (!result) return E_POINTER;
        *result = nullptr;
        Ref<IActivationFactory> activation_factory;
        HRESULT hr = Factory(name, false, activation_factory.put());
        if (FAILED(hr)) return hr;
        hr = activation_factory->ActivateInstance(result);
        if (hr != E_NOTIMPL) return hr;

        Ref<IMuxcComposableFactory> composable;
        hr = activation_factory->QueryInterface(
            factory_iid, reinterpret_cast<void**>(composable.put()));
        if (FAILED(hr)) return hr;
        void* inner = nullptr;
        void* projected = nullptr;
        hr = composable->CreateInstance(nullptr, &inner, &projected);
        if (FAILED(hr)) return hr;
        auto* projected_unknown = static_cast<IUnknown*>(projected);
        if (projected_unknown) {
            hr = projected_unknown->QueryInterface(
                openxaml::iid::IInspectable,
                reinterpret_cast<void**>(result));
            projected_unknown->Release();
        } else {
            hr = E_POINTER;
        }
        if (inner) static_cast<IUnknown*>(inner)->Release();
        return hr;
    }

    template <class T>
    HRESULT Statics(const wchar_t* name, bool host, REFIID iid,
                    T** result) const {
        if (!result) return E_POINTER;
        *result = nullptr;
        Ref<IActivationFactory> factory;
        const HRESULT hr = Factory(name, host, factory.put());
        if (FAILED(hr)) return hr;
        return factory->QueryInterface(iid, reinterpret_cast<void**>(result));
    }

    const std::string& path() const { return path_; }

private:
    HMODULE module_ = nullptr;
    DllGetActivationFactoryFn get_factory_ = nullptr;
    bool custom_host_ = false;
    std::string path_;
};

class Observations {
public:
    void String(const std::string& key, const std::string& value) {
        values_[key] = "\"" + Escape(value) + "\"";
    }
    void Number(const std::string& key, long long value) {
        values_[key] = std::to_string(value);
    }
    void Bool(const std::string& key, bool value) {
        values_[key] = value ? "true" : "false";
    }
    void HResult(const std::string& key, HRESULT value) {
        String(key, Hr(value));
    }
    void Raw(const std::string& key, std::string value) {
        values_[key] = std::move(value);
    }

    bool Write(const std::wstring& output, const Runtime& runtime,
               const std::string& mode) const {
        std::ofstream stream(output.c_str(), std::ios::binary);
        if (!stream) return false;
        stream << "{\n  \"schema_version\": 1,\n"
               << "  \"runtime\": {\"mode\": \"" << Escape(mode)
               << "\", \"module\": \"" << Escape(runtime.path()) << "\"},\n"
               << "  \"observations\": {\n";
        bool first = true;
        for (const auto& [key, value] : values_) {
            if (!first) stream << ",\n";
            first = false;
            stream << "    \"" << Escape(key) << "\": " << value;
        }
        stream << "\n  }\n}\n";
        return static_cast<bool>(stream);
    }

private:
    std::map<std::string, std::string> values_;
};

class SelectionHandler final : public wuxc::ISelectionChangedEventHandler {
public:
    explicit SelectionHandler(std::vector<IInspectable*> items) : items_(std::move(items)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override {
        if (!result) return E_POINTER;
        if (IsEqualGUID(iid, IID_IUnknown)) {
            *result = static_cast<wuxc::ISelectionChangedEventHandler*>(this);
            AddRef();
            return S_OK;
        }
        *result = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return static_cast<ULONG>(InterlockedIncrement(&references_));
    }
    ULONG STDMETHODCALLTYPE Release() override {
        return static_cast<ULONG>(InterlockedDecrement(&references_));
    }
    HRESULT STDMETHODCALLTYPE Invoke(
        IInspectable* sender, wuxc::ISelectionChangedEventArgs* args) override {
        Event event{};
        event.sender = sender != nullptr;
        if (args) {
            Ref<__FIVector_1_IInspectable> removed;
            Ref<__FIVector_1_IInspectable> added;
            event.removed_hr = args->get_RemovedItems(removed.put());
            event.added_hr = args->get_AddedItems(added.put());
            if (removed) {
                UINT32 count = 0;
                if (SUCCEEDED(removed->get_Size(&count))) event.removed = static_cast<int>(count);
            }
            if (added) {
                UINT32 count = 0;
                if (SUCCEEDED(added->get_Size(&count))) event.added = static_cast<int>(count);
            }
        }
        events.push_back(event);
        return S_OK;
    }

    struct Event {
        bool sender = false;
        int removed = -1;
        int added = -1;
        HRESULT removed_hr = E_POINTER;
        HRESULT added_hr = E_POINTER;
    };
    std::vector<Event> events;

private:
    LONG references_ = 1;
    std::vector<IInspectable*> items_;
};

struct Hit {
    int x = -1;
    int y = -1;
    int item = -1;
    bool sender = false;
    bool args = false;
    bool item_matches = false;
    bool tab_matches = false;
};

class CloseHandler final : public ProjectedTabCloseHandler {
public:
    explicit CloseHandler(std::vector<IInspectable*> items) : items_(std::move(items)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override {
        if (!result) return E_POINTER;
        if (IsEqualGUID(iid, IID_IUnknown)) {
            *result = static_cast<ProjectedTabCloseHandler*>(this);
            AddRef();
            return S_OK;
        }
        *result = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return static_cast<ULONG>(InterlockedIncrement(&references_));
    }
    ULONG STDMETHODCALLTYPE Release() override {
        return static_cast<ULONG>(InterlockedDecrement(&references_));
    }
    HRESULT STDMETHODCALLTYPE Invoke(
        IMuxcTabView* sender,
        IMuxcTabViewTabCloseRequestedEventArgs* args) override {
        Hit hit{};
        hit.x = x;
        hit.y = y;
        hit.sender = sender != nullptr;
        hit.args = args != nullptr;
        Ref<IInspectable> item;
        Ref<IInspectable> tab;
        if (args) {
            (void)args->get_Item(reinterpret_cast<void**>(item.put()));
            (void)args->get_Tab(reinterpret_cast<void**>(tab.put()));
        }
        for (size_t index = 0; index < items_.size(); ++index) {
            if (SameIdentity(item.get(), items_[index])) {
                hit.item = static_cast<int>(index);
                hit.item_matches = true;
            }
            if (SameIdentity(tab.get(), items_[index])) hit.tab_matches = true;
        }
        hits.push_back(hit);
        return S_OK;
    }

    int x = -1;
    int y = -1;
    std::vector<Hit> hits;

private:
    LONG references_ = 1;
    std::vector<IInspectable*> items_;
};

class AddClickHandler final : public ProjectedAddClickHandler {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override {
        if (!result) return E_POINTER;
        if (IsEqualGUID(iid, IID_IUnknown)) {
            *result = static_cast<ProjectedAddClickHandler*>(this);
            AddRef();
            return S_OK;
        }
        *result = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return static_cast<ULONG>(InterlockedIncrement(&references_));
    }
    ULONG STDMETHODCALLTYPE Release() override {
        return static_cast<ULONG>(InterlockedDecrement(&references_));
    }
    HRESULT STDMETHODCALLTYPE Invoke(IMuxcTabView* sender, IInspectable*) override {
        hits.push_back({x, y, sender ? 1 : 0});
        return S_OK;
    }

    int x = -1;
    int y = -1;
    struct AddHit { int x; int y; int sender; };
    std::vector<AddHit> hits;

private:
    LONG references_ = 1;
};

void Pump() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

void PumpFor(DWORD milliseconds) {
    const ULONGLONG finish = GetTickCount64() + milliseconds;
    do {
        Pump();
        const ULONGLONG now = GetTickCount64();
        if (now >= finish) break;
        const DWORD left = static_cast<DWORD>(std::min<ULONGLONG>(finish - now, 20));
        MsgWaitForMultipleObjects(0, nullptr, FALSE, left, QS_ALLINPUT);
    } while (true);
    Pump();
}

bool TakeForeground(HWND window) {
    const DWORD current_thread = GetCurrentThreadId();
    HWND previous = GetForegroundWindow();
    DWORD previous_thread = previous
        ? GetWindowThreadProcessId(previous, nullptr) : 0;
    const bool attached = previous_thread && previous_thread != current_thread &&
        AttachThreadInput(current_thread, previous_thread, TRUE);
    ShowWindow(window, SW_SHOW);
    SetWindowPos(window, HWND_TOPMOST, 40, 40, 660, 180,
                 SWP_SHOWWINDOW);
    BringWindowToTop(window);
    SetActiveWindow(window);
    SetFocus(window);
    SetForegroundWindow(window);
    if (attached)
        AttachThreadInput(current_thread, previous_thread, FALSE);
    PumpFor(50);
    return GetForegroundWindow() == window;
}

HRESULT StartDispatcherQueue(Ref<IUnknown>& controller) {
    enum DispatcherQueueThreadType { DQTYPE_THREAD_DEDICATED = 1, DQTYPE_THREAD_CURRENT = 2 };
    enum DispatcherQueueApartmentType {
        DQTAT_COM_NONE = 0, DQTAT_COM_ASTA = 1, DQTAT_COM_STA = 2
    };
    struct DispatcherQueueOptions {
        DWORD dwSize;
        DispatcherQueueThreadType threadType;
        DispatcherQueueApartmentType apartmentType;
    };
    using CreateFn = HRESULT (WINAPI*)(
        DispatcherQueueOptions, IUnknown**);
    HMODULE core_messaging = LoadLibraryW(L"CoreMessaging.dll");
    if (!core_messaging) return HRESULT_FROM_WIN32(GetLastError());
    auto create = reinterpret_cast<CreateFn>(
        GetProcAddress(core_messaging, "CreateDispatcherQueueController"));
    if (!create) return HRESULT_FROM_WIN32(GetLastError());
    DispatcherQueueOptions options{
        sizeof(DispatcherQueueOptions), DQTYPE_THREAD_CURRENT, DQTAT_COM_STA};
    return create(options, controller.put());
}

std::string SelectionJson(const std::vector<SelectionHandler::Event>& events) {
    std::ostringstream out;
    out << '[';
    for (size_t index = 0; index < events.size(); ++index) {
        if (index) out << ',';
        const auto& event = events[index];
        out << "{\"sender\":" << (event.sender ? "true" : "false")
            << ",\"removed\":" << event.removed
            << ",\"added\":" << event.added
            << ",\"removed_hr\":\"" << Hr(event.removed_hr)
            << "\",\"added_hr\":\"" << Hr(event.added_hr) << "\"}";
    }
    out << ']';
    return out.str();
}

std::string CloseHitsJson(const std::vector<Hit>& hits) {
    std::ostringstream out;
    out << '[';
    for (size_t index = 0; index < hits.size(); ++index) {
        if (index) out << ',';
        const auto& hit = hits[index];
        out << "{\"x\":" << hit.x << ",\"y\":" << hit.y
            << ",\"item\":" << hit.item
            << ",\"sender\":" << (hit.sender ? "true" : "false")
            << ",\"args\":" << (hit.args ? "true" : "false")
            << ",\"item_matches\":" << (hit.item_matches ? "true" : "false")
            << ",\"tab_matches\":" << (hit.tab_matches ? "true" : "false")
            << '}';
    }
    out << ']';
    return out.str();
}

std::string AddHitsJson(const std::vector<AddClickHandler::AddHit>& hits) {
    std::ostringstream out;
    out << '[';
    for (size_t index = 0; index < hits.size(); ++index) {
        if (index) out << ',';
        const auto& hit = hits[index];
        out << "{\"x\":" << hit.x << ",\"y\":" << hit.y
            << ",\"sender\":" << (hit.sender ? "true" : "false") << '}';
    }
    out << ']';
    return out.str();
}

HRESULT Capture(HWND window, const std::wstring& path, int width, int height) {
    POINT origin{};
    if (!ClientToScreen(window, &origin)) return HRESULT_FROM_WIN32(GetLastError());
    HDC screen = GetDC(nullptr);
    if (!screen) return HRESULT_FROM_WIN32(GetLastError());
    HDC memory = CreateCompatibleDC(screen);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS,
                                      &pixels, nullptr, 0);
    if (!memory || !bitmap || !pixels) {
        const HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        if (bitmap) DeleteObject(bitmap);
        if (memory) DeleteDC(memory);
        ReleaseDC(nullptr, screen);
        return hr;
    }
    HGDIOBJ previous = SelectObject(memory, bitmap);
    const BOOL copied = BitBlt(memory, 0, 0, width, height, screen,
                               origin.x, origin.y, SRCCOPY | CAPTUREBLT);
    HRESULT result = copied ? S_OK : HRESULT_FROM_WIN32(GetLastError());
    if (copied) {
        HANDLE output = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (output == INVALID_HANDLE_VALUE) {
            result = HRESULT_FROM_WIN32(GetLastError());
        } else {
            DWORD written = 0;
            const DWORD size = static_cast<DWORD>(width * height * 4);
            if (!WriteFile(output, pixels, size, &written, nullptr) || written != size)
                result = HRESULT_FROM_WIN32(GetLastError());
            CloseHandle(output);
        }
    }
    SelectObject(memory, previous);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    return result;
}

template <class T>
HRESULT Query(IUnknown* source, REFIID iid, Ref<T>& result) {
    if (!source) return E_POINTER;
    return source->QueryInterface(iid, reinterpret_cast<void**>(result.put()));
}

void RecordBool(Observations& observations, const std::string& key,
                HRESULT (STDMETHODCALLTYPE IMuxcTabView::*getter)(boolean*),
                IMuxcTabView* value) {
    boolean result = 0xff;
    const HRESULT hr = value ? (value->*getter)(&result) : E_POINTER;
    observations.HResult(key + ".hr", hr);
    if (SUCCEEDED(hr)) observations.Bool(key + ".value", result != 0);
}

void Click(HWND island, int x, int y, bool real_input, CloseHandler& close,
           AddClickHandler& add) {
    close.x = add.x = x;
    close.y = add.y = y;
    if (real_input) {
        POINT point{x, y};
        ClientToScreen(island, &point);
        SetCursorPos(point.x, point.y);
        PumpFor(5);
        INPUT input[2]{};
        input[0].type = input[1].type = INPUT_MOUSE;
        input[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        input[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
        SendInput(2, input, sizeof(INPUT));
        PumpFor(5);
    } else {
        SendMessageW(island, WM_MOUSEMOVE, 0, MAKELPARAM(x, y));
        SendMessageW(island, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
        SendMessageW(island, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
        Pump();
    }
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 4 || argc > 5) {
        std::fwprintf(stderr,
            L"usage: muxc_tabview_oracle <official|openxaml> <dll> <output.json> [pixels.bgra]\n");
        return 2;
    }
    const std::wstring mode = argv[1];
    if (mode != L"official" && mode != L"openxaml") {
        std::fwprintf(stderr, L"mode must be official or openxaml\n");
        return 2;
    }

    Observations observations;
    const HRESULT ro = RoInitialize(RO_INIT_SINGLETHREADED);
    observations.HResult("init.ro", ro);

    Runtime runtime;
    const HRESULT loaded = runtime.Load(argv[2], mode == L"openxaml");
    observations.HResult("init.load_library", loaded);
    if (FAILED(loaded)) {
        runtime.Load(L"kernel32.dll", false);
        observations.Write(argv[3], runtime, Narrow(mode));
        return 3;
    }

    Ref<IUnknown> queue;
    const HRESULT queue_hr = StartDispatcherQueue(queue);
    observations.HResult("init.dispatcher_queue", queue_hr);

    Ref<wuxh::IWindowsXamlManagerStatics> manager_statics;
    HRESULT manager_factory_hr = runtime.Statics(
        L"Windows.UI.Xaml.Hosting.WindowsXamlManager", true,
        openxaml::iid::Windows_UI_Xaml_Hosting_IWindowsXamlManagerStatics,
        manager_statics.put());
    observations.HResult("init.xaml_manager_factory", manager_factory_hr);
    Ref<wuxh::IWindowsXamlManager> manager;
    HRESULT manager_hr = manager_statics
        ? manager_statics->InitializeForCurrentThread(manager.put())
        : manager_factory_hr;
    observations.HResult("init.xaml_manager", manager_hr);

    Ref<IInspectable> tab_instance;
    HRESULT tab_activation = runtime.ActivateComposable(
        L"Microsoft.UI.Xaml.Controls.TabView", IID_IMuxcTabViewFactory,
        tab_instance.put());
    observations.HResult("activation.tab_view", tab_activation);
    observations.String("activation.tab_view.class", RuntimeClass(tab_instance.get()));
    Ref<IMuxcTabView> tab_view;
    const HRESULT tab_qi = Query(tab_instance.get(), IID_IMuxcTabView, tab_view);
    observations.HResult("activation.tab_view.qi", tab_qi);

    Ref<IInspectable> first_instance;
    Ref<IInspectable> second_instance;
    const HRESULT first_activation = runtime.ActivateComposable(
        L"Microsoft.UI.Xaml.Controls.TabViewItem", IID_IMuxcTabViewItemFactory,
        first_instance.put());
    const HRESULT second_activation = runtime.ActivateComposable(
        L"Microsoft.UI.Xaml.Controls.TabViewItem", IID_IMuxcTabViewItemFactory,
        second_instance.put());
    observations.HResult("activation.first_item", first_activation);
    observations.HResult("activation.second_item", second_activation);
    observations.String("activation.item.class", RuntimeClass(first_instance.get()));
    Ref<IMuxcTabViewItem> first_item;
    Ref<IMuxcTabViewItem> second_item;
    observations.HResult("activation.first_item.qi",
        Query(first_instance.get(), IID_IMuxcTabViewItem, first_item));
    observations.HResult("activation.second_item.qi",
        Query(second_instance.get(), IID_IMuxcTabViewItem, second_item));

    if (!tab_view || !first_item || !second_item) {
        observations.Write(argv[3], runtime, Narrow(mode));
        manager.reset();
        manager_statics.reset();
        queue.reset();
        // The selected DLL keeps process-wide factory state. Let process exit
        // tear COM down instead of unloading beneath that state on this fatal
        // diagnostic path.
        return 4;
    }

    INT32 integer = -99;
    HRESULT hr = tab_view->get_TabWidthMode(&integer);
    observations.HResult("defaults.tab_width_mode.hr", hr);
    if (SUCCEEDED(hr)) observations.Number("defaults.tab_width_mode.value", integer);
    integer = -99;
    hr = tab_view->get_CloseButtonOverlayMode(&integer);
    observations.HResult("defaults.close_overlay.hr", hr);
    if (SUCCEEDED(hr)) observations.Number("defaults.close_overlay.value", integer);
    RecordBool(observations, "defaults.add_button_visible",
               &IMuxcTabView::get_IsAddTabButtonVisible, tab_view.get());
    RecordBool(observations, "defaults.can_drag",
               &IMuxcTabView::get_CanDragTabs, tab_view.get());
    RecordBool(observations, "defaults.can_reorder",
               &IMuxcTabView::get_CanReorderTabs, tab_view.get());
    RecordBool(observations, "defaults.allow_drop",
               &IMuxcTabView::get_AllowDropTabs, tab_view.get());
    integer = -99;
    hr = tab_view->get_SelectedIndex(&integer);
    observations.HResult("defaults.selected_index.hr", hr);
    if (SUCCEEDED(hr)) observations.Number("defaults.selected_index.value", integer);
    Ref<IInspectable> selected;
    hr = tab_view->get_SelectedItem(reinterpret_cast<void**>(selected.put()));
    observations.HResult("defaults.selected_item.hr", hr);
    observations.Bool("defaults.selected_item.null", !selected);
    boolean closable = 0;
    hr = first_item->get_IsClosable(&closable);
    observations.HResult("defaults.item_closable.hr", hr);
    if (SUCCEEDED(hr)) observations.Bool("defaults.item_closable.value", closable != 0);

    observations.HResult("roundtrip.tab_width_mode.put",
                         tab_view->put_TabWidthMode(2));
    integer = -99;
    hr = tab_view->get_TabWidthMode(&integer);
    observations.HResult("roundtrip.tab_width_mode.get", hr);
    if (SUCCEEDED(hr)) observations.Number("roundtrip.tab_width_mode.value", integer);
    observations.HResult("roundtrip.close_overlay.put",
                         tab_view->put_CloseButtonOverlayMode(2));
    integer = -99;
    hr = tab_view->get_CloseButtonOverlayMode(&integer);
    observations.HResult("roundtrip.close_overlay.get", hr);
    if (SUCCEEDED(hr)) observations.Number("roundtrip.close_overlay.value", integer);
    observations.HResult("roundtrip.can_drag.put", tab_view->put_CanDragTabs(1));
    RecordBool(observations, "roundtrip.can_drag",
               &IMuxcTabView::get_CanDragTabs, tab_view.get());
    observations.HResult("roundtrip.can_reorder.put", tab_view->put_CanReorderTabs(1));
    RecordBool(observations, "roundtrip.can_reorder",
               &IMuxcTabView::get_CanReorderTabs, tab_view.get());
    observations.HResult("roundtrip.allow_drop.put", tab_view->put_AllowDropTabs(1));
    RecordBool(observations, "roundtrip.allow_drop",
               &IMuxcTabView::get_AllowDropTabs, tab_view.get());
    observations.HResult("roundtrip.item_closable.put_false",
                         first_item->put_IsClosable(0));
    closable = 1;
    hr = first_item->get_IsClosable(&closable);
    observations.HResult("roundtrip.item_closable.get_false", hr);
    if (SUCCEEDED(hr)) observations.Bool("roundtrip.item_closable.false_value", closable != 0);
    observations.HResult("roundtrip.item_closable.put_true",
                         first_item->put_IsClosable(1));

    Ref<__FIVector_1_IInspectable> items;
    hr = tab_view->get_TabItems(reinterpret_cast<void**>(items.put()));
    observations.HResult("collection.get", hr);
    UINT32 count = 0;
    if (items) hr = items->get_Size(&count);
    observations.HResult("collection.initial_size.hr", items ? hr : E_POINTER);
    observations.Number("collection.initial_size.value", count);
    HRESULT append_first = items ? items->Append(first_instance.get()) : E_POINTER;
    HRESULT append_second = items ? items->Append(second_instance.get()) : E_POINTER;
    observations.HResult("collection.append_first", append_first);
    observations.HResult("collection.append_second", append_second);
    count = 0;
    hr = items ? items->get_Size(&count) : E_POINTER;
    observations.HResult("collection.final_size.hr", hr);
    observations.Number("collection.final_size.value", count);

    SelectionHandler selection({first_instance.get(), second_instance.get()});
    EventRegistrationToken selection_token{};
    hr = tab_view->add_SelectionChanged(&selection, &selection_token);
    observations.HResult("selection.add_handler", hr);
    observations.Bool("selection.token_nonzero", selection_token.value != 0);
    observations.HResult("selection.select_first",
                         tab_view->put_SelectedItem(first_instance.get()));
    observations.HResult("selection.repeat_first",
                         tab_view->put_SelectedItem(first_instance.get()));
    observations.HResult("selection.select_second",
                         tab_view->put_SelectedItem(second_instance.get()));
    observations.HResult("selection.select_index_zero",
                         tab_view->put_SelectedIndex(0));
    integer = -99;
    hr = tab_view->get_SelectedIndex(&integer);
    observations.HResult("selection.final_index.hr", hr);
    if (SUCCEEDED(hr)) observations.Number("selection.final_index.value", integer);
    selected.reset();
    hr = tab_view->get_SelectedItem(reinterpret_cast<void**>(selected.put()));
    observations.HResult("selection.final_item.hr", hr);
    observations.Bool("selection.final_item.first",
                      SameIdentity(selected.get(), first_instance.get()));
    observations.Raw("selection.events", SelectionJson(selection.events));
    observations.HResult("selection.remove_handler",
                         tab_view->remove_SelectionChanged(selection_token));

    HWND parent = CreateWindowExW(
        0, L"STATIC", L"MUXC TabView oracle",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        40, 40, 660, 180, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    observations.HResult("hosting.parent",
                         parent ? S_OK : HRESULT_FROM_WIN32(GetLastError()));

    Ref<IInspectable> source_instance;
    const HRESULT source_activation = runtime.Activate(
        L"Windows.UI.Xaml.Hosting.DesktopWindowXamlSource", true,
        source_instance.put());
    observations.HResult("hosting.source.activate", source_activation);
    Ref<wuxh::IDesktopWindowXamlSource> source;
    observations.HResult("hosting.source.qi",
        Query(source_instance.get(),
              openxaml::iid::Windows_UI_Xaml_Hosting_IDesktopWindowXamlSource,
              source));
    Ref<IDesktopWindowXamlSourceNative> native;
    observations.HResult("hosting.source.native_qi",
        Query(source_instance.get(), IID_IslandNative, native));
    HWND island = nullptr;
    HRESULT attach_hr = native && parent ? native->AttachToWindow(parent) : E_POINTER;
    observations.HResult("hosting.attach", attach_hr);
    HRESULT handle_hr = native ? native->get_WindowHandle(&island) : E_POINTER;
    observations.HResult("hosting.window_handle", handle_hr);
    wchar_t island_class[256]{};
    if (island) GetClassNameW(island, island_class, ARRAYSIZE(island_class));
    observations.String("hosting.island_class", Narrow(island_class));

    Ref<wux::IUIElement> element;
    Ref<wux::IFrameworkElement> framework;
    observations.HResult("hosting.element.qi",
        Query(tab_instance.get(), openxaml::iid::Windows_UI_Xaml_IUIElement, element));
    observations.HResult("hosting.framework.qi",
        Query(tab_instance.get(), openxaml::iid::Windows_UI_Xaml_IFrameworkElement,
              framework));

    Ref<IInspectable> root_instance;
    observations.HResult("hosting.root.activate", runtime.Activate(
        L"Windows.UI.Xaml.Controls.Grid", true, root_instance.put()));
    Ref<wux::IUIElement> root_element;
    Ref<wux::IFrameworkElement> root_framework;
    Ref<wuxc::IPanel> root_panel;
    observations.HResult("hosting.root.element_qi",
        Query(root_instance.get(), openxaml::iid::Windows_UI_Xaml_IUIElement,
              root_element));
    observations.HResult("hosting.root.framework_qi",
        Query(root_instance.get(), openxaml::iid::Windows_UI_Xaml_IFrameworkElement,
              root_framework));
    observations.HResult("hosting.root.panel_qi",
        Query(root_instance.get(), openxaml::iid::Windows_UI_Xaml_Controls_IPanel,
              root_panel));

    Ref<IInspectable> controls_resources_instance;
    const HRESULT controls_resources_activation = runtime.Activate(
        L"Microsoft.UI.Xaml.Controls.XamlControlsResources", false,
        controls_resources_instance.put());
    observations.HResult("hosting.controls_resources.activate",
                         controls_resources_activation);
    Ref<wux::IResourceDictionary> controls_resources;
    observations.HResult("hosting.controls_resources.qi",
        Query(controls_resources_instance.get(), IID_IResourceDictionary,
              controls_resources));
    Ref<wux::IResourceDictionary> local_resources;
    HRESULT local_resources_hr = root_framework
        ? root_framework->get_Resources(local_resources.put()) : E_POINTER;
    observations.HResult("hosting.resources.get", local_resources_hr);
    Ref<__FIVector_1_Windows__CUI__CXaml__CResourceDictionary> merged;
    HRESULT merged_hr = local_resources
        ? local_resources->get_MergedDictionaries(merged.put()) : E_POINTER;
    observations.HResult("hosting.resources.merged", merged_hr);
    const HRESULT merge_hr = merged && controls_resources
        ? merged->Append(controls_resources.get())
        : E_POINTER;
    observations.HResult("hosting.resources.append_controls", merge_hr);

    Ref<__FIVector_1_Windows__CUI__CXaml__CUIElement> root_children;
    HRESULT children_hr = root_panel
        ? root_panel->get_Children(root_children.put()) : E_POINTER;
    observations.HResult("hosting.root.children", children_hr);
    Ref<IInspectable> xaml_root_instance;
    Ref<wux::IUIElement> xaml_root_element;
    Ref<wux::IUIElement> hosted_element;
    Ref<IMuxcTabView> hosted_tab;
    Ref<IInspectable> hosted_first;
    Ref<IInspectable> hosted_second;
    Ref<wux::IFrameworkElement> hosted_framework;
    wux::IUIElement* content_element = root_element.get();

    if (mode == L"official") {
        Ref<wuxmk::IXamlReaderStatics> reader;
        HRESULT reader_hr = runtime.Statics(
            L"Windows.UI.Xaml.Markup.XamlReader", true,
            IID_IXamlReaderStatics, reader.put());
        observations.HResult("diagnostic.xaml_reader.factory", reader_hr);
        HString markup(
            L"<muxc:TabView "
            L"xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' "
            L"xmlns:muxc='using:Microsoft.UI.Xaml.Controls' "
            L"Width='640' Height='120' CloseButtonOverlayMode='Always'>"
            L"<muxc:TabViewItem Header='One' IsClosable='True'/>"
            L"<muxc:TabViewItem Header='Two' IsClosable='True'/>"
            L"</muxc:TabView>");
        HRESULT load_hr = reader
            ? reader->Load(markup.get(), xaml_root_instance.put()) : reader_hr;
        observations.HResult("diagnostic.xaml_reader.load", load_hr);
        if (FAILED(load_hr))
            observations.String("diagnostic.xaml_reader.error",
                                RestrictedErrorText());
        observations.HResult("diagnostic.xaml_root.element_qi",
            Query(xaml_root_instance.get(), openxaml::iid::Windows_UI_Xaml_IUIElement,
                  xaml_root_element));
        observations.HResult("diagnostic.xaml_tab.qi",
            Query(xaml_root_instance.get(), IID_IMuxcTabView, hosted_tab));
        observations.HResult("diagnostic.xaml_tab.framework_qi",
            Query(xaml_root_instance.get(),
                  openxaml::iid::Windows_UI_Xaml_IFrameworkElement,
                  hosted_framework));
        (void)Query(xaml_root_instance.get(),
                    openxaml::iid::Windows_UI_Xaml_IUIElement,
                    hosted_element);
        Ref<__FIVector_1_IInspectable> xaml_items;
        HRESULT xaml_items_hr = hosted_tab
            ? hosted_tab->get_TabItems(reinterpret_cast<void**>(xaml_items.put()))
            : E_POINTER;
        observations.HResult("diagnostic.xaml_tab.items", xaml_items_hr);
        observations.HResult("diagnostic.xaml_tab.first_item",
            xaml_items ? xaml_items->GetAt(0, hosted_first.put()) : E_POINTER);
        observations.HResult("diagnostic.xaml_tab.second_item",
            xaml_items ? xaml_items->GetAt(1, hosted_second.put()) : E_POINTER);
        // A sparse package supplies WinUI's resources but has no compiled app
        // metadata for XamlReader's `using:` resolver. In that environment the
        // parser correctly reports "type not found" even though direct WinRT
        // activation works. Keep that diagnostic, then exercise the exact same
        // public TabView instance used by the detached contract checks.
        if (!hosted_tab) {
            (void)Query(tab_instance.get(), IID_IMuxcTabView, hosted_tab);
            (void)Query(tab_instance.get(),
                        openxaml::iid::Windows_UI_Xaml_IFrameworkElement,
                        hosted_framework);
            (void)Query(tab_instance.get(),
                        openxaml::iid::Windows_UI_Xaml_IUIElement,
                        hosted_element);
            (void)Query(first_instance.get(), openxaml::iid::IInspectable,
                        hosted_first);
            (void)Query(second_instance.get(), openxaml::iid::IInspectable,
                        hosted_second);
        }
    } else {
        observations.HResult("diagnostic.xaml_reader.factory", E_NOTIMPL);
        observations.HResult("diagnostic.xaml_reader.load", E_NOTIMPL);
        (void)Query(tab_instance.get(), IID_IMuxcTabView, hosted_tab);
        (void)Query(tab_instance.get(),
                    openxaml::iid::Windows_UI_Xaml_IFrameworkElement,
                    hosted_framework);
        (void)Query(tab_instance.get(),
                    openxaml::iid::Windows_UI_Xaml_IUIElement,
                    hosted_element);
        (void)Query(first_instance.get(), openxaml::iid::IInspectable,
                    hosted_first);
        (void)Query(second_instance.get(), openxaml::iid::IInspectable,
                    hosted_second);
    }

    observations.HResult("hosting.root.append_tab",
        root_children && hosted_element
            ? root_children->Append(hosted_element.get()) : E_POINTER);

    CloseHandler close({hosted_first.get(), hosted_second.get()});
    AddClickHandler add;
    EventRegistrationToken close_token{};
    EventRegistrationToken add_token{};
    observations.HResult("input.close_handler.add",
        hosted_tab ? hosted_tab->add_TabCloseRequested(
            static_cast<ProjectedTabCloseHandler*>(&close), &close_token)
                   : E_POINTER);
    observations.HResult("input.add_handler.add",
        hosted_tab ? hosted_tab->add_AddTabButtonClick(
            static_cast<ProjectedAddClickHandler*>(&add), &add_token)
                   : E_POINTER);

    if (framework) {
        observations.HResult("hosting.width", framework->put_Width(640.0));
        observations.HResult("hosting.height", framework->put_Height(120.0));
    }
    if (root_framework) {
        observations.HResult("hosting.root.width", root_framework->put_Width(640.0));
        observations.HResult("hosting.root.height", root_framework->put_Height(120.0));
    }
    observations.HResult("hosting.content",
        source && content_element ? source->put_Content(content_element) : E_POINTER);
    bool input_sweep_valid = false;
    if (island) {
        SetWindowPos(island, nullptr, 0, 0, 640, 120,
                     SWP_NOACTIVATE | SWP_NOZORDER | SWP_SHOWWINDOW);
        input_sweep_valid = TakeForeground(parent);
        observations.Bool("hosting.foreground", input_sweep_valid);
        UpdateWindow(parent);
        PumpFor(500);
        observations.HResult("hosting.select_first",
            hosted_tab ? hosted_tab->put_SelectedIndex(0) : E_POINTER);
        PumpFor(100);
        DOUBLE actual_width = -1.0;
        DOUBLE actual_height = -1.0;
        HRESULT actual_hr = hosted_framework
            ? hosted_framework->get_ActualWidth(&actual_width) : E_POINTER;
        observations.HResult("hosting.actual_width.hr", actual_hr);
        if (SUCCEEDED(actual_hr))
            observations.Number("hosting.actual_width.value",
                                static_cast<long long>(actual_width));
        actual_hr = hosted_framework
            ? hosted_framework->get_ActualHeight(&actual_height) : E_POINTER;
        observations.HResult("hosting.actual_height.hr", actual_hr);
        if (SUCCEEDED(actual_hr))
            observations.Number("hosting.actual_height.value",
                                static_cast<long long>(actual_height));
    }

    if (island && argc == 5) {
        observations.HResult("pixels.capture", Capture(island, argv[4], 640, 120));
        observations.Number("pixels.width", 640);
        observations.Number("pixels.height", 120);
    }

    observations.Bool("input.sweep.valid", input_sweep_valid);
    if (island && input_sweep_valid) {
        for (int y = 4; y < 60; y += 8)
            for (int x = 4; x < 640; x += 8)
                Click(island, x, y, true, close, add);
    }
    observations.Raw("input.close_hits", CloseHitsJson(close.hits));
    observations.Raw("input.add_hits", AddHitsJson(add.hits));
    observations.Number("input.close_hit_count",
                        static_cast<long long>(close.hits.size()));
    observations.Number("input.add_hit_count",
                        static_cast<long long>(add.hits.size()));
    observations.HResult("input.close_handler.remove",
        hosted_tab ? hosted_tab->remove_TabCloseRequested(close_token) : E_POINTER);
    observations.HResult("input.add_handler.remove",
        hosted_tab ? hosted_tab->remove_AddTabButtonClick(add_token) : E_POINTER);

    const bool wrote = observations.Write(argv[3], runtime, Narrow(mode));
    // XAML and WinUI both retain process-wide factory and compositor state.
    // A fresh process is already the oracle's isolation boundary, so leave
    // teardown to the loader after the complete output has been closed. This
    // also avoids testing shutdown order instead of the TabView contract.
    ExitProcess(wrote ? 0 : 5);
}
