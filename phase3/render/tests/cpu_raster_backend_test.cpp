#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cpu_raster_backend.h"

using namespace openxaml;
using namespace openxaml::render;

namespace {

int failures = 0;

void Check(bool condition, const char* expression, int line) {
    if (condition) return;
    std::cerr << "cpu_raster_backend_test.cpp:" << line << ": CHECK failed: " << expression
              << "\n";
    ++failures;
}

#define CHECK(expression) Check(static_cast<bool>(expression), #expression, __LINE__)

constexpr Color kClear{0xff, 0x01, 0x02, 0x03};
constexpr Color kRed{0xff, 0xff, 0x00, 0x00};
constexpr Color kGreen{0xff, 0x00, 0xff, 0x00};
constexpr Color kBlue{0xff, 0x00, 0x00, 0xff};

std::shared_ptr<const LocalDisplayList> Fill(Rect bounds, Color color) {
    auto list = std::make_shared<LocalDisplayList>();
    list->commands.push_back(LocalFillRect{bounds, color});
    return list;
}

VisualNode Node(std::uint64_t id, std::uint64_t parent = 0) {
    VisualNode node;
    node.id = NodeId{id};
    node.parent = NodeId{parent};
    return node;
}

bool HasIssue(const RasterResult& result, RenderIssueCode code, NodeId node = NodeId{}) {
    for (const RenderIssue& issue : result.issues) {
        if (issue.code == code && (!node.Valid() || issue.node == node)) return true;
    }
    return false;
}

void LocalTranslationsAndPaintOrderAreDeterministic() {
    VisualNode root = Node(1);
    root.local_transform = Matrix3x2::Translation(1.0, 1.0);
    root.children = {NodeId{2}, NodeId{3}, NodeId{4}};

    VisualNode first = Node(2, 1);
    first.local_transform = Matrix3x2::Translation(2.5, 1.5);
    first.content = Fill(Rect{0.0, 0.0, 3.0, 3.0}, kRed);
    first.z_index = 0;
    first.order = 2;

    VisualNode last = Node(3, 1);
    last.local_transform = Matrix3x2::Translation(3.0, 2.0);
    last.content = Fill(Rect{0.0, 0.0, 2.0, 2.0}, kBlue);
    last.z_index = 1;

    VisualNode middle = Node(4, 1);
    middle.local_transform = Matrix3x2::Translation(3.0, 2.0);
    middle.content = Fill(Rect{0.0, 0.0, 2.0, 2.0}, kGreen);
    middle.z_index = 0;
    middle.order = 3;

    // Deliberately not in NodeId or paint order. SceneSnapshot canonicalizes
    // storage, while OrderedChildren defines rendering order.
    SceneSnapshot scene(Size{10.0, 8.0}, NodeId{1}, {last, root, middle, first});
    const RasterResult result = CpuRasterBackend{}.Render(scene, kClear);

    CHECK(result.complete());
    // Translation composes to (3.5, 2.5), then the existing per-edge
    // round-half-up rule maps the fill to [4,7) x [3,6).
    CHECK(result.surface.At(4, 3) == Pack(kBlue));
    CHECK(result.surface.At(6, 5) == Pack(kRed));
    CHECK(result.surface.At(3, 3) == Pack(kClear));
}

void RectangularClipsComposeAcrossTheTree() {
    VisualNode root = Node(1);
    root.clip = Clip::FromRect(Rect{2.0, 1.0, 5.0, 5.0});
    root.children = {NodeId{2}};

    VisualNode child = Node(2, 1);
    child.local_transform = Matrix3x2::Translation(1.0, 1.0);
    child.clip = Clip::FromRect(Rect{2.0, 1.0, 5.0, 2.0});
    child.content = Fill(Rect{-10.0, -10.0, 30.0, 30.0}, kRed);

    SceneSnapshot scene(Size{10.0, 8.0}, NodeId{1}, {root, child});
    const RasterResult result = CpuRasterBackend{}.Render(scene, kClear);

    CHECK(result.complete());
    // Root clip is [2,7)x[1,6); translated child clip is [3,8)x[2,4).
    CHECK(result.surface.At(3, 2) == Pack(kRed));
    CHECK(result.surface.At(6, 3) == Pack(kRed));
    CHECK(result.surface.At(2, 2) == Pack(kClear));
    CHECK(result.surface.At(3, 4) == Pack(kClear));
}

void InvisibleNodesAndZeroOpacitySkipTheirSubtrees() {
    VisualNode root = Node(1);
    root.children = {NodeId{2}, NodeId{3}};

    VisualNode hidden = Node(2, 1);
    hidden.visible = false;
    hidden.content = Fill(Rect{0.0, 0.0, 4.0, 4.0}, kRed);

    VisualNode transparent = Node(3, 1);
    transparent.opacity = 0.0;
    transparent.content = Fill(Rect{4.0, 0.0, 4.0, 4.0}, kBlue);

    SceneSnapshot scene(Size{8.0, 4.0}, NodeId{1}, {root, hidden, transparent});
    const RasterResult result = CpuRasterBackend{}.Render(scene, kClear);

    CHECK(result.complete());
    CHECK(result.surface.At(1, 1) == Pack(kClear));
    CHECK(result.surface.At(5, 1) == Pack(kClear));
}

void AffineSolidFillsAreRasterizedInsteadOfRefused() {
    VisualNode root = Node(1);
    root.children = {NodeId{2}};

    VisualNode affine = Node(2, 1);
    affine.local_transform.m11 = 2.0;
    affine.content = Fill(Rect{0.0, 0.0, 2.0, 2.0}, kRed);

    SceneSnapshot scene(Size{8.0, 2.0}, NodeId{1}, {root, affine});
    const RasterResult result = CpuRasterBackend{}.Render(scene, kClear);

    CHECK(result.complete());
    CHECK(result.surface.At(0, 0) == Pack(kRed));
    CHECK(result.surface.At(3, 1) == Pack(kRed));
    CHECK(result.surface.At(4, 0) == Pack(kClear));
}

void DeclaredUnsupportedTransformsAreIssuesAndNeverPaintUntransformed() {
    VisualNode root = Node(1);
    root.unsupported_transform = "Windows.UI.Xaml.Media.CompositeTransform";
    root.content = Fill(Rect{0.0, 0.0, 2.0, 2.0}, kRed);
    const RasterResult result = CpuRasterBackend{}.Render(
        SceneSnapshot(Size{2.0, 2.0}, NodeId{1}, {root}), kClear);
    CHECK(HasIssue(result, RenderIssueCode::UnsupportedTransform, NodeId{1}));
    CHECK(result.surface.At(0, 0) == Pack(kClear));
}

void RotatedCoverageClipAndOpacityHaveExactPremultipliedVectors() {
    VisualNode root = Node(1);
    root.local_transform = Matrix3x2::RotationAround(45.0, {1.0, 1.0})
                               .Then(Matrix3x2::Translation(2.0, 2.0));
    root.content = Fill(Rect{0.0, 0.0, 2.0, 2.0}, kRed);
    root.opacity = 0.5;

    RasterResult result = CpuRasterBackend{}.Render(
        SceneSnapshot(Size{6.0, 6.0}, NodeId{1}, {root}), Color{0, 0, 0, 0});
    CHECK(result.complete());
    CHECK(result.surface.At(3, 3) == PackPremultiplied(0x6a, 0x6a, 0x00, 0x00));
    CHECK(result.surface.At(2, 1) == PackPremultiplied(0x0b, 0x0b, 0x00, 0x00));
    CHECK(result.surface.At(1, 1) == 0u);

    root.opacity = 1.0;
    root.clip = Clip::FromRect(Rect{0.0, 0.0, 1.0, 2.0});
    result = CpuRasterBackend{}.Render(
        SceneSnapshot(Size{6.0, 6.0}, NodeId{1}, {root}), Color{0, 0, 0, 0});
    CHECK(result.complete());
    CHECK(result.surface.At(2, 2) == PackPremultiplied(0xd3, 0xd3, 0x00, 0x00));
    CHECK(result.surface.At(3, 2) == PackPremultiplied(0x6a, 0x6a, 0x00, 0x00));
    CHECK(result.surface.At(3, 3) == 0u);
}

void EquivalentAxisAlignedTransformsUseOneExactRasterRule() {
    auto render_single = [](const Matrix3x2& transform) {
        VisualNode root = Node(1);
        root.local_transform = transform;
        root.content = Fill(Rect{0.25, 0.25, 2.5, 2.5}, kRed);
        return CpuRasterBackend{}.Render(
            SceneSnapshot(Size{5.0, 5.0}, NodeId{1}, {root}), kClear);
    };

    const RasterResult identity = render_single(Matrix3x2::Identity());
    const RasterResult full_turn =
        render_single(Matrix3x2::RotationAround(360.0, {1.5, 1.5}));
    CHECK(identity.complete());
    CHECK(full_turn.complete());
    CHECK(identity.surface.pixels() == full_turn.surface.pixels());

    VisualNode parent = Node(1);
    parent.children = {NodeId{2}};
    parent.local_transform = Matrix3x2::RotationAround(17.0, {1.5, 1.5});
    VisualNode child = Node(2, 1);
    child.local_transform = Matrix3x2::RotationAround(-17.0, {1.5, 1.5});
    child.content = Fill(Rect{0.25, 0.25, 2.5, 2.5}, kRed);
    const RasterResult cancelled = CpuRasterBackend{}.Render(
        SceneSnapshot(Size{5.0, 5.0}, NodeId{1}, {parent, child}), kClear);
    CHECK(cancelled.complete());
    CHECK(identity.surface.pixels() == cancelled.surface.pixels());

    const RasterResult quarter_turn = render_single(
        Matrix3x2::RotationAround(90.0, {1.5, 1.5}));
    CHECK(quarter_turn.complete());
    CHECK(identity.surface.pixels() == quarter_turn.surface.pixels());
}

void UnrepresentableExtentsAndAffineOverflowAreNamedIssues() {
    VisualNode root = Node(1);
    root.content = Fill(Rect{0.0, 0.0, 1.0, 1.0}, kRed);
    const RasterResult huge_surface = CpuRasterBackend{}.Render(
        SceneSnapshot(Size{static_cast<double>(std::numeric_limits<int>::max()) + 1.0,
                           1.0},
                      NodeId{1}, {root}),
        kClear);
    CHECK(HasIssue(huge_surface, RenderIssueCode::InvalidScene));
    CHECK(huge_surface.surface.width() == 0);

    root.local_transform = Matrix3x2::Translation(1e300, 1e300);
    const RasterResult off_surface = CpuRasterBackend{}.Render(
        SceneSnapshot(Size{2.0, 2.0}, NodeId{1}, {root}), kClear);
    CHECK(off_surface.complete());
    CHECK(off_surface.surface.At(0, 0) == Pack(kClear));

    root.local_transform = Matrix3x2::Identity();
    root.local_transform.m11 = 1e308;
    root.content = Fill(Rect{0.0, 0.0, 2.0, 1.0}, kRed);
    const RasterResult overflow = CpuRasterBackend{}.Render(
        SceneSnapshot(Size{2.0, 2.0}, NodeId{1}, {root}), kClear);
    CHECK(HasIssue(overflow, RenderIssueCode::InvalidScene));
    CHECK(overflow.surface.At(0, 0) == Pack(kClear));

    bool rejected_pixel_count = false;
    try {
        (void)Surface(std::numeric_limits<int>::max(),
                      std::numeric_limits<int>::max(), kClear);
    } catch (const std::length_error&) {
        rejected_pixel_count = true;
    }
    CHECK(rejected_pixel_count);
}

void FractionalFillsUseExactPremultipliedSourceOver() {
    VisualNode root = Node(1);
    auto content = std::make_shared<LocalDisplayList>();
    content->commands.push_back(
        LocalFillRect{Rect{3.0, 0.0, 1.0, 1.0}, Color{0x00, 0xff, 0xff, 0xff}});
    content->commands.push_back(
        LocalFillRect{Rect{0.0, 0.0, 2.0, 1.0}, Color{0xc0, 0xff, 0x40, 0x00}});
    content->commands.push_back(
        LocalFillRect{Rect{1.0, 0.0, 2.0, 1.0}, Color{0x80, 0x00, 0xa0, 0xff}});
    root.content = std::move(content);

    const RasterResult result = CpuRasterBackend{}.Render(
        SceneSnapshot(Size{4.0, 1.0}, NodeId{1}, {root}), Color{0, 0, 0, 0});
    CHECK(result.complete());
    CHECK(result.surface.At(0, 0) == PackPremultiplied(0xc0, 0xc0, 0x30, 0x00));
    CHECK(result.surface.At(1, 0) == PackPremultiplied(0xe0, 0x60, 0x68, 0x80));
    CHECK(result.surface.At(2, 0) == PackPremultiplied(0x80, 0x00, 0x50, 0x80));
    CHECK(result.surface.At(3, 0) == PackPremultiplied(0x00, 0x00, 0x00, 0x00));
}

void OpacityCompositesTheSubtreeOnce() {
    VisualNode root = Node(1);
    root.opacity = 0.5;
    root.children = {NodeId{2}};
    root.content = Fill(Rect{0.0, 0.0, 3.0, 1.0}, kRed);

    VisualNode child = Node(2, 1);
    child.local_transform = Matrix3x2::Translation(1.0, 0.0);
    child.content = Fill(Rect{0.0, 0.0, 2.0, 1.0}, kBlue);

    const RasterResult result = CpuRasterBackend{}.Render(
        SceneSnapshot(Size{3.0, 1.0}, NodeId{1}, {root, child}), Color{0, 0, 0, 0});
    CHECK(result.complete());
    CHECK(result.surface.At(0, 0) == PackPremultiplied(0x80, 0x80, 0x00, 0x00));
    CHECK(result.surface.At(1, 0) == PackPremultiplied(0x80, 0x00, 0x00, 0x80));
    CHECK(result.surface.At(2, 0) == PackPremultiplied(0x80, 0x00, 0x00, 0x80));
}

void InvalidTopologyIsReportedBeforeRendering() {
    VisualNode root = Node(1);
    root.children = {NodeId{99}};
    root.content = Fill(Rect{0.0, 0.0, 2.0, 2.0}, kRed);

    SceneSnapshot scene(Size{2.0, 2.0}, NodeId{1}, {root});
    const RasterResult result = CpuRasterBackend{}.Render(scene, kClear);

    CHECK(HasIssue(result, RenderIssueCode::InvalidScene));
    CHECK(result.surface.At(0, 0) == Pack(kClear));
}

class RecordingTextRasterizer final : public TextRasterizer {
public:
    bool DrawText(const TextRasterRequest& request, Surface& surface,
                  std::string&) override {
        called = true;
        node = request.node;
        bounds = request.bounds;
        clipped = request.has_clip;
        clip = request.clip;
        text = request.text.text;
        surface.FillRect(Rect{bounds.x, bounds.y, 1.0, 1.0}, request.text.color);
        return true;
    }

