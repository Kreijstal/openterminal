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
| L5 | resources: `x:Key`, `{StaticResource}`, `{ThemeResource}`; the `x:` directives | 0 | no oracle yet — see below |
| L5 | styles: `Style`, `Setter`, `BasedOn` | 0 | no oracle yet — see below |
| L7 | Terminal's own pages | 69 | **33**, down from 36 — see below |

"Measured" rather than "cases": L0 has eighteen cases and four measurements,
L5 has a hundred and twenty-six and none at all, and L7 has ninety cases against
sixty-nine measurements — the twenty-one the application dictionary unblocked
are newer than the last oracle run. Everything authored after that run is
pending, not passing — see [the property system](#the-property-system) and [what
is still open](#what-is-still-open).

### Why L7 went from 36 to 33

Nothing measures differently. Six of the sixty-nine recorded measurements no
longer have a case, and three of those six were matching.

A harvested case is identified by the hash of its markup, and the harvester
emits *maximal* loadable subtrees — the largest subtree with no blocker, never
the ones inside it. Two subtrees in `MyPage.xaml` used to be maximal because
their parent `Grid` was blocked by a `{ThemeResource}`. The application
dictionary answers that key, so the parent is loadable now, and the two children
stopped being candidates: one thirteen-element `Grid` stands where a
three-element `StackPanel` and a six-element `Grid` did.

That larger `Grid` contains a `TextBox`, so this implementation cannot load it,
and it has no measurement yet either. The three matching cases are inside
something we will match again the moment `TextBox` is — the coverage moved
behind a bigger door, it did not go away. It is worth stating plainly rather
than smoothing over, because a number that falls is exactly the kind of thing a
summary tends to lose.

L1–L3 is every measured case that does not need text measurement or a control
set — `Border`, `Grid`, `StackPanel`, and the `FrameworkElement` semantics
under all of them. Of the 36 L7 measurements that are still red, thirty fail as
`the type 'ScrollViewer' is not implemented` and the like, rather than as wrong
numbers, and the other six are the recordings whose case the dictionary absorbed
— which is the distinction worth keeping: nothing here is quietly approximate.

A further 562 generated cases — `L1-shape`, `L2-content`, `L3-canvas`,
`L3-scroll`, `L4-icon`, `L4-source` and all of `L5` — have no recorded
measurement yet and so are neither passing nor failing.
`Canvas`, `ContentPresenter`, `Path`, `PathIcon` and `Image` are implemented and
every case that measures one is in that set; the two `PathIcon` subtrees
Terminal's own markup supplies are the only witnesses any of them has today.
`ScrollViewer` and `FontIcon` are the other kind: 235 of those cases measure a
type this code refuses outright, and they were authored to make implementing it
possible. They are listed under [what is still
open](#what-is-still-open).

### What the 36 red L7 cases are waiting for

Ranked by how many they are, since that is the order they are worth doing in:

| blocked on | cases | why it is not done |
|---|---:|---|
| `FontIcon` | 15 | its size is a glyph measured in Segoe MDL2 Assets or Segoe Fluent Icons — the harvest now reads both, and `L4-icon` asks what the size is made of, but neither has been measured yet. Their *recorded* numbers are worse than missing: the probe was dropping non-ASCII out of a case file, so all fifteen measured an empty `Glyph`. Fixed, and the next run's L7 digest moves because of it |
| `ScrollViewer` | 9 | see below |
| a measurement whose case was absorbed | 6 | [see above](#why-l7-went-from-36-to-33); the next oracle run retires them |
| `ToolTip`, `Run` | 3 | a templated control whose content is an inline |
| `Thumb`, `ControlTemplate`, `Rectangle` | 3 | applying a control template is not implemented |

The `TextBox` row that used to be here is gone for the same reason the six
appeared: its three cases were one of the two absorbed subtrees.

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

That is a question for the oracle rather than for a reader of the WinUI source,
and [`L3-scroll`](../xaml-db/README.md#scrollviewer-sizes-itself-two-different-ways)
is now 166 cases asking it: the full visibility cross product with the viewer
sized and unsized, scroll mode against visibility, padding, the overflow
boundary one axis at a time, and six Border-only replicas of the three recorded
shapes. Once those are measured this type is implementable from evidence, and
until they are it stays refused. The cases land in L3, which CI gates on, so the
next measured run turns that gate red on 166 cases that all say
`the type 'ScrollViewer' is not implemented` — the cost of asking, and the
reason to do the implementing straight after.

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

`ctest` also runs one test that is not about this code at all.
[`tests/probe_text_test.cpp`](tests/probe_text_test.cpp) covers
[`phase3/harness/json_text.h`](../harness/json_text.h), which is how the *oracle*
probe reads text out of a case file. The probe only builds on Windows, so
nothing in it could be checked here — and the part of it that is plain text
handling was silently dropping every non-ASCII character, which is how fifteen
recorded `FontIcon` measurements came to be measurements of an empty glyph. The
code moved into a dependency-free header so that this suite could hold it.

[`tests/error_hygiene_test.cpp`](tests/error_hygiene_test.cpp) is there for the
same reason and a second probe bug. WinRT restricted error info is per-thread
global state, so a description left behind by one case was being served to a
later one that failed with the same `HRESULT`: eleven of the last run's 19
recorded failures name a resource key their own markup never mentions. The probe
now clears that state before every load, and
[`phase3/harness/error_hygiene.h`](../harness/error_hygiene.h) is the check that
the clearing held — a message naming a key the markup does not contain cannot be
about this case. It is one-sided by construction, since a key that *is* present
proves nothing. The contamination and what it costs are written up in
[the xaml-db README](../xaml-db/README.md#recorded-error-messages-before-run-31017111065-are-not-evidence).

## The resource system

L5 is implemented and is the one level with nothing to check it against yet:
the resource cases were authored after the last measurement run, so there are
no recorded numbers for them and inventing some would defeat the point of
having an oracle. In the meantime each case that hides a value behind a
resource is paired with a twin that writes the same value inline, and
[`check_twins.py`](../scripts/check_twins.py) holds the pair to measuring
identically — 56 of the 58 pairs across all five L5 groups and `L4-source` agree
here, the other two needing glyph widths that cannot be derived from the
recorded measurements. That is
self-consistency, not coverage, and it is reported separately for that reason;
what it does prove is that a resolved `{StaticResource}` reaches the property
with the value the dictionary holds. Four further cases are questions rather
than assertions, and have no twin at all — see
[the corpus README](../xaml-db/README.md#l5-before-the-oracle-has-seen-it).

What a load *refuses* to do has no measurement either, and that is the other
half: [`tests/resources_test.cpp`](tests/resources_test.cpp) holds a failed
lookup to naming the key it could not find, and `ctest` runs it beside the
property tests.

### The application dictionary

A lookup no longer stops at the root of the markup. Past it is one more
dictionary, the one no markup declares: in a running Terminal it is what
`<XamlControlsResources/>` puts in `Application.Resources`, and here it is
WinUI 2.8.4's half of that, extracted from the pinned open source by
[`extract_winui_theme_resources.py`](../scripts/extract_winui_theme_resources.py)
and loaded at startup on the same convention the fonts use — a directory beside
the corpus, an optional argument to override it, and nothing at all on a bare
checkout.

`{ThemeResource}` resolves too, by exactly the code `{StaticResource}` does.
The two differ in *when*: a `ThemeResource` is re-evaluated when the
application's theme changes and a `StaticResource` is frozen at parse time.
Nothing here ever changes theme — a case pins one and is measured under it — so
here they are one operation, and resolving them by one path is what keeps a
`{ThemeResource}` case comparable to its inlined twin.

Two things this does *not* do, and both matter more than they look:

**A resource whose value has no textual form is not loaded at all.** The
dictionary holds 2,852 keys for the `Light` theme and this loads 2,208 of them.
The other 644 are `Style`s, `ControlTemplate`s, converters and brushes built on
`SystemAccentColor` — a colour the OS supplies and the source does not have.
They are dropped at load rather than stored as unusable entries, so a lookup
for one fails with "not found", the same as a key that does not exist. Storing
them would mean resolving to text no property can read, which is a worse
failure and a later one.

**A `Color` still cannot supply a `Background`.** The dictionary is full of both
colours and the brushes made from them, spelled identically —
`TextFillColorPrimary` is a `Color` and `TextFillColorPrimaryBrush` is a brush
on it. WinUI refuses the first where a `Brush` is wanted, and so does this;
without that check the database would happily resolve markup the runtime
rejects, which would turn a shared dictionary from a source of coverage into a
source of false green.

A brush *is* loadable, and only because of what a brush's attribute form is: a
colour. `Background="{ThemeResource ControlFillColorDefaultBrush}"` and
`Background="#B3FFFFFF"` reach the property by the same path and through the
same parser, which is the identity the whole resource design rests on. The
extractor follows each brush's `Color` reference to a literal and records where
it landed; a brush whose colour it could not reach is one of the 644.

The theme is per case, from the case's own `environment.theme`. That is more
than the probe does — the probe takes whatever theme its XAML host defaults to
and does not pin one, which is a real determinism hole on the oracle side and
is named here rather than mirrored. It costs nothing today: `Default` and
`Light` differ in 106 colours and in exactly one other value,
`InfoBadgeIconHeight`, which nothing in the corpus measures.

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

## The x: directives

`x:` names are not properties. They instruct the loader, so the property
registry cannot judge them — it would report `x:Load` as a missing member of
`Border`, which is the wrong reason for the right refusal and no reason at all
for the ones that work. [`src/xdirectives.cpp`](src/xdirectives.cpp) takes the
whole set off an element's attributes before the registry sees the rest, which
is what lets the registry keep saying "not a property of this type" and mean it.

**The primitives as elements.** `<Border.Width><x:Double>60</x:Double></Border.Width>`
is what `Width="60"` is. Terminal writes 37 of these: 26 keyed entries in
resource dictionaries, which already worked, and 11 as the content of a property
element — `<ToggleButton.Tag>` with an `<x:Int32>`, `<DiscreteObjectKeyFrame.Value>`
with an `<x:Boolean>` — on properties whose declared type is `object` and where
an attribute would supply a string. Both forms now go through the same
`MakeResource`, so the same type check runs on both: an `x:String` does not
satisfy a `Width` whether it came out of a dictionary or was written in place.
The `L5-xprimitives` cases twin each one against the attribute spelling.

**Deferral.** `x:Load="False"` and `x:DeferLoadStrategy="Lazy"` describe an
element that is not created. Here it is absent: not attached to its parent,
measured by nothing, occupying no slot — and nothing can bring it back, because
both directives are realised by a code-behind asking for the element by name and
a `XamlReader.Load` has none. **That is a provisional reading.** x:Load is a
compiled-markup feature, the runtime may honour it, ignore it or refuse the
markup, and those are three different trees; `L5-xdirectives-*` asks, and
[`tests/xdirectives_test.cpp`](tests/xdirectives_test.cpp) pins what we do in
the meantime so that "provisional" means written down rather than drifting.

**`x:Uid`.** A uid is a key into a localised string table, and a standalone load
has no resource map — so by default a uid resolves to nothing and sets nothing,
which is what both this and the oracle probe do. A table can be supplied:
[`distil_resw_strings.py`](../scripts/distil_resw_strings.py) reads a pinned
Terminal checkout's `.resw` files into `uid -> property -> value`, and
`measure_cases` takes it as a fifth argument, after the fonts and the
application dictionary. No corpus case
uses one, because a case measured against a table one side has and the other
does not would disagree by construction.

Precedence — whether a uid's value beats a local attribute — is a guess, and the
one this makes is that it does, on the reading that a directive whose purpose is
translation must beat the author it is translating. Nothing documents it and no
standalone case can ask it, because there is no way to put a resource map in
front of the oracle from markup alone.

Everything else stays a named refusal. `x:Bind`, `x:DataType`, `x:Class`,
`x:FieldModifier` and `x:Key` outside a dictionary fail by their own name, and
`x:Name` is dropped as it always was.

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

A fourth argument, the application dictionary, follows the same convention and
defaults to `<cases>/../theme-resources`. It is generated rather than committed,
so materialize it first — once, from a WinUI 2.8.4 checkout:

    git clone --filter=blob:none https://github.com/microsoft/microsoft-ui-xaml /tmp/winui
    git -C /tmp/winui checkout --detach 4aa80ad6d272241a6a603f85507063e9fb6bcf92
    python3 phase3/scripts/extract_winui_theme_resources.py /tmp/winui \
        --out phase3/xaml-db/theme-resources/winui-2.8.4.json

Without it, `measure_cases` says `theme resources loaded: 0` and every lookup
that would have reached the dictionary fails naming its key — which is the state
this was in before, and is still a correct run rather than a broken one.

A fifth argument supplies the table `x:Uid` resolves against. It is last because
it is the only one of the three with no default: a run that silently found one
would measure markup the oracle cannot, and every uid case would disagree for a
reason nothing reported.

    python3 phase3/scripts/distil_resw_strings.py /tmp/windows-terminal \
        --out /tmp/terminal-strings.json
    /tmp/layout-build/measure_cases phase3/xaml-db/cases /tmp/layout-results \
        phase3/xaml-db/fonts/derived phase3/xaml-db/theme-resources \
        /tmp/terminal-strings.json

Two checks need no oracle at all, and run from a bare checkout:

    ctest --test-dir /tmp/layout-build      # what a load refuses, and by what
                                            # name, which slot a value is in,
                                            # what the oracle probe reads out of
                                            # a case file, and whether a message
                                            # it recorded is about that case
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
  Two things that were missing are not any more: CI harvests both icon fonts
  for the codepoints Terminal names, and `L4-icon` asks what a `FontIcon`'s size
  is actually made of instead of leaving it to be assumed. Neither has been
  measured yet, and implementing this before they have would be inventing the
  rule and the metrics in the same commit.
- **No `ScrollViewer`.** See [the table above](#what-the-33-red-l7-cases-are-waiting-for).
  `L3-scroll` is the evidence being gathered; nothing here reads it yet.
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
- **`Application.Resources` holds WinUI 2's half and not the OS's.** A lookup
  walks the element, its ancestors, and then the extracted dictionary. What is
  missing from the tail of that chain is the OS's own `Windows.UI.Xaml`
  dictionary, which is closed: 28 of the keys Terminal names live only there,
  the accent-colour palette among them. `{StaticResource
  SystemControlForegroundBaseHighBrush}` is one of the 28 and still fails here.
  Merged dictionaries inside a page are not implemented either. A lookup that
  needed either fails by name; it never falls back on something plausible.
- **No `{Binding}`, no `{x:Bind}`, no `{TemplateBinding}`.**
  `{StaticResource}` and `{ThemeResource}` are the only markup extensions that
  resolve. Every other one is a named refusal, including on properties where
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
| `L3-scroll` | 166 | when does a `ScrollViewer` reserve sixteen pixels for a scroll bar, and when does it stretch its content to the viewport? |
| `L2-content` | 72 | what does `ContentPresenter`'s content alignment default to? |
| `L4-icon` | 69 | does a `FontIcon` measure its glyph or report a `FontSize` square? |
| `L3-canvas` | 63 | does a `Canvas` report its slot or nothing? |
| `L1-shape` | 60 | are a shape's bounds tight, and is its desired size the right edge or the width? |
| `L5-styles` | 50 | the level has no measurement at all either, and six of them ask a question outright — see below |
| `L5-resources` | 40 | every one of them: the level has no measurement at all |
| `L7-terminal` | 21 | does a bare `XamlReader.Load` reach `Application.Resources` — the cases the dictionary unblocked depend on the answer |
| `L5-xprimitives` | 17 | does a primitive written as an object element reach a typed property, and does an `x:String` convert to an enum? |
| `L0-props` | 14 | does `UseLayoutRounding` inherit, how does a tie at `.5` break, and what does a `ContentControl` do with content it is not stretching? |
| `L5-theme` | 13 | the same question the 21 above ask, asked directly: is WinUI 2's dictionary in the probe's host or only the OS's? |
| `L4-source` | 6 | is `Text="x"` the same thing as `<TextBlock>x</TextBlock>`? |
| `L5-xdirectives` | 6 | does a runtime load honour `x:Load` at all, and does it tolerate an `x:Uid` with no resource map? |

`L3-scroll` and `L4-icon` are a different kind of open from the rest.
`L3-canvas`, `L1-shape`, `L2-content`, `L0-props`, `L4-source` and all five L5
groups check answers this code already gives; those two measure types
it refuses to give one for at all, so they cannot fail on a number — only on
the refusal — and they exist so that the next version of this file can move
them out of this table entirely.

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
