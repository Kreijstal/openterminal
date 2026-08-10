// TabView selection is a runtime semantic, not just property storage: Windows
// Terminal attaches the selected pane from its SelectionChanged handler.

#include "sdk.h"

#include <roapi.h>

#include <cstdio>
#include <cwchar>

namespace wuxc = ABI::Windows::UI::Xaml::Controls;

inline constexpr GUID IID_IMuxcTabView = {
    0x6aa787ab, 0x5a30, 0x5ea2,
    {0xbe, 0x5b, 0xae, 0xd8, 0x68, 0x38, 0x17, 0x56}};

// Microsoft.UI.Xaml is described by its WinMD rather than the Windows SDK
// headers used to compile this standalone ABI client. Keep the exact ITabView
// method order through SelectionChanged so this test exercises the real slot.
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
    virtual HRESULT STDMETHODCALLTYPE add_TabCloseRequested(void*, EventRegistrationToken*) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_TabCloseRequested(EventRegistrationToken) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_TabDroppedOutside(void*, EventRegistrationToken*) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_TabDroppedOutside(EventRegistrationToken) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_AddTabButtonClick(void*, EventRegistrationToken*) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_AddTabButtonClick(EventRegistrationToken) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_TabItemsChanged(void*, EventRegistrationToken*) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_TabItemsChanged(EventRegistrationToken) = 0;
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
    virtual HRESULT STDMETHODCALLTYPE remove_SelectionChanged(EventRegistrationToken) = 0;
};

