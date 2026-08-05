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
| L5 | resources: `x:Key`, `{StaticResource}` | 0 | no oracle yet — see below |
| L5 | styles: `Style`, `Setter`, `BasedOn` | 0 | no oracle yet — see below |
| L7 | Terminal's own pages | 69 | **36** |

"Measured" rather than "cases": L0 has eighteen cases and four
measurements, and L5 has ninety cases and none at all. Everything authored
after the last oracle run is pending, not passing — see [the property
system](#the-property-system) and [what is still open](#what-is-still-open).

L1–L3 is every measured case that does not need text measurement or a control
set — `Border`, `Grid`, `StackPanel`, and the `FrameworkElement` semantics
under all of them. The 33 L7 cases that are still red fail as
`the type 'ScrollViewer' is not implemented` and the like, rather than as wrong
numbers, which is the distinction worth keeping: nothing here is quietly
approximate.

A further 285 generated cases — `L1-shape`, `L2-content`, `L3-canvas` and all
of `L5-resources` and `L5-styles` — have no recorded measurement yet and so are
neither passing
nor failing. `Canvas`, `ContentPresenter`, `Path`, `PathIcon` and `Image` are
implemented and every case that measures one is in that set; the two `PathIcon`
subtrees Terminal's own markup supplies are the only witnesses any of them has
today. They are listed under [what is still open](#what-is-still-open).

### What the 33 red L7 cases are waiting for

Ranked by how many they are, since that is the order they are worth doing in:

| blocked on | cases | why it is not done |
|---|---:|---|
| `FontIcon` | 15 | its size is a glyph measured in Segoe MDL2 Assets or Segoe Fluent Icons, and no metrics for either are harvested |
| `ScrollViewer` | 9 | see below |
| `ToolTip`, `Run` | 3 | a templated control whose content is an inline |
| `TextBox`, `Button` | 3 | templated controls with text in them |
| `Thumb`, `ControlTemplate`, `Rectangle` | 3 | applying a control template is not implemented |

Three more were waiting on a `TextBlock` in Segoe UI, and are not any longer:
the two numbers the corpus solves for itself are enough for them, so they match
on a bare checkout along with the rest of L4's derivable half.

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
whether it affects measure, and an effective value is chosen from

| slot | what writes it |
|---|---|
| local | the markup on the element itself |
| style | the element's `Style`, `BasedOn` already merged |
| inherited | the nearest ancestor with a local or style value |
| default | the registration |

in that order. It is ported in shape from
[dotnet/wpf](https://github.com/dotnet/wpf)'s `DependencyObject`; the slots the
references have and this does not are triggers, animation, coercion and a
control's built-in `generic.xaml` style, which are the levels above this one.

That order is the core's own — *"1. Local value 2. Style 3. Built-in style
4. Default value"*, in `CDependencyObject::EvaluateBaseValue` — and WPF's
`BaseValueSourceInternal`, where `Local` is 11, `Style` is 5 and `Inherited` is
2. The consequence worth stating, because it is the one that surprises: **a
style setter beats a value inherited from an ancestor.** In the core there is
no inherited *slot* at all; inheritance is a read-time walk that stops at the
first ancestor whose value is not still the default, and a style setter makes a
value not-default. So the same fact produces both halves of the behaviour —
a style setter shadows what the element would have inherited, and is itself
inherited by everything below.

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

## The resource system

L5 is implemented and is the one level with nothing to check it against yet:
the resource cases were authored after the last measurement run, so there are
no recorded numbers for them and inventing some would defeat the point of
having an oracle. In the meantime each case that hides a value behind a
resource is paired with a twin that writes the same value inline, and
[`check_twins.py`](../scripts/check_twins.py) holds the pair to measuring
identically — 39 of the 40 pairs across both L5 groups agree here, the last
needing a glyph width that cannot be derived from the recorded measurements.
That is
self-consistency, not coverage, and it is reported separately for that reason;
what it does prove is that a resolved `{StaticResource}` reaches the property
with the value the dictionary holds. Four further cases are questions rather
than assertions, and have no twin at all — see
[the corpus README](../xaml-db/README.md#l5-before-the-oracle-has-seen-it).

What a load *refuses* to do has no measurement either, and that is the other
half: [`tests/resources_test.cpp`](tests/resources_test.cpp) holds a failed
lookup to naming the key it could not find, and `ctest` runs it beside the
property tests.

## The style system

`Style`, `Setter`, `BasedOn`, and both routes a style takes to an element:
explicit, behind an `x:Key` and referenced by `Style="{StaticResource K}"`, and
implicit, keyed by `TargetType` and applied to every element of that type in
scope that has no `Style` of its own. [`style.h`](src/style.h) is the whole of
it; the slot it writes into is the one in the table above.

Three things about it are worth stating because they are decisions rather than
consequences:

**Setters are resolved and parsed where they are written.** A setter goes
through the *ordinary* attribute parser against a scratch node of the target
type, so `<Setter Property="Width" Value="60"/>` and `Width="60"` on the
element reach the property through one piece of code. They cannot disagree
about what `60` means, about which types have a `Width`, or about the message
when the value is not a number — which is the same argument the resource system
makes for carrying literals instead of parsed values, one level up. It also
means a setter for a property the `TargetType` does not have is a load failure
at the dictionary rather than a setter that can never fire.

**`BasedOn` is flattened once, at build time.** A style's setter list is the
base's with the derived style's substituted in per property, which is what
`CStyle::CreateMergedBasedOnSetterCollection` builds at seal time. Nothing
downstream walks a chain. A cycle is not expressible here at all: a style
enters its dictionary only once it is finished, so a self-reference fails as an
unresolved key rather than as a loop — the runtime needs its loop check because
its `Style` is a mutable object that can be pointed anywhere before sealing.

**Implicit and explicit are one mechanism with two lookups.** They produce one
`Style` object and one application, and an explicit `Style=` replaces the
implicit style rather than merging with it — the two occupy a single slot.
The lookups differ, deliberately: the implicit key is an *exact* type match
(`ResolveImplicitStyleKeyImpl` hands the element's own class name to a
dictionary probe, and WPF's `FindImplicitStyleResource` passes `this.GetType()`),
while an explicit `Style=` is validated with an is-a check
(`CFrameworkElement::ValidateTargetType` asks `OfTypeByIndex`). Neither of those
is observable in this corpus, because every markup type here is concrete and no
two of them derive from one another — `L5-styles-implicit-derived-type` and
`L5-styles-explicit-derived-target` are the pair that asks the runtime, and this
implementation refuses both by name because it has no abstract `Control` type.

The same split holds for the other half: what the corpus can see is checked by
50 `L5-styles` cases, 22 of them twinned; what it cannot is checked by
[`tests/style_test.cpp`](tests/style_test.cpp), which `ctest` runs. That file
exists for a sharper reason than the resource one. A measurement is a tree of
numbers, so it cannot say *which slot* a value came from — and that is the
entire content of what a style is. An implementation that wrote setters into
the local slot measures identically to this one in every case the corpus can
express, and differs the moment anything clears a local value or replaces a
style.

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

Two checks need no oracle at all, and run from a bare checkout:

    ctest --test-dir /tmp/layout-build      # what a load refuses, and which slot a value is in
    python3 phase3/scripts/check_twins.py \
        --cases phase3/xaml-db/cases --results /tmp/layout-results

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

The style system comes from both at once, and they agree about everything that
matters here: the precedence order (`CDependencyObject::EvaluateBaseValue`,
`EffectiveValueEntry.BaseValueSourceInternal`), the `BasedOn` merge
(`CStyle::CreateMergedBasedOnSetterCollection`, `StyleHelper.ProcessSelfStyles`),
exact-type matching for an implicit style and is-a matching for an explicit
one, and that a style-provided value is inherited by descendants exactly as a
written one is. Where they differ is in the *shape* of the store rather than in
its answers — the core has no inherited slot and reaches the same result with a
read-time walk — and this follows the core, because the walk was already here.

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

- **Text is measured against rules, not a text stack.** `TextBlock` bottoms out
  in DirectWrite, and there is none here: what [`text.cpp`](src/text.cpp) has
  is three rules read off the corpus, plus a font's advance widths. No
  shaping, no kerning, no ligatures, no fallback for a character the metrics do
  not cover. It stays quarantined at L4 for that reason — text-measurement
  error would otherwise contaminate every panel that contains it.
- **No `RelativePanel`, and no control templates.** `ContentPresenter` is here
  because it is a layout type; `Button`, `TextBox`, `ToolTip` and `Thumb` are
  not, because their size is their template's and applying one is not
  implemented. `ContentControl` is the exception, and it is not a real one — it
  is where L0 sets an inherited `FontSize`, and it has no template, so the
  `ContentPresenter` that would carry its `Padding` and content alignment is
  absent rather than modelled. Every measured case that uses it has an empty
  `TextBlock` at the origin, which is where `Left`/`Top` and `Stretch` both land
  it, so none of them can tell the guess from the truth.
  `L0-props-content-stretch` can, and has no measurement yet.
- **No `FontIcon`.** It is the largest single L7 blocker at fifteen cases, and
  every one of them is blocked on icon-font glyph metrics rather than on
  layout. `PathIcon` is implemented because its size comes from a geometry.
- **No `ScrollViewer`.** See [the table above](#what-the-33-red-l7-cases-are-waiting-for).
- **No stroked shapes, and no `Stretch` but `None`.** A stroked shape grows by
  half its thickness on every side and a stretched one is scaled into its
  constraint; no case in the corpus has either, so markup asking for one is
  refused by name.
- **No elliptical arcs in path data.** `A` needs the endpoint-to-centre
  conversion and then the extremes of a rotated arc. Nothing in the corpus has
  one.
- **No `LayoutTransform`.** WPF measures a transformed element by fitting a
  maximal rectangle in local space; none of that is here.
- **The property store has three sources**, local, style and inherited.
  Triggers, animation, coercion and a control's built-in `generic.xaml` style
  are levels above this one; the lookup is written so they slot in beside the
  three rather than being threaded through callers. There is no built-in-style
  layer because nothing here has a control template to carry one.
- **`Grid` star resolution follows WPF's pre-4.7 algorithm**, not the
  `MaxDiscrepancy` rework. The corpus does not distinguish them: it has no
  definition-level `MinWidth`/`MaxWidth`, which is the only thing they disagree
  about.
- **Round-half-to-even is unconfirmed**, and so is whether `UseLayoutRounding`
  inherits. Both are the ported behaviour rather than a measured one.
  `L0-props-rounding-half` and `L0-props-rounding-inherited` are the cases
  authored to settle them, and neither has a measurement yet.
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
- **No control templates, and no triggers.** `Style`, `Setter` and `BasedOn`
  are implemented; `Style.Triggers`, `Setter.Target`, `ControlTemplate`,
  `VisualState` setters and a control's built-in `generic.xaml` style are not.
  A `<Setter>` naming a `Target` is refused by name rather than treated as a
  `Property`.
- **A style is not re-applied after the tree is built.** The runtime resolves
  an implicit style at `CreationComplete` and again on entering a live tree,
  and re-resolves when a `Resources` dictionary is replaced. Here it is
  resolved once, when the element's markup closes, and never again — nothing
  in this corpus moves an element or replaces a dictionary after the load.
  `ClearStyleValues` exists and is tested, because replacing a style is what
  makes the separate slot necessary, but no markup path calls it.
- **`Setter.Value` is resolved eagerly.** The runtime defers a setter's
  `{StaticResource}` until the value is first needed. Here it is resolved when
  the style is parsed, against the dictionaries in scope where the style is
  written. `L5-styles-setter-value-resource-scope` is the case where the two
  could differ, and it is a question rather than a claim.
- **Resources are literal text, not objects.** A resource holds the string its
  declaring element carried, and resolution hands that string to the same
  property parser an inline attribute would reach. It is why a resolved case
  and its inlined twin cannot drift apart — and it is also why a resource type
  with no textual form is not expressible.
- **The `x:` prefix is matched literally.** Every case binds `x` to the XAML
  language namespace, and this parser assumes that binding rather than
  resolving prefixes. A document that spelled it differently would not be
  understood.

## What is still open

Answers this code gives that nothing has yet checked. All of them have
generated cases waiting for the next oracle run, and until that run they are
neither passing nor failing:

| group | cases | the question |
|---|---:|---|
| `L3-canvas` | 63 | does a `Canvas` report its slot or nothing? |
| `L2-content` | 72 | what does `ContentPresenter`'s content alignment default to? |
| `L1-shape` | 60 | are a shape's bounds tight, and is its desired size the right edge or the width? |
| `L0-props` | 14 | does `UseLayoutRounding` inherit, how does a tie at `.5` break, and what does a `ContentControl` do with content it is not stretching? |
| `L5-resources` | 40 | every one of them: the level has no measurement at all |
| `L5-styles` | 50 | the same, and six of them ask a question outright — see below |

The six `L5-styles` cases that carry `oracle_decides` are the ones neither
reference settles for a `XamlReader.Load` with no `Application`:

| case | this implementation's answer | why it might be wrong |
|---|---|---|
| `implicit-own-dictionary` | applies — the outer `Border` is styled by its own dictionary | the core's walk starts at the element, but `{StaticResource}` in this same parser cannot see a dictionary declared below the attribute reading it |
| `implicit-forward-dictionary` | does not apply | the runtime applies styles after the parse, so it may well find a dictionary written below |
| `setter-value-resource-scope` | resolved where the style is written, so `60` | the runtime defers the lookup; a deferred one might pick up the styled element's scope instead, giving `100` |
| `duplicate-setter` | last setter wins, so `100` | the core's lookup says so, but its parser may reject the duplicate first |
| `implicit-derived-type` | refused: no abstract `Control` type | the runtime loads it and should not apply it, by exact-type match |
| `explicit-derived-target` | refused, same reason | the runtime loads it and *should* apply it, by is-a match |

The last two are a deliberate pair: the interesting finding is the runtime
answering them differently, and a run that answered both the same way would mean
one of the two documented rules is not what the source says.

The `L1-shape` answers have two witnesses each already, from Terminal's two
`PathIcon` cases, but both of those geometries start near the origin and
neither distinguishes a tight curve bound from the hull of its control points
by more than a rounding step. The generated ones do.

These land in L1, L2 and L3, which is the range CI gates on, so the next
measured run either keeps the gate green or turns it red on a specific case
with a specific number. That is the intended outcome of authoring them: an
unchecked answer that stays unchecked is worse than one that fails loudly.
