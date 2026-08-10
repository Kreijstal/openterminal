#include "dcomp_scene_backend.h"

#include <d3d11.h>
#include <dcomp.h>
#include <dxgi.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <optional>
#include <unordered_map>
#include <utility>

namespace openxaml {
namespace render {
namespace {

template <typename T>
void SafeRelease(T*& value) noexcept {
    if (value) {
        value->Release();
        value = nullptr;
    }
}

bool Finite(double value) noexcept { return std::isfinite(value); }

bool ValidRect(const Rect& rect) noexcept {
    return Finite(rect.x) && Finite(rect.y) && Finite(rect.width) && Finite(rect.height) &&
           rect.width >= 0.0 && rect.height >= 0.0;
}

bool Floatable(double value) noexcept {
    return Finite(value) &&
           std::abs(value) <= static_cast<double>(std::numeric_limits<float>::max());
}

const Rect& CommandBounds(const DrawCommand& command) {
    return std::visit([](const auto& value) -> const Rect& { return value.bounds; }, command);
}

struct ExternalSignature {
    ExternalSurfaceKind kind = ExternalSurfaceKind::None;
    std::uint64_t generation = 0;
    std::uintptr_t native_value = 0;
    const void* lifetime = nullptr;

    bool operator==(const ExternalSignature& other) const noexcept {
        return kind == other.kind && generation == other.generation &&
               native_value == other.native_value && lifetime == other.lifetime;
    }
};

std::vector<ExternalSignature> ExternalSignatures(const VisualNode& node) {
    std::vector<ExternalSignature> result;
    if (!node.content) return result;
    for (const auto& command : node.content->commands) {
        if (const auto* external = std::get_if<LocalExternalSurface>(&command)) {
            result.push_back({external->source.kind, external->source.generation,
                              external->source.native_value, external->source.lifetime.get()});
        }
    }
    return result;
}

struct SurfaceKey {
    NodeId node{};
    std::size_t stratum = 0;
    const LocalDisplayList* display_list = nullptr;
    const TextRasterizer* text_rasterizer = nullptr;
    std::size_t begin = 0;
    std::size_t end = 0;
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;

    bool operator==(const SurfaceKey& other) const noexcept {
        return node == other.node && stratum == other.stratum &&
               display_list == other.display_list && text_rasterizer == other.text_rasterizer &&
               begin == other.begin && end == other.end &&
               left == other.left && top == other.top && right == other.right &&
               bottom == other.bottom;
    }
};

struct SurfaceKeyHash {
    std::size_t operator()(const SurfaceKey& key) const noexcept {
        std::size_t hash = static_cast<std::size_t>(key.node.value);
        auto mix = [&hash](std::size_t value) {
            hash ^= value + static_cast<std::size_t>(0x9e3779b9u) + (hash << 6) + (hash >> 2);
        };
        mix(key.stratum);
        mix(reinterpret_cast<std::size_t>(key.display_list));
        mix(reinterpret_cast<std::size_t>(key.text_rasterizer));
        mix(key.begin);
        mix(key.end);
        mix(static_cast<std::size_t>(key.left));
        mix(static_cast<std::size_t>(key.top));
        mix(static_cast<std::size_t>(key.right));
        mix(static_cast<std::size_t>(key.bottom));
        return hash;
    }
};

struct ExternalKey {
    NodeId node{};
    std::size_t command = 0;
    ExternalSignature source{};

