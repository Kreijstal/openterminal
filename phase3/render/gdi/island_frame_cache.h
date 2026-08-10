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

// A value-only record of the scene a committed frame was rasterized from.
//
// The cache deliberately retains no scene, Element or brush after Rebuild.
// These are the same kind of state as the refusals beside them -- copied
// strings and numbers -- so a host can publish exactly what it committed
// without holding anything alive. They exist so that "what was rendered" is
// answerable from outside the process, by a checker that never links the
// renderer, rather than only from a picture of the result.
struct FrameNodeRecord {
    std::string path;
    std::string type;
    Rect slot;          // in the parent's coordinates
    Size actual;        // the render size
    double origin_x = 0.0;  // the accumulated render origin, in surface space
    double origin_y = 0.0;
    double opacity = 1.0;
    bool visible = true;
    bool has_layout_storage = false;
    std::size_t commands = 0;
};

// One solid rectangle the frame painted, in surface coordinates, with the
// colour it was painted in. `what` says which part of the element it is.
struct FrameFillRecord {
    std::string path;
    std::string what;
    Rect bounds;
    Color color;
};

// One text run's box, in surface coordinates. A checker uses these to know
// which points of the surface a fill colour alone does not predict.
struct FrameTextRecord {
    std::string path;
    Rect bounds;
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
    // The committed frame's scene, as values. A frame built from no content
    // has all three empty; a frame that was rebuilt and refused keeps the
    // previous frame's records, exactly as it keeps the previous pixels.
    const std::vector<FrameNodeRecord>& scene_nodes() const { return scene_nodes_; }
    const std::vector<FrameFillRecord>& scene_fills() const { return scene_fills_; }
    const std::vector<FrameTextRecord>& scene_texts() const { return scene_texts_; }
    // How many nodes and fills the scene had before this cap was applied.
    std::size_t scene_node_total() const { return scene_node_total_; }
    std::size_t scene_fill_total() const { return scene_fill_total_; }
    // A frame is not allowed to make an unbounded record. Everything past
    // these counts is dropped and reported by the totals above.
    static constexpr std::size_t kMaxSceneRecords = 512;

    // The scene of one frame, before it is committed. Public because the
    // compilation that produces it is a free function beside Rebuild rather
    // than a member: it reads a display list and writes values, and has no
    // business seeing the cache's frame state.
    struct SceneRecords {
        std::vector<FrameNodeRecord> nodes;
        std::vector<FrameFillRecord> fills;
        std::vector<FrameTextRecord> texts;
        std::size_t node_total = 0;
        std::size_t fill_total = 0;
    };
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
                     std::vector<RenderIssue>&& render_issues,
                     SceneRecords&& scene);

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
    std::vector<FrameNodeRecord> scene_nodes_;
    std::vector<FrameFillRecord> scene_fills_;
    std::vector<FrameTextRecord> scene_texts_;
    std::size_t scene_node_total_ = 0;
    std::size_t scene_fill_total_ = 0;
};

}  // namespace render
}  // namespace openxaml

#endif  // OPENXAML_RENDER_GDI_ISLAND_FRAME_CACHE_H
