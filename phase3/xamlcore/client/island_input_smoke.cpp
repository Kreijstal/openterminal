#include <windows.h>

#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

#include "island_input_manager.h"

using openxaml::winrt::IslandCharacterEvent;
using openxaml::winrt::IslandInputManager;
using openxaml::winrt::IslandInputResult;
using openxaml::winrt::IslandInputSink;
using openxaml::winrt::IslandKeyEvent;
using openxaml::winrt::IslandPointerCapture;
using openxaml::winrt::IslandPointerEvent;
using openxaml::winrt::IslandPointerEventKind;
using openxaml::winrt::IslandPointerUpdateKind;

namespace {

int failures = 0;
constexpr wchar_t kParentClass[] = L"OpenXaml.IslandInputSmoke.Parent";
constexpr wchar_t kChildClass[] = L"OpenXaml.IslandInputSmoke.Child";

void Check(bool condition, const char* expression, int line) {
    if (condition) return;
    std::cerr << "island_input_smoke.cpp:" << line
              << ": CHECK failed: " << expression << "\n";
    ++failures;
}

#define CHECK(expression) Check(static_cast<bool>(expression), #expression, __LINE__)

struct TrackingSink : IslandInputSink {
    void OnIslandFocusChanged(bool focused) noexcept override {
        focus_changes.push_back(focused);
    }
    bool OnIslandKey(const IslandKeyEvent& event) noexcept override {
        keys.push_back(event);
        return handle_keys;
    }
    bool OnIslandCharacter(const IslandCharacterEvent& event) noexcept override {
        characters.push_back(event);
        return handle_characters;
    }
    bool OnIslandPointer(const IslandPointerEvent& event) noexcept override {
        pointers.push_back(event);
        return handle_pointers;
    }
    void OnIslandPointerCaptureLost(
        const IslandPointerCapture& capture) noexcept override {
        lost_captures.push_back(capture);
    }

    bool handle_keys = true;
    bool handle_characters = true;
    bool handle_pointers = true;
    std::vector<bool> focus_changes;
    std::vector<IslandKeyEvent> keys;
    std::vector<IslandCharacterEvent> characters;
    std::vector<IslandPointerEvent> pointers;
    std::vector<IslandPointerCapture> lost_captures;
};

struct ReentrantSink final : TrackingSink {
    void OnIslandFocusChanged(bool focused) noexcept override {
        TrackingSink::OnIslandFocusChanged(focused);
        if (focused && manager && replacement) manager->Attach(replacement);
    }

    IslandInputManager* manager = nullptr;
    std::shared_ptr<IslandInputSink> replacement;
};

struct ParentState {
    HWND child = nullptr;
};

LRESULT CALLBACK ParentProc(HWND window, UINT message,
                            WPARAM wparam, LPARAM lparam) {
    auto* state = reinterpret_cast<ParentState*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        state = static_cast<ParentState*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(state));
    }
    if (message == WM_SETFOCUS && state && state->child) {
        // This is the same parent-to-island handoff used by Terminal's
        // IslandWindow. User32 then sends WM_SETFOCUS to the child itself.
        SetFocus(state->child);
        return 0;
    }
    if (message == WM_NCDESTROY) SetWindowLongPtrW(window, GWLP_USERDATA, 0);
    return DefWindowProcW(window, message, wparam, lparam);
}

LRESULT CALLBACK ChildProc(HWND window, UINT message,
                           WPARAM wparam, LPARAM lparam) {
    auto* manager = reinterpret_cast<IslandInputManager*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        manager = static_cast<IslandInputManager*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(manager));
    }
    if (manager) {
        switch (message) {
            case WM_SETFOCUS:
                manager->OnHostFocusChanged(true);
                return 0;
            case WM_KILLFOCUS:
                manager->OnHostFocusChanged(false);
                return 0;
            case WM_KEYDOWN:
            case WM_KEYUP:
            case WM_SYSKEYDOWN:
            case WM_SYSKEYUP: {
                const IslandInputResult result =
                    manager->ForwardKeyMessage(message, wparam, lparam);
                if (result.recognized && result.handled) return 0;
                break;
            }
            case WM_CHAR:
            case WM_SYSCHAR:
            case WM_UNICHAR: {
                const IslandInputResult result =
                    manager->ForwardCharacterMessage(message, wparam, lparam);
                if (result.recognized && result.handled) return 0;
                break;
            }
            case WM_MOUSEMOVE:
            case WM_LBUTTONDOWN:
            case WM_LBUTTONUP:
            case WM_MOUSEWHEEL: {
                const IslandInputResult result = manager->ForwardPointerMessage(
                    window, message, wparam, lparam, 123456);
                if (result.recognized && result.handled) return 0;
                break;
            }
            case WM_CAPTURECHANGED:
                if (reinterpret_cast<HWND>(lparam) != window)
                    manager->OnHostPointerCaptureLost();
                break;
        }
    }
    if (message == WM_NCDESTROY) SetWindowLongPtrW(window, GWLP_USERDATA, 0);
    return DefWindowProcW(window, message, wparam, lparam);
}

