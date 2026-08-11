# First pixels

The layout core matches the recorded runtime to the half-pixel across 1176 of
1177 measurements, and until now nothing drew any of it. This does — the minimum
that can be drawn honestly, and no more.

    cmake -S phase3/render -B build && cmake --build build
    build/render_cases phase3/xaml-db/cases /tmp/dumps <fonts-dir>
    python3 phase4/scripts/check_render.py --dumps /tmp/dumps

## The rule

**Paint only what is derived from recorded truth.**

The corpus records three things per element: a desired size, a render size and a
layout slot. That makes a rectangle's *geometry* derivable — slot origin plus
render size, accumulated down the tree — and it makes a rectangle's *colour*
derivable when the markup, or the WinUI 2.8.4 theme dictionary a
`{ThemeResource}` resolves through, spelled one.

Everything else is a **named no-draw**: an entry saying which element and which
feature, never an approximation. The Wave 6 rendered-output harvester now lives
in `harness/xaml_render_probe.cpp`; until its Windows artifact is ingested by a
feature-specific comparison there is still no expectation that could catch a
plausible-looking gradient, so a plausible-looking gradient remains exactly the
wrong number the project's standing rules forbid.

The named no-draws are the renderer work list. In the focused corpus each one
is now an explicit acceptance failure against native pixels and visual state;
the broader layout corpus keeps the name so an unprobed variation is never
mistaken for a pass.

## What paints

| feature | disposition |
|---|---|
| `Background` on Border, Panel, ContentPresenter, Control | **paints** the arranged rect, when the brush reduces to an opaque colour |
| `Background` written as `#rgb`, `#argb`, `#rrggbb`, `#aarrggbb` | **paints** |
| `Background="{ThemeResource K}"` | **paints** — the dictionary resolves the key to the same literal an inlined attribute would have written, so both routes reach one parser |
| `Background="Transparent"` | paints nothing, and that is the answer, not a refusal |
| `BorderThickness` with a `BorderBrush` | **paints** four mitred insets at the thickness layout rounded — the same number, because [chrome.cpp](../layout/src/chrome.cpp) rounds it *because it is drawn* |
| `BorderThickness` with no brush | paints nothing, as the runtime paints nothing |
| `Visibility="Collapsed"` | paints nothing, and refuses nothing |
| a `TextBlock`'s text | **positions** a run at the arranged origin, in the family and size the measurement path used; the glyphs are the platform's |

## What refuses, by name

| named no-draw | why nothing here can paint it |
|---|---|
| a partly transparent brush (`0 < alpha < 255`) | needs alpha composition over what is under it, and no recording says what the runtime composes |
| a brush with no colour — `<X.Background><SolidColorBrush/></X.Background>`, or one a `Style` supplied | the parser drops what is inside a property-element brush, so nothing here knows the colour |
| `Opacity` other than 1 | makes the subtree a composed layer; nothing recorded says what it composes with |
| a non-`Stretch`, non-`Left`/`Top` alignment inside a slot bigger than the element | the runtime moves the element inside its slot; the corpus records the slot and the size and never the offset between them |
| an element with no layout storage — a `Shape`, an `Image`, anything under a `Canvas` | it takes no part in layout, so no recorded measurement gives it a rect at all. `Fill` is refused for this reason and no other |
| a case painting the reserved probe-ink colour | ink and background could not be told apart in the round trip |
| a text run in a font the system has not got | substituting another face would put ink where nothing measured it |
| `CornerRadius` on chrome that is actually drawn | the layout core carries the property now — it loads, it round-trips, and it moves nothing, which is the runtime's behaviour — but a rounded corner is not one of the axis-aligned rectangles this pass paints and the round trip recovers, and no recorded measurement gives its pixels. A zero radius, or one on chrome with no brush, covers no pixel and is not refused |
| gradients, `ImageBrush`, shadows, `RenderTransform`, theme animation | the layout core does not carry them, and the new pixel harvest has not yet been connected to implementations of these features |
| `BorderBrush` from markup | no type registers the property, so the three corpus cases that set one fail at load — which is what the oracle answers for them too. The paint path takes a border brush; no markup can currently give it one |

## The gate

The dumps have nothing to be compared *against*, so they are compared to
themselves — the claim is checked rather than the picture, in
[`phase4/scripts/check_render.py`](../../phase4/scripts/check_render.py):

1. **The geometry is the measurement path's.** `render_cases` writes the
   `measure_cases`-shaped tree beside every dump, and the whole corpus of those
   is byte-identical to what `measure_cases` writes. The checker then diffs the
   sidecar's slots and render sizes against that tree, node for node.
