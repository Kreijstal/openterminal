// Retained DirectComposition projection for an immutable render scene.
//
// The core backend is expressed against DcompPlatform so ordering, resource
// reuse and failure atomicity can be tested without a compositor.  The
// WindowsDcompPlatform at the bottom is the real COM boundary.

#ifndef OPENXAML_RENDER_DCOMP_SCENE_BACKEND_H
#define OPENXAML_RENDER_DCOMP_SCENE_BACKEND_H

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "cpu_raster_backend.h"

struct IDCompositionDesktopDevice;
struct IDCompositionTarget;

namespace openxaml {
namespace render {

class DcompObject {
public:
    virtual ~DcompObject() = default;
};

using DcompObjectPtr = std::shared_ptr<DcompObject>;

struct DcompVisualProperties {
    Matrix3x2 transform = Matrix3x2::Identity();
    double offset_x = 0.0;
    double offset_y = 0.0;
    double opacity = 1.0;
    bool has_clip = false;
    Rect clip{};
};

// All methods queue work in the platform transaction.  PublishRoot is the
// only operation allowed to make a staged tree current and must finish with a
// compositor Commit.  A failed PublishRoot leaves the previously published
// root current (DirectComposition commits are atomic).
class DcompPlatform {
public:
    virtual ~DcompPlatform() = default;

    virtual HRESULT CreateVisual(DcompObjectPtr& visual) noexcept = 0;
    virtual HRESULT ConfigureVisual(const DcompObjectPtr& visual,
                                    const DcompVisualProperties& properties) noexcept = 0;
    virtual HRESULT SetContent(const DcompObjectPtr& visual,
                               const DcompObjectPtr& content) noexcept = 0;
    // Children are supplied in bottom-to-top paint order.
    virtual HRESULT SetChildren(const DcompObjectPtr& visual,
                                const std::vector<DcompObjectPtr>& children) noexcept = 0;

    virtual HRESULT UploadCpuSurface(const Surface& surface,
                                     DcompObjectPtr& content) noexcept = 0;
    virtual HRESULT ImportExternalSurface(const ExternalSurfaceReference& source,
                                          DcompObjectPtr& content) noexcept = 0;

    virtual HRESULT PublishRoot(const DcompObjectPtr& root) noexcept = 0;
    virtual HRESULT DetachRoot() noexcept = 0;
};

struct DcompNodeVersion {
    NodeId node{};
    std::uint64_t generation = 0;
};

enum class DcompIssueCode {
    InvalidScene,
    InvalidVersions,
    UnsupportedTransform,
    InvalidGeometry,
    InvalidExternalSurface,
    CpuRasterFailure,
    PublicationCanceled,
    PlatformFailure,
};

// The host owns the lifetime/identity token behind this guard. It is sampled
// after every scene/resource staging operation and immediately before SetRoot
// and Commit. A false result abandons the staged tree without changing the
// compositor transaction.
class DcompPublishGuard {
public:
    virtual ~DcompPublishGuard() = default;
    virtual bool CanPublish() noexcept = 0;
};

struct DcompIssue {
    static constexpr std::size_t kNoCommand = RenderIssue::kNoCommand;

    DcompIssueCode code = DcompIssueCode::InvalidScene;
    NodeId node{};
    std::size_t command_index = kNoCommand;
    HRESULT error = S_OK;
    std::string message;
};

struct DcompUpdateStats {
    std::size_t scene_nodes = 0;
    std::size_t visible_nodes = 0;
    std::size_t scene_commands = 0;
    std::size_t fill_commands = 0;
    std::size_t image_brush_commands = 0;
    std::size_t text_commands = 0;
    std::size_t external_surface_commands = 0;
    std::size_t created_visuals = 0;
    std::size_t reused_nodes = 0;
    std::size_t uploaded_cpu_surfaces = 0;
    std::size_t reused_cpu_surfaces = 0;
    std::size_t imported_external_surfaces = 0;
    std::size_t reused_external_surfaces = 0;
};

struct DcompUpdateResult {
    bool committed = false;
    bool reused_root = false;
    std::uint64_t generation = 0;
    Size extent{};
    HRESULT error = S_OK;
    DcompUpdateStats stats{};
    std::vector<RenderIssue> render_issues;
    std::vector<DcompIssue> issues;

    bool complete() const noexcept {
        return committed && error == S_OK && render_issues.empty() && issues.empty();
    }
};

class DcompSceneBackend final {
public:
    explicit DcompSceneBackend(std::shared_ptr<DcompPlatform> platform);
    ~DcompSceneBackend();

    DcompSceneBackend(const DcompSceneBackend&) = delete;
    DcompSceneBackend& operator=(const DcompSceneBackend&) = delete;

    // Versions are per-node mutation generations. Every scene node must have
    // exactly one nonzero generation. Reusing a generation promises that all
    // retained state for that NodeId is immutable and unchanged. Visuals are
    // reused only when the complete committed tree is unchanged: a DComp
    // visual already attached to the committed tree cannot be inserted into a
    // separately staged parent. CPU/imported content resources may still be
    // reused while an attachable ancestor closure is rebuilt.
    DcompUpdateResult Update(const SceneSnapshot& scene,
                             const std::vector<DcompNodeVersion>& versions,
                             TextRasterizer* text_rasterizer = nullptr,
                             DcompPublishGuard* publish_guard = nullptr) noexcept;

    // Detach is an explicit compositor transaction. A failure preserves the
    // backend's last committed state so callers can diagnose/retry teardown.
    HRESULT Detach() noexcept;

    bool ready() const noexcept;
    std::uint64_t generation() const noexcept;
    Size extent() const noexcept;
    DcompObjectPtr root() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

// Production adapter. It creates premultiplied BGRA DirectComposition
// surfaces, uploads through the underlying D3D11 texture, imports composition
// handles through IDCompositionDesktopDevice::CreateSurfaceFromHandle, and
// AddRefs DXGI swap chains for SetContent. `target` may be null for an
// unattached probe/backend; PublishRoot still commits the device transaction.
class WindowsDcompPlatform final : public DcompPlatform {
public:
    WindowsDcompPlatform(IDCompositionDesktopDevice* device, IDCompositionTarget* target);
    ~WindowsDcompPlatform() override;

    WindowsDcompPlatform(const WindowsDcompPlatform&) = delete;
    WindowsDcompPlatform& operator=(const WindowsDcompPlatform&) = delete;

    HRESULT CreateVisual(DcompObjectPtr& visual) noexcept override;
    HRESULT ConfigureVisual(const DcompObjectPtr& visual,
                            const DcompVisualProperties& properties) noexcept override;
    HRESULT SetContent(const DcompObjectPtr& visual,
                       const DcompObjectPtr& content) noexcept override;
    HRESULT SetChildren(const DcompObjectPtr& visual,
                        const std::vector<DcompObjectPtr>& children) noexcept override;
    HRESULT UploadCpuSurface(const Surface& surface,
                             DcompObjectPtr& content) noexcept override;
    HRESULT ImportExternalSurface(const ExternalSurfaceReference& source,
                                  DcompObjectPtr& content) noexcept override;
    HRESULT PublishRoot(const DcompObjectPtr& root) noexcept override;
    HRESULT DetachRoot() noexcept override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace render
}  // namespace openxaml

#endif  // OPENXAML_RENDER_DCOMP_SCENE_BACKEND_H
