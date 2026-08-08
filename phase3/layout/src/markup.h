// Realises the XAML subset the corpus uses into an element tree.
//
// Everything unrecognised is an error, never a silent no-op. A parser that
// ignores an attribute it does not know produces a layout that is wrong in a
// way the numbers alone cannot explain -- the case would fail with no
// indication that the markup was never fully applied.

#ifndef OPENXAML_MARKUP_H
#define OPENXAML_MARKUP_H

#include <memory>
#include <stdexcept>
#include <string>

#include "element.h"
#include "binding.h"
#include "resw_strings.h"

namespace openxaml {

// Declared rather than included: the loader below fills one, and nothing in
// this header reads anything out of it. default_styles.h includes control.h,
// which every markup consumer would then get whether it wanted it or not.
class DefaultStyleRegistry;

class MarkupError : public std::runtime_error {
public:
    explicit MarkupError(const std::string& what) : std::runtime_error(what) {}
};

std::unique_ptr<Element> LoadMarkup(const std::string& markup);

// The same load, with a table for x:Uid to resolve against. Every corpus case
// goes through the overload above, because the oracle probe has no resource map
// either; this one is what a measurement run against Terminal's own strings
// would use.
std::unique_ptr<Element> LoadMarkup(const std::string& markup, const StringTable& strings);

// Realises {Binding}/{x:Bind} expressions against an observable data source.
// The source must outlive the returned tree, matching DataContext ownership in
// the WinUI object graph.
std::unique_ptr<Element> LoadMarkup(const std::string& markup, ObservableObject& source);
std::unique_ptr<Element> LoadMarkup(const std::string& markup, const StringTable& strings,
                                    ObservableObject& source);

// --- the framework's own default styles ---------------------------------------

// What a default-style load could not take, and why.
//
// Not an error list. The reconstructed table describes the whole framework --
// 66 types in the system generic.xaml alone -- and this project implements a
// fraction of them, so most of what is dropped is dropped because no element
// of that type can exist here to be styled. That is a fact about coverage, not
// a fault, and the difference has to be visible: a setter dropped for a
// property the type really has would be a wrong number waiting to happen,
// while one dropped for a property this parser has never heard of cannot
// change any measurement, because nothing reads it.
struct DefaultStyleReport {
    // Styles taken, by target type.
    std::vector<std::string> applied;
    // "Button.CornerRadius: no such property" -- one line per setter dropped.
    std::vector<std::string> dropped_setters;
    // Target types with no element type here to style.
    std::vector<std::string> unknown_types;
    // Styles held out deliberately, by name: see the hold list in the loader.
    std::vector<std::string> held;
};

// Loads the reconstructed default-style database into `registry`.
//
// `path` may be a file or a directory of them, and a directory that is not
// there loads nothing and is not an error -- the same rule the theme-resource
// database follows, and for the same reason: the database is generated rather
// than committed, and a bare checkout has to run.
//
// Returns how many built-in styles were registered. The styles are read
// through the same setter parser markup goes through, so a setter that would
// fail in a page fails here in the same words -- there is no second grammar
// for the framework's own styles to be wrong in.
int LoadDefaultStyles(DefaultStyleRegistry& registry, const std::string& path,
                      DefaultStyleReport* report = nullptr);

}  // namespace openxaml

#endif  // OPENXAML_MARKUP_H
