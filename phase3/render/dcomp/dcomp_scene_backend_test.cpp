#include "dcomp_scene_backend.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace openxaml;
using namespace openxaml::render;

[[noreturn]] void Fail(const std::string& message) {
    std::cerr << message << '\n';
    std::exit(1);
}

void Check(bool condition, const std::string& message) {
    if (!condition) Fail(message);
}

enum class FakeKind { Visual, CpuSurface, External };

struct FakeObject final : DcompObject {
    explicit FakeObject(FakeKind kind_, std::uint64_t serial_) : kind(kind_), serial(serial_) {}

    FakeKind kind;
    std::uint64_t serial;
    DcompVisualProperties properties{};
    DcompObjectPtr content;
    std::vector<DcompObjectPtr> children;
    std::vector<std::uint32_t> pixels;
    int width = 0;
    int height = 0;
    ExternalSurfaceReference external{};
    FakeObject* parent = nullptr;
};

std::shared_ptr<FakeObject> AsFake(const DcompObjectPtr& value) {
    return std::dynamic_pointer_cast<FakeObject>(value);
}

class FakePlatform final : public DcompPlatform {
public:
    HRESULT CreateVisual(DcompObjectPtr& visual) noexcept override {
        if (fail_create_after == 0) return E_ACCESSDENIED;
        if (fail_create_after > 0) --fail_create_after;
        try {
            visual = std::make_shared<FakeObject>(FakeKind::Visual, ++serial);
            ++create_visual_calls;
            return S_OK;
        } catch (...) {
            return E_OUTOFMEMORY;
        }
    }

    HRESULT ConfigureVisual(const DcompObjectPtr& visual,
                            const DcompVisualProperties& properties) noexcept override {
        const auto object = AsFake(visual);
        if (!object || object->kind != FakeKind::Visual) return E_INVALIDARG;
        object->properties = properties;
        return S_OK;
    }

    HRESULT SetContent(const DcompObjectPtr& visual,
                       const DcompObjectPtr& content) noexcept override {
        const auto object = AsFake(visual);
        if (!object || object->kind != FakeKind::Visual || !content) return E_INVALIDARG;
        object->content = content;
        return S_OK;
    }

    HRESULT SetChildren(const DcompObjectPtr& visual,
                        const std::vector<DcompObjectPtr>& children) noexcept override {
        const auto object = AsFake(visual);
        if (!object || object->kind != FakeKind::Visual) return E_INVALIDARG;
        for (const auto& child : children) {
            const auto child_object = AsFake(child);
            if (!child_object || child_object->kind != FakeKind::Visual ||
                (child_object->parent && child_object->parent != object.get())) {
                return E_INVALIDARG;
            }
        }
        for (const auto& child : children) AsFake(child)->parent = object.get();
        object->children = children;
        return S_OK;
    }

    HRESULT UploadCpuSurface(const Surface& surface, DcompObjectPtr& content) noexcept override {
        try {
            auto object = std::make_shared<FakeObject>(FakeKind::CpuSurface, ++serial);
            object->width = surface.width();
            object->height = surface.height();
            object->pixels = surface.pixels();
            content = std::move(object);
            ++upload_calls;
            return S_OK;
        } catch (...) {
            return E_OUTOFMEMORY;
        }
    }

    HRESULT ImportExternalSurface(const ExternalSurfaceReference& source,
                                  DcompObjectPtr& content) noexcept override {
        if (!source || source.kind != ExternalSurfaceKind::CompositionSurfaceHandle) {
            return E_INVALIDARG;
        }
        try {
            auto object = std::make_shared<FakeObject>(FakeKind::External, ++serial);
            object->external = source;
            content = std::move(object);
            ++import_calls;
            return S_OK;
        } catch (...) {
            return E_OUTOFMEMORY;
        }
    }

    HRESULT PublishRoot(const DcompObjectPtr& root) noexcept override {
        ++publish_calls;
        if (FAILED(publish_result)) return publish_result;
        published_root = root;
        return S_OK;
    }

    HRESULT DetachRoot() noexcept override {
        ++detach_calls;
        if (FAILED(detach_result)) return detach_result;
        published_root.reset();
        return S_OK;
    }

