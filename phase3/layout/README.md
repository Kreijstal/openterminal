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
| L5 | resources: `x:Key`, `{StaticResource}` | 40 | no oracle yet — see below |
| L7 | Terminal's own pages | 69 | 0 |

L1–L3 is every case that does not need text measurement or a control set —
`Border`, `Grid`, `StackPanel`, and the `FrameworkElement` semantics under all
of them. L7 fails as `the type 'ScrollViewer' is not implemented` and the like,
rather than as wrong numbers, which is the distinction worth keeping: nothing
here is quietly approximate.

L5 is implemented and is the one level with nothing to check it against yet:
the resource cases were authored after the last measurement run, so there are
no recorded numbers for them and inventing some would defeat the point of
having an oracle. In the meantime each case that hides a value behind a
resource is paired with a twin that writes the same value inline, and
[`check_twins.py`](../scripts/check_twins.py) holds the pair to measuring
identically — 17 of the 18 pairs agree here, the last needing a glyph width
that cannot be derived from the recorded measurements. That is
self-consistency, not coverage, and it is reported separately for that reason;
what it does prove is that a resolved `{StaticResource}` reaches the property
with the value the dictionary holds. Four further cases are questions rather
than assertions, and have no twin at all — see
[the corpus README](../xaml-db/README.md#l5-before-the-oracle-has-seen-it).

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

Two checks need no oracle at all, and run from a bare checkout:

    ctest --test-dir /tmp/layout-build      # what a load refuses, and by what name
    python3 phase3/scripts/check_twins.py \
        --cases phase3/xaml-db/cases --results /tmp/layout-results

`measure_cases` takes the same arguments as the oracle probe and writes the same
files, so the two are directly diffable. A single `g++ -std=c++17 src/*.cpp`
also works if CMake is not worth the trouble.

## What it is a port of

The algorithms come from [dotnet/wpf](https://github.com/dotnet/wpf) (MIT), at
commit `2ca037562c207924e53cfcc99286e523d3694de3` — `FrameworkElement`'s
`MeasureCore`/`ArrangeCore`, and `Border`, `StackPanel` and `Grid`'s
`MeasureOverride`/`ArrangeOverride`. WinUI's own layout core is not public;
WPF's is the same design by the same team, and the corpus decides where they
part company.

Two places where they do, both found by running the corpus rather than by
reading:

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

## Deliberate omissions

Not "to do later" in the vague sense — these are the specific things this code
does not do, so that a passing run is not read as more than it is:

- **No text.** `TextBlock` bottoms out in DirectWrite. It is quarantined at L4
  in the corpus for the same reason: text-measurement error would contaminate
  every panel that contains it.
- **No `Canvas`, no `RelativePanel`, no controls.** Only the three types the
  corpus below L4 uses.
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
- **No `Application.Resources`, no merged dictionaries, no theme dictionaries.**
  A resource lookup walks the element and its ancestors and then stops. That is
  where WinUI's own theme resources would be, and it is why
  `{StaticResource SystemControlForegroundBaseHighBrush}` resolves in Terminal
  and not here. A lookup that would have needed one of the three fails by name;
  it never falls back on something plausible.
- **No `{ThemeResource}`, no `{Binding}`, no `{x:Bind}`, no
  `{TemplateBinding}`.** `{StaticResource}` is the only markup extension that
  resolves. Every other one is a named refusal, including on properties where
  the difference would not show in the numbers.
- **No styles, no setters, no control templates.** L5 in the corpus is scoped
  to resource lookup; the rest of what that level names is not started.
- **Resources are literal text, not objects.** A resource holds the string its
  declaring element carried, and resolution hands that string to the same
  property parser an inline attribute would reach. It is why a resolved case
  and its inlined twin cannot drift apart — and it is also why a resource type
  with no textual form is not expressible.
- **The `x:` prefix is matched literally.** Every case binds `x` to the XAML
  language namespace, and this parser assumes that binding rather than
  resolving prefixes. A document that spelled it differently would not be
  understood.
