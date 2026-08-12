// The two halves of an application's default-style universe, and the rules
// that decide what a lookup in it answers.
//
// **The dictionary is layered, and which layers exist is the host's fact.**
// What `Application.Resources` holds in a running WinUI 2 application is not
// one dictionary but a stack: the framework's own `generic.xaml` at the
// bottom, `XamlControlsResources` merged over it, the application's own
// dictionaries above that. A key both halves define answers from the higher
// one; a key only the framework defines still answers. Those are the rules
// the layer tests below pin, and they are Terminal's -- its App.xaml merges
// XamlControlsResources first thing.
//
// The corpus is not recorded in that host. The oracle probe
// (phase3/harness/xaml_probe.cpp) has no Application object at all, so
// nothing merges anything there and the framework's floor is the only layer
// a lookup can reach: `L5-defaults-layer-order` recorded ButtonPadding as the
// framework's 8,4,8,5, and every key only WinUI 2 defines was refused by
// name. The ceiling test below pins how a measurement host loads the same
// two-file database as that one-layer host without giving up the two-layer
// rules a real application still needs.
//
// The built-in style half cannot be checked by the corpus at all:
//
// **A built-in style is a slot, and a measurement cannot see a slot.** The
// same argument style_test.cpp makes for the style slot, one layer further
// down: `docs/design-notes/styles.md` in microsoft-ui-xaml (MIT, 188f602b)
// states that the property system has a `BaseValueSourceStyle` layer and a
// `BaseValueSourceBuiltInStyle` layer, that they coexist, and that the first
// wins where both set the same property. An implementation that wrote the
// framework's setters into the style slot measures identically in every case
// the corpus can express, and differs the moment an application's own implicit
// style sets one property of a control -- where the real runtime keeps the
// rest of the built-in style and this one would have erased it.
//
// Deliberately not a framework, for the reason the files beside it give.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "border.h"
#include "control.h"
#include "default_styles.h"
#include "element.h"
#include "markup.h"
#include "property.h"
#include "resources.h"
#include "style.h"

