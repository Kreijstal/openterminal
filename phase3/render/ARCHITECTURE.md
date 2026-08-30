# Renderer architecture

This document is the implementation contract for growing the current exact
rectangle renderer into the renderer used by the XAML runtime.  The design is a
retained visual tree whose nodes own local display lists.  A layer planner turns
an immutable scene snapshot into raster work and composition work; raster
backends produce pixels, and a presenter attaches the result to a host.

The acceptance oracle, rather than resemblance to one Microsoft implementation,
defines correctness.  The architecture exists to make every observed boundary
have one owner and one independently testable implementation.

## Pinned design evidence

The design takes inspiration from two exact upstream revisions:

* WPF, `dotnet/wpf` commit
  `2ca037562c207924e53cfcc99286e523d3694de3`.  In particular, WPF separates its
  `Visual` tree from recorded `RenderData`, records drawing through a
  `DrawingContext`, schedules rendering after invalidation, and synchronizes
  resources through a composition boundary.
* The published Microsoft XAML source, `microsoft/microsoft-ui-xaml` commit
  `188f602b27cdb47572b28c380e9c087b02e1ccee`.  Its XAML core keeps property and
  content render data, propagates render and bounds dirtiness, performs a render
  walk, creates composition nodes when an element requires one, and keeps
  DirectComposition hosting separate from element content generation.  This is
  also the published source lineage used by the native oracle.

The local records for those pins are
[`research/wpf/2ca037562c207924e53cfcc99286e523d3694de3/README.md`](../../research/wpf/2ca037562c207924e53cfcc99286e523d3694de3/README.md)
and
[`research/microsoft-ui-xaml/188f602b27cdb47572b28c380e9c087b02e1ccee/README.md`](../../research/microsoft-ui-xaml/188f602b27cdb47572b28c380e9c087b02e1ccee/README.md).
They are evidence for boundaries, not code templates.

We borrow:

* retained visual state distinct from the dependency-object and layout trees;
* drawing commands recorded in local coordinates;
* explicit dirty propagation and a coalesced frame update;
* a composition tree that can be sparser than the visual tree;
* separate content generation, rasterization, composition, and presentation;
* device-independent resources with backend-owned realizations.

We deliberately do not reproduce WPF's DUCE protocol, cross-process channel,
generated resource protocol, or compatibility complexity.  We also do not copy
Microsoft XAML's internal `HWCompTreeNode` hierarchy or its optimization policy.
Those mechanisms solve product constraints that are not yet ours.

## Non-goals and prohibited shortcuts

The renderer is not a collection of `HDC` calls made by individual controls.
No `UIElement`, layout object, brush, or display-list operation may paint
directly into a window DC.  GDI can remain a platform backend and diagnostic
tool, but it cannot define scene semantics.

Raw Win32, GDI, Direct2D, DirectWrite, D3D, DXGI, or DirectComposition traces are
not a renderer contract.  Traces can diagnose a failing oracle case, but a
private call sequence may change while the public visual state and pixels stay
identical.  Backend API objects therefore do not appear in the scene model.

There are no feature-specific pixel patches, case identifiers in production
rendering code, guessed theme values, substituted fonts presented as matches,
or tolerance rules hidden in a backend.  An unsupported operation remains an
explicit refusal until it has an implementation tested against the appropriate
boundary.

## Pipeline

The frame pipeline is:

```text
XAML dependency objects
        |
        v
measure and arrange
        |
        v
SceneBuilder synchronizes retained VisualNodes and local DisplayLists
        |
        v
immutable SceneSnapshot
        |
        v
LayerPlanner produces a CompositionPlan
        |
        +---------------------+
        |                     |
        v                     v
RasterBackend work       compositor state updates
        |                     |
        +----------+----------+
                   v
                Presenter
                   |
                   v
            HWND or offscreen target
```

Layout computes geometry but never paints.  `SceneBuilder` reads only an
already-arranged tree.  The backend never asks an element to measure, arrange,
resolve a property, shape text, or enumerate children.

## Scene model

The following names describe responsibilities.  Exact C++ spelling can evolve
while these ownership rules remain stable.

