// The table an x:Uid resolves against.
//
// A .resw file is a flat list of keys, and the key encodes both halves of what
// it sets: "SaveButton.Content" gives the SaveButton uid a Content, and
// "[using:Windows.UI.Xaml.Automation]AutomationProperties.Name" gives it an
// attached one. phase3/scripts/distil_resw_strings.py does that split against a
// pinned Terminal checkout and writes the result as JSON; this reads it back.
//
// Distilled rather than parsed here for the same reason the font metrics are:
// the checkout is research input, not repository content, so the table is
// generated and untracked, and the layout core takes it as an argument instead
// of knowing where Terminal lives.
//
// Empty is the normal state. No corpus case ships a table, because the oracle
// probe has no resource map either -- a uid it cannot find sets nothing, and
// that is the behaviour both sides have to agree on before anything harder can
// be asked.

#ifndef OPENXAML_RESW_STRINGS_H
#define OPENXAML_RESW_STRINGS_H

#include <map>
#include <string>

namespace openxaml {

// uid -> property -> value. The property is spelled as an attribute would
// spell it: "Text", "Content", "AutomationProperties.Name".
class StringTable {
public:
    using Properties = std::map<std::string, std::string>;

    void Add(const std::string& uid, const std::string& property, const std::string& value);

    // Null when the uid is in no table. Not an error: a uid with no entry is
    // exactly what every element in the corpus has, and what an element in a
    // real page has when its resources have not been loaded.
    const Properties* Find(const std::string& uid) const;

    bool empty() const { return entries_.empty(); }
    size_t size() const { return entries_.size(); }

private:
    std::map<std::string, Properties> entries_;
};

// The empty table, for the callers that have none. A reference so that a
// parser can hold one without every call site owning an object.
const StringTable& NoStrings();

// Reads the JSON the distiller writes. Throws for a file that is not there or
// not that shape, because a table asked for and silently absent would make
// every x:Uid quietly do nothing.
StringTable LoadStringTable(const std::string& path);

}  // namespace openxaml

#endif  // OPENXAML_RESW_STRINGS_H