    std::uint64_t serial = 0;
    int create_visual_calls = 0;
    int upload_calls = 0;
    int import_calls = 0;
    int publish_calls = 0;
    int detach_calls = 0;
    int fail_create_after = -1;
    HRESULT publish_result = S_OK;
    HRESULT detach_result = S_OK;
    DcompObjectPtr published_root;
};

std::shared_ptr<const LocalDisplayList> ThreeLayerContent(
    const ExternalSurfaceReference& external) {
    auto list = std::make_shared<LocalDisplayList>();
    list->commands.push_back(LocalFillRect{{0, 0, 2, 2}, Color{255, 255, 0, 0}});
    list->commands.push_back(LocalExternalSurface{{0, 0, 2, 2}, external});
    list->commands.push_back(LocalFillRect{{0, 0, 2, 2}, Color{255, 0, 0, 255}});
    return list;
}

SceneSnapshot OneNodeScene(const std::shared_ptr<const LocalDisplayList>& content,
                           double opacity = 1.0) {
    VisualNode root{};
    root.id = NodeId{1};
    root.local_bounds = {0, 0, 2, 2};
    root.local_transform = Matrix3x2::Translation(4, 7);
    root.clip = Clip::FromRect({0, 0, 2, 2});
    root.opacity = opacity;
    root.content = content;
    return SceneSnapshot({20, 20}, root.id, {std::move(root)});
}

SceneSnapshot TwoNodeScene(const std::shared_ptr<const LocalDisplayList>& root_content,
                           const std::shared_ptr<const LocalDisplayList>& child_content,
                           bool child_visible = true, double child_opacity = 1.0,
                           double root_opacity = 1.0) {
    VisualNode root{};
    root.id = NodeId{1};
    root.children = {NodeId{2}};
    root.local_bounds = {0, 0, 4, 4};
    root.opacity = root_opacity;
    root.content = root_content;

    VisualNode child{};
    child.id = NodeId{2};
    child.parent = root.id;
    child.local_bounds = {0, 0, 2, 2};
    child.visible = child_visible;
    child.opacity = child_opacity;
    child.content = child_content;
    return SceneSnapshot({4, 4}, root.id, {std::move(root), std::move(child)});
}

class FixedPublishGuard final : public DcompPublishGuard {
public:
    explicit FixedPublishGuard(bool allowed) : allowed_(allowed) {}
    bool CanPublish() noexcept override {
        ++calls;
        return allowed_;
    }
    int calls = 0;

private:
    bool allowed_;
};

void StrataPreserveExternalZOrderAndProperties() {
    auto platform = std::make_shared<FakePlatform>();
    DcompSceneBackend backend(platform);
    auto lifetime = std::make_shared<int>(42);
    ExternalSurfaceReference external{ExternalSurfaceKind::CompositionSurfaceHandle, 7, 0x1234,
                                      lifetime};
    const auto content = ThreeLayerContent(external);

    const auto result = backend.Update(OneNodeScene(content, 0.5), {{NodeId{1}, 11}});
    Check(result.complete(), "initial retained DComp update failed");
    Check(result.generation == 1 && result.extent.width == 20 && result.extent.height == 20,
          "committed generation or extent is wrong");
    Check(result.stats.uploaded_cpu_surfaces == 2 &&
              result.stats.imported_external_surfaces == 1,
          "initial update did not create two CPU strata and one external import");

    const auto root = AsFake(platform->published_root);
    Check(root && root->children.size() == 3, "root paint list does not have three strata");
    Check(root->properties.transform.dx == 4 && root->properties.transform.dy == 7 &&
              root->properties.offset_x == 0 && root->properties.offset_y == 0,
          "node translation was not represented exactly once");
    Check(root->properties.opacity == 0.5 && root->properties.has_clip,
          "node group opacity/clip was not retained on the container");

    const auto below = AsFake(root->children[0]);
    const auto middle = AsFake(root->children[1]);
    const auto above = AsFake(root->children[2]);
    Check(AsFake(below->content)->kind == FakeKind::CpuSurface &&
              AsFake(middle->content)->kind == FakeKind::External &&
              AsFake(above->content)->kind == FakeKind::CpuSurface,
          "external content was not kept between its CPU strata");
    Check(AsFake(below->content)->pixels[0] == Pack(Color{255, 255, 0, 0}),
          "lower CPU stratum does not contain the red command");
    Check(AsFake(above->content)->pixels[0] == Pack(Color{255, 0, 0, 255}),
          "upper CPU stratum does not contain the blue overlay command");
    Check(middle->properties.offset_x == 0 && middle->properties.offset_y == 0 &&
              middle->properties.has_clip && middle->properties.clip.width == 2,
          "external command bounds were not projected as offset plus local clip");
}