```cpp
using NodeId = uint64_t;
using DisplayListId = uint64_t;
using ResourceId = uint64_t;

struct VisualNode {
    NodeId id;
    NodeId parent;
    std::vector<NodeId> children;

    Rect layout_slot;              // Parent coordinates, for observation.
    Size render_size;              // Local content extent.
    Matrix3x2 local_transform;     // Local coordinates to parent coordinates.
    Clip local_clip;
    float opacity;
    bool visible;
    int32_t z_index;

    DisplayListId content;
    DirtyFlags dirty;
};

struct SceneSnapshot {
    uint64_t generation;
    Size logical_size;
    float rasterization_scale;
    NodeId root;
    immutable_map<NodeId, VisualNode> nodes;
    immutable_map<DisplayListId, DisplayList> display_lists;
    immutable_map<ResourceId, ResourceDescription> resources;
};
```

`NodeId`, `DisplayListId`, and `ResourceId` are stable within one live scene.
They are never pointers, backend handles, element paths, or hashes whose
collisions affect correctness.  Replacing a resource increments its generation
or allocates a new identity, so an older snapshot cannot observe new contents.

The retained visual tree is related to, but not identical to, the XAML element
tree.  An element can own more than one implementation visual, and an
implementation visual need not have a public element.  Each oracle-visible
element still has one stable primary visual record from which its effective
state is reported.

Child order is deterministic.  The synchronized order is ascending effective
`Canvas.ZIndex`, with stable source order for equal values.  Traversal order,
hit-test order, and composition order must be derived from the same ordering
function; three independent interpretations are forbidden.

### Coordinate rules

Every display list uses its owning node's local device-independent coordinates.
It does not contain ancestors' offsets or a transform-to-root.  A node's
`local_transform` contains the arranged translation and render transform in the
defined XAML order.  The effective transform is obtained by composing the
ancestor chain exactly once during snapshot construction or plan traversal.

Clips are also local.  The planner transforms and intersects them while walking
the scene.  Empty clips cull a subtree without rewriting its content list.
Layout slots remain observable layout values; they are not silently replaced by
post-transform bounds.

All geometry is stored at the precision produced by layout and shaping.
Conversion to device pixels occurs only in a raster backend with the snapshot's
rasterization scale and pixel-snapping state.  `SnapRect` remains the contract
for the current whole-pixel rectangle slice until an oracle-backed coverage rule
supersedes it for a richer primitive.

### Display lists

A `DisplayList` is immutable, replayable content for one visual.  Its initial
state is identity transform, no clip, opacity 1, and no backend target.  The
minimum extensible command vocabulary is:

```text
Save
Restore
ConcatTransform(Matrix3x2)
IntersectClipRect(Rect)
IntersectClipRoundedRect(RoundedRect)
IntersectClipPath(GeometryId)
FillRect(Rect, BrushId)
FillRoundedRect(RoundedRect, BrushId)
FillPath(GeometryId, BrushId, FillRule)
StrokePath(GeometryId, PenId)
DrawImage(ImageId, source_rect, destination_rect, sampling)
DrawGlyphRun(GlyphRunId, BrushId, baseline_origin)
```

Commands are values and may refer only to immutable resource descriptions by
ID.  Lists must have balanced `Save` and `Restore` commands, finite numbers,
valid resource types, and no commands after a validation error.  Invalid lists
are rejected before a backend sees them.

Structural visual properties normally live on `VisualNode`, not in its display
list.  This lets an opacity, transform, clip, visibility, or ordering change
reuse unchanged content.  Nested display-list state is available for element
content that genuinely needs it, such as geometry groups.

The current flat `RectOp` and `TextOp` representation is the first migration
input.  Existing output must stay exact while operations move into per-node
local lists; flattening to root coordinates is not the long-term model.

## Scene synchronization and invalidation

Dependency-property metadata assigns each renderer-visible change to one or
more invalidation categories:

| Category | Meaning | Examples |
|---|---|---|
| `Measure` | Intrinsic or constrained size may change. Implies arrange, content, and bounds work. | width, height, margin, font family, font size, text |
| `Arrange` | Position or final size may change. Implies visual synchronization and bounds work. | alignment, grid placement, padding after measure is valid |
| `Content` | Local drawing commands or referenced content resources changed. | background, border brush, border thickness, foreground, image source |
| `Transform` | Local-to-parent transform changed but local drawing did not. | render transform, arranged offset when size is unchanged |
| `Clip` | Local clip or accumulated visible bounds changed. | explicit clip, layout clip, corner clipping |
| `Composition` | Existing content combines differently or layer requirements changed. | opacity, effect, cache mode, external surface |
| `Structure` | Parent, children, visibility, or order changed. | add/remove/reparent, visibility, `Canvas.ZIndex` |
| `Resource` | An immutable logical resource received a new version. | animated brush value, decoded image, font-face availability |
| `Device` | Backend realizations are invalid while logical scene data remains valid. | device loss, target replacement, scale/color-space change |

