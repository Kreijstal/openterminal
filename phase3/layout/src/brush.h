// Brushes, to the extent that layout has an opinion about them: none.
//
// A Background or a Fill never moves anything. It is here so that markup
// carrying one can be realised at all -- Terminal's pages set Background on
// almost every Grid -- and so that a brush that is *not* understood is still
// refused by name. Accepting any string as a colour would mean a typo in a
// page loaded silently and the case passed for the wrong reason.

#ifndef OPENXAML_BRUSH_H
#define OPENXAML_BRUSH_H

#include <string>

namespace openxaml {

// The full runtime name of a brush type, as the oracle spells it. Throws
// MarkupError for a type that is not implemented.
std::string FullBrushTypeName(const std::string& short_name);

// Validates the shorthand a brush-typed attribute takes -- "#ff0000",
// "#80000000", "Transparent". Throws MarkupError for anything else. The parsed
// colour is discarded: nothing downstream can use it.
void ValidateColor(const std::string& text, const std::string& where);

}  // namespace openxaml

#endif  // OPENXAML_BRUSH_H
