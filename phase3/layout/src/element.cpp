#include "element.h"

#include <algorithm>
#include <atomic>
#include <stdexcept>
#include <utility>

namespace openxaml {
namespace {

std::uint64_t NextRenderNodeId() {
    static std::atomic<std::uint64_t> next{1};
    return next.fetch_add(1, std::memory_order_relaxed);
}

// Registration runs before main, at namespace scope, rather than on first use.
// Markup resolves an attribute name against the registry, and a property that
// nobody had happened to read yet would look like a property that does not
// exist -- which is the one error this parser is not allowed to get wrong.
//
// The owners are the runtime's, not this file's: Opacity and UseLayoutRounding
// are UIElement's, the sizing and alignment properties are FrameworkElement's.
// Nothing reads the distinction today; the corpus will, as soon as a case sets
// one of them on something that is not a FrameworkElement.
const DependencyProperty* const kWidth =
    RegisterProperty("FrameworkElement", "Width", {Auto(), false, true});
const DependencyProperty* const kHeight =
    RegisterProperty("FrameworkElement", "Height", {Auto(), false, true});
const DependencyProperty* const kMinWidth =
    RegisterProperty("FrameworkElement", "MinWidth", {0.0, false, true});
const DependencyProperty* const kMaxWidth =
    RegisterProperty("FrameworkElement", "MaxWidth", {kInfinity, false, true});
const DependencyProperty* const kMinHeight =
    RegisterProperty("FrameworkElement", "MinHeight", {0.0, false, true});
const DependencyProperty* const kMaxHeight =
    RegisterProperty("FrameworkElement", "MaxHeight", {kInfinity, false, true});
const DependencyProperty* const kMargin =
    RegisterProperty("FrameworkElement", "Margin", {Thickness{}, false, true});

// Alignment affects arrange, not measure: an element that moves inside its
// slot did not change what it asked for. There is no affects_arrange flag
// because nothing here re-arranges a tree -- the corpus measures each one
// once -- and a flag no measurement can check would be decoration.
const DependencyProperty* const kHorizontalAlignment = RegisterProperty(
    "FrameworkElement", "HorizontalAlignment",
    {static_cast<int>(HorizontalAlignment::Stretch), false, false, true});
const DependencyProperty* const kVerticalAlignment =
    RegisterProperty("FrameworkElement", "VerticalAlignment",
                     {static_cast<int>(VerticalAlignment::Stretch), false, false, true});

// Registered as not inheriting, and that half is a guess. WPF's is an
// inherited property; WinUI documents the effect as reaching the subtree
// without saying what carries it, and every case measured so far sets it
// nowhere or everywhere. L0-props-rounding-inherited is authored to settle it
// and has no measurement yet -- if the oracle leaves the inner Border's
// fraction alone, this flag is wrong.
const DependencyProperty* const kUseLayoutRounding =
    RegisterProperty("UIElement", "UseLayoutRounding", {true, false, true});
// Purely visual, so it changes no size. The corpus still has a case for it,
// because a property system that dropped it would measure identically.
const DependencyProperty* const kOpacity = RegisterProperty("UIElement", "Opacity", {1.0, false, false});
// Collapsed suspends the element from layout, so it does affect measure. Not
// inherited: WinUI collapses a subtree by collapsing the element above it, and
// a child of a collapsed panel is never measured at all rather than being told
// it is collapsed too.
const DependencyProperty* const kVisibility = RegisterProperty(
    "UIElement", "Visibility", {static_cast<int>(Visibility::Visible), false, true});
const DependencyProperty* const kRenderTransformOrigin = RegisterProperty(
    "UIElement", "RenderTransformOrigin", {std::string("0,0"), false, false});

// Panel.Background, which is where Grid, StackPanel and Canvas get theirs.
// Border and ContentPresenter declare their own, as the runtime does.
//
// A brush, carried as the name of the type that spells it. Nothing here
// paints, so the value is stored and never interpreted -- which is honest
// about what it is: enough to show the property system carried it, and not a
// colour.
const DependencyProperty* const kPanelBackground =
    RegisterProperty("Panel", "Background", {std::string(), false, false});

// The inherited text properties, shared between Control and TextBlock. See
// element.h for why they are registered once rather than on each of them.
//
// The default font size is 14, which is the runtime's. No case in the corpus
// leaves FontSize unset on an element whose height can be seen, so that number
// is taken from the SDK rather than measured -- L0-props-inherited-default is
// the case authored to pin it, and it has no measurement yet.
const DependencyProperty* const kFontSize =
    RegisterProperty(kTextPropertyOwner, "FontSize", {14.0, true, true});
const DependencyProperty* const kFontFamily =
    RegisterProperty(kTextPropertyOwner, "FontFamily", {std::string("Segoe UI"), true, true});
// Inherited like the other two, and the only one of them whose inheritance no
// measurement can hold: a brush moves no pixel of layout, so the oracle
// records the same numbers whichever value reaches the element.
const DependencyProperty* const kForeground =
    RegisterProperty(kTextPropertyOwner, "Foreground", {std::string(), true, false});

// The effective bounds an explicit Width/Height imposes.
//
// The collapsing here is the reason an explicit size behaves like a pin rather
// than a preference: Width="40" sets both the min and the max to 40, so
// nothing downstream can stretch or shrink it. Auto leaves min at 0 and max at
// infinity, which is why an Auto element takes exactly what its content asks.
struct MinMax {
    double min_width = 0.0;
    double max_width = kInfinity;
    double min_height = 0.0;
    double max_height = kInfinity;