Flags are monotonic during one frame: work can add stronger invalidations but
cannot clear them before a snapshot successfully incorporates the change.
Changes propagate upward only as far as needed to update subtree bounds and
layer decisions.  A dirty ancestor does not automatically force unchanged
descendants to rebuild their local display lists.

`SceneBuilder` performs these steps in order:

1. Complete pending measure and arrange work.
2. Apply structural edits and establish the deterministic child order.
3. Rebuild local display lists only for content-dirty visuals.
4. Synchronize transforms, clips, opacity, visibility, bounds, and resources.
5. Validate all new lists and references.
6. Publish one complete snapshot and clear only the flags represented by it.

If synchronization fails, the previously published snapshot remains usable.
There is no partly updated scene visible to a backend.

## Layer and composition planning

The visual tree is retained for semantic correctness.  The composition tree is
a derived, potentially sparse execution plan.  A visual does not get an
offscreen surface merely because it exists.

`LayerPlanner::Plan(const SceneSnapshot&, const PlanHistory&)` returns an
immutable `CompositionPlan` containing ordered raster passes, intermediate
surfaces, layer dependencies, effective transforms and clips, damage, and final
composition operations.  The same input snapshot and capability set must
produce the same semantic plan.

A separate layer is required when direct replay cannot preserve the observed
group semantics, including:

* opacity applied to a subtree with overlapping descendants;
* an effect or mask that consumes the completed subtree;
* a complex clip whose semantics require intermediate coverage;
* explicit bitmap caching;
* an external or independently presented surface such as Terminal content;
* an independently animated subtree whose content should not be rerasterized.

Group opacity is applied once to the completed subtree.  Applying the opacity
to each primitive or child independently is incorrect when content overlaps.
Layer bounds include the subtree's transformed ink bounds and required effect
outsets, then are clipped to target limits.  Empty or fully transparent layers
may be culled only when doing so cannot affect hit testing, retained resource
state, or a public composition visual.

The first planner may emit one root raster pass plus temporary surfaces.  A
later DirectComposition planner may map eligible layers to persistent platform
visuals.  Neither choice changes `SceneSnapshot` or display-list semantics.

## Raster backends

The backend boundary consumes validated, device-independent work:

```cpp
class RasterBackend {
public:
    virtual BackendCapabilities Capabilities() const = 0;
    virtual RasterResult Rasterize(const RasterPass&, ResourceProvider&) = 0;
    virtual void DiscardDeviceResources() = 0;
};
```

There are two required backend roles:

* A deterministic CPU reference backend defines portable execution of the
  supported command set and produces top-down premultiplied BGRA8.  It is the
  narrowest place to debug geometry, clipping, ordering, and blend semantics.
* A Direct2D/DirectWrite production backend realizes the same commands using
  Windows graphics APIs.  Differences from the CPU backend must be explained by
  an explicit capability or rasterization contract and ultimately settled by
  the native oracle.

GDI may implement a limited backend for Wine bring-up and glyph diagnostics.
It is not an alternate definition of alpha, clipping, text placement, or
rounding.

The common color model is premultiplied BGRA8 at the oracle boundary.  Blending
uses source-over unless a command explicitly names another oracle-backed blend
mode.  Transparent initialization is distinct from the reserved opaque backdrop
used by the rectangle recovery suite.

A backend must not mutate the scene, choose fallback fonts, reshape text,
reorder operations, or consult XAML objects.  It can cache realizations, batch
compatible commands, and choose API calls, provided its observable result is
unchanged.

## External surface boundary

A `SwapChainPanel` does not paint; a producer draws into a surface it owns and
presents it, and the frame composites whatever is in that surface at the
panel's arranged rectangle.  The scene therefore carries a
`LocalExternalSurface` command holding the panel's local rectangle and an
`ExternalSurfaceReference`: a kind, a generation and an opaque native value,
plus a lifetime token that keeps the duplicated handle or AddRef'd swap chain
alive for the whole immutable snapshot.  Layout learns nothing about `HANDLE`,
`IUnknown`, DXGI or DirectComposition.

