// The FrameworkElement layout contract.
//
// Measure/Arrange are the outer, sealed halves -- they own margins, explicit
// sizes, min/max clamping and alignment. MeasureOverride/ArrangeOverride are
// what a panel or a decorator implements. Keeping the split is the whole point:
// almost everything that makes XAML layout surprising lives in the outer half,
// and a panel that reimplements any of it stops agreeing with the runtime.
//
// The values layout reads are dependency properties, not fields -- see
// property.h. Layout does not care where a value came from, only what it is,
// so it reads effective values through accessors and the precedence chain
// stays in one place.

#ifndef OPENXAML_ELEMENT_H
#define OPENXAML_ELEMENT_H

#include <memory>
#include <functional>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "brush.h"
#include "layout.h"
#include "property.h"
#include "binding.h"
#include "events.h"
#include "external_surface.h"
#include "geometry.h"
#include "visual_state.h"

namespace openxaml {

// The inherited text properties.
//
// WinUI declares FontSize, FontFamily and Foreground on Control and again on
// TextBlock, as separate dependency properties that nonetheless inherit from
// one another. They are registered once here against a shared owner that sits
// in the owner chain of exactly those two types -- one registration, the same
// behaviour, and the same refusal of `FontSize` on a StackPanel that the
// runtime gives.
//
// They are free functions rather than members because the two types that carry
// them have no common base below Element, and putting them on Element would
// offer a FontSize to a Grid. An element between them that has no FontSize of
// its own does not block the value: inheritance walks the tree, and a Border
// with nothing in its store simply passes the ancestor's value through.
const DependencyProperty& FontSizeProperty();
const DependencyProperty& FontFamilyProperty();
const DependencyProperty& ForegroundProperty();

// Panel.Background: every Panel has one, which is Grid's, StackPanel's and
// Canvas's. Border and ContentPresenter declare their own, as the runtime
// does. A free function for the same reason the text properties are: Panel is
// a class here, but the property is registered against the owner name rather
// than against it.
const DependencyProperty& PanelBackgroundProperty();

// The owner shared by Control and TextBlock. Not a runtime type -- see above.
inline constexpr const char* kTextPropertyOwner = "TextProperties";

// The host owns this object and elements keep only weak references to it. A
// closed host can therefore make every callback inert in one operation even
// if a detached element was accidentally retained by a caller. Explicit tree
// detachment is still required; this is the lifetime backstop, not a substitute
// for correct ownership.
class RenderInvalidationSink final {
public:
    using Callback = std::function<void(bool layout)>;

    explicit RenderInvalidationSink(Callback callback)
        : callback_(std::move(callback)) {}

    void Notify(bool layout) {
        // A callback may close the sink. Invoke a copy so clearing callback_
        // during the call cannot invalidate the callable on the stack.
        Callback callback;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            callback = callback_;
        }
        if (callback) callback(layout);
    }
    void Close() {
        std::lock_guard<std::mutex> lock(mutex_);
        callback_ = {};
    }
    bool open() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return static_cast<bool>(callback_);
    }

private:
    mutable std::mutex mutex_;
    Callback callback_;
};

class Element : public DependencyObject {
public:
    Element();
    Element(const Element&) = delete;
    Element& operator=(const Element&) = delete;
    Element(Element&&) = delete;
    Element& operator=(Element&&) = delete;

    // Stable for this element's lifetime and never derived from its address.
    // Retained snapshots can therefore reuse a node identity without pointer
    // reuse making a new element look like an old one.
    std::uint64_t render_node_id() const { return render_node_id_; }

    // The name the oracle reports, so that results compare directly against
    // measurements from the real runtime.
    virtual std::string TypeName() const = 0;

    // The framework events registered on this element. Two of them are raised
    // by a layout pass; the rest are stored and never raised, which events.h
    // says why of, one event at a time.
    EventRegistrations& events() { return events_; }
    const EventRegistrations& events() const { return events_; }

    void Measure(Size available);
    void Arrange(Rect final_rect);

    // Whether the element takes part in layout on its own account.
    //
    // The runtime gives layout storage -- which is where both recorded sizes
    // live -- only to an element that is a layout element or whose parent is
    // one, and measures and arranges only those. The list is the runtime's:
    // Border, Control, ContentPresenter, IconElement, TextBlock and Panel say
    // yes, Canvas is the Panel that says no, and Shape and Image never did.
    // A Border under a Canvas is therefore never measured at all, because the
    // Canvas above it is not a layout element and never runs its own measure.
    virtual bool IsLayoutElement() const { return false; }