2. **The pixels are re-derived independently.** Every rectangle is re-snapped in
   Python from the geometry table — not from the render pass's own rect list —
   painted into a framebuffer with the layout core's own round-half-up edge rule,
   and compared to the dump byte for byte. No tolerance: solid axis-aligned rects
   at whole-pixel edges are exactly invertible, which is the whole reason the
   render pass is restricted to them.
3. **The rectangles are extracted back out.** Each dump is cut into maximal runs
   of one colour and coalesced; every recovered rectangle has to be one the
   layout asked for, or a piece of one a later rectangle painted over. A painted
   shape that was not a clean rectangle reassembles as several and fails.
4. **Two renders are byte-identical**, dumps and sidecars alike.

Over the corpus, natively:

| outcome | cases |
|---|---:|
| painted, round trip exact | 1147 |
| refused by name | 19 |
| failed | 0 |
| not laid out (the measurement path does not load them either) | 11 |

Under Wine the GDI backend applies the same geometry gate: 1058 painted, 119
refused by name, 0 failed, 10 not laid out. Of the refusals, 113 are cases
whose text is in Segoe UI, which is on no host this builds on and has no
metrically identical stand-in, so they refuse rather than substitute a
different font; the other 6 are the unarranged elements the native run refuses
too.

**How much of that is a real rectangle: six cases, twelve rectangles, 608560
pixels.** The rest of the corpus paints nothing at all, and for those the gate
checks the surface is entirely backdrop — which is a real check, and a weak one.
The corpus was authored to measure layout, and it shows: of its 32 `Background`
attributes, seventeen are `Transparent`, fifteen resolve to a partly transparent
theme brush, and six are opaque. Cases with opaque backgrounds, nested and
overlapping, are the cheapest way to make this gate bite harder, and they need no
new probe capability — only new authored markup, whose layout the existing oracle
already answers.

## In a window

`gdi/xaml_window.cpp` paints one case into a real `HWND` under Xvfb and reads the
pixels back off the window rather than off the DIB the paint went through. For
`L7-terminal-0e66f8e18d-s0` — a Terminal subtree whose page background comes from
the theme dictionary, with a red and a blue panel over it — the window read-back
is **byte-identical to the offscreen GDI dump and to the native software dump**.

A screenshot of the X display, taken by ImageMagick while the window was up and
sharing no code with any of the above, recovers:

| colour | on screen | the layout's rectangle |
|---|---|---|
| `#ff0000` | 200x268 at (4,62) | `[0,32,200,300]` |
| `#0000ff` | 200x268 at (204,62) | `[200,32,400,300]` |
| `#f3f3f3` | 400x32 at (4,30) | `[0,0,400,300]`, less what the two panels cover |

(4,30) is where the window manager put the client area. Every size is exact.

## Glyphs are the platform's; positions are ours

The broad Wine round-trip does not consume the focused native glyph oracle, so
it still invents nothing: the backend selects the real font and calls
`ExtTextOutW`, unclipped. The separate strict acceptance test compares the
focused text programs against native glyph pixels and DirectWrite runs.

What that leaves checkable is containment: the box the harvested advances derive
has to contain the ink the platform draws. `gdi/ink_check.cpp` puts that question
to the one font whose metrics *and* glyphs are both available here — Cascadia
Mono, out of the pinned Terminal checkout, the same file phase3's CI already
harvests for the level 7 case that measures in it.

It holds on all six samples, and getting there took drawing the run on this
project's numbers rather than on GDI's. Both of GDI's own placements disagree
with the recorded runtime, in the same direction and for the same reason — it
rounds to whole pixels where the runtime does not:

  * **Vertically.** A top-aligned `ExtTextOutW` puts the baseline wherever GDI's
    glyph cell says, and that cell comes from the font's win metrics, which for
    Cascadia Mono exceed the `hhea` metrics the line box was measured from. At
    size 12 the baseline landed about 2 pixels low and every descender fell out
    of the box. The run is now drawn `TA_BASELINE` at the baseline
    `display_list.cpp` derives from the same harvested ascent.
  * **Horizontally.** GDI rounds each advance to a whole pixel, so six of
    Cascadia Mono's 11.71875 at size 20 walk the pen to 72 where the arrange
    says 70.32. The run is now drawn with an explicit distance array built from
    `TextBlock::ShapedAdvances` — the measurement path's own shaping, kerning
    and snapping included — differenced from rounded *positions* so the error
    stays under a pixel across the whole run instead of accumulating.

So the delegation is narrower than it was, and honest: the imagery is GDI's, and
every position is ours. What remains un-pinned in this backend is what a glyph
looks like. The native render harvest now records that output together with the
exact Segoe UI file identity; consuming those captures in the comparison gate is
the next step.