Placement is the renderer's and never the producer's.  The arranged rectangle,
the retained clip above it, paint order and the frame's scene record are all
resolved by the backend from the snapshot.  The one question a CPU backend
cannot answer for itself is *what pixels the source is*, because importing a
swap chain or a composition surface handle needs a graphics device.  That, and
only that, is the seam:

```cpp
class ExternalSurfaceReader {
public:
    virtual bool ReadExternalSurface(const ExternalSurfaceReference& source,
                                     ExternalSurfaceView& view,
                                     std::string& message) = 0;
};
```

`ExternalSurfaceView` is premultiplied BGRA8, top row first, with a stride --
the same colour model as everything else at this boundary.  It is borrowed for
the duration of one `Render` call, so a reader may return a mapping it releases
afterwards.

Two implementations exist or are expected:

* `CpuExternalSurfaceReader` imports `ExternalSurfaceKind::CpuBgraImage`, a
  producer's premultiplied image already resident in the process.  It refuses
  every other kind by name.
* A DXGI reader implements the same interface for
  `ExternalSurfaceKind::DxgiSwapChain`: `GetBuffer` on the presented back
  buffer, copy to a staging texture, map, return the mapping as a view.  It is
  the only part of this path that needs `Present` to work, and nothing
  downstream of the view changes when it arrives.

The CPU compositor composites the imported pixels source-over at whole pixels,
one for one.  It never resamples: a producer sizes its surface to the panel it
is bound to, and stretching what it presented would put content on the screen
the producer never drew.  A surface whose extent is not the arranged extent, a
scaled or rotated transform above it, a non-axis-aligned clip over it, a source
no reader could import, and a frame with no reader bound at all are each named
as `UnsupportedExternalSurface` with the measurements in the message.  None of
them leaves pixels that could be mistaken for composited ones.

DirectComposition takes the other route for a GPU source: it imports the
resource the producer already owns and gives it a visual, rather than reading
pixels back. A small immutable `CpuBgraImage`, such as a decoded control icon,
is uploaded to an owned composition surface. The generation and lifetime token
keep that upload cached until the producer changes the image.

## Text boundary

Text is shaped before display-list construction.  `DrawGlyphRun` references an
immutable run containing at least:

```cpp
struct GlyphRun {
    FontFaceId face;
    float font_size;
    std::vector<uint16_t> glyph_indices;
    std::vector<float> advances;
    std::vector<GlyphOffset> offsets;
    Point baseline_origin;
    MeasuringMode measuring_mode;
    PixelSnapping snapping;
    TextDirection direction;
};
```

The retained run can additionally preserve source text, clusters, bidi levels,
and fallback spans for accessibility, selection, and diagnostics.  Those fields
do not authorize a backend to shape the source text again.

Font fallback and shaping are platform services behind a text-layout boundary.
The selected face identity, glyph indices, advances, offsets, and baseline are
snapshot data.  Missing face data is an explicit refusal or resource-pending
state; silently substituting a face is not acceptance.

DirectWrite can both shape runs and rasterize their glyphs, but those are two
different roles.  Keeping the shaped result between them lets the CPU,
DirectWrite, and diagnostic backends consume identical placement.

## Resource and device lifetime

Logical resources belong to a snapshot and contain no device pointers.  They
are immutable descriptions such as solid or gradient brushes, path geometry,
font-face identity, decoded image content, and stroke parameters.  Resources
may be interned by canonical content, but equality must not depend on allocation
addresses or nondeterministic iteration order.

Each backend owns a `ResourceCache` keyed by logical `ResourceId`, resource
generation, backend device generation, scale, and any other realization input.
Backend objects are created lazily on the thread required by that backend.
Cache eviction changes performance only.

Device loss follows one rule:

1. The presenter stops submitting work to the invalid target.
2. The backend increments its device generation and releases all device-bound
   targets and resource realizations.
3. Logical snapshots and decoded/source data remain valid.
4. The presenter recreates the target, and resources are realized lazily while
   replaying the latest complete snapshot.

No element is reloaded and no layout pass is caused solely by graphics-device
loss.  A rasterization-scale change is different: it invalidates device-space
realizations and pixel snapping, and may also invalidate text measurement when
the measured contract depends on snapping.

