// Which elements take part in layout at all, and what the ones that do not
// report instead.
//
// Windows.UI.Xaml allocates layout storage only for an element that is a
// layout element or whose parent is one -- the condition guarding
// EnsureLayoutStorage in CUIElement::Measure and CUIElement::Arrange. Nothing
// else about the element is different; what changes is where the two recorded
// numbers come from:
//
//   DesiredSize    reads that storage and reports nothing without it
//                  (CoreImports::UIElement_GetDesiredSize).
//   ActualWidth    reads the render size from that storage, and without it
//                  falls back to the size the markup specified -- or to zero
//                  while the element is still measure-dirty, which it stays
//                  when no parent ever measured it
//                  (CFrameworkElement::GetActualWidth).
//
// A tree of numbers cannot say which of the two branches produced a zero, so
// these cases pin the distinction: a Shape reports nothing but still renders
// at its specified size, while the child of a Canvas reports nothing and
// renders at nothing, because it was never measured in the first place.

#include <iostream>
#include <memory>
#include <stdexcept>

#include "border.h"
#include "canvas.h"
#include "content_presenter.h"
#include "icon.h"
#include "image.h"
#include "shape.h"
#include "stack_panel.h"

using namespace openxaml;

namespace {
void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::unique_ptr<Border> SizedBorder(double width, double height) {
    auto border = std::make_unique<Border>();
    border->set_width(width);
    border->set_height(height);
    return border;
}

// A Shape is not a layout element, so a root one has no layout storage: it
// reports no desired size at all, and its geometry -- which is what its
// MeasureOverride would have returned -- reaches neither number.
void ShapeReportsNothingAndRendersNothing() {
    Path path;
    path.data = {false, 0, 0, 10, 10};
    path.Measure({400, 300});
    path.Arrange({0, 0, 400, 300});
    Check(path.desired_size().width == 0 && path.desired_size().height == 0,
          "Shape desired size");
    Check(path.render_size().width == 0 && path.render_size().height == 0,
          "unsized Shape render size");
}

// The other half of the fallback: no storage, but the element was measured, so
// ActualWidth answers with the specified size rather than with zero. The
// desired size stays nothing -- an explicit Width never reaches it.
void ShapeRendersItsSpecifiedSize() {
    Path path;
    path.data = {false, 0, 0, 10, 10};
    path.set_width(40);
    path.set_height(40);
    path.Measure({400, 300});
    path.Arrange({0, 0, 400, 300});
    Check(path.desired_size().width == 0 && path.desired_size().height == 0,
          "sized Shape desired size");
    Check(path.render_size().width == 40 && path.render_size().height == 40,
          "sized Shape render size");

    Image image;
    image.set_width(40);
    image.set_height(40);
    image.Measure({400, 300});
    image.Arrange({0, 0, 400, 300});
    Check(image.desired_size().width == 0 && image.desired_size().height == 0,
          "sized Image desired size");
    Check(image.render_size().width == 40 && image.render_size().height == 40,
          "sized Image render size");
}

// A Canvas is the one Panel that is not a layout element. Its own numbers
// follow the same fallback as a Shape's -- nothing desired, the specified size
// rendered -- and because its MeasureOverride never runs, its children are
// never measured and stay at the measure-dirty zero.
void CanvasLaysOutNothing() {
    Canvas canvas;
    auto* first = SizedBorder(30, 20).release();
    auto* second = SizedBorder(30, 20).release();
    Canvas::SetLeft(*first, 25);
    Canvas::SetTop(*first, 10);
    Canvas::SetLeft(*second, 40);
    Canvas::SetTop(*second, 40);
    canvas.AddChild(std::unique_ptr<Element>(first));
    canvas.AddChild(std::unique_ptr<Element>(second));
    canvas.Measure({400, 300});
    canvas.Arrange({0, 0, 400, 300});

    Check(canvas.desired_size().width == 0 && canvas.desired_size().height == 0,
          "Canvas desired size");
    Check(canvas.render_size().width == 0 && canvas.render_size().height == 0,
          "unsized Canvas render size");
    for (const Element* child : canvas.Children()) {
        Check(child->desired_size().width == 0 && child->desired_size().height == 0,
              "Canvas child desired size");
        Check(child->render_size().width == 0 && child->render_size().height == 0,
              "Canvas child render size");
        Check(child->layout_slot().x == 0 && child->layout_slot().y == 0,
              "Canvas child layout slot");
    }
}

void SizedCanvasRendersItsSpecifiedSize() {
    Canvas canvas;
    canvas.set_width(200);
    canvas.set_height(150);
    canvas.Measure({400, 300});
    canvas.Arrange({0, 0, 400, 300});
    Check(canvas.desired_size().width == 0 && canvas.desired_size().height == 0,
          "sized Canvas desired size");
    Check(canvas.render_size().width == 200 && canvas.render_size().height == 150,
          "sized Canvas render size");
}

// The guard on the classification. An IconElement is a layout element even
// though it draws the same geometry a Path does, and a Panel that is not a
// Canvas measures its children as it always did -- so the rule above must not
// widen into "leaf elements report nothing".
void LayoutElementsStillMeasure() {
    PathIcon icon;
    icon.data = {false, 0, 0, 10, 10};
    icon.Measure({400, 300});
    Check(icon.desired_size().width == 10 && icon.desired_size().height == 10,
          "PathIcon desired size");

    StackPanel panel;
    panel.AddChild(SizedBorder(30, 20));
    panel.Measure({400, 300});
    panel.Arrange({0, 0, 400, 300});
    Check(panel.desired_size().width == 30 && panel.desired_size().height == 20,
          "StackPanel desired size");
    Check(panel.Children().front()->render_size().width == 30,
          "StackPanel child render size");
}

// ContentPresenter aligns its content with HorizontalContentAlignment, whose
// default is Stretch -- FrameworkElement's default, not Control's, which is
// Center. Content with no explicit size therefore fills the inner rect, and
// the margin comes out of what it fills.
void PresenterStretchesUnsizedContent() {
    auto content = std::make_unique<Border>();
    content->set_margin({4, 4, 4, 4});
    ContentPresenter presenter;
    presenter.SetContent(std::move(content));
    presenter.Measure({400, 300});
    presenter.Arrange({0, 0, 400, 300});
    Check(presenter.desired_size().width == 8 && presenter.desired_size().height == 8,
          "presenter desired size");
    const Element* child = presenter.Children().front();
    Check(child->render_size().width == 392 && child->render_size().height == 292,
          "stretched content render size");
}
}  // namespace

int main() {
    try {
        ShapeReportsNothingAndRendersNothing();
        ShapeRendersItsSpecifiedSize();
        CanvasLaysOutNothing();
        SizedCanvasRendersItsSpecifiedSize();
        LayoutElementsStillMeasure();
        PresenterStretchesUnsizedContent();
        std::cout << "layout participation tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
