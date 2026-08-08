// A 32-bit surface and the only drawing primitive this pass claims: a solid,
// axis-aligned, fully opaque rectangle.
//
// Deliberately not a rasteriser. There is no anti-aliasing, no blending and no
// sub-pixel coverage, because there is nothing to be faithful *to*: no recorded
// measurement says what the runtime puts in a partly covered pixel. What the
// corpus does pin is where an edge lands, and an edge lands on a whole device
// pixel -- that is what UseLayoutRounding is for, and phase3/layout/src/chrome
// says in as many words that a border thickness is rounded *because it is
// drawn*. So the only honest rasteriser is one that fills whole pixels, and one
// that fills whole pixels is exactly invertible: a checker can recover the
// rectangle it was given, to the pixel, which is the gate this track is built
// on.
//
// Plain C++17 -- no Windows, no GDI. The GDI backend under Wine draws the same
// display list with the same snapping so the two dumps can be compared.

#ifndef OPENXAML_RENDER_SURFACE_H
#define OPENXAML_RENDER_SURFACE_H

#include <cstdint>
#include <string>
#include <vector>

#include "brush.h"
#include "layout.h"

namespace openxaml {
namespace render {

// Where a rectangle's edges land. Round-half-up, per edge, which is the rule
// the layout core rounds sizes with (see RoundHalfUp in layout.h) -- so a rect
// whose numbers are already whole, as every rounded layout's are, maps to
// itself exactly.
struct PixelRect {
    int left = 0;
    int top = 0;
    int right = 0;   // exclusive
    int bottom = 0;  // exclusive

    int width() const { return right - left; }
    int height() const { return bottom - top; }
    bool empty() const { return right <= left || bottom <= top; }
};

// Where a solid fill lands: each edge to the nearest pixel, which is how the
// runtime rasterises a rectangle and what every recovered rectangle is held to.
PixelRect SnapRect(const Rect& rect);

// Every pixel the rectangle overlaps at all, however slightly. This is the
// right box for asking whether ink stayed inside a run, and it is not the fill
// box: a run measured 70.32 wide covers a third of pixel column 70, so ink
// there is inside the box the arrange derived even though a fill of the same
// rectangle would stop at column 69. Using the fill box for containment calls
// that pixel an escape and is wrong by up to half a pixel on every edge.
PixelRect TouchedRect(const Rect& rect);

class Surface {
public:
    Surface(int width, int height, Color clear);

    int width() const { return width_; }
    int height() const { return height_; }

    // 0xAARRGGBB per pixel, row-major, top row first. The same order a
    // bottom-up Windows DIB holds after its rows are flipped, which is what the
    // GDI backend hands back.
    const std::vector<std::uint32_t>& pixels() const { return pixels_; }
    std::vector<std::uint32_t>& pixels() { return pixels_; }

    std::uint32_t At(int x, int y) const { return pixels_[static_cast<size_t>(y) * width_ + x]; }

    // Opaque fill, clipped to the surface. An alpha other than 255 is a
    // programming error here: the display list refuses those upstream, by name,
    // rather than letting a blend rule nothing measured leak in.
    void FillRect(const Rect& rect, Color color);

private:
    int width_;
    int height_;
    std::vector<std::uint32_t> pixels_;
};

inline std::uint32_t Pack(Color c) {
    return (static_cast<std::uint32_t>(c.a) << 24) | (static_cast<std::uint32_t>(c.r) << 16) |
           (static_cast<std::uint32_t>(c.g) << 8) | static_cast<std::uint32_t>(c.b);
}

// The colour every dump starts from. Not white: white is a colour a case could
// legitimately paint, and a dump whose blank areas were indistinguishable from
// a painted white rectangle would make the round trip ambiguous. This one is
// reserved the same way the probe ink is.
inline Color BackdropColor() { return Color{0xff, 0x00, 0x80, 0x80}; }

// A binary PPM (P6). Chosen over PNG on purpose: it needs no compressor, so
// two runs of the same input are byte-identical without depending on a zlib
// version, and a checker can read it in twenty lines of Python with no
// third-party module. Alpha is not written -- every pixel in a dump is opaque
// by construction, since the only fills that reach a surface are opaque.
std::string ToPpm(const Surface& surface);

}  // namespace render
}  // namespace openxaml

#endif  // OPENXAML_RENDER_SURFACE_H
