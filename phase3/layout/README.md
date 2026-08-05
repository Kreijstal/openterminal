# Layout core

A native reimplementation of the XAML layout contract, checked against
measurements recorded from the real `Windows.UI.Xaml`.

It builds and runs anywhere — no WinRT, no Windows, no Wine. That is the point:
the expectations were captured once by [the probe](../harness/xaml_probe.cpp) on
`windows-latest`, and everything after that is a plain native binary diffed
against a JSON file. The loop is seconds long and runs on a Linux laptop.

## Where it stands

Against build `10.0.26100.33158`:

| level | subject | measured | matching |
|-------|---------------------------------------------|------:|---------:|
| L0 | property system | 4 | **4** |
| L1 | one element: explicit size, margin | 72 | **72** |
| L2 | one parent, one child: alignment × margin × sizing | 192 | **192** |
| L3 | panels: `StackPanel`, `Grid` | 132 | **132** |
| L4 | text: `TextBlock` | 72 | **36** without a font — see below |
| L7 | Terminal's own pages | 69 | 3 |

"Measured" rather than "cases": L0 has eighteen cases and four
measurements. The other fourteen were authored after the last oracle run and
are pending, not passing — see [the property system](#the-property-system).

L1–L3 is every case that does not need text measurement or a control set —
`Border`, `Grid`, `StackPanel`, and the `FrameworkElement` semantics under all
of them. L7 is 3 rather than 0 because `x:Name` is now accepted and dropped
instead of rejected; the other 66 fail as `the type 'ScrollViewer' is not
implemented` and the like, rather than as wrong numbers, which is the
distinction worth keeping: nothing here is quietly approximate.

L4 is implemented and is the one level whose result depends on a font. Two of
the numbers inside that font can be solved from the recorded measurements
alone — the baseline-to-baseline distance and the advance width of `M` — and
those are committed, so the 36 cases that depend on nothing else match on a
checkout with no Segoe UI and no artifact:

    measure_cases phase3/xaml-db/cases /tmp/results phase3/xaml-db/fonts/derived

The other 36 measure a word and a pangram, which pin the sum of several
advances and not any one of them. They fail naming the character they have no
advance for, until the harvested metrics are fetched. See
[the fonts directory](../xaml-db/fonts/) and [`text.cpp`](src/text.cpp).

L0 depends on the same file for the same reason: an inherited `FontSize` is
only visible as a line height, so the case that measures one needs a font. With
no metrics loaded it fails as `no harvested metrics` and L0 reads 3 of 4.

## The property system

Values are not fields. [`property.h`](src/property.h) is a dependency-property
store: each property is registered once with a default, whether it inherits and
whether it affects measure, and an effective value is chosen from a local value,
then an ancestor's, then the default. It is ported in shape from
[dotnet/wpf](https://github.com/dotnet/wpf)'s `DependencyObject`, cut down to
the two sources the corpus can currently see — the slots WPF has between them
are styles, triggers, animation and coercion, which are the levels above this
one.

The distinction it exists for is *unset* against *set to the default*.
`<TextBlock/>` inside a `FontSize="22"` control measures at 22, and
`<TextBlock FontSize="14"/>` inside the same control measures at 14 even though
14 is also the registered default. A field cannot hold both of those, which is
why both L0 failures were the same failure.

What markup can name is decided by the registry rather than by a chain of type
tests, which is where `Opacity` on any `UIElement` and `Grid.Column` on anything
at all come from, and where `FontSize` on a `StackPanel` is refused — with the
message the real runtime gives, which is the whole reason the corpus has that
case.

Fourteen of L0's eighteen cases have no measurement yet. They are the questions
the four measured ones leave open — precedence when a local value equals the
default, inheritance through an element that does not declare the property, the
nearest of two ancestors — and three of them settle things this code currently
guesses at: whether `UseLayoutRounding` inherits, and how a tie at exactly `.5`
breaks. Until the next oracle run they are pending, and pending is not passing.

Two things the property system does that no case can ever check, because a
measurement is a tree of numbers: that a value was inherited rather than copied,
and that clearing one restores the ancestor rather than the default.
[`tests/property_test.cpp`](tests/property_test.cpp) is where those live; `ctest`
runs it.

## Running it

    cmake -S phase3/layout -B /tmp/layout-build && cmake --build /tmp/layout-build
    ctest --test-dir /tmp/layout-build                    # the property system
    python3 phase3/scripts/fetch_measurements.py          # prints a directory
    python3 phase3/scripts/fetch_measurements.py --fonts  # and the font metrics
    /tmp/layout-build/measure_cases phase3/xaml-db/cases /tmp/layout-results <fonts>
    python3 phase3/scripts/check_layout.py \
        --expected <that directory> --actual /tmp/layout-results --levels L0,L1,L2,L3

The font metrics argument is optional and defaults to `<cases>/../fonts`. Pass
`phase3/xaml-db/fonts/derived` instead to run text against the numbers the
corpus solved for itself; the two directories are never mixed, and a directory
holding two sets of metrics for one family is refused rather than resolved.

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

- **Text is measured against rules, not a text stack.** `TextBlock` bottoms out
  in DirectWrite, and there is none here: what [`text.cpp`](src/text.cpp) has
  is three rules read off the corpus, plus a font's advance widths. No
  shaping, no kerning, no ligatures, no fallback for a character the metrics do
  not cover. It stays quarantined at L4 for that reason — text-measurement
  error would otherwise contaminate every panel that contains it.
- **No `Canvas`, no `RelativePanel`, no control set.** Only the types the corpus
  uses.
  `ContentControl` is here for one reason — it is where L0 sets an inherited
  `FontSize` — and it is not a real one: it has no template, so the
  `ContentPresenter` that would carry its `Padding` and content alignment is
  absent rather than modelled. Every measured case that uses it has an empty
  `TextBlock` at the origin, which is where `Left`/`Top` and `Stretch` both land
  it, so none of them can tell the guess from the truth.
  `L0-props-content-stretch` can, and has no measurement yet.
- **No `LayoutTransform`.** WPF measures a transformed element by fitting a
  maximal rectangle in local space; none of that is here.
- **The property store has two sources**, local and inherited. Styles, triggers,
  animation and coercion are levels above this one; the lookup is written so
  they slot in between rather than being threaded through callers.
- **`Grid` star resolution follows WPF's pre-4.7 algorithm**, not the
  `MaxDiscrepancy` rework. The corpus does not distinguish them: it has no
  definition-level `MinWidth`/`MaxWidth`, which is the only thing they disagree
  about.
- **Round-half-to-even is unconfirmed**, and so is whether `UseLayoutRounding`
  inherits. Both are the ported behaviour rather than a measured one.
  `L0-props-rounding-half` and `L0-props-rounding-inherited` are the cases
  authored to settle them, and neither has a measurement yet.
