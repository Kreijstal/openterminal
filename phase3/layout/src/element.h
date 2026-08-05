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
#include <string>
#include <vector>

#include "layout.h"
#include "property.h"

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

class Element : public DependencyObject {
public:
    // The name the oracle reports, so that results compare directly against
    // measurements from the real runtime.
    virtual std::string TypeName() const = 0;

    void Measure(Size available);
    void Arrange(Rect final_rect);

    Size desired_size() const { return desired_size_; }
    Size render_size() const { return render_size_; }
    // The rect the parent arranged this element into. This is what
    // LayoutInformation::GetLayoutSlot reports, and it is not the same as the
    // element's rendered position -- alignment moves the render offset inside
    // the slot but never moves the slot.
    Rect layout_slot() const { return layout_slot_; }

    virtual std::vector<Element*> Children() const { return {}; }

    // Set when a property that affects measure moves, cleared by Measure.
    // Nothing consults it: the corpus measures each tree once, and a Measure
    // that skipped clean elements would be an optimisation no measurement can
    // check. It is here because the invalidation it records is the observable
    // half of an inherited value changing under an element.
    bool needs_measure() const { return needs_measure_; }

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

protected:
    virtual Size MeasureOverride(Size available) = 0;
    virtual Size ArrangeOverride(Size final_size) = 0;

    void OnPropertyChanged(const DependencyProperty& property) override;
    std::vector<DependencyObject*> InheritanceChildren() const override;

    // Called by whatever takes ownership of a child, so that the child reads
    // inherited values through its new parent from that point on.
    void Adopt(Element& child) { child.SetInheritanceParent(this); }

private:
    Size desired_size_;
    Size render_size_;
    // Desired size before max-clamping and before the parent's available size
    // capped it. Arrange needs it: the layout protocol says a child is never
    // arranged smaller than what it asked for, even when the parent had less
    // room, and the clipped desired size no longer remembers what was asked.
    Size unclipped_desired_size_;
    Rect layout_slot_;
    bool needs_measure_ = true;
};

// Everything with a Children collection. Border is deliberately not a Panel --
// it has a single Child, and the distinction shows up in the measured tree.
class Panel : public Element {
public:
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
