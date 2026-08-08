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
    RegisterAttachedProperty("Canvas", "Left", {0.0, false, false});
const DependencyProperty* const kTop =
    RegisterAttachedProperty("Canvas", "Top", {0.0, false, false});

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

Size Canvas::ArrangeOverride(Size final_size) {
    for (Element* child : Children()) {
        const Size desired = child->desired_size();
        child->Arrange({GetLeft(*child), GetTop(*child), desired.width, desired.height});
    }

    // The arrange size, as `CCanvas::ArrangeOverride` returns, and not the
    // zero this used to return.
    //
    // The zero was inferred from a Canvas whose recorded ActualWidth and
    // ActualHeight were zero in a 400x300 slot, which looked like a straight
    // contradiction of the published source. It was not: that Canvas was the
    // root of its tree, so it was never a layout element's child, never
    // arranged at all, and its ActualWidth came from the specified-size
    // fallback rather than from here. The whole L3-canvas cross product agrees
    // -- every unsized Canvas reports zero and every Canvas with Width="200"
    // reports 200, in the same 400x300 slot -- which is the fallback's
    // signature and not this function's.
    return final_size;
}

}  // namespace openxaml
