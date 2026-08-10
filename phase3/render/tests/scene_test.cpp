#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "scene.h"

using namespace openxaml;
using namespace openxaml::render;

namespace {

int failures = 0;

void Check(bool condition, const char* what, int line) {
    if (condition) return;
    std::cerr << "scene_test.cpp:" << line << ": CHECK failed: " << what << "\n";
    ++failures;
}

#define CHECK(condition) Check(static_cast<bool>(condition), #condition, __LINE__)

bool Close(double left, double right) { return std::abs(left - right) < 1e-9; }

VisualNode Node(std::uint64_t id, std::uint64_t parent = 0) {
    VisualNode node;
    node.id = NodeId{id};
    node.parent = NodeId{parent};
    node.local_bounds = {0.0, 0.0, 10.0, 10.0};
    return node;
}

void LookupIsStableAndContentIsLocal() {
    auto content = std::make_shared<LocalDisplayList>();
    content->commands.push_back(LocalFillRect{{1.0, 2.0, 3.0, 4.0},
                                              Color{0xff, 0x10, 0x20, 0x30}});

    VisualNode root = Node(10);
    root.children = {NodeId{30}};
    VisualNode child = Node(30, 10);
    child.content = content;

    // Producer order is deliberately not lookup order.
    SceneSnapshot scene({100.0, 80.0}, NodeId{10}, {child, root});
    CHECK(scene.Validate());
    CHECK(scene.nodes().front().id == NodeId{10});
    CHECK(scene.Find(NodeId{30}) != nullptr);
    CHECK(scene.Find(NodeId{30})->content == content);
    CHECK(std::get<LocalFillRect>(content->commands[0]).bounds.x == 1.0);
    CHECK(scene.Find(NodeId{999}) == nullptr);
}

void TransformsAccumulateFromLocalToRoot() {
    VisualNode root = Node(1);
    root.children = {NodeId{2}};
    root.local_transform = Matrix3x2::Translation(3.0, 4.0);
    VisualNode middle = Node(2, 1);
    middle.children = {NodeId{3}};
    middle.local_transform = Matrix3x2::Translation(10.0, 20.0);
    VisualNode leaf = Node(3, 2);
    leaf.local_transform = Matrix3x2::Translation(-2.0, 5.0);

    SceneSnapshot scene({100.0, 80.0}, NodeId{1}, {leaf, root, middle});
    const std::optional<Matrix3x2> transform = scene.TransformToRoot(NodeId{3});
    CHECK(transform.has_value());
    const Point point = transform->TransformPoint({1.0, 2.0});
    CHECK(Close(point.x, 12.0));
    CHECK(Close(point.y, 31.0));

    const Rect bounds = transform->TransformRect({0.0, 0.0, 2.0, 3.0});
    CHECK(Close(bounds.x, 11.0));
    CHECK(Close(bounds.y, 29.0));
    CHECK(Close(bounds.width, 2.0));
    CHECK(Close(bounds.height, 3.0));
}

void RotationAroundAVisualOriginUsesTheRetainedAffineConvention() {
    const Matrix3x2 rotation = Matrix3x2::RotationAround(90.0, {2.0, 3.0});
    const Point pivot = rotation.TransformPoint({2.0, 3.0});
    const Point right = rotation.TransformPoint({3.0, 3.0});
    CHECK(Close(pivot.x, 2.0));
    CHECK(Close(pivot.y, 3.0));
    CHECK(Close(right.x, 2.0));
    CHECK(Close(right.y, 4.0));

    const Matrix3x2 full_turn = Matrix3x2::RotationAround(360.0, {2.0, 3.0});
    CHECK(full_turn.m11 == 1.0 && full_turn.m12 == 0.0);
    CHECK(full_turn.m21 == 0.0 && full_turn.m22 == 1.0);
    CHECK(full_turn.dx == 0.0 && full_turn.dy == 0.0);
    CHECK(rotation.m11 == 0.0 && rotation.m12 == 1.0);
    CHECK(rotation.m21 == -1.0 && rotation.m22 == 0.0);

    VisualNode root = Node(1);
    root.children = {NodeId{2}};
    root.local_transform = Matrix3x2::Translation(10.0, 20.0);
    VisualNode child = Node(2, 1);
    child.local_transform = rotation.Then(Matrix3x2::Translation(5.0, 7.0));
    const SceneSnapshot scene({100.0, 80.0}, NodeId{1}, {root, child});
    const std::optional<Matrix3x2> composed = scene.TransformToRoot(NodeId{2});
    CHECK(composed.has_value());
    if (composed) {
        const Point transformed_pivot = composed->TransformPoint({2.0, 3.0});
        CHECK(Close(transformed_pivot.x, 17.0));
        CHECK(Close(transformed_pivot.y, 30.0));
    }
}

void ChildrenHaveDeterministicPaintOrder() {
    VisualNode root = Node(1);
    root.children = {NodeId{4}, NodeId{2}, NodeId{3}};
    VisualNode high = Node(4, 1);
    high.z_index = 2;
    VisualNode later = Node(2, 1);
    later.order = 8;
    VisualNode earlier = Node(3, 1);
    earlier.order = 3;

    SceneSnapshot scene({100.0, 80.0}, NodeId{1}, {high, later, earlier, root});
    const std::vector<const VisualNode*> ordered = scene.OrderedChildren(NodeId{1});
    CHECK(ordered.size() == 3);
    CHECK(ordered[0]->id == NodeId{3});
    CHECK(ordered[1]->id == NodeId{2});
    CHECK(ordered[2]->id == NodeId{4});
}

void ClipOpacityVisibilityAndDirtyStateSurviveSnapshot() {
    VisualNode root = Node(1);
    root.clip = Clip::FromRect({2.0, 3.0, 20.0, 30.0});
    root.opacity = 0.25;
    root.visible = false;
    root.dirty = DirtyFlags::Content | DirtyFlags::Composition;

    SceneSnapshot scene({100.0, 80.0}, NodeId{1}, {root});
    const VisualNode* stored = scene.Find(NodeId{1});
    CHECK(stored != nullptr);
    CHECK(stored->clip.kind == Clip::Kind::Rect);
    CHECK(stored->clip.bounds.width == 20.0);
    CHECK(stored->opacity == 0.25);
    CHECK(!stored->visible);
    CHECK(HasDirty(stored->dirty, DirtyFlags::Content));
    CHECK(HasDirty(stored->dirty, DirtyFlags::Composition));
    CHECK(!HasDirty(stored->dirty, DirtyFlags::Layout));
}

void ValidationRejectsDuplicateAndDanglingIds() {
    VisualNode duplicate_a = Node(1);
    VisualNode duplicate_b = Node(1);
    SceneSnapshot duplicates({10.0, 10.0}, NodeId{1}, {duplicate_a, duplicate_b});
    std::string error;
    CHECK(!duplicates.Validate(&error));
    CHECK(error == "duplicate node id 1");

    VisualNode root = Node(1);
    root.children = {NodeId{2}};
    SceneSnapshot dangling_child({10.0, 10.0}, NodeId{1}, {root});
    CHECK(!dangling_child.Validate(&error));
    CHECK(error == "node 1 has dangling child 2");

    VisualNode orphan = Node(2, 99);
    SceneSnapshot dangling_parent({10.0, 10.0}, NodeId{1}, {Node(1), orphan});
    CHECK(!dangling_parent.Validate(&error));
    CHECK(error == "node 2 has a dangling parent");

    VisualNode invalid_external = Node(1);
    auto content = std::make_shared<LocalDisplayList>();
    content->commands.push_back(LocalExternalSurface{
        Rect{0.0, 0.0, 2.0, 2.0}, ExternalSurfaceReference{}});
    invalid_external.content = std::move(content);
    SceneSnapshot missing_resource(
        {10.0, 10.0}, NodeId{1}, {invalid_external});
    CHECK(!missing_resource.Validate(&error));
    CHECK(error == "node 1 has an invalid external surface reference");

    VisualNode invalid_image = Node(1);
    auto image_content = std::make_shared<LocalDisplayList>();
    image_content->commands.push_back(
        LocalImageBrushFill{Rect{0.0, 0.0, 2.0, 2.0}, {}});
    invalid_image.content = std::move(image_content);
    SceneSnapshot missing_image_source(
        {10.0, 10.0}, NodeId{1}, {invalid_image});
    CHECK(!missing_image_source.Validate(&error));
    CHECK(error == "node 1 has an image brush with no source");
}

}  // namespace

int main() {
    LookupIsStableAndContentIsLocal();
    TransformsAccumulateFromLocalToRoot();
    RotationAroundAVisualOriginUsesTheRetainedAffineConvention();
    ChildrenHaveDeterministicPaintOrder();
    ClipOpacityVisibilityAndDirtyStateSurviveSnapshot();
    ValidationRejectsDuplicateAndDanglingIds();

    if (failures == 0) {
        std::cout << "scene tests passed\n";
        return 0;
    }
    std::cerr << failures << " scene test(s) failed\n";
    return 1;
}
