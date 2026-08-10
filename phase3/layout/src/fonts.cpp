#include "fonts.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
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

// Keys are decimal codepoints. Rejecting anything else keeps a mangled file
// from loading as a font with a few odd characters.
char32_t Codepoint(const std::string& text, const std::string& where) {
    size_t consumed = 0;
    const unsigned long value = std::stoul(text, &consumed);
    if (consumed != text.size()) throw JsonError(where + ": \"" + text + "\" is not a codepoint");
    return static_cast<char32_t>(value);
}

// "left,right", the two decimal codepoints the adjustment sits between. One
// key rather than nested objects so the block sorts and diffs the way the
// advances do.
std::map<std::pair<char32_t, char32_t>, double> ParseKerning(const JsonValue& kerning,
                                                            const std::string& where,
                                                            const std::string& what) {
    if (kerning.kind != JsonValue::Kind::Object)
        throw JsonError(where + ": \"" + what + "\" is not an object");
    std::map<std::pair<char32_t, char32_t>, double> pairs;
    for (const auto& entry : kerning.object) {
        if (entry.second.kind != JsonValue::Kind::Number)
            throw JsonError(where + ": the kerning for " + entry.first + " is not a number");
        const size_t comma = entry.first.find(',');
        if (comma == std::string::npos)
            throw JsonError(where + ": \"" + entry.first +
                            "\" is not a codepoint pair; expected \"left,right\"");
        const char32_t left = Codepoint(entry.first.substr(0, comma), where);
        const char32_t right = Codepoint(entry.first.substr(comma + 1), where);
        pairs[{left, right}] = entry.second.number;
    }
    return pairs;
}

}  // namespace

FontMetrics ParseFontMetrics(const std::string& json, const std::string& where) {
    const JsonValue document = ParseJson(json);

    FontMetrics metrics;
    metrics.units_per_em = Number(document, "units_per_em", where);
    if (metrics.units_per_em <= 0.0) throw JsonError(where + ": units_per_em must be positive");

    // Required, not defaulted. A metrics file whose origin has to be guessed at
    // is exactly the file that must not load: the derived one carries two
    // numbers and the harvested one carries a font, and they are told apart
    // nowhere else.
    const JsonValue& provenance = document.At("provenance");
    if (provenance.kind != JsonValue::Kind::String)
        throw JsonError(where + ": \"provenance\" is not a string");
    if (provenance.string == "harvested") {
        metrics.provenance = FontProvenance::Harvested;
    } else if (provenance.string == "derived") {
        metrics.provenance = FontProvenance::Derived;
    } else {
        throw JsonError(where + ": \"" + provenance.string +
                        "\" is not a provenance; expected \"harvested\" or \"derived\"");
    }

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
        metrics.advances[Codepoint(entry.first, where)] = entry.second.number;
    }
    if (metrics.advances.empty()) throw JsonError(where + ": the metrics have no advances");

    if (document.Has("system_fallbacks")) {
        const JsonValue& fallbacks = document.At("system_fallbacks");
        if (fallbacks.kind != JsonValue::Kind::Object)
            throw JsonError(where + ": \"system_fallbacks\" is not an object");
        for (const auto& entry : fallbacks.object) {
            if (entry.second.kind != JsonValue::Kind::Object)
                throw JsonError(where + ": the system fallback for " + entry.first +
                                " is not an object");
            const JsonValue& family = entry.second.At("family");
            if (family.kind != JsonValue::Kind::String || family.string.empty())
                throw JsonError(where + ": the system fallback for " + entry.first +
                                " has no family");
            FallbackGlyphMetrics fallback;
            fallback.family = family.string;
            fallback.scale = Number(entry.second, "scale", where);
            fallback.units_per_em = Number(entry.second, "units_per_em", where);
            fallback.advance = Number(entry.second, "advance", where);
            if (fallback.scale <= 0.0 || fallback.units_per_em <= 0.0 ||
                fallback.advance < 0.0) {
                throw JsonError(where + ": the system fallback for " + entry.first +
                                " has invalid metrics");
            }
            metrics.system_fallbacks[Codepoint(entry.first, where)] =
                std::move(fallback);
        }
    }

    // Two spellings, and which one a file uses says where its numbers came
    // from. A harvest writes "font_kerning", the font's own two tables kept
    // apart; a derived file writes "kerning", the pairs the recordings
    // witnessed. A harvest claiming the second is the shape of a run that
    // predates the L4-kern series and must not load quietly.
    if (document.Has("kerning")) {
        if (metrics.provenance != FontProvenance::Derived) {
            throw JsonError(
                where + ": a harvested file spells the font's pair table \"font_kerning\"; "
                "\"kerning\" is what a derived file states the recordings witnessed, and "
                "the two are not the same claim");
        }
        metrics.kerning = ParseKerning(document.At("kerning"), where, "kerning");
    }
    // The font's own pairs, which the L4-kern recordings show the runtime does
    // apply -- measured on its own, every pair moved by exactly what the font
    // says.
    //
    // The two tables are read in this order for two reasons the recordings
    // settle. GPOS wins on value where both carry a pair: Segoe UI's disagree
    // about Te, Ta, To, Wa and Ya (-200/-211, -230/-217, -200/-211, -80/-76,
    // -180/-199) and the runtime measured the GPOS value in all five. And GPOS
    // wins on reach: only its pairs move a run away from the front, which is
    // rule 5 in text.cpp and is why the subset is kept.
    if (document.Has("font_kerning")) {
        const JsonValue& tables = document.At("font_kerning");
        if (tables.kind != JsonValue::Kind::Object)
            throw JsonError(where + ": \"font_kerning\" is not an object");
        for (const char* table : {"kern", "gpos"}) {
            if (!tables.Has(table)) continue;
            const bool anywhere = std::string(table) == "gpos";
            for (const auto& entry : ParseKerning(tables.At(table), where, table)) {
                metrics.kerning[entry.first] = entry.second;
                if (anywhere) metrics.kerns_anywhere.insert(entry.first);
            }
        }
    }
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
    std::map<std::string, std::string> claimed;  // family -> the file that claimed it
    for (const fs::path& file : files) {
        std::ifstream in(file, std::ios::binary);
        std::ostringstream buffer;
        buffer << in.rdbuf();

        const std::string where = file.filename().string();
        const JsonValue document = ParseJson(buffer.str());
        const JsonValue& family = document.At("family");
        if (family.kind != JsonValue::Kind::String)
            throw JsonError(where + ": \"family\" is not a string");

        // Two files for one family used to resolve by sort order, which is how
        // a derived file dropped next to a harvested one silently wins or
        // silently loses. Neither answer is defensible, so say so instead.
        const auto previous = claimed.find(family.string);
        if (previous != claimed.end()) {
            throw JsonError(where + ": \"" + family.string + "\" is already claimed by " +
                            previous->second + "; a family has one set of metrics");
        }
        claimed.emplace(family.string, where);

        library.Add(family.string, ParseFontMetrics(buffer.str(), where));
        ++loaded;
    }
    return loaded;
}

}  // namespace openxaml
