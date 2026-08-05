// What the style system does that no measurement can see, and what it refuses.
//
// Two kinds of check, for the two reasons a corpus cannot make them.
//
// The first is the same reason resources_test.cpp exists: a case that is
// supposed to fail to load has no measurement, so there is nothing for
// check_layout.py to compare. Every named refusal below is one of those --
// an unknown TargetType, a setter for a property the type does not have, a
// value that is not readable as that property's type, a style applied to the
// wrong kind of element. A style system that skipped any of them silently
// would produce a layout that is merely wrong, and wrong in a way no number
// traces back to the style.
//
// The second is the property store. A measurement is a tree of numbers, so it
// cannot say *which slot* a value came from -- and that is the entire content
// of what a style is. An implementation that wrote a setter straight into the
// local slot measures identically to this one in every case the corpus can
// express, and differs the moment anything clears a local value or replaces a
// style. Those are the checks in the second half.
//
// Deliberately not a framework, for the reasons the two files beside it give.

#include <cstdio>
#include <memory>
#include <string>

#include "border.h"
#include "control.h"
#include "element.h"
#include "grid.h"
#include "markup.h"
#include "markup_tree.h"
#include "property.h"
#include "stack_panel.h"
#include "style.h"
#include "text.h"

namespace {

int failures = 0;
int checks = 0;

// Measuring a TextBlock needs metrics for the family it names, and the
// invalidation checks measure one. These are not Segoe UI and are not
// pretending to be: nothing here checks a text size, so any self-consistent
// numbers do. The same arrangement property_test.cpp makes, for the same
// reason -- the test stays independent of a font harvest.
void InstallATestFont() {
    openxaml::FontMetrics metrics;
    metrics.units_per_em = 1000.0;
    metrics.ascender = 800.0;
    metrics.descender = -200.0;
    metrics.advances[U'M'] = 500.0;
    openxaml::FontLibrary::Default().Add("Segoe UI", metrics);
}

const char* kPresentation = "http://schemas.microsoft.com/winfx/2006/xaml/presentation";
const char* kXaml = "http://schemas.microsoft.com/winfx/2006/xaml";

std::string Document(const std::string& root_tag, const std::string& attributes,
                     const std::string& body) {
    std::string head = "<" + root_tag + " xmlns=\"" + kPresentation + "\" xmlns:x=\"" + kXaml + "\"";
    if (!attributes.empty()) head += " " + attributes;
    return head + ">" + body + "</" + root_tag + ">";
}

std::string Resources(const std::string& owner, const std::string& entries) {
    return "<" + owner + ".Resources>" + entries + "</" + owner + ".Resources>";
}

void Fail(const std::string& what, const std::string& detail) {
    ++failures;
    std::printf("FAIL %s\n     %s\n", what.c_str(), detail.c_str());
}

// The markup must be rejected, and the rejection must contain `expected`. Both
// halves matter: a load that fails for an unrelated reason would otherwise
// count as this test passing.
void Rejects(const std::string& what, const std::string& markup, const std::string& expected) {
    ++checks;
    try {
        openxaml::LoadMarkup(markup);
    } catch (const std::exception& error) {
        const std::string message = error.what();
        if (message.find(expected) == std::string::npos)
            Fail(what, "rejected, but with \"" + message + "\" rather than \"" + expected + "\"");
        return;
    }
    Fail(what, "loaded, and should not have");
}

void Loads(const std::string& what, const std::string& markup) {
    ++checks;
    try {
        openxaml::LoadMarkup(markup);
    } catch (const std::exception& error) {
        Fail(what, std::string("refused it: ") + error.what());
    }
}

void Check(bool condition, const std::string& what) {
    ++checks;
    if (!condition) Fail(what, "the check did not hold");
}

void CheckDouble(double actual, double expected, const std::string& what) {
    ++checks;
    if (actual == expected) return;
    Fail(what, "got " + std::to_string(actual) + ", wanted " + std::to_string(expected));
}

// The element at the end of the single-child chain, which is where every
// markup case below puts the thing under test.
openxaml::Element* Innermost(openxaml::Element* element) {
    while (!element->Children().empty()) element = element->Children().front();
    return element;
}

const std::string kBoxy =
    "<Style x:Key=\"Boxy\" TargetType=\"Border\">"
    "<Setter Property=\"Width\" Value=\"60\"/><Setter Property=\"Height\" Value=\"30\"/></Style>";

// --- what a style refuses -----------------------------------------------------

void TargetTypes() {
    Rejects("a TargetType no type answers to",
            Document("Grid", "",
                     Resources("Grid", "<Style x:Key=\"S\" TargetType=\"Nonsense\">"
                                       "<Setter Property=\"Width\" Value=\"60\"/></Style>") +
                         "<Border Style=\"{StaticResource S}\"/>"),
            "the type 'Nonsense' is not implemented");

    // The abstract bases are not markup types here, and a style that names one
    // has to say so rather than matching nothing quietly. This is also what
    // L5-styles-implicit-derived-type records as the reason it cannot answer
    // its own question locally.
    Rejects("a TargetType that is a base class this parser does not model",
            Document("Grid", "",
                     Resources("Grid", "<Style TargetType=\"Control\">"
                                       "<Setter Property=\"FontSize\" Value=\"24\"/></Style>") +
                         "<ContentControl/>"),
            "the type 'Control' is not implemented");

    Rejects("a Style with no TargetType at all",
            Document("Grid", "",
                     Resources("Grid", "<Style x:Key=\"S\">"
                                       "<Setter Property=\"Width\" Value=\"60\"/></Style>") +
                         "<Border Style=\"{StaticResource S}\"/>"),
            "must have a non-null value for TargetType");

    // The mismatch that only the explicit route can reach -- an implicit style
    // is filed under the type it targets and could never find a Grid.
    Rejects("a style applied to an element of the wrong type",
            Document("Grid", "", Resources("Grid", kBoxy) +
                                     "<StackPanel Style=\"{StaticResource Boxy}\"/>"),
            "cannot apply a Style with TargetType 'Border' to an object of type 'StackPanel'");

    Rejects("a Style carrying a property that is not TargetType or BasedOn",
            Document("Grid", "",
                     Resources("Grid", "<Style x:Key=\"S\" TargetType=\"Border\" Width=\"3\">"
                                       "<Setter Property=\"Width\" Value=\"60\"/></Style>") +
                         "<Border Style=\"{StaticResource S}\"/>"),
            "<Style> does not take 'Width'");
}

void Setters() {
    // The same message the same property gets as an attribute, because it is
    // the same code that produces it. That identity is the point of routing a
    // setter through the ordinary attribute parser.
    Rejects("a setter for a property the TargetType does not have",
            Document("Grid", "",
                     Resources("Grid", "<Style x:Key=\"S\" TargetType=\"StackPanel\">"
                                       "<Setter Property=\"FontSize\" Value=\"24\"/></Style>") +
                         "<StackPanel Style=\"{StaticResource S}\"/>"),
            "the property 'FontSize' was not found in type "
            "'Windows.UI.Xaml.Controls.StackPanel'");

    Rejects("a setter for a property no type has",
            Document("Grid", "",
                     Resources("Grid", "<Style x:Key=\"S\" TargetType=\"Border\">"
                                       "<Setter Property=\"Nonsense\" Value=\"1\"/></Style>") +
                         "<Border Style=\"{StaticResource S}\"/>"),
            "the property 'Nonsense' was not found in type");

    Rejects("a value that cannot be read as the property's type",
            Document("Grid", "",
                     Resources("Grid", "<Style x:Key=\"S\" TargetType=\"Border\">"
                                       "<Setter Property=\"Width\" Value=\"wide\"/></Style>") +
                         "<Border Style=\"{StaticResource S}\"/>"),
            "cannot read \"wide\" as a number for Width");

    Rejects("an enumeration value that is not one of the names",
            Document("Grid", "",
                     Resources("Grid",
                               "<Style x:Key=\"S\" TargetType=\"Border\">"
                               "<Setter Property=\"HorizontalAlignment\" Value=\"Sideways\"/>"
                               "</Style>") +
                         "<Border Style=\"{StaticResource S}\"/>"),
            "\"Sideways\" is not a valid HorizontalAlignment");

    Rejects("a setter with no Property",
            Document("Grid", "",
                     Resources("Grid", "<Style x:Key=\"S\" TargetType=\"Border\">"
                                       "<Setter Value=\"60\"/></Style>") +
                         "<Border Style=\"{StaticResource S}\"/>"),
            "a <Setter> has no Property");

    Rejects("a setter with no Value",
            Document("Grid", "",
                     Resources("Grid", "<Style x:Key=\"S\" TargetType=\"Border\">"
                                       "<Setter Property=\"Width\"/></Style>") +
                         "<Border Style=\"{StaticResource S}\"/>"),
            "the Setter for 'Width' was given no Value");

    Rejects("a setter given a Value twice, once each way",
            Document("Grid", "",
                     Resources("Grid",
                               "<Style x:Key=\"S\" TargetType=\"Border\">"
                               "<Setter Property=\"Width\" Value=\"60\"><Setter.Value>30"
                               "</Setter.Value></Setter></Style>") +
                         "<Border Style=\"{StaticResource S}\"/>"),
            "has a Value attribute and a <Setter.Value> as well");

    Rejects("an empty <Setter.Value>",
            Document("Grid", "",
                     Resources("Grid", "<Style x:Key=\"S\" TargetType=\"Border\">"
                                       "<Setter Property=\"Width\"><Setter.Value></Setter.Value>"
                                       "</Setter></Style>") +
                         "<Border Style=\"{StaticResource S}\"/>"),
            "<Setter.Value> for 'Width' is empty");

    Rejects("a <Setter.Value> holding an object that is not a lookup",
            Document("Grid", "",
                     Resources("Grid", "<Style x:Key=\"S\" TargetType=\"Border\">"
                                       "<Setter Property=\"Width\"><Setter.Value><Border/>"
                                       "</Setter.Value></Setter></Style>") +
                         "<Border Style=\"{StaticResource S}\"/>"),
            "takes a <StaticResource>, not <Border>");

    Rejects("a setter carrying anything besides Property and Value",
            Document("Grid", "",
                     Resources("Grid",
                               "<Style x:Key=\"S\" TargetType=\"Border\">"
                               "<Setter Property=\"Width\" Value=\"60\" Target=\"x\"/></Style>") +
                         "<Border Style=\"{StaticResource S}\"/>"),
            "a <Setter> does not take 'Target'");

    // "Can't style the Style property" -- the core refuses it before asking
    // what it would mean, and so does this.
    Rejects("a setter for Style itself",
            Document("Grid", "",
                     Resources("Grid", "<Style x:Key=\"S\" TargetType=\"Border\">"
                                       "<Setter Property=\"Style\" Value=\"x\"/></Style>") +
                         "<Border Style=\"{StaticResource S}\"/>"),
            "a <Setter> cannot set 'Style'");

    // Text and Data are carried on the parsed node rather than in the store,
    // so a setter for either would apply nothing at all. Naming it is the only
    // honest outcome; silently doing nothing is the one this parser refuses.
    Rejects("a setter for a property that never reaches the store",
            Document("Grid", "",
                     Resources("Grid", "<Style x:Key=\"S\" TargetType=\"TextBlock\">"
                                       "<Setter Property=\"Text\" Value=\"M\"/></Style>") +
                         "<TextBlock Style=\"{StaticResource S}\"/>"),
            "nothing it sets reaches the property store");

    Rejects("a <Style> holding something that is not a Setter",
            Document("Grid", "",
                     Resources("Grid", "<Style x:Key=\"S\" TargetType=\"Border\"><Border/></Style>") +
                         "<Border Style=\"{StaticResource S}\"/>"),
            "holds <Setter> elements, not <Border>");
}

void BasedOn() {
    Rejects("a BasedOn naming a key that is not there",
            Document("Grid", "",
                     Resources("Grid", "<Style x:Key=\"S\" TargetType=\"Border\" "
                                       "BasedOn=\"{StaticResource Missing}\">"
                                       "<Setter Property=\"Width\" Value=\"60\"/></Style>") +
                         "<Border Style=\"{StaticResource S}\"/>"),
            "resource 'Missing' not found");

    Rejects("a BasedOn naming a resource that is not a Style",
            Document("Grid", "",
                     Resources("Grid", "<x:Double x:Key=\"W\">60</x:Double>"
                                       "<Style x:Key=\"S\" TargetType=\"Border\" "
                                       "BasedOn=\"{StaticResource W}\">"
                                       "<Setter Property=\"Width\" Value=\"60\"/></Style>") +
                         "<Border Style=\"{StaticResource S}\"/>"),
            "cannot supply 'BasedOn'");

    Rejects("a BasedOn that is not a markup extension",
            Document("Grid", "",
                     Resources("Grid", kBoxy +
                                       "<Style x:Key=\"S\" TargetType=\"Border\" BasedOn=\"Boxy\">"
                                       "<Setter Property=\"Margin\" Value=\"4\"/></Style>") +
                         "<Border Style=\"{StaticResource S}\"/>"),
            "'BasedOn' takes a {StaticResource}");

    Rejects("a BasedOn whose base targets an unrelated type",
            Document("Grid", "",
                     Resources("Grid",
                               "<Style x:Key=\"Panel\" TargetType=\"StackPanel\">"
                               "<Setter Property=\"Spacing\" Value=\"6\"/></Style>"
                               "<Style x:Key=\"S\" TargetType=\"Border\" "
                               "BasedOn=\"{StaticResource Panel}\">"
                               "<Setter Property=\"Width\" Value=\"60\"/></Style>") +
                         "<Border Style=\"{StaticResource S}\"/>"),
            "can only base on a Style whose target type is a base type of 'Border'");

    // A cycle is not expressible: a style enters its dictionary only once it
    // is finished, so the only way to name itself is a key that is not there
    // yet. The runtime needs a loop check because its Style is a mutable
    // object; this is what takes the place of one.
    Rejects("a style based on itself",
            Document("Grid", "",
                     Resources("Grid", "<Style x:Key=\"S\" TargetType=\"Border\" "
                                       "BasedOn=\"{StaticResource S}\">"
                                       "<Setter Property=\"Width\" Value=\"60\"/></Style>") +
                         "<Border Style=\"{StaticResource S}\"/>"),
            "resource 'S' not found");
}

void References() {
    Rejects("a Style= naming a key that is not there",
            Document("Grid", "", "<Border Style=\"{StaticResource Boxy}\"/>"),
            "resource 'Boxy' not found");

    // The two directions of the same confusion, each named for what it is.
    Rejects("a Style= naming a resource that is not a Style",
            Document("Grid", "",
                     Resources("Grid", "<x:Double x:Key=\"W\">60</x:Double>") +
                         "<Border Style=\"{StaticResource W}\"/>"),
            "cannot supply 'Style'");

    Rejects("a length property naming a Style",
            Document("Grid", "",
                     Resources("Grid", kBoxy) + "<Border Width=\"{StaticResource Boxy}\"/>"),
            "the resource 'Boxy' is Style, which cannot supply 'Width'");

    Rejects("a Style= that is a literal rather than a lookup",
            Document("Grid", "", "<Border Style=\"Boxy\"/>"),
            "'Style' takes a {StaticResource}");

    Rejects("a Style= behind an extension that is not implemented",
            Document("Grid", "", "<Border Style=\"{Binding Boxy}\"/>"),
            "the markup extension '{Binding}' is not implemented");

    // {ThemeResource} is the other spelling of the same lookup, so a Style=
    // written that way reaches the style path rather than being refused as an
    // extension -- and a key nothing declares fails as a missing key, which is
    // what says the reference was resolved and not merely accepted.
    Loads("a Style= written as a {ThemeResource}",
          Document("Grid", "",
                   Resources("Grid", kBoxy) + "<Border Style=\"{ThemeResource Boxy}\"/>"));

    Rejects("a Style= naming a key no dictionary in scope declares",
            Document("Grid", "", "<Border Style=\"{ThemeResource Boxy}\"/>"),
            "resource 'Boxy' not found for 'Style'");

    Rejects("two implicit styles for one type in one dictionary",
            Document("Grid", "",
                     Resources("Grid",
                               "<Style TargetType=\"Border\">"
                               "<Setter Property=\"Width\" Value=\"60\"/></Style>"
                               "<Style TargetType=\"Border\">"
                               "<Setter Property=\"Width\" Value=\"100\"/></Style>") +
                         "<Border/>"),
            "two implicit Styles for 'Border' are declared in one dictionary");

    Rejects("two explicit styles under one key",
            Document("Grid", "", Resources("Grid", kBoxy + kBoxy) +
                                     "<Border Style=\"{StaticResource Boxy}\"/>"),
            "the resource key 'Boxy' is declared twice in one dictionary");

    // A style with no setters is legal and inert -- the runtime seals and
    // applies it like any other. Refusing it would be inventing a rule.
    Loads("a style with no setters at all",
          Document("Grid", "",
                   Resources("Grid", "<Style x:Key=\"S\" TargetType=\"Border\"/>") +
                       "<Border Style=\"{StaticResource S}\"/>"));

    Loads("an implicit style for a type nothing in the tree is",
          Document("Grid", "",
                   Resources("Grid", "<Style TargetType=\"TextBlock\">"
                                     "<Setter Property=\"FontSize\" Value=\"24\"/></Style>") +
                       "<Border/>"));
}

// --- which slot the value landed in -------------------------------------------

void AStyleValueIsNotALocalValue() {
    using namespace openxaml;

    std::unique_ptr<Element> root = LoadMarkup(
        Document("Grid", "", Resources("Grid", kBoxy) +
                                 "<Border Style=\"{StaticResource Boxy}\"/>"));
    Element* border = Innermost(root.get());

    // The whole of what a style is, and the whole of what no measurement can
    // see: the value is there, and it is not in the local slot.
    CheckDouble(border->width(), 60.0, "a setter supplies the value");
    Check(!border->HasLocalValue(Element::WidthProperty()),
          "a setter does not write the local slot");
    Check(border->HasStyleValue(Element::WidthProperty()), "a setter writes the style slot");

    // And a property no setter names is still untouched, rather than having
    // been given its default by the application.
    Check(!border->HasStyleValue(Element::MarginProperty()),
          "a style writes only the properties its setters name");
    Check(!border->HasLocalValue(Element::MarginProperty()),
          "and does not write the local slot for the others either");
}

void ALocalValueShadowsTheStyleWithoutReplacingIt() {
    using namespace openxaml;

    std::unique_ptr<Element> root = LoadMarkup(
        Document("Grid", "", Resources("Grid", kBoxy) +
                                 "<Border Style=\"{StaticResource Boxy}\" Width=\"20\"/>"));
    Element* border = Innermost(root.get());

    CheckDouble(border->width(), 20.0, "a local value beats a setter");
    Check(border->HasStyleValue(Element::WidthProperty()),
          "and the setter is still underneath it");

    // The check the corpus cannot make. An implementation that wrote setters
    // into the local slot measures identically up to here and gives Auto from
    // this point on.
    border->ClearValue(Element::WidthProperty());
    CheckDouble(border->width(), 60.0, "clearing the local value exposes the setter");

    border->ClearStyleValues();
    Check(IsAuto(border->width()), "and clearing the style exposes the default");
}

void ReapplyingAStyleReplacesTheOldOne() {
    using namespace openxaml;

    Border border;
    Style wide;
    wide.target_type = "Border";
    wide.setters.push_back(StyleSetter{&Element::WidthProperty(), 60.0});
    wide.setters.push_back(StyleSetter{&Element::HeightProperty(), 30.0});

    Style tall;
    tall.target_type = "Border";
    tall.setters.push_back(StyleSetter{&Element::HeightProperty(), 90.0});

    ApplyStyle(border, wide, "Border", Border::Owners());
    CheckDouble(border.width(), 60.0, "the first style applies");
    CheckDouble(border.height(), 30.0, "both of its setters apply");

    // Replacing a style is clear-then-apply, and the clear is what makes the
    // difference: a Width the old style set and the new one does not must go
    // back to Auto rather than being left behind.
    border.ClearStyleValues();
    ApplyStyle(border, tall, "Border", Border::Owners());
    Check(IsAuto(border.width()), "a property the new style does not set goes back to the default");
    CheckDouble(border.height(), 90.0, "and one it does set takes the new value");

    // A local value set in between survives both, which is the reference's
    // rule: a local set masks the style layer until it is cleared.
    border.set_height(11.0);
    border.ClearStyleValues();
    ApplyStyle(border, wide, "Border", Border::Owners());
    CheckDouble(border.height(), 11.0, "a local value set in between still wins");
    border.ClearValue(Element::HeightProperty());
    CheckDouble(border.height(), 30.0, "and clearing it exposes the style that was applied since");
}

void AStyleValueIsInheritedLikeALocalOne() {
    using namespace openxaml;

    auto text = std::make_unique<TextBlock>();
    TextBlock* leaf = text.get();
    ContentControl control;
    control.SetContent(std::move(text));

    Style big;
    big.target_type = "ContentControl";
    big.setters.push_back(StyleSetter{&FontSizeProperty(), 24.0});

    ApplyStyle(control, big, "ContentControl", ContentControl::Owners());

    // The question the corpus asks as L5-styles-fontsize-inherits-from-style,
    // asked here about the store instead of about a line height: an ancestor's
    // style value stops the inheritance walk exactly as a written one does.
    CheckDouble(leaf->font_size(), 24.0, "a style value on an ancestor is inherited");
    Check(!leaf->HasLocalValue(FontSizeProperty()), "and is not copied into the descendant");
    Check(!leaf->HasStyleValue(FontSizeProperty()), "nor into its style slot");

    // And it moves when the style does, rather than having been snapshotted.
    control.ClearStyleValues();
    CheckDouble(leaf->font_size(), 14.0, "removing the style takes the inherited value with it");
}

void AStyleValueBeatsAnInheritedOne() {
    using namespace openxaml;

    auto text = std::make_unique<TextBlock>();
    TextBlock* leaf = text.get();
    ContentControl control;
    control.SetContent(std::move(text));
    control.set_font_size(22.0);
    CheckDouble(leaf->font_size(), 22.0, "the descendant inherits to begin with");

    Style small;
    small.target_type = "TextBlock";
    small.setters.push_back(StyleSetter{&FontSizeProperty(), 10.0});
    ApplyStyle(*leaf, small, "TextBlock", TextBlock::Owners());

    // The slot order, stated as one number: style is above inherited.
    CheckDouble(leaf->font_size(), 10.0, "the descendant's own style beats what it inherits");

    leaf->set_font_size(30.0);
    CheckDouble(leaf->font_size(), 30.0, "and a local value beats both");
    leaf->ClearValue(FontSizeProperty());
    CheckDouble(leaf->font_size(), 10.0, "clearing it falls back to the style, not to the ancestor");
    leaf->ClearStyleValues();
    CheckDouble(leaf->font_size(), 22.0, "and clearing that falls back to the ancestor");
}

void AStyleValueInvalidatesMeasure() {
    using namespace openxaml;

    Border border;
    border.Measure({100.0, 100.0});
    Check(!border.needs_measure(), "measuring clears the flag");

    Style wide;
    wide.target_type = "Border";
    wide.setters.push_back(StyleSetter{&Element::WidthProperty(), 60.0});
    ApplyStyle(border, wide, "Border", Border::Owners());
    Check(border.needs_measure(), "applying a style that changes a size invalidates measure");

    // A style whose setter changes nothing changes nothing. The store compares
    // effective values rather than counting writes, and a style is not special.
    Border shadowed;
    shadowed.set_width(60.0);
    shadowed.Measure({100.0, 100.0});
    ApplyStyle(shadowed, wide, "Border", Border::Owners());
    Check(!shadowed.needs_measure(),
          "a setter under a local value of the same size invalidates nothing");
}

void AnAncestorsStyleInvalidatesTheSubtree() {
    using namespace openxaml;

    auto text = std::make_unique<TextBlock>();
    TextBlock* leaf = text.get();
    ContentControl control;
    control.SetContent(std::move(text));
    control.Measure({100.0, 100.0});
    Check(!leaf->needs_measure(), "the subtree starts clean");

    Style big;
    big.target_type = "ContentControl";
    big.setters.push_back(StyleSetter{&FontSizeProperty(), 24.0});
    ApplyStyle(control, big, "ContentControl", ContentControl::Owners());
    Check(leaf->needs_measure(), "a style on an ancestor invalidates what inherits from it");
}

void TheMergeIsByPropertyAndNotByName() {
    using namespace openxaml;

    // Border's Padding and the panels' Padding are two different properties
    // that are spelled the same. A merge that matched on the name would have
    // one override the other, and nothing measurable would ever notice.
    Check(FindProperty(Border::Owners(), "Padding") != FindProperty(Grid::Owners(), "Padding"),
          "the two Paddings really are different properties");

    Style base;
    base.target_type = "Border";
    base.setters.push_back(StyleSetter{&Border::PaddingProperty(), Thickness{1, 1, 1, 1}});
    Style derived = base;
    derived.setters.push_back(StyleSetter{&ChromePaddingProperty(), Thickness{2, 2, 2, 2}});
    Check(derived.setters.size() == 2, "a same-named setter for a different property is a second one");
    Check(derived.Find(Border::PaddingProperty()) != nullptr, "and the first one is still there");
}

// --- the merge, through the parser --------------------------------------------

void BasedOnMerges() {
    using namespace openxaml;

    const std::string base = "<Style x:Key=\"Base\" TargetType=\"Border\">"
                             "<Setter Property=\"Width\" Value=\"60\"/>"
                             "<Setter Property=\"Height\" Value=\"24\"/></Style>";
    const std::string derived = "<Style x:Key=\"Derived\" TargetType=\"Border\" "
                                "BasedOn=\"{StaticResource Base}\">"
                                "<Setter Property=\"Width\" Value=\"100\"/></Style>";

    std::unique_ptr<Element> root = LoadMarkup(
        Document("Grid", "", Resources("Grid", base + derived) +
                                 "<Border Style=\"{StaticResource Derived}\"/>"));
    Element* border = Innermost(root.get());
    CheckDouble(border->width(), 100.0, "the derived setter overrides the base's");
    CheckDouble(border->height(), 24.0, "and the base's other setter survives");

    // The base is unchanged by having been derived from -- the merge builds a
    // new list rather than editing the one it was given.
    root = LoadMarkup(Document("Grid", "", Resources("Grid", base + derived) +
                                               "<Border Style=\"{StaticResource Base}\"/>"));
    border = Innermost(root.get());
    CheckDouble(border->width(), 60.0, "the base style is not modified by the derived one");

    // Last setter wins within one style, which is what the core's reverse
    // search means. Not an error there and not one here.
    root = LoadMarkup(Document(
        "Grid", "",
        Resources("Grid", "<Style x:Key=\"S\" TargetType=\"Border\">"
                          "<Setter Property=\"Width\" Value=\"60\"/>"
                          "<Setter Property=\"Width\" Value=\"100\"/></Style>") +
            "<Border Style=\"{StaticResource S}\"/>"));
    CheckDouble(Innermost(root.get())->width(), 100.0, "a property set twice takes the last");
}

void ImplicitAndExplicitDoNotMerge() {
    using namespace openxaml;

    // An element with a Style= is not also given the implicit style for its
    // type: the two occupy one slot and the explicit one takes it whole.
    std::unique_ptr<Element> root = LoadMarkup(Document(
        "Grid", "",
        Resources("Grid", "<Style TargetType=\"Border\">"
                          "<Setter Property=\"Width\" Value=\"60\"/>"
                          "<Setter Property=\"Height\" Value=\"30\"/></Style>"
                          "<Style x:Key=\"Slim\" TargetType=\"Border\">"
                          "<Setter Property=\"Width\" Value=\"20\"/></Style>") +
            "<Border Style=\"{StaticResource Slim}\"/>"));
    Element* border = Innermost(root.get());
    CheckDouble(border->width(), 20.0, "the explicit style applies");
    Check(IsAuto(border->height()), "and the implicit style's other setter does not");
}

}  // namespace

int main() {
    InstallATestFont();

    TargetTypes();
    Setters();
    BasedOn();
    References();

    AStyleValueIsNotALocalValue();
    ALocalValueShadowsTheStyleWithoutReplacingIt();
    ReapplyingAStyleReplacesTheOldOne();
    AStyleValueIsInheritedLikeALocalOne();
    AStyleValueBeatsAnInheritedOne();
    AStyleValueInvalidatesMeasure();
    AnAncestorsStyleInvalidatesTheSubtree();
    TheMergeIsByPropertyAndNotByName();

    BasedOnMerges();
    ImplicitAndExplicitDoNotMerge();

    std::printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
