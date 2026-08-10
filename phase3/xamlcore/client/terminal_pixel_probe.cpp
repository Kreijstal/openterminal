// Reads desktop pixels out of a live hosting window, and says how much to
// trust them.
//
// This is the pixel half of the visible-XBF-UI gate. It deliberately knows
// nothing about XAML, the runtime DLL or the process it observes: it finds a
// top-level window by class name, samples a fixed grid of points inside that
// window's client area from the *desktop* DC, and prints what it read. The
// checker compares those readings against the runtime's own record of the
// frame it committed, so neither side can confirm itself.
//
// It also measures, with plain GDI and no XAML, how far this loader displaces
// a layered child from where UpdateLayeredWindow was told to put it. A desktop
// island presents through UpdateLayeredWindow(ULW_ALPHA) on a
// WS_CHILD | WS_EX_LAYERED window whose destination it computes from that
// child's own client origin; a loader that applies a screen-space point inside
// the parent-relative child transaction moves the child by the parent's client
// origin a second time, so the frame lands somewhere other than over the host.
// research/wine/af5241854c513c2e68938425cc6cd3cac40b943a/layered-child-update.md
// records that boundary and the narrow Wine change that closes it. Measuring it
// here rather than assuming it is what lets the checker enforce the pixel
// readings on a loader that places a layered child correctly and skip them by
// name -- never silently -- on one that does not.

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

namespace {

struct Options {
    std::wstring window_class = L"CASCADIA_HOSTING_WINDOW_CLASS";
    unsigned deadline_ms = 25000;
    unsigned interval_ms = 100;
    unsigned columns = 9;
    unsigned rows = 7;
};

// How far this loader displaces a layered child from where
// UpdateLayeredWindow was told to put it. Zero means the loader places it as
// the Win32 contract says, and pixel readings can be enforced.
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
            static_cast<unsigned*>(bits)[index] = 0xff000000u;
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

struct Sample {
    int x = 0;   // client coordinates of the observed window
    int y = 0;
    int screen_x = 0;  // and where that landed on the desktop
    int screen_y = 0;
    bool read = false;
    COLORREF color = CLR_INVALID;
};

struct Search {
    const wchar_t* window_class = nullptr;
    HWND found = nullptr;
    long found_area = 0;
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
        if (!known && search->seen->size() < 32) search->seen->push_back(name);
    }
    if (std::wcscmp(class_name, search->window_class) != 0) return TRUE;

    RECT client{};
    const bool visible = IsWindowVisible(window) != FALSE;
    const bool measured = GetClientRect(window, &client) != FALSE;
    const long area = measured
        ? static_cast<long>(client.right - client.left) *
              static_cast<long>(client.bottom - client.top)
        : 0;
    if (search->candidates && search->candidates->size() < 16) {
        wchar_t description[192]{};
        std::swprintf(description, 191, L"hwnd=%p visible=%d client=%ldx%ld",
                      static_cast<void*>(window), visible ? 1 : 0,
                      measured ? client.right - client.left : -1,
                      measured ? client.bottom - client.top : -1);
        const std::wstring text(description);
        bool known = false;
        for (const std::wstring& previous : *search->candidates)
            known = known || previous == text;
        if (!known) search->candidates->push_back(text);
    }
    // A process can own several windows of one class -- a hidden one and the
    // one on screen. Take the largest visible one rather than the first, so
    // which window is read does not depend on enumeration order.
    if (visible && area > search->found_area) {
        search->found = window;
        search->found_area = area;
    }
    return TRUE;
}

