// The rules the harvested Terminal subtrees pin, and the shape of the tree
// they are reported in.
//
// A level 7 case is a subtree lifted out of Terminal's own markup, so it
// reaches parts of the runtime the generated levels never do: a Control with a
// template of its own, a transform hung off a Shape. Each of those is one recorded
// answer, and each is written out here as arithmetic so that changing it is a
// deliberate edit rather than a case that quietly moves.

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#include "basic_controls.h"
#include "markup.h"

using namespace openxaml;

namespace {

// Not assert(): a condition here may carry the side effect the next line
// depends on, and NDEBUG would erase it along with the check.
void Check(bool condition, const char* what, int line) {
    if (condition) return;
    std::cerr << "terminal_subtree_test.cpp:" << line << ": CHECK failed: " << what << "\n";
    std::exit(1);
}

#define CHECK(condition) Check(static_cast<bool>(condition), #condition, __LINE__)

const char* kXamlNamespaces =
    " xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\""
    " xmlns:x=\"http://schemas.microsoft.com/winfx/2006/xaml\"";

// The probe's walk descends into a Panel's children, a Border's child, and the
// Content of a ContentControl or a ContentPresenter -- and into nothing else.
// A Control's template child is a visual child none of those four reach, so
// L7-terminal-b6a4672b94 records the Thumb alone even though the Thumb's own
// ControlTemplate puts a Rectangle inside it. An implementation that reports
// the Rectangle is not measuring anything differently; it is answering a
// question the oracle was never asked.
void ATemplatedControlIsALeafInTheRecordedTree() {
    const std::string markup =
        std::string("<Thumb") + kXamlNamespaces + " x:Name=\"SwitchThumb\">"
        "<Thumb.Template><ControlTemplate TargetType=\"Thumb\">"
        "<Rectangle Fill=\"Transparent\"/>"
        "</ControlTemplate></Thumb.Template></Thumb>";
    std::unique_ptr<Element> thumb = LoadMarkup(markup);

    // The template child is built, adopted and measured -- it is the layout,
    // and Children() is what layout walks.
    CHECK(thumb->Children().size() == 1);
    thumb->Measure({400.0, 300.0});
    thumb->Arrange({0.0, 0.0, 400.0, 300.0});
    CHECK(thumb->render_size().width == 400.0);
    CHECK(thumb->render_size().height == 300.0);

    // What the oracle recorded is the Thumb and nothing under it.
    CHECK(thumb->RecordedChildren().empty());
}

// The same walk, from the other side: a ContentControl's content *is* reached,
// so dropping it from the recorded tree would lose a node the oracle has.
void AContentControlsElementContentIsRecorded() {
    std::unique_ptr<Element> control =
        LoadMarkup(std::string("<ContentControl") + kXamlNamespaces + "><Border Width=\"10\"/>"
                   "</ContentControl>");
    CHECK(control->RecordedChildren().size() == 1);
    CHECK(control->RecordedChildren().front() == control->Children().front());
}

// A RenderTransform is applied to the element's visual after layout has run,
// so it reaches neither measure nor arrange: L7-terminal-65dec6afa8 is a
// 12 x 12 Rectangle carrying a <CompositeTransform/>, and the oracle records
// the same pair a Rectangle without one records -- nothing desired, because a
// Shape at the root of a tree is not a layout element, and 12 x 12 rendered,
// because that is what the markup asked for.
void ARenderTransformDoesNotReachLayout() {
    const std::string markup =
        std::string("<Rectangle") + kXamlNamespaces + " Width=\"12\" Height=\"12\""
        " RadiusX=\"7\" RadiusY=\"7\" RenderTransformOrigin=\"0.5, 0.5\">"
        "<Rectangle.RenderTransform><CompositeTransform/></Rectangle.RenderTransform>"
        "</Rectangle>";
    std::unique_ptr<Element> shape = LoadMarkup(markup);
    shape->Measure({400.0, 300.0});
    shape->Arrange({0.0, 0.0, 400.0, 300.0});
    CHECK(shape->desired_size().width == 0.0);
    CHECK(shape->desired_size().height == 0.0);
    CHECK(shape->render_size().width == 12.0);
    CHECK(shape->render_size().height == 12.0);

    // Layout-inert is not the same as unread. An element that is no transform
    // at all is still refused by name, rather than being waved through as one
    // more thing that cannot move a number.
    std::string refusal;
    try {
        (void)LoadMarkup(std::string("<Rectangle") + kXamlNamespaces + ">"
                         "<Rectangle.RenderTransform><Border/></Rectangle.RenderTransform>"
                         "</Rectangle>");
    } catch (const MarkupError& error) {
        refusal = error.what();
    }
    CHECK(refusal.find("<Border>") != std::string::npos);
}

}  // namespace

int main() {
    ATemplatedControlIsALeafInTheRecordedTree();
    AContentControlsElementContentIsRecorded();
    ARenderTransformDoesNotReachLayout();
    std::cout << "terminal subtree rules ok\n";
    return 0;
}
