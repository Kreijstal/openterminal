# Layout core

A native reimplementation of the XAML layout contract, checked against
measurements recorded from the real `Windows.UI.Xaml`.

It builds and runs anywhere — no WinRT, no Windows, no Wine. That is the point:
the expectations were captured once by [the probe](../harness/xaml_probe.cpp) on
`windows-latest`, and everything after that is a plain native binary diffed
against a JSON file. The loop is seconds long and runs on a Linux laptop.

## Where it stands

Against build `10.0.26100.33158`:

| level | subject | cases | matching |
|-------|---------------------------------------------|------:|---------:|
| L0 | property system | 4 | 2 |
| L1 | one element: explicit size, margin | 72 | **72** |
| L2 | one parent, one child: alignment × margin × sizing | 192 | **192** |
| L3 | panels: `StackPanel`, `Grid` | 132 | **132** |
| L4 | text: `TextBlock` | 72 | needs the font metrics — see below |
| L7 | Terminal's own pages | 69 | **33** |

L1–L3 is every case that does not need text measurement or a control set —
`Border`, `Grid`, `StackPanel`, and the `FrameworkElement` semantics under all
of them. The 36 L7 cases that are still red fail as
`the type 'ScrollViewer' is not implemented` and the like, rather than as wrong
numbers, which is the distinction worth keeping: nothing here is quietly
approximate.

