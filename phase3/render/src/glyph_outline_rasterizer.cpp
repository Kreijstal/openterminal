#include "glyph_outline_rasterizer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace openxaml {
namespace render {
namespace {

// Strict UTF-8, refusing rather than substituting, because a replacement
// character would be painted as a glyph nothing measured. Mirrors the decoder
// the layout core shapes with (phase3/layout/src/text.cpp).
bool DecodeUtf8(const std::string& text, std::vector<char32_t>& out) {
    out.clear();
    size_t index = 0;
    while (index < text.size()) {
        const auto lead = static_cast<unsigned char>(text[index]);
        size_t extra = 0;
        char32_t code = 0;
        if (lead < 0x80) {
            code = lead;
        } else if ((lead & 0xE0) == 0xC0) {
            extra = 1;
            code = lead & 0x1F;
        } else if ((lead & 0xF0) == 0xE0) {
            extra = 2;
            code = lead & 0x0F;
        } else if ((lead & 0xF8) == 0xF0) {
            extra = 3;
            code = lead & 0x07;
        } else {
            return false;
        }
        if (index + extra >= text.size()) return false;
        for (size_t step = 1; step <= extra; ++step) {
            const auto unit = static_cast<unsigned char>(text[index + step]);
            if ((unit & 0xC0) != 0x80) return false;
            code = (code << 6) | (unit & 0x3F);
        }
        // Overlong encodings and surrogates are not text.
        if ((extra == 1 && code < 0x80) || (extra == 2 && code < 0x800) ||
            (extra == 3 && code < 0x10000) || code > 0x10FFFF ||
            (code >= 0xD800 && code <= 0xDFFF)) {
            return false;
        }
        out.push_back(code);
        index += extra + 1;
    }
    return true;
}

struct Edge {
    double x0, y0, x1, y1;
    int direction;  // +1 when the edge goes down the surface, -1 up
};

struct Crossing {
    double x;
    int direction;
};

std::string CodepointName(char32_t code) {
    char text[16]{};
    std::snprintf(text, sizeof(text), "U+%04lX", static_cast<unsigned long>(code));
    return text;
}

}  // namespace

void FlattenCubic(Point from, Point control1, Point control2, Point to,
                  OutlinePolygon& out) {
    for (int chord = 1; chord <= kCubicChords; ++chord) {
        if (chord == kCubicChords) {
            // The endpoint is the recording's own number, not an evaluation
            // that lands a rounding away from it.
            out.push_back(to);
            break;
        }
        const double t = static_cast<double>(chord) / kCubicChords;
        const double u = 1.0 - t;
        const double b0 = u * u * u;
        const double b1 = 3.0 * u * u * t;
        const double b2 = 3.0 * u * t * t;
        const double b3 = t * t * t;
        out.push_back({b0 * from.x + b1 * control1.x + b2 * control2.x + b3 * to.x,
                       b0 * from.y + b1 * control1.y + b2 * control2.y + b3 * to.y});
    }
}

void FillContours(Surface& surface, const std::vector<OutlinePolygon>& contours,
                  OutlineFillMode fill_mode, Color ink, const PixelRect& clip) {
    std::vector<Edge> edges;
    double top = 0.0;
    double bottom = 0.0;
    double left = 0.0;
    double right = 0.0;
    bool any = false;
    for (const OutlinePolygon& contour : contours) {
        if (contour.size() < 2) continue;
        for (size_t index = 0; index < contour.size(); ++index) {
            const Point& here = contour[index];
            const Point& next = contour[(index + 1) % contour.size()];
            if (!any) {
                top = bottom = here.y;
                left = right = here.x;
                any = true;
            }
            top = std::min(top, here.y);
            bottom = std::max(bottom, here.y);
            left = std::min(left, here.x);
            right = std::max(right, here.x);
            if (here.y == next.y) continue;  // horizontal edges never cross a scanline
            edges.push_back(Edge{here.x, here.y, next.x, next.y,
                                 next.y > here.y ? 1 : -1});
        }
    }
    if (!any || edges.empty()) return;

    PixelRect box = clip;
    box.left = std::max({box.left, 0, static_cast<int>(std::floor(left))});
    box.top = std::max({box.top, 0, static_cast<int>(std::floor(top))});
    box.right = std::min({box.right, surface.width(),
                          static_cast<int>(std::ceil(right))});
    box.bottom = std::min({box.bottom, surface.height(),
                           static_cast<int>(std::ceil(bottom))});
    if (box.empty()) return;

    const double sample_weight = 1.0 / kCoverageRowSamples;
    std::vector<double> coverage(static_cast<size_t>(box.width()));
    std::vector<Crossing> crossings;
    for (int y = box.top; y < box.bottom; ++y) {
        std::fill(coverage.begin(), coverage.end(), 0.0);
        for (int sample = 0; sample < kCoverageRowSamples; ++sample) {
            const double sy = y + (sample + 0.5) * sample_weight;
            crossings.clear();
            for (const Edge& edge : edges) {
                const double lower = std::min(edge.y0, edge.y1);
                const double upper = std::max(edge.y0, edge.y1);
                // Half-open, so a scanline through a shared vertex counts the
                // meeting edges exactly once between them.
                if (!(lower <= sy && sy < upper)) continue;
                const double t = (sy - edge.y0) / (edge.y1 - edge.y0);
                crossings.push_back(
                    Crossing{edge.x0 + t * (edge.x1 - edge.x0), edge.direction});
            }
            if (crossings.size() < 2) continue;
            std::sort(crossings.begin(), crossings.end(),
                      [](const Crossing& a, const Crossing& b) {
                          if (a.x != b.x) return a.x < b.x;
                          return a.direction < b.direction;
                      });
            int winding = 0;
            double span_start = 0.0;
            bool in_span = false;
            for (const Crossing& crossing : crossings) {
                const bool was_inside =
                    fill_mode == OutlineFillMode::Winding ? winding != 0
                                                          : (winding & 1) != 0;
                winding += fill_mode == OutlineFillMode::Winding
                               ? crossing.direction
                               : 1;
                const bool now_inside =
                    fill_mode == OutlineFillMode::Winding ? winding != 0
                                                          : (winding & 1) != 0;
                if (!was_inside && now_inside) {
                    span_start = crossing.x;
                    in_span = true;
                } else if (was_inside && !now_inside && in_span) {
                    const double span_left = std::max(span_start,
                                                      static_cast<double>(box.left));
                    const double span_right = std::min(crossing.x,
                                                       static_cast<double>(box.right));
                    in_span = false;
                    if (span_right <= span_left) continue;
                    int column = static_cast<int>(std::floor(span_left));
                    for (; column < span_right; ++column) {
                        const double covered =
                            std::min(span_right, static_cast<double>(column + 1)) -
                            std::max(span_left, static_cast<double>(column));
                        if (covered > 0.0)
                            coverage[static_cast<size_t>(column - box.left)] +=
                                covered * sample_weight;
                    }
                }
            }
        }
        for (int column = box.left; column < box.right; ++column) {
            const double value = coverage[static_cast<size_t>(column - box.left)];
            if (value <= 0.0) continue;
            surface.BlendPixel(column, y, ink, std::min(1.0, value));
        }
    }
}

bool DrawRecordedOutlineTextRun(const GlyphOutlineLibrary& library, Surface& surface,
                                const TextOp& run, Color ink, std::string& diagnostic) {
    const GlyphOutlineFamily* family = library.Resolve(run.font_family);
    if (!family) {
        diagnostic = run.path + ": no recorded outlines for any family in \"" +
                     run.font_family + "\"";
        return false;
    }
    if (run.text.empty()) return true;
    if (!std::isfinite(run.font_size) || run.font_size <= 0.0) {
        diagnostic = run.path + ": recorded outlines need a finite positive font size";
        return false;
    }
    // Zero is the display list's way of saying no metrics measured this line;
    // placing the pen anyway would be a guess about where the baseline sits.
    if (!std::isfinite(run.baseline) || run.baseline <= 0.0) {
        diagnostic = run.path + ": recorded outlines cannot place a run whose "
                     "baseline was never measured";
        return false;
    }
    if (run.has_clip) {
        const double clip_right = run.clip.x + run.clip.width;
        const double clip_bottom = run.clip.y + run.clip.height;
        const auto is_integer = [](double value) {
            return std::isfinite(value) && value == std::floor(value);
        };
        if (!is_integer(run.clip.x) || !is_integer(run.clip.y) ||
            !is_integer(clip_right) || !is_integer(clip_bottom)) {
            // The same refusal the DirectWrite painter makes, for the same
            // reason: a fractional clip needs edge-coverage masking.
            diagnostic = run.path + ": recorded outlines cannot mask a fractional "
                         "text clip; edge-coverage masking is not implemented";
            return false;
        }
    }

    Rect effective = run.bounds;
    if (run.has_clip) {
        const double left = std::max(effective.x, run.clip.x);
        const double top = std::max(effective.y, run.clip.y);
        const double right = std::min(effective.x + effective.width,
                                      run.clip.x + run.clip.width);
        const double bottom = std::min(effective.y + effective.height,
                                       run.clip.y + run.clip.height);
        effective = {left, top, std::max(0.0, right - left),
                     std::max(0.0, bottom - top)};
    }
    PixelRect box = TouchedRect(effective);
    box.left = std::max(box.left, 0);
    box.top = std::max(box.top, 0);
    box.right = std::min(box.right, surface.width());
    box.bottom = std::min(box.bottom, surface.height());
    if (box.empty()) return true;

    std::vector<char32_t> codepoints;
    if (!DecodeUtf8(run.text, codepoints)) {
        diagnostic = run.path + ": recorded outlines cannot paint text that is not "
                     "valid UTF-8";
        return false;
    }
    if (run.advances.size() != codepoints.size()) {
        diagnostic = run.path + ": recorded outlines need one advance per codepoint "
                     "and the run retains " + std::to_string(run.advances.size()) +
                     " for " + std::to_string(codepoints.size());
        return false;
    }

    // The same one-line rule the DirectWrite painter applies, float
    // accumulation included, so a run refuses through the same gate whichever
    // painter looks at it: the display list carries advances, not where the
    // layout put the breaks.
    bool breaks_line = false;
    for (char32_t code : codepoints)
        breaks_line |= code == 0x0a || code == 0x0d || code == 0x2028 || code == 0x2029;
    double retained_width = 0.0;
    for (double advance : run.advances)
        retained_width = static_cast<double>(
            static_cast<float>(retained_width + advance));
    const bool one_line = !breaks_line &&
        (!run.wrap || retained_width <= run.bounds.width + 0.0001);
    if (!one_line) {
        diagnostic = run.path + ": recorded outlines cannot place a line-broken run: "
                     "the display list retains advances, not where the breaks went";
        return false;
    }

    // Validate the whole run before painting any of it, so a gap in the
    // middle refuses cleanly instead of leaving half a string on the surface.
    for (char32_t code : codepoints) {
        if (family->outlines.find(code) == family->outlines.end()) {
            diagnostic = run.path + ": recorded outlines for \"" + family->family +
                         "\" do not cover " + CodepointName(code) +
                         "; a substitute box would claim ink the font never drew";
            return false;
        }
    }

    const double scale = run.font_size / family->units_per_em;
    double pen_x = run.bounds.x;
    const double pen_y = run.bounds.y + run.baseline;
    std::vector<OutlinePolygon> device_contours;
    for (size_t index = 0; index < codepoints.size(); ++index) {
        const GlyphOutline& glyph = family->outlines.at(codepoints[index]);
        device_contours.clear();
        for (const OutlineContour& contour : glyph.contours) {
            OutlinePolygon polygon;
            Point pen{pen_x + contour.start_x * scale, pen_y + contour.start_y * scale};
            polygon.push_back(pen);
            for (const OutlineSegment& segment : contour.segments) {
                const Point to{pen_x + segment.x[segment.cubic ? 2 : 0] * scale,
                               pen_y + segment.y[segment.cubic ? 2 : 0] * scale};
                if (segment.cubic) {
                    FlattenCubic(polygon.back(),
                                 {pen_x + segment.x[0] * scale,
                                  pen_y + segment.y[0] * scale},
                                 {pen_x + segment.x[1] * scale,
                                  pen_y + segment.y[1] * scale},
                                 to, polygon);
                } else {
                    polygon.push_back(to);
                }
            }
            device_contours.push_back(std::move(polygon));
        }
        if (!device_contours.empty())
            FillContours(surface, device_contours, glyph.fill_mode, ink, box);
        pen_x += run.advances[index];
    }
    return true;
}

bool DrawRecordedOutlineTextRun(Surface& surface, const TextOp& run, Color ink,
                                std::string& diagnostic) {
    return DrawRecordedOutlineTextRun(GlyphOutlineLibrary::Default(), surface, run, ink,
                                      diagnostic);
}

void RecordedOutlineTextBackend::DrawRuns(Surface& surface,
                                          const std::vector<TextOp>& runs, Color ink,
                                          std::vector<std::string>& failures,
                                          std::vector<std::string>* painters) {
    for (const TextOp& run : runs) {
        std::string diagnostic;
        if (DrawRecordedOutlineTextRun(library_, surface, run, ink, diagnostic)) {
            if (painters) painters->push_back(name());
        } else {
            failures.push_back(std::move(diagnostic));
            if (painters) painters->push_back(std::string());
        }
    }
}

}  // namespace render
}  // namespace openxaml
