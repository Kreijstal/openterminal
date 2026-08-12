// Recorded glyph outlines, read back for painting.
//
// The writing side is phase3/scripts/harvest_glyph_outlines.py: one JSON
// document per family, design units, cubics exactly as the recording
// ID2D1SimplifiedGeometrySink delivered them. This is the reading side, and it
// enforces the same structural rules the harvester's check_shapes() does --
// a document that violates one loads as a named error, never as a family that
// paints part of a string.
//
// Plain C++17, no Windows: the Linux corpus harness paints from these outlines
// through the same code the GDI harness does. See
// phase3/xaml-db/glyph-outlines/README.md for the whole design.

#ifndef OPENXAML_RENDER_GLYPH_OUTLINES_H
#define OPENXAML_RENDER_GLYPH_OUTLINES_H

#include <map>
#include <string>
#include <vector>

namespace openxaml {
namespace render {

// Which winding rule fills the glyph. Recorded, not assumed: it is the
// difference between an `o` with a counter and a solid blob.
enum class OutlineFillMode { Winding, Alternate };

// One segment with the kind the recording sink delivered: a line to
// (x[0], y[0]), or a cubic through two control points ending at (x[2], y[2]).
// Design units; y grows downward, so ink above the baseline is negative.
struct OutlineSegment {
    bool cubic = false;
    double x[3] = {0.0, 0.0, 0.0};
    double y[3] = {0.0, 0.0, 0.0};
};

struct OutlineContour {
    double start_x = 0.0;
    double start_y = 0.0;
    std::vector<OutlineSegment> segments;
};

struct GlyphOutline {
    int glyph_index = 0;
    // The design advance, recorded as an independent reading of the number the
    // metrics harvest carries. It is a cross-check, never a position: every
    // painted position comes from the display list's own advances.
    double advance = 0.0;
    OutlineFillMode fill_mode = OutlineFillMode::Winding;
    // Empty for a glyph with no ink -- the space really does record zero
    // contours, and that is a blank, not a gap.
    std::vector<OutlineContour> contours;
};

struct GlyphOutlineFamily {
    std::string family;
    double units_per_em = 0.0;
    std::map<char32_t, GlyphOutline> outlines;
};

// Parses one recorded-outline document. Throws JsonError naming the file and
// the rule for anything the harvester would have refused to write.
GlyphOutlineFamily ParseGlyphOutlines(const std::string& json, const std::string& where);

class GlyphOutlineLibrary {
public:
    void Add(GlyphOutlineFamily family);

    // The first entry of a comma-separated FontFamily value that has recorded
    // outlines, or null when none has. Matching is ASCII case-insensitive,
    // because that is how DirectWrite resolves family names, and the corpus
    // writes values like "Segoe UI, Segoe Fluent Icons, Segoe MDL2 Assets".
    const GlyphOutlineFamily* Resolve(const std::string& family_list) const;

    bool empty() const { return families_.empty(); }
    std::vector<std::string> families() const;

    // Process-wide, the way FontLibrary::Default() is: the GDI backend reaches
    // it from inside DrawRuns and is not handed one.
    static GlyphOutlineLibrary& Default();

private:
    // Keyed by the ASCII-lowercased family name.
    std::map<std::string, GlyphOutlineFamily> families_;
};

// Adds every *.json under `directory` to `library`, keyed by the family each
// document declares. Returns how many loaded. A missing directory loads
// nothing and is not an error -- a run without recorded outlines refuses text
// the way it always has; a malformed document is a JsonError naming the file.
int LoadGlyphOutlineDirectory(GlyphOutlineLibrary& library, const std::string& directory);

}  // namespace render
}  // namespace openxaml

#endif  // OPENXAML_RENDER_GLYPH_OUTLINES_H
