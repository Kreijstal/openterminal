#include "fonts.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#include "json.h"

namespace fs = std::filesystem;

namespace openxaml {
namespace {

double Number(const JsonValue& parent, const std::string& key, const std::string& where) {
    const JsonValue& value = parent.At(key);
    if (value.kind != JsonValue::Kind::Number)
        throw JsonError(where + ": \"" + key + "\" is not a number");
    return value.number;
}

}  // namespace

FontMetrics ParseFontMetrics(const std::string& json, const std::string& where) {
    const JsonValue document = ParseJson(json);

    FontMetrics metrics;
    metrics.units_per_em = Number(document, "units_per_em", where);
    if (metrics.units_per_em <= 0.0) throw JsonError(where + ": units_per_em must be positive");

    const JsonValue& hhea = document.At("hhea");
    metrics.ascender = Number(hhea, "ascender", where);
    metrics.descender = Number(hhea, "descender", where);
    metrics.line_gap = Number(hhea, "line_gap", where);

    const JsonValue& advances = document.At("advances");
    if (advances.kind != JsonValue::Kind::Object)
        throw JsonError(where + ": \"advances\" is not an object");
    for (const auto& entry : advances.object) {
        if (entry.second.kind != JsonValue::Kind::Number)
            throw JsonError(where + ": the advance for " + entry.first + " is not a number");
        // Keys are decimal codepoints. Rejecting anything else keeps a
        // mangled file from loading as a font with a few odd characters.
        size_t consumed = 0;
        const unsigned long codepoint = std::stoul(entry.first, &consumed);
        if (consumed != entry.first.size())
            throw JsonError(where + ": \"" + entry.first + "\" is not a codepoint");
        metrics.advances[static_cast<char32_t>(codepoint)] = entry.second.number;
    }
    if (metrics.advances.empty()) throw JsonError(where + ": the metrics have no advances");
    return metrics;
}

int LoadFontDirectory(FontLibrary& library, const std::string& directory) {
    std::error_code failure;
    if (!fs::is_directory(directory, failure)) return 0;

    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(directory)) {
        if (entry.path().extension() == ".json") files.push_back(entry.path());
    }
    // Sorted, so that two fonts claiming the same family resolve the same way
    // on every machine rather than by directory order.
    std::sort(files.begin(), files.end());

    int loaded = 0;
    for (const fs::path& file : files) {
        std::ifstream in(file, std::ios::binary);
        std::ostringstream buffer;
        buffer << in.rdbuf();

        const std::string where = file.filename().string();
        const JsonValue document = ParseJson(buffer.str());
        const JsonValue& family = document.At("family");
        if (family.kind != JsonValue::Kind::String)
            throw JsonError(where + ": \"family\" is not a string");

        library.Add(family.string, ParseFontMetrics(buffer.str(), where));
        ++loaded;
    }
    return loaded;
}

}  // namespace openxaml