void StableVersionsReuseSubtreesAndResources() {
    auto platform = std::make_shared<FakePlatform>();
    DcompSceneBackend backend(platform);
    auto lifetime = std::make_shared<int>(1);
    ExternalSurfaceReference external{ExternalSurfaceKind::CompositionSurfaceHandle, 5, 0x88,
                                      lifetime};
    const auto content = ThreeLayerContent(external);
    auto first = backend.Update(OneNodeScene(content), {{NodeId{1}, 1}});
    Check(first.complete(), "reuse setup update failed");
    const DcompObjectPtr first_root = backend.root();
    const int creates = platform->create_visual_calls;
    const int uploads = platform->upload_calls;
    const int imports = platform->import_calls;
    const int publishes = platform->publish_calls;

    auto repeated = backend.Update(OneNodeScene(content), {{NodeId{1}, 1}});
    Check(repeated.complete() && repeated.reused_root && repeated.generation == 1,
          "unchanged NodeId+generation did not reuse the committed root");
    Check(backend.root() == first_root && platform->create_visual_calls == creates &&
              platform->upload_calls == uploads && platform->import_calls == imports &&
              platform->publish_calls == publishes,
          "unchanged update performed compositor work");

    auto changed = backend.Update(OneNodeScene(content), {{NodeId{1}, 2}});
    Check(changed.complete() && changed.generation == 2 && !changed.reused_root,
          "changed node generation did not publish a new visual tree");
    Check(changed.stats.reused_cpu_surfaces == 2 &&
              changed.stats.reused_external_surfaces == 1,
          "immutable CPU/import resources were not reused across a node rebuild");
    Check(platform->upload_calls == uploads && platform->import_calls == imports,
          "node-only mutation re-uploaded immutable content");
}

void InvisibleAndZeroOpacityBranchesDoNotLowerDescendants() {
    auto platform = std::make_shared<FakePlatform>();
    DcompSceneBackend backend(platform);
    auto unsupported = std::make_shared<LocalDisplayList>();
    auto external_lifetime = std::make_shared<int>(7);
    unsupported->commands.push_back(LocalExternalSurface{
        {0, 0, 2, 2},
        ExternalSurfaceReference{ExternalSurfaceKind::CompositionSurfaceHandle,
                                 1, 0x777, external_lifetime}});
    unsupported->commands.push_back(
        LocalImageBrushFill{{0, 0, 2, 2}, "must-not-be-decoded.png"});
    auto empty = std::make_shared<LocalDisplayList>();

    auto invisible = backend.Update(TwoNodeScene(empty, unsupported, false),
                                    {{NodeId{1}, 1}, {NodeId{2}, 1}});
    const auto invisible_root = AsFake(platform->published_root);
    const auto invisible_placeholder = invisible_root && !invisible_root->children.empty()
        ? AsFake(invisible_root->children.front()) : nullptr;
    Check(invisible.complete() && platform->upload_calls == 0 &&
              platform->import_calls == 0 && invisible_placeholder &&
              invisible_placeholder->children.empty() &&
              invisible_placeholder->properties.opacity == 1.0,
          "an invisible branch lowered an unreachable command or external resource");

    auto transparent = backend.Update(TwoNodeScene(empty, unsupported, true, 0.0),
                                      {{NodeId{1}, 2}, {NodeId{2}, 2}});
    Check(transparent.complete() && platform->upload_calls == 0 &&
              platform->import_calls == 0,
          "a zero-opacity branch lowered an unreachable command or external resource");
}

void PartialChangesRebuildTheAncestorClosure() {
    auto platform = std::make_shared<FakePlatform>();
    DcompSceneBackend backend(platform);
    auto empty = std::make_shared<LocalDisplayList>();
    Check(backend.Update(TwoNodeScene(empty, empty),
                         {{NodeId{1}, 1}, {NodeId{2}, 1}}).complete(),
          "partial-reuse setup failed");

    // The child generation stays stable while its parent changes. Reusing the
    // committed child visual would ask real DComp to parent it twice.
    const auto changed = backend.Update(TwoNodeScene(empty, empty, true, 1.0, 0.5),
                                        {{NodeId{1}, 2}, {NodeId{2}, 1}});
    Check(changed.complete() && changed.stats.reused_nodes == 0,
          "a partial update reused an already-parented child visual");
}

