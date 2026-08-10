// End-to-end retained-renderer smoke test for DesktopWindowXamlSource.
//
// This deliberately observes only HWND pixels and a read-only private frame
// generation. It does not reach into Element or call the renderer directly:
// the invalidation, layout, frame compilation and WM_PAINT boundaries must all
// work for the test to pass.

#include "sdk.h"

#include <roapi.h>

#include <cstdint>
#include <cstdio>
#include <cwchar>

#include "com.h"
#include "openxaml_iids.h"

namespace wf = ABI::Windows::Foundation;
namespace wux = ABI::Windows::UI::Xaml;
namespace wuxc = ABI::Windows::UI::Xaml::Controls;
namespace wuxh = ABI::Windows::UI::Xaml::Hosting;
namespace wuxm = ABI::Windows::UI::Xaml::Media;
namespace wuxi = ABI::Windows::UI::Xaml::Input;

namespace {

inline constexpr GUID IID_OpenXamlDesktopWindowXamlSourceNative = {
    0x3cbcf1bf, 0x2f76, 0x4e9c,
    {0x96, 0xab, 0xe8, 0x4b, 0x37, 0x97, 0x25, 0x54}};
inline constexpr HRESULT RO_CLOSED = static_cast<HRESULT>(0x80000013UL);

int failures = 0;
int skipped = 0;

void Check(bool condition, const char* message) {
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", message);
    ++failures;
}

// How far this loader displaces a layered child from where UpdateLayeredWindow
// was told to put it.
//
// The island presents through UpdateLayeredWindow(ULW_ALPHA) on a
// WS_CHILD | WS_EX_LAYERED window with a destination in screen coordinates, as
// the Win32 contract requires. A loader that applies that screen-space point
// inside the parent-relative child transaction moves the child by the parent's
// client origin a second time: the pixels land where they were asked to, and
// the window's own coordinates do not agree with them. Every read that goes
// through the child's client space -- which is every pixel assertion here --
// then reads outside its own frame.
//
// research/wine/af5241854c513c2e68938425cc6cd3cac40b943a/layered-child-update.md
// records that boundary and the narrow Wine change that closes it. This
// measures it rather than assuming it, with plain GDI and no XAML, so the
// pixel assertions below are skipped by name on a loader that has the defect
// and enforced on one that does not.
POINT LayeredChildDisplacement() {
    POINT displacement{0, 0};
    HWND parent = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPEDWINDOW,
                                  0, 0, 64, 64, nullptr, nullptr,
                                  GetModuleHandleW(nullptr), nullptr);
    if (!parent) return displacement;
    HWND child = CreateWindowExW(WS_EX_LAYERED, L"STATIC", L"",
                                 WS_CHILD | WS_VISIBLE, 0, 0, 8, 8, parent,
                                 nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!child) {
        DestroyWindow(parent);
        return displacement;
    }

    POINT requested{0, 0};
    ClientToScreen(child, &requested);

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = 8;
    info.bmiHeader.biHeight = -8;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits,
                                      nullptr, 0);
    if (bitmap && bits) {
        for (int index = 0; index < 8 * 8; ++index)
            static_cast<std::uint32_t*>(bits)[index] = 0xff000000u;
        HGDIOBJ previous = SelectObject(memory, bitmap);
        POINT source{0, 0};
        SIZE size{8, 8};
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        if (UpdateLayeredWindow(child, screen, &requested, &size, memory,
                                &source, 0, &blend, ULW_ALPHA)) {
            RECT placed{};
            GetWindowRect(child, &placed);
            displacement.x = placed.left - requested.x;
            displacement.y = placed.top - requested.y;
        }
        SelectObject(memory, previous);
    }
    if (bitmap) DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    DestroyWindow(parent);
    return displacement;
}

POINT layered_child_displacement{0, 0};

// A pixel assertion, which only means anything when the loader puts a layered
// child where it was told to.
void CheckPixel(bool condition, const char* message) {
    if (layered_child_displacement.x || layered_child_displacement.y) {
        std::fprintf(stderr,
                     "SKIP: %s -- this loader displaces a layered child by "
                     "%+ld,%+ld; see research/wine/"
                     "af5241854c513c2e68938425cc6cd3cac40b943a/"
                     "layered-child-update.md\n",
                     message, layered_child_displacement.x,
                     layered_child_displacement.y);
        ++skipped;
        return;
    }
    Check(condition, message);
}

