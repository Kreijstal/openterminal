// The thread's CoreWindow answers for the thread's keyboard.
//
// Windows Terminal reads the modifier keys through
// CoreWindow::GetForCurrentThread() on every keystroke, in a noexcept method
// that does not check the window for null, because on Windows a XAML island
// thread always has one. This asks the three questions that method depends on:
// that the class resolves to a window at all, that the window is the thread's
// own rather than a fresh one per call, and that what it reports about a key is
// what the thread's keyboard says about that key.
//
// A stub that answers "success, here is nothing" fails at the first of them,
// which is what the failing address inside TermControl::_GetPressedModifierKeys
// was.

#include <roapi.h>
#include <windows.h>

#include <cstdio>

#include <inspectable.h>
#include <winstring.h>

// Windows.UI.Core.ICoreWindowStatic and the two methods of ICoreWindow this
// depends on. Declared by hand rather than included, so this test binds to the
// vtable slots the SDK documents rather than to the runtime's own headers --
// a test that shares its subject's mistakes cannot find them.
inline constexpr GUID IID_ICoreWindowStatic = {
    0x4d239005, 0x3c2a, 0x41b1,
    {0x90, 0x22, 0x53, 0x6b, 0xb9, 0xcf, 0x93, 0xb1}};

enum CoreVirtualKeyStates {
    CoreVirtualKeyStates_None = 0,
    CoreVirtualKeyStates_Down = 1,
    CoreVirtualKeyStates_Locked = 2,
};

struct ICoreWindowMinimal : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_AutomationHostProvider(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Bounds(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_CustomProperties(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Dispatcher(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_FlowDirection(int*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_FlowDirection(int) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsInputEnabled(boolean*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsInputEnabled(boolean) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_PointerCursor(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_PointerCursor(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_PointerPosition(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Visible(boolean*) = 0;
    virtual HRESULT STDMETHODCALLTYPE Activate() = 0;
    virtual HRESULT STDMETHODCALLTYPE Close() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetAsyncKeyState(int, CoreVirtualKeyStates*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetKeyState(int, CoreVirtualKeyStates*) = 0;
};

struct ICoreWindowStaticMinimal : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE GetForCurrentThread(ICoreWindowMinimal**) = 0;
};

namespace {

int failures = 0;

// Always evaluated, and always reported: a check that only speaks when it
// fails cannot be told apart from one that never ran.
void Check(bool condition, const char* what, const char* detail) {
    std::printf("core-window %s: %s (%s)\n", condition ? "ok" : "FAILED", what,
                detail);
    if (!condition) ++failures;
}

}  // namespace

int main() {
    const HRESULT initialized = RoInitialize(RO_INIT_SINGLETHREADED);
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) {
        std::fprintf(stderr, "RoInitialize failed: 0x%08lx\n",
                     static_cast<unsigned long>(initialized));
        return 1;
    }

    HSTRING_HEADER header{};
    HSTRING name = nullptr;
    static constexpr wchar_t class_name[] = L"Windows.UI.Core.CoreWindow";
    if (FAILED(WindowsCreateStringReference(class_name,
                                            ARRAYSIZE(class_name) - 1, &header,
                                            &name)))
        return 2;

    ICoreWindowStaticMinimal* statics = nullptr;
    const HRESULT activation = RoGetActivationFactory(
        name, IID_ICoreWindowStatic, reinterpret_cast<void**>(&statics));
    if (FAILED(activation) || !statics) {
        std::fprintf(stderr, "CoreWindow statics unavailable: 0x%08lx\n",
                     static_cast<unsigned long>(activation));
        return 3;
    }

    ICoreWindowMinimal* window = nullptr;
    const HRESULT got = statics->GetForCurrentThread(&window);
    char detail[128]{};
    std::snprintf(detail, sizeof(detail), "hr=0x%08lx window=%p",
                  static_cast<unsigned long>(got),
                  static_cast<void*>(window));
    // The whole point. A caller that is handed nothing here faults on its next
    // line, because there is nothing in the contract that says it might be.
    Check(SUCCEEDED(got) && window != nullptr,
          "the thread has a CoreWindow", detail);
    if (!window) {
        statics->Release();
        return 4;
    }

    ICoreWindowMinimal* again = nullptr;
    const HRESULT twice = statics->GetForCurrentThread(&again);
    std::snprintf(detail, sizeof(detail), "first=%p second=%p",
                  static_cast<void*>(window), static_cast<void*>(again));
    Check(SUCCEEDED(twice) && again == window,
          "the same thread gets the same window", detail);
    if (again) again->Release();

    // What the thread's keyboard says is what the window must report. The key
    // state is set here rather than typed, so this measures the reporting and
    // not the desktop's input queue.
    BYTE keyboard[256]{};
    Check(GetKeyboardState(keyboard) != FALSE, "the thread has a key state",
          "GetKeyboardState");
    keyboard[VK_SHIFT] = 0x80;
    keyboard[VK_CONTROL] = 0x00;
    keyboard[VK_CAPITAL] = 0x01;
    SetKeyboardState(keyboard);

    CoreVirtualKeyStates state = CoreVirtualKeyStates_None;
    HRESULT hr = window->GetKeyState(VK_SHIFT, &state);
    std::snprintf(detail, sizeof(detail), "hr=0x%08lx state=%d",
                  static_cast<unsigned long>(hr), static_cast<int>(state));
    Check(SUCCEEDED(hr) && (state & CoreVirtualKeyStates_Down) != 0,
          "a held key reads as Down", detail);

    state = CoreVirtualKeyStates_None;
    hr = window->GetKeyState(VK_CONTROL, &state);
    std::snprintf(detail, sizeof(detail), "hr=0x%08lx state=%d",
                  static_cast<unsigned long>(hr), static_cast<int>(state));
    Check(SUCCEEDED(hr) && (state & CoreVirtualKeyStates_Down) == 0,
          "a key nobody is holding does not read as Down", detail);

    state = CoreVirtualKeyStates_None;
    hr = window->GetKeyState(VK_CAPITAL, &state);
    std::snprintf(detail, sizeof(detail), "hr=0x%08lx state=%d",
                  static_cast<unsigned long>(hr), static_cast<int>(state));
    // Locked is a separate bit from Down on purpose: a toggled key that is not
    // held sets one and not the other, and a caller comparing for equality
    // with Down would be wrong about both.
    Check(SUCCEEDED(hr) && (state & CoreVirtualKeyStates_Locked) != 0,
          "a toggled key reads as Locked", detail);

    window->Release();
    statics->Release();
    std::printf("core-window failures=%d\n", failures);
    return failures == 0 ? 0 : 10;
}
