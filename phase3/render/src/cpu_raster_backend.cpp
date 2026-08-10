#include "cpu_raster_backend.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace openxaml {
namespace render {
namespace {

using Polygon = std::vector<Point>;

struct ClipState {
    bool present = false;
    bool axis_aligned = false;
    Rect bounds{};
    Polygon polygon;
};

bool IsFinite(const Rect& rect) {
    return std::isfinite(rect.x) && std::isfinite(rect.y) && std::isfinite(rect.width) &&
           std::isfinite(rect.height);
}

bool HasValidExtent(const Rect& rect) {
    return IsFinite(rect) && rect.width >= 0.0 && rect.height >= 0.0 &&
           std::isfinite(rect.x + rect.width) && std::isfinite(rect.y + rect.height);
}

bool NearlyZero(double value) {
    return std::abs(value) <= 64.0 * std::numeric_limits<double>::epsilon();
}

bool IsTranslation(const Matrix3x2& matrix) {
    return std::isfinite(matrix.m11) && std::isfinite(matrix.m12) &&
           std::isfinite(matrix.m21) && std::isfinite(matrix.m22) &&
           std::isfinite(matrix.dx) && std::isfinite(matrix.dy) &&
           NearlyZero(matrix.m11 - 1.0) && NearlyZero(matrix.m12) &&
           NearlyZero(matrix.m21) && NearlyZero(matrix.m22 - 1.0);
}

bool IsAxisAligned(const Matrix3x2& matrix) {
    if (!std::isfinite(matrix.m11) || !std::isfinite(matrix.m12) ||
        !std::isfinite(matrix.m21) || !std::isfinite(matrix.m22) ||
        !std::isfinite(matrix.dx) || !std::isfinite(matrix.dy))
        return false;
    return (NearlyZero(matrix.m12) && NearlyZero(matrix.m21)) ||
           (NearlyZero(matrix.m11) && NearlyZero(matrix.m22));
}

Rect Translate(Rect rect, double x, double y) {
    rect.x += x;
    rect.y += y;
    return rect;
}

Rect Intersect(const Rect& left, const Rect& right) {
    const double x = std::max(left.x, right.x);
    const double y = std::max(left.y, right.y);
    const double far_x = std::min(left.x + left.width, right.x + right.width);
    const double far_y = std::min(left.y + left.height, right.y + right.height);
    return Rect{x, y, std::max(0.0, far_x - x), std::max(0.0, far_y - y)};
}

bool IsEmpty(const Rect& rect) { return rect.width <= 0.0 || rect.height <= 0.0; }

Polygon RectPolygon(const Rect& rect, const Matrix3x2& transform) {
    return {
        transform.TransformPoint({rect.x, rect.y}),
        transform.TransformPoint({rect.x + rect.width, rect.y}),
        transform.TransformPoint({rect.x + rect.width, rect.y + rect.height}),
        transform.TransformPoint({rect.x, rect.y + rect.height}),
    };
}

long double Cross(Point left, Point right, Point point) {
    const long double edge_x = static_cast<long double>(right.x) - left.x;
    const long double edge_y = static_cast<long double>(right.y) - left.y;
    const long double point_x = static_cast<long double>(point.x) - left.x;
    const long double point_y = static_cast<long double>(point.y) - left.y;
    return edge_x * point_y - edge_y * point_x;
}

bool PolygonArea(const Polygon& polygon, double& area) {
    area = 0.0;
    if (polygon.size() < 3) return true;
    long double twice_area = 0.0;
    const Point origin = polygon.front();
    for (std::size_t index = 0; index < polygon.size(); ++index) {
        const Point& here = polygon[index];
        const Point& next = polygon[(index + 1) % polygon.size()];
        if (!std::isfinite(here.x) || !std::isfinite(here.y)) return false;
        const long double here_x = static_cast<long double>(here.x) - origin.x;
        const long double here_y = static_cast<long double>(here.y) - origin.y;
        const long double next_x = static_cast<long double>(next.x) - origin.x;
        const long double next_y = static_cast<long double>(next.y) - origin.y;
        twice_area += here_x * next_y - here_y * next_x;
    }
    const long double result = twice_area * 0.5L;
    if (!std::isfinite(result) ||
        std::abs(result) > std::numeric_limits<double>::max())
        return false;
    area = static_cast<double>(result);
    return std::isfinite(area);
}

bool LineIntersection(Point start, Point end, Point clip_start, Point clip_end,
                      Point& intersection) {
    const long double direction_x = static_cast<long double>(end.x) - start.x;
    const long double direction_y = static_cast<long double>(end.y) - start.y;
    const long double edge_x = static_cast<long double>(clip_end.x) - clip_start.x;
    const long double edge_y = static_cast<long double>(clip_end.y) - clip_start.y;
    const long double denominator = edge_x * direction_y - edge_y * direction_x;
    const long double scale =
        (std::abs(edge_x) + std::abs(edge_y)) *
        (std::abs(direction_x) + std::abs(direction_y));
    if (!std::isfinite(denominator) || !std::isfinite(scale) ||
        std::abs(denominator) <=
            64.0L * std::numeric_limits<long double>::epsilon() * scale)
        return false;
    const long double from_x = static_cast<long double>(clip_start.x) - start.x;
    const long double from_y = static_cast<long double>(clip_start.y) - start.y;
    const long double t = (edge_x * from_y - edge_y * from_x) / denominator;
    const long double x = start.x + direction_x * t;
    const long double y = start.y + direction_y * t;
    if (!std::isfinite(t) || !std::isfinite(x) || !std::isfinite(y) ||
        std::abs(x) > std::numeric_limits<double>::max() ||
        std::abs(y) > std::numeric_limits<double>::max())
        return false;
    intersection = {static_cast<double>(x), static_cast<double>(y)};
    return std::isfinite(intersection.x) && std::isfinite(intersection.y);
}

bool IntersectConvex(Polygon subject, const Polygon& clip, Polygon& result) {
    result.clear();
    double clip_area = 0.0;
    double subject_area = 0.0;
    if (!PolygonArea(subject, subject_area) || !PolygonArea(clip, clip_area)) return false;
    if (subject.size() < 3 || clip.size() < 3 || NearlyZero(subject_area) ||
        NearlyZero(clip_area))
        return true;
    const long double orientation = clip_area >= 0.0 ? 1.0L : -1.0L;
    for (std::size_t edge_index = 0; edge_index < clip.size(); ++edge_index) {
        const Point clip_start = clip[edge_index];
        const Point clip_end = clip[(edge_index + 1) % clip.size()];
        Polygon input = std::move(subject);
        subject.clear();
        if (input.empty()) break;
        Point start = input.back();
        bool start_inside = orientation * Cross(clip_start, clip_end, start) >= -1e-12L;
        for (Point end : input) {
            const bool end_inside =
                orientation * Cross(clip_start, clip_end, end) >= -1e-12L;
            if (end_inside != start_inside) {
                Point intersection;
                if (!LineIntersection(start, end, clip_start, clip_end, intersection))
                    return false;
                subject.push_back(intersection);
            }
            if (end_inside) subject.push_back(end);
            start = end;
            start_inside = end_inside;
        }
    }
    double result_area = 0.0;
    if (!PolygonArea(subject, result_area)) return false;
    result = std::move(subject);
    return true;
}

Rect PolygonBounds(const Polygon& polygon) {
    if (polygon.empty()) return {};
    double left = polygon.front().x;
    double right = left;
    double top = polygon.front().y;
    double bottom = top;
    for (Point point : polygon) {
        left = std::min(left, point.x);
        right = std::max(right, point.x);
        top = std::min(top, point.y);
        bottom = std::max(bottom, point.y);
    }
    return {left, top, right - left, bottom - top};
}

bool IsFinite(const Matrix3x2& matrix) {
    return std::isfinite(matrix.m11) && std::isfinite(matrix.m12) &&
           std::isfinite(matrix.m21) && std::isfinite(matrix.m22) &&
           std::isfinite(matrix.dx) && std::isfinite(matrix.dy);
}

bool RasterPolygon(const Polygon& polygon, Color color, Surface& target) {
    double polygon_area = 0.0;
    if (!PolygonArea(polygon, polygon_area)) return false;
    if (polygon.size() < 3 || std::abs(polygon_area) <= 1e-15) return true;
    const Rect bounds = PolygonBounds(polygon);
    const auto floor_to_surface = [](double value, int extent) {
        if (value <= 0.0) return 0;
        if (value >= static_cast<double>(extent)) return extent;
        return static_cast<int>(std::floor(value));
    };
    const auto ceil_to_surface = [](double value, int extent) {
        if (value <= 0.0) return 0;
        if (value >= static_cast<double>(extent)) return extent;
        return static_cast<int>(std::ceil(value));
    };
    const int left = floor_to_surface(bounds.x, target.width());
    const int top = floor_to_surface(bounds.y, target.height());
    const int right = ceil_to_surface(bounds.x + bounds.width, target.width());
    const int bottom = ceil_to_surface(bounds.y + bounds.height, target.height());
    for (int y = top; y < bottom; ++y) {
        for (int x = left; x < right; ++x) {
            const Polygon pixel = RectPolygon(
                Rect{static_cast<double>(x), static_cast<double>(y), 1.0, 1.0},
                Matrix3x2::Identity());
            Polygon covered;
            if (!IntersectConvex(polygon, pixel, covered)) return false;
            double covered_area = 0.0;
            if (!PolygonArea(covered, covered_area)) return false;
            const double coverage = std::min(1.0, std::abs(covered_area));
            if (coverage > 1e-15) target.BlendPixel(x, y, color, coverage);
        }
    }
    return true;
}

class Walker {
public:
    Walker(const SceneSnapshot& scene, RasterResult& result, TextRasterizer* text_rasterizer,
           ExternalSurfaceReader* external_reader)
        : scene_(scene), result_(result), text_rasterizer_(text_rasterizer),
          external_reader_(external_reader) {}

