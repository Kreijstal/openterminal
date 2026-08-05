// Unit tests for the property system.
//
// The corpus is the arbiter for anything the runtime can be asked about, and
// nothing here duplicates it: every check below is a fact about the store that
// no measurement can express. A recorded measurement is a tree of numbers, so
// it cannot say whether a value was inherited or copied, whether clearing one
// restored the default or the ancestor, or whether an element noticed that a
// value moved. Those are the three things this file holds the engine to.
//
// No framework. A failing check prints what it expected and where, and the
// binary exits non-zero, which is all a test of this size needs.

#include <cmath>
#include <iostream>
#include <memory>
#include <string>

#include "border.h"
#include "canvas.h"
#include "chrome.h"
#include "content_presenter.h"
#include "control.h"
#include "grid.h"
#include "markup.h"
#include "property.h"
#include "stack_panel.h"
#include "text.h"

namespace {

int failures = 0;

void Check(bool condition, const std::string& what, int line) {
    if (condition) return;
    std::cerr << "property_test.cpp:" << line << ": " << what << "\n";
    ++failures;
}

template <typename T>
void CheckEqual(const T& actual, const T& expected, const std::string& what, int line) {
    if (actual == expected) return;
    std::cerr << "property_test.cpp:" << line << ": " << what << ": expected " << expected
              << ", got " << actual << "\n";
    ++failures;
}

#define CHECK(condition) Check((condition), #condition, __LINE__)
#define CHECK_EQUAL(actual, expected) CheckEqual((actual), (expected), #actual, __LINE__)

using namespace openxaml;

// Measuring a TextBlock needs metrics for the family it names, and the
// invalidation tests measure one. These are not Segoe UI and are not pretending
// to be: nothing here checks a text size, so any self-consistent numbers do,
// and inventing them here keeps the test independent of a harvest.
void InstallATestFont() {
    FontMetrics metrics;
    metrics.units_per_em = 1000.0;
    metrics.ascender = 800.0;
    metrics.descender = -200.0;
    metrics.advances[U'M'] = 500.0;
    FontLibrary::Default().Add("Segoe UI", metrics);
}

// --- metadata and defaults ----------------------------------------------------

void DefaultsComeFromTheRegistration() {
    Border border;
    CHECK(IsAuto(border.width()));
    CHECK_EQUAL(border.opacity(), 1.0);
    CHECK_EQUAL(border.max_width(), kInfinity);
    CHECK(border.use_layout_rounding());
    CHECK(!border.HasLocalValue(Element::WidthProperty()));

    // Reading a default is not the same as having one. Nothing was written, so
    // the store stays empty and a later style has something to write into.
    border.set_width(120.0);
    CHECK(border.HasLocalValue(Element::WidthProperty()));
    CHECK_EQUAL(border.width(), 120.0);
}

void ClearingRestoresWhatWasUnderneath() {
    Border border;
    border.set_width(120.0);
    border.ClearValue(Element::WidthProperty());
    CHECK(!border.HasLocalValue(Element::WidthProperty()));
    CHECK(IsAuto(border.width()));

    // Clearing a property that was never set is not an error and not a write.
    border.ClearValue(Element::HeightProperty());
    CHECK(!border.HasLocalValue(Element::HeightProperty()));
}

// --- lookup by name -----------------------------------------------------------

void NameLookupFollowsTheOwnerChain() {
    // FontSize is declared on Control and on TextBlock, and on nothing else.
    // This is the rule the runtime enforced when the corpus first tried to set
    // it on a StackPanel.
    CHECK(FindProperty(ContentControl::Owners(), "FontSize") != nullptr);
    CHECK(FindProperty(TextBlock::Owners(), "FontSize") != nullptr);
    CHECK(FindProperty(StackPanel::Owners(), "FontSize") == nullptr);
    CHECK(FindProperty(Border::Owners(), "FontSize") == nullptr);

    // The same property, not two that share a name -- which is what makes one
    // inherit into the other.
    CHECK(FindProperty(ContentControl::Owners(), "FontSize") ==
          FindProperty(TextBlock::Owners(), "FontSize"));

    // Inherited from FrameworkElement and UIElement respectively.
    CHECK(FindProperty(Border::Owners(), "Width") == &Element::WidthProperty());
    CHECK(FindProperty(Border::Owners(), "Opacity") == &Element::OpacityProperty());

    // An attached property carries its own owner, so it resolves on anything.
    CHECK(FindProperty(Border::Owners(), "Grid.Column") == &Grid::ColumnProperty());
    CHECK(FindProperty(TextBlock::Owners(), "Grid.Row") == &Grid::RowProperty());

    CHECK(FindProperty(Border::Owners(), "Nonsense") == nullptr);
    // A property of a type this element is not. BorderThickness reached
    // StackPanel and Grid in WinUI 2.6, so a TextBlock carries the rule now --
    // and a Canvas is the one panel that did not get the chrome, because it
    // has no content rect for a Padding to deflate.
    CHECK(FindProperty(TextBlock::Owners(), "BorderThickness") == nullptr);
    CHECK(FindProperty(Canvas::Owners(), "BorderThickness") == nullptr);
    CHECK(FindProperty(Canvas::Owners(), "Padding") == nullptr);

    // The panels' chrome is one property shared between them, and it is not
    // Border's. Two properties with the same name on different owners are
    // different properties -- which is exactly the case where a registry that
    // matched on the name alone would look correct and be wrong.
    CHECK(FindProperty(StackPanel::Owners(), "BorderThickness") ==
          &ChromeBorderThicknessProperty());
    CHECK(FindProperty(Grid::Owners(), "BorderThickness") == &ChromeBorderThicknessProperty());
    CHECK(FindProperty(ContentPresenter::Owners(), "Padding") == &ChromePaddingProperty());
    CHECK(FindProperty(Border::Owners(), "BorderThickness") == &Border::BorderThicknessProperty());
    CHECK(&Border::BorderThicknessProperty() != &ChromeBorderThicknessProperty());
    CHECK(&Border::PaddingProperty() != &ChromePaddingProperty());

    // Background is Panel's for the three panels and each type's own for the
    // two that are not panels -- the runtime's arrangement, and the reason a
    // Canvas takes a Background while it takes no Padding.
    CHECK(FindProperty(Canvas::Owners(), "Background") == &PanelBackgroundProperty());
    CHECK(FindProperty(Grid::Owners(), "Background") == &PanelBackgroundProperty());
    CHECK(FindProperty(Border::Owners(), "Background") == &Border::BackgroundProperty());
    CHECK(FindProperty(TextBlock::Owners(), "Background") == nullptr);

    // Canvas.Left and Canvas.Top are attached, so they resolve on anything --
    // and Spacing is StackPanel's alone, which no other panel answers to.
    CHECK(FindProperty(Border::Owners(), "Canvas.Left") == &Canvas::LeftProperty());
    CHECK(FindProperty(StackPanel::Owners(), "Spacing") == &StackPanel::SpacingProperty());
    CHECK(FindProperty(Grid::Owners(), "Spacing") == nullptr);

    // Visibility is UIElement's, so every type has it.
    CHECK(FindProperty(Canvas::Owners(), "Visibility") == &Element::VisibilityProperty());
    CHECK(FindProperty(TextBlock::Owners(), "Visibility") == &Element::VisibilityProperty());
}

// --- inheritance --------------------------------------------------------------

void InheritedValuesReachThroughTheTree() {
    auto text = std::make_unique<TextBlock>();
    TextBlock* leaf = text.get();
    auto border = std::make_unique<Border>();
    border->SetChild(std::move(text));

    ContentControl control;
    control.SetContent(std::move(border));

    // A Border has no FontSize of its own and does not stop one passing
    // through it, which is how the runtime propagates an inherited value.
    control.set_font_size(22.0);
    CHECK_EQUAL(leaf->font_size(), 22.0);
    CHECK(!leaf->HasLocalValue(FontSizeProperty()));

    // Local wins, and shadows the subtree below it as well.
    leaf->set_font_size(30.0);
    CHECK_EQUAL(leaf->font_size(), 30.0);
    control.set_font_size(11.0);
    CHECK_EQUAL(leaf->font_size(), 30.0);

    // Clearing the local value is not "set it to the default": it goes back to
    // reading the ancestor, which no field can express.
    leaf->ClearValue(FontSizeProperty());
    CHECK_EQUAL(leaf->font_size(), 11.0);

    // And with nothing above it either, the registered default.
    control.ClearValue(FontSizeProperty());
    CHECK_EQUAL(leaf->font_size(), 14.0);
}

void ANonInheritedPropertyStaysWhereItWasSet() {
    auto text = std::make_unique<TextBlock>();
    TextBlock* leaf = text.get();
    ContentControl control;
    control.SetContent(std::move(text));

    control.set_opacity(0.5);
    CHECK_EQUAL(control.opacity(), 0.5);
    CHECK_EQUAL(leaf->opacity(), 1.0);

    control.set_width(120.0);
    CHECK(IsAuto(leaf->width()));
}

void ReParentingReadsTheNewAncestor() {
    ContentControl warm;
    warm.set_font_size(22.0);
    ContentControl cold;

    // Straight through the engine rather than by moving a child between two
    // parents' collections, because who owns the element is a separate
    // question and this is the one being asked.
    TextBlock leaf;
    CHECK_EQUAL(leaf.font_size(), 14.0);

    leaf.SetInheritanceParent(&warm);
    CHECK_EQUAL(leaf.font_size(), 22.0);

    // Under something that says nothing about FontSize: back to the default,
    // not still reading the tree it left.
    leaf.SetInheritanceParent(&cold);
    CHECK_EQUAL(leaf.font_size(), 14.0);

    leaf.SetInheritanceParent(nullptr);
    CHECK_EQUAL(leaf.font_size(), 14.0);
}

// --- invalidation -------------------------------------------------------------

void ChangingAValueInvalidatesMeasure() {
    Border border;
    border.Measure({100.0, 100.0});
    CHECK(!border.needs_measure());

    // Opacity is registered as affecting nothing, and it is the only property
    // in the corpus that does not: a store that invalidated on every write
    // would look identical here without the flag meaning anything.
    border.set_opacity(0.5);
    CHECK(!border.needs_measure());

    border.set_width(40.0);
    CHECK(border.needs_measure());
}

void AnAncestorChangingInvalidatesTheSubtree() {
    auto text = std::make_unique<TextBlock>();
    TextBlock* leaf = text.get();
    ContentControl control;
    control.SetContent(std::move(text));

    control.Measure({100.0, 100.0});
    CHECK(!leaf->needs_measure());

    // The leaf never had a FontSize written to it and is stale anyway, which
    // is the whole point of tracking inheritance rather than copying values
    // down at construction.
    control.set_font_size(22.0);
    CHECK(leaf->needs_measure());

    // A local value shadows the ancestor, so the ancestor moving underneath it
    // changes nothing and invalidates nothing.
    leaf->set_font_size(30.0);
    control.Measure({100.0, 100.0});
    CHECK(!leaf->needs_measure());
    control.set_font_size(9.0);
    CHECK(!leaf->needs_measure());
}

// --- markup -------------------------------------------------------------------

void MarkupSetsOnlyWhatItWrote() {
    const std::string xmlns = "http://schemas.microsoft.com/winfx/2006/xaml/presentation";

    // The failure this whole file exists under: a TextBlock with no FontSize
    // must not come out of the parser carrying a local 14.
    std::unique_ptr<Element> root = LoadMarkup(
        "<ContentControl xmlns=\"" + xmlns + "\" FontSize=\"22\"><TextBlock/></ContentControl>");
    Element* leaf = root->Children().front();
    CHECK(!leaf->HasLocalValue(FontSizeProperty()));
    CHECK_EQUAL(leaf->GetDouble(FontSizeProperty()), 22.0);

    // Written out by hand, it stays -- even though it is the same value the
    // registration defaults to.
    root = LoadMarkup("<ContentControl xmlns=\"" + xmlns +
                      "\" FontSize=\"22\"><TextBlock FontSize=\"14\"/></ContentControl>");
    leaf = root->Children().front();
    CHECK(leaf->HasLocalValue(FontSizeProperty()));
    CHECK_EQUAL(leaf->GetDouble(FontSizeProperty()), 14.0);
}

void MarkupRefusesAPropertyTheTypeDoesNotHave() {
    const std::string xmlns = "http://schemas.microsoft.com/winfx/2006/xaml/presentation";
    bool refused = false;
    try {
        LoadMarkup("<StackPanel xmlns=\"" + xmlns + "\" FontSize=\"22\"/>");
    } catch (const MarkupError& error) {
        refused = true;
        CHECK_EQUAL(std::string(error.what()),
                    std::string("the property 'FontSize' was not found in type "
                                "'Windows.UI.Xaml.Controls.StackPanel'"));
    }
    CHECK(refused);

    // And one that no type has at all.
    refused = false;
    try {
        LoadMarkup("<Border xmlns=\"" + xmlns + "\" Nonsense=\"1\"/>");
    } catch (const MarkupError&) {
        refused = true;
    }
    CHECK(refused);
}

}  // namespace

int main() {
    InstallATestFont();

    DefaultsComeFromTheRegistration();
    ClearingRestoresWhatWasUnderneath();
    NameLookupFollowsTheOwnerChain();
    InheritedValuesReachThroughTheTree();
    ANonInheritedPropertyStaysWhereItWasSet();
    ReParentingReadsTheNewAncestor();
    ChangingAValueInvalidatesMeasure();
    AnAncestorChangingInvalidatesTheSubtree();
    MarkupSetsOnlyWhatItWrote();
    MarkupRefusesAPropertyTheTypeDoesNotHave();

    if (failures) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "property_test: all checks passed\n";
    return 0;
}
