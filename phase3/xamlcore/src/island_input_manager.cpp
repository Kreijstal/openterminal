#include "island_input_manager.h"

#include <windowsx.h>

#include <utility>

namespace openxaml::winrt {

namespace {

bool IsKeyMessage(UINT message) noexcept {
    return message == WM_KEYDOWN || message == WM_KEYUP ||
           message == WM_SYSKEYDOWN || message == WM_SYSKEYUP;
}

bool IsCharacterMessage(UINT message) noexcept {
    return message == WM_CHAR || message == WM_SYSCHAR || message == WM_UNICHAR;
}

bool IsPointerMessage(UINT message) noexcept {
    switch (message) {
        case WM_MOUSEMOVE:
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        case WM_MBUTTONDBLCLK:
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP:
        case WM_XBUTTONDBLCLK:
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
            return true;
        default:
            return false;
    }
}

IslandPointerUpdateKind PointerUpdateKind(UINT message, WPARAM wparam) noexcept {
    switch (message) {
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
            return IslandPointerUpdateKind::LeftButtonPressed;
        case WM_LBUTTONUP:
            return IslandPointerUpdateKind::LeftButtonReleased;
        case WM_RBUTTONDOWN:
        case WM_RBUTTONDBLCLK:
            return IslandPointerUpdateKind::RightButtonPressed;
        case WM_RBUTTONUP:
            return IslandPointerUpdateKind::RightButtonReleased;
        case WM_MBUTTONDOWN:
        case WM_MBUTTONDBLCLK:
            return IslandPointerUpdateKind::MiddleButtonPressed;
        case WM_MBUTTONUP:
            return IslandPointerUpdateKind::MiddleButtonReleased;
        case WM_XBUTTONDOWN:
        case WM_XBUTTONDBLCLK:
            return HIWORD(wparam) == XBUTTON2
                ? IslandPointerUpdateKind::XButton2Pressed
                : IslandPointerUpdateKind::XButton1Pressed;
        case WM_XBUTTONUP:
            return HIWORD(wparam) == XBUTTON2
                ? IslandPointerUpdateKind::XButton2Released
                : IslandPointerUpdateKind::XButton1Released;
        default:
            return IslandPointerUpdateKind::Other;
    }
}

IslandPointerEventKind PointerEventKind(UINT message) noexcept {
    switch (message) {
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
            return IslandPointerEventKind::Wheel;
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP:
        case WM_XBUTTONUP:
            return IslandPointerEventKind::Released;
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONDBLCLK:
        case WM_XBUTTONDOWN:
        case WM_XBUTTONDBLCLK:
            return IslandPointerEventKind::Pressed;
        default:
            return IslandPointerEventKind::Moved;
    }
}

bool IsValidUnicodeScalar(std::uint32_t value) noexcept {
    return value <= 0x10ffff && !(value >= 0xd800 && value <= 0xdfff);
}

}  // namespace

void IslandInputManager::Attach(std::shared_ptr<IslandInputSink> sink) noexcept {
    if (sink_ == sink) return;
    ClearPointerCapture(true);
    sink_ = std::move(sink);
    ReconcileFocus();
}

bool IslandInputManager::Detach(
    const std::shared_ptr<IslandInputSink>& expected) noexcept {
    if (!expected || sink_ != expected) return false;
    ClearPointerCapture(true);
    sink_.reset();
    ReconcileFocus();
    return true;
}

void IslandInputManager::SetHostFocusRequester(
    std::function<bool()> requester) {
    host_focus_requester_ = std::move(requester);
}

void IslandInputManager::ClearHostFocusRequester() noexcept {
    host_focus_requester_ = {};
}

void IslandInputManager::SetHostPointerCaptureCallbacks(
    std::function<bool()> capture, std::function<void()> release) {
    host_pointer_capture_requester_ = std::move(capture);
    host_pointer_capture_releaser_ = std::move(release);
}

void IslandInputManager::ClearHostPointerCaptureCallbacks() noexcept {
    ClearPointerCapture(true);
    host_pointer_capture_requester_ = {};
    host_pointer_capture_releaser_ = {};
}

bool IslandInputManager::RequestHostFocus() noexcept {
    try {
        // A SetFocus callback can synchronously re-enter and clear the host
        // binding through WM_KILLFOCUS/Close. Keep its callable alive until
        // that stack has unwound.
        const std::function<bool()> requester = host_focus_requester_;
        return requester && requester();
    } catch (...) {
        return false;
    }
}

void IslandInputManager::OnHostFocusChanged(bool focused) noexcept {
    if (host_focused_ == focused) return;
    host_focused_ = focused;
    ReconcileFocus();
}

IslandInputResult IslandInputManager::ForwardKeyMessage(
    UINT message, WPARAM wparam, LPARAM lparam) noexcept {
    if (!IsKeyMessage(message)) return {};

    const std::shared_ptr<IslandInputSink> target = notified_sink_;
    if (!host_focused_ || !target) return {true, false};

    IslandKeyEvent event;
    event.message = message;
    event.virtual_key = static_cast<std::uint32_t>(wparam);
    event.key_down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
    event.system_key = message == WM_SYSKEYDOWN || message == WM_SYSKEYUP;
    event.status = DecodeStatus(lparam);
    return {true, target->OnIslandKey(event)};
}

IslandInputResult IslandInputManager::ForwardCharacterMessage(
    UINT message, WPARAM wparam, LPARAM lparam) noexcept {
    if (!IsCharacterMessage(message)) return {};

    // Per the WM_UNICHAR contract, UNICODE_NOCHAR asks whether the window
    // supports this message. It is a query, not a routed character event.
    if (message == WM_UNICHAR && wparam == UNICODE_NOCHAR) return {true, true};
    const std::uint32_t character = static_cast<std::uint32_t>(wparam);
    if (message == WM_UNICHAR && !IsValidUnicodeScalar(character)) {
        return {true, false};
    }

    const std::shared_ptr<IslandInputSink> target = notified_sink_;
    if (!host_focused_ || !target) return {true, false};

    IslandCharacterEvent event;
    event.message = message;
    event.character = character;
    event.system_character = message == WM_SYSCHAR;
    event.status = DecodeStatus(lparam);
    return {true, target->OnIslandCharacter(event)};
}

IslandInputResult IslandInputManager::ForwardPointerMessage(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam,
    std::uint64_t timestamp) noexcept {
    if (!IsPointerMessage(message)) return {};

    POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
    if (message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL) {
        // Wheel lParam is screen-relative; every other mouse message reaching
        // a child window is already in that child's client coordinates.
        if (!window || !ScreenToClient(window, &point)) return {true, false};
    }

    const std::shared_ptr<IslandInputSink> target = sink_;
    if (!target) return {true, false};

    IslandPointerEvent event;
    event.message = message;
    event.kind = PointerEventKind(message);
    event.update_kind = PointerUpdateKind(message, wparam);
    event.frame_id = ++pointer_frame_id_;
    if (!event.frame_id) event.frame_id = ++pointer_frame_id_;
    if (!timestamp) {
        timestamp = static_cast<std::uint64_t>(
            static_cast<std::uint32_t>(GetMessageTime())) * 1000u;
    }
    event.timestamp = timestamp;
    event.x = static_cast<double>(point.x);
    event.y = static_cast<double>(point.y);

    const UINT keys = LOWORD(wparam);
    event.left_button = (keys & MK_LBUTTON) != 0;
    event.right_button = (keys & MK_RBUTTON) != 0;
    event.middle_button = (keys & MK_MBUTTON) != 0;
    event.xbutton1 = (keys & MK_XBUTTON1) != 0;
    event.xbutton2 = (keys & MK_XBUTTON2) != 0;
    event.shift = (keys & MK_SHIFT) != 0;
    event.control = (keys & MK_CONTROL) != 0;
    event.menu = (GetKeyState(VK_MENU) & 0x8000) != 0;
    event.windows = (GetKeyState(VK_LWIN) & 0x8000) != 0 ||
                    (GetKeyState(VK_RWIN) & 0x8000) != 0;
    if (event.kind == IslandPointerEventKind::Wheel) {
        event.wheel_delta = GET_WHEEL_DELTA_WPARAM(wparam);
        event.horizontal_wheel = message == WM_MOUSEHWHEEL;
    }

    if (pointer_capture_) {
        const std::shared_ptr<IslandInputSink> capture_sink =
            pointer_capture_sink_.lock();
        if (capture_sink == target) {
            event.captured_node = pointer_capture_->node_id;
        } else {
            ClearPointerCapture(true);
        }
    }
    last_pointer_event_ = event;
    has_last_pointer_event_ = true;
    return {true, target->OnIslandPointer(event)};
}

bool IslandInputManager::CapturePointer(
    const std::shared_ptr<IslandInputSink>& expected,
    std::uint32_t pointer_id, std::uint64_t node_id) noexcept {
    if (!expected || sink_ != expected || !pointer_id || !node_id) return false;
    if (pointer_capture_ && pointer_capture_->pointer_id == pointer_id &&
        pointer_capture_->node_id == node_id &&
        pointer_capture_sink_.lock() == expected) return true;

    ClearPointerCapture(true);
    std::function<bool()> requester = host_pointer_capture_requester_;
    bool captured = true;
    try {
        if (requester) captured = requester();
    } catch (...) {
        captured = false;
    }
    if (!captured || sink_ != expected) {
        if (captured) {
            try {
                const std::function<void()> releaser =
                    host_pointer_capture_releaser_;
                if (releaser) releaser();
            } catch (...) {
            }
        }
        return false;
    }
    pointer_capture_ = IslandPointerCapture{pointer_id, node_id};
    pointer_capture_sink_ = expected;
    return true;
}

bool IslandInputManager::ReleasePointer(
    const std::shared_ptr<IslandInputSink>& expected,
    std::uint32_t pointer_id, std::uint64_t node_id) noexcept {
    if (!expected || sink_ != expected || !pointer_capture_ ||
        pointer_capture_sink_.lock() != expected ||
        pointer_capture_->pointer_id != pointer_id ||
        pointer_capture_->node_id != node_id) return false;
    ClearPointerCapture(true);
    return true;
}

void IslandInputManager::OnHostPointerCaptureLost() noexcept {
    ClearPointerCapture(false);
}

void IslandInputManager::OnHostPointerCanceled() noexcept {
    const std::shared_ptr<IslandInputSink> target = sink_;
    if (target && has_last_pointer_event_) {
        IslandPointerEvent canceled = last_pointer_event_;
        canceled.kind = IslandPointerEventKind::Canceled;
        canceled.update_kind = IslandPointerUpdateKind::Other;
        if (pointer_capture_) canceled.captured_node = pointer_capture_->node_id;
        (void)target->OnIslandPointer(canceled);
    }
    ClearPointerCapture(true);
}

void IslandInputManager::ClearPointerCapture(bool release_host) noexcept {
    if (!pointer_capture_) return;
    const IslandPointerCapture lost = *pointer_capture_;
    const std::shared_ptr<IslandInputSink> owner = pointer_capture_sink_.lock();
    pointer_capture_.reset();
    pointer_capture_sink_.reset();

    if (release_host) {
        try {
            const std::function<void()> releaser = host_pointer_capture_releaser_;
            if (releaser) releaser();
        } catch (...) {
        }
    }
    if (owner) owner->OnIslandPointerCaptureLost(lost);
}

IslandPhysicalKeyStatus IslandInputManager::DecodeStatus(LPARAM lparam) noexcept {
    const std::uint32_t bits = static_cast<std::uint32_t>(lparam);
    IslandPhysicalKeyStatus status;
    status.repeat_count = bits & 0xffff;
    status.scan_code = (bits >> 16) & 0xff;
    status.extended = (bits & (1u << 24)) != 0;
    status.menu_key_down = (bits & (1u << 29)) != 0;
    status.was_key_down = (bits & (1u << 30)) != 0;
    status.released = (bits & (1u << 31)) != 0;
    return status;
}

void IslandInputManager::ReconcileFocus() noexcept {
    if (reconciling_focus_) {
        focus_reconcile_pending_ = true;
        return;
    }

    reconciling_focus_ = true;
    do {
        focus_reconcile_pending_ = false;
        const std::shared_ptr<IslandInputSink> wanted =
            host_focused_ ? sink_ : std::shared_ptr<IslandInputSink>{};
        if (notified_sink_ == wanted) continue;

        if (notified_sink_) {
            std::shared_ptr<IslandInputSink> losing = std::move(notified_sink_);
            losing->OnIslandFocusChanged(false);
            // The callback may have installed and focused another sink.
            focus_reconcile_pending_ = true;
            continue;
        }

        if (wanted) {
            // Set this before invoking so a reentrant key message observes the
            // sink which is currently receiving its focus notification.
            notified_sink_ = wanted;
            wanted->OnIslandFocusChanged(true);
            focus_reconcile_pending_ = true;
        }
    } while (focus_reconcile_pending_);
    reconciling_focus_ = false;
}

}  // namespace openxaml::winrt