    void Run() {
        const VisualNode* root = scene_.Find(scene_.root());
        if (!root) {
            Issue(RenderIssueCode::InvalidScene, scene_.root(), RenderIssue::kNoCommand,
                  "the scene root does not identify a node");
            return;
        }
        Walk(*root, Matrix3x2::Identity(), ClipState{}, result_.surface);
    }

private:
    void Issue(RenderIssueCode code, NodeId node, std::size_t command_index,
               std::string message) {
        result_.issues.push_back(RenderIssue{code, node, command_index, std::move(message)});
    }

    void Walk(const VisualNode& node, const Matrix3x2& parent_to_root,
              ClipState inherited_clip, Surface& target) {
        if (!visited_.insert(node.id.value).second) {
            Issue(RenderIssueCode::InvalidScene, node.id, RenderIssue::kNoCommand,
                  "a node was reached more than once; the retained scene is not a tree");
            return;
        }
        if (!node.visible) return;

        if (!node.unsupported_transform.empty()) {
            Issue(RenderIssueCode::UnsupportedTransform, node.id,
                  RenderIssue::kNoCommand,
                  "the retained " + node.unsupported_transform +
                      " cannot be lowered to the affine scene representation");
            return;
        }

        if (!std::isfinite(node.opacity) || node.opacity < 0.0 || node.opacity > 1.0) {
            Issue(RenderIssueCode::InvalidScene, node.id, RenderIssue::kNoCommand,
                  "node opacity is not a finite value in the range [0, 1]");
            return;
        }
        if (node.opacity == 0.0) return;

        const Matrix3x2 transform_to_root = node.local_transform.Then(parent_to_root);
        if (!IsFinite(transform_to_root)) {
            Issue(RenderIssueCode::InvalidScene, node.id, RenderIssue::kNoCommand,
                  "composed affine transform is not finite");
            return;
        }
        ClipState clip = inherited_clip;
        switch (node.clip.kind) {
            case Clip::Kind::None:
                break;
            case Clip::Kind::Rect: {
                if (!HasValidExtent(node.clip.bounds)) {
                    Issue(RenderIssueCode::InvalidScene, node.id, RenderIssue::kNoCommand,
                          "rectangular clip contains a non-finite value");
                    return;
                }
                const Polygon local_clip = RectPolygon(node.clip.bounds, transform_to_root);
                double local_clip_area = 0.0;
                if (!PolygonArea(local_clip, local_clip_area)) {
                    Issue(RenderIssueCode::InvalidScene, node.id,
                          RenderIssue::kNoCommand,
                          "transformed rectangular clip has non-finite vertices");
                    return;
                }
                const Rect local_clip_bounds = PolygonBounds(local_clip);
                if (!HasValidExtent(local_clip_bounds)) {
                    Issue(RenderIssueCode::InvalidScene, node.id, RenderIssue::kNoCommand,
                          "transformed rectangular clip is not representable");
                    return;
                }
                if (!clip.present) {
                    clip.polygon = local_clip;
                    clip.bounds = local_clip_bounds;
                    clip.axis_aligned = IsAxisAligned(transform_to_root);
                } else if (clip.axis_aligned && IsAxisAligned(transform_to_root)) {
                    clip.bounds = Intersect(clip.bounds, local_clip_bounds);
                    clip.polygon = RectPolygon(clip.bounds, Matrix3x2::Identity());
                } else {
                    Polygon intersection;
                    if (!IntersectConvex(clip.polygon, local_clip, intersection)) {
                        Issue(RenderIssueCode::InvalidScene, node.id,
                              RenderIssue::kNoCommand,
                              "rectangular clip intersection is numerically unstable");
                        return;
                    }
                    clip.polygon = std::move(intersection);
                    clip.bounds = PolygonBounds(clip.polygon);
                    clip.axis_aligned = false;
                }
                clip.present = true;
                if (clip.polygon.size() < 3 || IsEmpty(clip.bounds)) return;
                break;
            }
            default:
                Issue(RenderIssueCode::UnsupportedClip, node.id, RenderIssue::kNoCommand,
                      "the CPU reference backend implements rectangular clips only");
                return;
        }

        if (node.opacity != 1.0) {
            // Opacity belongs to the composed subtree, not to each draw. Paint
            // the node and all descendants at full strength into transparent
            // premultiplied storage, then apply opacity exactly once. This is
            // observably different for overlapping descendants.
            Surface layer(target.width(), target.height(), Color{0, 0, 0, 0});
            PaintSubtree(node, transform_to_root, clip, layer);
            target.CompositeLayer(layer, node.opacity);
            return;
        }

        PaintSubtree(node, transform_to_root, clip, target);
    }