template <class Interface>
Interface* Activate(const wchar_t* name, const GUID& iid) {
    HSTRING class_name = nullptr;
    if (FAILED(WindowsCreateString(
            name, static_cast<UINT32>(std::wcslen(name)), &class_name)))
        return nullptr;
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

IInspectable* ActivateInspectable(const wchar_t* name) {
    HSTRING class_name = nullptr;
    if (FAILED(WindowsCreateString(
            name, static_cast<UINT32>(std::wcslen(name)), &class_name)))
        return nullptr;
    IInspectable* instance = nullptr;
    const HRESULT hr = RoActivateInstance(class_name, &instance);
    WindowsDeleteString(class_name);
    return SUCCEEDED(hr) ? instance : nullptr;
}

bool SameIdentity(IUnknown* left, IUnknown* right) {
    if (!left || !right) return left == right;
    IUnknown* left_identity = nullptr;
    IUnknown* right_identity = nullptr;
    const HRESULT left_hr = left->QueryInterface(
        IID_IUnknown, reinterpret_cast<void**>(&left_identity));
    const HRESULT right_hr = right->QueryInterface(
        IID_IUnknown, reinterpret_cast<void**>(&right_identity));
    const bool same = SUCCEEDED(left_hr) && SUCCEEDED(right_hr) &&
                      left_identity == right_identity;
    if (left_identity) left_identity->Release();
    if (right_identity) right_identity->Release();
    return same;
}

class SelectionHandler final : public wuxc::ISelectionChangedEventHandler {
public:
    ~SelectionHandler() {
        if (retained_args_) retained_args_->Release();
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        if (IsEqualGUID(iid, IID_IUnknown)) {
            AddRef();
            *object = static_cast<wuxc::ISelectionChangedEventHandler*>(this);
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return static_cast<ULONG>(InterlockedIncrement(&references_));
    }
    ULONG STDMETHODCALLTYPE Release() override {
        return static_cast<ULONG>(InterlockedDecrement(&references_));
    }

    HRESULT STDMETHODCALLTYPE Invoke(
        IInspectable*, wuxc::ISelectionChangedEventArgs* args) override {
        ++calls;
        if (retained_args_) retained_args_->Release();
        retained_args_ = args;
        if (retained_args_) retained_args_->AddRef();
        return S_OK;
    }

    bool HasChange(IInspectable* removed, IInspectable* added) const {
        if (!retained_args_) return false;
        __FIVector_1_IInspectable* removed_items = nullptr;
        __FIVector_1_IInspectable* added_items = nullptr;
        if (FAILED(retained_args_->get_RemovedItems(&removed_items)) ||
            FAILED(retained_args_->get_AddedItems(&added_items)) ||
            !removed_items || !added_items) {
            if (removed_items) removed_items->Release();
            if (added_items) added_items->Release();
            return false;
        }
        UINT32 removed_count = 0;
        UINT32 added_count = 0;
        removed_items->get_Size(&removed_count);
        added_items->get_Size(&added_count);
        bool matches = removed_count == (removed ? 1u : 0u) &&
                       added_count == (added ? 1u : 0u);
        if (matches && removed) {
            IInspectable* value = nullptr;
            matches = SUCCEEDED(removed_items->GetAt(0, &value)) &&
                      SameIdentity(value, removed);
            if (value) value->Release();
        }
        if (matches && added) {
            IInspectable* value = nullptr;
            matches = SUCCEEDED(added_items->GetAt(0, &value)) &&
                      SameIdentity(value, added);
            if (value) value->Release();
        }
        removed_items->Release();
        added_items->Release();
        return matches;
    }

    int calls = 0;

private:
    LONG references_ = 1;
    wuxc::ISelectionChangedEventArgs* retained_args_ = nullptr;
};

int main() {
    int failures = 0;
    const auto check = [&](bool condition, const char* message) {
        if (!condition) {
            std::fprintf(stderr, "FAIL: %s\n", message);
            ++failures;
        }
    };

    const HRESULT initialized = RoInitialize(RO_INIT_SINGLETHREADED);
    check(SUCCEEDED(initialized) || initialized == RPC_E_CHANGED_MODE,
          "RoInitialize");

    IMuxcTabView* tab_view = Activate<IMuxcTabView>(
        L"Microsoft.UI.Xaml.Controls.TabView", IID_IMuxcTabView);
    IInspectable* first = ActivateInspectable(
        L"Microsoft.UI.Xaml.Controls.TabViewItem");
    IInspectable* second = ActivateInspectable(
        L"Microsoft.UI.Xaml.Controls.TabViewItem");
    check(tab_view && first && second, "activate TabView and two items");

    __FIVector_1_IInspectable* items = nullptr;
    if (tab_view)
        check(SUCCEEDED(tab_view->get_TabItems(
                  reinterpret_cast<void**>(&items))) && items,
              "get TabItems");
    if (items && first && second) {
        check(SUCCEEDED(items->Append(first)), "append first item");
        check(SUCCEEDED(items->Append(second)), "append second item");
    }

    SelectionHandler handler;
    EventRegistrationToken token{};
    if (tab_view) {
        check(SUCCEEDED(tab_view->add_SelectionChanged(&handler, &token)) &&
                  token.value != 0,
              "register SelectionChanged");
        check(SUCCEEDED(tab_view->put_SelectedItem(first)), "select first item");
        check(handler.calls == 1 && handler.HasChange(nullptr, first),
              "first selection raises added item and retained args stay valid");
        INT32 index = -2;
        tab_view->get_SelectedIndex(&index);
        check(index == 0, "SelectedItem synchronizes SelectedIndex");

        check(SUCCEEDED(tab_view->put_SelectedItem(first)),
              "repeat selected item");
        check(handler.calls == 1, "repeat selection raises no duplicate event");

        check(SUCCEEDED(tab_view->put_SelectedItem(second)),
              "select second item");
        check(handler.calls == 2 && handler.HasChange(first, second),
              "selection change reports removed and added items");
        tab_view->get_SelectedIndex(&index);
        check(index == 1, "second item index is synchronized");

        check(SUCCEEDED(tab_view->put_SelectedIndex(0)),
              "select by index");
        void* selected_value = nullptr;
        tab_view->get_SelectedItem(&selected_value);
        check(SameIdentity(static_cast<IInspectable*>(selected_value), first),
              "SelectedIndex synchronizes SelectedItem");
        if (selected_value)
            static_cast<IInspectable*>(selected_value)->Release();
        check(handler.calls == 3 && handler.HasChange(second, first),
              "index selection raises the same typed event");

        check(SUCCEEDED(tab_view->remove_SelectionChanged(token)),
              "remove SelectionChanged");
        check(SUCCEEDED(tab_view->put_SelectedItem(second)),
              "selection after handler removal");
        check(handler.calls == 3, "removed handler is not invoked");
    }

    if (items) items->Release();
    if (second) second->Release();
    if (first) first->Release();
    if (tab_view) tab_view->Release();
    if (SUCCEEDED(initialized)) RoUninitialize();

    if (!failures) std::puts("TabView selection checks passed");
    return failures ? 1 : 0;
}
