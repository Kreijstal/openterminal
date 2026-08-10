// The host-facing input boundary for one DesktopWindowXamlSource island.
//
// This layer knows Win32 message encoding, but deliberately knows no XAML
// control classes. A projected visual tree supplies one lifetime-safe sink;
// the child HWND reports focus changes and forwards messages here. Keeping
// that boundary explicit lets the focus manager evolve without teaching the
// window procedure about individual controls or routed-event storage.

#ifndef OPENXAML_XAMLCORE_ISLAND_INPUT_MANAGER_H
#define OPENXAML_XAMLCORE_ISLAND_INPUT_MANAGER_H

#include <windows.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <optional>

namespace openxaml::winrt {

// The opt-in trace of the input path, from the host window's message to the
// routed event that carries it. A keystroke that never arrives, one that
// arrives at an unfocused island, and one that is routed to nothing are three
// different failures with the same symptom -- nothing happens -- and only a
// record written at each of those points can tell them apart.
//
// The environment is read once: this sits on the message path, and a
// GetEnvironmentVariable per keystroke would be a cost paid by every run.
inline bool IslandInputTraceEnabled() noexcept {
    static const bool enabled =
        GetEnvironmentVariableW(L"OPENXAML_TRACE_INPUT", nullptr, 0) != 0;
    return enabled;
}

inline void TraceIslandInput(const char* format, ...) noexcept {
    if (!IslandInputTraceEnabled()) return;
    char line[320]{};
    std::va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(line, sizeof(line), format, arguments);
    va_end(arguments);
    OutputDebugStringA(line);
}

struct IslandPhysicalKeyStatus {
    std::uint32_t repeat_count = 0;
    std::uint32_t scan_code = 0;
    bool extended = false;
    bool menu_key_down = false;
    bool was_key_down = false;
    bool released = false;
};

struct IslandKeyEvent {
    UINT message = 0;
    std::uint32_t virtual_key = 0;
    bool key_down = false;
    bool system_key = false;
    IslandPhysicalKeyStatus status;
};

struct IslandCharacterEvent {
    UINT message = 0;
    // WM_CHAR/WM_SYSCHAR carry one UTF-16 code unit. WM_UNICHAR carries one
    // Unicode scalar. The sink decides how to combine surrogate pairs because
    // routed CharacterReceived semantics belong above this Win32 boundary.
    std::uint32_t character = 0;
    bool system_character = false;
    IslandPhysicalKeyStatus status;
};

enum class IslandPointerEventKind {
    Moved,
    Pressed,
    Released,
    Wheel,
    Entered,
    Exited,
    Canceled,
    CaptureLost,
};

// Values deliberately match Windows.UI.Input.PointerUpdateKind. Keeping the
// host boundary independent of WinRT headers still lets the projection copy
// the value without a lossy second interpretation.
enum class IslandPointerUpdateKind : std::int32_t {
    Other = 0,
    LeftButtonPressed = 1,
    LeftButtonReleased = 2,
    RightButtonPressed = 3,
    RightButtonReleased = 4,
    MiddleButtonPressed = 5,
    MiddleButtonReleased = 6,
    XButton1Pressed = 7,
    XButton1Released = 8,
    XButton2Pressed = 9,
    XButton2Released = 10,
};

struct IslandPointerEvent {
    UINT message = 0;
    IslandPointerEventKind kind = IslandPointerEventKind::Moved;
    IslandPointerUpdateKind update_kind = IslandPointerUpdateKind::Other;
    std::uint32_t pointer_id = 1;  // Win32 mouse is the island's primary pointer.
    std::uint32_t frame_id = 0;
    std::uint64_t timestamp = 0;  // Microseconds, matching PointerPoint.
    double x = 0.0;               // Child-client DIPs.
    double y = 0.0;
    std::int32_t wheel_delta = 0;
    bool horizontal_wheel = false;
    bool left_button = false;
    bool right_button = false;
    bool middle_button = false;
    bool xbutton1 = false;
    bool xbutton2 = false;
    bool shift = false;
    bool control = false;
    bool menu = false;
    bool windows = false;
    // Zero means hit-test normally. A nonzero value is the stable retained
    // node identity captured for this pointer in this island.
    std::uint64_t captured_node = 0;
};

struct IslandPointerCapture {
    std::uint32_t pointer_id = 0;
    std::uint64_t node_id = 0;
};

struct IslandInputResult {
    // False means the caller passed a message outside this API's boundary.
    bool recognized = false;
    // A recognized event may remain unhandled and fall through to DefWindowProc.
    bool handled = false;
};

class IslandInputSink {
public:
    virtual ~IslandInputSink() = default;