    bool called = false;
    bool clipped = false;
    NodeId node;
    Rect bounds;
    Rect clip;
    std::string text;
};

SceneSnapshot TextScene() {
    VisualNode root = Node(1);
    root.local_transform = Matrix3x2::Translation(2.0, 3.0);
    root.clip = Clip::FromRect(Rect{0.0, 0.0, 5.0, 5.0});
    auto content = std::make_shared<LocalDisplayList>();
    LocalText text;
    text.bounds = Rect{1.0, 1.0, 3.0, 2.0};
    text.text = "hi";
    text.font_family = "Test";
    text.font_size = 12.0;
    text.baseline = 9.0;
    text.advances = {3.0, 2.0};
    text.color = kBlue;
    content->commands.push_back(std::move(text));
    root.content = std::move(content);
    return SceneSnapshot(Size{10.0, 10.0}, NodeId{1}, {root});
}

void TextIsDelegatedOrReported() {
    const SceneSnapshot scene = TextScene();
    const RasterResult missing = CpuRasterBackend{}.Render(scene, kClear);
    CHECK(HasIssue(missing, RenderIssueCode::MissingTextRasterizer, NodeId{1}));

    RecordingTextRasterizer rasterizer;
    const RasterResult rendered = CpuRasterBackend{}.Render(scene, kClear, &rasterizer);
    CHECK(rendered.complete());
    CHECK(rasterizer.called);
    CHECK(rasterizer.node == NodeId{1});
    CHECK(rasterizer.text == "hi");
    CHECK(rasterizer.bounds.x == 3.0 && rasterizer.bounds.y == 4.0);
    CHECK(rasterizer.clipped);
    CHECK(rasterizer.clip.x == 2.0 && rasterizer.clip.y == 3.0);
    CHECK(rendered.surface.At(3, 4) == Pack(kBlue));
}

void TransformedTextIsACommandIssueAndIsNeverPaintedUntransformed() {
    VisualNode root = Node(1);
    root.local_transform = Matrix3x2::RotationAround(20.0, {1.0, 1.0});
    auto content = std::make_shared<LocalDisplayList>();
    LocalText text;
    text.bounds = Rect{0.0, 0.0, 2.0, 2.0};
    text.text = "x";
    text.color = kBlue;
    content->commands.push_back(text);
    root.content = std::move(content);

    RecordingTextRasterizer rasterizer;
    const RasterResult result = CpuRasterBackend{}.Render(
        SceneSnapshot(Size{4.0, 4.0}, NodeId{1}, {root}), kClear, &rasterizer);
    CHECK(HasIssue(result, RenderIssueCode::UnsupportedTransform, NodeId{1}));
    CHECK(!rasterizer.called);
    CHECK(result.surface.At(0, 0) == Pack(kClear));
}

void CancellingTransformsDelegateTextAsTheResultingIdentity() {
    VisualNode parent = Node(1);
    parent.children = {NodeId{2}};
    parent.local_transform = Matrix3x2::RotationAround(17.0, {1.0, 1.0});
    VisualNode child = Node(2, 1);
    child.local_transform = Matrix3x2::RotationAround(-17.0, {1.0, 1.0});
    auto content = std::make_shared<LocalDisplayList>();
    LocalText text;
    text.bounds = Rect{0.0, 0.0, 2.0, 2.0};
    text.text = "x";
    text.color = kBlue;
    content->commands.push_back(text);
    child.content = std::move(content);

    RecordingTextRasterizer rasterizer;
    const RasterResult result = CpuRasterBackend{}.Render(
        SceneSnapshot(Size{4.0, 4.0}, NodeId{1}, {parent, child}), kClear,
        &rasterizer);
    CHECK(result.complete());
    CHECK(rasterizer.called);
}

void ExternalSurfacesAreNamedUntilACompositorBackendExists() {
    VisualNode root = Node(1);
    auto content = std::make_shared<LocalDisplayList>();
    ExternalSurfaceReference source;
    source.kind = ExternalSurfaceKind::CompositionSurfaceHandle;
    source.generation = 3;
    source.native_value = 0x1234;
    source.lifetime = std::make_shared<int>(1);
    content->commands.push_back(
        LocalExternalSurface{Rect{0.0, 0.0, 2.0, 2.0}, std::move(source)});
    content->commands.push_back(LocalFillRect{Rect{0.0, 0.0, 1.0, 1.0}, kGreen});
    root.content = std::move(content);

    const RasterResult result = CpuRasterBackend{}.Render(
        SceneSnapshot(Size{2.0, 2.0}, NodeId{1}, {root}), kClear);
    CHECK(HasIssue(result, RenderIssueCode::UnsupportedExternalSurface,
                   NodeId{1}));
    CHECK(result.issues.size() == 1);
    CHECK(result.surface.At(0, 0) == Pack(kGreen));
    CHECK(result.surface.At(1, 1) == Pack(kClear));
}

void SourcedImageBrushesAreRetainedAsANamedBackendBoundary() {
    VisualNode root = Node(1);
    auto content = std::make_shared<LocalDisplayList>();
    content->commands.push_back(
        LocalImageBrushFill{Rect{0.0, 0.0, 2.0, 2.0}, "Assets/Backdrop.png"});
    content->commands.push_back(LocalFillRect{Rect{0.0, 0.0, 1.0, 1.0}, kGreen});
    root.content = std::move(content);

    const RasterResult result = CpuRasterBackend{}.Render(
        SceneSnapshot(Size{2.0, 2.0}, NodeId{1}, {root}), kClear);
    CHECK(HasIssue(result, RenderIssueCode::UnsupportedImageBrush, NodeId{1}));
    CHECK(result.issues.size() == 1);
    if (!result.issues.empty()) {
        CHECK(result.issues[0].command_index == 0);
        CHECK(result.issues[0].message.find("decoding and sampling") !=
              std::string::npos);
    }
    // A later supported command still paints; the image was reported, not
    // mistaken for a reason to discard or flatten the whole local list.
    CHECK(result.surface.At(0, 0) == Pack(kGreen));
    CHECK(result.surface.At(1, 1) == Pack(kClear));
}

}  // namespace

