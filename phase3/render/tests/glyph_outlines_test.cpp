// The recorded-outline loader refuses what the harvester would have refused.
//
// phase3/scripts/harvest_glyph_outlines.py holds the writing side to a set of
// structural rules (check_shapes). This is the reading side of the same
// contract: a document that violates a rule loads as a named error, never as a
// family that paints part of what it claims. Everything here is synthesised,
// so the test runs anywhere the render core builds and never skips.

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "glyph_outlines.h"
#include "json.h"

using namespace openxaml;
using namespace openxaml::render;

namespace {

int failures = 0;

void Check(bool condition, const char* expression, int line) {
    if (condition) return;
    std::cerr << "glyph_outlines_test.cpp:" << line << ": CHECK failed: " << expression
              << "\n";
    ++failures;
}

#define CHECK(expression) Check(static_cast<bool>(expression), #expression, __LINE__)

// A minimal, valid document: one line-only triangle glyph and one blank glyph,
// in the exact shape harvest_glyph_outlines.py writes.
std::string ValidDocument() {
    return R"({
 "schema_version": 1,
 "boundary": "IDWriteFontFace::GetGlyphRunOutline -> ID2D1SimplifiedGeometrySink",
 "family": "Test Outline Font",
 "provenance": "harvested",
 "units_per_em": 1000,
 "coordinates": "design units; emSize == units_per_em, no transform",
 "outlines": {
  "32": {"glyph_index": 3, "advance": 500, "fill_mode": "winding", "contours": []},
  "65": {"glyph_index": 36, "advance": 600, "fill_mode": "alternate", "contours": [
   {"start": [0, 0], "segments": [["l", 600, 0], ["c", 600, -700, 300, -700, 0, -700]]}
  ]}
 }
})";
}

std::string RejectionOf(const std::string& document) {
    try {
        ParseGlyphOutlines(document, "test.json");
    } catch (const JsonError& error) {
        return error.what();
    }
    return std::string();
}

void AValidDocumentLoadsCompletely() {
    const GlyphOutlineFamily family = ParseGlyphOutlines(ValidDocument(), "test.json");
    CHECK(family.family == "Test Outline Font");
    CHECK(family.units_per_em == 1000.0);
    CHECK(family.outlines.size() == 2);
    const auto space = family.outlines.find(U' ');
    CHECK(space != family.outlines.end());
    CHECK(space->second.contours.empty());
    CHECK(space->second.advance == 500.0);
    const auto letter = family.outlines.find(U'A');
    CHECK(letter != family.outlines.end());
    CHECK(letter->second.fill_mode == OutlineFillMode::Alternate);
    CHECK(letter->second.contours.size() == 1);
    const OutlineContour& contour = letter->second.contours[0];
    CHECK(contour.start_x == 0.0 && contour.start_y == 0.0);
    CHECK(contour.segments.size() == 2);
    CHECK(!contour.segments[0].cubic);
    CHECK(contour.segments[0].x[0] == 600.0 && contour.segments[0].y[0] == 0.0);
    CHECK(contour.segments[1].cubic);
    CHECK(contour.segments[1].x[2] == 0.0 && contour.segments[1].y[2] == -700.0);
}

