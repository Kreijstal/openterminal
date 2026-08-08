#include "gdi_target.h"

#include <cmath>
#include <cstring>
#include <set>
#include <string>
#include <vector>

namespace openxaml {
namespace render {
namespace {

std::wstring Widen(const std::string& utf8) {
    if (utf8.empty()) return std::wstring();
    const int needed = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                                           static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), out.data(),
                        needed);
    return out;
}

struct FamilyProbe {
    std::wstring wanted;
    bool found = false;
};

int CALLBACK CollectFamily(const LOGFONTW* logical, const TEXTMETRICW*, DWORD, LPARAM param) {
    auto* probe = reinterpret_cast<FamilyProbe*>(param);
    if (probe->wanted == logical->lfFaceName) probe->found = true;
    return 1;
}

}  // namespace

DibTarget::DibTarget(int width, int height) : width_(width), height_(height) {
    if (width_ <= 0 || height_ <= 0) return;
    dc_ = CreateCompatibleDC(nullptr);
    if (!dc_) return;

    BITMAPINFO info;
    ZeroMemory(&info, sizeof(info));
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = width_;
    // Negative: top-down, so row zero is the top row and the memory order is
    // the order the PPM and the software surface both use. A bottom-up DIB
    // would need a flip on every transfer and one of them would eventually be
    // forgotten.
    info.bmiHeader.biHeight = -height_;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    bitmap_ = CreateDIBSection(dc_, &info, DIB_RGB_COLORS, &bits_, nullptr, 0);
    if (!bitmap_) {
        bits_ = nullptr;
        return;
    }
    previous_ = SelectObject(dc_, bitmap_);
}

DibTarget::~DibTarget() {
    if (dc_ && previous_) SelectObject(dc_, previous_);
    if (bitmap_) DeleteObject(bitmap_);
    if (dc_) DeleteDC(dc_);
}

void DibTarget::Load(const Surface& surface) {
    if (!bits_) return;
    GdiFlush();
    std::memcpy(bits_, surface.pixels().data(),
                surface.pixels().size() * sizeof(std::uint32_t));
}

void DibTarget::Store(Surface& surface) const {
    if (!bits_) return;
    GdiFlush();
    std::memcpy(surface.pixels().data(), bits_,
                surface.pixels().size() * sizeof(std::uint32_t));
}

bool FontFamilyInstalled(const std::string& family) {
    FamilyProbe probe;
    probe.wanted = Widen(family);
    LOGFONTW logical;
    ZeroMemory(&logical, sizeof(logical));
    logical.lfCharSet = DEFAULT_CHARSET;
    if (probe.wanted.size() >= LF_FACESIZE) return false;
    wcscpy(logical.lfFaceName, probe.wanted.c_str());
    HDC screen = GetDC(nullptr);
    EnumFontFamiliesExW(screen, &logical, CollectFamily, reinterpret_cast<LPARAM>(&probe), 0);
    ReleaseDC(nullptr, screen);
    return probe.found;
}