int main() {
    LocalTranslationsAndPaintOrderAreDeterministic();
    RectangularClipsComposeAcrossTheTree();
    InvisibleNodesAndZeroOpacitySkipTheirSubtrees();
    AffineSolidFillsAreRasterizedInsteadOfRefused();
    DeclaredUnsupportedTransformsAreIssuesAndNeverPaintUntransformed();
    RotatedCoverageClipAndOpacityHaveExactPremultipliedVectors();
    EquivalentAxisAlignedTransformsUseOneExactRasterRule();
    UnrepresentableExtentsAndAffineOverflowAreNamedIssues();
    FractionalFillsUseExactPremultipliedSourceOver();
    OpacityCompositesTheSubtreeOnce();
    InvalidTopologyIsReportedBeforeRendering();
    TextIsDelegatedOrReported();
    TransformedTextIsACommandIssueAndIsNeverPaintedUntransformed();
    CancellingTransformsDelegateTextAsTheResultingIdentity();
    ExternalSurfacesAreNamedUntilACompositorBackendExists();
    SourcedImageBrushesAreRetainedAsANamedBackendBoundary();

    if (failures != 0) {
        std::cerr << failures << " CPU raster backend check(s) failed\n";
        return 1;
    }
    std::cout << "CPU raster backend checks passed\n";
    return 0;
}
