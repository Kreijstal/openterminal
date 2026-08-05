#include "element.h"

#include <algorithm>

namespace openxaml {
namespace {

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
    {static_cast<int>(HorizontalAlignment::Stretch), false, false});
const DependencyProperty* const kVerticalAlignment =
    RegisterProperty("FrameworkElement", "VerticalAlignment",
                     {static_cast<int>(VerticalAlignment::Stretch), false, false});

const DependencyProperty* const kUseLayoutRounding =
    RegisterProperty("UIElement", "UseLayoutRounding", {true, false, true});
// Purely visual, so it changes no size. The corpus still has a case for it,
// because a property system that dropped it would measure identically.
const DependencyProperty* const kOpacity = RegisterProperty("UIElement", "Opacity", {1.0, false, false});

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

void Element::OnPropertyChanged(const DependencyProperty& property) {
    if (property.affects_measure()) needs_measure_ = true;
}

std::vector<DependencyObject*> Element::InheritanceChildren() const {
    std::vector<Element*> children = Children();
    std::vector<DependencyObject*> out;
    out.reserve(children.size());
    for (Element* child : children) out.push_back(child);
    return out;
}

void Element::Measure(Size available) {
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
    layout_slot_ = final_rect;

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
}

}  // namespace openxaml