### 2026-08-11: the same argument, against DirectWrite

The two paragraphs above describe the GDI text path, which the retained scene
renderer replaced with DirectWrite. The replacement kept the *conclusion* --
positions are ours -- as an assertion instead of as a mechanism, and lost it:
it laid the run out through `IDWriteTextLayout`, drew it wherever DirectWrite
put it, and refused the run unless DirectWrite's advances equalled the retained
ones to a ten-thousandth. They cannot be equal. Rule 7 snaps an inline run's
advances to 1/300 of a DIP and DirectWrite does not snap at all, so Cascadia
Mono at size 14 is 8.203333 retained against 8.203125 shaped, and the run is
refused by 0.000208. Ninety-one of the corpus's text cases are inline content.

Three things were wrong at once, and all three are fixed:

  * **The ink tool loaded the font into the wrong subsystem.** `ink_check.cpp`
    called `AddFontResourceExW(..., FR_PRIVATE)`, which is GDI's private list;
    DirectWrite's system collection does not read it. Measured under Wine: after
    a successful `AddFontResourceExW`, `FindFamilyName(L"Cascadia Mono")` on the
    updated system collection returns `exists=0`, while the same file through
    `IDWriteFontSetBuilder1` yields a collection whose one family is
    `Cascadia Mono`. So a real font file was in the process's hand and the only
    subsystem that draws could not see it. `AddPrivateDirectWriteFontFile` now
    hands the file to the collection that matters.
  * **A private face was only reachable through an alias.** The provider looked
    the requested family up in the system collection, then -- only if the
    manifest declared an alias for that exact name -- in the private one. A font
    loaded under the name written in it was therefore unresolvable. It is now
    looked for in the private collection under its own name as well.
  * **Positions were DirectWrite's.** A single-line run is now drawn as an
    explicit `DWRITE_GLYPH_RUN`: glyph indices from the resolved face, one
    advance per codepoint straight out of the display list, origin at the
    retained baseline. This is the distance array of the GDI era, in the API
    that replaced it. A run the layout broke into several lines still goes
    through `IDWriteTextLayout`, because the display list carries advances and
    not where the breaks went, and that path still refuses unless DirectWrite
    reproduces every retained advance -- which is the honest boundary of what
    can be checked from this data.

The ink samples hold, on eight samples now rather than six: the two added ones
set the text through the `Text` property, so the snapped and the unsnapped
advance rules are both drawn and both contained.

A fourth thing was wrong and is fixed with them, because it decided which of
the above could even be seen: rasterizing one run used to *install* the provider
as the layout text provider, so the first case in the directory that carried a
text run handed every later case's measurement to whatever faces the machine
happened to have. Same corpus, different numbers, decided by filename order.
Drawing now takes a provider without installing one; a host that wants the
platform to be the authority still says so by name, as `xamlcore` does.

What still refuses over the corpus is Segoe UI, and it is not a defect. It is
not redistributable, no copy exists on a machine this builds on, and nothing
open is metrically identical to it, so its 113 text cases refuse by name. They
refuse where the refusal belongs now, too: they lay out against the harvested
Segoe UI metrics like every other case and it is the *draw* that has nothing to
draw with, rather than the measurement being handed to a platform that has
never heard of the family.

The fifteen icon cases stopped refusing, and that is worth saying plainly
because it is not a win: they were refusing because the install-on-draw bug
above had handed their measurement to DirectWrite, which has no Segoe icon
family. Measured against the harvested icon metrics they lay out, and they
paint -- without their glyph, exactly as the native run paints them, because
`display_list.cpp` emits no glyph op for a `FontIcon` at all. Drawing one needs
somewhere to put it, and where a glyph sits inside a stretched icon slot is not
in any recording here: real XAML centres it in the icon's template and this
corpus records only the slot. So that gap is a missing recording, not a missing
font -- and the pinned open Uno Fluent Icons file, whose metrics match both
harvested Segoe icon families exactly (2048 units per em, ascent 2048, descent
0, line gap 0, advance 2048 across all 44 codepoints Terminal names), would not
close it. `build_render.py` prepares that font for the provider test, which is
the one thing here that does use it, and keeps it out of the corpus harness's
environment.

One subtlety in the checking. A run's box becomes pixels twice, by two different
rules, and they are not interchangeable: a *fill* snaps each edge to the nearest
pixel, because that is how the runtime rasterises a rectangle, while *containment*
takes every pixel the box overlaps at all (`TouchedRect`). A run measured 70.32
wide covers a third of pixel column 70, so ink there is inside the measured box
even though a fill of the same rectangle stops at column 69. Judging ink by the
fill box calls that pixel an escape and is wrong by up to half a pixel on every
edge; ink a full pixel beyond still fails, which is the part that carries weight.

