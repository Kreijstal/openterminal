// What the recorded ScrollViewer answers say about the control's layout, in
// the form the recordings cannot: the corpus reports a tree of numbers per
// case, so it can say a viewer asked for 60x48 but not which of the two
// scrollbars the extra eight came from. Every number below is copied from a
// measurement, and the case it came from is named beside it.

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#include "markup.h"
#include "scroll_viewer.h"

using namespace openxaml;

namespace {

// Not assert(): a condition here may carry the side effect the next line
// depends on, and NDEBUG would erase it along with the check.
void Check(bool condition, const char* what, int line) {
    if (condition) return;
    std::cerr << "scroll_viewer_test.cpp:" << line << ": CHECK failed: " << what << "\n";
    std::exit(1);
}

#define CHECK(condition) Check(static_cast<bool>(condition), #condition, __LINE__)

const char* kNamespace = R"( xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation")";

// Measure and arrange the way measure_cases does, so a test number and a
// recorded number mean the same thing.
std::unique_ptr<Element> Layout(const std::string& markup, Size available) {
    std::unique_ptr<Element> root = LoadMarkup(markup);
    root->Measure(available);
    const Size desired = root->desired_size();
    root->Arrange({0.0, 0.0, std::isinf(available.width) ? desired.width : available.width,
                   std::isinf(available.height) ? desired.height : available.height});
    return root;
}

std::string Viewer(const std::string& attributes, const std::string& content) {
    return "<ScrollViewer" + std::string(kNamespace) + " " + attributes + ">" + content +
           "</ScrollViewer>";
}

bool Near(double actual, double expected) { return std::fabs(actual - expected) < 0.01; }

const char* kFixedContent = R"(<Border Width="60" Height="40"/>)";

// L3-scroll-free-*: an unconstrained viewer around 60x40. A forced scrollbar
// does not widen the viewer by its own thickness -- the template overlays the
// bars on the content -- but it does hold open the row or column it sits in,
// so a forced *pair* costs the shorter bar's thickness against the other bar's
// minimum length. Only free-visible-visible sees it: 40 is under the vertical
// bar's 32 minimum plus the horizontal bar's 16 thickness.
void ForcedBarsHoldOpenTheirTrack() {
    const Size available{400, 300};

    auto both = Layout(Viewer(R"(HorizontalScrollBarVisibility="Visible"
                                 VerticalScrollBarVisibility="Visible")",
                              kFixedContent),
                       available);
    CHECK(Near(both->desired_size().width, 60));   // L3-scroll-free-visible-visible-a0
    CHECK(Near(both->desired_size().height, 48));

    auto vertical = Layout(Viewer(R"(HorizontalScrollBarVisibility="Auto"
                                     VerticalScrollBarVisibility="Visible")",
                                  kFixedContent),
                           available);
    CHECK(Near(vertical->desired_size().width, 60));  // L3-scroll-free-auto-visible-a0
    CHECK(Near(vertical->desired_size().height, 40));

    auto horizontal = Layout(Viewer(R"(HorizontalScrollBarVisibility="Visible"
                                       VerticalScrollBarVisibility="Disabled")",
                                    kFixedContent),
                             available);
    CHECK(Near(horizontal->desired_size().width, 60));  // L3-scroll-free-visible-disabled-a0
    CHECK(Near(horizontal->desired_size().height, 40));

    // The same forced pair around content that already clears both minimums
    // costs nothing at all. L3-scroll-pad-asym-visible-small-free.
    auto padded = Layout(Viewer(R"(Padding="8,4,12,6" HorizontalScrollBarVisibility="Visible"
                                   VerticalScrollBarVisibility="Visible")",
                                kFixedContent),
                         available);
    CHECK(Near(padded->desired_size().width, 80));
    CHECK(Near(padded->desired_size().height, 50));
}

// L3-scroll-shape-zero-width-child: a bare viewer around a 0x16 child asks for
// 16x32 -- the vertical bar's thickness across and its minimum length down.
// Nothing else in the corpus pins the default: a viewer that defaulted its
// vertical bar to Auto would hide the bar here and ask for 0x16.
void VerticalBarIsVisibleByDefault() {
    auto viewer = Layout(Viewer("", R"(<Border Height="16"/>)"), {400, 300});
    CHECK(Near(viewer->desired_size().width, 16));
    CHECK(Near(viewer->desired_size().height, 32));

    auto* scroller = dynamic_cast<ScrollViewer*>(viewer.get());
    CHECK(scroller != nullptr);
    CHECK(scroller->vertical_scroll_bar_visibility() == ScrollBarVisibility::Visible);
    CHECK(scroller->horizontal_scroll_bar_visibility() == ScrollBarVisibility::Disabled);
}