    explicit MinMax(const Element& e) {
        max_height = e.max_height();
        min_height = e.min_height();
        double l = e.height();
        double h = IsAuto(l) ? kInfinity : l;
        max_height = std::max(std::min(h, max_height), min_height);
        h = IsAuto(l) ? 0.0 : l;
        min_height = std::max(std::min(max_height, h), min_height);

        max_width = e.max_width();
        min_width = e.min_width();
        l = e.width();
        double w = IsAuto(l) ? kInfinity : l;
        max_width = std::max(std::min(w, max_width), min_width);
        w = IsAuto(l) ? 0.0 : l;
        min_width = std::max(std::min(max_width, w), min_width);
    }
};

}  // namespace

Element::Element() : render_node_id_(NextRenderNodeId()) { events_.Bind(this); }

const DependencyProperty& FontSizeProperty() { return *kFontSize; }
const DependencyProperty& FontFamilyProperty() { return *kFontFamily; }
const DependencyProperty& ForegroundProperty() { return *kForeground; }

const DependencyProperty& Element::WidthProperty() { return *kWidth; }
const DependencyProperty& Element::HeightProperty() { return *kHeight; }
const DependencyProperty& Element::MinWidthProperty() { return *kMinWidth; }
const DependencyProperty& Element::MaxWidthProperty() { return *kMaxWidth; }
const DependencyProperty& Element::MinHeightProperty() { return *kMinHeight; }
const DependencyProperty& Element::MaxHeightProperty() { return *kMaxHeight; }
const DependencyProperty& Element::MarginProperty() { return *kMargin; }
const DependencyProperty& Element::HorizontalAlignmentProperty() { return *kHorizontalAlignment; }
const DependencyProperty& Element::VerticalAlignmentProperty() { return *kVerticalAlignment; }
const DependencyProperty& Element::UseLayoutRoundingProperty() { return *kUseLayoutRounding; }
const DependencyProperty& Element::OpacityProperty() { return *kOpacity; }
const DependencyProperty& Element::VisibilityProperty() { return *kVisibility; }
const DependencyProperty& Element::RenderTransformOriginProperty() { return *kRenderTransformOrigin; }
const DependencyProperty& PanelBackgroundProperty() { return *kPanelBackground; }

bool Element::CanAttachVisualChild(const Element& child) const {
    if (&child == this) return false;
    if (child.visual_parent_ != nullptr && child.visual_parent_ != this) return false;
    if (child.visual_parent_ != this) {
        const std::shared_ptr<RenderInvalidationSink> child_sink =
            child.render_invalidation_sink_.lock();
        if (child_sink && child_sink->open()) return false;
    }

    // Attaching an ancestor below its own descendant closes a cycle. Walking
    // this chain is enough because every accepted node has at most one parent.
    for (const Element* ancestor = this; ancestor != nullptr;
         ancestor = ancestor->visual_parent_) {
        if (ancestor == &child) return false;
    }
    return true;
}

bool Element::AttachVisualChild(Element& child) {
    if (!CanAttachVisualChild(child)) return false;
    if (child.visual_parent_ == this) return true;

    child.visual_parent_ = this;
    child.SetInheritanceParent(this);
    child.PropagateRenderInvalidationSink(render_invalidation_sink_);
    return true;
}

void Element::DetachVisualChild(Element& child) {
    if (child.visual_parent_ != this) return;

    // Clear the old island before inherited values are re-evaluated. Those
    // changes call OnPropertyChanged and must not invalidate the old host.
    child.PropagateRenderInvalidationSink({});
    child.visual_parent_ = nullptr;
    child.SetInheritanceParent(nullptr);
}

void Element::PropagateRenderInvalidationSink(
    std::weak_ptr<RenderInvalidationSink> sink) {
    render_invalidation_sink_ = sink;
    for (Element* child : Children()) {
        if (child) child->PropagateRenderInvalidationSink(sink);
    }
}

bool Element::AttachRenderInvalidationSink(
    const std::shared_ptr<RenderInvalidationSink>& sink) {
    if (!sink || !sink->open()) return false;
    // Only visual roots are host-attachable. Descendants receive the sink by
    // propagation from their one visual parent; accepting a direct claim here
    // would let the same visual appear both in that tree and as an island root.
    if (visual_parent_ != nullptr) return false;
    const std::shared_ptr<RenderInvalidationSink> current =
        render_invalidation_sink_.lock();
    if (current && current != sink && current->open()) return false;
    PropagateRenderInvalidationSink(sink);
    return true;
}

void Element::DetachRenderInvalidationSink(
    const std::shared_ptr<RenderInvalidationSink>& sink) {
    const std::shared_ptr<RenderInvalidationSink> current =
        render_invalidation_sink_.lock();
    if (current && current != sink) return;
    PropagateRenderInvalidationSink({});
}

void Element::InvalidateRender(bool layout) {
    if (std::shared_ptr<RenderInvalidationSink> sink =
            render_invalidation_sink_.lock()) {
        sink->Notify(layout);
    }
}

void Element::set_visual_clip(VisualClip value) {
    if (visual_clip_ == value) return;
    visual_clip_ = std::move(value);
    InvalidateRender(false);
}

void Element::set_visual_transform(VisualTransform value) {
    if (visual_transform_ == value) return;
    visual_transform_ = std::move(value);
    InvalidateRender(false);
}

void Element::set_render_transform_origin(Point value) {
    if (render_transform_origin_.x == value.x && render_transform_origin_.y == value.y) return;
    render_transform_origin_ = value;
    InvalidateRender(false);
}

void Element::Adopt(Element& child) {
    if (!AttachVisualChild(child))
        throw std::logic_error("an element cannot have two visual parents or form a cycle");
}

void Element::OnPropertyChanged(const DependencyProperty& property) {
    if (property.affects_measure()) needs_measure_ = true;
    InvalidateRender(property.affects_measure() || property.affects_arrange());
}

std::vector<DependencyObject*> Element::InheritanceChildren() const {
    std::vector<Element*> children = Children();
    std::vector<DependencyObject*> out;
    out.reserve(children.size());
    for (Element* child : children) out.push_back(child);
    return out;
}

Size Element::specified_size() const {
    auto resolve = [](double explicit_size, double minimum, double maximum) {
        const double asked = IsAuto(explicit_size) ? std::min(minimum, kInfinity) : explicit_size;
        const double clamped = std::max(std::min(asked, maximum), minimum);
        // Nothing was asked for, and an infinite size is not an answer.
        return std::isinf(clamped) ? 0.0 : clamped;
    };
    return {resolve(width(), min_width(), max_width()),
            resolve(height(), min_height(), max_height())};
}

bool Element::TakesPartInLayout() const {
    if (IsLayoutElement()) return true;
    const auto* parent = dynamic_cast<const Element*>(inheritance_parent());
    return parent != nullptr && parent->IsLayoutElement();
}

void Element::Measure(Size available) {
    // An element that takes no part in layout is measured by nobody: it gets
    // no layout storage, MeasureOverride never runs -- so a Canvas never
    // reaches its own children, and they stay unmeasured -- and the only thing
    // this call leaves behind is the cleared measure-dirty flag that lets
    // ActualWidth answer with the specified size instead of with zero.
    if (!TakesPartInLayout()) {
        needs_measure_ = false;
        return;
    }
    has_layout_storage_ = true;

    // A collapsed element is suspended from layout rather than merely hidden:
    // MeasureOverride is never reached, no explicit Width or MinWidth applies,
    // and the parent is told the element wants nothing. Treating it as a
    // zero-sized visible element would give the same answer here but not under
    // a StackPanel, which counts visible children to place its spacing.
    if (visibility() == Visibility::Collapsed) {
        unclipped_desired_size_ = Size{};
        desired_size_ = Size{};
        needs_measure_ = false;
        return;
    }

    const bool rounding = use_layout_rounding();
    const Thickness element_margin = margin();

    // Margins are rounded before they are subtracted, not after. Rounding the
    // difference instead would make the result depend on the available size:
    // the same margin would consume a different amount at different widths.
    const double margin_width = rounding
                                    ? RoundLayoutValue(element_margin.horizontal(), dpi_scale_x)
                                    : element_margin.horizontal();
    const double margin_height = rounding
                                     ? RoundLayoutValue(element_margin.vertical(), dpi_scale_y)
                                     : element_margin.vertical();

    // What the parent offered, less what the margin will consume.
    Size framework_available{std::max(available.width - margin_width, 0.0),
                             std::max(available.height - margin_height, 0.0)};

    MinMax mm(*this);
    if (rounding) {
        mm.min_width = RoundLayoutValue(mm.min_width, dpi_scale_x);
        mm.max_width = RoundLayoutValue(mm.max_width, dpi_scale_x);
        mm.min_height = RoundLayoutValue(mm.min_height, dpi_scale_y);
        mm.max_height = RoundLayoutValue(mm.max_height, dpi_scale_y);
    }

    framework_available.width =
        std::max(mm.min_width, std::min(framework_available.width, mm.max_width));
    framework_available.height =
        std::max(mm.min_height, std::min(framework_available.height, mm.max_height));

    if (rounding)
        framework_available = RoundLayoutSize(framework_available, dpi_scale_x, dpi_scale_y);

    Size desired = MeasureOverride(framework_available);

    desired.width = std::max(desired.width, mm.min_width);
    desired.height = std::max(desired.height, mm.min_height);

    // The "true minimum": what the element needs to render its content, before
    // anything clips it. Arrange reads it back.
    unclipped_desired_size_ = desired;

    desired.width = std::min(desired.width, mm.max_width);
    desired.height = std::min(desired.height, mm.max_height);

    // Margins can be negative, so this stays signed until the last step.
    double clipped_width = desired.width + margin_width;
    double clipped_height = desired.height + margin_height;

    // Overconstrained: the parent wins. What the element reports upward can
    // never exceed what it was offered, whatever its own sizes worked out to.
    clipped_width = std::min(clipped_width, available.width);
    clipped_height = std::min(clipped_height, available.height);

    if (rounding) {
        clipped_width = RoundLayoutValue(clipped_width, dpi_scale_x);
        clipped_height = RoundLayoutValue(clipped_height, dpi_scale_y);
    }

    desired_size_ = {std::max(0.0, clipped_width), std::max(0.0, clipped_height)};
    needs_measure_ = false;
}

void Element::Arrange(Rect final_rect) {
    // Opened before the early returns, so that the outermost Arrange of a tree
    // is the pass whatever it turns out to do. The pass raises what it
    // accumulated when this goes out of scope, which is the point the
    // reference's layout manager drains its queues at -- see events.h.
    LayoutPass pass;

    // No layout storage, no slot to record and no render size to compute: an
    // element that takes no part in layout is not arranged either.
    if (!TakesPartInLayout()) return;

    // Read before anything moves it. The reference's ArrangeCore captures
    // oldRenderSize in the same place and for the same comparison.
    const Size previous_render_size = render_size_;

    // The slot is recorded even for a collapsed element -- the parent did
    // place it, and LayoutInformation reports that placement -- but nothing
    // below the slot happens.
    layout_slot_ = final_rect;
    if (visibility() == Visibility::Collapsed) {
        render_size_ = Size{};
        render_origin_ = Point{final_rect.x, final_rect.y};
        return;
    }

    const bool rounding = use_layout_rounding();
    const Thickness element_margin = margin();

    Size arrange_size = final_rect.size();
    const double margin_width = rounding
                                    ? RoundLayoutValue(element_margin.horizontal(), dpi_scale_x)
                                    : element_margin.horizontal();
    const double margin_height = rounding
                                     ? RoundLayoutValue(element_margin.vertical(), dpi_scale_y)
                                     : element_margin.vertical();
    arrange_size.width = std::max(0.0, arrange_size.width - margin_width);
    arrange_size.height = std::max(0.0, arrange_size.height - margin_height);

    const Size unclipped = unclipped_desired_size_;

    // A parent may hand down less than the child asked for. The child is not
    // arranged into that smaller box -- it is arranged at what it asked for and
    // clipped afterwards -- because a MeasureOverride that could have coped
    // with less would have said so.
    if (LessThan(arrange_size.width, unclipped.width)) arrange_size.width = unclipped.width;
    if (LessThan(arrange_size.height, unclipped.height)) arrange_size.height = unclipped.height;

    // Stretch fills the slot; every other alignment takes only what the element
    // asked for and positions that within the slot.
    if (horizontal_alignment() != HorizontalAlignment::Stretch)
        arrange_size.width = unclipped.width;
    if (vertical_alignment() != VerticalAlignment::Stretch) arrange_size.height = unclipped.height;

    MinMax mm(*this);
    if (rounding) {
        mm.min_width = RoundLayoutValue(mm.min_width, dpi_scale_x);
        mm.max_width = RoundLayoutValue(mm.max_width, dpi_scale_x);
        mm.min_height = RoundLayoutValue(mm.min_height, dpi_scale_y);
        mm.max_height = RoundLayoutValue(mm.max_height, dpi_scale_y);
    }

    // Max wins over stretch, but never shrinks below what the element asked
    // for -- otherwise setting MaxWidth could arrange an element smaller than
    // its own content.
    const double effective_max_width = std::max(unclipped.width, mm.max_width);
    if (LessThan(effective_max_width, arrange_size.width)) arrange_size.width = effective_max_width;
    const double effective_max_height = std::max(unclipped.height, mm.max_height);
    if (LessThan(effective_max_height, arrange_size.height))
        arrange_size.height = effective_max_height;

    if (rounding) arrange_size = RoundLayoutSize(arrange_size, dpi_scale_x, dpi_scale_y);

    // Unclipped on purpose: the element does not know the layout system may
    // clip it, and should render as though it got everything it returned.
    render_size_ = ArrangeOverride(arrange_size);

    // The slot is layout information; the visual offset is separate retained
    // state. Align the returned ink inside the margin-reduced client box, as
    // FrameworkElement::ArrangeCore does after ArrangeOverride. In particular,
    // an overflowing Stretch degenerates to Left/Top while explicit Center,
    // Right and Bottom retain their (possibly negative) offsets.
    Size client_size{std::max(0.0, final_rect.width - margin_width),
                     std::max(0.0, final_rect.height - margin_height)};
    if (rounding) client_size = RoundLayoutSize(client_size, dpi_scale_x, dpi_scale_y);

    double render_x = final_rect.x + element_margin.left +
                      AlignmentOffset(horizontal_alignment(), client_size.width,
                                      render_size_.width);
    double render_y = final_rect.y + element_margin.top +
                      AlignmentOffset(vertical_alignment(), client_size.height,
                                      render_size_.height);
    if (rounding) {
        render_x = RoundLayoutValue(render_x, dpi_scale_x);
        render_y = RoundLayoutValue(render_y, dpi_scale_y);
    }
    render_origin_ = Point{render_x, render_y};

    // ArrangeOverride has already arranged the children, so a child that moved
    // is queued before its parent -- and the queue is raised backwards, which
    // is what delivers SizeChanged to a parent first.
    LayoutPass::EnqueueSizeChanged(*this, previous_render_size, render_size_);
}

}  // namespace openxaml