## Layers

    src/          plain C++17, no Windows, no GDI, built and gated on Linux
      scene.{h,cpp}         retained visual nodes and immutable local display lists
      raster_backend.h      the scene-to-pixels contract and structured failures
      cpu_raster_backend.*  deterministic traversal, clipping and rectangle rasterization
      display_list.{h,cpp}  builds the scene; keeps the temporary flat oracle view
      surface.{h,cpp}       a 32-bit surface and one primitive: an opaque, snapped rect
      case_runner.{h,cpp}   one corpus case, laid out and painted
      render_cases.cpp      the corpus harness
    gdi/          the only code that touches a platform, cross-compiled with mingw
      gdi_target.{h,cpp}    a DIB, a font, and ExtTextOut -- plus the host paint entry
      render_cases_gdi.cpp  the same corpus render, with glyphs
      xaml_window.cpp       one case in a real window, read back off the window
      ink_check.cpp         does the derived box contain the drawn ink

Rectangles are replayed from the retained scene by `CpuRasterBackend` and
rasterised by `src/surface.cpp` on both sides — even under Wine, where the
result is copied into the DIB rather than redrawn through GDI `FillRect`. That
is deliberate: GDI's fill would be a second rasteriser with its own rounding,
and then a disagreement between the two backends would be unattributable. As it
stands the rectangle pixels of a Wine dump and a Linux dump are byte-identical,
so a diff between them is exactly the ink.

## The paint entry a host calls

    // phase3/render/src/display_list.h
    DisplayList Build(const Element& arranged_root, Size surface);

    // phase3/render/src/case_runner.h
    Surface RasterizeDisplayList(const DisplayList&, TextBackend*, Color clear,
                                 Color text_ink,
                                 std::vector<std::string>& text_failures,
                                 std::vector<RenderIssue>& render_issues);

Build once, replay the retained scene as often as the host repaints.
`xaml_window.cpp` is that host in its smallest form: its GDI presenter copies
the CPU backend's surface into the window DC and delegates only text imagery to
GDI. `DesktopWindowXamlSource` should call the same scene boundary rather than
letting projected XAML elements draw into an HDC.

The flat `rects` and `texts` arrays still travel beside the scene only because
the existing acceptance sidecars consume them. Focused tests render the scene
and the flat compatibility view independently and require identical BGRA bytes
for the supported slice. New hosts must consume `SceneSnapshot`, not those
arrays.

## Running the Wine half

    python3 phase3/scripts/build_render.py \
        --cases phase3/xaml-db/cases \
        --fonts "$(python3 phase3/scripts/fetch_measurements.py --fonts)" \
        --theme-resources phase3/xaml-db/theme-resources \
        --ink-font <a .ttf> --window-case L7-terminal-0e66f8e18d-s0

Needs `x86_64-w64-mingw32-g++` and `wine`. Two steps need a display and say so
by name without one: the live window, and the island frame-cache test, whose
layered-child case creates real windows. Everything lands under
`/tmp/openterminal-render` and nothing is committed — dumps least of all, at a
quarter of a gigabyte for one corpus pass.

**Three of those inputs are generated and none of them is optional.** The cases
are `generate_cases.py` output, the fonts are the harvested per-family metrics
from the measurement artifact, and the theme resources are
`extract_winui_theme_resources.py` output. Pointing `--fonts` at
`phase3/xaml-db/fonts` — which holds only the two numbers the corpus solved for
itself, under `derived/` — does not fail: it loads 241 text and icon cases with
`no harvested metrics for the font family "Segoe UI"` and they land in the
checker's *not laid out* column, where a run can report zero failures having
measured nothing. Omitting `--theme-resources` does the same to another 41.

### Why the dump root carries a provenance record

Nothing regenerates the dumps on the gate's behalf: `build_render.py` writes
them and `phase4/tests/test_render_wine.py` reads whatever is in the scratch
directory. Through waves 5 and 6 that gate was green against a dump root
predating both, and the drift it could not see was a real one — wave 5 gave
`FrameworkElement::ArrangeCore` its alignment placement (`render_origin_` in
`phase3/layout/src/element.cpp`), so an element with an explicit size in a
`Stretch` slot moved to the middle of it while its layout slot stayed where it
was. Regenerating on that code failed 628 of 1187 cases with

    /Windows.UI.Xaml.Controls.Border: absolute origin [140.0, 0.0] is not the
    accumulated [0.0, 0.0]

