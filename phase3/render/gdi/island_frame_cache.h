// A retained desktop-island frame, prepared before WM_PAINT.
//
// Rebuild is the only method that sees an Element. It compiles the arranged
// tree, rasterizes it, and commits a complete GDI DIB. After it returns the
// cache owns pixels and value diagnostics only: no Element, COM object, brush,
// font or display-list pointer survives. An opaque/offscreen WM_PAINT can call
// Present, while a transparent desktop island publishes the same immutable
// DIB through PresentLayeredChild outside ordinary HDC painting.

#ifndef OPENXAML_RENDER_GDI_ISLAND_FRAME_CACHE_H
#define OPENXAML_RENDER_GDI_ISLAND_FRAME_CACHE_H

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "display_list.h"
#include "gdi_target.h"

namespace openxaml {
namespace render {

struct FramePresentResult {
    bool presented = false;
    DWORD error = ERROR_SUCCESS;
};

class IslandFrameCache {
public:
    IslandFrameCache() = default;
    ~IslandFrameCache() = default;

    IslandFrameCache(const IslandFrameCache&) = delete;
    IslandFrameCache& operator=(const IslandFrameCache&) = delete;

    // Builds into temporary state and commits only after the complete surface
    // is resident in a DIB. A failed rebuild leaves the previous frame and its
    // generation intact, while last_build_error describes the failed attempt.
    bool Rebuild(const Element& arranged_root, Size surface, Color clear) noexcept;

    // Commits a complete frame containing only the caller's clear color. This
    // is the null-content counterpart to Rebuild: it requires no Element and
    // prevents a host from replaying stale content after content is detached.
    bool RebuildClear(Size surface, Color clear) noexcept;

    // Publishes an already-built temporary cache only if the destination has
    // not changed since the host began that build. This is the commit point
    // for layout callbacks that can re-enter the host and publish a newer
    // frame while an outer build is still in progress. Candidate generation
    // numbers are deliberately ignored; the destination owns its one
    // monotonically increasing committed sequence.
    bool PublishFrom(IslandFrameCache&& candidate,
                     std::uint64_t expected_generation) noexcept;

    // An offscreen/opaque-HWND presentation path. It performs no layout,
    // scene compilation, rasterization or allocation and never throws. GDI
    // AlphaBlend into an ordinary child HDC does *not* make that child
    // transparent to its parent; desktop-island hosts must use
    // PresentLayeredChild instead.
    FramePresentResult Present(HDC destination, int x = 0, int y = 0) const noexcept;

    // Presents the entire cached premultiplied surface through a real layered
    // child window. `layered_child` must have WS_CHILD | WS_EX_LAYERED and
    // `screen_origin` is the desired child client origin in screen
    // coordinates (normally ClientToScreen(parent, {0, 0})). The call updates
    // the child's position, size and pixels as one UpdateLayeredWindow pass.
    FramePresentResult PresentLayeredChild(HWND layered_child,
                                           POINT screen_origin) const noexcept;

    bool ready() const { return ready_; }
    std::uint64_t generation() const { return generation_; }
    int width() const { return width_; }
    int height() const { return height_; }
    bool has_transparency() const { return has_transparency_; }

    const std::string& last_build_error() const { return last_build_error_; }
    const std::vector<Refusal>& refusals() const { return refusals_; }
    const std::vector<std::string>& text_failures() const { return text_failures_; }
    const std::vector<RenderIssue>& render_issues() const { return render_issues_; }

    // A deterministic, single-line snapshot of the committed frame and all
    // named no-draw diagnostics. Hosts may write this to an opt-in debug sink
    // without retaining scene, Element, or COM state.
    std::string DiagnosticsText() const;

    // Wine bounds the text it displays for one OutputDebugString call. These
    // independently writable lines keep every diagnostic discoverable: the
    // first line is a frame summary and each remaining line describes exactly
    // one build error, refusal, text failure, or replay issue. Long values are
    // deterministically abbreviated, and no line exceeds this byte count.
    // Leaves room for the host's "OpenXaml frame diagnostic " prefix within
    // Wine's bounded OutputDebugString rendering.
    static constexpr std::size_t kMaxDiagnosticsLineLength = 220;
    std::vector<std::string> DiagnosticsLines() const;

private:
    bool CommitFrame(Surface&& pixels, std::unique_ptr<DibTarget>&& dib,
                     std::vector<Refusal>&& refusals,
                     std::vector<std::string>&& text_failures,
                     std::vector<RenderIssue>&& render_issues);

    std::unique_ptr<DibTarget> dib_;
    bool ready_ = false;
    std::uint64_t generation_ = 0;
    int width_ = 0;
    int height_ = 0;
    bool has_transparency_ = false;
    std::string last_build_error_;
    std::vector<Refusal> refusals_;
    std::vector<std::string> text_failures_;
    std::vector<RenderIssue> render_issues_;
};

}  // namespace render
}  // namespace openxaml

#endif  // OPENXAML_RENDER_GDI_ISLAND_FRAME_CACHE_H
