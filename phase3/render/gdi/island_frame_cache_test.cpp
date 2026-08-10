#include <windows.h>

#include <iostream>
#include <memory>

#include "border.h"
#include "grid.h"
#include "island_frame_cache.h"
#include "surface.h"

using namespace openxaml;
using namespace openxaml::render;

namespace {

int failures = 0;
constexpr wchar_t kLayeredTestClass[] = L"OpenXaml.IslandFrameCache.LayeredTest";

void Check(bool condition, const char* what, int line) {
    if (condition) return;
    std::cerr << "island_frame_cache_test.cpp:" << line
              << ": CHECK failed: " << what << "\n";
    ++failures;
}

#define CHECK(condition) Check(static_cast<bool>(condition), #condition, __LINE__)

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
        const FramePresentResult result =
            cache.PresentLayeredChild(child, origin);
        CHECK(result.presented);
        CHECK(result.error == ERROR_SUCCESS);
        ShowWindow(child, SW_SHOWNOACTIVATE);
        GdiFlush();

        HDC screen = GetDC(nullptr);
        CHECK(screen != nullptr);
        if (screen) {
            // A screen DC observes the compositor's result. CAPTUREBLT is
            // deliberately absent: on Wine it asks for the layered source
            // surface itself, whose alpha-zero RGB is black, rather than the
            // parent pixels visible through that surface.
            for (int y = 0; y < 4; ++y) {
                for (int x = 0; x < 4; ++x)
                    CHECK(GetPixel(screen, origin.x + x, origin.y + y) ==
                          RGB(0x90, 0x80, 0x70));
                for (int x = 4; x < 8; ++x)
                    CHECK(GetPixel(screen, origin.x + x, origin.y + y) ==
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

void TransparentClearAndTextAreHandledWithoutInventingAlpha() {
    DibTarget target(6, 4);
    CHECK(target.valid());
    GdiTextBackend backend(target);
    Surface transparent(6, 4, Color{0, 0x12, 0x34, 0x56});
    const std::vector<std::uint32_t> before = transparent.pixels();
    TextOp run;
    run.bounds = {0.0, 0.0, 6.0, 4.0};
    run.text = "x";
    run.font_family = "this family must not be queried";
    run.font_size = 12.0;
    run.baseline = 3.0;
    run.advances = {4.0};
    std::vector<std::string> failures;
    backend.DrawRuns(transparent, {run}, Color{0xff, 0xff, 0xff, 0xff}, failures);
    CHECK(failures.size() == 1);
    CHECK(failures[0].find("intersects non-opaque pixels") != std::string::npos);
    CHECK(transparent.pixels() == before);

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

    if (failures == 0) {
        std::cout << "island frame cache checks passed\n";
        return 0;
    }
    std::cerr << failures << " island frame cache check(s) failed\n";
    return 1;
}