    // What UIElement.DesiredSize reports. An element with no layout storage
    // has no desired size to report and answers with nothing, whatever its
    // Width says -- the explicit size never reaches this number.
    Size desired_size() const {
        if (!has_layout_storage_ || visibility() == Visibility::Collapsed) return Size{};
        return desired_size_;
    }
    // What ActualWidth and ActualHeight report. Without layout storage there
    // is no render size to read: an element that was measured answers with the
    // size the markup specified, and one that was never measured at all -- so
    // is still measure-dirty -- answers with zero.
    Size render_size() const {
        if (has_layout_storage_) return render_size_;
        return needs_measure_ ? Size{} : specified_size();
    }
    // The size the markup asked for, with no reference to any constraint:
    // Width if it is set, otherwise MinWidth, clamped into the min/max range
    // either way. Unbounded means nothing was asked for, which is zero.
    Size specified_size() const;
    // The rect the parent arranged this element into. This is what
    // LayoutInformation::GetLayoutSlot reports, and it is not the same as the
    // element's rendered position -- alignment moves the render offset inside
    // the slot but never moves the slot.
    Rect layout_slot() const { return layout_slot_; }

    // The retained visual origin in the parent's coordinate space. Arrange
    // computes it after ArrangeOverride has returned the ink size: margins
    // inset the client box and alignment positions that ink inside it. Keeping
    // this separately is what lets LayoutInformation continue reporting the
    // unchanged slot while rendering uses the real FrameworkElement offset.
    Point render_origin() const { return render_origin_; }

    // Whether the element was given layout storage, which is the difference
    // between "arranged at zero" and "never arranged". The render pass needs
    // it: an element with no storage has no rect the corpus has verified, so
    // it is a named no-draw rather than a rect at the origin.
    bool has_layout_storage() const { return has_layout_storage_; }

    // Where this element paints, in its parent's coordinates. This is the
    // retained render origin paired with the element's unclipped render size,
    // not the layout slot's origin or size.
    Rect render_bounds() const {
        const Size size = render_size();
        return Rect{render_origin_.x, render_origin_.y, size.width, size.height};
    }

    // The brushes the markup set, carried for the render pass. Layout reads
    // none of them -- see brush.h.
    const BrushValue& background_brush() const { return background_brush_; }
    void set_background_brush(BrushValue value) { background_brush_ = value; }
    const BrushValue& border_brush() const { return border_brush_; }
    void set_border_brush(BrushValue value) { border_brush_ = value; }
    const BrushValue& fill_brush() const { return fill_brush_; }
    void set_fill_brush(BrushValue value) { fill_brush_ = value; }
    const BrushValue& stroke_brush() const { return stroke_brush_; }
    void set_stroke_brush(BrushValue value) { stroke_brush_ = value; }
    const BrushValue& foreground_brush() const { return foreground_brush_; }
    void set_foreground_brush(BrushValue value) { foreground_brush_ = value; }

    void SetExternalSurfaceProvider(
        std::shared_ptr<const ExternalSurfaceProvider> provider) {
        external_surface_provider_ = std::move(provider);
    }
    ExternalSurfaceReference CaptureExternalSurface() const noexcept {
        return external_surface_provider_
            ? external_surface_provider_->CaptureExternalSurface()
            : ExternalSurfaceReference{};
    }
    void NotifyExternalSurfaceChanged() { InvalidateRender(false); }

    virtual std::vector<Element*> Children() const { return {}; }

    // Visual ownership is independent from C++ allocation ownership. The
    // layout-only implementation usually owns children with unique_ptr, while
    // the WinRT projection owns them with COM references. Both must form one
    // acyclic, single-parent visual tree.
    Element* visual_parent() const { return visual_parent_; }
    bool CanAttachVisualChild(const Element& child) const;
    bool AttachVisualChild(Element& child);
    void DetachVisualChild(Element& child);

    // Claims a root for one host-owned invalidation sink. Attaching a different
    // live sink is refused: visual_parent cannot detect a root installed in two
    // islands. A weak reference avoids the host -> content -> callback -> host
    // cycle. Detach is identity-checked so a stale host cannot disconnect a
    // root that has since moved elsewhere.
    bool AttachRenderInvalidationSink(
        const std::shared_ptr<RenderInvalidationSink>& sink);
    void DetachRenderInvalidationSink(
        const std::shared_ptr<RenderInvalidationSink>& sink);
    void InvalidateRender(bool layout);
    void NotifyVisualStructureChanged() { InvalidateRender(true); }