void EveryStructuralRuleIsARefusalByName() {
    std::string wrong_schema = ValidDocument();
    wrong_schema.replace(wrong_schema.find("\"schema_version\": 1"),
                         std::string("\"schema_version\": 1").size(),
                         "\"schema_version\": 2");
    CHECK(RejectionOf(wrong_schema).find("schema_version") != std::string::npos);

    std::string notdef = ValidDocument();
    notdef.replace(notdef.find("\"glyph_index\": 36"),
                   std::string("\"glyph_index\": 36").size(), "\"glyph_index\": 0");
    CHECK(RejectionOf(notdef).find(".notdef") != std::string::npos);

    std::string bad_fill = ValidDocument();
    bad_fill.replace(bad_fill.find("\"alternate\""), std::string("\"alternate\"").size(),
                     "\"nonzero\"");
    CHECK(RejectionOf(bad_fill).find("fill_mode") != std::string::npos);

    std::string short_cubic = ValidDocument();
    short_cubic.replace(short_cubic.find("[\"c\", 600, -700, 300, -700, 0, -700]"),
                        std::string("[\"c\", 600, -700, 300, -700, 0, -700]").size(),
                        "[\"c\", 600, -700]");
    CHECK(RejectionOf(short_cubic).find("coordinate") != std::string::npos);

    std::string bad_kind = ValidDocument();
    bad_kind.replace(bad_kind.find("[\"l\", 600, 0]"),
                     std::string("[\"l\", 600, 0]").size(), "[\"q\", 600, 0]");
    CHECK(!RejectionOf(bad_kind).empty());

    std::string derived = ValidDocument();
    derived.replace(derived.find("\"harvested\""), std::string("\"harvested\"").size(),
                    "\"derived\"");
    CHECK(RejectionOf(derived).find("provenance") != std::string::npos);

    std::string no_family = ValidDocument();
    no_family.replace(no_family.find("\"Test Outline Font\""),
                      std::string("\"Test Outline Font\"").size(), "\"\"");
    CHECK(RejectionOf(no_family).find("family") != std::string::npos);
}

void TheLibraryResolvesTheWayFontFamilyListsAreWritten() {
    GlyphOutlineLibrary library;
    CHECK(library.empty());
    CHECK(library.Resolve("Test Outline Font") == nullptr);
    library.Add(ParseGlyphOutlines(ValidDocument(), "test.json"));
    CHECK(!library.empty());
    CHECK(library.Resolve("Test Outline Font") != nullptr);
    // The comma list resolves to its first entry that has outlines, exactly as
    // FamilyCandidates walks a FontFamily value; matching is ASCII
    // case-insensitive because DirectWrite family lookup is.
    CHECK(library.Resolve("Nonexistent, Test Outline Font") != nullptr);
    CHECK(library.Resolve("test outline font") != nullptr);
    CHECK(library.Resolve(" Test Outline Font ") != nullptr);
    CHECK(library.Resolve("Nonexistent") == nullptr);
    CHECK(library.Resolve("") == nullptr);
}

void ADirectoryLoadsEveryDocumentAndAMissingOneLoadsNothing() {
    namespace fs = std::filesystem;
    const fs::path root =
        fs::temp_directory_path() / "openxaml-glyph-outlines-test";
    fs::remove_all(root);
    fs::create_directories(root);
    std::ofstream(root / "test-outline-font.json") << ValidDocument();

    GlyphOutlineLibrary library;
    CHECK(LoadGlyphOutlineDirectory(library, root.string()) == 1);
    CHECK(library.Resolve("Test Outline Font") != nullptr);

    GlyphOutlineLibrary empty_library;
    CHECK(LoadGlyphOutlineDirectory(empty_library,
                                    (root / "no-such-directory").string()) == 0);
    CHECK(empty_library.empty());

    // A malformed document in the directory is an error naming the file, not a
    // directory that quietly loaded the rest.
    std::ofstream(root / "broken.json") << "{\"schema_version\": 7}";
    GlyphOutlineLibrary broken_library;
    bool threw = false;
    try {
        LoadGlyphOutlineDirectory(broken_library, root.string());
    } catch (const JsonError& error) {
        threw = std::string(error.what()).find("broken.json") != std::string::npos;
    }
    CHECK(threw);
    fs::remove_all(root);
}

}  // namespace

int main() {
    AValidDocumentLoadsCompletely();
    EveryStructuralRuleIsARefusalByName();
    TheLibraryResolvesTheWayFontFamilyListsAreWritten();
    ADirectoryLoadsEveryDocumentAndAMissingOneLoadsNothing();

    if (failures != 0) {
        std::cerr << failures << " glyph outline loader check(s) failed\n";
        return 1;
    }
    std::cout << "glyph outline loader checks passed\n";
    return 0;
}
