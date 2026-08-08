// What a stable XBF index means.
//
// XBF does not spell type and property names in the file when the platform
// already knows them: it writes a *stable* index instead -- a number the
// platform promises never to reuse, so a compiled page keeps working across
// releases. That saves the file a string table and costs the reader a table of
// its own, which is this.
//
// Provenance, and why this is hand-written rather than generated. The numbers
// are published, MIT-licensed, in microsoft/microsoft-ui-xaml at 188f602b as
// dxaml/xcp/core/Parser/StableXbfIndexes.g.h -- about 1800 types and 2900
// properties. Copying all of it in would be committing a downloaded generated
// file under another name, and almost none of it names something this runtime
// can build. So the table below holds exactly the indexes the corpus and
// Terminal's own pages actually use, transcribed one at a time and checked
// against genxbf's real output by the equivalence gate. An index that is not
// here is refused *by number*, which is a fact ("stable type index 515 is not
// implemented") rather than a guess.
//
// Names are given as markup spells them, not as the enum spells them:
// Grid_Column is written `Grid.Column` because that is the attribute the text
// path parses, and the whole point is that the two paths agree.

#ifndef OPENXAML_XBF_NAMES_H
#define OPENXAML_XBF_NAMES_H

#include <cstdint>
#include <string>

namespace openxaml {
namespace xbf {

// What kind of member an index names, which decides how it is written back out.
enum class MemberKind {
    // An ordinary property: `Width="20"`.
    Attribute,
    // An attached property, whose name carries its owner: `Grid.Column="1"`.
    Attached,
    // The property an element's children go into. Its values are written as
    // child elements with no wrapper, which is what the markup they came from
    // did: `<Grid><Border/></Grid>`, not `<Grid><Panel.Children>...`.
    Content,
    // A property whose object values are written as a property element,
    // `<Grid.ColumnDefinitions>`. The prefix is the element's own type name,
    // not the declaring type's, because that is what XAML property element
    // syntax uses and what the text path parses.
    Element,
};

struct MemberName {
    // The name as markup spells it, e.g. "Width", "Grid.Column",
    // "ColumnDefinitions".
    const char* name;
    MemberKind kind;
};

// Null when the index is not one this runtime implements.
const char* TypeName(std::uint16_t stable_index);

// Null when the index is not one this runtime implements.
const MemberName* PropertyName(std::uint16_t stable_index);

// The name of a value in the enumeration `stable_type_index`, or empty when
// either the enumeration or the value is not implemented. `known_type` says
// which of those two it was, so the error can name the right one.
std::string EnumValueName(std::uint16_t stable_type_index, std::uint32_t value, bool* known_type);

}  // namespace xbf
}  // namespace openxaml

#endif  // OPENXAML_XBF_NAMES_H
