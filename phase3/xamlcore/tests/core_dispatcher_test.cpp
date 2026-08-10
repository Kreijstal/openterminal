#include <windows.h>

#include <cstdio>

#include "core_dispatcher.h"

namespace wf = ABI::Windows::Foundation;
namespace wuc = ABI::Windows::UI::Core;

inline constexpr GUID IID_IAsyncInfoValue = {
    0x00000036, 0x0000, 0x0000,
    {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

class DispatchedHandler final : public wuc::IDispatchedHandler {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (IsEqualGUID(iid, IID_IUnknown)) {
            *value = static_cast<wuc::IDispatchedHandler*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs_); }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG refs = InterlockedDecrement(&refs_);
        if (!refs) delete this;
        return refs;
    }
    HRESULT STDMETHODCALLTYPE Invoke() override {
        invoked = true;
        return S_OK;
    }
    bool invoked = false;
private:
    LONG refs_ = 1;
};

class CompletedHandler final : public wf::IAsyncActionCompletedHandler {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (IsEqualGUID(iid, IID_IUnknown)) {
            *value = static_cast<wf::IAsyncActionCompletedHandler*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs_); }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG refs = InterlockedDecrement(&refs_);
        if (!refs) delete this;
        return refs;
    }
    HRESULT STDMETHODCALLTYPE Invoke(wf::IAsyncAction*, wf::AsyncStatus value) override {
        invoked = true;
        status = value;
        return S_OK;
    }
    bool invoked = false;
    wf::AsyncStatus status = wf::AsyncStatus::Started;
private:
    LONG refs_ = 1;
};

int main() {
    wuc::ICoreDispatcher* dispatcher = nullptr;
    if (FAILED(openxaml::winrt::GetCoreDispatcherForCurrentThread(&dispatcher)) ||
        !dispatcher) {
        std::fputs("dispatcher construction failed\n", stderr);
        return 1;
    }
    boolean access = false;
    if (FAILED(dispatcher->get_HasThreadAccess(&access)) || !access) return 2;
    wuc::ICoreDispatcher* same_dispatcher = nullptr;
    if (FAILED(openxaml::winrt::GetCoreDispatcherForCurrentThread(&same_dispatcher)) ||
        same_dispatcher != dispatcher)
        return 10;
    same_dispatcher->Release();
    struct ThreadProbe {
        wuc::ICoreDispatcher* dispatcher;
        boolean access = true;
    } probe{dispatcher};
    HANDLE thread = CreateThread(
        nullptr, 0,
        [](void* context) -> DWORD {
            auto* probe = static_cast<ThreadProbe*>(context);
            return FAILED(probe->dispatcher->get_HasThreadAccess(&probe->access));
        },
        &probe, 0, nullptr);
    if (!thread || WaitForSingleObject(thread, 5000) != WAIT_OBJECT_0) return 11;
    DWORD thread_result = 1;
    GetExitCodeThread(thread, &thread_result);
    CloseHandle(thread);
    if (thread_result || probe.access) return 12;

    auto* callback = new DispatchedHandler();
    wf::IAsyncAction* action = nullptr;
    if (FAILED(dispatcher->RunAsync(wuc::CoreDispatcherPriority_Low, callback,
                                    &action)) || !action)
        return 3;
    if (callback->invoked) return 4;

    IAsyncInfo* info = nullptr;
    if (FAILED(action->QueryInterface(IID_IAsyncInfoValue,
                                      reinterpret_cast<void**>(&info))) || !info)
        return 5;
    wf::AsyncStatus status{};
    if (FAILED(info->get_Status(&status)) || status != wf::AsyncStatus::Started)
        return 6;

    auto* completed = new CompletedHandler();
    if (FAILED(action->put_Completed(completed))) return 7;
    MSG message{};
    while (!callback->invoked && GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    if (!callback->invoked || !completed->invoked ||
        completed->status != wf::AsyncStatus::Completed)
        return 8;
    if (FAILED(info->get_Status(&status)) || status != wf::AsyncStatus::Completed ||
        FAILED(action->GetResults()))
        return 9;

    completed->Release();
    callback->Release();
    info->Release();
    action->Release();
    dispatcher->Release();
    std::puts("core dispatcher checks passed");
    return 0;
}