## Presentation and hosting

`Presenter` owns target attachment and frame lifetime, not scene construction:

```cpp
class Presenter {
public:
    virtual void Attach(const HostTarget&) = 0;
    virtual void Resize(PixelSize, float rasterization_scale) = 0;
    virtual PresentResult Present(const SceneSnapshot&, const CompositionPlan&) = 0;
    virtual void Detach() = 0;
};
```

An offscreen presenter writes the oracle BGRA surface.  An HWND presenter owns
paint scheduling, resize, occlusion, target creation, device-loss recovery, and
submission.  A DirectComposition presenter additionally owns the platform
composition root and commit boundary.  `DesktopWindowXamlSource` attaches a
root to a presenter; it does not become a rasterizer.

Window invalidation schedules a frame.  `WM_PAINT` or the corresponding host
callback may request presentation of the latest snapshot, but it must not walk
and mutate XAML state while holding a destination DC.  Direct `HDC` painting is
confined to a presenter/backend adapter when that backend is selected.

## Threading and snapshots

The first implementation is single-threaded: the UI thread performs layout,
scene synchronization, planning, rasterization, and presentation in order.
Nevertheless, the immutable snapshot boundary is required from the beginning.

The UI thread exclusively owns mutable dependency objects, layout state,
`VisualNode` construction state, and dirty flags.  A published `SceneSnapshot`
and its resource descriptions are immutable and reference-counted.  A render
thread may later consume generation N while the UI thread builds generation
N+1.  Publication is atomic, and frames may be skipped but never partially
merged.

Asynchronous image decode or font discovery publishes a new logical resource
generation through the UI-thread synchronization point.  A worker cannot
mutate a resource already reachable from a snapshot.  Backend device objects
are created, used, and destroyed only on their backend's designated thread.

## Hit testing and diagnostics

Hit testing uses the synchronized visual tree's child order, inverse effective
transforms, visibility, and clips.  It does not infer geometry from pixels or a
backend's composition objects.  This keeps input consistent when the backend
changes or a visual is flattened into an ancestor's raster pass.

Diagnostic sidecars are projections of `SceneSnapshot`, shaped text, and the
executed plan.  They must not become a second mutable source of geometry.
Stable element paths are diagnostic/oracle identifiers, while `NodeId` is the
runtime identity.

## Acceptance ownership

The strict checker in `phase4/scripts/check_render_oracle.py` compares exact
native boundaries.  Each boundary has one primary producer:

| Acceptance boundary | Primary owner |
|---|---|
| desired size | layout measure |
| actual/render size and layout slot | layout arrange, copied by `SceneBuilder` |
| transform to root | `SceneBuilder` composition of `VisualNode.local_transform` |
| clip | visual synchronization plus `LayerPlanner` clip accumulation |
| opacity | `VisualNode` and group semantics in `LayerPlanner` |
| visibility | structural visual synchronization |
| `Canvas.ZIndex` and sibling order | the shared child-order function |
| text content, family, and size | text layout input |
| glyph indices, advances, offsets, and baseline | text shaping and `GlyphRun` |
| premultiplied BGRA8 dimensions and bytes | raster backend, layer composition, and presenter target contract |

The exact native-oracle comparison has no tolerance and no expected-failure
list.  A named renderer refusal is useful progress information, but it still
fails strict acceptance.  The broader rectangle recovery and two-run
determinism checks remain in place during migration because they catch errors
independently of native pixels.

The native render-boundary harvest records public visual state, pixels, glyph
runs, environment identity, and rasterization scale.  That is sufficient to
assign a mismatch to layout, scene synchronization, shaping, rasterization, or
composition without requiring a private API trace.

## Staged implementation

Every stage preserves all earlier exact tests and adds focused tests for its new
invariants.  Generated binaries and pixel artifacts remain outside the
repository.

### Stage 1: typed local display lists

Introduce command and resource value types, validation, and per-node local
lists.  Translate the current opaque rectangles without changing their output.
Keep a compatibility flattening/execution path only as an explicit migration
adapter, not as a second scene model.

Exit criteria: existing rectangle dumps and sidecars remain byte-identical;
list validation rejects unbalanced state and invalid resource references; a
nested case proves parent offsets are applied exactly once.

### Stage 2: retained visual synchronization

Add stable `VisualNode` identities, structure synchronization, deterministic
z-order, dirty categories, and immutable `SceneSnapshot` publication.  Continue
to rasterize on the UI thread.