// L3-scroll-mode-*: ScrollMode moves no number in the corpus. The pair below
// differs only in HorizontalScrollMode/VerticalScrollMode, and the recorded
// content is 300x260 either way -- what unbounds an axis at measure is the
// scrollbar visibility, not the mode.
void ScrollModeDoesNotBoundTheContent() {
    const std::string content = R"(<Border Width="300" Height="260"/>)";
    auto disabled_mode = Layout(
        Viewer(R"(Width="200" Height="150" HorizontalScrollBarVisibility="Auto"
                  VerticalScrollBarVisibility="Auto" HorizontalScrollMode="Disabled"
                  VerticalScrollMode="Disabled")",
               content),
        {400, 300});
    const Element* child = disabled_mode->Children().front();
    CHECK(Near(child->desired_size().width, 300));  // L3-scroll-mode-disabled-disabled-auto-large
    CHECK(Near(child->desired_size().height, 260));

    // ScrollBarVisibility="Disabled" is what bounds an axis, and it bounds it
    // to the padded client size. L3-scroll-pad-asym-disabled-large-sized.
    auto disabled_bars =
        Layout(Viewer(R"(Width="200" Height="150" Padding="8,4,12,6"
                         HorizontalScrollBarVisibility="Disabled"
                         VerticalScrollBarVisibility="Disabled")",
                      content),
               {400, 300});
    const Element* bounded = disabled_bars->Children().front();
    CHECK(Near(bounded->desired_size().width, 180));
    CHECK(Near(bounded->desired_size().height, 140));
}

// L3-scroll-pad-asym-*: the padding is the presenter's margin, so it moves the
// presenter and not the content inside it -- every recorded content slot under
// a padded viewer sits at the origin. L3-scroll-shape-padded-stretch adds the
// other half: the content still stretches across the whole padded client,
// because the bars are overlaid rather than subtracted from the viewport.
void PaddingMovesThePresenterNotTheContent() {
    auto padded = Layout(Viewer(R"(Padding="8,4,12,6" HorizontalScrollBarVisibility="Auto"
                                   VerticalScrollBarVisibility="Auto")",
                                kFixedContent),
                         {400, 300});
    const Element* child = padded->Children().front();
    CHECK(Near(child->layout_slot().x, 0));
    CHECK(Near(child->layout_slot().y, 0));

    auto stretched = Layout(
        Viewer(R"(Padding="3")", R"(<Border Padding="16" HorizontalAlignment="Stretch"/>)"),
        {400, 300});
    const Element* stretch_child = stretched->Children().front();
    CHECK(Near(stretch_child->render_size().width, 394));
    CHECK(Near(stretch_child->render_size().height, 294));
    CHECK(Near(stretch_child->layout_slot().x, 0));
    CHECK(Near(stretch_child->layout_slot().y, 0));
}

// The scroll offsets no measurement can reach: every recorded case is at the
// top-left, so the extent/viewport pair the offsets clamp against is only
// observable here. The viewport is the padded client size, undiminished by the
// overlaid bars.
void OffsetsClampAgainstTheOverlaidViewport() {
    auto root = Layout(Viewer(R"(Width="200" Height="150" HorizontalScrollBarVisibility="Visible"
                                 VerticalScrollBarVisibility="Visible")",
                              R"(<Border Width="300" Height="260"/>)"),
                       {400, 300});
    auto* viewer = dynamic_cast<ScrollViewer*>(root.get());
    CHECK(viewer != nullptr);
    CHECK(Near(viewer->desired_size().width, 200));  // L3-scroll-vis-visible-visible-large
    CHECK(Near(viewer->desired_size().height, 150));
    CHECK(Near(viewer->extent().width, 300) && Near(viewer->extent().height, 260));
    CHECK(Near(viewer->viewport().width, 200) && Near(viewer->viewport().height, 150));
    viewer->ScrollTo(1000, 1000);
    CHECK(Near(viewer->horizontal_offset(), 100));
    CHECK(Near(viewer->vertical_offset(), 110));
}

}  // namespace

int main() {
    ForcedBarsHoldOpenTheirTrack();
    VerticalBarIsVisibleByDefault();
    ScrollModeDoesNotBoundTheContent();
    PaddingMovesThePresenterNotTheContent();
    OffsetsClampAgainstTheOverlaidViewport();
    std::cout << "ScrollViewer tests passed\n";
    return 0;
}