    void PaintSubtree(const VisualNode& node, const Matrix3x2& transform_to_root,
                      const ClipState& clip, Surface& target) {

        if (node.content) {
            for (std::size_t index = 0; index < node.content->commands.size(); ++index) {
                const DrawCommand& command = node.content->commands[index];
                if (const auto* fill = std::get_if<LocalFillRect>(&command)) {
                    DrawFill(node.id, index, *fill, transform_to_root, clip, target);
                } else if (std::get_if<LocalImageBrushFill>(&command)) {
                    Issue(RenderIssueCode::UnsupportedImageBrush, node.id, index,
                          "ImageBrush source decoding and sampling are not implemented by "
                          "the CPU reference backend");
                } else if (const auto* text = std::get_if<LocalText>(&command)) {
                    DrawText(node.id, index, *text, transform_to_root, clip, target);
                } else if (const auto* external =
                               std::get_if<LocalExternalSurface>(&command)) {
                    DrawExternalSurface(node.id, index, *external, transform_to_root,
                                        clip, target);
                } else {
                    Issue(RenderIssueCode::InvalidScene, node.id, index,
                          "display list contains an unknown command alternative");
                }
            }
        }

        for (const VisualNode* child : scene_.OrderedChildren(node.id)) {
            if (!child) {
                Issue(RenderIssueCode::InvalidScene, node.id, RenderIssue::kNoCommand,
                      "the scene contains a missing child node");
                continue;
            }
            Walk(*child, transform_to_root, clip, target);
        }
    }