    // The WinRT bridge installs this callback on each projected element. An
    // island layout pass walks the layout tree and invokes it after arrange,
    // which is the point at which FrameworkElement.Loaded/LayoutUpdated are
    // observable. Keeping the callback on the layout node also lets a root
    // reach projected descendants without exposing ABI objects to this
    // platform-neutral library.
    void SetLayoutPassCallback(std::function<void()> callback) {
        layout_pass_callback_ = std::move(callback);
    }
    void NotifyLayoutPass() {
        if (layout_pass_callback_) layout_pass_callback_();
        // Fetch children after the callback: a LayoutUpdated handler is
        // allowed to mutate the visual tree (Terminal does this at startup).
        for (Element* child : Children()) {
            if (child) child->NotifyLayoutPass();
        }
    }

    // The children the *oracle* has, which is not the same collection.
    //
    // A recorded tree is whatever the probe's walk reached, and that walk asks
    // four questions and no more: a Panel's Children, a Border's Child, and
    // the Content of a ContentControl or a ContentPresenter -- see
    // `walk` in ../../harness/xaml_probe.cpp. Anything else in the runtime's
    // visual tree is a node the oracle was never asked about, so reporting it
    // makes a case fail on a node the recording has no counterpart for rather
    // than on a number. The default is the layout children, because for all
    // four of those the two collections are the same one.
    virtual std::vector<Element*> RecordedChildren() const { return Children(); }

    // Set when a property that affects measure moves, cleared by Measure --
    // including the Measure of an element that takes no part in layout, which
    // does nothing else. That is the whole difference between the two zeros an
    // element without layout storage can render at: measured, so answer with
    // the specified size; never measured, so answer with nothing.
    bool needs_measure() const { return needs_measure_; }

    void KeepBinding(std::unique_ptr<BindingExpression> binding) {
        bindings_.push_back(std::move(binding));
    }
    size_t binding_count() const { return bindings_.size(); }
    void KeepVisualStateManager(std::shared_ptr<NameScope> names,
                                std::unique_ptr<VisualStateManager> manager) {
        namescope_ = std::move(names);
        visual_states_ = std::move(manager);
    }
    VisualStateManager* visual_state_manager() const { return visual_states_.get(); }
    bool GoToState(const std::string& state_name, bool use_transitions = true) {
        return visual_states_ && visual_states_->GoToState(state_name, use_transitions);
    }

    // FrameworkElement properties. Width and Height are NaN when unset, which
    // is what Auto means: no explicit size, take what the content needs.
    double width() const { return GetDouble(WidthProperty()); }
    void set_width(double value) { SetValue(WidthProperty(), value); }
    double height() const { return GetDouble(HeightProperty()); }
    void set_height(double value) { SetValue(HeightProperty(), value); }
    double min_width() const { return GetDouble(MinWidthProperty()); }
    void set_min_width(double value) { SetValue(MinWidthProperty(), value); }
    double max_width() const { return GetDouble(MaxWidthProperty()); }
    void set_max_width(double value) { SetValue(MaxWidthProperty(), value); }
    double min_height() const { return GetDouble(MinHeightProperty()); }
    void set_min_height(double value) { SetValue(MinHeightProperty(), value); }
    double max_height() const { return GetDouble(MaxHeightProperty()); }
    void set_max_height(double value) { SetValue(MaxHeightProperty(), value); }
    const Thickness& margin() const { return GetThickness(MarginProperty()); }
    void set_margin(Thickness value) { SetValue(MarginProperty(), value); }

    HorizontalAlignment horizontal_alignment() const {
        return static_cast<HorizontalAlignment>(GetInt(HorizontalAlignmentProperty()));
    }
    void set_horizontal_alignment(HorizontalAlignment value) {
        SetValue(HorizontalAlignmentProperty(), static_cast<int>(value));
    }
    VerticalAlignment vertical_alignment() const {
        return static_cast<VerticalAlignment>(GetInt(VerticalAlignmentProperty()));
    }
    void set_vertical_alignment(VerticalAlignment value) {
        SetValue(VerticalAlignmentProperty(), static_cast<int>(value));
    }

    // Collapsed suspends the element from layout entirely -- see layout.h. It
    // is UIElement's rather than FrameworkElement's, and it does not inherit:
    // collapsing a panel removes its subtree by removing the panel, not by
    // handing each child a value.
    Visibility visibility() const {
        return static_cast<Visibility>(GetInt(VisibilityProperty()));
    }
    void set_visibility(Visibility value) {
        SetValue(VisibilityProperty(), static_cast<int>(value));
    }

