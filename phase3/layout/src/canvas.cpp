#include "canvas.h"

namespace openxaml {
namespace {

// Registered under their dotted names, which is also how the markup writes
// them: the name carries its owner, so it resolves on any element rather than
// against that element's own type chain.
//
// Neither affects measure. A Canvas measures every child with an infinite
// constraint whatever its position is, so moving one changes only where it is
// arranged.
const DependencyProperty* const kLeft =
    RegisterProperty("Canvas", "Canvas.Left", {0.0, false, false});
const DependencyProperty* const kTop =
    RegisterProperty("Canvas", "Canvas.Top", {0.0, false, false});

// Canvas has a Background, as every Panel does, and no Padding: there is no
// content rect for one to deflate.
const std::vector<std::string> kOwners = {"Canvas", "Panel", "FrameworkElement", "UIElement"};

}  // namespace

const DependencyProperty& Canvas::LeftProperty() { return *kLeft; }
const DependencyProperty& Canvas::TopProperty() { return *kTop; }

const std::vector<std::string>& Canvas::Owners() { return kOwners; }

double Canvas::GetLeft(const Element& element) { return element.GetDouble(*kLeft); }
void Canvas::SetLeft(Element& element, double value) { element.SetValue(*kLeft, value); }
double Canvas::GetTop(const Element& element) { return element.GetDouble(*kTop); }
void Canvas::SetTop(Element& element, double value) { element.SetValue(*kTop, value); }

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
        child->Arrange({GetLeft(*child), GetTop(*child), desired.width, desired.height});
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
