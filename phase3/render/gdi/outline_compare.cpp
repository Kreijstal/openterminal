// Are the recorded outlines what DirectWrite reads off the same file here?
//
// Containment cannot answer that: an outline painter that shipped the wrong
// typeface at the right advances would pass every containment check in the
// project. The glyph-outlines README names the honest form of the proof --
// the same glyphs through DirectWrite over the font file and through the
// recording, compared -- and this is that tool. build_render.py has already
// checked the file's SHA-256 against the one the recording carries, so
// everything below is over one file. The face comes straight from that file
// -- no family resolution, nothing to resolve wrongly -- and each glyph's
// index from the live cmap must equal the recorded one, so no comparison can
// silently be of two different glyphs.
//
// Two checks, because the recording and a rendering are different claims:
//
// 1. **The recording is the boundary's own answer.** Per recorded codepoint,
//    IDWriteFontFace::GetGlyphRunOutline runs here, into the same kind of
//    recording sink the harvest used, and the two outlines must be the same
//    curves: same fill rule, and every densely sampled point of each within
//    two design units -- a thousandth of the em -- of the other. Geometric,
//    not structural, because implementations segment one shape differently
//    (Wine's sink re-segments 80 of Cascadia's 95 recorded outlines while
//    describing identical curves); a wrong glyph is off by tens to hundreds
//    of units.
//
// 2. **An independent rasteriser agrees about the ink.** Each sample is
//    painted through DirectWrite's glyph-run analysis and through the
//    recorded-outline scanline filler, and the two ink masks are held to a
//    symmetric Hausdorff distance (Chebyshev) of three pixels. Three, not
//    one, and the number is a measurement, not a shrug: the recording is the
//    *unhinted* outline by construction, while every rasterisation
//    DirectWrite offers here applies the font's own grid-fitting -- Cascadia
//    Mono's grave accent carries hint instructions that move it 2-3 pixels at
//    24px, and asking the unhinted painting to land within one pixel of that
//    would measure the font's hinting, not the recording. What the mask check
//    still catches at three pixels is every systematic error: a flipped axis,
//    a wrong scale, a wrong typeface. What it cannot catch -- a subtly wrong
//    contour -- is exactly what check 1 catches at two design units.

#include <windows.h>

#include <d2d1.h>
#include <dwrite_2.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "display_list.h"
#include "glyph_outline_rasterizer.h"
#include "glyph_outlines.h"
#include "json.h"
#include "surface.h"

namespace fs = std::filesystem;
using namespace openxaml;
using namespace openxaml::render;

namespace {

template <class T>
class ComPtr {
public:
    ComPtr() = default;
    ~ComPtr() { Reset(); }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    T* get() const { return value_; }
    T** put() { Reset(); return &value_; }
    T* operator->() const { return value_; }
    explicit operator bool() const { return value_ != nullptr; }
    void Reset() {
        if (value_) value_->Release();
        value_ = nullptr;
    }
private:
    T* value_ = nullptr;
};

std::string EncodeUtf8(char32_t code) {
    std::string out;
    if (code < 0x80) {
        out.push_back(static_cast<char>(code));
    } else if (code < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (code >> 6)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else if (code < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (code >> 12)));
        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (code >> 18)));
        out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    }
    return out;
}

// The ink mask of a painting: every pixel that is not the backdrop. Any
// difference counts -- a faint antialiased fringe is ink.
std::vector<bool> InkMask(const Surface& surface) {
    const std::uint32_t backdrop = Pack(BackdropColor());
    std::vector<bool> mask(surface.pixels().size());
    for (std::size_t index = 0; index < mask.size(); ++index)
        mask[index] = surface.pixels()[index] != backdrop;
    return mask;
}