    void DrawFill(NodeId node, std::size_t index, const LocalFillRect& fill,
                  const Matrix3x2& transform_to_root, const ClipState& clip,
                  Surface& target) {
        if (!HasValidExtent(fill.bounds)) {
            Issue(RenderIssueCode::InvalidScene, node, index,
                  "fill rectangle does not have finite, non-negative bounds");
            return;
        }
        if (fill.color.a == 0 || IsEmpty(fill.bounds)) return;

        if (IsAxisAligned(transform_to_root) && (!clip.present || clip.axis_aligned)) {
            Rect bounds = transform_to_root.TransformRect(fill.bounds);
            if (!HasValidExtent(bounds)) {
                Issue(RenderIssueCode::InvalidScene, node, index,
                      "translated fill rectangle is not representable");
                return;
            }
            if (clip.present) bounds = Intersect(bounds, clip.bounds);
            bounds = Intersect(bounds, Rect{0.0, 0.0,
                                            static_cast<double>(target.width()),
                                            static_cast<double>(target.height())});
            if (!IsEmpty(bounds)) target.BlendRect(bounds, fill.color);
            return;
        }

        Polygon polygon = RectPolygon(fill.bounds, transform_to_root);
        double polygon_area = 0.0;
        if (!PolygonArea(polygon, polygon_area)) {
            Issue(RenderIssueCode::InvalidScene, node, index,
                  "transformed fill rectangle has non-finite vertices");
            return;
        }
        const Rect bounds = PolygonBounds(polygon);
        if (!HasValidExtent(bounds)) {
            Issue(RenderIssueCode::InvalidScene, node, index,
                  "transformed fill rectangle is not representable");
            return;
        }
        if (clip.present) {
            Polygon intersection;
            if (!IntersectConvex(std::move(polygon), clip.polygon, intersection)) {
                Issue(RenderIssueCode::InvalidScene, node, index,
                      "fill and clip intersection is numerically unstable");
                return;
            }
            polygon = std::move(intersection);
        }
        if (!RasterPolygon(polygon, fill.color, target)) {
            Issue(RenderIssueCode::InvalidScene, node, index,
                  "fill coverage is not finite and stable");
        }
    }

