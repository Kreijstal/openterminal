// Types into a live hosting window from outside it, and says exactly what it
// did.
//
// This is the input half of the phase-4 input round-trip gate, and the sibling
// of terminal_pixel_probe.cpp: it links nothing of this runtime on purpose. It
// finds a top-level window by class name, reports the window tree and the
// focus state it found there, injects a sequence of keystrokes by one named
// mechanism, and reports what each injection returned. Nothing about the
// terminal, the shell or the pseudoconsole is known to it -- whether the
// keystrokes had an effect is answered elsewhere, by the effect itself.
//
// Two mechanisms are offered because it is not obvious in advance which one a
// Wine/Xvfb desktop actually delivers to a XAML island's child window:
//
//   --mechanism sendinput    SetForegroundWindow, then SendInput with
//                            KEYEVENTF_UNICODE (and real virtual keys for the
//                            keys that have no character). This is the whole
//                            path: the desktop's input queue, the loader's
//                            keyboard state, the focus window, the message
//                            loop of the process being typed into.
//   --mechanism postmessage  PostMessage WM_KEYDOWN/WM_CHAR/WM_KEYUP straight
//                            to the XAML island's own child window (falling
//                            back to the focus window, then the host). This
//                            skips the desktop queue and the loader's keyboard
//                            state, so it measures a strictly shorter path.
//   --mechanism none         Do everything except inject. This is the negative
//                            control: a gate that passes with this cannot be
//                            measuring the input path at all.
//
// The report is line-oriented and prefixed with "input ", so a reader can tell
// what this process observed apart from anything the observed process wrote.

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