    bool operator==(const ExternalKey& other) const noexcept {
        return node == other.node && command == other.command && source == other.source;
    }
};

struct ExternalKeyHash {
    std::size_t operator()(const ExternalKey& key) const noexcept {
        std::size_t hash = static_cast<std::size_t>(key.node.value ^ key.source.generation);
        hash ^= key.command + (hash << 6) + (hash >> 2);
        hash ^= static_cast<std::size_t>(key.source.native_value) + (hash << 6) + (hash >> 2);
        hash ^= reinterpret_cast<std::size_t>(key.source.lifetime) + (hash << 6) + (hash >> 2);
        hash ^= static_cast<std::size_t>(key.source.kind) + (hash << 6) + (hash >> 2);
        return hash;
    }
};

struct SurfaceEntry {
    DcompObjectPtr content;
    std::shared_ptr<const LocalDisplayList> display_list_lifetime;
};

struct ExternalEntry {
    DcompObjectPtr content;
    std::shared_ptr<const void> source_lifetime;
};

struct NodeEntry {
    std::uint64_t version = 0;
    std::shared_ptr<const LocalDisplayList> display_list;
    std::vector<ExternalSignature> external_signatures;
    std::vector<NodeId> child_ids;
    std::vector<DcompObjectPtr> child_nodes;
    DcompObjectPtr visual;
};

struct NodeIdHash {
    std::size_t operator()(NodeId id) const noexcept {
        return static_cast<std::size_t>(id.value ^ (id.value >> 32));
    }
};

struct PixelBounds {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
};

bool CommandRangeBounds(const LocalDisplayList& list, std::size_t begin, std::size_t end,
                        PixelBounds& bounds) noexcept {
    bool have_bounds = false;
    constexpr double kIntMin = static_cast<double>(std::numeric_limits<int>::min()) + 1.0;
    constexpr double kIntMax = static_cast<double>(std::numeric_limits<int>::max()) - 1.0;
    for (std::size_t index = begin; index < end; ++index) {
        const Rect& rect = CommandBounds(list.commands[index]);
        if (!ValidRect(rect)) return false;
        if (rect.width == 0.0 || rect.height == 0.0) continue;
        const double right = rect.x + rect.width;
        const double bottom = rect.y + rect.height;
        if (!Finite(right) || !Finite(bottom) || rect.x < kIntMin || rect.y < kIntMin ||
            right > kIntMax || bottom > kIntMax) {
            return false;
        }
        const int left_pixel = static_cast<int>(std::floor(rect.x));
        const int top_pixel = static_cast<int>(std::floor(rect.y));
        const int right_pixel = static_cast<int>(std::ceil(right));
        const int bottom_pixel = static_cast<int>(std::ceil(bottom));
        if (!have_bounds) {
            bounds = {left_pixel, top_pixel, right_pixel, bottom_pixel};
            have_bounds = true;
        } else {
            bounds.left = std::min(bounds.left, left_pixel);
            bounds.top = std::min(bounds.top, top_pixel);
            bounds.right = std::max(bounds.right, right_pixel);
            bounds.bottom = std::max(bounds.bottom, bottom_pixel);
        }
    }
    return !have_bounds || (bounds.right > bounds.left && bounds.bottom > bounds.top);
}

class WindowsObject final : public DcompObject {
public:
    explicit WindowsObject(IUnknown* value) noexcept : value_(value) {}
    ~WindowsObject() override { SafeRelease(value_); }

    IUnknown* get() const noexcept { return value_; }

    template <typename T>
    HRESULT CopyAs(T** value) const noexcept {
        if (!value) return E_POINTER;
        *value = nullptr;
        return value_ ? value_->QueryInterface(__uuidof(T), reinterpret_cast<void**>(value))
                      : E_NOINTERFACE;
    }

private:
    IUnknown* value_ = nullptr;
};

}  // namespace

class DcompSceneBackend::Impl {
public:
    explicit Impl(std::shared_ptr<DcompPlatform> platform_) : platform(std::move(platform_)) {}

    struct BuildState {
        const SceneSnapshot& scene;
        const std::unordered_map<NodeId, std::uint64_t, NodeIdHash>& versions;
        TextRasterizer* text_rasterizer = nullptr;
        DcompUpdateResult& result;
        std::unordered_map<NodeId, NodeEntry, NodeIdHash> nodes;
        std::unordered_map<SurfaceKey, SurfaceEntry, SurfaceKeyHash> surfaces;
        std::unordered_map<ExternalKey, ExternalEntry, ExternalKeyHash> externals;
    };

    bool Fail(BuildState& state, DcompIssueCode code, NodeId node, std::size_t command,
              HRESULT error, std::string message) noexcept {
        state.result.error = FAILED(error) ? error : E_FAIL;
        try {
            state.result.issues.push_back({code, node, command, error, std::move(message)});
        } catch (...) {
            // The HRESULT remains available even when diagnostics allocation fails.
        }
        return false;
    }

    bool PlatformCall(BuildState& state, HRESULT error, NodeId node, std::size_t command,
                      const char* operation) noexcept {
        if (SUCCEEDED(error)) return true;
        return Fail(state, DcompIssueCode::PlatformFailure, node, command, error, operation);
    }

    bool ValidVisualProperties(const VisualNode& node) const noexcept {
        const Matrix3x2& value = node.local_transform;
        if (!Floatable(value.m11) || !Floatable(value.m12) || !Floatable(value.m21) ||
            !Floatable(value.m22) || !Floatable(value.dx) || !Floatable(value.dy) ||
            !Finite(node.opacity) || node.opacity < 0.0 || node.opacity > 1.0) {
            return false;
        }
        return node.clip.kind != Clip::Kind::Rect ||
               (ValidRect(node.clip.bounds) && Floatable(node.clip.bounds.x) &&
                Floatable(node.clip.bounds.y) && Floatable(node.clip.bounds.width) &&
                Floatable(node.clip.bounds.height));
    }

