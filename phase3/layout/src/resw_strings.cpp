#include "resw_strings.h"

#include <fstream>
#include <sstream>

#include "json.h"

namespace openxaml {

void StringTable::Add(const std::string& uid, const std::string& property,
                      const std::string& value) {
    if (uid.empty()) throw JsonError("a string table entry has an empty uid");
    if (property.empty())
        throw JsonError("the uid '" + uid + "' has an entry with an empty property name");
    Properties& properties = entries_[uid];
    if (properties.count(property)) {
        throw JsonError("the uid '" + uid + "' sets '" + property +
                        "' twice; a resw key cannot be declared twice");
    }
    properties.emplace(property, value);
}

const StringTable::Properties* StringTable::Find(const std::string& uid) const {
    const auto found = entries_.find(uid);
    return found == entries_.end() ? nullptr : &found->second;
}

const StringTable& NoStrings() {
    static const StringTable kEmpty;
    return kEmpty;
}

StringTable LoadStringTable(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw JsonError("cannot read the string table " + path);
    std::ostringstream buffer;
    buffer << in.rdbuf();

    const JsonValue document = ParseJson(buffer.str());
    // The distiller writes both halves of what it found: "strings" is the
    // uid-keyed table, and the keys with no property half live elsewhere in the
    // file because they are code-side lookups and cannot be applied to markup.
    const JsonValue& strings = document.At("strings");
    if (strings.kind != JsonValue::Kind::Object)
        throw JsonError(path + ": \"strings\" is not an object");

    StringTable table;
    for (const auto& [uid, properties] : strings.object) {
        if (properties.kind != JsonValue::Kind::Object)
            throw JsonError(path + ": the entry for '" + uid + "' is not an object");
        for (const auto& [property, value] : properties.object) {
            if (value.kind != JsonValue::Kind::String) {
                throw JsonError(path + ": " + uid + "." + property +
                                " is not a string");
            }
            table.Add(uid, property, value.string);
        }
    }
    return table;
}

}  // namespace openxaml