Exit criteria: unchanged content retains its display-list identity across
transform-only frames; add/remove/reparent and equal-z ordering are deterministic;
the `d2d_tree`, `mini_xaml`, and `nested_xaml` learning cases report the oracle's
visual state.

### Stage 3: CPU transforms, clips, and alpha

Implement transform accumulation, rectangular clipping, premultiplied
source-over blending, layer planning, and group opacity in the deterministic CPU
backend.  Add rounded clips or paths only with their corresponding oracle case.

Exit criteria: transform, clip, opacity, visibility, and z-order fields match
the strict oracle; overlapping translucent subtree cases match exact native
BGRA; `d2d_alpha` proves group rather than per-primitive opacity.

### Stage 4: shaped glyph runs

Replace backend-visible strings with immutable `GlyphRun` resources.  Connect
the recorded or platform shaping boundary and make all glyph backends consume
the same positions.

Exit criteria: the strict oracle matches run count, content, face/family, size,
indices, advances, offsets, and baseline; missing fonts refuse explicitly;
`dwrite_text` has exact placement before glyph raster differences are considered.

### Stage 5: Direct2D/DirectWrite raster backend

Implement backend resource realization, an offscreen target, device generation,
and command replay.  Start with the command subset already correct in the CPU
backend, then expand one oracle-backed primitive at a time.

Exit criteria: `d2d_window` and `d2d_display_list` survive repaint, resize, and
device recreation; supported CPU and Direct2D cases have the same scene
sidecars; native pixel acceptance, not API-call similarity, decides raster
correctness.

### Stage 6: HWND presenter and XAML island connection

Move window attachment, invalidation, target resize, and presentation into an
HWND presenter.  Have the runtime publish and present a scene for its actual
root rather than painting a fixed host background.

Before connecting the presenter, the projected WinRT tree must supply safe
mutation boundaries. Collection replacement/removal detaches an old child from
its inheritance parent and invalidation sink *before* releasing the collection's
COM reference; attachment establishes one parent and rejects cycles. Root
replacement follows the same detach-before-release rule. The invalidation sink
is weak and closeable, so a retained child cannot call a destroyed island and
the island does not form a reference cycle through its content.

Projected brushes are live resources, not colours copied once by a property
setter. Assigning a `SolidColorBrush` synchronizes its current color and
opacity, and later brush mutation invalidates every attached consumer through
lifetime-safe subscriptions. Removing or replacing the brush detaches those
subscriptions before releasing it. Unsupported brush types remain explicit
scene-construction refusals. Arrange-invalidating properties are classified
separately from measure and paint changes; `affects_measure == false` does not
make alignment or positioning a paint-only change.

`WM_PAINT` replays the latest immutable snapshot and always balances
`BeginPaint`/`EndPaint`; it does not measure, mutate the projected tree, or
dereference element or brush pointers. Frame construction is coalesced ahead of
presentation. Two paint messages without a mutation therefore replay the same
snapshot generation and bytes.

Exit criteria: repeated paint without state changes reuses a snapshot; resize
produces one new correctly sized frame; window readback agrees with the
offscreen result for the same backend; detach and device loss leave no stale
host handles. Focused lifetime tests retain and mutate an old root or removed
child after detach/close, reattach a subtree to a second island, mutate a shared
solid brush after assignment, and reject duplicate-parent and cyclic trees.

### Stage 7: persistent composition layers

Implement a compositor backend and DirectComposition presenter for the layer
plan.  Add persistent surfaces, damage updates, independent transforms and
opacity, and explicit external-surface nodes.

Exit criteria: `dcomp_window` exposes deterministic attach and commit
boundaries; flattening versus persistent-layer implementations give identical
oracle pixels and visual observations; Terminal's external content surface can
be hosted without coupling its renderer to XAML element code.

### Stage 8: coverage expansion

Add rounded geometry, general paths, images, gradients, effects, shadows,
scrolling, animation, and accessibility-relevant retained state as separate
vertical slices.  Each slice begins with a native case and public boundary,
then adds a scene operation or planner rule, CPU reference behavior where
feasible, a production implementation, and exact acceptance.

This ordering is architectural, not a permission to make unsupported cases
look plausible.  Until a stage supplies its full boundary, the existing named
refusal remains the correct implementation state.