    bool MakeContentVisual(BuildState& state, NodeId node, std::size_t command,
                           const DcompObjectPtr& content, const DcompVisualProperties& properties,
                           DcompObjectPtr& result) noexcept {
        if (!PlatformCall(state, platform->CreateVisual(result), node, command,
                          "DirectComposition CreateVisual failed")) {
            return false;
        }
        ++state.result.stats.created_visuals;
        if (!PlatformCall(state, platform->ConfigureVisual(result, properties), node, command,
                          "DirectComposition visual configuration failed")) {
            return false;
        }
        return PlatformCall(state, platform->SetContent(result, content), node, command,
                            "DirectComposition SetContent failed");
    }

    bool BuildCpuStratum(BuildState& state, const VisualNode& owner, std::size_t stratum,
                         std::size_t begin, std::size_t end, DcompObjectPtr& visual) noexcept {
        PixelBounds pixel_bounds{};
        if (!CommandRangeBounds(*owner.content, begin, end, pixel_bounds)) {
            return Fail(state, DcompIssueCode::InvalidGeometry, owner.id, begin, E_INVALIDARG,
                        "CPU stratum bounds are invalid or outside the integer pixel domain");
        }
        if (pixel_bounds.right <= pixel_bounds.left || pixel_bounds.bottom <= pixel_bounds.top) {
            return true;
        }

        SurfaceKey key{owner.id, stratum, owner.content.get(), state.text_rasterizer, begin, end,
                       pixel_bounds.left, pixel_bounds.top, pixel_bounds.right,
                       pixel_bounds.bottom};
        DcompObjectPtr content;
        const auto existing = state.surfaces.find(key);
        if (existing != state.surfaces.end()) {
            content = existing->second.content;
            ++state.result.stats.reused_cpu_surfaces;
        } else {
            auto commands = std::make_shared<LocalDisplayList>();
            commands->commands.insert(commands->commands.end(),
                                      owner.content->commands.begin() + begin,
                                      owner.content->commands.begin() + end);

            VisualNode local{};
            local.id = owner.id;
            local.local_bounds = {0.0, 0.0,
                                  static_cast<double>(pixel_bounds.right - pixel_bounds.left),
                                  static_cast<double>(pixel_bounds.bottom - pixel_bounds.top)};
            local.local_transform = Matrix3x2::Translation(-pixel_bounds.left, -pixel_bounds.top);
            local.opacity = 1.0;
            local.visible = true;
            local.content = std::move(commands);

            SceneSnapshot local_scene(
                {local.local_bounds.width, local.local_bounds.height}, owner.id, {std::move(local)});
            RasterResult raster = cpu.Render(local_scene, Color{0, 0, 0, 0},
                                             state.text_rasterizer);
            if (!raster.complete()) {
                for (auto issue : raster.issues) {
                    if (issue.command_index != RenderIssue::kNoCommand) {
                        issue.command_index += begin;
                    }
                    state.result.render_issues.push_back(std::move(issue));
                }
                return Fail(state, DcompIssueCode::CpuRasterFailure, owner.id, begin, E_NOTIMPL,
                            "CPU stratum contains a named unsupported render semantic");
            }
            if (!PlatformCall(state, platform->UploadCpuSurface(raster.surface, content), owner.id,
                              begin, "DirectComposition CPU surface upload failed")) {
                return false;
            }
            ++state.result.stats.uploaded_cpu_surfaces;
            state.surfaces.emplace(key, SurfaceEntry{content, owner.content});
        }

        DcompVisualProperties properties{};
        properties.offset_x = pixel_bounds.left;
        properties.offset_y = pixel_bounds.top;
        return MakeContentVisual(state, owner.id, begin, content, properties, visual);
    }

    bool BuildExternal(BuildState& state, const VisualNode& owner, std::size_t command_index,
                       const LocalExternalSurface& external, DcompObjectPtr& visual) noexcept {
        if (!external.source || external.source.generation == 0) {
            return Fail(state, DcompIssueCode::InvalidExternalSurface, owner.id, command_index,
                        E_INVALIDARG,
                        "external surface requires kind, native value, lifetime and generation");
        }
        if (!ValidRect(external.bounds) || !Floatable(external.bounds.x) ||
            !Floatable(external.bounds.y) || !Floatable(external.bounds.width) ||
            !Floatable(external.bounds.height)) {
            return Fail(state, DcompIssueCode::InvalidGeometry, owner.id, command_index,
                        E_INVALIDARG, "external surface bounds are invalid");
        }
        if (external.bounds.width == 0.0 || external.bounds.height == 0.0) return true;

        ExternalSignature signature{external.source.kind, external.source.generation,
                                    external.source.native_value,
                                    external.source.lifetime.get()};
        ExternalKey key{owner.id, command_index, signature};
        DcompObjectPtr content;
        const auto existing = state.externals.find(key);
        if (existing != state.externals.end()) {
            content = existing->second.content;
            ++state.result.stats.reused_external_surfaces;
        } else {
            if (!PlatformCall(state, platform->ImportExternalSurface(external.source, content),
                              owner.id, command_index,
                              "DirectComposition external surface import failed")) {
                return false;
            }
            ++state.result.stats.imported_external_surfaces;
            state.externals.emplace(key, ExternalEntry{content, external.source.lifetime});
        }

        DcompVisualProperties properties{};
        properties.offset_x = external.bounds.x;
        properties.offset_y = external.bounds.y;
        properties.has_clip = true;
        properties.clip = {0.0, 0.0, external.bounds.width, external.bounds.height};
        return MakeContentVisual(state, owner.id, command_index, content, properties, visual);
    }

