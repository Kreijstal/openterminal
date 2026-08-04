// The FrameworkElement layout contract.
//
// Measure/Arrange are the outer, sealed halves -- they own margins, explicit
// sizes, min/max clamping and alignment. MeasureOverride/ArrangeOverride are
// what a panel or a decorator implements. Keeping the split is the whole point:
// almost everything that makes XAML layout surprising lives in the outer half,
// and a panel that reimplements any of it stops agreeing with the runtime.

#ifndef OPENXAML_ELEMENT_H
#define OPENXAML_ELEMENT_H

#include <memory>
#include <string>
#include <vector>

#include "layout.h"

namespace openxaml {

class Element {
public:
    virtual ~Element() = default;

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

    // FrameworkElement properties. Width/Height are NaN when unset.
    double width = Auto();
    double height = Auto();
    double min_width = 0.0;
    double max_width = kInfinity;
    double min_height = 0.0;
    double max_height = kInfinity;
    Thickness margin;
    HorizontalAlignment horizontal_alignment = HorizontalAlignment::Stretch;
    VerticalAlignment vertical_alignment = VerticalAlignment::Stretch;

    // On by default, as it is in WinUI. The corpus pins the DPI scale to 1.0,
    // so every case here rounds to whole numbers; the scale is carried as a
    // field anyway because it is the only thing that would need to change to
    // measure a high-DPI oracle.
    bool use_layout_rounding = true;
    double dpi_scale_x = 1.0;
    double dpi_scale_y = 1.0;

    // Grid attached properties. They live here rather than on Grid because
    // that is where XAML puts them: any element can carry them, and only a
    // Grid parent reads them.
    int grid_column = 0;
    int grid_row = 0;
    int grid_column_span = 1;
    int grid_row_span = 1;

protected:
    virtual Size MeasureOverride(Size available) = 0;
    virtual Size ArrangeOverride(Size final_size) = 0;

private:
    Size desired_size_;
    Size render_size_;
    // Desired size before max-clamping and before the parent's available size
    // capped it. Arrange needs it: the layout protocol says a child is never
    // arranged smaller than what it asked for, even when the parent had less
    // room, and the clipped desired size no longer remembers what was asked.
    Size unclipped_desired_size_;
    Rect layout_slot_;
};

// Everything with a Children collection. Border is deliberately not a Panel --
// it has a single Child, and the distinction shows up in the measured tree.
class Panel : public Element {
public:
    void AddChild(std::unique_ptr<Element> child) {
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