// The largest distance from an inked pixel of `from` to the nearest inked
// pixel of `to`, in Chebyshev metric. 0 when `from` has no ink. `worst_x` and
// `worst_y` receive one offending pixel, so a failure names where to look.
int DirectedHausdorff(const std::vector<bool>& from, const std::vector<bool>& to,
                      int width, int height, int give_up_at, int& worst_x,
                      int& worst_y) {
    int worst = 0;
    worst_x = -1;
    worst_y = -1;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (!from[static_cast<std::size_t>(y) * width + x]) continue;
            int nearest = give_up_at + 1;
            for (int radius = 0; radius <= give_up_at; ++radius) {
                bool found = false;
                for (int dy = -radius; dy <= radius && !found; ++dy) {
                    const int ty = y + dy;
                    if (ty < 0 || ty >= height) continue;
                    for (int dx = -radius; dx <= radius; ++dx) {
                        if (std::max(std::abs(dx), std::abs(dy)) != radius) continue;
                        const int tx = x + dx;
                        if (tx < 0 || tx >= width) continue;
                        if (to[static_cast<std::size_t>(ty) * width + tx]) {
                            found = true;
                            break;
                        }
                    }
                }
                if (found) {
                    nearest = radius;
                    break;
                }
            }
            if (nearest > worst) {
                worst = nearest;
                worst_x = x;
                worst_y = y;
            }
        }
    }
    return worst;
}

struct SampleResult {
    std::string id;
    int dwrite_pixels = 0;
    int outline_pixels = 0;
    int hausdorff = 0;
    bool passed = false;
    std::string failure;
};

std::string HrMessage(const char* operation, HRESULT hr) {
    char text[160]{};
    std::snprintf(text, sizeof(text), "%s failed with HRESULT 0x%08lx",
                  operation, static_cast<unsigned long>(hr));
    return text;
}

// The same recording sink the harvest probe implements: GetGlyphRunOutline
// speaks ID2D1SimplifiedGeometrySink, and recording is the whole
// implementation.
class RecordingSink final : public ID2D1SimplifiedGeometrySink {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override {
        if (!value) return E_POINTER;
        if (IsEqualGUID(iid, __uuidof(IUnknown)) ||
            IsEqualGUID(iid, __uuidof(ID2D1SimplifiedGeometrySink))) {
            *value = static_cast<ID2D1SimplifiedGeometrySink*>(this);
            AddRef();
            return S_OK;
        }
        *value = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override { return --references_; }

    void STDMETHODCALLTYPE SetFillMode(D2D1_FILL_MODE mode) override {
        fill_mode = mode == D2D1_FILL_MODE_ALTERNATE ? OutlineFillMode::Alternate
                                                     : OutlineFillMode::Winding;
    }
    void STDMETHODCALLTYPE SetSegmentFlags(D2D1_PATH_SEGMENT) override {}
    void STDMETHODCALLTYPE BeginFigure(D2D1_POINT_2F start, D2D1_FIGURE_BEGIN) override {
        contours.emplace_back();
        contours.back().start_x = start.x;
        contours.back().start_y = start.y;
    }
    void STDMETHODCALLTYPE AddLines(const D2D1_POINT_2F* points, UINT32 count) override {
        for (UINT32 i = 0; i < count; ++i) {
            OutlineSegment segment;
            segment.cubic = false;
            segment.x[0] = points[i].x;
            segment.y[0] = points[i].y;
            contours.back().segments.push_back(segment);
        }
    }
    void STDMETHODCALLTYPE AddBeziers(const D2D1_BEZIER_SEGMENT* beziers,
                                      UINT32 count) override {
        for (UINT32 i = 0; i < count; ++i) {
            OutlineSegment segment;
            segment.cubic = true;
            segment.x[0] = beziers[i].point1.x;
            segment.y[0] = beziers[i].point1.y;
            segment.x[1] = beziers[i].point2.x;
            segment.y[1] = beziers[i].point2.y;
            segment.x[2] = beziers[i].point3.x;
            segment.y[2] = beziers[i].point3.y;
            contours.back().segments.push_back(segment);
        }
    }
    void STDMETHODCALLTYPE EndFigure(D2D1_FIGURE_END) override {}
    HRESULT STDMETHODCALLTYPE Close() override { return S_OK; }

