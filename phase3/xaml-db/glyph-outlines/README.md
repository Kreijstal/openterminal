# Recorded glyph outlines, and the plan for painting from them

Nothing in this directory is committed. The JSON is CI output, on the same
terms as `../measurements/` and `../fonts/`: it is a reading taken off a font
on the runner, regenerated deterministically, and what is committed is the
script that reads it. `.gitignore` covers `*.json` here. This README is the
source; the data is not.

## The problem, in numbers

The render frontier stands at 1058 painted-exact, 122 refused, 0 failed, 7 not
laid out over 1187 cases. **113 of the 122 refusals are one message:**

    DirectWrite could not resolve any requested family in "Segoe UI"

Those are not layout failures. `../fonts/segoe-ui.json` carries every advance
the corpus uses — harvested on the Windows runner, cross-checked against
`../fonts/derived/segoe-ui.json`, which the corpus solved out of the recorded
measurements independently. So the cases measure with real numbers, produce a
correct `TextOp` with a correct box, correct baseline and one correct advance
per codepoint, reach `DrawDirectWriteTextRun`, and refuse — because the machine
painting has no Segoe UI installed, and a widths table cannot be painted.

Three things that would close it and are all refused:

* **install a substitute and paint that.** The ink would be a different
  typeface at the recorded advances. Every containment check would pass and
  every pixel would be a lie.
* **synthesise shapes from the metrics.** Inventing data, with extra steps.
* **redistribute the font.** Not ours to redistribute.

The fourth is to record the ink the way the widths are already recorded.

## What the harvest records

`../../harness/glyph_outline_probe.cpp` calls
`IDWriteFontFace::GetGlyphRunOutline` into a recording
`ID2D1SimplifiedGeometrySink`. Nothing is intercepted, nothing is disassembled,
no font file travels — the same posture as `harvest_font_metrics.py` reading
`hmtx` and `cmap`, one table further along.

    {
      "schema_version": 1,
      "boundary": "IDWriteFontFace::GetGlyphRunOutline -> ID2D1SimplifiedGeometrySink",
      "family": "Cascadia Mono",
      "provenance": "harvested",
      "licence": "SIL Open Font License 1.1; shipped in microsoft/terminal ...",
      "units_per_em": 2048,
      "coordinates": "design units; emSize == units_per_em, no transform",
      "source": {"file": "...CascadiaMono.ttf", "sha256": "..."},
      "outlines": {
        "77": {
          "glyph_index": 48,
          "advance": 1229,
          "fill_mode": "winding",
          "contours": [{"start": [x, y], "segments": [["l", x, y],
                                                      ["c", x1,y1, x2,y2, x,y]]}]
        }
      }
    }

Four decisions worth stating:

**Design units, not pixels.** `emSize` is passed as the face's
`designUnitsPerEm` with no transform, so what arrives is the font's own integer
grid — independent of size, DPI and every rendering parameter. A run at 20 dip
scales by `20/2048` at paint time. Nothing is baked in that a consumer would
have to divide back out and could get wrong.

**Nine significant digits.** `GetGlyphRunOutline` returns `FLOAT`, and `%.9g` is
the round-trip guarantee for binary32. TrueType quadratics arrive as cubics —
D2D's sink has no quadratic — and the exact conversion puts control points at
thirds of a design unit, which is not a representable decimal. Nine digits
records the float DirectWrite produced; rounding to something prettier would
record a shape the runtime never described.

**Segments keep the kind the sink delivered them with**, `"l"` or `"c"`, rather
than being normalised to one form. Normalising is a claim about equivalence,
and the consumer should be free to make it or not.

**`fill_mode` is recorded, not assumed.** Which winding rule fills a glyph is
the difference between an `o` with a counter and a solid blob.

The harvest is refused unless it agrees with the metrics harvest on family,
`units_per_em`, the file's SHA-256, and **every design advance**. That last one
is the load-bearing check: it is an independent reading of the same number
through a different API, so agreement means the outline and the width came off
the same glyph of the same file. A codepoint the metrics cover and the outlines
do not is fatal — a partial harvest paints part of a string and refuses the
rest, which looks like it worked.

## The licence gate, which is why Segoe UI is not here

`harvest_glyph_outlines.py` carries a committed `CLEARED` list and refuses any
family not on it, by name, before it runs the probe:

    Segoe UI: outline harvesting is not cleared for this family. Recording a
    font's outlines is a different act from recording its metrics -- the
    metrics this project already keeps are not generally copyrightable and the
    outline data generally is -- and phase3/xaml-db/fonts/README.md authorises
    no outline table. The decision is a licensing question and belongs to a
    human ...

