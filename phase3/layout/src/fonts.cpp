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
                                                            const std::string& where) {
    if (kerning.kind != JsonValue::Kind::Object)
        throw JsonError(where + ": \"kerning\" is not an object");
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

    // Kerning is a *derived* metric here, and a harvested file claiming it is
    // an error rather than a bonus. The recorded runs settle it: the runner's
    // Segoe UI kerns "ox", "ro", "ve" and "rm", and the pangram containing the
    // first three measures the raw sum of its advances at all three sizes, so
    // the runtime did not apply them. It did apply "Te". Which pairs survive is
    // therefore a question about the runtime and not about the font, and only
    // the measurements can answer it -- see phase3/xaml-db/fonts/README.md.
    //
    // The font's own tables are still harvested, under "font_kerning", as
    // evidence. Nothing reads them here, which is the whole point of the
    // separate key.
    if (document.Has("kerning")) {
        if (metrics.provenance != FontProvenance::Derived) {
            throw JsonError(
                where + ": a harvested file may not carry \"kerning\"; which pairs the "
                "runtime applies is solved from the recorded measurements, not read "
                "out of the font, and the font's own table belongs in \"font_kerning\"");
        }
        metrics.kerning = ParseKerning(document.At("kerning"), where);
    }
    return metrics;
}

int LoadImpliedKerning(FontLibrary& library, const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw JsonError("cannot read implied kerning from " + path);
    std::ostringstream buffer;
    buffer << in.rdbuf();

    const std::string where = fs::path(path).filename().string();
    const JsonValue document = ParseJson(buffer.str());

    // Derived only. A harvested file has the font's table, which is the one
    // thing this must not install -- that is the mistake the whole split
    // exists to prevent.
    const JsonValue& provenance = document.At("provenance");
    if (provenance.kind != JsonValue::Kind::String || provenance.string != "derived") {
        throw JsonError(where + ": implied kerning comes from a derived file; this one's "
                                "provenance is not \"derived\"");
    }
    const JsonValue& family = document.At("family");
    if (family.kind != JsonValue::Kind::String)
        throw JsonError(where + ": \"family\" is not a string");

    const std::map<std::pair<char32_t, char32_t>, double> pairs =
        document.Has("kerning") ? ParseKerning(document.At("kerning"), where)
                                : std::map<std::pair<char32_t, char32_t>, double>();

    // Naming a family that was not loaded means the two inputs are describing
    // different runs, which is worth stopping for: silently applying nothing
    // would measure every kerned case wrongly and blame the layout.
    if (!library.SetKerning(family.string, pairs)) {
        throw JsonError(where + ": no metrics are loaded for \"" + family.string +
                        "\", so there is nothing for its kerning to apply to");
    }
    return static_cast<int>(pairs.size());
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
