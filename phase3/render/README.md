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
feature, never an approximation. Wave 6 in [the roadmap](../ROADMAP.md) is where
rendered-output probes get recorded; until they exist there is no measurement
that could catch a plausible-looking gradient, so a plausible-looking gradient is
exactly the wrong number the project's standing rules forbid.

The named no-draws are not a to-do list for this directory. They are the work
list for the next oracle cycle — each one names a capability a pixel probe would
have to record before anything here could paint it.

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
| `CornerRadius`, gradients, `ImageBrush`, shadows, `RenderTransform`, theme animation | the layout core does not carry them, and there is no pixel oracle to check them against if it did |
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

Under Wine with the GDI backend the same corpus gives 1035 / 131 / 0 / 11 — the
extra 112 refusals are every case that carries text, because they all measure in
Segoe UI and it is not on this machine.

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

There is no oracle for what a glyph looks like, so none is invented. The Wine
backend selects the real font and calls `ExtTextOutW` at the origin the layout
produced, unclipped — a clip to the run's own box would erase the evidence
whenever the box was wrong.

What that leaves checkable is containment: the box the harvested advances derive
has to contain the ink the platform draws. `gdi/ink_check.cpp` puts that question
to the one font whose metrics *and* glyphs are both available here — Cascadia
Mono, out of the pinned Terminal checkout, the same file phase3's CI already
harvests for the level 7 case that measures in it.

**It does not hold, and that is a finding rather than a bug to paper over.** GDI
rounds a font's per-glyph advances and its ascent and descent to whole pixels;
the runtime the corpus recorded does not. Measured on six samples, the ink runs
up to 2 pixels past the box's bottom on any string with a descender, and up to 2
pixels past its right edge on the widest one, while four of the six stay inside
horizontally. So the delegation is real in both directions: the imagery is GDI's,
and so is a metric rounding that is not the runtime's. Pinning that difference
needs a rendered-output probe, which is the next cycle's work and not this one's.

## Layers

    src/          plain C++17, no Windows, no GDI, built and gated on Linux
      display_list.{h,cpp}  what paints and what refuses -- the only opinionated file
      surface.{h,cpp}       a 32-bit surface and one primitive: an opaque, snapped rect
      case_runner.{h,cpp}   one corpus case, laid out and painted
      render_cases.cpp      the corpus harness
    gdi/          the only code that touches a platform, cross-compiled with mingw
      gdi_target.{h,cpp}    a DIB, a font, and ExtTextOut -- plus the host paint entry
      render_cases_gdi.cpp  the same corpus render, with glyphs
      xaml_window.cpp       one case in a real window, read back off the window
      ink_check.cpp         does the derived box contain the drawn ink

Rectangles are rasterised by `src/surface.cpp` on both sides — even under Wine,
where the fills go into the DIB's own memory rather than through `FillRect`.
That is deliberate: GDI's fill would be a second rasteriser with its own
rounding, and then a disagreement between the two backends would be
unattributable. As it stands the rectangle pixels of a Wine dump and a Linux dump
are byte-identical, so a diff between them is exactly the ink.

## The paint entry a host calls

    // phase3/render/src/display_list.h
    DisplayList Build(const Element& arranged_root, Size surface);

    // phase3/render/gdi/gdi_target.h
    void Paint(HDC destination, int x, int y, const DisplayList&, Color ink,
               std::vector<std::string>& failures);

Build once, paint as often as the host repaints. `xaml_window.cpp` is that host
in its smallest form — `WM_PAINT` calls `Paint` with the window's DC — and it is
the shape `DesktopWindowXamlSource` island hosting needs: a display list is a
value, and the DC it lands on is the caller's business.

## Running the Wine half

    python3 phase3/scripts/build_render.py \
        --cases phase3/xaml-db/cases --fonts <a fonts directory> \
        --ink-font <a .ttf> --window-case L7-terminal-0e66f8e18d-s0

Needs `x86_64-w64-mingw32-g++` and `wine`; the window step also needs a display.
Everything lands under `/tmp/openterminal-render` and nothing is committed —
dumps least of all, at a quarter of a gigabyte for one corpus pass.

## What the next oracle cycle would have to record

Each named no-draw above is a probe capability that does not exist yet. In the
order that would unblock the most:

1. **A rendered-output probe at all** — the arranged tree painted by the real
   runtime into a readable surface, per case. Everything else here is downstream
   of that one capability.
2. **The render offset inside a layout slot.** The corpus records the slot and
   the render size and never the offset between them, which is why a centred or
   right-aligned element that does not fill its slot is refused. One recorded
   offset per alignment would settle it, and it needs no pixels — a
   `TransformToVisual` reading would do.
3. **Alpha composition.** What the runtime puts in a pixel where a partly
   transparent brush meets what is under it: premultiplied or not, and rounded
   which way. Fifteen corpus cases are blocked on this one number.
4. **Glyph metrics as the runtime lays them out**, against the same font this
   backend hands to GDI. The ink check above measures a two-pixel disagreement
   and cannot say which side is right.
5. **What colour a `TextBlock` with no `Foreground` paints in.** The default
   comes from a control style this core does not apply, so the dumps use a
   reserved probe ink and claim nothing about colour.
6. **Corner radii, gradients, shadows and opacity layers**, in that order of how
   often Terminal's own markup asks for them.
