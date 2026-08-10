#include "scene.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>

namespace openxaml {
namespace render {
namespace {

bool Finite(double value) { return std::isfinite(value); }

bool ValidRect(const Rect& rect) {
    return Finite(rect.x) && Finite(rect.y) && Finite(rect.width) && Finite(rect.height) &&
           rect.width >= 0.0 && rect.height >= 0.0;
}

bool ValidMatrix(const Matrix3x2& matrix) {
    return Finite(matrix.m11) && Finite(matrix.m12) && Finite(matrix.m21) &&
           Finite(matrix.m22) && Finite(matrix.dx) && Finite(matrix.dy);
}

std::string IdText(NodeId id) {
    std::ostringstream stream;
    stream << id.value;
    return stream.str();
}

bool Fail(std::string* error, const std::string& message) {
    if (error) *error = message;
    return false;
}

}  // namespace

Point Matrix3x2::TransformPoint(Point point) const {
    return {point.x * m11 + point.y * m21 + dx,
            point.x * m12 + point.y * m22 + dy};
}

Matrix3x2 Matrix3x2::RotationAround(double degrees, Point center) {
    constexpr double kPi = 3.141592653589793238462643383279502884;
    double normalized = std::remainder(degrees, 360.0);
    // libm is not required to return exact zero/one at quadrant angles. Those
    // residuals are observable here because they would select polygon coverage
    // for a visual which is exactly axis aligned. Canonicalize the four exact
    // quadrants before doing any trigonometry.
    double cosine = 0.0;
    double sine = 0.0;
    if (normalized == 0.0) {
        cosine = 1.0;
    } else if (normalized == 90.0) {
        sine = 1.0;
    } else if (normalized == -90.0) {
        sine = -1.0;
    } else if (normalized == 180.0 || normalized == -180.0) {
        cosine = -1.0;
    } else {
        const double radians = normalized * kPi / 180.0;
        cosine = std::cos(radians);
        sine = std::sin(radians);
    }
    return {cosine,
            sine,
            -sine,
            cosine,
            center.x * (1.0 - cosine) + center.y * sine,
            center.y * (1.0 - cosine) - center.x * sine};
}

Rect Matrix3x2::TransformRect(const Rect& rect) const {
    const Point points[] = {
        TransformPoint({rect.x, rect.y}),
        TransformPoint({rect.x + rect.width, rect.y}),
        TransformPoint({rect.x, rect.y + rect.height}),
        TransformPoint({rect.x + rect.width, rect.y + rect.height}),
    };

    double left = points[0].x;
    double right = points[0].x;
    double top = points[0].y;
    double bottom = points[0].y;
    for (const Point& point : points) {
        left = std::min(left, point.x);
        right = std::max(right, point.x);
        top = std::min(top, point.y);
        bottom = std::max(bottom, point.y);
    }
    return {left, top, right - left, bottom - top};
}

Matrix3x2 Matrix3x2::Then(const Matrix3x2& next) const {
    return {
        m11 * next.m11 + m12 * next.m21,
        m11 * next.m12 + m12 * next.m22,
        m21 * next.m11 + m22 * next.m21,
        m21 * next.m12 + m22 * next.m22,
        dx * next.m11 + dy * next.m21 + next.dx,
        dx * next.m12 + dy * next.m22 + next.dy,
    };
}

SceneSnapshot::SceneSnapshot(Size surface, NodeId root, std::vector<VisualNode> nodes)
    : surface_(surface), root_(root), nodes_(std::move(nodes)) {
    std::stable_sort(nodes_.begin(), nodes_.end(), [](const VisualNode& left,
                                                      const VisualNode& right) {
        return left.id < right.id;
    });
}

const VisualNode* SceneSnapshot::Find(NodeId id) const {
    const auto found = std::lower_bound(
        nodes_.begin(), nodes_.end(), id,
        [](const VisualNode& node, NodeId requested) { return node.id < requested; });
    if (found == nodes_.end() || found->id != id) return nullptr;
    return &*found;
}

std::vector<const VisualNode*> SceneSnapshot::OrderedChildren(NodeId parent) const {
    std::vector<const VisualNode*> result;
    const VisualNode* node = Find(parent);
    if (!node) return result;

    result.reserve(node->children.size());
    for (NodeId child : node->children) {
        if (const VisualNode* found = Find(child)) result.push_back(found);
    }
    std::stable_sort(result.begin(), result.end(), [](const VisualNode* left,
                                                      const VisualNode* right) {
        if (left->z_index != right->z_index) return left->z_index < right->z_index;
        if (left->order != right->order) return left->order < right->order;
        return left->id < right->id;
    });
    return result;
}

std::optional<Matrix3x2> SceneSnapshot::TransformToRoot(NodeId id) const {
    Matrix3x2 result = Matrix3x2::Identity();
    std::set<NodeId> visited;
    const VisualNode* current = Find(id);
    if (!current) return std::nullopt;

    while (current) {
        if (!visited.insert(current->id).second) return std::nullopt;
        result = result.Then(current->local_transform);
        if (!current->parent.Valid()) break;
        current = Find(current->parent);
        if (!current) return std::nullopt;
    }
    return result;
}

bool SceneSnapshot::Validate(std::string* error) const {
    if (error) error->clear();
    if (!Finite(surface_.width) || !Finite(surface_.height) || surface_.width < 0.0 ||
        surface_.height < 0.0) {
        return Fail(error, "surface size is not finite and non-negative");
    }
    if (!root_.Valid()) return Fail(error, "root id is invalid");
    if (!Find(root_)) return Fail(error, "root " + IdText(root_) + " does not exist");

    for (std::size_t index = 0; index < nodes_.size(); ++index) {
        const VisualNode& node = nodes_[index];
        if (!node.id.Valid()) return Fail(error, "node id 0 is invalid");
        if (index > 0 && nodes_[index - 1].id == node.id) {
            return Fail(error, "duplicate node id " + IdText(node.id));
        }
        if (!ValidRect(node.local_bounds)) {
            return Fail(error, "node " + IdText(node.id) + " has invalid bounds");
        }
        if (!ValidMatrix(node.local_transform)) {
            return Fail(error, "node " + IdText(node.id) + " has invalid transform");
        }
        if (!Finite(node.opacity) || node.opacity < 0.0 || node.opacity > 1.0) {
            return Fail(error, "node " + IdText(node.id) + " has invalid opacity");
        }
        if (node.clip.kind == Clip::Kind::Rect && !ValidRect(node.clip.bounds)) {
            return Fail(error, "node " + IdText(node.id) + " has invalid clip");
        }
        if (node.content) {
            for (std::size_t command_index = 0;
                 command_index < node.content->commands.size(); ++command_index) {
                const DrawCommand& command = node.content->commands[command_index];
                if (const auto* image = std::get_if<LocalImageBrushFill>(&command)) {
                    if (!ValidRect(image->bounds)) {
                        return Fail(error, "node " + IdText(node.id) +
                                               " has an image brush with invalid bounds");
                    }
                    if (image->source.empty()) {
                        return Fail(error, "node " + IdText(node.id) +
                                               " has an image brush with no source");
                    }
                    continue;
                }
                const auto* external = std::get_if<LocalExternalSurface>(&command);
                if (!external) continue;
                if (!ValidRect(external->bounds)) {
                    return Fail(error, "node " + IdText(node.id) +
                                           " has an external surface with invalid bounds");
                }
                if (!external->source) {
                    return Fail(error, "node " + IdText(node.id) +
                                           " has an invalid external surface reference");
                }
            }
        }

        if (node.id == root_) {
            if (node.parent.Valid()) return Fail(error, "root has a parent");
        } else if (!node.parent.Valid() || !Find(node.parent)) {
            return Fail(error, "node " + IdText(node.id) + " has a dangling parent");
        }

        std::set<NodeId> child_ids;
        for (NodeId child_id : node.children) {
            if (!child_ids.insert(child_id).second) {
                return Fail(error, "node " + IdText(node.id) + " repeats child " +
                                       IdText(child_id));
            }
            const VisualNode* child = Find(child_id);
            if (!child) {
                return Fail(error, "node " + IdText(node.id) + " has dangling child " +
                                       IdText(child_id));
            }
            if (child->parent != node.id) {
                return Fail(error, "child " + IdText(child_id) + " disagrees with parent " +
                                       IdText(node.id));
            }
        }
    }

    // Parent links must be mirrored by a child link, and following them must
    // reach the declared root. This rejects disconnected cycles as well as a
    // producer that only filled one side of the relationship.
    for (const VisualNode& node : nodes_) {
        if (node.id != root_) {
            const VisualNode* parent = Find(node.parent);
            if (!parent || std::find(parent->children.begin(), parent->children.end(), node.id) ==
                               parent->children.end()) {
                return Fail(error, "parent does not list child " + IdText(node.id));
            }
        }

        std::set<NodeId> chain;
        const VisualNode* current = &node;
        while (current->parent.Valid()) {
            if (!chain.insert(current->id).second) {
                return Fail(error, "parent cycle at node " + IdText(current->id));
            }
            current = Find(current->parent);
            if (!current) break;  // The dangling-parent check above reports this first.
        }
        if (!current || current->id != root_) {
            return Fail(error, "node " + IdText(node.id) + " is disconnected from root");
        }
    }

    return true;
}

}  // namespace render
}  // namespace openxaml