// FindWindowW returns whichever window of the class the loader enumerates
// first. Enumerate instead: an unmatched run can then also report what it saw.
HWND FindHostingWindow(const std::wstring& window_class,
                       std::vector<std::wstring>* seen,
                       std::vector<std::wstring>* candidates) {
    Search search;
    search.window_class = window_class.c_str();
    search.seen = seen;
    search.candidates = candidates;
    EnumWindows(CollectWindow, reinterpret_cast<LPARAM>(&search));
    return search.found;
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
        } else if (name == L"--deadline-ms" && value &&
                   ParseUnsigned(value, &options->deadline_ms)) {
            ++index;
        } else if (name == L"--interval-ms" && value &&
                   ParseUnsigned(value, &options->interval_ms)) {
            ++index;
        } else if (name == L"--columns" && value &&
                   ParseUnsigned(value, &options->columns)) {
            ++index;
        } else if (name == L"--rows" && value &&
                   ParseUnsigned(value, &options->rows)) {
            ++index;
        } else {
            std::fprintf(stderr, "probe: unknown argument\n");
            return false;
        }
    }
    return options->columns >= 1 && options->rows >= 1 &&
           options->interval_ms >= 1;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    Options options;
    if (!ParseOptions(argc, argv, &options)) return 2;

    const POINT displacement = LayeredChildDisplacement();
    std::printf("probe displacement=%+ld,%+ld\n", displacement.x,
                displacement.y);

    // The grid is fixed before anything is observed, so which points are read
    // cannot depend on what was found there.
    std::vector<Sample> samples(
        static_cast<std::size_t>(options.columns) * options.rows);

    const DWORD started = GetTickCount();
    unsigned attempts = 0;
    unsigned observations = 0;
    int client_width = 0;
    int client_height = 0;
    POINT client_origin{0, 0};
    bool ever_found = false;

    std::vector<std::wstring> seen_classes;
    std::vector<std::wstring> candidates;
    while (GetTickCount() - started < options.deadline_ms) {
        ++attempts;
        HWND window = FindHostingWindow(options.window_class, &seen_classes,
                                        &candidates);
        RECT client{};
        POINT origin{0, 0};
        if (window && GetClientRect(window, &client) &&
            ClientToScreen(window, &origin) &&
            client.right - client.left > 1 && client.bottom - client.top > 1) {
            ever_found = true;
            client_width = client.right - client.left;
            client_height = client.bottom - client.top;
            client_origin = origin;
            HDC desktop = GetDC(nullptr);
            if (desktop) {
                for (unsigned row = 0; row < options.rows; ++row) {
                    for (unsigned column = 0; column < options.columns; ++column) {
                        const std::size_t index =
                            static_cast<std::size_t>(row) * options.columns + column;
                        // Cell centres, so no sample lands on a boundary
                        // between two fills.
                        const int x = static_cast<int>(
                            (2 * column + 1) * client_width / (2 * options.columns));
                        const int y = static_cast<int>(
                            (2 * row + 1) * client_height / (2 * options.rows));
                        const int screen_x = static_cast<int>(origin.x) + x;
                        const int screen_y = static_cast<int>(origin.y) + y;
                        const COLORREF pixel = GetPixel(desktop, screen_x, screen_y);
                        samples[index].x = x;
                        samples[index].y = y;
                        samples[index].screen_x = screen_x;
                        samples[index].screen_y = screen_y;
                        if (pixel != CLR_INVALID) {
                            samples[index].read = true;
                            samples[index].color = pixel;
                        }
                    }
                }
                ReleaseDC(nullptr, desktop);
                ++observations;
            }
        }
        Sleep(options.interval_ms);
    }

    std::printf("probe window found=%s client=%dx%d origin=%ld,%ld "
                "attempts=%u observations=%u\n",
                ever_found ? "true" : "false", client_width, client_height,
                client_origin.x, client_origin.y, attempts, observations);
    // Which windows were on screen, so a run that matched nothing says what it
    // did see instead of only that it saw nothing.
    for (const std::wstring& name : seen_classes)
        std::printf("probe class name=%ls\n", name.c_str());
    for (const std::wstring& candidate : candidates)
        std::printf("probe candidate %ls\n", candidate.c_str());
    for (std::size_t index = 0; index < samples.size(); ++index) {
        const Sample& sample = samples[index];
        if (!sample.read) {
            std::printf("probe sample index=%zu x=%d y=%d sx=%d sy=%d rgb=none\n",
                        index, sample.x, sample.y, sample.screen_x,
                        sample.screen_y);
            continue;
        }
        std::printf("probe sample index=%zu x=%d y=%d sx=%d sy=%d rgb=%02x%02x%02x\n",
                    index, sample.x, sample.y, sample.screen_x, sample.screen_y,
                    GetRValue(sample.color), GetGValue(sample.color),
                    GetBValue(sample.color));
    }
    std::printf("probe done\n");
    std::fflush(stdout);
    return 0;
}
