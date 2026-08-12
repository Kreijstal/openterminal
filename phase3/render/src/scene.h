// Retained, platform-neutral rendering state.
//
// Layout owns sizes and positions; this layer records the visual state that a
// renderer consumes.  Nothing here names an HWND, a Direct2D resource or a
// platform thread.  In particular, display-list coordinates are local to the
// visual that owns them.  Flattening them early would make transforms, clips
// and incremental updates impossible to represent faithfully.

#ifndef OPENXAML_RENDER_SCENE_H
#define OPENXAML_RENDER_SCENE_H

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "brush.h"
#include "external_surface.h"
#include "layout.h"

namespace openxaml {
namespace render {

// A NodeId identifies the same visual across successive snapshots.  Zero is
// reserved for "no node", including the parent of the root visual.
struct NodeId {
    std::uint64_t value = 0;

    constexpr NodeId() = default;
    explicit constexpr NodeId(std::uint64_t value_) : value(value_) {}

    constexpr bool Valid() const { return value != 0; }
};

constexpr bool operator==(NodeId left, NodeId right) { return left.value == right.value; }
constexpr bool operator!=(NodeId left, NodeId right) { return !(left == right); }
constexpr bool operator<(NodeId left, NodeId right) { return left.value < right.value; }

using Point = openxaml::Point;

// The Direct2D/XAML affine layout: a point is transformed as
//
//   x' = x*m11 + y*m21 + dx
//   y' = x*m12 + y*m22 + dy
//
// General affine arithmetic costs little here and prevents a translation-only
// first backend from leaking that limitation into the retained scene format.
struct Matrix3x2 {
    double m11 = 1.0;
    double m12 = 0.0;
    double m21 = 0.0;
    double m22 = 1.0;
    double dx = 0.0;
    double dy = 0.0;

    static constexpr Matrix3x2 Identity() { return {}; }
    static constexpr Matrix3x2 Translation(double x, double y) {
        return {1.0, 0.0, 0.0, 1.0, x, y};
    }
    static Matrix3x2 RotationAround(double degrees, Point center);
    static constexpr Matrix3x2 ScaleAround(double x, double y, Point center) {
        return {x, 0.0, 0.0, y,
                center.x * (1.0 - x), center.y * (1.0 - y)};
    }

    Point TransformPoint(Point point) const;
    Rect TransformRect(const Rect& rect) const;

    // Returns a matrix which applies this matrix and then `next`.
    Matrix3x2 Then(const Matrix3x2& next) const;
};

struct Clip {
    enum class Kind { None, Rect };

    Kind kind = Kind::None;
    Rect bounds;

    static constexpr Clip None() { return {}; }
    static constexpr Clip FromRect(const Rect& rect) { return {Kind::Rect, rect}; }
};

enum class DirtyFlags : std::uint8_t {
    None = 0,
    Layout = 1u << 0,
    Content = 1u << 1,
    Composition = 1u << 2,
    Children = 1u << 3,
};

constexpr DirtyFlags operator|(DirtyFlags left, DirtyFlags right) {
    return static_cast<DirtyFlags>(static_cast<std::uint8_t>(left) |
                                   static_cast<std::uint8_t>(right));
}
constexpr DirtyFlags operator&(DirtyFlags left, DirtyFlags right) {
    return static_cast<DirtyFlags>(static_cast<std::uint8_t>(left) &
                                   static_cast<std::uint8_t>(right));
}
inline DirtyFlags& operator|=(DirtyFlags& left, DirtyFlags right) {
    left = left | right;
    return left;
}
constexpr bool AnyDirty(DirtyFlags flags) { return flags != DirtyFlags::None; }
constexpr bool HasDirty(DirtyFlags flags, DirtyFlags requested) {
    return (flags & requested) == requested;
}

struct LocalFillRect {
    Rect bounds;
    Color color;
};

// An ImageBrush whose source was supplied in markup remains a first-class
// scene command. Decoding and sampling are backend responsibilities; a
// backend without them reports UnsupportedImageBrush rather than silently
// treating the image as transparent. An ImageBrush with no source emits no
// command because its specified native semantics are to paint nothing.
struct LocalImageBrushFill {
    Rect bounds;
    std::string source;
};

// This is deliberately still a measured text run rather than a final glyph
// run.  It preserves all text truth currently produced by layout without
// pretending that shaping already happened.  A later GlyphRun command can be
// added without changing the retained-tree or backend boundaries.
struct LocalText {
    Rect bounds;
    std::string text;
    std::string font_family;
    double font_size = 0.0;
    double baseline = 0.0;
    std::vector<double> advances;
    Color color;
    bool wrap = false;
    bool bold = false;
    std::string language = "en-US";
    // The element path, carried so a backend's answer for this run -- which
    // painter drew it -- can be recorded against the sidecar's text table.
    // Identification only; nothing geometric reads it.
    std::string path;
};

// A live producer-owned surface occupies this local rectangle. The lifetime
// token keeps the duplicated handle/AddRef'd swap chain alive for the whole
// immutable scene snapshot. Import and presentation belong to a platform
// compositor; a CPU backend must report that boundary rather than omit it.
struct LocalExternalSurface {
    Rect bounds;
    ExternalSurfaceReference source;
};

using DrawCommand =
    std::variant<LocalFillRect, LocalImageBrushFill, LocalText, LocalExternalSurface>;

struct LocalDisplayList {
    // Command order is paint order and is therefore part of the snapshot.
    std::vector<DrawCommand> commands;
};

struct VisualNode {
    NodeId id;
    NodeId parent;
    std::vector<NodeId> children;

    Rect local_bounds;
    Matrix3x2 local_transform = Matrix3x2::Identity();
    // A declared transform which the compiler cannot lower to local_transform.
    // Keeping its runtime type prevents the backend from painting the subtree
    // untransformed while still retaining the visual for diagnostics.
    std::string unsupported_transform;
    Clip clip = Clip::None();
    double opacity = 1.0;
    bool visible = true;

    // Lower z-index paints first. `order` is the stable sibling insertion
    // order and NodeId is the final deterministic tie breaker.
    std::int32_t z_index = 0;
    std::uint64_t order = 0;

    // Sharing an immutable list lets unchanged content survive across frames
    // without allowing a published snapshot to be edited behind its back.
    std::shared_ptr<const LocalDisplayList> content;
    DirtyFlags dirty = DirtyFlags::None;
};

// A complete frame's retained state. Nodes are copied and sorted by NodeId at
// construction, making lookup and validation independent of producer order.
// After publication the class exposes only const access.
class SceneSnapshot {
public:
    SceneSnapshot(Size surface, NodeId root, std::vector<VisualNode> nodes);

    const Size& surface() const { return surface_; }
    NodeId root() const { return root_; }
    const std::vector<VisualNode>& nodes() const { return nodes_; }

    const VisualNode* Find(NodeId id) const;
    std::vector<const VisualNode*> OrderedChildren(NodeId parent) const;

    // The returned matrix maps the node's local coordinate system to root
    // coordinates. A missing or cyclic parent chain has no transform.
    std::optional<Matrix3x2> TransformToRoot(NodeId id) const;

    // Checks identity, topology and finite render state. If supplied, `error`
    // receives the first deterministic failure in NodeId order.
    bool Validate(std::string* error = nullptr) const;

private:
    Size surface_;
    NodeId root_;
    std::vector<VisualNode> nodes_;
};

}  // namespace render
}  // namespace openxaml

#endif  // OPENXAML_RENDER_SCENE_H