template <class Interface>
Interface* Activate(const wchar_t* name, const GUID& iid) {
    HSTRING class_name = nullptr;
    if (FAILED(WindowsCreateString(name, static_cast<UINT32>(std::wcslen(name)),
                                   &class_name))) {
        return nullptr;
    }
    IInspectable* instance = nullptr;
    const HRESULT activated = RoActivateInstance(class_name, &instance);
    WindowsDeleteString(class_name);
    if (FAILED(activated) || !instance) return nullptr;
    Interface* result = nullptr;
    const HRESULT queried = instance->QueryInterface(iid,
        reinterpret_cast<void**>(&result));
    instance->Release();
    return SUCCEEDED(queried) ? result : nullptr;
}

void PumpPostedMessages() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

COLORREF PaintAndRead(HWND window, int x = 4, int y = 4) {
    RedrawWindow(window, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
    POINT screen{x, y};
    if (!ClientToScreen(window, &screen)) return CLR_INVALID;
    // A layered HWND's own DC is not an observation of compositor output.
    // Read the final desktop pixel so alpha-to-parent behavior is covered too.
    HDC dc = GetDC(nullptr);
    if (!dc) return CLR_INVALID;
    const COLORREF pixel = GetPixel(dc, screen.x, screen.y);
    ReleaseDC(nullptr, dc);
    return pixel;
}

bool IsRgb(COLORREF pixel, BYTE red, BYTE green, BYTE blue) {
    return pixel != CLR_INVALID && GetRValue(pixel) == red &&
           GetGValue(pixel) == green && GetBValue(pixel) == blue;
}

struct Root {
    wuxc::IBorder* border = nullptr;
    wux::IUIElement* element = nullptr;
    wuxm::ISolidColorBrush* brush = nullptr;

    void Release() {
        if (element) element->Release();
        if (border) border->Release();
        if (brush) brush->Release();
        element = nullptr;
        border = nullptr;
        brush = nullptr;
    }
};

class CloseOnLayout final : public __FIEventHandler_1_IInspectable {
public:
    explicit CloseOnLayout(wf::IClosable* target) : target_(target) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        if (IsEqualGUID(iid, IID_IUnknown)) {
            *object = static_cast<__FIEventHandler_1_IInspectable*>(this);
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
        const ULONG remaining =
            static_cast<ULONG>(InterlockedDecrement(&references_));
        if (!remaining) delete this;
        return remaining;
    }
    HRESULT STDMETHODCALLTYPE Invoke(IInspectable*, IInspectable*) override {
        if (!called_) {
            called_ = true;
            return target_->Close();
        }
        return S_OK;
    }

private:
    ~CloseOnLayout() = default;
    LONG references_ = 1;
    wf::IClosable* target_;  // Invocation is synchronous while the caller owns it.
    bool called_ = false;
};

class BlockingPointerHandler final : public wuxi::IPointerEventHandler {
public:
    BlockingPointerHandler(HANDLE entered, HANDLE resume)
        : entered_(entered), resume_(resume) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        if (IsEqualGUID(iid, IID_IUnknown)) {
            *object = static_cast<wuxi::IPointerEventHandler*>(this);
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
        const ULONG remaining = static_cast<ULONG>(
            InterlockedDecrement(&references_));
        if (!remaining) delete this;
        return remaining;
    }
    HRESULT STDMETHODCALLTYPE Invoke(
        IInspectable*, wuxi::IPointerRoutedEventArgs*) override {
        SetEvent(entered_);
        return WaitForSingleObject(resume_, 5000) == WAIT_OBJECT_0
            ? S_OK : E_FAIL;
    }

private:
    ~BlockingPointerHandler() = default;
    LONG references_ = 1;
    HANDLE entered_ = nullptr;
    HANDLE resume_ = nullptr;
};

struct FinalReleaseWork {
    HANDLE entered = nullptr;
    HANDLE resume = nullptr;
    wuxh::IDesktopWindowXamlSource* source = nullptr;
};

DWORD WINAPI ReleaseSourceDuringWndProc(void* context) {
    auto* work = static_cast<FinalReleaseWork*>(context);
    if (WaitForSingleObject(work->entered, 5000) == WAIT_OBJECT_0 &&
        work->source) {
        work->source->Release();
        work->source = nullptr;
    }
    SetEvent(work->resume);
    return 0;
}

Root MakeRoot(ABI::Windows::UI::Color color) {
    Root root;
    root.border = Activate<wuxc::IBorder>(
        L"Windows.UI.Xaml.Controls.Border",
        openxaml::iid::Windows_UI_Xaml_Controls_IBorder);
    root.brush = Activate<wuxm::ISolidColorBrush>(
        L"Windows.UI.Xaml.Media.SolidColorBrush",
        openxaml::iid::Windows_UI_Xaml_Media_ISolidColorBrush);
    if (!root.border || !root.brush) return root;

    wuxm::IBrush* projected = nullptr;
    root.brush->put_Color(color);
    root.brush->QueryInterface(openxaml::iid::Windows_UI_Xaml_Media_IBrush,
                               reinterpret_cast<void**>(&projected));
    if (projected) {
        root.border->put_Background(projected);
        projected->Release();
    }
    root.border->QueryInterface(openxaml::iid::Windows_UI_Xaml_IUIElement,
                                reinterpret_cast<void**>(&root.element));
    return root;
}

struct Island {
    wuxh::IDesktopWindowXamlSource* source = nullptr;
    IDesktopWindowXamlSourceNative* native = nullptr;
    wf::IClosable* closable = nullptr;
    openxaml::winrt::IOpenXamlIslandDiagnostics* diagnostics = nullptr;
    HWND window = nullptr;

    bool Create(HWND parent, int width, int height) {
        source = Activate<wuxh::IDesktopWindowXamlSource>(
            L"Windows.UI.Xaml.Hosting.DesktopWindowXamlSource",
            openxaml::iid::Windows_UI_Xaml_Hosting_IDesktopWindowXamlSource);
        if (!source) return false;
        source->QueryInterface(IID_OpenXamlDesktopWindowXamlSourceNative,
                               reinterpret_cast<void**>(&native));
        source->QueryInterface(openxaml::iid::Windows_Foundation_IClosable,
                               reinterpret_cast<void**>(&closable));
        source->QueryInterface(openxaml::winrt::IID_IOpenXamlIslandDiagnostics,
                               reinterpret_cast<void**>(&diagnostics));
        if (!native || !closable || !diagnostics ||
            FAILED(native->AttachToWindow(parent)) ||
            FAILED(native->get_WindowHandle(&window))) {
            return false;
        }
        SetWindowPos(window, nullptr, 0, 0, width, height,
                     SWP_NOACTIVATE | SWP_NOZORDER | SWP_SHOWWINDOW);
        return true;
    }

    std::uint64_t Generation() const {
        std::uint64_t result = 0;
        if (diagnostics) diagnostics->GetFrameGeneration(&result);
        return result;
    }

    void ReleaseWithoutClose() {
        if (diagnostics) diagnostics->Release();
        if (closable) closable->Release();
        if (native) native->Release();
        if (source) source->Release();
        diagnostics = nullptr;
        closable = nullptr;
        native = nullptr;
        source = nullptr;
        window = nullptr;
    }
};

}  // namespace