    bool BuildNode(BuildState& state, const VisualNode& node, DcompObjectPtr& visual) noexcept {
        const auto version = state.versions.find(node.id);
        if (version == state.versions.end()) {
            return Fail(state, DcompIssueCode::InvalidVersions, node.id, DcompIssue::kNoCommand,
                        E_INVALIDARG, "scene node has no mutation generation");
        }
        const auto signatures = ExternalSignatures(node);

        // Visibility suppresses the whole retained subtree. In particular, a
        // collapsed branch must not import an external handle or turn an
        // otherwise unreachable unsupported command into a frame failure.
        // Its placeholder visual preserves sibling ordering without lowering
        // any hidden semantics.
        const bool suppress_subtree = !node.visible;
        if (!suppress_subtree && !node.unsupported_transform.empty()) {
            return Fail(state, DcompIssueCode::UnsupportedTransform, node.id,
                        DcompIssue::kNoCommand, E_NOTIMPL,
                        "visual declares unsupported transform: " + node.unsupported_transform);
        }
        if (!suppress_subtree && !ValidVisualProperties(node)) {
            return Fail(state, DcompIssueCode::InvalidGeometry, node.id,
                        DcompIssue::kNoCommand, E_INVALIDARG,
                        "visual transform, clip or opacity cannot be represented by DirectComposition");
        }

        if (!PlatformCall(state, platform->CreateVisual(visual), node.id,
                          DcompIssue::kNoCommand, "DirectComposition CreateVisual failed")) {
            return false;
        }
        ++state.result.stats.created_visuals;
        DcompVisualProperties node_properties{};
        if (suppress_subtree) {
            // Empty content and no children already make the placeholder
            // invisible. Keep the default opacity so a collapsed branch does
            // not require an otherwise unnecessary DComp EffectGroup.
            node_properties.opacity = 1.0;
        } else {
            node_properties.transform = node.local_transform;
            node_properties.opacity = node.opacity;
            node_properties.has_clip = node.clip.kind == Clip::Kind::Rect;
            node_properties.clip = node.clip.bounds;
        }
        if (!PlatformCall(state, platform->ConfigureVisual(visual, node_properties), node.id,
                          DcompIssue::kNoCommand,
                          "DirectComposition node visual configuration failed")) {
            return false;
        }

        std::vector<NodeId> child_ids;
        std::vector<DcompObjectPtr> child_nodes;
        // CPU parity: a zero-opacity node validates its own retained semantics
        // but does not visit or rasterize descendants.
        if (!suppress_subtree && node.opacity != 0.0) {
            for (const VisualNode* child : state.scene.OrderedChildren(node.id)) {
                DcompObjectPtr child_visual;
                if (!BuildNode(state, *child, child_visual)) return false;
                child_ids.push_back(child->id);
                child_nodes.push_back(std::move(child_visual));
            }
        }

        std::vector<DcompObjectPtr> paint_order;
        if (!suppress_subtree && node.opacity != 0.0 && node.content) {
            const auto& commands = node.content->commands;
            std::size_t begin = 0;
            std::size_t stratum = 0;
            for (std::size_t index = 0; index <= commands.size(); ++index) {
                const bool boundary = index == commands.size() ||
                                      std::holds_alternative<LocalExternalSurface>(commands[index]);
                if (!boundary) continue;
                if (begin < index) {
                    DcompObjectPtr cpu_visual;
                    if (!BuildCpuStratum(state, node, stratum++, begin, index, cpu_visual)) {
                        return false;
                    }
                    if (cpu_visual) paint_order.push_back(std::move(cpu_visual));
                }
                if (index < commands.size()) {
                    DcompObjectPtr external_visual;
                    if (!BuildExternal(state, node, index,
                                       std::get<LocalExternalSurface>(commands[index]),
                                       external_visual)) {
                        return false;
                    }
                    if (external_visual) paint_order.push_back(std::move(external_visual));
                }
                begin = index + 1;
            }
            paint_order.insert(paint_order.end(), child_nodes.begin(), child_nodes.end());
        } else if (!suppress_subtree && node.opacity != 0.0) {
            paint_order = child_nodes;
        }

        if (!PlatformCall(state, platform->SetChildren(visual, paint_order), node.id,
                          DcompIssue::kNoCommand,
                          "DirectComposition visual child ordering failed")) {
            return false;
        }
        state.nodes.emplace(node.id,
                            NodeEntry{version->second, node.content, signatures,
                                      std::move(child_ids), std::move(child_nodes), visual});
        return true;
    }

