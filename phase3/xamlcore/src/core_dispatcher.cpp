#include "core_dispatcher.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <new>
#include <vector>

#include "com.h"

namespace openxaml::winrt {
namespace {

namespace wf = ABI::Windows::Foundation;
namespace wuc = ABI::Windows::UI::Core;

inline constexpr GUID IID_IAsyncActionValue = {
    0x5a648006, 0x843a, 0x4da9,
    {0x86, 0x5b, 0x9d, 0x26, 0xe5, 0xdf, 0xad, 0x7b}};
inline constexpr GUID IID_ICoreDispatcherValue = {
    0x60db2fa8, 0xb705, 0x4fde,
    {0xa7, 0xd6, 0xeb, 0xbb, 0x18, 0x91, 0xd3, 0x9e}};
inline constexpr GUID IID_IAsyncInfoValue = {
    0x00000036, 0x0000, 0x0000,
    {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
inline constexpr HRESULT kRoClosed = static_cast<HRESULT>(0x80000013UL);
inline constexpr HRESULT kIllegalMethodCall = static_cast<HRESULT>(0x8000000EUL);
inline constexpr HRESULT kIllegalDelegateAssignment =
    static_cast<HRESULT>(0x80000018UL);

std::atomic<UINT32> next_action_id{1};

void TraceDispatcher(const char* event, UINT32 id, HRESULT result = S_OK) noexcept {
    if (!GetEnvironmentVariableW(L"OPENXAML_TRACE_DISPATCHER", nullptr, 0)) return;
    char message[192]{};
    std::snprintf(message, sizeof(message),
                  "OpenXaml dispatcher event=%s thread=%lu action=%lu result=0x%08lx\n",
                  event, static_cast<unsigned long>(GetCurrentThreadId()),
                  static_cast<unsigned long>(id),
                  static_cast<unsigned long>(static_cast<ULONG>(result)));
    OutputDebugStringA(message);
}

class DispatcherAction final : public ComObject,
                               public wf::IAsyncAction,
                               public IAsyncInfo {
public:
    DispatcherAction() : id_(next_action_id.fetch_add(1)) {}
    ~DispatcherAction() override {
        if (completed_) completed_->Release();
    }

    const wchar_t* RuntimeClassName() const override {
        return L"Windows.Foundation.IAsyncAction";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(IID_IAsyncActionValue, wf::IAsyncAction)
        OPENXAML_QI_ARM(IID_IAsyncInfoValue, IAsyncInfo)
        OPENXAML_QI_ARM(IID_IUnknown, wf::IAsyncAction)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable, wf::IAsyncAction)
        *object = nullptr;
        return TraceQueryInterfaceMiss(RuntimeClassName(), iid);
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE put_Completed(
        wf::IAsyncActionCompletedHandler* handler) override {
        wf::AsyncStatus status = wf::AsyncStatus::Started;
        AcquireSRWLockExclusive(&lock_);
        if (closed_) {
            ReleaseSRWLockExclusive(&lock_);
            return kRoClosed;
        }
        if (completed_) {
            ReleaseSRWLockExclusive(&lock_);
            return kIllegalDelegateAssignment;
        }
        if (handler) handler->AddRef();
        completed_ = handler;
        status = status_;
        if (handler && status != wf::AsyncStatus::Started) handler->AddRef();
        ReleaseSRWLockExclusive(&lock_);

        if (handler && status != wf::AsyncStatus::Started) {
            AddRef();
            const HRESULT hr = handler->Invoke(
                static_cast<wf::IAsyncAction*>(this), status);
            handler->Release();
            Release();
            return hr;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_Completed(
        wf::IAsyncActionCompletedHandler** handler) override {
        if (!handler) return E_POINTER;
        AcquireSRWLockShared(&lock_);
        *handler = completed_;
        if (*handler) (*handler)->AddRef();
        ReleaseSRWLockShared(&lock_);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetResults() override {
        AcquireSRWLockShared(&lock_);
        const bool closed = closed_;
        const wf::AsyncStatus status = status_;
        const HRESULT error = error_;
        ReleaseSRWLockShared(&lock_);
        if (closed) return kRoClosed;
        if (status == wf::AsyncStatus::Started) return kIllegalMethodCall;
        if (status == wf::AsyncStatus::Canceled) return E_ABORT;
        return error;
    }

    HRESULT STDMETHODCALLTYPE get_Id(UINT32* id) override {
        if (!id) return E_POINTER;
        *id = id_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Status(wf::AsyncStatus* status) override {
        if (!status) return E_POINTER;
        AcquireSRWLockShared(&lock_);
        *status = status_;
        ReleaseSRWLockShared(&lock_);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_ErrorCode(HRESULT* error) override {
        if (!error) return E_POINTER;
        AcquireSRWLockShared(&lock_);
        *error = error_;
        ReleaseSRWLockShared(&lock_);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Cancel() override {
        Finish(wf::AsyncStatus::Canceled, E_ABORT);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Close() override {
        AcquireSRWLockExclusive(&lock_);
        closed_ = true;
        auto* completed = completed_;
        completed_ = nullptr;
        ReleaseSRWLockExclusive(&lock_);
        if (completed) completed->Release();
        return S_OK;
    }

    bool ShouldInvoke() const {
        AcquireSRWLockShared(&lock_);
        const bool invoke = status_ == wf::AsyncStatus::Started && !closed_;
        ReleaseSRWLockShared(&lock_);
        return invoke;
    }
    UINT32 Id() const noexcept { return id_; }
    void Complete(HRESULT result) {
        Finish(SUCCEEDED(result) ? wf::AsyncStatus::Completed
                                 : wf::AsyncStatus::Error,
               result);
    }

private:
    void Finish(wf::AsyncStatus status, HRESULT error) {
        wf::IAsyncActionCompletedHandler* completed = nullptr;
        AcquireSRWLockExclusive(&lock_);
        if (status_ == wf::AsyncStatus::Started) {
            status_ = status;
            error_ = error;
            completed = completed_;
            if (completed) completed->AddRef();
        }
        ReleaseSRWLockExclusive(&lock_);

        if (completed) {
            AddRef();
            completed->Invoke(static_cast<wf::IAsyncAction*>(this), status);
            completed->Release();
            Release();
        }
    }

    mutable SRWLOCK lock_ = SRWLOCK_INIT;
    const UINT32 id_;
    wf::AsyncStatus status_ = wf::AsyncStatus::Started;
    HRESULT error_ = S_OK;
    wf::IAsyncActionCompletedHandler* completed_ = nullptr;
    bool closed_ = false;
};

struct DispatchWork {
    wuc::CoreDispatcherPriority priority;
    std::uint64_t order;
    wuc::IDispatchedHandler* handler;
    DispatcherAction* action;
};

struct DispatcherQueue {
    SRWLOCK lock = SRWLOCK_INIT;
    std::vector<DispatchWork*> work;
    std::uint64_t next_order = 0;
    bool wake_pending = false;
    HWND window = nullptr;
};

constexpr UINT kDispatchMessage = WM_APP + 0x584;
constexpr wchar_t kDispatcherWindowClass[] = L"OpenXaml.CoreDispatcher.Queue";
INIT_ONCE dispatcher_class_once = INIT_ONCE_STATIC_INIT;

BOOL CALLBACK RegisterDispatcherClass(PINIT_ONCE, PVOID, PVOID*) {
    WNDCLASSEXW descriptor{};
    descriptor.cbSize = sizeof(descriptor);
    descriptor.lpfnWndProc = [](HWND window, UINT message, WPARAM wparam,
                               LPARAM lparam) -> LRESULT {
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
            SetWindowLongPtrW(window, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }
        auto* queue = reinterpret_cast<DispatcherQueue*>(
            GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message != kDispatchMessage || !queue)
            return DefWindowProcW(window, message, wparam, lparam);

        DispatchWork* selected = nullptr;
        bool post_next = false;
        AcquireSRWLockExclusive(&queue->lock);
        if (!queue->work.empty()) {
            const auto best = std::max_element(
                queue->work.begin(), queue->work.end(),
                [](const DispatchWork* left, const DispatchWork* right) {
                    if (left->priority != right->priority)
                        return left->priority < right->priority;
                    return left->order > right->order;
                });
            selected = *best;
            queue->work.erase(best);
        }
        post_next = !queue->work.empty();
        queue->wake_pending = post_next;
        ReleaseSRWLockExclusive(&queue->lock);

        if (post_next) PostMessageW(window, kDispatchMessage, 0, 0);
        if (selected) {
            HRESULT result = S_OK;
            TraceDispatcher("invoke-begin", selected->action->Id());
            if (selected->action->ShouldInvoke())
                result = selected->handler->Invoke();
            TraceDispatcher("invoke-end", selected->action->Id(), result);
            selected->action->Complete(result);
            selected->handler->Release();
            selected->action->Release();
            delete selected;
        }
        return 0;
    };
    descriptor.hInstance = GetModuleHandleW(nullptr);
    descriptor.lpszClassName = kDispatcherWindowClass;
    return RegisterClassExW(&descriptor) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

thread_local DispatcherQueue* current_queue = nullptr;

DispatcherQueue* EnsureCurrentQueue() {
    if (current_queue && IsWindow(current_queue->window)) return current_queue;
    if (!InitOnceExecuteOnce(&dispatcher_class_once, RegisterDispatcherClass,
                             nullptr, nullptr))
        return nullptr;
    auto* queue = new (std::nothrow) DispatcherQueue();
    if (!queue) return nullptr;
    queue->window = CreateWindowExW(
        0, kDispatcherWindowClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE,
        nullptr, GetModuleHandleW(nullptr), queue);
    if (!queue->window) {
        delete queue;
        return nullptr;
    }
    current_queue = queue;
    return queue;
}

HRESULT Enqueue(DispatcherQueue* queue, wuc::CoreDispatcherPriority priority,
                wuc::IDispatchedHandler* handler, DispatcherAction* action) {
    if (!IsWindow(queue->window) ||
        reinterpret_cast<DispatcherQueue*>(
            GetWindowLongPtrW(queue->window, GWLP_USERDATA)) != queue)
        return HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE);
    auto* work = new (std::nothrow) DispatchWork();
    if (!work) return E_OUTOFMEMORY;
    handler->AddRef();
    action->AddRef();
    work->priority = priority;
    work->handler = handler;
    work->action = action;

    bool post = false;
    AcquireSRWLockExclusive(&queue->lock);
    work->order = queue->next_order++;
    try {
        queue->work.push_back(work);
    } catch (const std::bad_alloc&) {
        ReleaseSRWLockExclusive(&queue->lock);
        handler->Release();
        action->Release();
        delete work;
        return E_OUTOFMEMORY;
    }
    if (!queue->wake_pending) {
        queue->wake_pending = true;
        post = true;
    }
    ReleaseSRWLockExclusive(&queue->lock);

    if (!post || PostMessageW(queue->window, kDispatchMessage, 0, 0)) return S_OK;

    const DWORD error = GetLastError();
    AcquireSRWLockExclusive(&queue->lock);
    const auto found = std::find(queue->work.begin(), queue->work.end(), work);
    if (found != queue->work.end()) queue->work.erase(found);
    queue->wake_pending = !queue->work.empty();
    ReleaseSRWLockExclusive(&queue->lock);
    handler->Release();
    action->Release();
    delete work;
    return HRESULT_FROM_WIN32(error);
}

class CoreDispatcherObject final : public ComObject,
                                   public wuc::ICoreDispatcher {
public:
    explicit CoreDispatcherObject(DispatcherQueue* queue)
        : queue_(queue), owner_thread_(GetCurrentThreadId()) {}

    const wchar_t* RuntimeClassName() const override {
        return L"Windows.UI.Core.CoreDispatcher";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(IID_ICoreDispatcherValue, wuc::ICoreDispatcher)
        OPENXAML_QI_ARM(IID_IUnknown, wuc::ICoreDispatcher)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable, wuc::ICoreDispatcher)
        *object = nullptr;
        return TraceQueryInterfaceMiss(RuntimeClassName(), iid);
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE get_HasThreadAccess(boolean* value) override {
        if (!value) return E_POINTER;
        *value = GetCurrentThreadId() == owner_thread_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ProcessEvents(wuc::CoreProcessEventsOption) override {
        TraceRuntime("OpenXaml: E_NOTIMPL ICoreDispatcher.ProcessEvents\n");
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE RunAsync(
        wuc::CoreDispatcherPriority priority,
        wuc::IDispatchedHandler* callback,
        wf::IAsyncAction** action) override {
        if (!action) return E_POINTER;
        *action = nullptr;
        if (!callback) return E_INVALIDARG;
        if (priority < wuc::CoreDispatcherPriority_Idle ||
            priority > wuc::CoreDispatcherPriority_High)
            return E_INVALIDARG;

        auto* created = new (std::nothrow) DispatcherAction();
        if (!created) return E_OUTOFMEMORY;
        const HRESULT hr = Enqueue(queue_, priority, callback, created);
        if (FAILED(hr)) {
            created->Release();
            return hr;
        }
        TraceDispatcher("enqueued", created->Id());
        *action = static_cast<wf::IAsyncAction*>(created);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE RunIdleAsync(
        wuc::IIdleDispatchedHandler*, wf::IAsyncAction** action) override {
        if (!action) return E_POINTER;
        *action = nullptr;
        TraceRuntime("OpenXaml: E_NOTIMPL ICoreDispatcher.RunIdleAsync\n");
        return E_NOTIMPL;
    }

private:
    DispatcherQueue* queue_;
    DWORD owner_thread_;
};

}  // namespace

HRESULT GetCoreDispatcherForCurrentThread(wuc::ICoreDispatcher** value) {
    if (!value) return E_POINTER;
    *value = nullptr;
    // CoreDispatcher identity belongs to a Win32 thread, not to whichever
    // DependencyObject getter happened to be called. Keep one owning identity
    // for the lifetime of the thread queue and hand callers normal COM refs.
    static thread_local wuc::ICoreDispatcher* dispatcher = nullptr;
    if (dispatcher) {
        dispatcher->AddRef();
        *value = dispatcher;
        return S_OK;
    }
    DispatcherQueue* queue = EnsureCurrentQueue();
    if (!queue) return HRESULT_FROM_WIN32(GetLastError());
    auto* created = new (std::nothrow) CoreDispatcherObject(queue);
    if (!created) return E_OUTOFMEMORY;
    dispatcher = static_cast<wuc::ICoreDispatcher*>(created);
    dispatcher->AddRef();
    *value = dispatcher;
    return S_OK;
}

}  // namespace openxaml::winrt
