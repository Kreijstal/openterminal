#include "glyph_outlines.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

#include "json.h"

namespace fs = std::filesystem;

namespace openxaml {
namespace render {
namespace {

// The schema this reader knows. Kept equal to SCHEMA_VERSION in
// phase3/scripts/harvest_glyph_outlines.py; a document from a different
// harvester is refused by name rather than half-read.
constexpr int kSchemaVersion = 1;

double Number(const JsonValue& parent, const std::string& key, const std::string& where) {
    const JsonValue& value = parent.At(key);
    if (value.kind != JsonValue::Kind::Number)
        throw JsonError(where + ": \"" + key + "\" is not a number");
    if (!std::isfinite(value.number))
        throw JsonError(where + ": \"" + key + "\" is not finite");
    return value.number;
}

double FiniteNumber(const JsonValue& value, const std::string& where) {
    if (value.kind != JsonValue::Kind::Number || !std::isfinite(value.number))
        throw JsonError(where + ": a coordinate is not a finite number");
    return value.number;
}

// Keys are decimal codepoints, exactly as the metrics files spell theirs.
char32_t Codepoint(const std::string& text, const std::string& where) {
    size_t consumed = 0;
    const unsigned long value = std::stoul(text, &consumed);
    if (consumed != text.size())
        throw JsonError(where + ": \"" + text + "\" is not a codepoint");
    return static_cast<char32_t>(value);
}

std::string LowerAscii(const std::string& text) {
    std::string out = text;
    for (char& c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

OutlineContour ParseContour(const JsonValue& value, const std::string& where) {
    if (value.kind != JsonValue::Kind::Object)
        throw JsonError(where + ": a contour is not an object");
    const JsonValue& start = value.At("start");
    if (start.kind != JsonValue::Kind::Array || start.array.size() != 2)
        throw JsonError(where + ": a contour start is not a point");
    OutlineContour contour;
    contour.start_x = FiniteNumber(start.array[0], where);
    contour.start_y = FiniteNumber(start.array[1], where);
    const JsonValue& segments = value.At("segments");
    if (segments.kind != JsonValue::Kind::Array)
        throw JsonError(where + ": a contour's segments are not an array");
    for (const JsonValue& entry : segments.array) {
        if (entry.kind != JsonValue::Kind::Array || entry.array.empty() ||
            entry.array[0].kind != JsonValue::Kind::String) {
            throw JsonError(where + ": a segment does not name its kind");
        }
        const std::string& kind = entry.array[0].string;
        OutlineSegment segment;
        if (kind == "l") {
            segment.cubic = false;
        } else if (kind == "c") {
            segment.cubic = true;
        } else {
            throw JsonError(where + ": segment kind \"" + kind +
                            "\" is neither a line nor a cubic");
        }
        const size_t coordinates = segment.cubic ? 6 : 2;
        if (entry.array.size() != coordinates + 1) {
            throw JsonError(where + ": a \"" + kind + "\" segment carries " +
                            std::to_string(entry.array.size() - 1) +
                            " coordinate(s), expected " + std::to_string(coordinates));
        }
        for (size_t point = 0; point * 2 + 1 < coordinates + 1; ++point) {
            segment.x[point] = FiniteNumber(entry.array[1 + point * 2], where);
            segment.y[point] = FiniteNumber(entry.array[2 + point * 2], where);
        }
        contour.segments.push_back(segment);
    }
    return contour;
}

}  // namespace

GlyphOutlineFamily ParseGlyphOutlines(const std::string& json, const std::string& where) {
    const JsonValue document = ParseJson(json);

    const JsonValue& schema = document.At("schema_version");
    if (schema.kind != JsonValue::Kind::Number ||
        schema.number != static_cast<double>(kSchemaVersion)) {
        std::ostringstream message;
        message << where << ": schema_version is not " << kSchemaVersion
                << "; this reader and that harvester are different contracts";
        throw JsonError(message.str());
    }

    // Required for the same reason ParseFontMetrics requires it: a document
    // whose origin has to be guessed at is exactly the one that must not load.
    // Only "harvested" exists for outlines -- nothing derives a shape.
    const JsonValue& provenance = document.At("provenance");
    if (provenance.kind != JsonValue::Kind::String || provenance.string != "harvested")
        throw JsonError(where + ": \"provenance\" is not \"harvested\"; recorded "
                        "outlines have exactly one origin and this document does not "
                        "claim it");

    GlyphOutlineFamily family;
    const JsonValue& name = document.At("family");
    if (name.kind != JsonValue::Kind::String || name.string.empty())
        throw JsonError(where + ": the document names no family");
    family.family = name.string;

    family.units_per_em = Number(document, "units_per_em", where);
    if (family.units_per_em <= 0.0)
        throw JsonError(where + ": units_per_em must be positive");

    const JsonValue& outlines = document.At("outlines");
    if (outlines.kind != JsonValue::Kind::Object)
        throw JsonError(where + ": \"outlines\" is not an object");
    for (const auto& entry : outlines.object) {
        const char32_t codepoint = Codepoint(entry.first, where);
        std::ostringstream spot;
        spot << where << " U+" << std::hex << std::uppercase
             << static_cast<unsigned long>(codepoint);
        const std::string here = spot.str();
        if (entry.second.kind != JsonValue::Kind::Object)
            throw JsonError(here + ": the outline is not an object");

        GlyphOutline glyph;
        glyph.glyph_index = static_cast<int>(Number(entry.second, "glyph_index", here));
        if (glyph.glyph_index == 0)
            throw JsonError(here + ": glyph index 0 is .notdef; the font does not have "
                            "this glyph and a box is not its shape");
        glyph.advance = Number(entry.second, "advance", here);
        if (glyph.advance < 0.0)
            throw JsonError(here + ": the design advance is negative");

        const JsonValue& fill = entry.second.At("fill_mode");
        if (fill.kind == JsonValue::Kind::String && fill.string == "winding") {
            glyph.fill_mode = OutlineFillMode::Winding;
        } else if (fill.kind == JsonValue::Kind::String && fill.string == "alternate") {
            glyph.fill_mode = OutlineFillMode::Alternate;
        } else {
            throw JsonError(here + ": fill_mode is neither \"winding\" nor "
                            "\"alternate\"; which rule fills the glyph decides whether "
                            "a counter is a hole or ink");
        }

        const JsonValue& contours = entry.second.At("contours");
        if (contours.kind != JsonValue::Kind::Array)
            throw JsonError(here + ": \"contours\" is not an array");
        for (const JsonValue& contour : contours.array)
            glyph.contours.push_back(ParseContour(contour, here));

        family.outlines[codepoint] = std::move(glyph);
    }
    return family;
}

void GlyphOutlineLibrary::Add(GlyphOutlineFamily family) {
    families_[LowerAscii(family.family)] = std::move(family);
}

const GlyphOutlineFamily* GlyphOutlineLibrary::Resolve(
    const std::string& family_list) const {
    std::size_t begin = 0;
    while (begin <= family_list.size()) {
        const std::size_t comma = family_list.find(',', begin);
        std::string candidate = family_list.substr(
            begin, comma == std::string::npos ? std::string::npos : comma - begin);
        const std::size_t first = candidate.find_first_not_of(" \t");
        const std::size_t last = candidate.find_last_not_of(" \t");
        if (first != std::string::npos) {
            candidate = candidate.substr(first, last - first + 1);
            const auto found = families_.find(LowerAscii(candidate));
            if (found != families_.end()) return &found->second;
        }
        if (comma == std::string::npos) break;
        begin = comma + 1;
    }
    return nullptr;
}

std::vector<std::string> GlyphOutlineLibrary::families() const {
    std::vector<std::string> out;
    for (const auto& entry : families_) out.push_back(entry.second.family);
    return out;
}

GlyphOutlineLibrary& GlyphOutlineLibrary::Default() {
    static GlyphOutlineLibrary library;
    return library;
}

int LoadGlyphOutlineDirectory(GlyphOutlineLibrary& library, const std::string& directory) {
    std::error_code failure;
    if (!fs::is_directory(directory, failure)) return 0;

    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(directory)) {
        if (entry.path().extension() == ".json") files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());

    int loaded = 0;
    for (const fs::path& file : files) {
        std::ifstream input(file, std::ios::binary);
        std::ostringstream buffer;
        buffer << input.rdbuf();
        library.Add(ParseGlyphOutlines(buffer.str(), file.filename().string()));
        ++loaded;
    }
    return loaded;
}

}  // namespace render
}  // namespace openxaml