namespace {

enum class Mechanism { SendInput, PostMessage, None };

struct Options {
    std::wstring window_class = L"CASCADIA_HOSTING_WINDOW_CLASS";
    std::wstring text;
    std::wstring wait_file;  // typing starts once this exists
    unsigned deadline_ms = 30000;
    unsigned visible_grace_ms = 10000;
    unsigned settle_ms = 8000;
    unsigned interval_ms = 100;
    unsigned key_delay_ms = 40;
    unsigned columns = 9;
    unsigned rows = 7;
    bool press_enter = false;
    Mechanism mechanism = Mechanism::SendInput;
};

// Enough that any visible window of the class outranks any invisible one,
// whatever their client areas are.
constexpr long kVisibleBonus = 1 << 28;

struct Search {
    const wchar_t* window_class = nullptr;
    HWND found = nullptr;
    long found_area = 0;
    bool found_visible = false;
    std::vector<std::wstring>* seen = nullptr;
    std::vector<std::wstring>* candidates = nullptr;
};

BOOL CALLBACK CollectWindow(HWND window, LPARAM parameter) {
    auto* search = reinterpret_cast<Search*>(parameter);
    wchar_t class_name[256]{};
    if (!GetClassNameW(window, class_name, 255)) return TRUE;
    if (search->seen) {
        const std::wstring name(class_name);
        bool known = false;
        for (const std::wstring& previous : *search->seen)
            known = known || previous == name;
        if (!known && search->seen->size() < 48) search->seen->push_back(name);
    }
    if (std::wcscmp(class_name, search->window_class) != 0) return TRUE;
    RECT client{};
    const bool visible = IsWindowVisible(window) != FALSE;
    const bool measured = GetClientRect(window, &client) != FALSE;
    if (search->candidates && search->candidates->size() < 16) {
        wchar_t description[192]{};
        // The style bits matter: a window that exists, has a real client area
        // and never gains WS_VISIBLE was never shown, which is a different
        // failure from one that was shown somewhere unreadable.
        std::swprintf(description, 191,
                      L"hwnd=%p visible=%d client=%ldx%ld style=%08lx "
                      L"exstyle=%08lx",
                      static_cast<void*>(window), visible ? 1 : 0,
                      measured ? client.right - client.left : -1,
                      measured ? client.bottom - client.top : -1,
                      static_cast<unsigned long>(GetWindowLongW(window, GWL_STYLE)),
                      static_cast<unsigned long>(
                          GetWindowLongW(window, GWL_EXSTYLE)));
        const std::wstring text(description);
        bool known = false;
        for (const std::wstring& previous : *search->candidates)
            known = known || previous == text;
        if (!known) search->candidates->push_back(text);
    }
    if (!measured) return TRUE;
    // A window that exists but was never shown is still the window this
    // process must type into, and whether it is visible is a finding rather
    // than a reason to report nothing. Prefer a visible one when there is a
    // choice, so this never picks a hidden sibling over the real one.
    const long area = static_cast<long>(client.right - client.left) *
                      static_cast<long>(client.bottom - client.top);
    const long score = visible ? area + kVisibleBonus : area;
    if (score > search->found_area) {
        search->found = window;
        search->found_area = score;
        search->found_visible = visible;
    }
    return TRUE;
}

// FindWindowW returns whichever window of the class the loader enumerates
// first, and says nothing when there is none. Enumerate instead, so a run that
// matched nothing can report what it saw in place of only that it saw nothing.
HWND FindHostingWindow(const std::wstring& window_class,
                       std::vector<std::wstring>* seen,
                       std::vector<std::wstring>* candidates,
                       bool* visible) {
    Search search;
    search.window_class = window_class.c_str();
    search.seen = seen;
    search.candidates = candidates;
    EnumWindows(CollectWindow, reinterpret_cast<LPARAM>(&search));
    if (visible) *visible = search.found_visible;
    return search.found;
}

std::wstring ClassOf(HWND window) {
    if (!window) return L"(null)";
    wchar_t class_name[256]{};
    if (!GetClassNameW(window, class_name, 255)) return L"(unknown)";
    return class_name;
}

struct ChildSearch {
    const wchar_t* window_class = nullptr;
    HWND found = nullptr;
};

BOOL CALLBACK FindChildOfClass(HWND child, LPARAM parameter) {
    auto* search = reinterpret_cast<ChildSearch*>(parameter);
    wchar_t class_name[256]{};
    if (GetClassNameW(child, class_name, 255) &&
        std::wcscmp(class_name, search->window_class) == 0) {
        search->found = child;
        return FALSE;
    }
    EnumChildWindows(child, FindChildOfClass, parameter);
    return search->found == nullptr;
}

// The XAML island's own child window. Posting to it is what a message that
// survived the desktop queue would have reached anyway, and unlike GetFocus it
// can be found without attaching to the observed thread's input queue.
HWND FindIslandWindow(HWND host, const std::wstring& window_class) {
    ChildSearch search;
    search.window_class = window_class.c_str();
    EnumChildWindows(host, FindChildOfClass, reinterpret_cast<LPARAM>(&search));
    return search.found;
}

BOOL CALLBACK ReportChild(HWND child, LPARAM depth) {
    RECT rect{};
    GetWindowRect(child, &rect);
    std::printf("input child depth=%ld hwnd=%p class=%ls visible=%d "
                "rect=%ld,%ld,%ld,%ld\n",
                static_cast<long>(depth), static_cast<void*>(child),
                ClassOf(child).c_str(), IsWindowVisible(child) ? 1 : 0,
                rect.left, rect.top, rect.right - rect.left,
                rect.bottom - rect.top);
    if (depth < 3)
        EnumChildWindows(child, ReportChild, depth + 1);
    return TRUE;
}

// What the observed thread thinks has the keyboard. GetFocus is per input
// queue, so it can only be read while attached to that queue.
HWND FocusInThreadOf(HWND window, DWORD* thread_out) {
    const DWORD target = GetWindowThreadProcessId(window, nullptr);
    if (thread_out) *thread_out = target;
    if (!target) return nullptr;
    const DWORD self = GetCurrentThreadId();
    const bool attached = target != self &&
                          AttachThreadInput(self, target, TRUE) != FALSE;
    const HWND focus = GetFocus();
    if (attached) AttachThreadInput(self, target, FALSE);
    return focus;
}

struct Keystroke {
    wchar_t character = 0;  // 0 for a key with no character
    WORD virtual_key = 0;   // 0 when the character is injected as Unicode
};

std::vector<Keystroke> Keystrokes(const std::wstring& text, bool enter) {
    std::vector<Keystroke> strokes;
    for (const wchar_t character : text) strokes.push_back({character, 0});
    if (enter) strokes.push_back({L'\r', VK_RETURN});
    return strokes;
}

// SendInput's Unicode path. wScan carries the code unit and the virtual key is
// left at zero, which is what KEYEVENTF_UNICODE means.
unsigned SendUnicode(wchar_t character, bool key_up) {
    INPUT event{};
    event.type = INPUT_KEYBOARD;
    event.ki.wVk = 0;
    event.ki.wScan = static_cast<WORD>(character);
    event.ki.dwFlags = KEYEVENTF_UNICODE | (key_up ? KEYEVENTF_KEYUP : 0u);
    return SendInput(1, &event, sizeof(INPUT));
}

unsigned SendVirtualKey(WORD virtual_key, bool key_up) {
    INPUT event{};
    event.type = INPUT_KEYBOARD;
    event.ki.wVk = virtual_key;
    event.ki.wScan = static_cast<WORD>(
        MapVirtualKeyW(virtual_key, MAPVK_VK_TO_VSC));
    event.ki.dwFlags = key_up ? KEYEVENTF_KEYUP : 0u;
    return SendInput(1, &event, sizeof(INPUT));
}

LPARAM KeyLparam(WORD virtual_key, bool key_up) {
    const UINT scan = MapVirtualKeyW(virtual_key, MAPVK_VK_TO_VSC);
    LPARAM lparam = 1 | (static_cast<LPARAM>(scan) << 16);
    if (key_up) lparam |= (1LL << 30) | (1LL << 31);
    return lparam;
}

// The desktop pixels inside one window's client area, on a grid fixed before
// anything is observed. Read once before the keystrokes and once after, so a
// reader can tell whether typing changed what is on screen at all -- without
// this probe having to know what a rendered glyph looks like.
void SamplePixels(HWND window, const wchar_t* phase, unsigned columns,
                  unsigned rows) {
    RECT client{};
    POINT origin{0, 0};
    if (!GetClientRect(window, &client) || !ClientToScreen(window, &origin)) {
        std::printf("input pixels phase=%ls read=false\n", phase);
        return;
    }
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    HDC desktop = GetDC(nullptr);
    if (!desktop) {
        std::printf("input pixels phase=%ls read=false\n", phase);
        return;
    }
    for (unsigned row = 0; row < rows; ++row) {
        for (unsigned column = 0; column < columns; ++column) {
            const int x = static_cast<int>((2 * column + 1) * width /
                                           (2 * columns));
            const int y = static_cast<int>((2 * row + 1) * height / (2 * rows));
            const COLORREF pixel = GetPixel(desktop, origin.x + x, origin.y + y);
            const std::size_t index =
                static_cast<std::size_t>(row) * columns + column;
            if (pixel == CLR_INVALID) {
                std::printf("input pixel phase=%ls index=%zu x=%d y=%d "
                            "rgb=none\n", phase, index, x, y);
                continue;
            }
            std::printf("input pixel phase=%ls index=%zu x=%d y=%d "
                        "rgb=%02x%02x%02x\n", phase, index, x, y,
                        GetRValue(pixel), GetGValue(pixel), GetBValue(pixel));
        }
    }
    ReleaseDC(nullptr, desktop);
    std::printf("input pixels phase=%ls read=true client=%dx%d origin=%ld,%ld\n",
                phase, width, height, origin.x, origin.y);
}

bool ParseUnsigned(const wchar_t* text, unsigned* value) {
    if (!text || !*text) return false;
    wchar_t* end = nullptr;
    const unsigned long parsed = std::wcstoul(text, &end, 10);
    if (!end || *end) return false;
    *value = static_cast<unsigned>(parsed);
    return true;
}

bool ParseOptions(int argc, wchar_t** argv, Options* options) {
    for (int index = 1; index < argc; ++index) {
        const std::wstring name(argv[index]);
        const wchar_t* value = (index + 1 < argc) ? argv[index + 1] : nullptr;
        if (name == L"--class" && value) {
            options->window_class = value;
            ++index;
        } else if (name == L"--text" && value) {
            options->text = value;
            ++index;
        } else if (name == L"--wait-file" && value) {
            options->wait_file = value;
            ++index;
        } else if (name == L"--columns" && value &&
                   ParseUnsigned(value, &options->columns)) {
            ++index;
        } else if (name == L"--rows" && value &&
                   ParseUnsigned(value, &options->rows)) {
            ++index;
        } else if (name == L"--mechanism" && value) {
            const std::wstring mechanism(value);
            if (mechanism == L"sendinput") options->mechanism = Mechanism::SendInput;
            else if (mechanism == L"postmessage") options->mechanism = Mechanism::PostMessage;
            else if (mechanism == L"none") options->mechanism = Mechanism::None;
            else {
                std::fprintf(stderr, "input: unknown mechanism\n");
                return false;
            }
            ++index;
        } else if (name == L"--enter") {
            options->press_enter = true;
        } else if (name == L"--deadline-ms" && value &&
                   ParseUnsigned(value, &options->deadline_ms)) {
            ++index;
        } else if (name == L"--settle-ms" && value &&
                   ParseUnsigned(value, &options->settle_ms)) {
            ++index;
        } else if (name == L"--visible-grace-ms" && value &&
                   ParseUnsigned(value, &options->visible_grace_ms)) {
            ++index;
        } else if (name == L"--interval-ms" && value &&
                   ParseUnsigned(value, &options->interval_ms)) {
            ++index;
        } else if (name == L"--key-delay-ms" && value &&
                   ParseUnsigned(value, &options->key_delay_ms)) {
            ++index;
        } else {
            std::fprintf(stderr, "input: unknown argument\n");
            return false;
        }
    }
    return options->interval_ms >= 1 && options->columns >= 1 &&
           options->rows >= 1;
}

const wchar_t* MechanismName(Mechanism mechanism) {
    switch (mechanism) {
        case Mechanism::SendInput: return L"sendinput";
        case Mechanism::PostMessage: return L"postmessage";
        case Mechanism::None: return L"none";
    }
    return L"unknown";
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    Options options;
    if (!ParseOptions(argc, argv, &options)) return 2;

    std::printf("input mechanism=%ls text_length=%zu enter=%d\n",
                MechanismName(options.mechanism), options.text.size(),
                options.press_enter ? 1 : 0);

    const DWORD started = GetTickCount();
    HWND host = nullptr;
    unsigned attempts = 0;
    std::vector<std::wstring> seen_classes;
    std::vector<std::wstring> candidates;
    bool host_visible = false;
    // Wait for the window to exist, then give it a bounded grace period to be
    // shown. A window that is never shown is a finding to report, not a reason
    // to spend the whole deadline looking and then type into a process that
    // has moved on.
    HWND invisible_host = nullptr;
    DWORD first_seen = 0;
    while (GetTickCount() - started < options.deadline_ms) {
        ++attempts;
        bool visible = false;
        host = FindHostingWindow(options.window_class, &seen_classes,
                                 &candidates, &visible);
        RECT client{};
        if (host && GetClientRect(host, &client) &&
            client.right - client.left > 1 && client.bottom - client.top > 1) {
            host_visible = visible;
            if (visible) break;
            invisible_host = host;
            if (!first_seen) first_seen = GetTickCount();
            if (GetTickCount() - first_seen >= options.visible_grace_ms) break;
        }
        host = nullptr;
        Sleep(options.interval_ms);
    }
    if (!host && invisible_host) {
        host = invisible_host;
        host_visible = false;
    }
    for (const std::wstring& name : seen_classes)
        std::printf("input class name=%ls\n", name.c_str());
    for (const std::wstring& candidate : candidates)
        std::printf("input candidate %ls\n", candidate.c_str());
    if (!host) {
        std::printf("input window found=false attempts=%u\n", attempts);
        std::printf("input done injected=0 of=0\n");
        std::fflush(stdout);
        return 1;
    }

    RECT client{};
    GetClientRect(host, &client);
    std::printf("input window found=true hwnd=%p class=%ls client=%ldx%ld "
                "visible=%d attempts=%u\n",
                static_cast<void*>(host), options.window_class.c_str(),
                client.right - client.left, client.bottom - client.top,
                host_visible ? 1 : 0, attempts);

    // Typing into a shell that has not started yet measures nothing. When a
    // readiness file is named, wait for the shell itself to create it -- that
    // is the shell saying it ran, rather than this process guessing from a
    // clock. The settle after it is for the prompt, not for the spawn.
    bool shell_ready = options.wait_file.empty();
    unsigned ready_waits = 0;
    while (!shell_ready && GetTickCount() - started < options.deadline_ms) {
        ++ready_waits;
        shell_ready = GetFileAttributesW(options.wait_file.c_str()) !=
                      INVALID_FILE_ATTRIBUTES;
        if (!shell_ready) Sleep(options.interval_ms);
    }
    std::printf("input shell_ready=%s waits=%u file=%ls\n",
                shell_ready ? "true" : "false", ready_waits,
                options.wait_file.empty() ? L"(none)"
                                          : options.wait_file.c_str());
    Sleep(options.settle_ms);

    EnumChildWindows(host, ReportChild, 1);

    const HWND island = FindIslandWindow(host, L"OpenXaml.DesktopWindowXamlSource");
    std::printf("input island hwnd=%p class=%ls\n", static_cast<void*>(island),
                ClassOf(island).c_str());

    const HWND foreground_before = GetForegroundWindow();
    const BOOL brought = SetForegroundWindow(host);
    SetActiveWindow(host);
    Sleep(200);
    const HWND foreground_after = GetForegroundWindow();
    std::printf("input foreground requested=%d before=%p before_class=%ls "
                "after=%p after_class=%ls\n",
                brought ? 1 : 0, static_cast<void*>(foreground_before),
                ClassOf(foreground_before).c_str(),
                static_cast<void*>(foreground_after),
                ClassOf(foreground_after).c_str());

    DWORD host_thread = 0;
    const HWND focus = FocusInThreadOf(host, &host_thread);
    std::printf("input focus thread=%lu hwnd=%p class=%ls\n", host_thread,
                static_cast<void*>(focus), ClassOf(focus).c_str());

    SamplePixels(host, L"before", options.columns, options.rows);

    const std::vector<Keystroke> strokes =
        Keystrokes(options.text, options.press_enter);
    unsigned injected = 0;
    for (std::size_t index = 0; index < strokes.size(); ++index) {
        const Keystroke& stroke = strokes[index];
        unsigned sent_down = 0;
        unsigned sent_up = 0;
        switch (options.mechanism) {
            case Mechanism::None:
                break;
            case Mechanism::SendInput:
                if (stroke.virtual_key) {
                    sent_down = SendVirtualKey(stroke.virtual_key, false);
                    sent_up = SendVirtualKey(stroke.virtual_key, true);
                } else {
                    sent_down = SendUnicode(stroke.character, false);
                    sent_up = SendUnicode(stroke.character, true);
                }
                break;
            case Mechanism::PostMessage: {
                const HWND target = island ? island : (focus ? focus : host);
                const WORD virtual_key =
                    stroke.virtual_key
                        ? stroke.virtual_key
                        : static_cast<WORD>(VkKeyScanW(stroke.character) & 0xff);
                sent_down = PostMessageW(target, WM_KEYDOWN, virtual_key,
                                         KeyLparam(virtual_key, false)) ? 1 : 0;
                sent_down += PostMessageW(target, WM_CHAR, stroke.character,
                                          KeyLparam(virtual_key, false)) ? 1 : 0;
                sent_up = PostMessageW(target, WM_KEYUP, virtual_key,
                                       KeyLparam(virtual_key, true)) ? 1 : 0;
                break;
            }
        }
        if (sent_down) ++injected;
        std::printf("input key index=%zu char=%u vk=%u down=%u up=%u\n", index,
                    static_cast<unsigned>(stroke.character),
                    static_cast<unsigned>(stroke.virtual_key), sent_down,
                    sent_up);
        Sleep(options.key_delay_ms);
    }

    // Give whatever the keystrokes reached a moment to repaint before reading
    // the same grid again.
    Sleep(1500);
    SamplePixels(host, L"after", options.columns, options.rows);

    const HWND focus_after = FocusInThreadOf(host, nullptr);
    std::printf("input focus_after hwnd=%p class=%ls\n",
                static_cast<void*>(focus_after), ClassOf(focus_after).c_str());
    std::printf("input done injected=%u of=%zu\n", injected, strokes.size());
    std::fflush(stdout);
    return 0;
}