    void DrawText(NodeId node, std::size_t index, const LocalText& text,
                  const Matrix3x2& transform_to_root, const ClipState& clip,
                  Surface& target) {
        if (!HasValidExtent(text.bounds)) {
            Issue(RenderIssueCode::InvalidScene, node, index,
                  "text does not have finite, non-negative bounds");
            return;
        }
        if (text.text.empty() || IsEmpty(text.bounds)) return;
        if (!IsTranslation(transform_to_root)) {
            Issue(RenderIssueCode::UnsupportedTransform, node, index,
                  "transformed text requires an affine-aware glyph rasterizer contract");
            return;
        }
        if (clip.present && !clip.axis_aligned) {
            Issue(RenderIssueCode::UnsupportedClip, node, index,
                  "transformed text clipping requires a polygon-aware glyph rasterizer contract");
            return;
        }
        if (!text_rasterizer_) {
            Issue(RenderIssueCode::MissingTextRasterizer, node, index,
                  "text was present but no text rasterizer was supplied");
            return;
        }

        const Rect bounds =
            Translate(text.bounds, transform_to_root.dx, transform_to_root.dy);
        if (!HasValidExtent(bounds)) {
            Issue(RenderIssueCode::InvalidScene, node, index,
                  "translated text bounds are not representable");
            return;
        }
        if (clip.present && IsEmpty(Intersect(bounds, clip.bounds))) return;
        TextRasterRequest request{node, index, text, bounds, clip.present, clip.bounds};
        std::string message;
        if (!text_rasterizer_->DrawText(request, target, message)) {
            if (message.empty()) message = "the text rasterizer declined the request";
            Issue(RenderIssueCode::TextRasterizerFailure, node, index, std::move(message));
        }
    }

    // Composites producer-owned pixels into the frame at the rect layout
    // arranged for them.
    //
    // The pixels arrive already premultiplied and are composited source-over,
    // one for one, at whole pixels. They are never resampled: a swap chain is
    // sized by its producer to the panel it is bound to, and stretching what
    // it presented would put content on the screen the producer never drew.
    // Every case this cannot render exactly is named instead.
    void DrawExternalSurface(NodeId node, std::size_t index,
                             const LocalExternalSurface& external,
                             const Matrix3x2& transform_to_root, const ClipState& clip,
                             Surface& target) {
        if (!HasValidExtent(external.bounds)) {
            Issue(RenderIssueCode::InvalidScene, node, index,
                  "external surface rectangle does not have finite, non-negative bounds");
            return;
        }
        if (!external.source) {
            Issue(RenderIssueCode::UnsupportedExternalSurface, node, index,
                  "the retained external surface names no live source");
            return;
        }
        if (!external_reader_) {
            Issue(RenderIssueCode::UnsupportedExternalSurface, node, index,
                  "no external surface reader is bound to this frame, so producer-owned "
                  "content cannot be imported");
            return;
        }
        if (!IsTranslation(transform_to_root)) {
            Issue(RenderIssueCode::UnsupportedExternalSurface, node, index,
                  "a scaled or rotated external surface would have to be resampled; the "
                  "CPU reference backend composites producer pixels one for one");
            return;
        }
        if (clip.present && !clip.axis_aligned) {
            Issue(RenderIssueCode::UnsupportedExternalSurface, node, index,
                  "a non-axis-aligned clip over an external surface would need partial "
                  "pixel coverage of imported content");
            return;
        }

        ExternalSurfaceView view;
        std::string message;
        if (!external_reader_->ReadExternalSurface(external.source, view, message)) {
            if (message.empty()) message = "the external surface reader declined the source";
            Issue(RenderIssueCode::UnsupportedExternalSurface, node, index,
                  std::move(message));
            return;
        }
        if (!view.readable()) {
            Issue(RenderIssueCode::UnsupportedExternalSurface, node, index,
                  "the external surface reader returned a view that is not a readable "
                  "premultiplied image");
            return;
        }

        const Rect root_bounds = transform_to_root.TransformRect(external.bounds);
        if (!HasValidExtent(root_bounds)) {
            Issue(RenderIssueCode::InvalidScene, node, index,
                  "translated external surface rectangle is not representable");
            return;
        }
        const PixelRect placement = SnapRect(root_bounds);
        if (placement.width() != view.width || placement.height() != view.height) {
            Issue(RenderIssueCode::UnsupportedExternalSurface, node, index,
                  "the producer's " + std::to_string(view.width) + "x" +
                      std::to_string(view.height) + " surface is not the arranged " +
                      std::to_string(placement.width()) + "x" +
                      std::to_string(placement.height()) +
                      " rectangle; resampling it would invent content the producer "
                      "never presented");
            return;
        }
        if (placement.empty()) return;

        PixelRect visible = placement;
        if (clip.present) {
            const PixelRect clip_box = SnapRect(clip.bounds);
            visible.left = std::max(visible.left, clip_box.left);
            visible.top = std::max(visible.top, clip_box.top);
            visible.right = std::min(visible.right, clip_box.right);
            visible.bottom = std::min(visible.bottom, clip_box.bottom);
        }
        visible.left = std::max(visible.left, 0);
        visible.top = std::max(visible.top, 0);
        visible.right = std::min(visible.right, target.width());
        visible.bottom = std::min(visible.bottom, target.height());
        if (visible.empty()) return;

        for (int y = visible.top; y < visible.bottom; ++y) {
            const std::size_t row =
                static_cast<std::size_t>(y - placement.top) * view.stride_pixels;
            for (int x = visible.left; x < visible.right; ++x) {
                target.BlendPremultipliedPixel(
                    x, y, view.pixels[row + static_cast<std::size_t>(x - placement.left)]);
            }
        }
    }

