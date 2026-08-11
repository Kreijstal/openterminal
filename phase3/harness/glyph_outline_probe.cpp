// Record the ink of a font, as outlines, at the documented DirectWrite boundary.
//
// The problem this exists for, stated plainly. 113 render cases refuse with
//
//     DirectWrite could not resolve any requested family in "Segoe UI"
//
// because Segoe UI is not on the machine that paints. The metrics of it *are*
// recorded -- phase3/xaml-db/fonts/segoe-ui.json carries every advance the
// corpus uses, harvested on the Windows runner -- so those cases lay out with
// real numbers and then have nothing to draw with. The gap is not a bug in the
// renderer. It is that a font's *shapes* have never been recorded, only its
// widths, and you cannot paint a widths table.
//
// So: the same trick, one table further. `harvest_font_metrics.py` reads head,
// hhea, OS/2, hmtx, cmap, kern and GPOS off the runner's copy of a font we do
// not own and writes the numbers as JSON. This reads the outlines through
// IDWriteFontFace::GetGlyphRunOutline -- the documented boundary, the same one
// dwrite_glyph_probe.cpp uses for glyph runs -- and writes the contours as
// JSON. Nothing is intercepted, nothing is disassembled, no font file travels.
//
// LICENSING, WHICH IS NOT THIS FILE'S DECISION TO MAKE
// ----------------------------------------------------
// Metrics and outlines are not the same claim. In most jurisdictions the
// typeface *design* is not copyrightable but the outline data in the font file
// is, as a program or a compilation; the metrics this project already records
// sit on the other side of that line. Recording Segoe UI's contours is
// therefore a materially different act from recording its advance widths, and
// phase3/xaml-db/fonts/README.md -- which enumerates the tables that may be
// read, and lists no outline table -- does not authorise it.
//
// This file is the machinery, written so the decision can be made on the facts
// rather than on a guess about what would be involved. It is wired to nothing
// by default: no workflow step runs it for a font whose licence has not been
// checked, and phase3/xaml-db/glyph-outlines/README.md records which families
// are cleared. Cascadia Mono is cleared -- SIL OFL, and Terminal ships the file
// -- and is what the pipeline is proven on. Segoe UI is not cleared and is not
// harvested here until somebody with the authority to say so says so.
//
// WHAT COMES OUT
// --------------
// Design units, not pixels. GetGlyphRunOutline is called with emSize equal to
// the face's designUnitsPerEm and no transform, so the coordinates that arrive
// are the font's own integer grid -- independent of font size, of DPI, and of
// every rendering parameter. A run at 20.0 dip scales them by 20.0/2048 at
// paint time; nothing is baked in that a consumer would have to divide back
// out and could get wrong.
//
// Coordinates are printed with %.9g. GetGlyphRunOutline hands back FLOAT, and
// nine significant decimal digits is the round-trip guarantee for binary32:
// what is written parses back to the identical float. This matters because
// TrueType quadratics arrive here as cubics -- D2D's sink has no quadratic --
// and the exact conversion puts control points at thirds of a design unit,
// which is not a representable decimal. Recording nine digits records the
// float that DirectWrite produced. Rounding it to something prettier would
// record a shape the runtime never described.
//
// Usage:
//     glyph_outline_probe <family> <hex-codepoints,comma-separated> [<font-file>]
//
// The optional third argument is why this is testable at all. Cascadia Mono is
// the one family whose outlines are cleared to record, and it is not installed
// on the runner -- Terminal ships the file, and `harvest_font_metrics.py` reads
// its metrics straight out of the pinned checkout. Given a path, this does the
// same: CreateFontFileReference and CreateFontFace, no collection involved.
// Without one it looks the family up in the system collection, which is the
// only way to reach a font that exists solely as an installed face.
//
// stdout is one JSON document. Failure is a message on stderr and a non-zero
// exit; a missing glyph is a refusal and never a .notdef box, for the same
// reason the renderer refuses rather than substituting.

#include <windows.h>
#include <d2d1.h>
#include <dwrite_2.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void Check(HRESULT hr, const char* what) {
    if (FAILED(hr)) {
        std::ostringstream message;
        message << what << " failed with hresult 0x" << std::hex << static_cast<unsigned long>(hr);
        throw std::runtime_error(message.str());
    }
}