namespace {

int failures = 0;
int checks = 0;

void Fail(const std::string& what, const std::string& detail) {
    ++failures;
    std::printf("FAIL %s: %s\n", what.c_str(), detail.c_str());
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

void CheckText(const std::string& actual, const std::string& expected, const std::string& what) {
    ++checks;
    if (actual == expected) return;
    Fail(what, "got '" + actual + "', wanted '" + expected + "'");
}

openxaml::ResourceValue Number(const std::string& text) {
    return openxaml::ResourceValue{"x:Double", text, openxaml::ValueKind::Number, nullptr};
}

// --- the layered application dictionary ---------------------------------------

// A key both layers define answers from the higher one, and a key only the
// framework defines still answers. Both directions matter and they are
// different failures: getting the first wrong returns the framework's stale
// value for something WinUI 2 deliberately redefined, and getting the second
// wrong loses the framework's floor entirely.
//
// These are the rules of a host that has both layers -- a running WinUI 2
// application. The measurement hosts never build this library shape, because
// the probe they are compared against has one layer; see the ceiling test
// below.
void TheHigherLayerShadowsTheLowerOne() {
    openxaml::ThemeResourceLibrary library;

    openxaml::ResourceDictionary framework;
    framework.Add("SharedByBoth", Number("10"));
    framework.Add("OnlyTheFramework", Number("11"));
    library.Add("Default", std::move(framework),
                openxaml::ResourceLayer::GlobalThemeResources);

    openxaml::ResourceDictionary controls;
    controls.Add("SharedByBoth", Number("20"));
    controls.Add("OnlyWinUI", Number("21"));
    library.Add("Default", std::move(controls),
                openxaml::ResourceLayer::XamlControlsResources);

    const openxaml::ResourceScope scope = library.ActiveLayers();
    Check(scope.size() == 2, "both layers are in the lookup scope");

    CheckText(openxaml::LookUpResource(scope, "SharedByBoth", "test").text, "20",
              "XamlControlsResources shadows the framework's value");
    CheckText(openxaml::LookUpResource(scope, "OnlyTheFramework", "test").text, "11",
              "a key only the framework defines still resolves");
    CheckText(openxaml::LookUpResource(scope, "OnlyWinUI", "test").text, "21",
              "a key only WinUI 2 defines still resolves");

    // Three distinct keys across two layers of two keys each. The count is the
    // one number a caller uses to say how big the application dictionary is,
    // and summing the layers would report four.
    CheckDouble(static_cast<double>(library.ActiveKeyCount()), 3.0,
                "the key count is distinct keys, not the sum of the layers");
}

// The order is the enum's, not the insertion order. A loader that happened to
// read the two databases the other way round must not produce the other
// answer -- which is exactly what a directory listing would have done, since
// the two files are read in filename order.
void TheOrderDoesNotDependOnWhichLayerArrivedFirst() {
    openxaml::ThemeResourceLibrary library;

    openxaml::ResourceDictionary controls;
    controls.Add("Contested", Number("20"));
    library.Add("Default", std::move(controls),
                openxaml::ResourceLayer::XamlControlsResources);

    openxaml::ResourceDictionary framework;
    framework.Add("Contested", Number("10"));
    library.Add("Default", std::move(framework),
                openxaml::ResourceLayer::GlobalThemeResources);

    CheckText(openxaml::LookUpResource(library.ActiveLayers(), "Contested", "test").text, "20",
              "the layer decides, not the order the layers were loaded in");
}

// Two dictionaries claiming one theme *and* one layer is a duplicate, not a
// merge. It has no defensible resolution and is refused rather than resolved
// by arrival order.
void OneLayerCannotBeLoadedTwice() {
    openxaml::ThemeResourceLibrary library;
    library.Add("Default", openxaml::ResourceDictionary{},
                openxaml::ResourceLayer::XamlControlsResources);

    bool refused = false;
    try {
        library.Add("Default", openxaml::ResourceDictionary{},
                    openxaml::ResourceLayer::XamlControlsResources);
    } catch (const std::exception& error) {
        refused = std::string(error.what()).find("already loaded at this layer") !=
                  std::string::npos;
    }
    Check(refused, "a second dictionary for one theme and one layer is refused by name");
}

// A theme that exists in only one layer is a theme the library has. The
// framework's generic.xaml carries HighContrast and so does WinUI 2, but the
// shape has to work when only one of them does -- otherwise the set of themes
// an application has would depend on which halves happened to be generated.
void ALayerCanSupplyAThemeTheOtherDoesNot() {
    openxaml::ThemeResourceLibrary library;
    openxaml::ResourceDictionary framework;
    framework.Add("OnlyHere", Number("7"));
    library.Add("HighContrast", std::move(framework),
                openxaml::ResourceLayer::GlobalThemeResources);

    library.SetActiveTheme("HighContrast");
    CheckText(openxaml::LookUpResource(library.ActiveLayers(), "OnlyHere", "test").text, "7",
              "a theme only the framework layer has still resolves");
}

// --- XamlControlsResources ----------------------------------------------------

// The URI is the whole of what the class decides, and it decides it from two
// properties. Checked against `dev/dll/XamlControlsResources.cpp` (MIT, 2.8.4)
// rather than against a memory of it: the default is Version2 and not compact,
// which is what Terminal's App.xaml gets by not setting either.
void TheSourceUriIsTheOneTheMitSourceComputes() {
    openxaml::XamlControlsResources resources;
    CheckText(resources.SourceUri(),
              "ms-appx:///Microsoft.UI.Xaml/Themes/21h1_themeresources.xaml",
              "the default source is 21H1's Version2 theme resources");

    resources.set_use_compact_resources(true);
    CheckText(resources.SourceUri(),
              "ms-appx:///Microsoft.UI.Xaml/Themes/21h1_compact_themeresources.xaml",
              "UseCompactResources inserts compact_ between the release prefix and the file");

    resources.set_use_compact_resources(false);
    resources.set_controls_resources_version(
        openxaml::XamlControlsResources::Version::Version1);
    CheckText(resources.SourceUri(),
              "ms-appx:///Microsoft.UI.Xaml/Themes/21h1_themeresources_v1.xaml",
              "Version1 selects the pre-Fluent file");
}

// What merging it does to a lookup: its entries land above the framework's own
// and shadow them. This is the same rule as the layer checks above, reached
// through the class that actually performs the merge in an application --
// and only in an application: the oracle probe never constructs one, which is
// why ButtonPadding measures as 8,4,8,5 there and not as the 11,5,11,6 this
// merge serves.
void MergingItPutsItsEntriesAboveTheFrameworks() {
    openxaml::ThemeResourceLibrary library;
    openxaml::ResourceDictionary framework;
    framework.Add("ButtonPadding", Number("8,4,8,5"));
    framework.Add("OnlyTheFramework", Number("5"));
    library.Add("Light", std::move(framework),
                openxaml::ResourceLayer::GlobalThemeResources);

    openxaml::ResourceDictionary winui;
    winui.Add("ButtonPadding", Number("11,5,11,6"));
    std::map<std::string, openxaml::ResourceDictionary> dictionaries;
    dictionaries.emplace("Light", std::move(winui));

    openxaml::XamlControlsResources resources;
    resources.Merge(library, std::move(dictionaries));
    library.SetActiveTheme("Light");

    CheckText(openxaml::LookUpResource(library.ActiveLayers(), "ButtonPadding", "test").text,
              "11,5,11,6", "XamlControlsResources' value wins where both define the key");
    CheckText(openxaml::LookUpResource(library.ActiveLayers(), "OnlyTheFramework", "test").text,
              "5", "and the framework's floor is still underneath it");
}

// --- which layers a host actually has ------------------------------------------

// The two databases in one directory describe the application dictionary of a
// real WinUI 2 application -- and the oracle probe is not one. The probe
// (phase3/harness/xaml_probe.cpp) is a bare WindowsXamlManager on a thread:
// no Application object, no App.xaml, nothing ever merged. The recording says
// what that means for a lookup, three times over: every key only WinUI 2
// defines was refused by name (`L5-theme-double-height`,
// `L5-theme-thickness-padding`, and the CardStrokeColorDefaultBrush /
// ToggleSwitchOuterBorderStrokeThickness harvests at L7), a key only the
// framework defines resolved (`L5-defaults-framework-only-key`), and
// ButtonPadding -- which both halves define and disagree about -- measured as
// the framework's 8,4,8,5, not WinUI 2's 11,5,11,6
// (`L5-defaults-layer-order`, bit-identical to its framework-inline twin).
//
// So the loader takes a ceiling: the highest layer the loading host actually
// has. A measurement host models the probe and stops at the framework's
// floor; a host that models a running application loads everything, and the
// shadowing rules above are its semantics, unchanged.
void TheProbeHostCeilingLoadsOnlyTheFrameworksFloor() {
    namespace fs = std::filesystem;
    const fs::path directory = fs::temp_directory_path() / "openxaml-layer-ceiling-test";
    fs::remove_all(directory);
    fs::create_directories(directory);
    {
        std::ofstream out(directory / "default-styles.json");
        out << R"({"layer": "GlobalThemeResources", "sources": {}, "themes": {"Default": {)"
            << R"("ButtonPadding": {"status": "value", "type": "Thickness", "text": "8,4,8,5"},)"
            << R"("OnlyTheFramework": {"status": "value", "type": "x:Double", "text": "14"}}}})";
    }
    {
        // No "layer" member, like the real extractor output: it defaults to
        // XamlControlsResources, which is what the file is.
        std::ofstream out(directory / "winui-2.8.4.json");
        out << R"({"source": {"commit": "test"}, "themes": {"Default": {)"
            << R"("ButtonPadding": {"status": "value", "type": "Thickness", "text": "11,5,11,6"},)"
            << R"("OnlyWinUI": {"status": "value", "type": "x:Double", "text": "24"}}}})";
    }

    openxaml::ThemeResourceLibrary probe_host;
    const int keys = openxaml::LoadThemeResources(
        probe_host, directory.string(), openxaml::ResourceLayer::GlobalThemeResources);
    CheckDouble(static_cast<double>(keys), 2.0,
                "under the probe-host ceiling only the framework half's keys load");
    CheckText(openxaml::LookUpResource(probe_host.ActiveLayers(), "ButtonPadding", "test").text,
              "8,4,8,5",
              "a key both halves define answers from the framework, the only layer there is");
    bool refused = false;
    try {
        openxaml::LookUpResource(probe_host.ActiveLayers(), "OnlyWinUI", "test");
    } catch (const openxaml::MarkupError&) {
        refused = true;
    }
    Check(refused, "a key only WinUI 2 defines fails by name, exactly as the probe recorded");

    // The default ceiling is still the full application dictionary: both
    // layers load and WinUI 2's value shadows. That is a real host too -- it
    // is Terminal's -- it is just not the one the corpus is recorded in.
    openxaml::ThemeResourceLibrary application;
    openxaml::LoadThemeResources(application, directory.string());
    CheckText(openxaml::LookUpResource(application.ActiveLayers(), "ButtonPadding", "test").text,
              "11,5,11,6", "without a ceiling both layers load and WinUI 2's value shadows");

    fs::remove_all(directory);
}

// --- the built-in style slot --------------------------------------------------

// ContentControl rather than a bespoke subclass: it is the least specialised
// concrete Control here, so what is checked below is the layer and not
// anything a particular control does with it.
using StyledControl = openxaml::ContentControl;

openxaml::Style OneSetter(const std::string& target, const openxaml::DependencyProperty& property,
                          openxaml::PropertyValue value) {
    openxaml::Style style;
    style.target_type = target;
    style.setters.push_back({&property, std::move(value)});
    return style;
}

// The whole content of the new slot: a built-in setter is read when nothing
// above it says anything, and it is *not* a style value -- so the two can be
// told apart, which is what makes the next two checks meaningful.
void ABuiltInValueIsNotAStyleValue() {
    StyledControl control;
    const openxaml::Style style =
        OneSetter("ContentControl", openxaml::Element::MinWidthProperty(), 40.0);
    ApplyBuiltInStyle(control, style, "ContentControl", control.PropertyOwners());

    CheckDouble(control.min_width(), 40.0, "the built-in style supplies the value");
    Check(control.HasBuiltInStyleValue(openxaml::Element::MinWidthProperty()),
          "the value is in the built-in slot");
    Check(!control.HasStyleValue(openxaml::Element::MinWidthProperty()),
          "the value is not in the style slot");
    Check(!control.HasLocalValue(openxaml::Element::MinWidthProperty()),
          "the value is not in the local slot");
}

// The rule `docs/design-notes/styles.md` states in one sentence: where both
// layers set the same property, the `Style` layer wins.
void AStyleValueShadowsTheBuiltInOne() {
    StyledControl control;
    ApplyBuiltInStyle(control, OneSetter("ContentControl", openxaml::Element::MinWidthProperty(), 40.0),
                      "ContentControl", control.PropertyOwners());
    ApplyStyle(control, OneSetter("ContentControl", openxaml::Element::MinWidthProperty(), 90.0),
               "ContentControl", control.PropertyOwners());

    CheckDouble(control.min_width(), 90.0, "the application's style wins");
    Check(control.HasBuiltInStyleValue(openxaml::Element::MinWidthProperty()),
          "the built-in value is shadowed, not replaced");

    // And the whole reason the two are separate slots: removing the style
    // exposes what generic.xaml said, rather than whatever the style left.
    control.ClearStyleValues();
    CheckDouble(control.min_width(), 40.0, "removing the style exposes the built-in value again");
}

// The other half of the same sentence: the two coexist. An application style
// that sets one property must not erase the rest of the built-in style, which
// is exactly what the design note gives as the reason the two layers exist --
// an "EmptyStateHyperlinkStyle" that sets four properties and relies on the
// built-in style for its Template.
void AStyleThatSetsOnePropertyLeavesTheRestOfTheBuiltInStyle() {
    StyledControl control;
    openxaml::Style built_in;
    built_in.target_type = "ContentControl";
    built_in.setters.push_back({&openxaml::Element::MinWidthProperty(), 40.0});
    built_in.setters.push_back({&openxaml::Element::MinHeightProperty(), 32.0});
    ApplyBuiltInStyle(control, built_in, "ContentControl", control.PropertyOwners());

    ApplyStyle(control, OneSetter("ContentControl", openxaml::Element::MinWidthProperty(), 90.0),
               "ContentControl", control.PropertyOwners());

    CheckDouble(control.min_width(), 90.0, "the style's property is the style's");
    CheckDouble(control.min_height(), 32.0, "the property it does not set is still the framework's");
}

// A local value outranks both, which is the ordering the whole chain exists
// for and the one a wrong slot would break most visibly.
void ALocalValueShadowsBoth() {
    StyledControl control;
    ApplyBuiltInStyle(control, OneSetter("ContentControl", openxaml::Element::MinWidthProperty(), 40.0),
                      "ContentControl", control.PropertyOwners());
    ApplyStyle(control, OneSetter("ContentControl", openxaml::Element::MinWidthProperty(), 90.0),
               "ContentControl", control.PropertyOwners());
    control.set_min_width(5.0);

    CheckDouble(control.min_width(), 5.0, "the local value outranks both style layers");
    control.ClearValue(openxaml::Element::MinWidthProperty());
    CheckDouble(control.min_width(), 90.0, "clearing it exposes the style layer");
}

// A built-in value is an effective value like any other, so it inherits.
// The alternative -- a slot the inheritance walk does not look in -- would
// leave a TextBlock inside a styled control reading the registered default
// instead of the framework's font size, and no case in the corpus has a
// built-in style to notice it with.
void ABuiltInValueIsInherited() {
    StyledControl parent;
    auto child = std::make_unique<openxaml::Border>();
    openxaml::Border* observed = child.get();
    parent.SetContent(std::move(child));

    ApplyBuiltInStyle(parent, OneSetter("ContentControl", openxaml::FontSizeProperty(), 18.0),
                      "ContentControl", parent.PropertyOwners());
    CheckDouble(observed->GetDouble(openxaml::FontSizeProperty()), 18.0,
                "a built-in value flows down the inheritance chain");
}

// The target-type check is the same one an explicit style gets, and it is
// worth having for a different reason: a built-in table is keyed by type name,
// so a table built wrong writes one type's setters onto another and produces
// numbers with nothing in the markup to explain them.
void ABuiltInStyleRefusesTheWrongTarget() {
    openxaml::Border border;
    bool refused = false;
    try {
        ApplyBuiltInStyle(border, OneSetter("Button", openxaml::Element::MinWidthProperty(), 40.0),
                          "Border", border.PropertyOwners());
    } catch (const openxaml::MarkupError& error) {
        refused = std::string(error.what()).find("built-in Style with TargetType 'Button'") !=
                  std::string::npos;
    }
    Check(refused, "a built-in style for the wrong type is refused by name");
}

// --- the loader ---------------------------------------------------------------

// A database that is not there loads nothing and is not an error. The same
// rule the theme dictionary follows, and the reason is the same: the database
// is generated rather than committed, and a bare checkout has to run.
void AMissingDatabaseIsNotAnError() {
    openxaml::DefaultStyleRegistry registry;
    const int loaded = openxaml::LoadDefaultStyles(
        registry, "/nonexistent/openterminal/default-styles");
    CheckDouble(static_cast<double>(loaded), 0.0, "a missing database loads nothing");
    Check(registry.Find("Button") == nullptr, "and registers nothing");
}

}  // namespace

int main() {
    TheHigherLayerShadowsTheLowerOne();
    TheOrderDoesNotDependOnWhichLayerArrivedFirst();
    OneLayerCannotBeLoadedTwice();
    ALayerCanSupplyAThemeTheOtherDoesNot();

    TheSourceUriIsTheOneTheMitSourceComputes();
    MergingItPutsItsEntriesAboveTheFrameworks();

    TheProbeHostCeilingLoadsOnlyTheFrameworksFloor();

    ABuiltInValueIsNotAStyleValue();
    AStyleValueShadowsTheBuiltInOne();
    AStyleThatSetsOnePropertyLeavesTheRestOfTheBuiltInStyle();
    ALocalValueShadowsBoth();
    ABuiltInValueIsInherited();
    ABuiltInStyleRefusesTheWrongTarget();

    AMissingDatabaseIsNotAnError();

    std::printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