    const SceneSnapshot& scene_;
    RasterResult& result_;
    TextRasterizer* text_rasterizer_;
    ExternalSurfaceReader* external_reader_;
    std::unordered_set<std::uint64_t> visited_;
};

}  // namespace

RasterResult CpuRasterBackend::Render(const SceneSnapshot& scene, Color clear,
                                      TextRasterizer* text_rasterizer,
                                      ExternalSurfaceReader* external_reader) const {
    const Size extent = scene.surface();
    if (!std::isfinite(extent.width) || !std::isfinite(extent.height) || extent.width < 0.0 ||
        extent.height < 0.0) {
        RasterResult result(Surface(0, 0, clear));
        result.issues.push_back(RenderIssue{RenderIssueCode::InvalidScene, scene.root(),
                                            RenderIssue::kNoCommand,
                                            "scene surface has an invalid extent"});
        return result;
    }

    const double rounded_width = RoundHalfUp(extent.width);
    const double rounded_height = RoundHalfUp(extent.height);
    if (!std::isfinite(rounded_width) || !std::isfinite(rounded_height) ||
        rounded_width > std::numeric_limits<int>::max() ||
        rounded_height > std::numeric_limits<int>::max()) {
        RasterResult result(Surface(0, 0, clear));
        result.issues.push_back(RenderIssue{
            RenderIssueCode::InvalidScene, scene.root(), RenderIssue::kNoCommand,
            "scene surface does not fit the renderer's integer pixel extent"});
        return result;
    }
    const int pixel_width = static_cast<int>(rounded_width);
    const int pixel_height = static_cast<int>(rounded_height);
    const std::size_t width = static_cast<std::size_t>(pixel_width);
    const std::size_t height = static_cast<std::size_t>(pixel_height);
    const std::size_t max_pixels = std::vector<std::uint32_t>{}.max_size();
    if (height != 0 &&
        (width > std::numeric_limits<std::size_t>::max() / height ||
         width * height > max_pixels)) {
        RasterResult result(Surface(0, 0, clear));
        result.issues.push_back(RenderIssue{
            RenderIssueCode::InvalidScene, scene.root(), RenderIssue::kNoCommand,
            "scene surface pixel count is not representable"});
        return result;
    }

    RasterResult result(Surface(pixel_width, pixel_height, clear));
    std::string validation_error;
    if (!scene.Validate(&validation_error)) {
        result.issues.push_back(RenderIssue{RenderIssueCode::InvalidScene, scene.root(),
                                            RenderIssue::kNoCommand,
                                            std::move(validation_error)});
        return result;
    }
    Walker(scene, result, text_rasterizer, external_reader).Run();
    return result;
}

}  // namespace render
}  // namespace openxaml
