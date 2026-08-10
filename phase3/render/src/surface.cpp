#include "surface.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace openxaml {
namespace render {
namespace {

unsigned char Channel(std::uint32_t pixel, unsigned int shift) {
    return static_cast<unsigned char>((pixel >> shift) & 0xffu);
}

std::uint32_t SourceOver(std::uint32_t source, std::uint32_t destination) {
    const unsigned char source_alpha = Channel(source, 24);
    if (source_alpha == 0) return destination;
    if (source_alpha == 0xff) return source;
    const unsigned char inverse = static_cast<unsigned char>(0xffu - source_alpha);
    const auto over = [inverse](unsigned char source_channel,
                                unsigned char destination_channel) {
        const unsigned int value = static_cast<unsigned int>(source_channel) +
                                   MultiplyByte(destination_channel, inverse);
        return static_cast<unsigned char>(std::min(value, 255u));
    };
    return PackPremultiplied(
        over(source_alpha, Channel(destination, 24)),
        over(Channel(source, 16), Channel(destination, 16)),
        over(Channel(source, 8), Channel(destination, 8)),
        over(Channel(source, 0), Channel(destination, 0)));
}

unsigned char ScaleOpacity(unsigned char value, double opacity) {
    return static_cast<unsigned char>(std::floor(value * opacity + 0.5));
}

std::uint32_t ApplyOpacity(std::uint32_t pixel, double opacity) {
    return PackPremultiplied(ScaleOpacity(Channel(pixel, 24), opacity),
                             ScaleOpacity(Channel(pixel, 16), opacity),
                             ScaleOpacity(Channel(pixel, 8), opacity),
                             ScaleOpacity(Channel(pixel, 0), opacity));
}

PixelRect ClippedBox(const Rect& rect, int width, int height) {
    if (!std::isfinite(rect.x) || !std::isfinite(rect.y) ||
        !std::isfinite(rect.width) || !std::isfinite(rect.height) ||
        rect.width < 0.0 || rect.height < 0.0)
        throw std::logic_error("Surface rectangle is not finite and non-negative");
    const double far_x = rect.x + rect.width;
    const double far_y = rect.y + rect.height;
    if (!std::isfinite(far_x) || !std::isfinite(far_y))
        throw std::logic_error("Surface rectangle extent overflows");
    const double left = std::max(0.0, rect.x);
    const double top = std::max(0.0, rect.y);
    const double right = std::min(static_cast<double>(width), far_x);
    const double bottom = std::min(static_cast<double>(height), far_y);
    if (right <= left || bottom <= top) return {};
    return SnapRect({left, top, right - left, bottom - top});
}

int SaturatingPixel(double value, bool ceil) {
    if (std::isnan(value)) throw std::logic_error("pixel coordinate is NaN");
    const double rounded = ceil ? std::ceil(value) : RoundHalfUp(value);
    if (rounded <= static_cast<double>(std::numeric_limits<int>::min()))
        return std::numeric_limits<int>::min();
    if (rounded >= static_cast<double>(std::numeric_limits<int>::max()))
        return std::numeric_limits<int>::max();
    return static_cast<int>(rounded);
}

int SaturatingFloor(double value) {
    if (std::isnan(value)) throw std::logic_error("pixel coordinate is NaN");
    const double rounded = std::floor(value);
    if (rounded <= static_cast<double>(std::numeric_limits<int>::min()))
        return std::numeric_limits<int>::min();
    if (rounded >= static_cast<double>(std::numeric_limits<int>::max()))
        return std::numeric_limits<int>::max();
    return static_cast<int>(rounded);
}

}  // namespace

PixelRect SnapRect(const Rect& rect) {
    PixelRect out;
    out.left = SaturatingPixel(rect.x, false);
    out.top = SaturatingPixel(rect.y, false);
    out.right = SaturatingPixel(rect.x + rect.width, false);
    out.bottom = SaturatingPixel(rect.y + rect.height, false);
    return out;
}

PixelRect TouchedRect(const Rect& rect) {
    PixelRect out;
    out.left = SaturatingFloor(rect.x);
    out.top = SaturatingFloor(rect.y);
    out.right = SaturatingPixel(rect.x + rect.width, true);
    out.bottom = SaturatingPixel(rect.y + rect.height, true);
    return out;
}

Surface::Surface(int width, int height, Color clear)
    : width_(std::max(0, width)), height_(std::max(0, height)) {
    const std::size_t pixel_width = static_cast<std::size_t>(width_);
    const std::size_t pixel_height = static_cast<std::size_t>(height_);
    if (pixel_height != 0 &&
        pixel_width > std::numeric_limits<std::size_t>::max() / pixel_height)
        throw std::length_error("Surface pixel count overflows size_t");
    const std::size_t count = pixel_width * pixel_height;
    if (count > pixels_.max_size())
        throw std::length_error("Surface pixel count exceeds vector max_size");
    pixels_.assign(count, Pack(clear));
}

void Surface::FillRect(const Rect& rect, Color color) {
    if (color.a != 0xff)
        throw std::logic_error("Surface::FillRect was given a brush that is not opaque");
    PixelRect box = ClippedBox(rect, width_, height_);
    if (box.empty()) return;
    const std::uint32_t packed = Pack(color);
    for (int y = box.top; y < box.bottom; ++y) {
        const size_t row = static_cast<size_t>(y) * static_cast<size_t>(width_);
        std::fill(pixels_.begin() + static_cast<std::ptrdiff_t>(row + box.left),
                  pixels_.begin() + static_cast<std::ptrdiff_t>(row + box.right), packed);
    }
}

void Surface::BlendRect(const Rect& rect, Color color) {
    if (color.a == 0) return;
    if (color.a == 0xff) {
        FillRect(rect, color);
        return;
    }
    const PixelRect box = ClippedBox(rect, width_, height_);
    if (box.empty()) return;
    const std::uint32_t source = Pack(color);
    for (int y = box.top; y < box.bottom; ++y) {
        const size_t row = static_cast<size_t>(y) * static_cast<size_t>(width_);
        for (int x = box.left; x < box.right; ++x) {
            std::uint32_t& destination = pixels_[row + static_cast<size_t>(x)];
            destination = SourceOver(source, destination);
        }
    }
}

void Surface::BlendPixel(int x, int y, Color color, double coverage) {
    if (!std::isfinite(coverage) || coverage < 0.0 || coverage > 1.0)
        throw std::logic_error("Surface::BlendPixel coverage is outside [0, 1]");
    if (x < 0 || y < 0 || x >= width_ || y >= height_ || color.a == 0 || coverage == 0.0)
        return;
    const std::uint32_t source = coverage == 1.0
                                     ? Pack(color)
                                     : ApplyOpacity(Pack(color), coverage);
    std::uint32_t& destination =
        pixels_[static_cast<size_t>(y) * static_cast<size_t>(width_) +
                static_cast<size_t>(x)];
    destination = SourceOver(source, destination);
}

void Surface::CompositeLayer(const Surface& layer, double opacity) {
    if (layer.width_ != width_ || layer.height_ != height_)
        throw std::logic_error("Surface::CompositeLayer dimensions do not match");
    if (!std::isfinite(opacity) || opacity < 0.0 || opacity > 1.0)
        throw std::logic_error("Surface::CompositeLayer opacity is outside [0, 1]");
    if (opacity == 0.0) return;
    for (size_t index = 0; index < pixels_.size(); ++index) {
        const std::uint32_t source =
            opacity == 1.0 ? layer.pixels_[index] : ApplyOpacity(layer.pixels_[index], opacity);
        pixels_[index] = SourceOver(source, pixels_[index]);
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

std::string ToBgra(const Surface& surface) {
    std::string out;
    out.reserve(static_cast<size_t>(surface.width()) *
                static_cast<size_t>(surface.height()) * 4);
    for (std::uint32_t pixel : surface.pixels()) {
        out.push_back(static_cast<char>(pixel & 0xff));
        out.push_back(static_cast<char>((pixel >> 8) & 0xff));
        out.push_back(static_cast<char>((pixel >> 16) & 0xff));
        out.push_back(static_cast<char>((pixel >> 24) & 0xff));
    }
    return out;
}

}  // namespace render
}  // namespace openxaml
