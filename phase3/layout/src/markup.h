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
#include "resw_strings.h"

namespace openxaml {

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

}  // namespace openxaml

#endif  // OPENXAML_MARKUP_H