because `check_render.py` was still re-accumulating absolute origins out of slot
origins. It now re-derives each node's parent-local visual origin from the
measured slot, the render size and the declared margin and alignments, which is
the rule the layout core applies and an independent route to the same number.

So the sidecars carry a schema version the checker refuses to read across, and
`build_render.py` writes `provenance.json` into the dump directory it just
recreated: a digest of every source the harness is compiled from, and a digest
of the case corpus. The Wine gate verifies both before it believes a round trip.
See `phase3/scripts/render_provenance.py`.

## Native XAML render-boundary harvest

The Windows measurement workflow now generates eight small paint programs and
runs `xaml_render_probe` twice against the real in-OS `Windows.UI.Xaml`. The
`xaml-render-boundaries-<os-build>` artifact contains, for every program:

- the exact premultiplied BGRA8 bytes returned by `RenderTargetBitmap`;
- arranged and desired sizes plus each layout slot;
- effective element-to-root transforms (three transformed basis points);
- XAML clip bounds, opacity, sibling order and `Canvas.ZIndex`;
- public composition-visual offset, size, anchor, center, scale, rotation,
  opacity, visibility and clip type;
- rasterization scale, OS build and hashes of the relevant system fonts;
- DirectWrite glyph indices, advances, offsets, baselines, measuring mode,
  pixel snapping and system rendering parameters for every explicit text run.

The raw `.bgra` files are generated binary artifacts and are never committed.
`finalize_render_observations.py` validates their dimensions and emits a
deterministic textual manifest with SHA-256 and alpha/color statistics. The
workflow diffs the complete raw and textual output of two independent runs.

These are the appropriate renderer contracts. A D2D/DWrite/D3D/DXGI call log is
not one: it would couple the implementation to a private choice the real XAML
runtime can change while producing identical output. Such traces may later be
used to diagnose a pixel mismatch, but they are explicitly marked absent from
the oracle metadata. `RenderTargetBitmap` is also pre-DWM XAML content, not a
screenshot of the composed desktop.

To generate just the authored input corpus on any platform:

    python3 phase3/scripts/generate_render_cases.py --out /tmp/render-cases

On Windows, after building the probe as the workflow does:

    xaml_render_probe.exe /tmp/render-cases /tmp/render-observations
    python3 phase3/scripts/finalize_render_observations.py /tmp/render-observations

## Strict renderer acceptance

The harvest is an oracle only because `phase4/scripts/check_render_oracle.py`
uses it to accept or reject our renderer. The workflow downloads the exact
native artifact, renders the `cases/` directory carried inside it, and compares:

- every BGRA channel of every pixel, with no tolerance;
- case and node completeness, desired/actual sizes and layout slots;
- effective element-to-root transforms, clips, opacity, visibility and z-order;
- DirectWrite text content, family, size, baseline, advances, glyph indices and
  offsets;
- named refusals and text failures, both of which are acceptance failures even
  if the accidentally blank pixels match.

The report gives the first differing pixel, mismatch bounds, per-channel maximum
delta, both SHA-256 values and boundary-specific structural errors. Missing
output cannot pass vacuously, and malformed oracle data exits as an
infrastructure error rather than as a renderer mismatch.

The comparator is strict today and exits 1 while Wave 6 is incomplete. CI marks
that one step `continue-on-error` so the far-future gate does not block current
layout work; the red step and `native-render-acceptance-<os-build>` report remain
visible. Removing that single workflow line enables enforcement without
changing the test or its expectations.

Equivalent local invocation, given a downloaded native artifact:

    build/render_oracle_cases native-render-oracle/cases /tmp/actual fonts theme-resources
    python3 phase4/scripts/check_render_oracle.py \
        --oracle native-render-oracle --actual /tmp/actual --output acceptance.json

## What the oracle cycle records and what remains

1. **Recorded now: rendered output** — the arranged tree painted by the real
   runtime into a readable BGRA8 surface, per focused case.
2. **Recorded now: the render offset inside a layout slot.** The live-tree probe
   records `TransformToVisual` basis points as well as the layout slot, including
   centred, right-aligned and transformed elements.
3. **Recorded now for the focused overlap case: alpha composition.** Expanding
   this across the fifteen blocked layout cases follows review of the first
   artifact.
4. **Recorded now at the public DirectWrite boundary: glyph runs.** The text
   layout callback records indices, advances, offsets and raster settings. It
   complements the authoritative XAML pixels and makes a mismatch attributable
   without intercepting private calls made inside XAML.
5. **Still needed: the default `TextBlock` foreground**, then corner radii,
   gradients, shadows and opacity layers in Terminal-frequency order.