    bool CanReuseWholeTree(
        const SceneSnapshot& scene,
        const std::unordered_map<NodeId, std::uint64_t, NodeIdHash>& versions) const {
        if (!is_ready || root_id != scene.root() || nodes.size() != scene.nodes().size() ||
            extent.width != scene.surface().width || extent.height != scene.surface().height) {
            return false;
        }
        for (const VisualNode& node : scene.nodes()) {
            const auto committed = nodes.find(node.id);
            const auto version = versions.find(node.id);
            if (committed == nodes.end() || version == versions.end() ||
                committed->second.version != version->second ||
                committed->second.display_list.get() != node.content.get() ||
                committed->second.external_signatures != ExternalSignatures(node)) {
                return false;
            }
            std::vector<NodeId> children;
            for (const VisualNode* child : scene.OrderedChildren(node.id)) {
                children.push_back(child->id);
            }
            if (committed->second.child_ids != children) return false;
        }
        return true;
    }

    std::shared_ptr<DcompPlatform> platform;
    CpuRasterBackend cpu;
    std::unordered_map<NodeId, NodeEntry, NodeIdHash> nodes;
    std::unordered_map<SurfaceKey, SurfaceEntry, SurfaceKeyHash> surfaces;
    std::unordered_map<ExternalKey, ExternalEntry, ExternalKeyHash> externals;
    DcompObjectPtr root;
    NodeId root_id{};
    std::uint64_t generation = 0;
    Size extent{};
    bool is_ready = false;
};

DcompSceneBackend::DcompSceneBackend(std::shared_ptr<DcompPlatform> platform)
    : impl_(std::make_unique<Impl>(std::move(platform))) {}

DcompSceneBackend::~DcompSceneBackend() = default;

DcompUpdateResult DcompSceneBackend::Update(const SceneSnapshot& scene,
                                             const std::vector<DcompNodeVersion>& versions,
                                             TextRasterizer* text_rasterizer,
                                             DcompPublishGuard* publish_guard) noexcept {
    DcompUpdateResult result{};
    result.generation = impl_->generation;
    result.extent = impl_->extent;
    try {
        if (!impl_->platform) {
            result.error = E_POINTER;
            result.issues.push_back({DcompIssueCode::PlatformFailure, {},
                                     DcompIssue::kNoCommand, E_POINTER,
                                     "DirectComposition platform is null"});
            return result;
        }
        std::string scene_error;
        if (!scene.Validate(&scene_error)) {
            result.error = E_INVALIDARG;
            result.issues.push_back({DcompIssueCode::InvalidScene, {},
                                     DcompIssue::kNoCommand, E_INVALIDARG,
                                     std::move(scene_error)});
            return result;
        }
        result.stats.scene_nodes = scene.nodes().size();
        for (const VisualNode& node : scene.nodes()) {
            if (node.visible && node.opacity != 0.0) ++result.stats.visible_nodes;
            if (!node.content) continue;
            result.stats.scene_commands += node.content->commands.size();
            for (const DrawCommand& command : node.content->commands) {
                if (std::holds_alternative<LocalFillRect>(command)) {
                    ++result.stats.fill_commands;
                } else if (std::holds_alternative<LocalImageBrushFill>(command)) {
                    ++result.stats.image_brush_commands;
                } else if (std::holds_alternative<LocalText>(command)) {
                    ++result.stats.text_commands;
                } else if (std::holds_alternative<LocalExternalSurface>(command)) {
                    ++result.stats.external_surface_commands;
                }
            }
        }
        std::unordered_map<NodeId, std::uint64_t, NodeIdHash> version_map;
        for (const auto& version : versions) {
            if (!version.node.Valid() || version.generation == 0 ||
                !version_map.emplace(version.node, version.generation).second) {
                result.error = E_INVALIDARG;
                result.issues.push_back({DcompIssueCode::InvalidVersions, version.node,
                                         DcompIssue::kNoCommand, E_INVALIDARG,
                                         "node generations must be nonzero and unique"});
                return result;
            }
        }
        if (version_map.size() != scene.nodes().size()) {
            result.error = E_INVALIDARG;
            result.issues.push_back({DcompIssueCode::InvalidVersions, {},
                                     DcompIssue::kNoCommand, E_INVALIDARG,
                                     "every scene node requires exactly one generation"});
            return result;
        }
        for (const auto& entry : version_map) {
            if (!scene.Find(entry.first)) {
                result.error = E_INVALIDARG;
                result.issues.push_back({DcompIssueCode::InvalidVersions, entry.first,
                                         DcompIssue::kNoCommand, E_INVALIDARG,
                                         "generation names a node outside the scene"});
                return result;
            }
        }

        if (impl_->CanReuseWholeTree(scene, version_map)) {
            result.committed = true;
            result.reused_root = true;
            result.generation = impl_->generation;
            result.extent = impl_->extent;
            result.stats.reused_nodes = scene.nodes().size();
            return result;
        }

        Impl::BuildState state{scene, version_map, text_rasterizer, result, {},
                               impl_->surfaces, impl_->externals};
        DcompObjectPtr staged_root;
        if (!impl_->BuildNode(state, *scene.Find(scene.root()), staged_root)) return result;

        if (publish_guard && !publish_guard->CanPublish()) {
            result.error = E_ABORT;
            result.issues.push_back({DcompIssueCode::PublicationCanceled, scene.root(),
                                     DcompIssue::kNoCommand, E_ABORT,
                                     "host identity changed before DirectComposition Commit"});
            return result;
        }

        const HRESULT publish = impl_->platform->PublishRoot(staged_root);
        if (FAILED(publish)) {
            result.error = publish;
            result.issues.push_back({DcompIssueCode::PlatformFailure, scene.root(),
                                     DcompIssue::kNoCommand, publish,
                                     "DirectComposition root publication/Commit failed"});
            return result;
        }

        impl_->nodes.swap(state.nodes);
        impl_->surfaces.swap(state.surfaces);
        impl_->externals.swap(state.externals);
        impl_->root = std::move(staged_root);
        impl_->root_id = scene.root();
        impl_->extent = scene.surface();
        impl_->is_ready = true;
        ++impl_->generation;
        result.committed = true;
        result.generation = impl_->generation;
        result.extent = impl_->extent;
        result.error = S_OK;
        return result;
    } catch (const std::bad_alloc&) {
        result.error = E_OUTOFMEMORY;
    } catch (...) {
        result.error = E_FAIL;
    }
    try {
        result.issues.push_back({DcompIssueCode::PlatformFailure, {}, DcompIssue::kNoCommand,
                                 result.error, "DComp scene update threw before Commit"});
    } catch (...) {
    }
    return result;
}

HRESULT DcompSceneBackend::Detach() noexcept {
    if (!impl_->platform) return E_POINTER;
    const HRESULT result = impl_->platform->DetachRoot();
    if (FAILED(result)) return result;
    impl_->nodes.clear();
    impl_->surfaces.clear();
    impl_->externals.clear();
    impl_->root.reset();
    impl_->root_id = {};
    impl_->extent = {};
    impl_->is_ready = false;
    ++impl_->generation;
    return S_OK;
}

bool DcompSceneBackend::ready() const noexcept { return impl_->is_ready; }
std::uint64_t DcompSceneBackend::generation() const noexcept { return impl_->generation; }
Size DcompSceneBackend::extent() const noexcept { return impl_->extent; }
DcompObjectPtr DcompSceneBackend::root() const noexcept { return impl_->root; }

class WindowsDcompPlatform::Impl {
public:
    Impl(IDCompositionDesktopDevice* device_, IDCompositionTarget* target_)
        : device(device_), target(target_) {
        if (device) device->AddRef();
        if (target) target->AddRef();
    }
    ~Impl() {
        SafeRelease(target);
        SafeRelease(device);
    }

