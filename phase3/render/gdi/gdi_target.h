// The only part of the render pass that touches a platform.
//
// Everything that decides *what* is painted lives in ../src and builds on
// Linux with no Windows headers at all. This file draws a display list that has
// already been decided, and it draws it two ways:
//
//   * rectangles, into the DIB's own memory with the software surface -- the
//     same code the native harness uses, so the rectangle pixels of a Wine dump
//     and a Linux dump are byte-identical rather than merely similar. GDI's
//     FillRect would be a second rasteriser with its own rounding, and then a
//     disagreement between the two backends would be unattributable.
//   * text, through DirectWrite text layouts and glyph-run analyses. Shaping,
//     metrics, and ClearType channel coverage therefore come from one documented
//     Windows text stack rather than from independent GDI measurement and paint
//     paths. ClearType is accepted only over opaque pixels; a transparent target
//     is reported by name rather than silently switched to guessed grayscale.
//
// A font the system has not got is not substituted. The requested family list
// is resolved explicitly against DirectWrite's system collection, and ambiguous
// system fallback is reported rather than guessed.

#ifndef OPENXAML_RENDER_GDI_TARGET_H
#define OPENXAML_RENDER_GDI_TARGET_H

#include <windows.h>

#include <string>
#include <vector>

#include "case_runner.h"
#include "display_list.h"
#include "surface.h"

namespace openxaml {
namespace render {

// A 32-bit top-down DIB and the memory DC it is selected into.
class DibTarget {
public:
    DibTarget(int width, int height);
    ~DibTarget();

    DibTarget(const DibTarget&) = delete;
    DibTarget& operator=(const DibTarget&) = delete;

    bool valid() const { return bits_ != nullptr; }
    HDC dc() const { return dc_; }
    int width() const { return width_; }
    int height() const { return height_; }

    // Copies a software surface in, and reads the DIB back out into one. The
    // DIB's 32-bit BI_RGB pixels are 0xAARRGGBB little-endian, which is exactly
    // what Surface holds, so neither direction reorders anything.
    void Load(const Surface& surface);
    void Store(Surface& surface) const;

private:
    int width_ = 0;
    int height_ = 0;
    HDC dc_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ previous_ = nullptr;
    void* bits_ = nullptr;
};

// Whether the system has a font family under this exact name. Not a
// convenience: see the header comment.
bool FontFamilyInstalled(const std::string& family);

// Draws the rectangles of a display list into a target, then the text runs.
// `failures` collects one message per run that could not be drawn.
void PaintInto(DibTarget& target, const DisplayList& list, Color ink,
               std::vector<std::string>& failures);

// The paint entry a host has. Given a display list and a device context of any
// kind -- a window's, a printer's, a XAML island's composition surface -- this
// puts the pixels there. The rectangles go through a DIB so that they are the
// same pixels the offscreen dumps hold; the text goes straight to the DC.
//
// This is the surface phase3/xamlcore's island hosting calls: a display list is
// built once by Build() in ../src/display_list.h and painted as often as the
// host repaints.
void Paint(HDC destination, int x, int y, const DisplayList& list, Color ink,
           std::vector<std::string>& failures);

// The text backend the shared harness drives.
class GdiTextBackend : public TextBackend {
public:
    explicit GdiTextBackend(DibTarget& target) : target_(target) {}
    void DrawRuns(Surface& surface, const std::vector<TextOp>& runs, Color ink,
                  std::vector<std::string>& failures) override;
    std::string name() const override { return "directwrite"; }

private:
    DibTarget& target_;
};

}  // namespace render
}  // namespace openxaml

#endif  // OPENXAML_RENDER_GDI_TARGET_H