    OutlineFillMode fill_mode = OutlineFillMode::Winding;
    std::vector<OutlineContour> contours;

private:
    LONG references_ = 1;
};

// A glyph's contours flattened at design scale: cubics at a fine fixed depth
// (128 chords keeps the polyline within a small fraction of a design unit of
// the curve), lines as they are. Pen at the origin, scale 1, so everything is
// design units.
std::vector<OutlinePolygon> FlattenGlyphAtDesignScale(
    const std::vector<OutlineContour>& contours) {
    constexpr int kChords = 128;
    std::vector<OutlinePolygon> out;
    for (const OutlineContour& contour : contours) {
        OutlinePolygon polygon;
        polygon.push_back({contour.start_x, contour.start_y});
        for (const OutlineSegment& segment : contour.segments) {
            if (segment.cubic) {
                const Point from = polygon.back();
                for (int chord = 1; chord <= kChords; ++chord) {
                    const double t = static_cast<double>(chord) / kChords;
                    const double u = 1.0 - t;
                    polygon.push_back(
                        {u * u * u * from.x + 3.0 * u * u * t * segment.x[0] +
                             3.0 * u * t * t * segment.x[1] + t * t * t * segment.x[2],
                         u * u * u * from.y + 3.0 * u * u * t * segment.y[0] +
                             3.0 * u * t * t * segment.y[1] + t * t * t * segment.y[2]});
                }
            } else {
                polygon.push_back({segment.x[0], segment.y[0]});
            }
        }
        out.push_back(std::move(polygon));
    }
    return out;
}

// Points along the polylines no further than `step` apart, so a long straight
// edge is queried along its length and not only at its ends.
std::vector<Point> DensePoints(const std::vector<OutlinePolygon>& contours,
                               double step) {
    std::vector<Point> out;
    for (const OutlinePolygon& polygon : contours) {
        for (std::size_t index = 0; index < polygon.size(); ++index) {
            const Point& here = polygon[index];
            const Point& next = polygon[(index + 1) % polygon.size()];
            out.push_back(here);
            const double length = std::hypot(next.x - here.x, next.y - here.y);
            const int pieces = static_cast<int>(std::ceil(length / step));
            for (int piece = 1; piece < pieces; ++piece) {
                const double t = static_cast<double>(piece) / pieces;
                out.push_back({here.x + t * (next.x - here.x),
                               here.y + t * (next.y - here.y)});
            }
        }
    }
    return out;
}

double DistanceToSegment(Point point, Point a, Point b) {
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double length_squared = dx * dx + dy * dy;
    double t = 0.0;
    if (length_squared > 0.0)
        t = std::max(0.0, std::min(1.0, ((point.x - a.x) * dx + (point.y - a.y) * dy) /
                                            length_squared));
    return std::hypot(point.x - (a.x + t * dx), point.y - (a.y + t * dy));
}

// The largest distance from any sampled point of `from` to the polylines of
// `to`, stopping the per-point search as soon as it is within tolerance.
double DirectedCurveDistance(const std::vector<Point>& from,
                             const std::vector<OutlinePolygon>& to,
                             double tolerance) {
    double worst = 0.0;
    for (const Point& point : from) {
        double nearest = std::numeric_limits<double>::infinity();
        for (const OutlinePolygon& polygon : to) {
            for (std::size_t index = 0; index < polygon.size(); ++index) {
                nearest = std::min(
                    nearest, DistanceToSegment(point, polygon[index],
                                               polygon[(index + 1) % polygon.size()]));
                if (nearest <= tolerance) break;
            }
            if (nearest <= tolerance) break;
        }
        worst = std::max(worst, nearest);
    }
    return worst;
}

// Check 1: the recorded outline of one codepoint against what
// GetGlyphRunOutline answers here, over the same file at design scale.
// Geometric, not structural: implementations segment the same shape
// differently -- one splits where another merges, a quadratic becomes one
// cubic or two -- so the comparison is between the curves themselves. Every
// densely sampled point of each side must lie within two design units
// (a thousandth of the em) of the other side's outline; a wrong glyph is off
// by tens to hundreds. Returns an empty string on agreement.
std::string CompareRecordedOutline(IDWriteFontFace& face, char32_t code,
                                   const GlyphOutline& recorded,
                                   double units_per_em) {
    constexpr double kTolerance = 2.0;  // design units
    constexpr double kSampleStep = 8.0;
    std::ostringstream where;
    where << "U+" << std::hex << std::uppercase << static_cast<unsigned long>(code)
          << std::dec;

    const UINT16 index = static_cast<UINT16>(recorded.glyph_index);
    RecordingSink sink;
    const HRESULT hr = face.GetGlyphRunOutline(
        static_cast<FLOAT>(units_per_em), &index, nullptr, nullptr, 1, FALSE, FALSE,
        &sink);
    if (FAILED(hr)) return where.str() + ": " + HrMessage("GetGlyphRunOutline", hr);
    if (sink.fill_mode != recorded.fill_mode)
        return where.str() + ": the live outline fills by a different rule";
    if (sink.contours.empty() != recorded.contours.empty()) {
        return where.str() + (recorded.contours.empty()
                                  ? ": the recording is blank and the live outline is not"
                                  : ": the live outline is blank and the recording is not");
    }
    if (recorded.contours.empty()) return std::string();

    const std::vector<OutlinePolygon> live = FlattenGlyphAtDesignScale(sink.contours);
    const std::vector<OutlinePolygon> kept = FlattenGlyphAtDesignScale(recorded.contours);
    const double live_to_kept =
        DirectedCurveDistance(DensePoints(live, kSampleStep), kept, kTolerance);
    const double kept_to_live =
        DirectedCurveDistance(DensePoints(kept, kSampleStep), live, kTolerance);
    if (live_to_kept > kTolerance || kept_to_live > kTolerance) {
        std::ostringstream detail;
        detail << where.str() << ": the live outline and the recording are "
               << std::max(live_to_kept, kept_to_live)
               << " design units apart; these are not the same shape";
        return detail.str();
    }
    return std::string();
}

// DirectWrite's own unhinted grayscale rasterisation of the glyphs at the
// same pen positions the outline painter uses. Blended through BlendPixel so
// the two surfaces share every rounding rule but the coverage itself.
bool PaintDirectWriteReference(IDWriteFactory2& factory, IDWriteFontFace& face,
                               const std::vector<UINT16>& indices,
                               const std::vector<FLOAT>& advances, double font_size,
                               double pen_x, double pen_y, Color ink,
                               Surface& surface, std::string& error) {
    DWRITE_GLYPH_RUN run{};
    run.fontFace = &face;
    run.fontEmSize = static_cast<FLOAT>(font_size);
    run.glyphCount = static_cast<UINT32>(indices.size());
    run.glyphIndices = indices.data();
    run.glyphAdvances = advances.data();

    ComPtr<IDWriteGlyphRunAnalysis> analysis;
    HRESULT hr = factory.CreateGlyphRunAnalysis(
        &run, nullptr, DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC,
        DWRITE_MEASURING_MODE_NATURAL, DWRITE_GRID_FIT_MODE_DISABLED,
        DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE, static_cast<FLOAT>(pen_x),
        static_cast<FLOAT>(pen_y), analysis.put());
    if (FAILED(hr)) {
        error = HrMessage("IDWriteFactory2::CreateGlyphRunAnalysis", hr);
        return false;
    }
    RECT bounds{};
    hr = analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_ALIASED_1x1, &bounds);
    if (FAILED(hr)) {
        error = HrMessage("GetAlphaTextureBounds", hr);
        return false;
    }
    if (bounds.right <= bounds.left || bounds.bottom <= bounds.top) {
        error = "DirectWrite reported an empty alpha texture";
        return false;
    }
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    std::vector<BYTE> texture(static_cast<std::size_t>(width) * height);
    hr = analysis->CreateAlphaTexture(DWRITE_TEXTURE_ALIASED_1x1, &bounds,
                                      texture.data(),
                                      static_cast<UINT32>(texture.size()));
    if (FAILED(hr)) {
        error = HrMessage("CreateAlphaTexture", hr);
        return false;
    }
    for (int row = 0; row < height; ++row) {
        const int sy = bounds.top + row;
        if (sy < 0 || sy >= surface.height()) continue;
        for (int column = 0; column < width; ++column) {
            const int sx = bounds.left + column;
            if (sx < 0 || sx >= surface.width()) continue;
            const BYTE coverage = texture[static_cast<std::size_t>(row) * width + column];
            if (coverage) surface.BlendPixel(sx, sy, ink, coverage / 255.0);
        }
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 5) {
        std::cerr << "usage: outline_compare <font-file> <family> <outlines-dir>"
                     " <report-json> [dump-dir]\n";
        return 2;
    }
    const std::string font_file = argv[1];
    const std::string family_name = argv[2];
    const std::string outlines_dir = argv[3];
    const fs::path report_path = argv[4];
    // Optional: both paintings of every sample, as PPMs, so a failure can be
    // looked at instead of argued about.
    const fs::path dump_dir = argc >= 6 ? fs::path(argv[5]) : fs::path();