void PublicationGuardAbandonsAStaleStagedTree() {
    auto platform = std::make_shared<FakePlatform>();
    DcompSceneBackend backend(platform);
    auto empty = std::make_shared<LocalDisplayList>();
    FixedPublishGuard stale(false);
    const auto canceled = backend.Update(OneNodeScene(empty), {{NodeId{1}, 1}}, nullptr,
                                         &stale);
    Check(!canceled.committed && canceled.error == E_ABORT && stale.calls == 1 &&
              platform->publish_calls == 0 && !backend.ready() &&
              !canceled.issues.empty() &&
              canceled.issues.front().code == DcompIssueCode::PublicationCanceled,
          "a stale host transaction reached DirectComposition Commit");

    FixedPublishGuard current(true);
    const auto committed = backend.Update(OneNodeScene(empty), {{NodeId{1}, 1}}, nullptr,
                                          &current);
    Check(committed.complete() && current.calls == 1 && platform->publish_calls == 1,
          "a current host transaction was not published");
}

void FailedCommitRetainsLastGoodTree() {
    auto platform = std::make_shared<FakePlatform>();
    DcompSceneBackend backend(platform);
    auto lifetime = std::make_shared<int>(1);
    ExternalSurfaceReference external{ExternalSurfaceKind::CompositionSurfaceHandle, 1, 0x99,
                                      lifetime};
    const auto content = ThreeLayerContent(external);
    Check(backend.Update(OneNodeScene(content), {{NodeId{1}, 1}}).complete(),
          "transaction setup failed");
    const DcompObjectPtr committed_root = backend.root();
    platform->publish_result = E_NOTIMPL;

    auto failed = backend.Update(OneNodeScene(content, 0.25), {{NodeId{1}, 2}});
    Check(!failed.committed && failed.error == E_NOTIMPL,
          "failed platform Commit was not reported exactly");
    Check(backend.ready() && backend.generation() == 1 && backend.root() == committed_root &&
              platform->published_root == committed_root,
          "failed Commit replaced the last good retained tree");

    platform->publish_result = S_OK;
    auto recovered = backend.Update(OneNodeScene(content, 0.25), {{NodeId{1}, 2}});
    Check(recovered.complete() && recovered.generation == 2,
          "backend did not recover transactionally after a failed Commit");

    platform->detach_result = E_ACCESSDENIED;
    Check(backend.Detach() == E_ACCESSDENIED && backend.ready(),
          "failed detach discarded committed backend state");
    platform->detach_result = S_OK;
    Check(backend.Detach() == S_OK && !backend.ready() && backend.generation() == 3,
          "successful detach did not atomically retire the retained state");
}

void VersionAndUnsupportedSemanticsAreNamed() {
    auto platform = std::make_shared<FakePlatform>();
    DcompSceneBackend backend(platform);
    auto list = std::make_shared<LocalDisplayList>();
    list->commands.push_back(LocalImageBrushFill{{0, 0, 2, 2}, "asset.png"});
    const auto missing_version = backend.Update(OneNodeScene(list), {});
    Check(!missing_version.committed && !missing_version.issues.empty() &&
              missing_version.issues.front().code == DcompIssueCode::InvalidVersions,
          "missing node generation was not a named refusal");

    const auto image = backend.Update(OneNodeScene(list), {{NodeId{1}, 1}});
    Check(!image.committed && !image.render_issues.empty() &&
              image.render_issues.front().code == RenderIssueCode::UnsupportedImageBrush,
          "unsupported CPU stratum semantic was silently dropped");
    Check(platform->publish_calls == 0, "unsupported scene reached compositor Commit");
}

}  // namespace

int main() {
    StrataPreserveExternalZOrderAndProperties();
    StableVersionsReuseSubtreesAndResources();
    InvisibleAndZeroOpacityBranchesDoNotLowerDescendants();
    PartialChangesRebuildTheAncestorClosure();
    PublicationGuardAbandonsAStaleStagedTree();
    FailedCommitRetainsLastGoodTree();
    VersionAndUnsupportedSemanticsAreNamed();
    std::cout << "DComp scene backend checks passed\n";
    return 0;
}
