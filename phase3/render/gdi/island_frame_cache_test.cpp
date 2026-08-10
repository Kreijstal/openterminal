#include <windows.h>

#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "border.h"
#include "dwrite_text_provider.h"
#include "grid.h"
#include "island_frame_cache.h"
#include "surface.h"

using namespace openxaml;
using namespace openxaml::render;

namespace {

int failures = 0;
int skipped = 0;
constexpr wchar_t kLayeredTestClass[] = L"OpenXaml.IslandFrameCache.LayeredTest";
constexpr wchar_t kLayeredProbeClass[] = L"OpenXaml.IslandFrameCache.LayeredProbe";

void Check(bool condition, const char* what, int line) {
    if (condition) return;
    std::cerr << "island_frame_cache_test.cpp:" << line
              << ": CHECK failed: " << what << "\n";
    ++failures;
}

#define CHECK(condition) Check(static_cast<bool>(condition), #condition, __LINE__)

// What this loader actually does with a layered child's pixels, measured with
// plain GDI and no XAML.
//
// The island publishes a frame with UpdateLayeredWindow(ULW_ALPHA) on a
// WS_CHILD | WS_EX_LAYERED window and a destination in screen coordinates,
// which is the Win32 contract. Two things have to hold for a pixel assertion
// on the result to mean anything, and neither is this project's code:
//
//   * an opaque source pixel has to reach the screen, and
//   * an alpha-zero source pixel has to leave the parent's pixel showing.
//
// research/wine/af5241854c513c2e68938425cc6cd3cac40b943a/layered-child-update.md
// records why the second one does not hold on Wine: a layered child is given
// its own root-level ARGB X window rather than being composited into its
// parent's surface, so alpha zero resolves to zero instead of to the parent.
// The first one additionally does not hold when the window was not yet visible
// at the time of the update -- Win32 allows setting a layered window's content
// before showing it, and on this path the RGB does not survive the map.
//
// So both are measured, in the order the island itself uses, before anything
// asserts on a pixel.
struct LayeredChildPresentation {
    bool measured = false;
    // A publication made while the child was still hidden, read after it was
    // mapped. Win32 lets a host fill a layered window before showing it, and
    // the island's first frame does exactly that.
    bool opaque_survives_map = false;
    bool alpha_zero_reveals_parent_across_map = false;
    COLORREF across_map_opaque_read = 0;
    COLORREF across_map_alpha_zero_read = 0;
    // A publication made to an already mapped child: the steady-state path
    // every frame after the first takes, and the one the composition contract
    // itself is about.
    bool opaque_reaches_screen = false;
    bool alpha_zero_reveals_parent = false;
    COLORREF opaque_read = 0;
    COLORREF alpha_zero_read = 0;
    long displacement_x = 0;
    long displacement_y = 0;

