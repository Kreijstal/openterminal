// Two layout rules the wave-2 oracle answered, held here as arithmetic.
//
// Both are visible in the corpus, so this file is not the arbiter -- the
// recorded measurement is. It exists because each rule is one number in one
// case there, and a rule stated as a rule is what stops a later change from
// re-deciding it by accident.
//
// **The rounding tie.** Layout rounding is round-half-*up*, not the
// round-half-to-even the ported source's Math.Round does. L0-props-rounding-half
// records Width="120.5" as 121, and L5-xprimitives-boolean-rounding records a
// 60.5 x 30.5 child measuring 61 x 31 through a rounding parent. Only positive
// ties are recorded; the negative direction follows floor(v + 0.5) because that
// is the one rule that produces the recorded numbers without a second case.
//
// **ContentControl does not stretch its content.** Its content alignment
// defaults to Left/Top, so the content is arranged at its own desired size and
// placed at the origin of the padding rect. L0-props-content-stretch records a
// Border child of a 400 x 300 ContentControl at 0 x 0, and the four
// L0-props-inherits-* cases record the child of a stretched ContentControl at
// the height the inherited font gives it rather than at 300.

#include <iostream>
#include <memory>
#include <string>

#include "border.h"
#include "control.h"
#include "layout.h"
#include "stack_panel.h"

using namespace openxaml;

namespace {

int failures = 0;

// Not assert(): a condition here may carry the side effect the next line
// depends on, and NDEBUG would erase it along with the check.
void Check(bool condition, const char* what, int line) {
    if (condition) return;
    std::cerr << "layout_rules_test.cpp:" << line << ": CHECK failed: " << what << "\n";
    ++failures;
}

#define CHECK(condition) Check(static_cast<bool>(condition), #condition, __LINE__)

void RoundingTiesGoUp() {
    CHECK(RoundLayoutValue(120.5, 1.0) == 121.0);
    CHECK(RoundLayoutValue(60.5, 1.0) == 61.0);
    CHECK(RoundLayoutValue(30.5, 1.0) == 31.0);

    // The half-to-even tie-break would have answered 60 and 30 for these two,
    // which is the disagreement the two cases above settle.
    CHECK(RoundLayoutValue(59.5, 1.0) == 60.0);
    CHECK(RoundLayoutValue(0.5, 1.0) == 1.0);

    // Everything that is not a tie is unchanged.
    CHECK(RoundLayoutValue(0.4, 1.0) == 0.0);
    CHECK(RoundLayoutValue(0.6, 1.0) == 1.0);
    CHECK(RoundLayoutValue(120.0, 1.0) == 120.0);
    CHECK(RoundLayoutValue(-1.4, 1.0) == -1.0);
    CHECK(RoundLayoutValue(-1.6, 1.0) == -2.0);

    // The scaled path rounds the device-pixel value, so a half-DIP at 2x is
    // not a tie at all and only the tie at 0.25 is.
    CHECK(RoundLayoutValue(120.5, 2.0) == 120.5);
    CHECK(RoundLayoutValue(120.25, 2.0) == 120.5);

    // L0-props-rounding-half, end to end.
    Border border;
    border.set_width(120.5);
    border.Measure({400.0, 300.0});
    CHECK(border.desired_size().width == 121.0);

    // L5-xprimitives-boolean-rounding: the child opts out, the parent does not,
    // so the fractional size survives one level and is rounded by the next.
    auto inner = std::make_unique<Border>();
    inner->set_width(60.5);
    inner->set_height(30.5);
    inner->set_use_layout_rounding(false);
    Border outer;
    outer.SetChild(std::move(inner));
    outer.Measure({400.0, 300.0});
    CHECK(outer.desired_size().width == 61.0);
    CHECK(outer.desired_size().height == 31.0);
}

void ContentControlDoesNotStretchItsContent() {
    ContentControl control;
    auto child = std::make_unique<Border>();
    child->set_width(40.0);
    child->set_height(20.0);
    Border* content = child.get();
    control.SetContent(std::move(child));
    control.Measure({400.0, 300.0});
    control.Arrange({0.0, 0.0, 400.0, 300.0});

    CHECK(control.render_size().width == 400.0);
    CHECK(control.render_size().height == 300.0);
    CHECK(content->render_size().width == 40.0);
    CHECK(content->render_size().height == 20.0);
    CHECK(content->layout_slot().x == 0.0);
    CHECK(content->layout_slot().y == 0.0);
}

// L0-props-content-stretch: a Border with no size of its own inside a stretched
// ContentControl stays at nothing rather than filling the control.
void UnsizedContentStaysAtNothing() {
    ContentControl control;
    auto child = std::make_unique<Border>();
    Border* content = child.get();
    control.SetContent(std::move(child));
    control.Measure({400.0, 300.0});
    control.Arrange({0.0, 0.0, 400.0, 300.0});

    CHECK(content->render_size().width == 0.0);
    CHECK(content->render_size().height == 0.0);

    // And the default is Left/Top rather than "no alignment at all": asking for
    // Stretch brings the fill back, which is what makes the recorded zero a
    // statement about the default and not about the Border.
    control.set_horizontal_content_alignment(HorizontalAlignment::Stretch);
    control.set_vertical_content_alignment(VerticalAlignment::Stretch);
    control.Measure({400.0, 300.0});
    control.Arrange({0.0, 0.0, 400.0, 300.0});
    CHECK(content->render_size().width == 400.0);
    CHECK(content->render_size().height == 300.0);
}

// The other alignments move the content within the slot without resizing it.
void ContentAlignmentPlacesTheContent() {
    ContentControl control;
    auto child = std::make_unique<Border>();
    child->set_width(40.0);
    child->set_height(20.0);
    Border* content = child.get();
    control.SetContent(std::move(child));
    control.set_horizontal_content_alignment(HorizontalAlignment::Center);
    control.set_vertical_content_alignment(VerticalAlignment::Bottom);
    control.Measure({400.0, 300.0});
    control.Arrange({0.0, 0.0, 400.0, 300.0});
    CHECK(content->layout_slot().x == 180.0);
    CHECK(content->layout_slot().y == 280.0);
    CHECK(content->render_size().width == 40.0);
    CHECK(content->render_size().height == 20.0);
}

// Padding still insets the content, and the alignment is measured inside what
// the padding leaves rather than inside the whole slot.
void PaddingInsetsTheAlignedContent() {
    ContentControl control;
    auto child = std::make_unique<Border>();
    child->set_width(40.0);
    child->set_height(20.0);
    Border* content = child.get();
    control.SetContent(std::move(child));
    control.set_padding({10.0, 5.0, 30.0, 15.0});
    control.set_horizontal_content_alignment(HorizontalAlignment::Right);
    control.set_vertical_content_alignment(VerticalAlignment::Bottom);
    control.Measure({400.0, 300.0});
    control.Arrange({0.0, 0.0, 400.0, 300.0});
    CHECK(content->layout_slot().x == 330.0);
    CHECK(content->layout_slot().y == 265.0);
    CHECK(content->render_size().width == 40.0);
    CHECK(content->render_size().height == 20.0);
}

}  // namespace

int main() {
    RoundingTiesGoUp();
    ContentControlDoesNotStretchItsContent();
    UnsizedContentStaysAtNothing();
    ContentAlignmentPlacesTheContent();
    PaddingInsetsTheAlignedContent();

    if (failures) std::cerr << failures << " check(s) failed\n";
    return failures ? 1 : 0;
}
