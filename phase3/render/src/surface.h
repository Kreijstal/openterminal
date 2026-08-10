// A deterministic premultiplied BGRA8 surface. Opaque fills retain their exact
// overwrite path; fractional solid colours use integer source-over, and an
// intermediate surface can be composited once for subtree opacity.
//
// Axis-aligned rectangles retain their exact whole-pixel fast path. Affine
// geometry may instead supply deterministic area coverage per pixel; this is
// geometric antialiasing only, with no device-specific sampling or subpixel
// colour. Alpha blending is premultiplied source-over, and subtree opacity is
// applied to a completed intermediate layer rather than independently to its
// drawing commands.
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

    // Premultiplied 0xAARRGGBB per pixel, row-major, top row first. The same order a
    // bottom-up Windows DIB holds after its rows are flipped, which is what the
    // GDI backend hands back.
    const std::vector<std::uint32_t>& pixels() const { return pixels_; }
    std::vector<std::uint32_t>& pixels() { return pixels_; }

    std::uint32_t At(int x, int y) const { return pixels_[static_cast<size_t>(y) * width_ + x]; }

    // Opaque fill, clipped to the surface. An alpha other than 255 is a
    // programming error here, preserving the existing exact overwrite path.
    void FillRect(const Rect& rect, Color color);

    // Premultiplies the straight XAML colour and composites it source-over.
    // Transparent is a no-op and opaque dispatches to FillRect.
    void BlendRect(const Rect& rect, Color color);

    // Blends one source pixel with geometric area coverage in [0, 1]. The
    // straight XAML colour is premultiplied once, then every premultiplied
    // channel is scaled by coverage before source-over.
    void BlendPixel(int x, int y, Color color, double coverage);

    // Applies opacity once to a complete premultiplied layer, then composites
    // it source-over this surface. Dimensions must match.
    void CompositeLayer(const Surface& layer, double opacity);

    // Composites one already-premultiplied source pixel source-over this
    // surface, using the same integer arithmetic every other blend here uses.
    //
    // This is the primitive imported content needs. A brush colour arrives
    // straight and is premultiplied on the way in; a pixel produced outside
    // XAML -- a composition surface, a swap chain back buffer -- is already
    // premultiplied, and premultiplying it again would darken it. Coordinates
    // outside the surface are a no-op rather than an error, so a caller may
    // walk a rectangle that the surface only partly contains.
    void BlendPremultipliedPixel(int x, int y, std::uint32_t premultiplied);

private:
    int width_;
    int height_;
    std::vector<std::uint32_t> pixels_;
};

inline unsigned char MultiplyByte(unsigned char value, unsigned char scale) {
    return static_cast<unsigned char>((static_cast<unsigned int>(value) * scale + 127u) / 255u);
}

inline std::uint32_t PackPremultiplied(unsigned char a, unsigned char r, unsigned char g,
                                      unsigned char b) {
    return (static_cast<std::uint32_t>(a) << 24) | (static_cast<std::uint32_t>(r) << 16) |
           (static_cast<std::uint32_t>(g) << 8) | static_cast<std::uint32_t>(b);
}

inline std::uint32_t Pack(Color c) {
    return PackPremultiplied(c.a, MultiplyByte(c.r, c.a), MultiplyByte(c.g, c.a),
                             MultiplyByte(c.b, c.a));
}

// The colour every dump starts from. Not white: white is a colour a case could
// legitimately paint, and a dump whose blank areas were indistinguishable from
// a painted white rectangle would make the round trip ambiguous. This one is
// reserved the same way the probe ink is.
inline Color BackdropColor() { return Color{0xff, 0x00, 0x80, 0x80}; }

// A binary PPM (P6). Chosen over PNG for the opaque rectangle-recovery path:
// it needs no compressor and two runs are byte-identical. PPM cannot preserve
// alpha; premultiplied acceptance output always uses ToBgra below.
std::string ToPpm(const Surface& surface);

// The acceptance-oracle format: B, G, R, A bytes, top row first. Unlike PPM
// this preserves alpha and can therefore be compared byte-for-byte with
// Windows.UI.Xaml.Media.Imaging.RenderTargetBitmap.
std::string ToBgra(const Surface& surface);

}  // namespace render
}  // namespace openxaml

#endif  // OPENXAML_RENDER_SURFACE_H