    // On by default, as it is in WinUI. The corpus pins the DPI scale to 1.0,
    // so every case here rounds to whole numbers; the scale is carried as a
    // plain field anyway because it is not a XAML property -- it comes from the
    // environment the case is measured in, not from the markup.
    bool use_layout_rounding() const { return GetBool(UseLayoutRoundingProperty()); }
    void set_use_layout_rounding(bool value) { SetValue(UseLayoutRoundingProperty(), value); }
    double dpi_scale_x = 1.0;
    double dpi_scale_y = 1.0;

    // Opacity has no effect on layout at all. It is here because the corpus
    // reads it back: a property system that quietly dropped it would look
    // identical in every measured number, which is exactly the kind of silence
    // L0 exists to break.
    double opacity() const { return GetDouble(OpacityProperty()); }
    void set_opacity(double value) { SetValue(OpacityProperty(), value); }

    bool has_render_transform() const {
        return visual_transform_.kind != VisualTransformKind::None;
    }
    const VisualTransform& visual_transform() const { return visual_transform_; }
    void set_visual_transform(VisualTransform value);
    Point render_transform_origin() const { return render_transform_origin_; }
    void set_render_transform_origin(Point value);

    const VisualClip& visual_clip() const { return visual_clip_; }
    void set_visual_clip(VisualClip value);

    static const DependencyProperty& WidthProperty();
    static const DependencyProperty& HeightProperty();
    static const DependencyProperty& MinWidthProperty();
    static const DependencyProperty& MaxWidthProperty();
    static const DependencyProperty& MinHeightProperty();
    static const DependencyProperty& MaxHeightProperty();
    static const DependencyProperty& MarginProperty();
    static const DependencyProperty& HorizontalAlignmentProperty();
    static const DependencyProperty& VerticalAlignmentProperty();
    static const DependencyProperty& UseLayoutRoundingProperty();
    static const DependencyProperty& OpacityProperty();
    static const DependencyProperty& VisibilityProperty();
    static const DependencyProperty& RenderTransformOriginProperty();

protected:
    virtual Size MeasureOverride(Size available) = 0;
    virtual Size ArrangeOverride(Size final_size) = 0;

    void OnPropertyChanged(const DependencyProperty& property) override;
    std::vector<DependencyObject*> InheritanceChildren() const override;

    // Called by layout classes that own a child directly. A second parent or a
    // cycle is an implementation error rather than a tree to traverse until it
    // overflows.
    void Adopt(Element& child);
    void Orphan(Element& child) { DetachVisualChild(child); }

private:
    void PropagateRenderInvalidationSink(
        std::weak_ptr<RenderInvalidationSink> sink);

    const std::uint64_t render_node_id_;
    Element* visual_parent_ = nullptr;
    std::weak_ptr<RenderInvalidationSink> render_invalidation_sink_;
    std::function<void()> layout_pass_callback_;
    // True once the element has been given layout storage, which happens on
    // the first Measure that it takes part in. Both recorded sizes read it.
    bool TakesPartInLayout() const;

    bool has_layout_storage_ = false;
    Size desired_size_;
    Size render_size_;
    Point render_origin_;
    VisualTransform visual_transform_;
    Point render_transform_origin_;
    VisualClip visual_clip_;
    // Desired size before max-clamping and before the parent's available size
    // capped it. Arrange needs it: the layout protocol says a child is never
    // arranged smaller than what it asked for, even when the parent had less
    // room, and the clipped desired size no longer remembers what was asked.
    Size unclipped_desired_size_;
    Rect layout_slot_;
    bool needs_measure_ = true;
    BrushValue background_brush_;
    BrushValue border_brush_;
    BrushValue fill_brush_;
    BrushValue stroke_brush_;
    BrushValue foreground_brush_;
    std::shared_ptr<const ExternalSurfaceProvider> external_surface_provider_;
    EventRegistrations events_;
    std::vector<std::unique_ptr<BindingExpression>> bindings_;
    std::shared_ptr<NameScope> namescope_;
    std::unique_ptr<VisualStateManager> visual_states_;
};

// Everything with a Children collection. Border is deliberately not a Panel --
// it has a single Child, and the distinction shows up in the measured tree.
class Panel : public Element {
public:
    // Every Panel lays its children out, so every Panel is a layout element --
    // except Canvas, which overrides this back to false.
    bool IsLayoutElement() const override { return true; }

    void AddChild(std::unique_ptr<Element> child) {
        Adopt(*child);
        children_.push_back(std::move(child));
    }

    std::vector<Element*> Children() const override {
        std::vector<Element*> out;
        out.reserve(children_.size());
        for (const auto& child : children_) out.push_back(child.get());
        return out;
    }

protected:
    std::vector<std::unique_ptr<Element>> children_;
};

}  // namespace openxaml

#endif  // OPENXAML_ELEMENT_H