ATOM Register(const wchar_t* name, WNDPROC procedure) {
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = procedure;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpszClassName = name;
    return RegisterClassExW(&window_class);
}

LPARAM KeyBits(std::uint16_t repeat, std::uint8_t scan_code,
               bool extended, bool menu, bool previous, bool released) {
    std::uint32_t bits = repeat | (static_cast<std::uint32_t>(scan_code) << 16);
    if (extended) bits |= 1u << 24;
    if (menu) bits |= 1u << 29;
    if (previous) bits |= 1u << 30;
    if (released) bits |= 1u << 31;
    return static_cast<LPARAM>(bits);
}

}  // namespace

int main() {
    const ATOM parent_class = Register(kParentClass, ParentProc);
    const ATOM child_class = Register(kChildClass, ChildProc);
    CHECK(parent_class != 0);
    CHECK(child_class != 0);
    if (!parent_class || !child_class) return 2;

    IslandInputManager manager;
    auto first = std::make_shared<TrackingSink>();
    CHECK(!manager.has_sink());
    CHECK(!manager.host_focused());
    CHECK(first->focus_changes.empty());

    ParentState parent_state;
    HWND parent = CreateWindowExW(0, kParentClass, L"", WS_POPUP | WS_VISIBLE,
                                  10, 10, 80, 60, nullptr, nullptr,
                                  GetModuleHandleW(nullptr), &parent_state);
    HWND other = CreateWindowExW(0, L"STATIC", L"", WS_POPUP | WS_VISIBLE,
                                 100, 10, 40, 40, nullptr, nullptr,
                                 GetModuleHandleW(nullptr), nullptr);
    HWND child = parent ? CreateWindowExW(
        0, kChildClass, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, 0, 80, 60, parent, nullptr, GetModuleHandleW(nullptr), &manager) : nullptr;
    parent_state.child = child;
    CHECK(parent != nullptr);
    CHECK(other != nullptr);
    CHECK(child != nullptr);

    if (parent && other && child) {
        manager.SetHostFocusRequester([child] {
            SetFocus(child);
            return GetFocus() == child;
        });
        manager.SetHostPointerCaptureCallbacks(
            [child] {
                SetCapture(child);
                return GetCapture() == child;
            },
            [child] {
                if (GetCapture() == child) ReleaseCapture();
            });
        SetFocus(other);
        manager.Attach(first);
        CHECK(manager.has_sink());
        CHECK(first->focus_changes.empty());
        SetFocus(parent);
        CHECK(GetFocus() == child);
        CHECK(manager.host_focused());
        CHECK(!first->focus_changes.empty());
        CHECK(first->focus_changes.back());

        // Duplicate focus notifications must not duplicate routed GotFocus.
        first->focus_changes.clear();
        manager.OnHostFocusChanged(true);
        CHECK(first->focus_changes.empty());

        const LPARAM down = KeyBits(2, 0x1e, false, false, false, false);
        SendMessageW(child, WM_KEYDOWN, 'A', down);
        CHECK(first->keys.size() == 1);
        if (!first->keys.empty()) {
            const IslandKeyEvent& key = first->keys[0];
            CHECK(key.message == WM_KEYDOWN);
            CHECK(key.virtual_key == 'A');
            CHECK(key.key_down);
            CHECK(!key.system_key);
            CHECK(key.status.repeat_count == 2);
            CHECK(key.status.scan_code == 0x1e);
            CHECK(!key.status.extended);
            CHECK(!key.status.menu_key_down);
            CHECK(!key.status.was_key_down);
            CHECK(!key.status.released);
        }

        SendMessageW(child, WM_CHAR, 'a', KeyBits(1, 0x1e, false, false, false, false));
        CHECK(first->characters.size() == 1);
        if (!first->characters.empty()) {
            CHECK(first->characters[0].character == 'a');
            CHECK(first->characters[0].message == WM_CHAR);
            CHECK(!first->characters[0].system_character);
            CHECK(first->characters[0].status.scan_code == 0x1e);
        }

        SendMessageW(child, WM_LBUTTONDOWN, MK_LBUTTON | MK_SHIFT | MK_CONTROL,
                     MAKELPARAM(7, 9));
        CHECK(first->pointers.size() == 1);
        if (!first->pointers.empty()) {
            const IslandPointerEvent& pointer = first->pointers.back();
            CHECK(pointer.kind == IslandPointerEventKind::Pressed);
            CHECK(pointer.update_kind ==
                  IslandPointerUpdateKind::LeftButtonPressed);
            CHECK(pointer.x == 7.0 && pointer.y == 9.0);
            CHECK(pointer.left_button && pointer.shift && pointer.control);
            CHECK(pointer.pointer_id == 1);
            CHECK(pointer.frame_id != 0);
            CHECK(pointer.timestamp == 123456);
        }
        CHECK(manager.CapturePointer(first, 1, 42));
        CHECK(GetCapture() == child);
        SendMessageW(child, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(200, 210));
        CHECK(first->pointers.size() == 2);
        if (first->pointers.size() == 2)
            CHECK(first->pointers.back().captured_node == 42);
        CHECK(manager.ReleasePointer(first, 1, 42));
        CHECK(GetCapture() != child);
        CHECK(first->lost_captures.size() == 1);
        if (!first->lost_captures.empty()) {
            CHECK(first->lost_captures[0].pointer_id == 1);
            CHECK(first->lost_captures[0].node_id == 42);
        }

        POINT wheel{11, 13};
        ClientToScreen(child, &wheel);
        SendMessageW(child, WM_MOUSEWHEEL,
                     MAKEWPARAM(MK_CONTROL, WHEEL_DELTA),
                     MAKELPARAM(wheel.x, wheel.y));
        CHECK(first->pointers.size() == 3);
        if (first->pointers.size() == 3) {
            const IslandPointerEvent& pointer = first->pointers.back();
            CHECK(pointer.kind == IslandPointerEventKind::Wheel);
            CHECK(pointer.x == 11.0 && pointer.y == 13.0);
            CHECK(pointer.wheel_delta == WHEEL_DELTA);
            CHECK(!pointer.horizontal_wheel);
        }

        const LPARAM up = KeyBits(1, 0x1e, false, false, true, true);
        SendMessageW(child, WM_KEYUP, 'A', up);
        CHECK(first->keys.size() == 2);
        if (first->keys.size() == 2) {
            CHECK(!first->keys[1].key_down);
            CHECK(first->keys[1].status.was_key_down);
            CHECK(first->keys[1].status.released);
        }

        CHECK(!manager.ForwardKeyMessage(WM_MOUSEMOVE, 0, 0).recognized);
        const IslandInputResult unicode_query = manager.ForwardCharacterMessage(
            WM_UNICHAR, UNICODE_NOCHAR, 0);
        CHECK(unicode_query.recognized && unicode_query.handled);
        const IslandInputResult invalid_unicode = manager.ForwardCharacterMessage(
            WM_UNICHAR, 0x110000, 0);
        CHECK(invalid_unicode.recognized && !invalid_unicode.handled);

        auto second = std::make_shared<TrackingSink>();
        manager.Attach(second);
        CHECK(first->focus_changes == std::vector<bool>{false});
        CHECK(second->focus_changes == std::vector<bool>{true});
        CHECK(!manager.Detach(first));
        SendMessageW(child, WM_SYSKEYDOWN, VK_MENU,
                     KeyBits(1, 0x38, true, true, false, false));
        CHECK(second->keys.size() == 1);
        if (!second->keys.empty()) {
            CHECK(second->keys[0].system_key);
            CHECK(second->keys[0].status.extended);
            CHECK(second->keys[0].status.menu_key_down);
        }

        SetFocus(other);
        CHECK(!manager.host_focused());
        CHECK(second->focus_changes == std::vector<bool>({true, false}));
        SendMessageW(child, WM_KEYDOWN, 'B', KeyBits(1, 0x30, false, false, false, false));
        CHECK(second->keys.size() == 1);

        auto third = std::make_shared<TrackingSink>();
        manager.Attach(third);
        CHECK(third->focus_changes.empty());
        CHECK(manager.RequestHostFocus());
        CHECK(GetFocus() == child);
        CHECK(third->focus_changes == std::vector<bool>{true});

        auto final_sink = std::make_shared<TrackingSink>();
        auto reentrant = std::make_shared<ReentrantSink>();
        reentrant->manager = &manager;
        reentrant->replacement = final_sink;
        manager.Attach(reentrant);
        CHECK(third->focus_changes == std::vector<bool>({true, false}));
        CHECK(reentrant->focus_changes == std::vector<bool>({true, false}));
        CHECK(final_sink->focus_changes == std::vector<bool>{true});
        SendMessageW(child, WM_KEYDOWN, 'C', KeyBits(1, 0x2e, false, false, false, false));
        CHECK(reentrant->keys.empty());
        CHECK(final_sink->keys.size() == 1);
        CHECK(manager.Detach(final_sink));
        CHECK(final_sink->focus_changes == std::vector<bool>({true, false}));
        CHECK(!manager.has_sink());
        manager.ClearHostFocusRequester();
        manager.ClearHostPointerCaptureCallbacks();
        CHECK(!manager.RequestHostFocus());
    }

    if (child) DestroyWindow(child);
    if (other) DestroyWindow(other);
    if (parent) DestroyWindow(parent);
    UnregisterClassW(kChildClass, GetModuleHandleW(nullptr));
    UnregisterClassW(kParentClass, GetModuleHandleW(nullptr));

    if (failures == 0) {
        std::cout << "island input manager checks passed\n";
        return 0;
    }
    std::cerr << failures << " island input manager check(s) failed\n";
    return 1;
}
