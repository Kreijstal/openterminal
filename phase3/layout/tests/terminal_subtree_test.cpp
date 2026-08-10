// The rules the harvested Terminal subtrees pin, and the shape of the tree
// they are reported in.
//
// A level 7 case is a subtree lifted out of Terminal's own markup, so it
// reaches parts of the runtime the generated levels never do: a Control with a
// template of its own, a transform hung off a Shape, a ToolTip's default
// style, a string handed to a ContentControl. Each of those is one recorded
// answer, and each is written out here as arithmetic so that changing it is a
// deliberate edit rather than a case that quietly moves.

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#include "basic_controls.h"
#include "fonts.h"
#include "markup.h"
#include "text.h"

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

    // XML's expanded empty-element spelling is the same transform, not a
    // renderer feature boundary. Whitespace between the tags is ignored by
    // the scanner just as it is for the self-closing spelling.
    std::unique_ptr<Element> expanded = LoadMarkup(
        std::string("<Rectangle") + kXamlNamespaces + " Width=\"12\" Height=\"12\" "
        "RenderTransformOrigin=\"0.5,0.5\">"
        "<Rectangle.RenderTransform><RotateTransform Angle=\"17\">"
        "</RotateTransform></Rectangle.RenderTransform></Rectangle>");
    CHECK(expanded->visual_transform().kind == VisualTransformKind::Rotate);
    CHECK(expanded->visual_transform().angle_degrees == 17.0);
    CHECK(expanded->render_transform_origin().x == 0.5);
    CHECK(expanded->render_transform_origin().y == 0.5);

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

// A font whose line box is exactly its em, so that a line height in this test
// reads as the font size that produced it and nothing has to be un-rounded.
FontMetrics EmTallFace() {
    FontMetrics metrics;
    metrics.units_per_em = 2048;
    metrics.ascender = 2048;
    metrics.descender = 0;
    return metrics;
}

// The runtime's default ToolTip style, as L7-terminal-24911ba19e pins it. The
// case is `<ToolTip><TextBlock><Run/></TextBlock></ToolTip>` -- an empty line
// of text -- and the oracle records the ToolTip 18 x 30 with the TextBlock at
// 9 x 6 and 0 x 15.9609. So:
//
//   the content sits at 9 across and 6 down, and the ToolTip is 18 wide
//   around content that is not wide at all, which is ToolTipBorderPadding
//   ("9,6,9,8" in the harvested dictionary) and 30 - 16 - 6 confirms its
//   bottom; and
//   15.9609 is Segoe UI's line box at *12*, not at the 14 the case
//   environment inherits, which is ToolTipContentThemeFontSize.
//
// Both are style values rather than local ones: they are what the content
// inherits when the markup says nothing, and what the markup overrides when it
// does.
void AToolTipCarriesTheRecordedDefaultStyle() {
    FontLibrary::Default().Add("ToolTip Test Face", EmTallFace());

    ContentControl host;
    host.set_font_family("ToolTip Test Face");
    host.set_font_size(14.0);

    auto tip = std::make_unique<ToolTip>();
    ToolTip& tooltip = *tip;
    auto line = std::make_unique<TextBlock>();
    TextBlock& text = *line;
    tip->SetContent(std::move(line));
    host.SetContent(std::move(tip));

    host.Measure({400.0, 300.0});
    host.Arrange({0.0, 0.0, 400.0, 300.0});

    // The style's font size beats the 14 the host would otherwise have passed
    // down -- and it reaches the content, which sets neither.
    CHECK(tooltip.font_size() == 12.0);
    CHECK(text.font_size() == 12.0);
    CHECK(text.desired_size().height == 12.0);

    CHECK(tooltip.desired_size().width == 18.0);
    CHECK(tooltip.desired_size().height == 26.0);
    CHECK(text.layout_slot().x == 9.0);
    CHECK(text.layout_slot().y == 6.0);

    // Style, not local: markup that sets either one still wins.
    tooltip.set_font_size(30.0);
    CHECK(tooltip.font_size() == 30.0);
}

// What a string handed to a ContentControl is worth, which is nothing.
//
// L7-terminal-0e66f8e18d holds `<Button Grid.Row="0">Create</Button>` beside a
// 400-wide TextBox in a horizontal StackPanel, and the oracle records the
// Button 20 x 32 at all three available sizes, with no node under it and the
// panel 420 wide. "Create" in Segoe UI at 14 is 41 wide on its own, so the
// string is not in the 20: the runtime's Content is an `object`, the probe's
// walk only descends into one that is a UIElement, and a measurement detached
// from a live tree never turns this one into anything with a size.
//
// So 20 is the whole of the Button's chrome, and 32 is what the recording
// gives a Button whose content asks for nothing. Which part of 32 is chrome
// and which is the style's minimum height, no recording says; the corpus has
// exactly one Button in it.
void AStringContentIsNeitherANodeNorASize() {
    std::unique_ptr<Element> loaded =
        LoadMarkup(std::string("<Button") + kXamlNamespaces + " Grid.Row=\"0\">Create</Button>");
    auto* button = dynamic_cast<Button*>(loaded.get());
    CHECK(button != nullptr);
    CHECK(button->content_text() == "Create");
    CHECK(button->Children().empty());
    CHECK(button->RecordedChildren().empty());

    // The width a horizontal StackPanel measures its children with.
    button->Measure({kInfinity, 300.0});
    CHECK(button->desired_size().width == 20.0);
    CHECK(button->desired_size().height == 32.0);

    // Content that *is* an element still measures, and still shows through the
    // same chrome.
    std::unique_ptr<Element> sized = LoadMarkup(
        std::string("<Button") + kXamlNamespaces + "><Border Width=\"10\" Height=\"40\"/></Button>");
    sized->Measure({kInfinity, 300.0});
    CHECK(sized->desired_size().width == 30.0);
    CHECK(sized->desired_size().height == 40.0);
    CHECK(sized->RecordedChildren().size() == 1);
}

}  // namespace

int main() {
    ATemplatedControlIsALeafInTheRecordedTree();
    AContentControlsElementContentIsRecorded();
    ARenderTransformDoesNotReachLayout();
    AToolTipCarriesTheRecordedDefaultStyle();
    AStringContentIsNeitherANodeNorASize();
    std::cout << "terminal subtree rules ok\n";
    return 0;
}