    bool composites() const {
        return measured && opaque_reaches_screen && alpha_zero_reveals_parent;
    }
    bool survives_map() const {
        return measured && opaque_survives_map && alpha_zero_reveals_parent_across_map;
    }
};

LayeredChildPresentation layered_child_presentation;

LRESULT CALLBACK LayeredProbeWindowProc(HWND window, UINT message, WPARAM wparam,
                                        LPARAM lparam) {
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        if (dc) {
            RECT client{};
            GetClientRect(window, &client);
            HBRUSH background = CreateSolidBrush(RGB(0x11, 0x22, 0x33));
            FillRect(dc, &client, background);
            DeleteObject(background);
            EndPaint(window, &paint);
        }
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

LayeredChildPresentation MeasureLayeredChildPresentation() {
    LayeredChildPresentation measurement;

    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = LayeredProbeWindowProc;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpszClassName = kLayeredProbeClass;
    if (RegisterClassExW(&window_class) == 0 &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return measurement;

    HWND parent = CreateWindowExW(0, kLayeredProbeClass, L"", WS_POPUP, 200, 200, 8, 4,
                                  nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!parent) return measurement;
    ShowWindow(parent, SW_SHOWNOACTIVATE);
    UpdateWindow(parent);

    HWND child = CreateWindowExW(WS_EX_LAYERED, kLayeredProbeClass, L"", WS_CHILD, 0, 0,
                                 8, 4, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!child) {
        DestroyWindow(parent);
        return measurement;
    }

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = 8;
    info.bmiHeader.biHeight = -4;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bitmap && bits && memory && screen) {
        // Left half opaque, right half alpha zero -- the same two-sided source
        // the island's own presentation check uses.
        auto* pixels = static_cast<std::uint32_t*>(bits);
        for (int y = 0; y < 4; ++y) {
            for (int x = 0; x < 8; ++x)
                pixels[y * 8 + x] = x < 4 ? 0xff908070u : 0x00000000u;
        }
        HGDIOBJ previous = SelectObject(memory, bitmap);
        POINT origin{0, 0};
        if (ClientToScreen(parent, &origin)) {
            POINT destination = origin;
            POINT source{0, 0};
            SIZE size{8, 4};
            BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
            if (UpdateLayeredWindow(child, nullptr, &destination, &size, memory, &source,
                                    0, &blend, ULW_ALPHA)) {
                RECT placed{};
                if (GetWindowRect(child, &placed)) {
                    measurement.displacement_x = placed.left - origin.x;
                    measurement.displacement_y = placed.top - origin.y;
                }
                ShowWindow(child, SW_SHOWNOACTIVATE);
                GdiFlush();
                measurement.across_map_opaque_read = GetPixel(screen, origin.x, origin.y);
                measurement.across_map_alpha_zero_read =
                    GetPixel(screen, origin.x + 4, origin.y);
                measurement.opaque_survives_map =
                    measurement.across_map_opaque_read == RGB(0x90, 0x80, 0x70);
                measurement.alpha_zero_reveals_parent_across_map =
                    measurement.across_map_alpha_zero_read == RGB(0x11, 0x22, 0x33);

                // Publish again, now that the child is mapped. A loader that
                // only loses the content across the map answers differently
                // here than one whose layered child has no per-pixel alpha
                // against its parent at all.
                destination = origin;
                if (UpdateLayeredWindow(child, nullptr, &destination, &size, memory,
                                        &source, 0, &blend, ULW_ALPHA)) {
                    GdiFlush();
                    measurement.opaque_read = GetPixel(screen, origin.x, origin.y);
                    measurement.alpha_zero_read = GetPixel(screen, origin.x + 4, origin.y);
                    measurement.opaque_reaches_screen =
                        measurement.opaque_read == RGB(0x90, 0x80, 0x70);
                    measurement.alpha_zero_reveals_parent =
                        measurement.alpha_zero_read == RGB(0x11, 0x22, 0x33);
                    measurement.measured = true;
                }
            }
        }
        SelectObject(memory, previous);
    }
    if (bitmap) DeleteObject(bitmap);
    if (memory) DeleteDC(memory);
    if (screen) ReleaseDC(nullptr, screen);
    DestroyWindow(child);
    DestroyWindow(parent);
    UnregisterClassW(kLayeredProbeClass, GetModuleHandleW(nullptr));
    return measurement;
}

void ReportLayeredMeasurement(int line, const char* what) {
    const LayeredChildPresentation& measured = layered_child_presentation;
    std::cerr << "island_frame_cache_test.cpp:" << line << ": SKIP: " << what
              << " -- this loader "
              << (measured.measured ? "" : "could not be measured for ")
              << "layered-child presentation. Publishing to a mapped child: opaque="
              << (measured.opaque_reaches_screen ? "reaches screen" : "lost") << " (read 0x"
              << std::hex << static_cast<unsigned long>(measured.opaque_read) << std::dec
              << "), alpha-zero="
              << (measured.alpha_zero_reveals_parent ? "reveals parent" : "resolves to zero")
              << " (read 0x" << std::hex
              << static_cast<unsigned long>(measured.alpha_zero_read) << std::dec
              << "). Publishing before the map: opaque="
              << (measured.opaque_survives_map ? "survives" : "lost") << " (read 0x"
              << std::hex << static_cast<unsigned long>(measured.across_map_opaque_read)
              << std::dec << "), alpha-zero="
              << (measured.alpha_zero_reveals_parent_across_map ? "reveals parent"
                                                                : "resolves to zero")
              << " (read 0x" << std::hex
              << static_cast<unsigned long>(measured.across_map_alpha_zero_read) << std::dec
              << "). Child displacement " << measured.displacement_x << ","
              << measured.displacement_y
              << "; see research/wine/af5241854c513c2e68938425cc6cd3cac40b943a/"
                 "layered-child-update.md\n";
    ++skipped;
}

// The composition contract: an opaque frame pixel is exact on screen and an
// alpha-zero frame pixel leaves the parent's pixel showing. Enforced wherever
// the probe says this loader delivers both to a mapped layered child.
void CheckLayeredPixel(bool condition, const char* what, int line) {
    if (layered_child_presentation.composites()) {
        Check(condition, what, line);
        return;
    }
    ReportLayeredMeasurement(line, what);
}

// The separate claim that content published to a layered child that is not yet
// mapped is still there once it is. Win32 allows filling a layered window
// before showing it, and the island's first frame does; a loader that drops it
// is a distinct boundary from the one above and is named as its own.
void CheckLayeredPixelAcrossMap(bool condition, const char* what, int line) {
    if (layered_child_presentation.survives_map()) {
        Check(condition, what, line);
        return;
    }
    ReportLayeredMeasurement(line, what);
}

#define CHECK_LAYERED_PIXEL(condition) \
    CheckLayeredPixel(static_cast<bool>(condition), #condition, __LINE__)
#define CHECK_LAYERED_PIXEL_ACROSS_MAP(condition) \
    CheckLayeredPixelAcrossMap(static_cast<bool>(condition), #condition, __LINE__)

Surface PresentToSurface(const IslandFrameCache& cache, Color initial) {
    DibTarget destination(cache.width(), cache.height());
    Surface surface(cache.width(), cache.height(), initial);
    destination.Load(surface);
    const FramePresentResult result = cache.Present(destination.dc());
    CHECK(result.presented);
    CHECK(result.error == ERROR_SUCCESS);
    destination.Store(surface);
    return surface;
}

LRESULT CALLBACK LayeredTestWindowProc(HWND window, UINT message,
                                       WPARAM wparam, LPARAM lparam) {
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        if (dc && !(GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_LAYERED)) {
            RECT client{};
            GetClientRect(window, &client);
            HBRUSH background = CreateSolidBrush(RGB(0x11, 0x22, 0x33));
            FillRect(dc, &client, background);
            DeleteObject(background);
        }
        if (dc) {
            EndPaint(window, &paint);
        }
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

void ClearCoversTheWholeFrameAndPresentDoesNotRebuild() {
    IslandFrameCache cache;
    {
        Border arranged;
        arranged.Measure({7.0, 5.0});
        arranged.Arrange({0.0, 0.0, 7.0, 5.0});
        CHECK(cache.Rebuild(arranged, {7.0, 5.0},
                            Color{0xff, 0x12, 0x34, 0x56}));
    }  // The cache must not retain the Element.

    CHECK(cache.ready());
    CHECK(cache.generation() == 1);
    CHECK(cache.width() == 7);
    CHECK(cache.height() == 5);
    const std::uint64_t generation = cache.generation();

    const Surface first = PresentToSurface(cache, Color{0xff, 1, 2, 3});
    const Surface second = PresentToSurface(cache, Color{0xff, 9, 8, 7});
    CHECK(first.pixels() == second.pixels());
    CHECK(cache.generation() == generation);
    for (std::uint32_t pixel : first.pixels())
        CHECK(pixel == Pack(Color{0xff, 0x12, 0x34, 0x56}));
}

void RebuildCommitsACompleteNewFrame() {
    IslandFrameCache cache;
    Grid root;
    root.set_background_brush(
        BrushValue{true, true, Color{0xff, 0xaa, 0xbb, 0xcc}});
    root.Measure({4.0, 3.0});
    root.Arrange({0.0, 0.0, 4.0, 3.0});
    CHECK(cache.Rebuild(root, {4.0, 3.0}, Color{0xff, 0, 0, 0}));
    CHECK(cache.generation() == 1);
    const Surface painted = PresentToSurface(cache, Color{0xff, 1, 1, 1});
    for (std::uint32_t pixel : painted.pixels())
        CHECK(pixel == Pack(Color{0xff, 0xaa, 0xbb, 0xcc}));

    // Invalid input reports its failure without replacing the good frame.
    CHECK(!cache.Rebuild(root, {-1.0, 3.0}, Color{0xff, 9, 9, 9}));
    CHECK(!cache.last_build_error().empty());
    CHECK(cache.generation() == 1);
    const Surface retained = PresentToSurface(cache, Color{0xff, 2, 2, 2});
    CHECK(retained.pixels() == painted.pixels());
}

void ClearOnlyRebuildReplacesContentAtomically() {
    IslandFrameCache cache;
    Grid root;
    root.set_background_brush(
        BrushValue{true, true, Color{0xff, 0xaa, 0xbb, 0xcc}});
    root.Measure({4.0, 3.0});
    root.Arrange({0.0, 0.0, 4.0, 3.0});
    CHECK(cache.Rebuild(root, {4.0, 3.0}, Color{0xff, 0, 0, 0}));

    CHECK(cache.RebuildClear({4.0, 3.0}, Color{0, 0x21, 0x43, 0x65}));
    CHECK(cache.generation() == 2);
    CHECK(cache.has_transparency());
    CHECK(cache.refusals().empty());
    CHECK(cache.text_failures().empty());
    CHECK(cache.render_issues().empty());
    const Color destination{0xff, 7, 8, 9};
    const Surface cleared = PresentToSurface(cache, destination);
    for (std::uint32_t pixel : cleared.pixels())
        CHECK(pixel == Pack(destination));

    CHECK(!cache.RebuildClear({-1.0, 3.0}, Color{0xff, 1, 2, 3}));
    CHECK(cache.generation() == 2);
    const Surface retained = PresentToSurface(cache, destination);
    CHECK(retained.pixels() == cleared.pixels());
}

void DiagnosticsDescribeTheCommittedFrameAndFailedAttempt() {
    IslandFrameCache cache;
    CHECK(cache.RebuildClear({4.0, 3.0}, Color{}));
    CHECK(cache.DiagnosticsText() ==
          "generation=1 ready=true extent=4x3 transparency=true "
          "refusals=0 text_failures=0 render_issues=0");

    CHECK(!cache.RebuildClear({-1.0, 3.0}, Color{}));
    const std::string failed = cache.DiagnosticsText();
    CHECK(failed.find("generation=1 ready=true extent=4x3") == 0);
    CHECK(failed.find("build_error=\"the frame extent is not a finite, "
                      "non-negative DIB size\"") != std::string::npos);
}

class VerboseDiagnosticGrid final : public Grid {
public:
    std::string TypeName() const override {
        return "Windows.UI.Xaml.Controls.AVeryLongDiagnosticTypeWithAQuote\\\""
               "AndNewline\\nAndEnoughRepeatedPathMaterialToExceedTheDebugSinkBound"
               "012345678901234567890123456789012345678901234567890123456789";
    }
};

void DiagnosticsAreOneBoundedDeterministicLinePerRefusal() {
    IslandFrameCache cache;
    VerboseDiagnosticGrid root;
    root.set_background_brush(BrushValue{true, false, Color{}});
    root.Measure({4.0, 3.0});
    root.Arrange({0.0, 0.0, 4.0, 3.0});
    CHECK(cache.Rebuild(root, {4.0, 3.0}, Color{}));
    CHECK(cache.refusals().size() == 1);

    const std::vector<std::string> first = cache.DiagnosticsLines();
    const std::vector<std::string> second = cache.DiagnosticsLines();
    CHECK(first == second);
    CHECK(first.size() == 2);
    CHECK(first[0] ==
          "generation=1 ready=true extent=4x3 transparency=true "
          "refusals=1 text_failures=0 render_issues=0");
    CHECK(first[1].find("refusal[0] path=\"...") == 0);
    CHECK(first[1].find(" feature=\"background\"") != std::string::npos);
    CHECK(first[1].find(" reason=\"") != std::string::npos);
    CHECK(first[1].find("...") != std::string::npos);
    for (const std::string& line : first) {
        CHECK(line.size() <= IslandFrameCache::kMaxDiagnosticsLineLength);
        CHECK(std::string("OpenXaml frame diagnostic ").size() + line.size() + 1 <= 256);
        CHECK(line.find('\n') == std::string::npos);
        CHECK(line.find('\r') == std::string::npos);
    }

    CHECK(!cache.RebuildClear({-1.0, 3.0}, Color{}));
    const std::vector<std::string> failed = cache.DiagnosticsLines();
    CHECK(failed.size() == 3);
    CHECK(failed[1].find("build_error value=\"") == 0);
    CHECK(failed[2].find("refusal[0]") == 0);
}

void CandidatePublicationRejectsFailedAndStaleBuilds() {
    IslandFrameCache published;
    CHECK(published.RebuildClear({3.0, 2.0}, Color{0xff, 1, 2, 3}));
    CHECK(published.generation() == 1);

    IslandFrameCache first_candidate;
    CHECK(first_candidate.RebuildClear({3.0, 2.0},
                                       Color{0xff, 0x10, 0x20, 0x30}));
    CHECK(published.PublishFrom(std::move(first_candidate), 1));
    CHECK(!first_candidate.ready());
    CHECK(published.generation() == 2);
    const Surface first = PresentToSurface(published, Color{0xff, 9, 9, 9});

    IslandFrameCache failed_candidate;
    CHECK(!failed_candidate.RebuildClear({-1.0, 2.0},
                                         Color{0xff, 0x40, 0x50, 0x60}));
    CHECK(!published.PublishFrom(std::move(failed_candidate), 2));
    CHECK(published.generation() == 2);
    CHECK(PresentToSurface(published, Color{0xff, 8, 8, 8}).pixels() ==
          first.pixels());

    IslandFrameCache stale_candidate;
    IslandFrameCache newer_candidate;
    CHECK(stale_candidate.RebuildClear({3.0, 2.0},
                                       Color{0xff, 0x70, 0x80, 0x90}));
    CHECK(newer_candidate.RebuildClear({3.0, 2.0},
                                       Color{0xff, 0xa0, 0xb0, 0xc0}));
    CHECK(published.PublishFrom(std::move(newer_candidate), 2));
    CHECK(published.generation() == 3);
    const Surface newer = PresentToSurface(published, Color{0xff, 7, 7, 7});
    CHECK(!published.PublishFrom(std::move(stale_candidate), 2));
    CHECK(stale_candidate.ready());
    CHECK(published.generation() == 3);
    CHECK(PresentToSurface(published, Color{0xff, 6, 6, 6}).pixels() ==
          newer.pixels());

    CHECK(!published.PublishFrom(std::move(published), 3));
    CHECK(published.ready());
    CHECK(published.generation() == 3);
}

void PremultipliedCandidateIsNotDarkenedTwice() {
    IslandFrameCache cache;
    const Color half{0x80, 0xc8, 0x64, 0x32};
    CHECK(cache.RebuildClear({2.0, 1.0}, half));
    const Surface painted = PresentToSurface(cache, Color{0, 0, 0, 0});
    CHECK(painted.pixels().size() == 2);
    for (std::uint32_t pixel : painted.pixels()) CHECK(pixel == Pack(half));
}

void LayeredChildCompositesOverItsParent() {
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = LayeredTestWindowProc;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpszClassName = kLayeredTestClass;
    const ATOM atom = RegisterClassExW(&window_class);
    CHECK(atom != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS);

    HWND parent = CreateWindowExW(0, kLayeredTestClass, L"", WS_POPUP,
                                  40, 40, 8, 4, nullptr, nullptr,
                                  GetModuleHandleW(nullptr), nullptr);
    CHECK(parent != nullptr);
    HWND child = nullptr;
    if (parent) {
        // Paint the parent before mapping the layered child. A parent DC can
        // exclude an already-visible child from its clip region, in which
        // case there would be no parent pixels for alpha zero to reveal.
        ShowWindow(parent, SW_SHOWNOACTIVATE);
        UpdateWindow(parent);
        child = CreateWindowExW(WS_EX_LAYERED, kLayeredTestClass, L"",
                                WS_CHILD, 0, 0, 8, 4, parent,
                                nullptr, GetModuleHandleW(nullptr), nullptr);
    }
    CHECK(child != nullptr);

    if (parent && child) {
        Border overlay;
        overlay.set_width(4.0);
        overlay.set_horizontal_alignment(HorizontalAlignment::Left);
        overlay.set_background_brush(
            BrushValue{true, true, Color{0xff, 0x90, 0x80, 0x70}});
        overlay.Measure({8.0, 4.0});
        overlay.Arrange({0.0, 0.0, 8.0, 4.0});
        IslandFrameCache cache;
        CHECK(cache.Rebuild(overlay, {8.0, 4.0}, Color{}));

        POINT origin{0, 0};
        CHECK(ClientToScreen(parent, &origin));

        // First the island's own startup order: publish a complete frame into
        // the child before it is mapped, then map it. Nothing about that is
        // this project's to decide -- UpdateLayeredWindow is documented to
        // work on a window that is not yet visible -- so the pixels are only
        // asserted on a loader the probe measured delivering them.
        const FramePresentResult result =
            cache.PresentLayeredChild(child, origin);
        CHECK(result.presented);
        CHECK(result.error == ERROR_SUCCESS);
        ShowWindow(child, SW_SHOWNOACTIVATE);
        GdiFlush();

        // A screen DC observes the compositor's result. CAPTUREBLT is
        // deliberately absent: on Wine it asks for the layered source surface
        // itself, whose alpha-zero RGB is black, rather than the parent pixels
        // visible through that surface.
        HDC screen = GetDC(nullptr);
        CHECK(screen != nullptr);
        if (screen) {
            for (int y = 0; y < 4; ++y) {
                for (int x = 0; x < 4; ++x)
                    CHECK_LAYERED_PIXEL_ACROSS_MAP(
                        GetPixel(screen, origin.x + x, origin.y + y) ==
                        RGB(0x90, 0x80, 0x70));
                for (int x = 4; x < 8; ++x)
                    CHECK_LAYERED_PIXEL_ACROSS_MAP(
                        GetPixel(screen, origin.x + x, origin.y + y) ==
                        RGB(0x11, 0x22, 0x33));
            }
        }

        // Then the composition contract itself, on the mapped child: this is
        // the steady-state path every frame after the first takes, and it is
        // what "composites over its parent" means. The frame is the same one;
        // only the publication is new.
        const FramePresentResult republished =
            cache.PresentLayeredChild(child, origin);
        CHECK(republished.presented);
        CHECK(republished.error == ERROR_SUCCESS);
        CHECK(cache.generation() == 1);  // Presenting never rebuilds.
        GdiFlush();
        if (screen) {
            for (int y = 0; y < 4; ++y) {
                for (int x = 0; x < 4; ++x)
                    CHECK_LAYERED_PIXEL(GetPixel(screen, origin.x + x, origin.y + y) ==
                                        RGB(0x90, 0x80, 0x70));
                for (int x = 4; x < 8; ++x)
                    CHECK_LAYERED_PIXEL(GetPixel(screen, origin.x + x, origin.y + y) ==
                                        RGB(0x11, 0x22, 0x33));
            }
        }
        if (screen) ReleaseDC(nullptr, screen);

        HWND ordinary_child = CreateWindowExW(
            0, kLayeredTestClass, L"", WS_CHILD, 0, 0, 8, 4, parent,
            nullptr, GetModuleHandleW(nullptr), nullptr);
        CHECK(ordinary_child != nullptr);
        if (ordinary_child) {
            const FramePresentResult refused =
                cache.PresentLayeredChild(ordinary_child, origin);
            CHECK(!refused.presented);
            CHECK(refused.error == ERROR_INVALID_PARAMETER);
            DestroyWindow(ordinary_child);
        }
    }

    if (child) DestroyWindow(child);
    if (parent) DestroyWindow(parent);
    if (atom) UnregisterClassW(kLayeredTestClass, GetModuleHandleW(nullptr));
}

void PresentationFailuresAreContained() {
    IslandFrameCache empty;
    FramePresentResult result = empty.Present(nullptr);
    CHECK(!result.presented);
    CHECK(result.error == ERROR_INVALID_STATE);

    Border root;
    root.Measure({2.0, 2.0});
    root.Arrange({0.0, 0.0, 2.0, 2.0});
    CHECK(empty.Rebuild(root, {2.0, 2.0}, Color{0xff, 0, 0, 0}));
    result = empty.Present(nullptr);
    CHECK(!result.presented);
    CHECK(result.error == ERROR_INVALID_HANDLE);
}

// A run this prefix can actually put glyphs on a surface with, or nothing.
//
// The refusal the caller checks is about the *destination*, and it sits behind
// two earlier ones: DirectWrite answers "could not resolve any requested
// family" for a family that is not installed, and the renderer answers "no
// shaped advances" for a run layout never measured. Neither of those reaches
// the ClearType coverage decision, so this measures a run all the way to ink
// on an opaque surface and hands back the one that got there. A prefix with no
// such family makes the caller skip by name instead of asserting on a refusal
// that came from somewhere else.
bool MeasureTransparencyProbeRun(TextOp& run) {
    static const char* const candidates[] = {"Segoe UI", "Liberation Sans", "Arial",
                                             "DejaVu Sans", "Tahoma", "Cascadia Mono"};
    std::string diagnostic;
    if (!InstallDirectWriteRuntimeTextProvider(diagnostic)) return false;
    const std::shared_ptr<RuntimeTextProvider> provider = GetRuntimeTextProvider();
    if (!provider) return false;

    for (const char* candidate : candidates) {
        RuntimeTextResult measured;
        diagnostic.clear();
        if (!provider->Layout({candidate, "Terminal", 12.0, 60.0, false, false}, measured,
                              diagnostic))
            continue;

        TextOp candidate_run;
        candidate_run.bounds = {0.0, 0.0, 60.0, 20.0};
        candidate_run.text = "Terminal";
        candidate_run.font_family = candidate;
        candidate_run.font_size = 12.0;
        candidate_run.baseline = measured.baseline;
        candidate_run.advances = measured.advances;

        Surface opaque(60, 20, Color{0xff, 0xff, 0xff, 0xff});
        const std::vector<std::uint32_t> before = opaque.pixels();
        diagnostic.clear();
        if (DrawDirectWriteTextRun(opaque, candidate_run, Color{0xff, 0x00, 0x00, 0x00},
                                   diagnostic) &&
            opaque.pixels() != before) {
            run = candidate_run;
            return true;
        }
    }
    return false;
}

void TransparentClearAndTextAreHandledWithoutInventingAlpha() {
    // ClearType coverage is measured against the destination, so a run that
    // lands on non-opaque pixels has to be refused by name and leave them
    // exactly as they were. Inventing alpha there is the one thing a layered
    // island frame cannot survive: it would put opaque glyph pixels into a
    // region the frame declared transparent.
    TextOp probe;
    if (!MeasureTransparencyProbeRun(probe)) {
        std::cerr << "island_frame_cache_test.cpp: SKIP: transparent-destination text "
                     "refusal -- no probe family reaches ink on an opaque surface in this "
                     "prefix, so a run here is refused for its family rather than for its "
                     "destination\n";
        ++skipped;
    } else {
        DibTarget target(60, 20);
        CHECK(target.valid());
        GdiTextBackend backend(target);
        Surface transparent(60, 20, Color{0, 0x12, 0x34, 0x56});
        const std::vector<std::uint32_t> before = transparent.pixels();
        std::vector<std::string> failures;
        backend.DrawRuns(transparent, {probe}, Color{0xff, 0xff, 0xff, 0xff}, failures);
        CHECK(failures.size() == 1);
        if (failures.size() == 1)
            CHECK(failures[0].find("non-opaque pixels") != std::string::npos);
        CHECK(transparent.pixels() == before);
    }

    IslandFrameCache cache;
    Border root;
    root.Measure({3.0, 2.0});
    root.Arrange({0.0, 0.0, 3.0, 2.0});
    CHECK(cache.Rebuild(root, {3.0, 2.0}, Color{0, 0x21, 0x43, 0x65}));
    CHECK(cache.has_transparency());
    const Color destination{0xff, 1, 2, 3};
    const Surface presented = PresentToSurface(cache, destination);
    for (std::uint32_t pixel : presented.pixels())
        CHECK(pixel == Pack(destination));
}

}  // namespace

// The scene record is what a checker outside the process reads to learn what
// the committed frame was built from. It has to describe *this* frame: a
// cleared frame must not present the previous frame's nodes as its own, and a
// refused rebuild must keep the record that goes with the pixels still on
// screen.
void SceneRecordsDescribeTheCommittedFrame() {
    IslandFrameCache cache;
    Grid root;
    root.set_background_brush(
        BrushValue{true, true, Color{0xff, 0x0c, 0x0c, 0x0c}});
    auto child = std::make_unique<Border>();
    child->set_background_brush(
        BrushValue{true, true, Color{0xff, 0x33, 0x66, 0x99}});
    child->set_width(4.0);
    child->set_height(2.0);
    root.AddChild(std::move(child));
    root.Measure({8.0, 6.0});
    root.Arrange({0.0, 0.0, 8.0, 6.0});
    CHECK(cache.Rebuild(root, {8.0, 6.0}, Color{}));

    CHECK(cache.scene_nodes().size() >= 2);
    CHECK(cache.scene_node_total() == cache.scene_nodes().size());
    CHECK(cache.scene_nodes().front().type == root.TypeName());
    CHECK(cache.scene_nodes().front().actual.width == 8.0);
    CHECK(cache.scene_nodes().front().visible);
    CHECK(!cache.scene_nodes().front().path.empty());

    bool recorded_root_fill = false;
    bool recorded_child_fill = false;
    for (const FrameFillRecord& fill : cache.scene_fills()) {
        if (fill.color.r == 0x0c && fill.color.g == 0x0c && fill.color.b == 0x0c &&
            fill.color.a == 0xff && fill.bounds.width == 8.0 &&
            fill.bounds.height == 6.0) {
            recorded_root_fill = true;
        }
        if (fill.color.r == 0x33 && fill.color.g == 0x66 && fill.color.b == 0x99 &&
            fill.bounds.width == 4.0 && fill.bounds.height == 2.0) {
            recorded_child_fill = true;
        }
    }
    CHECK(recorded_root_fill);
    CHECK(recorded_child_fill);
    CHECK(cache.scene_fill_total() == cache.scene_fills().size());

    // A failed rebuild keeps the frame, so it must keep the frame's record.
    const std::size_t nodes = cache.scene_nodes().size();
    CHECK(!cache.Rebuild(root, {-1.0, 6.0}, Color{}));
    CHECK(cache.scene_nodes().size() == nodes);

    // A cleared frame was built from nothing and says so.
    CHECK(cache.RebuildClear({8.0, 6.0}, Color{}));
    CHECK(cache.scene_nodes().empty());
    CHECK(cache.scene_fills().empty());
    CHECK(cache.scene_texts().empty());
    CHECK(cache.scene_node_total() == 0);
}

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--diagnostics-only") {
        DiagnosticsDescribeTheCommittedFrameAndFailedAttempt();
        DiagnosticsAreOneBoundedDeterministicLinePerRefusal();
        if (failures == 0) {
            std::cout << "island frame cache diagnostics checks passed\n";
            return 0;
        }
        std::cerr << failures << " island frame cache diagnostics check(s) failed\n";
        return 1;
    }

    layered_child_presentation = MeasureLayeredChildPresentation();

    ClearCoversTheWholeFrameAndPresentDoesNotRebuild();
    SceneRecordsDescribeTheCommittedFrame();
    RebuildCommitsACompleteNewFrame();
    ClearOnlyRebuildReplacesContentAtomically();
    DiagnosticsDescribeTheCommittedFrameAndFailedAttempt();
    DiagnosticsAreOneBoundedDeterministicLinePerRefusal();
    CandidatePublicationRejectsFailedAndStaleBuilds();
    PremultipliedCandidateIsNotDarkenedTwice();
    LayeredChildCompositesOverItsParent();
    PresentationFailuresAreContained();
    TransparentClearAndTextAreHandledWithoutInventingAlpha();

    if (skipped > 0) {
        std::cout << skipped
                  << " island frame cache check(s) skipped by name after measurement\n";
    }
    if (failures == 0) {
        std::cout << "island frame cache checks passed\n";
        return 0;
    }
    std::cerr << failures << " island frame cache check(s) failed\n";
    return 1;
}
