#include "island_frame_cache.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <string_view>
#include <utility>

#include "case_runner.h"
#include "surface.h"

namespace openxaml {
namespace render {
namespace {

bool TryGetExtent(Size surface, PixelRect& extent, std::string& error) {
    if (!std::isfinite(surface.width) || !std::isfinite(surface.height) ||
        surface.width < 0.0 || surface.height < 0.0 ||
        surface.width > static_cast<double>(std::numeric_limits<int>::max()) ||
        surface.height > static_cast<double>(std::numeric_limits<int>::max())) {
        error = "the frame extent is not a finite, non-negative DIB size";
        return false;
    }

    extent = SnapRect(Rect{0.0, 0.0, surface.width, surface.height});
    if (extent.left != 0 || extent.top != 0 || extent.right < 0 || extent.bottom < 0) {
        error = "the frame extent does not map to a non-negative DIB";
        return false;
    }
    return true;
}

const char* RenderIssueName(RenderIssueCode code) {
    switch (code) {
        case RenderIssueCode::InvalidScene: return "invalid-scene";
        case RenderIssueCode::UnsupportedTransform: return "unsupported-transform";
        case RenderIssueCode::UnsupportedClip: return "unsupported-clip";
        case RenderIssueCode::UnsupportedOpacity: return "unsupported-opacity";
        case RenderIssueCode::UnsupportedBrushAlpha: return "unsupported-brush-alpha";
        case RenderIssueCode::UnsupportedImageBrush: return "unsupported-image-brush";
        case RenderIssueCode::UnsupportedExternalSurface:
            return "unsupported-external-surface";
        case RenderIssueCode::MissingTextRasterizer: return "missing-text-rasterizer";
        case RenderIssueCode::TextRasterizerFailure: return "text-rasterizer-failure";
    }
    return "unknown";
}

void AppendQuoted(std::string& output, const std::string& value) {
    static constexpr char kHex[] = "0123456789abcdef";
    output.push_back('"');
    for (const unsigned char character : value) {
        switch (character) {
            case '\\': output += "\\\\"; break;
            case '"': output += "\\\""; break;
            case '\n': output += "\\n"; break;
            case '\r': output += "\\r"; break;
            case '\t': output += "\\t"; break;
            default:
                if (character < 0x20) {
                    output += "\\x";
                    output.push_back(kHex[character >> 4]);
                    output.push_back(kHex[character & 0x0f]);
                } else {
                    output.push_back(static_cast<char>(character));
                }
                break;
        }
    }
    output.push_back('"');
}

std::string EscapeCharacter(unsigned char character) {
    static constexpr char kHex[] = "0123456789abcdef";
    switch (character) {
        case '\\': return "\\\\";
        case '"': return "\\\"";
        case '\n': return "\\n";
        case '\r': return "\\r";
        case '\t': return "\\t";
        default:
            if (character < 0x20) {
                std::string escaped = "\\x00";
                escaped[2] = kHex[character >> 4];
                escaped[3] = kHex[character & 0x0f];
                return escaped;
            }
            return std::string(1, static_cast<char>(character));
    }
}

std::string EscapeForBoundedLine(const std::string& value,
                                 std::size_t content_budget,
                                 bool keep_suffix) {
    std::vector<std::string> characters;
    characters.reserve(value.size());
    std::size_t escaped_size = 0;
    for (const unsigned char character : value) {
        characters.push_back(EscapeCharacter(character));
        escaped_size += characters.back().size();
    }

    std::string escaped;
    escaped.reserve(escaped_size < content_budget ? escaped_size : content_budget);
    if (escaped_size <= content_budget) {
        for (const std::string& character : characters) escaped += character;
        return escaped;
    }

    static constexpr std::string_view kOmission = "...";
    if (content_budget <= kOmission.size())
        return std::string(kOmission.substr(0, content_budget));
    const std::size_t value_budget = content_budget - kOmission.size();
    if (keep_suffix) {
        std::size_t selected = 0;
        std::size_t first = characters.size();
        while (first > 0 && selected + characters[first - 1].size() <= value_budget) {
            --first;
            selected += characters[first].size();
        }
        escaped = kOmission;
        for (; first < characters.size(); ++first) escaped += characters[first];
    } else {
        std::size_t selected = 0;
        for (const std::string& character : characters) {
            if (selected + character.size() > value_budget) break;
            escaped += character;
            selected += character.size();
        }
        escaped += kOmission;
    }
    return escaped;
}

void AppendBoundedQuoted(std::string& output, const std::string& value,
                         std::size_t content_budget, bool keep_suffix = false) {
    output.push_back('"');
    output += EscapeForBoundedLine(value, content_budget, keep_suffix);
    output.push_back('"');
}

}  // namespace

namespace {

// Copies the scene a frame was rasterized from into plain values.
//
// Index order is the display list's own walk order, which is paint order, so a
// checker can resolve which fill covers a point by taking the last one that
// does. Nothing here retains a scene node, an Element or a brush.
IslandFrameCache::SceneRecords RecordSceneOf(const DisplayList& list) {
    IslandFrameCache::SceneRecords records;
    records.node_total = list.geometry.size();
    records.fill_total = list.rects.size();
    const std::size_t node_count =
        std::min(list.geometry.size(), IslandFrameCache::kMaxSceneRecords);
    records.nodes.reserve(node_count);
    for (std::size_t index = 0; index < node_count; ++index) {
        const NodeGeometry& geometry = list.geometry[index];
        FrameNodeRecord record;
        record.path = geometry.path;
        record.type = geometry.type;
        record.slot = geometry.slot;
        record.actual = geometry.actual;
        record.origin_x = geometry.abs_x;
        record.origin_y = geometry.abs_y;
        record.opacity = geometry.opacity;
        record.visible = geometry.visible;
        record.has_layout_storage = geometry.has_layout_storage;
        if (list.scene) {
            if (const VisualNode* node = list.scene->Find(geometry.id);
                node && node->content) {
                record.commands = node->content->commands.size();
            }
        }
        records.nodes.push_back(std::move(record));
    }

    const std::size_t fill_count =
        std::min(list.rects.size(), IslandFrameCache::kMaxSceneRecords);
    records.fills.reserve(fill_count);
    for (std::size_t index = 0; index < fill_count; ++index) {
        const RectOp& rect = list.rects[index];
        records.fills.push_back(
            FrameFillRecord{rect.path, rect.what, rect.bounds, rect.color});
    }

    const std::size_t text_count =
        std::min(list.texts.size(), IslandFrameCache::kMaxSceneRecords);
    records.texts.reserve(text_count);
    for (std::size_t index = 0; index < text_count; ++index)
        records.texts.push_back(
            FrameTextRecord{list.texts[index].path, list.texts[index].bounds});

    const std::size_t external_count =
        std::min(list.externals.size(), IslandFrameCache::kMaxSceneRecords);
    records.external_surfaces.reserve(external_count);
    for (std::size_t index = 0; index < external_count; ++index) {
        const ExternalSurfaceOp& external = list.externals[index];
        records.external_surfaces.push_back(FrameExternalSurfaceRecord{
            external.path, external.bounds, external.kind, external.generation});
    }
    return records;
}

}  // namespace

bool IslandFrameCache::Rebuild(const Element& arranged_root, Size surface, Color clear,
                               ExternalSurfaceReader* external_reader) noexcept {
    last_build_error_.clear();

    try {
        PixelRect extent;
        if (!TryGetExtent(surface, extent, last_build_error_)) return false;

        // Every object below is temporary until the final commit. DisplayList
        // owns strings, commands and an immutable SceneSnapshot, never Element
        // pointers, and is destroyed before this method returns.
        DisplayList display_list = Build(arranged_root, surface);

        std::unique_ptr<DibTarget> next_dib;
        std::vector<std::string> next_text_failures;
        std::vector<RenderIssue> next_render_issues;
        Surface pixels(0, 0, clear);

        if (extent.right > 0 && extent.bottom > 0) {
            next_dib = std::make_unique<DibTarget>(extent.right, extent.bottom);
            if (!next_dib->valid()) {
                last_build_error_ = "GDI could not allocate the island frame DIB";
                return false;
            }
            GdiTextBackend text_backend(*next_dib);
            pixels = RasterizeDisplayList(display_list, &text_backend, clear,
                                          ProbeInkColor(), next_text_failures,
                                          next_render_issues, external_reader);
            if (pixels.width() != extent.right || pixels.height() != extent.bottom) {
                last_build_error_ = "the rasterizer returned a surface with the wrong extent";
                return false;
            }
        } else {
            // A minimized island has a legitimate complete empty frame. There
            // is no GDI allocation and Present becomes a successful no-op.
            pixels = RasterizeDisplayList(display_list, nullptr, clear,
                                          ProbeInkColor(), next_text_failures,
                                          next_render_issues, external_reader);
            if (pixels.width() != extent.right || pixels.height() != extent.bottom) {
                last_build_error_ = "the degenerate frame rasterized to the wrong extent";
                return false;
            }
        }

        return CommitFrame(std::move(pixels), std::move(next_dib),
                           std::move(display_list.refusals),
                           std::move(next_text_failures),
                           std::move(next_render_issues),
                           RecordSceneOf(display_list));
    } catch (const std::exception& error) {
        last_build_error_ = error.what();
    } catch (...) {
        last_build_error_ = "an unknown failure occurred while rebuilding the island frame";
    }
    return false;
}

bool IslandFrameCache::RebuildClear(Size surface, Color clear) noexcept {
    last_build_error_.clear();

    try {
        PixelRect extent;
        if (!TryGetExtent(surface, extent, last_build_error_)) return false;

        std::unique_ptr<DibTarget> next_dib;
        if (extent.right > 0 && extent.bottom > 0) {
            next_dib = std::make_unique<DibTarget>(extent.right, extent.bottom);
            if (!next_dib->valid()) {
                last_build_error_ = "GDI could not allocate the island frame DIB";
                return false;
            }
        }

        Surface pixels(extent.right, extent.bottom, clear);
        // A cleared frame has no scene. Saying so is the point: a checker must
        // not read the previous frame's record as a description of this one.
        return CommitFrame(std::move(pixels), std::move(next_dib), {}, {}, {},
                           SceneRecords{});
    } catch (const std::exception& error) {
        last_build_error_ = error.what();
    } catch (...) {
        last_build_error_ = "an unknown failure occurred while clearing the island frame";
    }
    return false;
}

bool IslandFrameCache::CommitFrame(Surface&& pixels, std::unique_ptr<DibTarget>&& dib,
                                   std::vector<Refusal>&& refusals,
                                   std::vector<std::string>&& text_failures,
                                   std::vector<RenderIssue>&& render_issues,
                                   SceneRecords&& scene) {
    bool next_has_transparency = false;
    for (std::uint32_t pixel : pixels.pixels()) {
        if ((pixel >> 24) != 0xff) {
            next_has_transparency = true;
            break;
        }
    }
    // Surface's single native invariant is premultiplied 0xAARRGGBB, exactly
    // what AlphaBlend and UpdateLayeredWindow consume. Never convert it again
    // here: doing so would darken every partially transparent frame twice.
    if (dib) dib->Load(pixels);

    if (generation_ == std::numeric_limits<std::uint64_t>::max()) {
        last_build_error_ = "the frame generation counter is exhausted";
        return false;
    }

    // Commit as one state transition. The old DIB remains alive until the new
    // one is complete, so a failed attempt never tears the frame WM_PAINT can
    // still present.
    const int next_width = pixels.width();
    const int next_height = pixels.height();
    dib_ = std::move(dib);
    width_ = next_width;
    height_ = next_height;
    has_transparency_ = next_has_transparency;
    refusals_ = std::move(refusals);
    text_failures_ = std::move(text_failures);
    render_issues_ = std::move(render_issues);
    scene_nodes_ = std::move(scene.nodes);
    scene_fills_ = std::move(scene.fills);
    scene_texts_ = std::move(scene.texts);
    scene_external_surfaces_ = std::move(scene.external_surfaces);
    scene_node_total_ = scene.node_total;
    scene_fill_total_ = scene.fill_total;
    ready_ = true;
    ++generation_;
    return true;
}

bool IslandFrameCache::PublishFrom(IslandFrameCache&& candidate,
                                   std::uint64_t expected_generation) noexcept {
    if (&candidate == this || !candidate.ready_ ||
        generation_ != expected_generation ||
        generation_ == std::numeric_limits<std::uint64_t>::max()) {
        return false;
    }

    // Every move below is non-throwing with std::allocator. Candidate remains
    // a valid, explicitly non-ready cache, so an accidentally repeated publish
    // cannot replay the same transaction.
    dib_ = std::move(candidate.dib_);
    width_ = candidate.width_;
    height_ = candidate.height_;
    has_transparency_ = candidate.has_transparency_;
    last_build_error_ = std::move(candidate.last_build_error_);
    refusals_ = std::move(candidate.refusals_);
    text_failures_ = std::move(candidate.text_failures_);
    render_issues_ = std::move(candidate.render_issues_);
    scene_nodes_ = std::move(candidate.scene_nodes_);
    scene_fills_ = std::move(candidate.scene_fills_);
    scene_texts_ = std::move(candidate.scene_texts_);
    scene_external_surfaces_ = std::move(candidate.scene_external_surfaces_);
    scene_node_total_ = candidate.scene_node_total_;
    scene_fill_total_ = candidate.scene_fill_total_;
    ready_ = true;
    ++generation_;

    candidate.ready_ = false;
    candidate.width_ = 0;
    candidate.height_ = 0;
    candidate.has_transparency_ = false;
    candidate.scene_node_total_ = 0;
    candidate.scene_fill_total_ = 0;
    return true;
}

std::string IslandFrameCache::DiagnosticsText() const {
    std::string output = "generation=" + std::to_string(generation_) +
        " ready=" + (ready_ ? "true" : "false") +
        " extent=" + std::to_string(width_) + "x" + std::to_string(height_) +
        " transparency=" + (has_transparency_ ? "true" : "false") +
        " refusals=" + std::to_string(refusals_.size()) +
        " text_failures=" + std::to_string(text_failures_.size()) +
        " render_issues=" + std::to_string(render_issues_.size());
    if (!last_build_error_.empty()) {
        output += " build_error=";
        AppendQuoted(output, last_build_error_);
    }
    for (std::size_t index = 0; index < refusals_.size(); ++index) {
        const Refusal& refusal = refusals_[index];
        output += " refusal[" + std::to_string(index) + "]={path=";
        AppendQuoted(output, refusal.path);
        output += ",feature=";
        AppendQuoted(output, refusal.feature);
        output += ",reason=";
        AppendQuoted(output, refusal.reason);
        output.push_back('}');
    }
    for (std::size_t index = 0; index < text_failures_.size(); ++index) {
        output += " text_failure[" + std::to_string(index) + "]=";
        AppendQuoted(output, text_failures_[index]);
    }
    for (std::size_t index = 0; index < render_issues_.size(); ++index) {
        const RenderIssue& issue = render_issues_[index];
        output += " render_issue[" + std::to_string(index) + "]={code=";
        output += RenderIssueName(issue.code);
        output += ",node=" + std::to_string(issue.node.value) + ",command=";
        output += issue.command_index == RenderIssue::kNoCommand
            ? "none" : std::to_string(issue.command_index);
        output += ",message=";
        AppendQuoted(output, issue.message);
        output.push_back('}');
    }
    return output;
}

std::vector<std::string> IslandFrameCache::DiagnosticsLines() const {
    std::vector<std::string> lines;
    lines.reserve(1 + (last_build_error_.empty() ? 0 : 1) + refusals_.size() +
                  text_failures_.size() + render_issues_.size());

    lines.push_back("generation=" + std::to_string(generation_) +
        " ready=" + (ready_ ? "true" : "false") +
        " extent=" + std::to_string(width_) + "x" + std::to_string(height_) +
        " transparency=" + (has_transparency_ ? "true" : "false") +
        " refusals=" + std::to_string(refusals_.size()) +
        " text_failures=" + std::to_string(text_failures_.size()) +
        " render_issues=" + std::to_string(render_issues_.size()));

    if (!last_build_error_.empty()) {
        std::string line = "build_error value=";
        AppendBoundedQuoted(line, last_build_error_, 190);
        lines.push_back(std::move(line));
    }
    for (std::size_t index = 0; index < refusals_.size(); ++index) {
        const Refusal& refusal = refusals_[index];
        std::string line = "refusal[" + std::to_string(index) + "] path=";
        AppendBoundedQuoted(line, refusal.path, 70, true);
        line += " feature=";
        AppendBoundedQuoted(line, refusal.feature, 32);
        line += " reason=";
        AppendBoundedQuoted(line, refusal.reason, 58);
        lines.push_back(std::move(line));
    }
    for (std::size_t index = 0; index < text_failures_.size(); ++index) {
        std::string line = "text_failure[" + std::to_string(index) + "] value=";
        AppendBoundedQuoted(line, text_failures_[index], 170);
        lines.push_back(std::move(line));
    }
    for (std::size_t index = 0; index < render_issues_.size(); ++index) {
        const RenderIssue& issue = render_issues_[index];
        std::string line = "render_issue[" + std::to_string(index) + "] code=";
        line += RenderIssueName(issue.code);
        line += " node=" + std::to_string(issue.node.value) + " command=";
        line += issue.command_index == RenderIssue::kNoCommand
            ? "none" : std::to_string(issue.command_index);
        line += " message=";
        AppendBoundedQuoted(line, issue.message, 90);
        lines.push_back(std::move(line));
    }

    // Fixed field budgets above leave room for the decimal form of every
    // size_t/uint64_t value. Keep the invariant explicit at this API boundary.
    for (std::string& line : lines) {
        if (line.size() > kMaxDiagnosticsLineLength)
            line.resize(kMaxDiagnosticsLineLength);
    }
    return lines;
}

FramePresentResult IslandFrameCache::Present(HDC destination, int x, int y) const noexcept {
    if (!ready_) return {false, ERROR_INVALID_STATE};
    if (width_ == 0 || height_ == 0) return {true, ERROR_SUCCESS};
    if (!destination || !dib_ || !dib_->valid()) return {false, ERROR_INVALID_HANDLE};

    SetLastError(ERROR_SUCCESS);
    bool presented = false;
    if (has_transparency_) {
        const BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        presented = AlphaBlend(destination, x, y, width_, height_, dib_->dc(),
                               0, 0, width_, height_, blend) != FALSE;
    } else {
        presented = BitBlt(destination, x, y, width_, height_, dib_->dc(),
                           0, 0, SRCCOPY) != FALSE;
    }
    if (presented)
        return {true, ERROR_SUCCESS};
    DWORD error = GetLastError();
    if (error == ERROR_SUCCESS) error = ERROR_GEN_FAILURE;
    return {false, error};
}

FramePresentResult IslandFrameCache::PresentLayeredChild(
    HWND layered_child, POINT screen_origin) const noexcept {
    if (!ready_) return {false, ERROR_INVALID_STATE};
    if (!layered_child || !IsWindow(layered_child))
        return {false, ERROR_INVALID_WINDOW_HANDLE};

    SetLastError(ERROR_SUCCESS);
    const LONG_PTR style = GetWindowLongPtrW(layered_child, GWL_STYLE);
    DWORD error = GetLastError();
    if (style == 0 && error != ERROR_SUCCESS) return {false, error};
    SetLastError(ERROR_SUCCESS);
    const LONG_PTR extended_style =
        GetWindowLongPtrW(layered_child, GWL_EXSTYLE);
    error = GetLastError();
    if (extended_style == 0 && error != ERROR_SUCCESS) return {false, error};
    if ((style & WS_CHILD) == 0 || (extended_style & WS_EX_LAYERED) == 0)
        return {false, ERROR_INVALID_PARAMETER};

    if (width_ == 0 || height_ == 0) {
        HWND parent = GetParent(layered_child);
        POINT parent_origin = screen_origin;
        if (!parent || !ScreenToClient(parent, &parent_origin)) {
            error = GetLastError();
            return {false, error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : error};
        }
        if (SetWindowPos(layered_child, nullptr, parent_origin.x, parent_origin.y,
                         0, 0, SWP_NOACTIVATE | SWP_NOZORDER)) {
            return {true, ERROR_SUCCESS};
        }
        error = GetLastError();
        return {false, error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : error};
    }
    if (!dib_ || !dib_->valid()) return {false, ERROR_INVALID_HANDLE};

    // MinGW models these legacy Win32 inputs as mutable pointers even though
    // UpdateLayeredWindow only reads them.
    SIZE size{width_, height_};
    POINT source{0, 0};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    SetLastError(ERROR_SUCCESS);
    if (UpdateLayeredWindow(layered_child, nullptr, &screen_origin, &size,
                            dib_->dc(), &source, 0, &blend, ULW_ALPHA)) {
        return {true, ERROR_SUCCESS};
    }
    error = GetLastError();
    return {false, error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : error};
}

}  // namespace render
}  // namespace openxaml