void GdiTextBackend::DrawRuns(Surface& surface, const std::vector<TextOp>& runs, Color ink,
                              std::vector<std::string>& failures) {
    if (!target_.valid()) {
        failures.push_back("no device context: the DIB could not be created");
        return;
    }
    target_.Load(surface);

    const int saved = SaveDC(target_.dc());
    SetBkMode(target_.dc(), TRANSPARENT);
    SetTextColor(target_.dc(), RGB(ink.r, ink.g, ink.b));
    SetTextAlign(target_.dc(), TA_LEFT | TA_TOP | TA_NOUPDATECP);

    std::set<std::string> already_named;
    for (const TextOp& run : runs) {
        // A FontFamily in XAML may be a fallback list. The measurement path
        // resolves the list and the corpus verifies which entry owns the
        // advances; this asks GDI for the first name only, and says so when
        // that name is not installed rather than letting GDI substitute.
        std::string first = run.font_family;
        const size_t comma = first.find(',');
        if (comma != std::string::npos) first = first.substr(0, comma);
        while (!first.empty() && first.back() == ' ') first.pop_back();

        if (!FontFamilyInstalled(first)) {
            if (already_named.insert(first).second) {
                failures.push_back("the font family \"" + first +
                                   "\" is not installed on this system, and substituting "
                                   "another would put the ink where nothing measured it");
            }
            continue;
        }

        LOGFONTW logical;
        ZeroMemory(&logical, sizeof(logical));
        // Negative height is the em size in device units, which is what a XAML
        // FontSize is at the corpus's pinned scale of 1.0.
        logical.lfHeight = -static_cast<LONG>(std::lround(run.font_size));
        logical.lfCharSet = DEFAULT_CHARSET;
        logical.lfOutPrecision = OUT_TT_PRECIS;
        logical.lfQuality = DEFAULT_QUALITY;
        const std::wstring face = Widen(first);
        if (face.size() >= LF_FACESIZE) {
            failures.push_back("the font family name \"" + first + "\" is too long for GDI");
            continue;
        }
        wcscpy(logical.lfFaceName, face.c_str());

        HFONT font = CreateFontIndirectW(&logical);
        if (!font) {
            failures.push_back("GDI would not create the font \"" + first + "\"");
            continue;
        }
        HGDIOBJ previous = SelectObject(target_.dc(), font);

        const std::wstring text = Widen(run.text);
        // Unclipped, deliberately. The claim this whole pass makes about text is
        // that the box the measurement path derived from the harvested advances
        // *contains* the ink the platform draws; the checker enforces exactly
        // that, and a clip to the box would erase the evidence whenever the
        // claim was false. So the run is drawn at its origin and left to spill
        // if it is going to, and the spill fails the gate.
        const PixelRect box = SnapRect(run.bounds);
        // Aligned on the baseline the measurement path derived, not on the top
        // of GDI's glyph cell. Those are different lines: GDI sizes its cell
        // from the font's win metrics, which for Cascadia Mono exceed the hhea
        // metrics the line box was measured with, so a top-aligned draw puts
        // the baseline about two pixels low at size 12 and the descenders land
        // outside the box the corpus verified.
        if (run.baseline <= 0.0) {
            failures.push_back("no harvested metrics gave a baseline for \"" + first +
                               "\", and placing the run by GDI's own ascent would put the "
                               "ink where nothing measured it");
            SelectObject(target_.dc(), previous);
            DeleteObject(font);
            continue;
        }
        // And spaced on the measurement path's advances, not GDI's. GDI rounds
        // each advance to a whole pixel, so six of Cascadia Mono's 11.71875 at
        // size 20 walk the pen to 72 where the arrange says the run is 70.32
        // wide and the last glyph lands outside its own box. The distances
        // below are the rounded *positions*, differenced, so the error stays
        // under a pixel across the run instead of accumulating per glyph.
        //
        // One advance per codepoint, one entry per UTF-16 unit: they part
        // company above the BMP, and there the run is left to GDI with the
        // mismatch reported rather than silently misaligned.
        std::vector<INT> distances;
        if (run.advances.size() == text.size()) {
            distances.reserve(text.size());
            double walked = 0.0;
            long placed = 0;
            for (double advance : run.advances) {
                walked += advance;
                const long next = std::lround(walked);
                distances.push_back(static_cast<INT>(next - placed));
                placed = next;
            }
        } else if (!run.advances.empty()) {
            failures.push_back("the run \"" + run.text.substr(0, 24) +
                               "\" has one advance per codepoint and more UTF-16 units than "
                               "codepoints, so the measured advances cannot be handed to GDI");
        }

        const UINT previous_align = GetTextAlign(target_.dc());
        SetTextAlign(target_.dc(), TA_LEFT | TA_BASELINE | TA_NOUPDATECP);
        ExtTextOutW(target_.dc(), box.left,
                    box.top + static_cast<int>(std::lround(run.baseline)), 0, nullptr,
                    text.c_str(), static_cast<UINT>(text.size()),
                    distances.empty() ? nullptr : distances.data());
        SetTextAlign(target_.dc(), previous_align);

        SelectObject(target_.dc(), previous);
        DeleteObject(font);
    }

    RestoreDC(target_.dc(), saved);
    target_.Store(surface);

    // The DIB's alpha channel is whatever GDI left behind: ExtTextOut writes
    // no alpha, so a pixel it touched comes back with zero there. The dumps are
    // opaque by construction, so the alpha is restored rather than written out
    // -- nothing downstream reads it, and leaving it would make two runs differ
    // only in a channel no one looks at.
    for (std::uint32_t& pixel : surface.pixels()) pixel |= 0xff000000u;
}

void PaintInto(DibTarget& target, const DisplayList& list, Color ink,
               std::vector<std::string>& failures) {
    Surface surface(target.width(), target.height(), BackdropColor());
    for (const RectOp& op : list.rects) surface.FillRect(op.bounds, op.color);
    GdiTextBackend backend(target);
    if (!list.texts.empty()) {
        backend.DrawRuns(surface, list.texts, ink, failures);
    } else {
        target.Load(surface);
    }
}

void Paint(HDC destination, int x, int y, const DisplayList& list, Color ink,
           std::vector<std::string>& failures) {
    const PixelRect box = SnapRect(Rect{0.0, 0.0, list.surface.width, list.surface.height});
    if (box.empty()) return;
    DibTarget target(box.right, box.bottom);
    if (!target.valid()) {
        failures.push_back("no device context: the DIB could not be created");
        return;
    }
    PaintInto(target, list, ink, failures);
    BitBlt(destination, x, y, target.width(), target.height(), target.dc(), 0, 0, SRCCOPY);
}

}  // namespace render
}  // namespace openxaml