Cleared today: **Cascadia Mono** and **Cascadia Code** — SIL OFL 1.1, and
Terminal ships both files, so the outlines already travel. That is what the
pipeline is proven on, and the workflow asserts the refusal of Segoe UI on
every run so the gate is exercised rather than trusted.

**Adding Segoe UI to `CLEARED` is the decision this whole track exists to put
in front of somebody who can make it.** The machinery is built so that the call
can be made on the facts. It is not made here.

## The consumption plan

Not implemented. What follows is the design, so that the implementation is a
review of a plan rather than an argument about one.

### Where it plugs in

`DrawDirectWriteTextRun(Surface&, const TextOp&, Color ink, std::string&)` in
`phase3/render/gdi/dwrite_text_provider.cpp` is the whole seam. Everything above
it — `Walker::DrawText` → `TextBackendAdapter` → `GdiTextBackend::DrawRuns` — is
platform-neutral and already hands over `bounds`, `baseline`, `font_size`,
`has_clip`/`clip` and one advance per codepoint. A sibling
`DrawRecordedOutlineTextRun` with the same signature needs nothing new from
layout, and the two are chosen between per run: recorded outlines first when
the family has them, DirectWrite when it does not, refusal when neither.

### Placement is already solved, and is not re-derived

The existing single-line path is the contract to reproduce verbatim: the pen
starts at `(bounds.x, bounds.y + baseline)` and codepoint *i* moves it by
`advances[i]` — the *layout's* advances, out of the display list, never the
font's. The recorded `advance` in this artifact is used **only** to check the
harvest against the metrics; it is never a position. That is what keeps painted
pixels recoverable back to layout: the glyph shape is the only new input, and
every coordinate that decides *where* it goes is one the corpus already
verified against the oracle.

### Rasterisation

`cpu_raster_backend.cpp`'s `RasterPolygon` cannot be reused — `IntersectConvex`
is Sutherland–Hodgman and glyphs are neither convex nor single-contour. What is
reusable is `Surface::BlendPixel(x, y, Color, double coverage)`: deterministic
integer arithmetic, which the "two renders are byte-identical" rule requires.

So: flatten each `"c"` segment to a polyline at a fixed, recorded subdivision
depth (fixed, not adaptive — an adaptive tolerance in device space makes the
output depend on the size a run happened to be painted at, and determinism is
worth more here than a few saved segments), scale design units by
`font_size / units_per_em`, translate to the pen position, accumulate all
contours of a glyph into one edge list, and fill with a scanline coverage
rasteriser honouring the recorded `fill_mode`. Feed the coverage to
`BlendPixel`.

Note this produces grayscale coverage where the DirectWrite path produces
ClearType 3x1, and goes through `BlendPixel` where that path writes raw
`0xff000000 | rgb`. The two will not agree pixel for pixel. That is acceptable
and must be stated: `check_render.py` blanks text boxes before its byte-exact
comparison, so the difference is invisible to the self-consistency gate — but
it is *not* invisible to a future comparison against the native render oracle,
and a run painted from outlines must be labelled as such in the sidecar so that
comparison is never made silently.

### The exactness rule, and what would actually be proved

`check_render.py` asks two things of text and no more: no pixel outside
`TouchedRect(bounds)` may differ from the solid re-rasterisation, and each
non-blank run must contain at least one pixel of exactly the probe ink. An
outline painter satisfying those has demonstrated that its ink lands inside the
box the layout computed — the containment property, which is real but weak.

Say plainly what it does not prove: that the shapes are Segoe UI's. Only the
recorded-versus-live comparison on a Windows runner can show that, and the
honest form of it is to paint the same corpus twice on the runner — once
through DirectWrite with the font installed, once through the recorded outlines
— and compare coverage. That gate belongs in the workflow next to the existing
render-oracle check and does not exist yet.

### Order of work

1. the scanline filler, with its own tests against known polygons, on Linux;
2. `DrawRecordedOutlineTextRun`, refusing by name for any codepoint with no
   recorded outline — never a `.notdef` box, for the same reason `Resolve`
   refuses rather than substituting;
3. a `--glyph-outlines DIR` argument on `render_cases_gdi.exe` (it currently
   takes cases, out, fonts, theme-resources, and no font *file* at all), and
   the matching `build_render.py` flag;
4. a sidecar field naming which painter drew each run;
5. the runner-side coverage comparison in 4. above;
6. and only then, if and when it is cleared, Segoe UI.

## Status

**Nothing here has ever run.** `glyph_outline_probe.cpp` builds only on Windows;
the workflow steps that build and run it are committed but have not been pushed,
so no run has produced an artifact. `fetch_measurements.py --glyph-outlines`
says exactly that and exits non-zero, and
`phase3/tests/test_harvest_glyph_outlines.py` carries the end-to-end case as a
named skip rather than a pass. The 113 refusals stand, and that is the honest
state of them.