    // Sink methods must contain projection/COM failures. HWND dispatch cannot
    // allow an exception to cross the window-procedure boundary.
    virtual void OnIslandFocusChanged(bool focused) noexcept = 0;
    virtual bool OnIslandKey(const IslandKeyEvent& event) noexcept = 0;
    virtual bool OnIslandCharacter(const IslandCharacterEvent& event) noexcept = 0;
    // Defaults preserve source compatibility while pointer projection is
    // integrated one layer at a time. A sink which does not opt in cannot
    // accidentally report the message handled.
    virtual bool OnIslandPointer(const IslandPointerEvent&) noexcept {
        return false;
    }
    virtual void OnIslandPointerCaptureLost(
        const IslandPointerCapture&) noexcept {}
};

class IslandInputManager {
public:
    IslandInputManager() = default;
    ~IslandInputManager() = default;

    IslandInputManager(const IslandInputManager&) = delete;
    IslandInputManager& operator=(const IslandInputManager&) = delete;

    // Replacing a sink while the HWND is focused sends a balanced loss to the
    // old sink and gain to the new one. Callback re-entry is reconciled before
    // this method returns.
    void Attach(std::shared_ptr<IslandInputSink> sink) noexcept;

    // Identity-checked detach prevents an old content transaction from
    // disconnecting a replacement root installed by a reentrant callback.
    bool Detach(const std::shared_ptr<IslandInputSink>& expected) noexcept;

    // Reciprocal element-to-host seam used by IControl::Focus. The callback
    // must capture only lifetime-safe host state (normally a weak HostState),
    // validate the current HWND binding, and call SetFocus on that child. A
    // projected element can hold a weak_ptr to the manager and request focus
    // without retaining the DesktopWindowXamlSource.
    void SetHostFocusRequester(std::function<bool()> requester);
    void ClearHostFocusRequester() noexcept;
    bool RequestHostFocus() noexcept;

    // Called for WM_SETFOCUS/WM_KILLFOCUS after User32 has made the transition.
    // Duplicate notifications are idempotent.
    void OnHostFocusChanged(bool focused) noexcept;

    IslandInputResult ForwardKeyMessage(UINT message, WPARAM wparam,
                                        LPARAM lparam) noexcept;
    IslandInputResult ForwardCharacterMessage(UINT message, WPARAM wparam,
                                              LPARAM lparam) noexcept;
    IslandInputResult ForwardPointerMessage(
        HWND window, UINT message, WPARAM wparam, LPARAM lparam,
        std::uint64_t timestamp = 0) noexcept;

    // Logical XAML capture and User32 capture are committed transactionally.
    // The expected sink identity prevents a detached root from capturing for
    // its replacement. The host callbacks must capture only lifetime-safe
    // window state and validate the current HWND/cookie before SetCapture.
    void SetHostPointerCaptureCallbacks(
        std::function<bool()> capture, std::function<void()> release);
    void ClearHostPointerCaptureCallbacks() noexcept;
    bool CapturePointer(const std::shared_ptr<IslandInputSink>& expected,
                        std::uint32_t pointer_id,
                        std::uint64_t node_id) noexcept;
    bool ReleasePointer(const std::shared_ptr<IslandInputSink>& expected,
                        std::uint32_t pointer_id,
                        std::uint64_t node_id) noexcept;
    void OnHostPointerCaptureLost() noexcept;
    void OnHostPointerCanceled() noexcept;
    std::optional<IslandPointerCapture> pointer_capture() const noexcept {
        return pointer_capture_;
    }

    bool host_focused() const noexcept { return host_focused_; }
    bool has_sink() const noexcept { return static_cast<bool>(sink_); }

private:
    static IslandPhysicalKeyStatus DecodeStatus(LPARAM lparam) noexcept;
    void ClearPointerCapture(bool release_host) noexcept;
    void ReconcileFocus() noexcept;

    std::shared_ptr<IslandInputSink> sink_;
    // The sink which has actually received focus=true. Tracking this
    // separately makes Attach/Detach balanced even when callbacks re-enter.
    std::shared_ptr<IslandInputSink> notified_sink_;
    std::function<bool()> host_focus_requester_;
    std::function<bool()> host_pointer_capture_requester_;
    std::function<void()> host_pointer_capture_releaser_;
    std::optional<IslandPointerCapture> pointer_capture_;
    std::weak_ptr<IslandInputSink> pointer_capture_sink_;
    std::uint32_t pointer_frame_id_ = 0;
    IslandPointerEvent last_pointer_event_;
    bool has_last_pointer_event_ = false;
    bool host_focused_ = false;
    bool reconciling_focus_ = false;
    bool focus_reconcile_pending_ = false;
};

}  // namespace openxaml::winrt

#endif  // OPENXAML_XAMLCORE_ISLAND_INPUT_MANAGER_H
