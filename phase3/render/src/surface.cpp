#include "surface.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace openxaml {
namespace render {

PixelRect SnapRect(const Rect& rect) {
    PixelRect out;
    out.left = static_cast<int>(RoundHalfUp(rect.x));
    out.top = static_cast<int>(RoundHalfUp(rect.y));
    out.right = static_cast<int>(RoundHalfUp(rect.x + rect.width));
    out.bottom = static_cast<int>(RoundHalfUp(rect.y + rect.height));
    return out;
}

PixelRect TouchedRect(const Rect& rect) {
    PixelRect out;
    out.left = static_cast<int>(std::floor(rect.x));
    out.top = static_cast<int>(std::floor(rect.y));
    out.right = static_cast<int>(std::ceil(rect.x + rect.width));
    out.bottom = static_cast<int>(std::ceil(rect.y + rect.height));
    return out;
}

Surface::Surface(int width, int height, Color clear)
    : width_(std::max(0, width)), height_(std::max(0, height)) {
    pixels_.assign(static_cast<size_t>(width_) * static_cast<size_t>(height_), Pack(clear));
}

void Surface::FillRect(const Rect& rect, Color color) {
    if (color.a != 0xff)
        throw std::logic_error("Surface::FillRect was given a brush that is not opaque");
    PixelRect box = SnapRect(rect);
    box.left = std::max(box.left, 0);
    box.top = std::max(box.top, 0);
    box.right = std::min(box.right, width_);
    box.bottom = std::min(box.bottom, height_);
    if (box.empty()) return;
    const std::uint32_t packed = Pack(color);
    for (int y = box.top; y < box.bottom; ++y) {
        const size_t row = static_cast<size_t>(y) * static_cast<size_t>(width_);
        std::fill(pixels_.begin() + static_cast<std::ptrdiff_t>(row + box.left),
                  pixels_.begin() + static_cast<std::ptrdiff_t>(row + box.right), packed);
    }
}

std::string ToPpm(const Surface& surface) {
    std::string out = "P6\n";
    out += std::to_string(surface.width());
    out += " ";
    out += std::to_string(surface.height());
    out += "\n255\n";
    out.reserve(out.size() + static_cast<size_t>(surface.width()) *
                                 static_cast<size_t>(surface.height()) * 3);
    for (std::uint32_t pixel : surface.pixels()) {
        out.push_back(static_cast<char>((pixel >> 16) & 0xff));
        out.push_back(static_cast<char>((pixel >> 8) & 0xff));
        out.push_back(static_cast<char>(pixel & 0xff));
    }
    return out;
}

}  // namespace render
}  // namespace openxaml