    IDCompositionDesktopDevice* device = nullptr;
    IDCompositionTarget* target = nullptr;
};

WindowsDcompPlatform::WindowsDcompPlatform(IDCompositionDesktopDevice* device,
                                           IDCompositionTarget* target)
    : impl_(std::make_unique<Impl>(device, target)) {}

WindowsDcompPlatform::~WindowsDcompPlatform() = default;

HRESULT WindowsDcompPlatform::CreateVisual(DcompObjectPtr& visual) noexcept {
    visual.reset();
    if (!impl_->device) return E_POINTER;
    IDCompositionVisual2* native = nullptr;
    const HRESULT result = impl_->device->CreateVisual(&native);
    if (FAILED(result)) return result;
    try {
        visual = std::make_shared<WindowsObject>(native);
        return S_OK;
    } catch (...) {
        native->Release();
        return E_OUTOFMEMORY;
    }
}

HRESULT WindowsDcompPlatform::ConfigureVisual(
    const DcompObjectPtr& visual, const DcompVisualProperties& properties) noexcept {
    if (!impl_->device) return E_POINTER;
    const auto native = std::dynamic_pointer_cast<WindowsObject>(visual);
    if (!native) return E_INVALIDARG;
    IDCompositionVisual2* value = nullptr;
    HRESULT result = native->CopyAs(&value);
    if (FAILED(result)) return result;

    if (!Floatable(properties.offset_x) || !Floatable(properties.offset_y) ||
        !Floatable(properties.opacity) || properties.opacity < 0.0 || properties.opacity > 1.0) {
        value->Release();
        return E_INVALIDARG;
    }
    const Matrix3x2& matrix = properties.transform;
    if (!Floatable(matrix.m11) || !Floatable(matrix.m12) || !Floatable(matrix.m21) ||
        !Floatable(matrix.m22) || !Floatable(matrix.dx) || !Floatable(matrix.dy)) {
        value->Release();
        return E_INVALIDARG;
    }
    const D2D_MATRIX_3X2_F transform = {
        static_cast<float>(matrix.m11), static_cast<float>(matrix.m12),
        static_cast<float>(matrix.m21), static_cast<float>(matrix.m22),
        static_cast<float>(matrix.dx), static_cast<float>(matrix.dy)};
    result = value->SetOffsetX(static_cast<float>(properties.offset_x));
    if (SUCCEEDED(result)) result = value->SetOffsetY(static_cast<float>(properties.offset_y));
    if (SUCCEEDED(result)) result = value->SetTransform(transform);
    if (SUCCEEDED(result) && properties.has_clip) {
        if (!ValidRect(properties.clip) || !Floatable(properties.clip.x) ||
            !Floatable(properties.clip.y) || !Floatable(properties.clip.width) ||
            !Floatable(properties.clip.height)) {
            result = E_INVALIDARG;
        } else {
            const D2D_RECT_F clip = {
                static_cast<float>(properties.clip.x), static_cast<float>(properties.clip.y),
                static_cast<float>(properties.clip.x + properties.clip.width),
                static_cast<float>(properties.clip.y + properties.clip.height)};
            result = value->SetClip(clip);
        }
    }
    if (SUCCEEDED(result) && properties.opacity != 1.0) {
        IDCompositionEffectGroup* effect = nullptr;
        result = impl_->device->CreateEffectGroup(&effect);
        if (SUCCEEDED(result)) result = effect->SetOpacity(static_cast<float>(properties.opacity));
        if (SUCCEEDED(result)) result = value->SetEffect(effect);
        SafeRelease(effect);
    }
    value->Release();
    return result;
}

HRESULT WindowsDcompPlatform::SetContent(const DcompObjectPtr& visual,
                                          const DcompObjectPtr& content) noexcept {
    const auto native_visual = std::dynamic_pointer_cast<WindowsObject>(visual);
    const auto native_content = std::dynamic_pointer_cast<WindowsObject>(content);
    if (!native_visual || !native_content) return E_INVALIDARG;
    IDCompositionVisual2* value = nullptr;
    HRESULT result = native_visual->CopyAs(&value);
    if (FAILED(result)) return result;
    result = value->SetContent(native_content->get());
    value->Release();
    return result;
}

HRESULT WindowsDcompPlatform::SetChildren(
    const DcompObjectPtr& visual, const std::vector<DcompObjectPtr>& children) noexcept {
    const auto native = std::dynamic_pointer_cast<WindowsObject>(visual);
    if (!native) return E_INVALIDARG;
    IDCompositionVisual2* parent = nullptr;
    HRESULT result = native->CopyAs(&parent);
    if (FAILED(result)) return result;
    IDCompositionVisual* previous = nullptr;
    for (const auto& child_object : children) {
        const auto child_native = std::dynamic_pointer_cast<WindowsObject>(child_object);
        if (!child_native) {
            result = E_INVALIDARG;
            break;
        }
        IDCompositionVisual2* child = nullptr;
        result = child_native->CopyAs(&child);
        if (FAILED(result)) break;
        result = parent->AddVisual(child, TRUE, previous);
        if (SUCCEEDED(result)) {
            SafeRelease(previous);
            previous = child;
            previous->AddRef();
        }
        child->Release();
        if (FAILED(result)) break;
    }
    SafeRelease(previous);
    parent->Release();
    return result;
}

HRESULT WindowsDcompPlatform::UploadCpuSurface(const Surface& surface,
                                                DcompObjectPtr& content) noexcept {
    content.reset();
    if (!impl_->device) return E_POINTER;
    if (surface.width() <= 0 || surface.height() <= 0) return E_INVALIDARG;
    IDCompositionSurface* composition_surface = nullptr;
    HRESULT result = impl_->device->CreateSurface(
        static_cast<UINT>(surface.width()), static_cast<UINT>(surface.height()),
        DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED, &composition_surface);
    if (FAILED(result)) return result;

    const RECT update_rect{0, 0, surface.width(), surface.height()};
    POINT offset{};
    IDXGISurface* dxgi_surface = nullptr;
    result = composition_surface->BeginDraw(&update_rect, __uuidof(IDXGISurface),
                                            reinterpret_cast<void**>(&dxgi_surface), &offset);
    bool began_draw = SUCCEEDED(result);
    if (SUCCEEDED(result)) {
        ID3D11Texture2D* texture = nullptr;
        // The IDXGISurface returned by BeginDraw is the texture's DXGI
        // projection. Query its D3D11 identity directly; IDXGISurface does not
        // expose IDXGIResource1::CreateSubresourceSurface in the reverse
        // direction.
        result = dxgi_surface->QueryInterface(__uuidof(ID3D11Texture2D),
                                              reinterpret_cast<void**>(&texture));
        if (SUCCEEDED(result)) {
            D3D11_TEXTURE2D_DESC description{};
            texture->GetDesc(&description);
            const LONG right = offset.x + surface.width();
            const LONG bottom = offset.y + surface.height();
            if (offset.x < 0 || offset.y < 0 || right < offset.x || bottom < offset.y ||
                static_cast<UINT>(right) > description.Width ||
                static_cast<UINT>(bottom) > description.Height) {
                result = E_FAIL;
            } else {
                ID3D11Device* device = nullptr;
                texture->GetDevice(&device);
                if (!device) {
                    result = E_NOINTERFACE;
                } else {
                    ID3D11DeviceContext* context = nullptr;
                    device->GetImmediateContext(&context);
                    if (!context) {
                        result = E_NOINTERFACE;
                    } else {
                        const D3D11_BOX box{static_cast<UINT>(offset.x),
                                            static_cast<UINT>(offset.y), 0,
                                            static_cast<UINT>(right),
                                            static_cast<UINT>(bottom), 1};
                        context->UpdateSubresource(
                            texture, 0, &box, surface.pixels().data(),
                            static_cast<UINT>(surface.width() * sizeof(std::uint32_t)), 0);
                        context->Release();
                    }
                    device->Release();
                }
            }
            texture->Release();
        }
    }
    SafeRelease(dxgi_surface);
    if (began_draw) {
        const HRESULT end = composition_surface->EndDraw();
        if (SUCCEEDED(result)) result = end;
    }
    if (FAILED(result)) {
        composition_surface->Release();
        return result;
    }
    try {
        content = std::make_shared<WindowsObject>(composition_surface);
        return S_OK;
    } catch (...) {
        composition_surface->Release();
        return E_OUTOFMEMORY;
    }
}

HRESULT WindowsDcompPlatform::ImportExternalSurface(
    const ExternalSurfaceReference& source, DcompObjectPtr& content) noexcept {
    content.reset();
    if (!impl_->device || !source) return E_INVALIDARG;
    IUnknown* value = nullptr;
    HRESULT result = S_OK;
    switch (source.kind) {
        case ExternalSurfaceKind::CompositionSurfaceHandle:
            result = impl_->device->CreateSurfaceFromHandle(
                reinterpret_cast<HANDLE>(source.native_value), &value);
            break;
        case ExternalSurfaceKind::DxgiSwapChain:
            value = reinterpret_cast<IUnknown*>(source.native_value);
            value->AddRef();
            break;
        case ExternalSurfaceKind::CpuBgraImage:
            // A producer's CPU image is composited by the CPU backend. Making
            // a DirectComposition surface out of it would mean this platform
            // uploading and owning a copy of the producer's pixels every
            // frame, which is a different design from importing a resource the
            // producer already owns on the GPU. Named rather than approximated.
            return E_NOTIMPL;
        case ExternalSurfaceKind::None:
            return E_INVALIDARG;
    }
    if (FAILED(result)) return result;
    try {
        content = std::make_shared<WindowsObject>(value);
        return S_OK;
    } catch (...) {
        value->Release();
        return E_OUTOFMEMORY;
    }
}

HRESULT WindowsDcompPlatform::PublishRoot(const DcompObjectPtr& root) noexcept {
    if (!impl_->device) return E_POINTER;
    IDCompositionVisual2* visual = nullptr;
    if (root) {
        const auto native = std::dynamic_pointer_cast<WindowsObject>(root);
        if (!native) return E_INVALIDARG;
        const HRESULT query = native->CopyAs(&visual);
        if (FAILED(query)) return query;
    }
    HRESULT result = S_OK;
    if (impl_->target) result = impl_->target->SetRoot(visual);
    SafeRelease(visual);
    if (SUCCEEDED(result)) result = impl_->device->Commit();
    return result;
}

HRESULT WindowsDcompPlatform::DetachRoot() noexcept {
    if (!impl_->device) return E_POINTER;
    HRESULT result = S_OK;
    if (impl_->target) result = impl_->target->SetRoot(nullptr);
    if (SUCCEEDED(result)) result = impl_->device->Commit();
    return result;
}

}  // namespace render
}  // namespace openxaml