int main() {
    if (FAILED(RoInitialize(RO_INIT_SINGLETHREADED))) return 2;

    layered_child_displacement = LayeredChildDisplacement();

    HWND parent = CreateWindowExW(0, L"STATIC", L"OpenXaml renderer smoke",
                                  WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                  0, 0, 160, 120, nullptr, nullptr,
                                  GetModuleHandleW(nullptr), nullptr);
    Check(parent != nullptr, "create parent HWND");

    Root red = MakeRoot({255, 255, 0, 0});
    Root green = MakeRoot({255, 0, 255, 0});
    Root yellow = MakeRoot({255, 255, 255, 0});
    Check(red.element && red.brush && green.element && green.brush &&
              yellow.element,
          "activate renderer roots and brushes");

    Island first;
    Check(parent && first.Create(parent, 32, 24), "create first desktop island");
    Check(first.source && red.element &&
              SUCCEEDED(first.source->put_Content(red.element)),
          "commit initial red root");
    const std::uint64_t red_generation = first.Generation();
    const COLORREF red_pixel = PaintAndRead(first.window);
    Check(red_generation > 0, "initial frame has a generation");
    CheckPixel(IsRgb(red_pixel, 255, 0, 0), "initial root paints red through HWND");

    Check(SUCCEEDED(red.brush->put_Color({255, 0, 0, 255})),
          "mutate retained brush red to blue");
    PumpPostedMessages();
    const std::uint64_t blue_generation = first.Generation();
    const COLORREF blue_pixel = PaintAndRead(first.window);
    Check(blue_generation == red_generation + 1,
          "brush mutation commits exactly one new frame");
    CheckPixel(IsRgb(blue_pixel, 0, 0, 255),
               "brush mutation repaints blue through HWND");

    const COLORREF replay_one = PaintAndRead(first.window);
    const COLORREF replay_two = PaintAndRead(first.window);
    Check(first.Generation() == blue_generation,
          "two WM_PAINT replays do not rebuild the frame");
    CheckPixel(replay_one == blue_pixel && replay_two == blue_pixel,
               "two WM_PAINT replays preserve cached pixels");

    SetWindowPos(first.window, nullptr, 0, 0, 48, 28,
                 SWP_NOACTIVATE | SWP_NOZORDER);
    INT32 frame_width = 0;
    INT32 frame_height = 0;
    first.diagnostics->GetFrameExtent(&frame_width, &frame_height);
    Check(first.Generation() == blue_generation + 1 &&
              frame_width == 48 && frame_height == 28,
          "WM_SIZE lays out and commits a resized frame outside WM_PAINT");

    Check(SUCCEEDED(first.source->put_Content(green.element)),
          "replace the first island root");
    const std::uint64_t green_generation = first.Generation();
    CheckPixel(IsRgb(PaintAndRead(first.window), 0, 255, 0),
               "replacement root paints green");
    red.brush->put_Color({255, 255, 0, 0});
    PumpPostedMessages();
    // Split deliberately: the generation half is a value the loader cannot
    // affect and is always enforced; only the pixel half depends on where a
    // layered child landed.
    Check(first.Generation() == green_generation,
          "retained old root mutation commits no frame on the replacement island");
    CheckPixel(IsRgb(PaintAndRead(first.window), 0, 255, 0),
               "retained old root mutation cannot invalidate replacement island");

    Check(SUCCEEDED(first.source->put_Content(nullptr)),
          "null content commits an explicit transparent frame");
    wux::IUIElement* cleared_content = reinterpret_cast<wux::IUIElement*>(1);
    first.source->get_Content(&cleared_content);
    const COLORREF clear_pixel = PaintAndRead(first.window);
    Check(cleared_content == nullptr &&
              first.Generation() == green_generation + 1,
          "null content advances exactly one frame generation");
    CheckPixel(!IsRgb(clear_pixel, 0, 255, 0),
               "transparent clear does not replay the detached green root pixels");

    Island second;
    Check(parent && second.Create(parent, 24, 20), "create second desktop island");
    Check(SUCCEEDED(second.source->put_Content(red.element)),
          "reattach detached old root to second island");
    CheckPixel(IsRgb(PaintAndRead(second.window), 255, 0, 0),
               "reattached root renders in second island");

    Island third;
    Check(parent && third.Create(parent, 24, 20), "create ownership-check island");
    Check(SUCCEEDED(third.source->put_Content(yellow.element)),
          "install ownership-check island's original root");
    Check(third.source->put_Content(red.element) == E_INVALIDARG,
          "root attached to another island is rejected");
    wux::IUIElement* still_yellow = nullptr;
    third.source->get_Content(&still_yellow);
    Check(still_yellow == yellow.element,
          "rejected root does not disturb prior island content");
    if (still_yellow) still_yellow->Release();

    Check(SUCCEEDED(first.closable->Close()) &&
              SUCCEEDED(first.closable->Close()),
          "Close is idempotent");
    Check(first.native->AttachToWindow(parent) == RO_CLOSED,
          "AttachToWindow rejects a permanently closed source");
    green.brush->put_Color({255, 0, 0, 255});
    PumpPostedMessages();
    HWND closed_window = nullptr;
    Check(first.native->get_WindowHandle(&closed_window) == E_UNEXPECTED,
          "closed island exposes no stale HWND");

    Island release_while_attached;
    Check(parent && release_while_attached.Create(parent, 12, 12),
          "create island for destructor-close path");
    const HWND released_window = release_while_attached.window;
    release_while_attached.ReleaseWithoutClose();
    Check(!IsWindow(released_window),
          "final Release leaves no HWND holding a dangling host pointer");

    // WindowBinding must turn its nonowning host pointer into a strong call
    // reference before dispatch. Drop the final external source reference on
    // another thread while a routed pointer handler keeps WndProc on-stack;
    // the source may be destroyed only after dispatch returns.
    Island concurrent_release;
    Root concurrent_root = MakeRoot({255, 24, 48, 72});
    Check(parent && concurrent_release.Create(parent, 18, 18) &&
              concurrent_root.element && SUCCEEDED(
                  concurrent_release.source->put_Content(
                      concurrent_root.element)),
          "create concurrent WndProc final-release island");
    HANDLE pointer_entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE pointer_resume = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    auto* blocking_pointer = new BlockingPointerHandler(
        pointer_entered, pointer_resume);
    EventRegistrationToken blocking_token{};
    Check(SUCCEEDED(concurrent_root.element->add_PointerMoved(
              blocking_pointer, &blocking_token)),
          "install blocking pointer handler");
    blocking_pointer->Release();
    const HWND concurrent_window = concurrent_release.window;
    // Leave source as the only external reference. WndProc will acquire its
    // independent lease before the worker releases this one.
    concurrent_release.native->Release();
    concurrent_release.native = nullptr;
    concurrent_release.closable->Release();
    concurrent_release.closable = nullptr;
    concurrent_release.diagnostics->Release();
    concurrent_release.diagnostics = nullptr;
    FinalReleaseWork release_work{
        pointer_entered, pointer_resume, concurrent_release.source};
    concurrent_release.source = nullptr;
    HANDLE release_thread = CreateThread(
        nullptr, 0, ReleaseSourceDuringWndProc, &release_work, 0, nullptr);
    Check(release_thread != nullptr, "start concurrent final-release worker");
    SendMessageW(concurrent_window, WM_MOUSEMOVE, 0, MAKELPARAM(4, 4));
    if (release_thread) {
        WaitForSingleObject(release_thread, 5000);
        CloseHandle(release_thread);
    }
    Check(release_work.source == nullptr && !IsWindow(concurrent_window),
          "WndProc lease defers final deletion and retires the HWND safely");
    concurrent_root.element->remove_PointerMoved(blocking_token);
    CloseHandle(pointer_resume);
    CloseHandle(pointer_entered);
    concurrent_root.Release();

    // PerformLayout raises LayoutUpdated. Closing from that callback exercises
    // the strong native/content token used across the reentrant boundary.
    Root reentrant_root = MakeRoot({255, 64, 32, 16});
    Island reentrant;
    Check(parent && reentrant.Create(parent, 16, 16),
          "create layout-reentrancy island");
    wux::IFrameworkElement* framework = nullptr;
    if (reentrant_root.element) {
        reentrant_root.element->QueryInterface(
            openxaml::iid::Windows_UI_Xaml_IFrameworkElement,
            reinterpret_cast<void**>(&framework));
    }
    auto* close_on_layout = new CloseOnLayout(reentrant.closable);
    EventRegistrationToken layout_token{};
    Check(framework && SUCCEEDED(framework->add_LayoutUpdated(
              close_on_layout, &layout_token)),
          "install reentrant Close LayoutUpdated handler");
    const HWND reentrant_window = reentrant.window;
    Check(reentrant.source && reentrant_root.element &&
              SUCCEEDED(reentrant.source->put_Content(reentrant_root.element)),
          "put_Content survives reentrant Close during layout");
    Check(!IsWindow(reentrant_window),
          "reentrant Close prevents stale frame publication and destroys HWND");
    if (framework) {
        framework->remove_LayoutUpdated(layout_token);
        framework->Release();
    }
    close_on_layout->Release();
    reentrant.ReleaseWithoutClose();
    reentrant_root.Release();

    // A parent can destroy its child without calling the WinRT Close method.
    // WM_NCDESTROY must retire the host token before retained brush callbacks
    // can enqueue work for a recycled HWND value.
    HWND transient_parent = CreateWindowExW(
        0, L"STATIC", L"OpenXaml parent-destroy smoke",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 80, 60,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    Root parent_destroy_root = MakeRoot({255, 32, 64, 96});
    Island parent_destroyed;
    Check(transient_parent && parent_destroyed.Create(transient_parent, 14, 14) &&
              SUCCEEDED(parent_destroyed.source->put_Content(
                  parent_destroy_root.element)),
          "create parent-destruction island");
    const std::uint64_t before_parent_destroy = parent_destroyed.Generation();
    DestroyWindow(transient_parent);
    parent_destroy_root.brush->put_Color({255, 96, 64, 32});
    PumpPostedMessages();
    HWND stale_window = nullptr;
    Check(parent_destroyed.native->get_WindowHandle(&stale_window) == E_UNEXPECTED &&
              parent_destroyed.Generation() == before_parent_destroy,
          "parent destruction retires HWND and ignores retained-root mutation");
    Check(SUCCEEDED(parent_destroyed.closable->Close()) &&
              SUCCEEDED(parent_destroyed.closable->Close()),
          "Close remains safe after parent-driven destruction");
    parent_destroyed.ReleaseWithoutClose();
    parent_destroy_root.Release();

    third.closable->Close();
    second.closable->Close();
    third.ReleaseWithoutClose();
    second.ReleaseWithoutClose();
    first.ReleaseWithoutClose();
    yellow.Release();
    green.Release();
    red.Release();
    if (parent) DestroyWindow(parent);

    RoUninitialize();
    if (skipped) {
        std::fprintf(stderr,
                     "%d pixel check(s) skipped by name: this loader does not "
                     "place a layered child where UpdateLayeredWindow was told "
                     "to. Everything else was enforced.\n", skipped);
    }
    if (!failures) std::puts("desktop island retained-renderer smoke passed");
    return failures ? 1 : 0;
}