template <typename T>
class ComPtr {
public:
    ComPtr() = default;
    ~ComPtr() { if (raw_) raw_->Release(); }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    T** Put() { return &raw_; }
    T* Get() const { return raw_; }
    T* operator->() const { return raw_; }
    explicit operator bool() const { return raw_ != nullptr; }

private:
    T* raw_ = nullptr;
};

// The exact spelling of a float32, in the fewest digits that round-trip.
// See the header comment: %.9g is the binary32 guarantee, and the thirds a
// quadratic-to-cubic conversion produces are why it is needed.
std::string Number(FLOAT value) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.9g", static_cast<double>(value));
    return buffer;
}

std::string Escape(const std::string& text) {
    std::string out;
    for (char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char escape[8];
                    std::snprintf(escape, sizeof escape, "\\u%04x", c);
                    out += escape;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

std::string Narrow(const std::wstring& text) {
    if (text.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                                   nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                        out.data(), size, nullptr, nullptr);
    return out;
}

// One contour: a start point and the segments that close it. Segments are
// tagged the way the sink delivered them -- "l" for a line, "c" for a cubic --
// rather than normalised into one form, because normalising is a claim about
// equivalence that the consumer should be free to make or not.
struct Segment {
    char kind;                 // 'l' or 'c'
    D2D1_POINT_2F points[3];   // 1 used for 'l', 3 for 'c'
};

struct Contour {
    D2D1_POINT_2F start;
    std::vector<Segment> segments;
    bool closed = false;
};

// A recording sink. GetGlyphRunOutline talks to ID2D1SimplifiedGeometrySink
// (IDWriteGeometrySink is a typedef for it) and this implements it by writing
// down what it is told, in order, and nothing else. No flattening, no curve
// subdivision, no winding computation: those are the consumer's business, and
// a recorder that made them would be recording its own opinion.
class OutlineRecorder final : public IDWriteGeometrySink {
public:
    // The sink is a stack object owned by this function; the reference count
    // exists only because the interface demands one. DirectWrite does not
    // outlive the call, so there is nothing for a real one to protect.
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (IsEqualIID(iid, __uuidof(IUnknown)) ||
            IsEqualIID(iid, __uuidof(ID2D1SimplifiedGeometrySink))) {
            *object = static_cast<ID2D1SimplifiedGeometrySink*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }

    void STDMETHODCALLTYPE SetFillMode(D2D1_FILL_MODE mode) override { fill_mode_ = mode; }
    void STDMETHODCALLTYPE SetSegmentFlags(D2D1_PATH_SEGMENT) override {}

    void STDMETHODCALLTYPE BeginFigure(D2D1_POINT_2F start, D2D1_FIGURE_BEGIN) override {
        Contour contour;
        contour.start = start;
        contours_.push_back(std::move(contour));
    }

    void STDMETHODCALLTYPE AddLines(const D2D1_POINT_2F* points, UINT32 count) override {
        if (contours_.empty()) { failed_ = "AddLines outside a figure"; return; }
        for (UINT32 i = 0; i < count; ++i) {
            Segment segment{'l', {points[i], {}, {}}};
            contours_.back().segments.push_back(segment);
        }
    }

    void STDMETHODCALLTYPE AddBeziers(const D2D1_BEZIER_SEGMENT* beziers, UINT32 count) override {
        if (contours_.empty()) { failed_ = "AddBeziers outside a figure"; return; }
        for (UINT32 i = 0; i < count; ++i) {
            Segment segment{'c', {beziers[i].point1, beziers[i].point2, beziers[i].point3}};
            contours_.back().segments.push_back(segment);
        }
    }

    void STDMETHODCALLTYPE EndFigure(D2D1_FIGURE_END end) override {
        if (contours_.empty()) { failed_ = "EndFigure outside a figure"; return; }
        // A glyph contour is always closed; an open one would mean the sink
        // was handed something that is not a fillable shape, and filling it
        // anyway is exactly the kind of guess this project does not make.
        contours_.back().closed = (end == D2D1_FIGURE_END_CLOSED);
        if (!contours_.back().closed) failed_ = "an open figure in a glyph outline";
    }

    HRESULT STDMETHODCALLTYPE Close() override { return S_OK; }

    const std::vector<Contour>& contours() const { return contours_; }
    D2D1_FILL_MODE fill_mode() const { return fill_mode_; }
    const std::string& failure() const { return failed_; }

private:
    std::vector<Contour> contours_;
    // GetGlyphRunOutline sets this itself; recorded rather than assumed
    // because which winding rule fills a glyph is the difference between a
    // letter with a counter and a solid blob.
    D2D1_FILL_MODE fill_mode_ = D2D1_FILL_MODE_ALTERNATE;
    std::string failed_;
};

std::vector<uint32_t> ParseCodepoints(const std::string& list) {
    std::vector<uint32_t> out;
    std::istringstream stream(list);
    std::string item;
    while (std::getline(stream, item, ',')) {
        if (item.empty()) continue;
        char* end = nullptr;
        unsigned long value = std::strtoul(item.c_str(), &end, 16);
        if (!end || *end) throw std::runtime_error("codepoint is not hexadecimal: " + item);
        out.push_back(static_cast<uint32_t>(value));
    }
    if (out.empty()) throw std::runtime_error("no codepoints requested");
    return out;
}

std::string FontFilePath(IDWriteFontFace* face) {
    UINT32 count = 1;
    ComPtr<IDWriteFontFile> file;
    Check(face->GetFiles(&count, file.Put()), "GetFiles");
    if (count != 1) throw std::runtime_error("the face is not a single file");
    const void* key = nullptr;
    UINT32 key_size = 0;
    Check(file->GetReferenceKey(&key, &key_size), "GetReferenceKey");
    ComPtr<IDWriteFontFileLoader> loader;
    Check(file->GetLoader(loader.Put()), "GetLoader");
    ComPtr<IDWriteLocalFontFileLoader> local;
    Check(loader->QueryInterface(__uuidof(IDWriteLocalFontFileLoader),
                                 reinterpret_cast<void**>(local.Put())),
          "IDWriteLocalFontFileLoader");
    UINT32 length = 0;
    Check(local->GetFilePathLengthFromKey(key, key_size, &length), "GetFilePathLengthFromKey");
    std::wstring path(length + 1, L'\0');
    Check(local->GetFilePathFromKey(key, key_size, path.data(), length + 1),
          "GetFilePathFromKey");
    path.resize(length);
    return Narrow(path);
}

std::wstring Widen(const std::string& text) {
    int wide = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    std::wstring out(static_cast<size_t>(wide), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out.data(), wide);
    out.resize(static_cast<size_t>(wide ? wide - 1 : 0));
    return out;
}

int Run(int argc, char** argv) {
    if (argc != 3 && argc != 4) {
        std::cerr << "usage: glyph_outline_probe <family> "
                     "<hex-codepoints,comma-separated> [<font-file>]\n";
        return 2;
    }
    const std::string family_utf8 = argv[1];
    const std::vector<uint32_t> codepoints = ParseCodepoints(argv[2]);
    const std::string font_file = argc == 4 ? argv[3] : std::string();
    const std::wstring family = Widen(family_utf8);

    ComPtr<IDWriteFactory> factory;
    Check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                              reinterpret_cast<IUnknown**>(factory.Put())),
          "DWriteCreateFactory");

    ComPtr<IDWriteFontFace> face;
    if (!font_file.empty()) {
        // Straight to the face. No collection, no family lookup, no matching:
        // the caller named a file, so there is nothing to resolve and nothing
        // that could resolve to a different font than the one it meant.
        ComPtr<IDWriteFontFile> file;
        Check(factory->CreateFontFileReference(Widen(font_file).c_str(), nullptr,
                                               file.Put()),
              "CreateFontFileReference");
        BOOL supported = FALSE;
        DWRITE_FONT_FILE_TYPE file_type{};
        DWRITE_FONT_FACE_TYPE face_type{};
        UINT32 faces = 0;
        Check(file->Analyze(&supported, &file_type, &face_type, &faces), "Analyze");
        if (!supported) throw std::runtime_error(font_file + " is not a font DirectWrite reads");
        IDWriteFontFile* files[] = {file.Get()};
        Check(factory->CreateFontFace(face_type, 1, files, 0,
                                      DWRITE_FONT_SIMULATIONS_NONE, face.Put()),
              "CreateFontFace");
    } else {
        ComPtr<IDWriteFontCollection> collection;
        Check(factory->GetSystemFontCollection(collection.Put(), TRUE),
              "GetSystemFontCollection");
        UINT32 index = 0;
        BOOL exists = FALSE;
        Check(collection->FindFamilyName(family.c_str(), &index, &exists), "FindFamilyName");
        if (!exists)
            throw std::runtime_error("no installed family named \"" + family_utf8 + "\"");
        ComPtr<IDWriteFontFamily> font_family;
        Check(collection->GetFontFamily(index, font_family.Put()), "GetFontFamily");
        ComPtr<IDWriteFont> font;
        Check(font_family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL,
                                                DWRITE_FONT_STRETCH_NORMAL,
                                                DWRITE_FONT_STYLE_NORMAL, font.Put()),
              "GetFirstMatchingFont");
        Check(font->CreateFontFace(face.Put()), "CreateFontFace");
    }

    DWRITE_FONT_METRICS metrics{};
    face->GetMetrics(&metrics);
    const FLOAT em = static_cast<FLOAT>(metrics.designUnitsPerEm);

    std::vector<UINT16> indices(codepoints.size());
    Check(face->GetGlyphIndices(codepoints.data(), static_cast<UINT32>(codepoints.size()),
                                indices.data()),
          "GetGlyphIndices");

    std::ostringstream out;
    out << "{\n \"schema_version\": 1,\n";
    out << " \"boundary\": \"IDWriteFontFace::GetGlyphRunOutline -> "
           "ID2D1SimplifiedGeometrySink\",\n";
    out << " \"family\": \"" << Escape(family_utf8) << "\",\n";
    out << " \"units_per_em\": " << metrics.designUnitsPerEm << ",\n";
    out << " \"coordinates\": \"design units; emSize == units_per_em, no transform\",\n";
    out << " \"source\": {\"file\": \"" << Escape(FontFilePath(face.Get())) << "\"},\n";
    out << " \"outlines\": {\n";

    for (size_t i = 0; i < codepoints.size(); ++i) {
        if (indices[i] == 0) {
            std::ostringstream message;
            message << "U+" << std::hex << std::uppercase << std::setw(4) << std::setfill('0')
                    << codepoints[i] << " is not covered by \"" << family_utf8
                    << "\"; a .notdef outline would be a shape the font does not have";
            throw std::runtime_error(message.str());
        }

        DWRITE_GLYPH_METRICS glyph_metrics{};
        Check(face->GetDesignGlyphMetrics(&indices[i], 1, &glyph_metrics, FALSE),
              "GetDesignGlyphMetrics");

        OutlineRecorder recorder;
        Check(face->GetGlyphRunOutline(em, &indices[i], nullptr, nullptr, 1,
                                       FALSE, FALSE, &recorder),
              "GetGlyphRunOutline");
        if (!recorder.failure().empty()) throw std::runtime_error(recorder.failure());

        if (i) out << ",\n";
        out << "  \"" << codepoints[i] << "\": {\n";
        out << "   \"glyph_index\": " << indices[i] << ",\n";
        out << "   \"advance\": " << glyph_metrics.advanceWidth << ",\n";
        out << "   \"fill_mode\": \""
            << (recorder.fill_mode() == D2D1_FILL_MODE_WINDING ? "winding" : "alternate")
            << "\",\n";
        out << "   \"contours\": [";
        const std::vector<Contour>& contours = recorder.contours();
        for (size_t c = 0; c < contours.size(); ++c) {
            out << (c ? ",\n    " : "\n    ") << "{\"start\": ["
                << Number(contours[c].start.x) << ", " << Number(contours[c].start.y)
                << "], \"segments\": [";
            const std::vector<Segment>& segments = contours[c].segments;
            for (size_t s = 0; s < segments.size(); ++s) {
                if (s) out << ", ";
                out << "[\"" << segments[s].kind << "\"";
                const int points = segments[s].kind == 'c' ? 3 : 1;
                for (int p = 0; p < points; ++p) {
                    out << ", " << Number(segments[s].points[p].x)
                        << ", " << Number(segments[s].points[p].y);
                }
                out << "]";
            }
            out << "]}";
        }
        out << (contours.empty() ? "" : "\n   ") << "]\n";
        out << "  }";
    }
    out << "\n }\n}\n";
    std::cout << out.str();
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return Run(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << "glyph_outline_probe: " << error.what() << "\n";
        return 1;
    }
}
