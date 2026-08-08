// One corpus case, in a real window.
//
// The offscreen dumps prove the render pass draws what the layout says. They do
// not prove any of it survives a window: a DIB is a buffer this process owns
// from end to end, and a window is a surface the window manager, the DWM
// equivalent under Wine, and WM_PAINT all get a say in. So this creates one,
// paints a case into it through the same entry a host would call, and reads the
// pixels back off the window itself.
//
// The check is the comparison, not the picture: the window's pixels have to be
// the offscreen dump's pixels. If they are, then the rectangles that the
// round-trip gate recovered from the dump are the rectangles that reached the
// screen, and the gate covers both.
//
// This is also the shape phase3/xamlcore's island hosting needs -- a display
// list built once and painted into whatever HDC the host hands over -- which is
// why the paint call here is `Paint(hdc, ...)` from gdi_target.h and not
// something private to this file.

#include <windows.h>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "case_runner.h"
#include "fonts.h"
#include "gdi_target.h"
#include "resources.h"
#include "surface.h"

using namespace openxaml;
using namespace openxaml::render;

namespace {

struct HostState {
    DisplayList* list = nullptr;
    std::vector<std::string> failures;
    bool painted = false;
};

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto* state = reinterpret_cast<HostState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_PAINT && state && state->list) {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(window, &paint);
        Paint(dc, 0, 0, *state->list, ProbeInkColor(), state->failures);
        EndPaint(window, &paint);
        state->painted = true;
        return 0;
    }
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, w, l);
}

std::string Slurp(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: xaml_window <case.json> <out.ppm> [fonts-dir] [theme-resources]"
                     " [hold-ms]\n";
        return 2;
    }
    const std::string case_path = argv[1];
    const std::string out_path = argv[2];
    if (argc >= 4) {
        try {
            LoadFontDirectory(FontLibrary::Default(), argv[3]);
        } catch (const std::exception& e) {
            std::cerr << "cannot load font metrics: " << e.what() << "\n";
            return 4;
        }
    }
    if (argc >= 5) {
        try {
            LoadThemeResources(ThemeResourceLibrary::Default(), argv[4]);
        } catch (const std::exception& e) {
            std::cerr << "cannot load theme resources: " << e.what() << "\n";
            return 4;
        }
    }

    CaseResult result = LayOutCase(Slurp(case_path));
    if (!result.load_error.empty()) {
        std::cerr << "the case did not load: " << result.load_error << "\n";
        return 4;
    }
    const PixelRect box =
        SnapRect(Rect{0.0, 0.0, result.list.surface.width, result.list.surface.height});
    if (box.empty()) {
        std::cerr << "the case arranges to nothing; there is no window to make\n";
        return 4;
    }

    HostState state;
    state.list = &result.list;

    WNDCLASSEXW window_class;
    ZeroMemory(&window_class, sizeof(window_class));
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = WindowProc;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpszClassName = L"OpenXamlRenderHost";
    // No background brush. A class brush would erase the window with a colour
    // nothing in this project chose, and it would land in the pixels this
    // compares.
    window_class.hbrBackground = nullptr;
    if (!RegisterClassExW(&window_class)) {
        std::cerr << "RegisterClassEx failed: " << GetLastError() << "\n";
        return 5;
    }

    // The client area is the surface, exactly. AdjustWindowRect turns that into
    // the outer size, so the window's client pixels and the dump's pixels are
    // the same grid rather than the same size by luck.
    RECT wanted{0, 0, box.right, box.bottom};
    AdjustWindowRect(&wanted, WS_OVERLAPPEDWINDOW, FALSE);
    HWND window = CreateWindowExW(0, window_class.lpszClassName, L"openxaml render",
                                  WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                  wanted.right - wanted.left, wanted.bottom - wanted.top,
                                  nullptr, nullptr, window_class.hInstance, nullptr);
    if (!window) {
        std::cerr << "CreateWindowEx failed: " << GetLastError() << "\n";
        return 5;
    }
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state));
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);

    MSG message;
    int spins = 0;
    while (!state.painted && spins < 200) {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(10);
        ++spins;
    }
    if (!state.painted) {
        std::cerr << "the window never received WM_PAINT\n";
        return 6;
    }

    // Read the pixels back off the window itself rather than off the DIB the
    // paint went through. Anything the window system did to them on the way --
    // a clip, a scale, a stray erase -- is then in what this writes out, which
    // is the entire reason for reading them here instead.
    RECT client;
    GetClientRect(window, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (width != box.right || height != box.bottom) {
        std::cerr << "the client area is " << width << "x" << height << " and the case wants "
                  << box.right << "x" << box.bottom << "\n";
        return 6;
    }

    HDC window_dc = GetDC(window);
    DibTarget readback(width, height);
    if (!readback.valid()) {
        std::cerr << "could not make a read-back DIB\n";
        ReleaseDC(window, window_dc);
        return 5;
    }
    if (!BitBlt(readback.dc(), 0, 0, width, height, window_dc, 0, 0, SRCCOPY)) {
        std::cerr << "BitBlt off the window failed: " << GetLastError() << "\n";
        ReleaseDC(window, window_dc);
        return 5;
    }
    ReleaseDC(window, window_dc);

    Surface surface(width, height, BackdropColor());
    readback.Store(surface);
    // The window's DC carries no alpha, so the channel comes back as whatever
    // was in the bits. The dumps are opaque by construction; see gdi_target.cpp.
    for (std::uint32_t& pixel : surface.pixels()) pixel |= 0xff000000u;

    std::ofstream(out_path, std::ios::binary) << ToPpm(surface);
    for (const std::string& failure : state.failures)
        std::cerr << "text: " << failure << "\n";
    std::cout << "painted " << result.id << " into a " << width << "x" << height
              << " window and read it back to " << out_path << "\n";
    std::cout.flush();

    // Optionally stay up. A screenshot taken off the display is the one check
    // that shares no code with this process, and it needs the window to still
    // be there when the camera goes off.
    const int hold_ms = argc >= 6 ? std::atoi(argv[5]) : 0;
    for (int waited = 0; waited < hold_ms; waited += 20) {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(20);
    }

    DestroyWindow(window);
    return 0;
}
