#include "canvas.h"

namespace openxaml {

Size Canvas::MeasureOverride(Size) {
    // The available size is not passed on. A Canvas positions absolutely, so
    // there is no sense in which a child of one is constrained by the parent,
    // and every child is asked what it would like at no limit at all.
    const Size unbounded{kInfinity, kInfinity};
    for (Element* child : Children()) child->Measure(unbounded);
    return Size{};
}

Size Canvas::ArrangeOverride(Size) {
    for (Element* child : Children()) {
        const Size desired = child->desired_size();
        child->Arrange({child->canvas_left, child->canvas_top, desired.width, desired.height});
    }

    // Zero, not the arrange size -- and this is the one place in this file
    // worth arguing about.
    //
    // The published WinUI source (microsoft-ui-xaml, `CCanvas::ArrangeOverride`)
    // returns `finalSize`, which would make a stretched Canvas report its
    // slot as its ActualWidth. The recorded oracle says otherwise: all three
    // sizes of the `SelectionCanvas` case measure a Canvas whose ActualWidth
    // and ActualHeight are zero, including the two where the slot is finite
    // and non-empty (400x300 and 100x50). Two of those three disagree with
    // `finalSize` in both axes, so this is not a rounding difference or an
    // artefact of one odd constraint.
    //
    // Windows.UI.Xaml is the arbiter here, as it is for the two divergences
    // already recorded in the README, and this is the answer it gives. It is
    // also the self-consistent one: a Canvas that reports nothing in measure
    // reporting nothing in arrange is the same statement twice. See the
    // pending L3-canvas cases for the cross product that will confirm it, or
    // narrow it, on the next oracle run.
    return Size{};
}

}  // namespace openxaml
