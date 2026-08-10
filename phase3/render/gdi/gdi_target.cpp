#include "gdi_target.h"
#include "dwrite_text_provider.h"

#include <algorithm>
#include <cmath>
#include <cstring>
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
    for (const TextOp& run : runs) {
        std::string diagnostic;
        if (!DrawDirectWriteTextRun(surface, run, ink, diagnostic))
            failures.push_back(std::move(diagnostic));
    }
    target_.Load(surface);
}

void PaintInto(DibTarget& target, const DisplayList& list, Color ink,
               std::vector<std::string>& failures) {
    GdiTextBackend backend(target);
    std::vector<RenderIssue> render_issues;
    Surface surface = RasterizeDisplayList(list, &backend, BackdropColor(), ink, failures,
                                           render_issues);
    for (const RenderIssue& issue : render_issues)
        failures.push_back(issue.message);
    target.Load(surface);
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