    // Furthest a pixel of one painting may sit from the other painting's ink.
    // Three pixels, and the number is a measurement: the recording is
    // unhinted by construction and DirectWrite's rasterisation here applies
    // the font's grid-fitting, which moves Cascadia's hinted grave accent 2-3
    // pixels at 24px. The mask check is the systematic-error net (axis flips,
    // wrong scale, wrong typeface); shape fidelity is check 1's, at half a
    // design unit.
    constexpr int kTolerance = 3;

    GlyphOutlineLibrary library;
    try {
        if (LoadGlyphOutlineDirectory(library, outlines_dir) == 0) {
            std::cerr << "no outline documents under " << outlines_dir << "\n";
            return 4;
        }
    } catch (const std::exception& e) {
        std::cerr << "cannot load glyph outlines: " << e.what() << "\n";
        return 4;
    }
    const GlyphOutlineFamily* family = library.Resolve(family_name);
    if (!family) {
        std::cerr << "no recorded outlines for \"" << family_name << "\"\n";
        return 4;
    }

    ComPtr<IDWriteFactory> factory;
    HRESULT hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_ISOLATED,
                                     __uuidof(IDWriteFactory),
                                     reinterpret_cast<IUnknown**>(factory.put()));
    if (FAILED(hr)) {
        std::cerr << HrMessage("DWriteCreateFactory", hr) << "\n";
        return 4;
    }
    ComPtr<IDWriteFactory2> factory2;
    hr = factory->QueryInterface(__uuidof(IDWriteFactory2),
                                 reinterpret_cast<void**>(factory2.put()));
    if (FAILED(hr)) {
        std::cerr << HrMessage("QueryInterface(IDWriteFactory2)", hr) << "\n";
        return 4;
    }

    // The face straight from the file: no collection, no family resolution,
    // nothing that could resolve to a different font than the one the
    // recording's SHA-256 was checked against.
    std::wstring wide_path;
    for (unsigned char c : font_file) wide_path.push_back(c);
    ComPtr<IDWriteFontFile> file;
    hr = factory->CreateFontFileReference(wide_path.c_str(), nullptr, file.put());
    if (FAILED(hr)) {
        std::cerr << HrMessage("CreateFontFileReference", hr) << "\n";
        return 4;
    }
    WINBOOL supported = FALSE;
    DWRITE_FONT_FILE_TYPE file_type{};
    DWRITE_FONT_FACE_TYPE face_type{};
    UINT32 face_count = 0;
    hr = file->Analyze(&supported, &file_type, &face_type, &face_count);
    if (FAILED(hr) || !supported || face_count == 0) {
        std::cerr << "DirectWrite cannot read " << font_file << " as a font\n";
        return 4;
    }
    IDWriteFontFile* files[] = {file.get()};
    ComPtr<IDWriteFontFace> face;
    hr = factory->CreateFontFace(face_type, 1, files, 0,
                                 DWRITE_FONT_SIMULATIONS_NONE, face.put());
    if (FAILED(hr)) {
        std::cerr << HrMessage("CreateFontFace", hr) << "\n";
        return 4;
    }

    // Check 1 over every recorded codepoint: the recording must be what the
    // boundary answers here, coordinate for coordinate.
    std::vector<std::string> outline_mismatches;
    for (const auto& entry : family->outlines) {
        const std::string mismatch = CompareRecordedOutline(
            *face.get(), entry.first, entry.second, family->units_per_em);
        if (!mismatch.empty()) {
            outline_mismatches.push_back(mismatch);
            std::cerr << "outline: " << mismatch << "\n";
        }
    }

    // Every recorded codepoint, in codepoint order, chunked into short runs.
    // The advances handed to both painters are the same numbers -- recorded
    // design advances scaled to the run size -- so the pens walk identically
    // and every coverage difference is shape, not placement.
    std::vector<std::vector<char32_t>> chunks;
    std::vector<char32_t> chunk;
    for (const auto& entry : family->outlines) {
        chunk.push_back(entry.first);
        if (chunk.size() == 16) {
            chunks.push_back(chunk);
            chunk.clear();
        }
    }
    if (!chunk.empty()) chunks.push_back(chunk);

    const double sizes[] = {14.0, 24.0};

    std::vector<SampleResult> results;
    int failed = 0;
    for (double size : sizes) {
        const double scale = size / family->units_per_em;
        int sample_index = 0;
        for (const std::vector<char32_t>& codes : chunks) {
            std::string text;
            std::vector<double> advances;
            std::vector<FLOAT> float_advances;
            std::vector<UINT32> codepoints;
            double width = 0.0;
            for (char32_t code : codes) {
                text += EncodeUtf8(code);
                codepoints.push_back(static_cast<UINT32>(code));
                const double advance = family->outlines.at(code).advance * scale;
                advances.push_back(advance);
                float_advances.push_back(static_cast<FLOAT>(advance));
                width += advance;
            }
            TextOp run;
            run.bounds = Rect{4.0, 4.0, width + 8.0, size * 2.0};
            run.text = text;
            run.font_family = family_name;
            run.font_size = size;
            // A generous baseline: one em down. Both painters get the same
            // number, and the box has room above and below.
            run.baseline = size;
            run.advances = advances;
            run.path = "/outline-compare";

            std::ostringstream id;
            id << "size-" << size << "-chunk-" << sample_index++;
            SampleResult sample;
            sample.id = id.str();

            const int surface_width =
                static_cast<int>(std::ceil(run.bounds.x * 2.0 + run.bounds.width));
            const int surface_height =
                static_cast<int>(std::ceil(run.bounds.y * 2.0 + run.bounds.height));
            Surface dwrite_surface(surface_width, surface_height, BackdropColor());
            Surface outline_surface(surface_width, surface_height, BackdropColor());
            const Color ink{0xff, 0x00, 0x00, 0x00};

            // The same glyphs, by index, or the comparison is of two glyphs.
            std::vector<UINT16> indices(codepoints.size());
            hr = face->GetGlyphIndices(codepoints.data(),
                                       static_cast<UINT32>(codepoints.size()),
                                       indices.data());
            std::string index_mismatch;
            if (FAILED(hr)) {
                index_mismatch = HrMessage("GetGlyphIndices", hr);
            } else {
                for (std::size_t i = 0; i < codes.size(); ++i) {
                    const int recorded = family->outlines.at(codes[i]).glyph_index;
                    if (recorded != indices[i]) {
                        std::ostringstream mismatch;
                        mismatch << "U+" << std::hex << std::uppercase
                                 << static_cast<unsigned long>(codes[i]) << std::dec
                                 << ": the live cmap maps to glyph " << indices[i]
                                 << " and the recording carries " << recorded
                                 << "; these are different glyphs";
                        index_mismatch = mismatch.str();
                        break;
                    }
                }
            }

            std::string dwrite_error;
            std::string outline_error;
            if (!index_mismatch.empty()) {
                sample.failure = index_mismatch;
            } else if (!PaintDirectWriteReference(
                           *factory2.get(), *face.get(), indices, float_advances,
                           size, run.bounds.x, run.bounds.y + run.baseline, ink,
                           dwrite_surface, dwrite_error)) {
                sample.failure = "DirectWrite refused: " + dwrite_error;
            } else if (!DrawRecordedOutlineTextRun(library, outline_surface, run, ink,
                                                   outline_error)) {
                sample.failure = "the recorded painter refused: " + outline_error;
            } else {
                const std::vector<bool> dwrite_mask = InkMask(dwrite_surface);
                const std::vector<bool> outline_mask = InkMask(outline_surface);
                sample.dwrite_pixels = static_cast<int>(
                    std::count(dwrite_mask.begin(), dwrite_mask.end(), true));
                sample.outline_pixels = static_cast<int>(
                    std::count(outline_mask.begin(), outline_mask.end(), true));
                if (sample.dwrite_pixels == 0 || sample.outline_pixels == 0) {
                    sample.failure = "a painter drew no ink at all";
                } else {
                    int forward_x = -1, forward_y = -1, backward_x = -1, backward_y = -1;
                    const int forward = DirectedHausdorff(
                        dwrite_mask, outline_mask, surface_width, surface_height,
                        kTolerance, forward_x, forward_y);
                    const int backward = DirectedHausdorff(
                        outline_mask, dwrite_mask, surface_width, surface_height,
                        kTolerance, backward_x, backward_y);
                    sample.hausdorff = std::max(forward, backward);
                    if (sample.hausdorff > kTolerance) {
                        std::ostringstream failure;
                        failure << "ink masks are more than " << kTolerance
                                << "px apart (directed " << forward << " at ("
                                << forward_x << "," << forward_y << ") / " << backward
                                << " at (" << backward_x << "," << backward_y
                                << ")); these are not the same shapes";
                        sample.failure = failure.str();
                    } else {
                        sample.passed = true;
                    }
                }
            }
            if (!dump_dir.empty()) {
                fs::create_directories(dump_dir);
                std::ofstream(dump_dir / (sample.id + "-dwrite.ppm"), std::ios::binary)
                    << ToPpm(dwrite_surface);
                std::ofstream(dump_dir / (sample.id + "-outline.ppm"), std::ios::binary)
                    << ToPpm(outline_surface);
            }
            if (!sample.passed) {
                ++failed;
                std::cerr << sample.id << ": " << sample.failure << "\n";
            }
            results.push_back(std::move(sample));
        }
    }

    std::ostringstream report;
    report << "{\n \"schema_version\": 1,\n";
    report << " \"family\": \"" << JsonEscape(family->family) << "\",\n";
    report << " \"tolerance_px\": " << kTolerance << ",\n";
    report << " \"outlines_checked\": " << family->outlines.size() << ",\n";
    report << " \"outline_mismatches\": [\n";
    for (std::size_t index = 0; index < outline_mismatches.size(); ++index) {
        report << "  \"" << JsonEscape(outline_mismatches[index]) << "\""
               << (index + 1 < outline_mismatches.size() ? ",\n" : "\n");
    }
    report << " ],\n";
    report << " \"compared\": " << results.size() << ",\n";
    report << " \"failed\": " << failed << ",\n";
    report << " \"samples\": [\n";
    for (std::size_t index = 0; index < results.size(); ++index) {
        const SampleResult& sample = results[index];
        report << "  {\"id\": \"" << JsonEscape(sample.id) << "\", \"passed\": "
               << (sample.passed ? "true" : "false") << ", \"dwrite_pixels\": "
               << sample.dwrite_pixels << ", \"outline_pixels\": "
               << sample.outline_pixels << ", \"hausdorff\": " << sample.hausdorff
               << ", \"failure\": \"" << JsonEscape(sample.failure) << "\"}"
               << (index + 1 < results.size() ? ",\n" : "\n");
    }
    report << " ]\n}\n";
    std::ofstream(report_path, std::ios::binary) << report.str();

    std::cout << family->outlines.size() << " recorded outline(s) checked against the "
              << "live boundary (" << outline_mismatches.size() << " mismatch(es)); "
              << results.size() << " sample(s) compared in \"" << family->family
              << "\", " << failed << " failed\n";
    return failed == 0 && outline_mismatches.empty() ? 0 : 5;
}