A further 195 generated cases — `L1-shape`, `L2-content`, `L3-canvas` — have no
recorded measurement yet and so are neither passing nor failing. They are
listed under [what is still open](#what-is-still-open).

### What the 36 red L7 cases are waiting for

Ranked by how many they are, since that is the order they are worth doing in:

| blocked on | cases | why it is not done |
|---|---:|---|
| `FontIcon` | 15 | its size is a glyph measured in Segoe MDL2 Assets or Segoe Fluent Icons, and no metrics for either are harvested |
| `ScrollViewer` | 9 | see below |
| `TextBlock` in Segoe UI | 3 | implemented; needs [the harvested font metrics](../xaml-db/fonts/) |
| `ToolTip`, `Run` | 3 | a templated control whose content is an inline |
| `TextBox`, `Button` | 3 | templated controls with text in them |
| `Thumb`, `ControlTemplate`, `Rectangle` | 3 | applying a control template is not implemented |

`ScrollViewer` is the one that is understood and still not done. Its layout is
its template's — a content presenter and two scroll bars in a grid — and the
three recorded cases do not agree with each other under any single reading of
it. Two of them give the viewer exactly its content's desired size plus its own
padding, with the content then stretched to the viewport, which is what a
viewer with both scroll directions disabled does. The third asks for sixteen
more pixels in each axis, exactly one scroll bar's worth per axis, and arranges
its content at the content's own size rather than the viewport's — which is
what a viewer with both directions *enabled* does. Neither case sets a
scrollbar visibility, and no other property distinguishes them. Implementing
one reading would make six cases pass and three produce wrong numbers, which is
a worse result than nine cases that say what is missing.

L4 is implemented and is the one level whose result depends on a file that is
not in the repository. Text measurement needs Segoe UI's metrics, those metrics
are harvested on the Windows runner, and without them the text cases fail with
`no harvested metrics for the font family "Segoe UI"`. With the two numbers that
can be solved from the recorded measurements alone — the baseline-to-baseline
distance and the advance width of `M` — the 36 cases that depend on nothing
else match exactly. See [the fonts directory](../xaml-db/fonts/) and
[`text.cpp`](src/text.cpp).

## Running it

    cmake -S phase3/layout -B /tmp/layout-build && cmake --build /tmp/layout-build
    python3 phase3/scripts/fetch_measurements.py          # prints a directory
    python3 phase3/scripts/fetch_measurements.py --fonts  # and the font metrics
    /tmp/layout-build/measure_cases phase3/xaml-db/cases /tmp/layout-results <fonts>
    python3 phase3/scripts/check_layout.py \
        --expected <that directory> --actual /tmp/layout-results --levels L1,L2,L3

The font metrics argument is optional and defaults to `<cases>/../fonts`.

`measure_cases` takes the same arguments as the oracle probe and writes the same
files, so the two are directly diffable. A single `g++ -std=c++17 src/*.cpp`
also works if CMake is not worth the trouble.

## What it is a port of

`FrameworkElement`'s `MeasureCore`/`ArrangeCore`, and `Border`, `StackPanel` and
`Grid`'s `MeasureOverride`/`ArrangeOverride`, come from
[dotnet/wpf](https://github.com/dotnet/wpf) (MIT), at commit
`2ca037562c207924e53cfcc99286e523d3694de3`. WPF's layout is the same design by
the same team, and the corpus decides where the two part company.

The types added since — `Canvas`, `ContentPresenter`, `Image`, `Path`,
`PathIcon`, and the panel chrome and spacing on `Grid` and `StackPanel` — come
from [microsoft/microsoft-ui-xaml](https://github.com/microsoft/microsoft-ui-xaml)
(MIT) instead, at commit `188f602b27cdb47572b28c380e9c087b02e1ccee`. That
repository publishes the XAML core itself under `dxaml/xcp/core/core/elements`,
which is the same code the oracle is running rather than a sibling of it, so
where it is available it is the better source. It also confirms the two
divergences below directly: `CFrameworkElement::MeasureCore` layout-rounds
unconditionally, and `CGrid` redistributes its rounding remainder only across
star definitions.

Three places where the ported source and the recorded oracle disagree, all
found by running the corpus rather than by reading:

**Layout rounding is on.** WPF leaves `UseLayoutRounding` off unless asked;
WinUI has it on. A `2*`/`*` split of 100 arranges as 67 and 33, not 66.6667 and
33.3333. Without it, four L3 cases disagree — and they are the only four whose
star split does not divide evenly, which is what made the cause obvious.

**Grid's rounding remainder goes to star definitions only.** After rounding each
column, the columns can add up to slightly less than the Grid, and the shortfall
is redistributed. Ported as written, that redistribution also fires for a Grid
whose columns are all `Auto` or fixed — which legitimately leave the Grid partly
empty — and grew them by a pixel each. Sixteen L3 cases said otherwise, all of
them star-free.

**`Canvas` reports no arranged size.** `CCanvas::ArrangeOverride` returns
`finalSize`, which would make a stretched Canvas report its slot as its
`ActualWidth`. All three recorded sizes of Terminal's `SelectionCanvas` say the
Canvas is zero by zero, including the two whose slot is finite and non-empty —
so it is not a rounding difference or an artefact of one odd constraint. This
is the one divergence with a single witness, and the 63 pending `L3-canvas`
cases exist to confirm it or narrow it.

## Deliberate omissions

Not "to do later" in the vague sense — these are the specific things this code
does not do, so that a passing run is not read as more than it is:

- **No text.** `TextBlock` bottoms out in DirectWrite. It is quarantined at L4
  in the corpus for the same reason: text-measurement error would contaminate
  every panel that contains it.
- **No controls, and no control templates.** `ContentPresenter` is here because
  it is a layout type; `Button`, `TextBox`, `ToolTip` and `Thumb` are not,
  because their size is their template's and applying one is not implemented.
- **No `FontIcon`.** It is the largest single L7 blocker at fifteen cases, and
  every one of them is blocked on icon-font glyph metrics rather than on
  layout. `PathIcon` is implemented because its size comes from a geometry.
- **No `ScrollViewer`.** See [the table above](#what-the-36-red-l7-cases-are-waiting-for).
- **No stroked shapes, and no `Stretch` but `None`.** A stroked shape grows by
  half its thickness on every side and a stretched one is scaled into its
  constraint; no case in the corpus has either, so markup asking for one is
  refused by name.
- **No elliptical arcs in path data.** `A` needs the endpoint-to-centre
  conversion and then the extremes of a rotated arc. Nothing in the corpus has
  one.
- **No `LayoutTransform`.** WPF measures a transformed element by fitting a
  maximal rectangle in local space; none of that is here.
- **No property system.** Values are plain fields, so L0 only passes where the
  answer does not depend on inheritance or precedence.
- **`Grid` star resolution follows WPF's pre-4.7 algorithm**, not the
  `MaxDiscrepancy` rework. The corpus does not distinguish them: it has no
  definition-level `MinWidth`/`MaxWidth`, which is the only thing they disagree
  about.
- **Round-half-to-even is unconfirmed.** No case in the corpus lands on an exact
  half, so the tie-break is the ported behaviour rather than a measured one.

## What is still open

Three answers this code gives that nothing has yet checked. All three have
generated cases waiting for the next oracle run, and until that run they are
neither passing nor failing:

| group | cases | the question |
|---|---:|---|
| `L3-canvas` | 63 | does a `Canvas` report its slot or nothing? |
| `L2-content` | 72 | what does `ContentPresenter`'s content alignment default to? |
| `L1-shape` | 60 | are a shape's bounds tight, and is its desired size the right edge or the width? |

The `L1-shape` answers have two witnesses each already, from Terminal's two
`PathIcon` cases, but both of those geometries start near the origin and
neither distinguishes a tight curve bound from the hull of its control points
by more than a rounding step. The generated ones do.

These land in L1, L2 and L3, which is the range CI gates on, so the next
measured run either keeps the gate green or turns it red on a specific case
with a specific number. That is the intended outcome of authoring them: an
unchecked answer that stays unchecked is worse than one that fails loudly.
