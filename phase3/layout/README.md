# Layout core

A native reimplementation of the XAML layout contract, checked against
measurements recorded from the real `Windows.UI.Xaml`.

It builds and runs anywhere — no WinRT, no Windows, no Wine. That is the point:
the expectations were captured once by [the probe](../harness/xaml_probe.cpp) on
`windows-latest`, and everything after that is a plain native binary diffed
against a JSON file. The loop is seconds long and runs on a Linux laptop.

## Where it stands

### Wave 3 and Wave 4 runtime surface

The native runtime now contains the Wave 3 behavior needed above the measured
layout layers:

- `{Binding}` and `{x:Bind}` parsing with one-time, one-way, two-way and
  one-way-to-source expressions, fallback values, converters, effective-value
  notifications, and an `INotifyPropertyChanged`-shaped source adapter;
- an animation value source above local/style/inherited values, namescopes,
  visual-state groups and setters, and endpoint/intermediate sampling for
  double, thickness and discrete storyboards;
- live `{TemplateBinding}` expressions, `ControlTemplate` construction, and a
  default-style registry intended to receive reconstructed `generic.xaml`
  entries; and
- markup realization for bindings, named storyboard targets, visual-state
  setters and `DoubleAnimation` timelines. Loading does not invent an initial
  state; the caller enters one through the element's `VisualStateManager`, as
  the real runtime does through `GoToState`.

Wave 4 adds virtualized `ItemsControl`/`ListView` realization and recycling,
selection, `Page`/`Frame` journals, `Popup`/`Flyout` lifetime and light-dismiss
state, and native content-control surfaces for the muxc types Terminal uses
(`NavigationView`, `TabView`, `TeachingTip`, `TreeView`, `NumberBox`,
`InfoBar`, and their smaller peers). The text-markup loader constructs these
framework and muxc types rather than rejecting them by name.

These are implementation claims backed by the focused `wave3` and `wave4`
tests. They are not oracle claims: the repository still has no recorded L6
measurements, and Terminal's full open `generic.xaml` templates and the muxc
WinRT ABI remain inputs to harvest and diff before the roadmap's Wave 3/4 exit
criteria can be marked measured.

Against build `10.0.26100.33158`:

| level | subject | measured | matching |
|-------|---------------------------------------------|------:|---------:|
| L0 | property system | 18 | **18** |
| L1 | elements: explicit size, margin, shapes | 132 | **132** |
| L2 | one parent, one child: alignment × margin × sizing, content | 264 | **264** |
| L3 | panels: `StackPanel`, `Grid`, `Canvas`, `ScrollViewer` | 361 | **361** |
| L4 | text and icons: `TextBlock`, `FontIcon` | 147 | **145** — two named refusals, see below |
| L5 | resources, styles and the `x:` directives | 126 | **124** — the brace-escape pair, see the kerning omission |
| L7 | Terminal's own pages | 90 | **84** — two refusal triples, see below |

The two L4 refusals and the two L7 refusal triples are named, not
approximate: `mdl2-latin-14` (which family the runtime fell back to is
unrecorded), `mdl2-weight-14` and `88c43239e4-s*` (what `FontWeight`
adds is unpinned by two observations — cases are authored to ask), and
`4edb490008-s*` (waiting for the Cascadia Mono harvest to reach an
oracle run). The counts above are against the fonts of the recorded
run plus the committed derived kerning.

Every level is now fully measured: run 31019336758 re-baselined the oracle
from 541 measurements to 1138, and the counts above are against that
recording. The thirty-nine cases authored since — `L4-kern` asking which
kerning pairs the runtime applies, and the weight probes beside it — are
pending, not passing, until the next run records them; the section
below on the [next re-baseline](#what-is-still-open) says what their
arrival costs.

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
under all of them. The recorded L7 snapshot predates the completion pass below,
so its 33/69 is now a stale gate rather than a current implementation count.
Running the current core over the pinned corpus produces no missing-type errors.
On a bare checkout, 21 L7 subcases remain unmeasurable only because the
committed derived font contains neither Cascadia Mono nor the icon glyphs.

A further 562 generated cases — `L1-shape`, `L2-content`, `L3-canvas`,
`L3-scroll`, `L4-icon`, `L4-source` and all of `L5` — have no recorded
measurement yet and so are neither passing nor failing.
`Canvas`, `ContentPresenter`, `Path`, `PathIcon` and `Image` are implemented and
every case that measures one is in that set; the two `PathIcon` subtrees
Terminal's own markup supplies are the only witnesses any of them has today.
`ScrollViewer` and `FontIcon` are now implemented, as are the Terminal blockers
that sat behind them: `Rectangle`, `TextBox`, `Button`, `ToolTip`/`Run`, and an
inline `Thumb.Template`.

### L7 completion pass

`ScrollViewer` has a single-content viewport, the visibility/mode
cross-product, overlaid scroll bars, extent/viewport state and clamped
offsets — see [what the 166 recorded answers
say](#what-a-scrollviewer-does-with-its-scroll-bars) for the four rules that
took the group from 110 to 166. `FontIcon` measures its Unicode glyph through the harvested text
metric pipeline and understands comma-separated font fallback. Shape and
control support now includes `Rectangle`, control padding, `Button`, `TextBox`,
`ToolTip` with folded `Run` inlines, and `Thumb` with an inline
`ControlTemplate` root.

The next Windows oracle run remains authoritative. In particular,
[`L3-scroll`](../xaml-db/README.md#scrollviewer-sizes-itself-two-different-ways)
and `L4-icon` were authored because the old three-case ScrollViewer evidence
was contradictory and the old FontIcon glyphs were corrupted by an ASCII-only
probe path. `L3-scroll` has since been recorded and answered; `L4-icon` has
not, so that half is still an implemented and focused-tested surface rather
than a numeric parity claim.

### What a `ScrollViewer` does with its scroll bars

The recorded group settles the question the three Terminal subtrees could not:
the bars are *overlaid* on the content, not reserved out of it. Four rules,
each with the case that pins it:

- **The content is measured unbounded in every axis whose
  `ScrollBarVisibility` is not `Disabled`, and against the padded client size
  in the axes where it is.** `ScrollMode` does not enter into it:
  `L3-scroll-mode-*` pairs cases differing only in
  `HorizontalScrollMode`/`VerticalScrollMode` and records the same 300×260
  content for both.
- **The content is arranged against the whole padded client too — no bar is
  subtracted.** `L3-scroll-shape-padded-stretch` stretches its child to
  394×294 inside a 400×300 viewer that has `Padding="3"` and a vertical bar.
- **The viewer's desired size per axis is the larger of what the padded
  content asked for and what the bars need side by side: the crossing bar's
  16 thickness plus this bar's 32 minimum length.** A visible bar holds open
  the auto track it sits in while the content spans both tracks, which is why
  a forced bar usually costs nothing and occasionally costs a little:
  60×40 content under a forced pair is recorded as 60×48, and a 0×16 child
  under a bare viewer as 16×32.
- **`VerticalScrollBarVisibility` defaults to `Visible`.** That same 16×32 is
  the proof: an `Auto` bar would be hidden around a child that fits, and the
  viewer would have asked for 0×16.

One number in there is not pinned by the corpus: the *horizontal* bar's 32
minimum width. No recorded case forces a horizontal bar next to content
narrower than 44, so every case passes with any value up to that. It is
written as the mirror of the vertical bar's minimum length, which the corpus
does pin, and it is the only constant in this file that a future case could
move without any existing case noticing.

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
| animation | active storyboards and visual-state setters |
| local | the markup on the element itself |
| style | the element's `Style`, `BasedOn` already merged |
| inherited | the nearest ancestor with a local or style value |
| default | the registration |

in that order. It is ported in shape from
[dotnet/wpf](https://github.com/dotnet/wpf)'s `DependencyObject`; the slots the
references have and this does not are triggers, coercion and a distinct
built-in-style slot. Default styles currently enter the style slot before an
explicit style replaces them.

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
(`CFrameworkElement::ValidateTargetType` asks `OfTypeByIndex`). The runtime has
now answered the pair that separates them: `L5-styles-explicit-derived-target`
applies a `TargetType="Control"` style to a `ContentControl` and
`L5-styles-implicit-derived-type` leaves the same style unapplied. So `Control`
is a legal `TargetType` here even though `<Control>` is not a legal tag — a
`TargetType` names a type identity, not an element the parser can build, and
the two registries answer separately.

**An implicit style reaches what is already in the tree when its dictionary
arrives — and nothing else.** Not "the subtree below the dictionary", which is
what this parser assumed. `L5-styles-implicit-target-type` declares one on a
`StackPanel` and writes two `Border`s under it, and the runtime records both
unstyled; `L5-styles-implicit-forward-dictionary` writes the `Border` first and
the dictionary below it, and that `Border` *is* styled;
`L5-styles-implicit-own-dictionary` records both halves at once, the `Border`
holding the dictionary styled by it and the `Border` written below it not. So
what applies the style is the resources-changed notification the runtime raises
when a `Resources` dictionary is attached, walking the owner and its current
subtree, and nothing runs it a second time — there is no live tree to enter
here and the recorded zeroes say no later pass happened. Ten of the thirteen
`L5-styles` cases that were failing turn on this one rule.

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

## XBF: loading a compiled page

Terminal ships its UI as `.xbf`, not as `.xaml`. The text markup is a build
input; what the shipped binary hands the runtime is the SDK XAML compiler's
output. A runtime that reads only text therefore reads nothing the real
application feeds it, and that is what `src/xbf.h` and `src/xbf_markup.h` are
for.

`xbf.cpp` is the format: the container, the six metadata tables, and the node
stream. `xbf_markup.cpp` is what the node stream *means* — XBF is not a
serialized tree but a recording of the calls the object writer made while the
compiler walked the markup ("create this type, push a scope, set that property,
pop"), so loading a page is replaying that recording. It is replayed back into
the document it came from and handed to the same markup engine the text path
uses, which is why nothing here can move a corpus number: the text path is not
touched.

Version 2.1 is implemented and nothing else. It is what `genxbf.dll` from
Windows SDK 10.0.26100.0 emits for WinUI 2.8.4 consumption, and it is the only
version this reader has ever seen output from; 2.0, for instance, does not
null-terminate its metadata strings, so accepting it would mis-read every string
in the file. Other versions are refused by number.

### The gate

The same markup can reach the runtime both ways, and the corpus is ~1090
documents whose layout is already verified against recorded oracle
measurements. So the loader is held to producing *byte-identical* measurements
to the text path over every case the real compiler accepts:

    python3 -B phase3/scripts/generate_cases.py
    cmake -S phase3/layout -B /tmp/layout-build -DCMAKE_BUILD_TYPE=Release
    cmake --build /tmp/layout-build
    python3 -B phase4/scripts/xbf_equivalence.py --tool /tmp/layout-build/xbf_equivalence

That needs the harvested SDK compiler under Wine — `phase2/scripts/
build_winui_xaml.py` puts it, and the MSBuild project this reuses, under `/tmp`.
No `.xbf` is ever committed; they are produced under `/tmp` and thrown away.

There is no new expectation anywhere in that gate. It cannot be satisfied by
agreeing with a number someone wrote down, only by decoding the format
correctly, and a case the compiler *rejects* is recorded with the compiler's own
error rather than skipped.

Where it stands: **1081 of 1087** corpus documents compiled by `genxbf`
10.0.26100.0, and **1081 of 1081 are identical** through both paths — 929
agreeing on a measured tree and 152 on the same refusal (the metrics the corpus
derived cover only the characters it measures alone, so both paths decline the
same text cases for the same reason). Twice, byte for byte.

The six the compiler itself rejects are an exclusion list, not a gap, and each
carries the compiler's message:

| case | `genxbf` said |
|---|---|
| `L5-xdirectives-defer-load-strategy-lazy` | `WMC0907: Element must have x:Name attribute specified since it uses x:DeferLoadStrategy.` |
| `L5-xdirectives-load-false-only-child` | `WMC0907: Element must have x:Name attribute specified since it uses x:Load.` |
| `L5-xdirectives-load-false-sibling` | the same |
| `L5-xdirectives-load-true` | the same |
| `L5-xprimitives-int32-width` | `WMC0015: Cannot assign 'Int32' into property 'Width', type must be assignable to 'Double'` |
| `L5-xprimitives-string-into-enum` | `WMC0015: Cannot assign 'String' into property 'HorizontalAlignment', type must be assignable to 'HorizontalAlignment'` |

Both groups are findings rather than accidents. The first says `x:Load` is a
*compiler* feature that `XamlReader.Load` accepts and `genxbf` will not compile
without an `x:Name` — so those four cases can only ever exist on the text path.
The second is the compiler agreeing statically with what the oracle recorded the
runtime doing at load time, which is a second, independent witness for two cases
the corpus records as errors.

`phase3/layout/tests/xbf_test.cpp` covers what the gate structurally cannot: a
malformed file is not something `genxbf` produces, so rejecting one has to be
tested against a fixture. That fixture is assembled byte by byte in the test
with a comment on every field, and when the compiled corpus is present the test
checks it against the compiler's actual output for the same markup instead of
believing it.

### What the format says that the markup does not

Two things the reconstruction has to get right that are easy to assume away, and
both were found by the gate rather than reasoned out:

* `<TextBlock>x</TextBlock>` and `<TextBlock Text="x"/>` are *not* the same
  node stream. The first pushes a string and adds it to `TextBlock.Inlines`;
  the second sets `TextBlock.Text`. The format keeps the distinction the corpus
  measures, which was not obvious in advance.
* Order is load-bearing. `<Border.Resources>` followed by
  `<Border.Width>{StaticResource …}</Border.Width>` resolves and the same two
  written the other way round does not — the corpus has both, one measuring and
  one failing by name. So an assignment recorded after a property element is
  written back as a property element, not hoisted onto the tag.

### Where it stops, and why that is the next wave's work list

Terminal's own 40 compiled pages all parse as containers and decode most of
their node streams, and every one of them then stops at a named boundary rather
than a guess. Those names are the work list:

| boundary | pages | what it is |
|---|---:|---|
| `SetConnectionId` | 27 | `x:Bind` and event handlers, which need code-behind |
| `DeferredElement v3` custom writer | 4 | `x:Load` deferral |
| `CheckPeerType` | 4 | the page asserts its `x:Class` peer exists |
| `VisualStateGroupCollection v5` custom writer | 2 | visual states, deferred |
| a stable property index this runtime has no name for | 2 | `CommonResources` and `SettingContainerStyle` reach further into the framework's property surface than the corpus does |
| a type only TerminalApp's metadata provider defines | 1 | `HighlightedTextControl` |

    python3 -B phase4/scripts/xbf_terminal_pages.py --tool /tmp/layout-build/xbf_dump

prints that table from whatever `phase2/scripts/build_winui_xaml.py` last built.

Two custom writers *are* decoded, because the corpus needs them: a deferred
`ResourceDictionary` (v3) and a deferred `Style` (v1/v2). The rest are named and
refused. `src/xbf_names.cpp` holds the stable index table, hand-transcribed for
exactly the indexes the corpus and Terminal's pages use — copying all ~1800
types and ~2900 properties out of the published header would be committing a
generated file under another name, and an index that is not in the table is
refused *by number*, which is a fact rather than a guess.

### What is still guessed, and how to stop guessing it

Every one of these is decoded from the published reader but is *not* exercised
by any case the gate runs, so it is written down here rather than believed. Each
line names the markup that would exercise it — an authored probe pair whose XBF
settles the question by being compiled, which costs one file and no oracle run.

| open | the probe that settles it |
|---|---|
| types and properties written into the file's own tables rather than as stable indexes: whether a known type's `m_uiTypeNamespace` indexes the namespace table and a property's `m_uiType` indexes the type table | a page referencing a type from a second WinMD, compiled with that WinMD as a reference — Terminal's `HighlightedTextControlStyle.xbf` already contains one and is the file to read |
| a brush written as a named colour comes back as `#AARRGGBB` | `Foreground="Red"` compiles to the colour `0xFFFF0000`; the name is gone from the file, so a reconstruction cannot recover it. Nothing measurable depends on it — but a consumer that draws would see the difference, and the recorded `background` string on a node does |
| the line-number stream | its length is computed (a sub-stream after it would otherwise start in the wrong place) and its contents are not decoded. A page whose error message quotes a line number is what would need it |
| `ResourceDictionary` v1/v2/v4, `Style` v3, `VisualStateGroupCollection`, `DeferredElement` custom writers | each is named and refused today; `Launch.xaml`, `NewTabMenu.xaml`, `TerminalPage.xaml`, `NullableColorPicker.xaml` and `SearchBoxControl.xaml` are the files that hold them |
| a directive namespace bound to a prefix other than `x` | one corpus case rewritten with `xmlns:xaml=` in place of `xmlns:x=` |

One question that *was* open and is now answered, by compiling both spellings
and reading the two node streams: `<TextBlock>x</TextBlock>` and
`<TextBlock Text="x"/>` are different in XBF. See above.

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

Two places where the ported source and the recorded oracle disagree, both
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

A third was withdrawn. **`Canvas` reports no arranged size** was recorded here
as a divergence from `CCanvas::ArrangeOverride`, on the strength of Terminal's
`SelectionCanvas` measuring zero by zero in a 400x300 slot. The 63 `L3-canvas`
cases that were pending to confirm it refuted it instead: an unsized Canvas
reports zero in that slot but one with `Width="200"` reports 200, which is not
an `ArrangeOverride` returning zero — it is an element that was never arranged
at all reading the fallback below. The source was right and the inference was
wrong.

### Not every element takes part in layout

The rule that replaced it, and the one that explains four groups at once:
Windows.UI.Xaml gives an element layout storage only when the element is a
*layout element* or its parent is one — the condition guarding
`EnsureLayoutStorage` in `CUIElement::Measure` and `CUIElement::Arrange`.
Everything else is measured and arranged by nobody, and the two numbers the
probe records come from elsewhere:

- `DesiredSize` reads that storage and reports nothing without it
  (`CoreImports::UIElement_GetDesiredSize`). An explicit `Width` never reaches
  it.
- `ActualWidth`/`ActualHeight` read the render size from that storage, and
  without it fall back to the size the markup specified — or to zero while the
  element is still measure-dirty, which it stays if no parent ever measured it
  (`CFrameworkElement::GetActualWidth`).

`Border`, `Control`, `ContentPresenter`, `IconElement`, `TextBlock` and `Panel`
are layout elements; `Canvas` is the `Panel` that is not, and `Shape` and
`Image` never were. So a root `Path` with `Width="40"` desires nothing and
renders 40x40, an empty root `Canvas` desires nothing and renders nothing while
one with `Width="200"` renders 200 wide, and a `Border` with `Width="30"` inside
a root `Canvas` renders *zero* — the Canvas above it is not a layout element,
so it never ran its own measure and never reached the child at all. All 39
`L1-shape`, 57 `L3-canvas` and the shape and image nodes in `L7-terminal` agree,
and the 93 `L1-shape` cases that always passed are the `PathIcon` ones, because
an `IconElement` is a layout element even though the `Path` it draws with is
not.

## Deliberate omissions

Not "to do later" in the vague sense — these are the specific things this code
does not do, so that a passing run is not read as more than it is:

- **Text is measured against rules, not a text stack.** `TextBlock` bottoms out
  in DirectWrite, and there is none here: what [`text.cpp`](src/text.cpp) has
  is three rules read off the corpus, plus a font's advance widths. No
  shaping, no kerning, no ligatures, no fallback for a character the metrics do
  not cover. It stays quarantined at L4 for that reason — text-measurement
  error would otherwise contaminate every panel that contains it.

  The kerning is now measured rather than merely absent. `Terminal` in Segoe UI
  measures 200 design units narrower than the sum of its advances, at 12, 14
  and 24 alike — a design-unit constant, so it is a pair adjustment and not a
  wrong advance, and every letter in it appears in the pangram case whose sum
  *is* right. `{StaticResource NotAKey}` loses 153 the same way, which is the
  whole of what `L5-resources-brace-escape` and its twin fail by. Fixing it
  needs the font's `kern`/`GPOS` pairs, and the harvest carries only advances;
  guessing which pair holds the 153 would be inventing the answer, so both
  cases stay red and named here instead.
- **No `RelativePanel`; incomplete stock templates.** Control-template
  construction, application and template binding are implemented, including
  Terminal's inline `Thumb.Template`. The complete open `generic.xaml` stock
  style set has not yet been reconstructed and oracle-diffed, so controls
  without an explicit template retain focused native layout contracts rather
  than claiming pixel parity with the stock theme.
- **No stroked shapes, and no `Stretch` but `None`.** A stroked shape grows by
  half its thickness on every side and a stretched one is scaled into its
  constraint; no case in the corpus has either, so markup asking for one is
  refused by name.
- **No elliptical arcs in path data.** `A` needs the endpoint-to-centre
  conversion and then the extremes of a rotated arc. Nothing in the corpus has
  one.
- **No `LayoutTransform`.** WPF measures a transformed element by fitting a
  maximal rectangle in local space; none of that is here.
- **The property store has four sources**, animation, local, style and
  inherited. Triggers, coercion and a distinct built-in-style slot are still
  absent; the lookup is written so they slot in beside the four rather than
  being threaded through callers.
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
- **Bindings and visual states are implemented, but the L6 oracle is absent.**
  Their focused tests pin notification, precedence, teardown and endpoint
  behavior locally. No result table calls those tests oracle parity.
- **The template engine is present; the full built-in style database is not.**
  `ControlTemplate`, live template bindings and a default-style registry are
  implemented. Reconstructing every open `generic.xaml` entry and diffing it
  against the runtime is still a separate pinned harvest. Style triggers and
  transition selection remain named omissions.
- **A style is not re-applied after the tree is built.** An implicit style is
  resolved when a `Resources` dictionary is attached, over the owner and the
  subtree already under it, and never again — which is what the recordings
  show, and it is why an element written below a dictionary is not styled by
  it. Nothing in this corpus moves an element or replaces a dictionary after
  the load, so the second and third occasions the runtime has for re-resolving
  are not modelled. `ClearStyleValues` exists and is tested, because replacing
  a style is what makes the separate slot necessary, but no markup path calls
  it.
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

Run 31019336758 recorded every case this table used to list, and the wave-3
tracks implemented the answers; the groups that once waited here are in the
level table at the top, passing. What remains open is smaller and sharper:

| open | cases | what settles it |
|---|---:|---|
| which kern pairs the runtime applies | 34 authored (`L4-kern`) | the next oracle run: one candidate pair per case splits the source-table, magnitude and glyph-class hypotheses |
| what `FontWeight` adds to a glyph | 5 authored + `mdl2-weight-14`, `88c43239e4-s*` refusing | the next run: two observations fit three rules, the probes were written to disagree |
| which family `mdl2-latin-14` fell back to | 1 refusing | a new kind of recording — the size is not explained by any harvested family |
| Cascadia Mono | `4edb490008-s*` refusing | the harvest now reads it from the pinned Terminal checkout; the next run carries it |
| the `brace-escape` pair | 2 failing on a number | the recorded runs imply a 153-design-unit adjustment somewhere in `{StaticResource NotAKey}`, and advances alone cannot say which pair holds it — `L4-kern` asks |

The 39 authored cases are pending, not passing, and their arrival is priced:
the digest gate stops the next measured run at `L4: 147 cases -> 186 cases`
until the re-baseline is read and accepted deliberately.

The six `L5-styles` cases that carry `oracle_decides` are the ones neither
reference settles for a `XamlReader.Load` with no `Application`:

| case | what the runtime answered | what this implementation had guessed |
|---|---|---|
| `implicit-own-dictionary` | applies — the outer `Border` *is* styled by its own dictionary, and the inner one, written below it, is not | applied to both |
| `implicit-forward-dictionary` | applies — a dictionary written below the element it targets still reaches it | did not apply |
| `implicit-derived-type` | loads, and does not apply: the implicit key is an exact type match | refused, for want of an abstract `Control` type |
| `explicit-derived-target` | loads, and *does* apply: the explicit route is an is-a match | refused, same reason |
| `setter-value-resource-scope` | resolved where the style is written, so `60` | the same; the guess held |
| `duplicate-setter` | last setter wins, so `100` | the same; the guess held |

The middle pair was the deliberate one, and the interesting finding is exactly
what it was authored for: the runtime answers them differently, so the two
documented lookup rules are both real. The first pair was the surprise — taken
together they say an implicit style reaches what already exists when its
dictionary is attached rather than the subtree written below it, which is the
opposite of the direction the corpus notes assumed.

That reach rule and the rest of L5 now pass against the recording, so this
section documents answers rather than bets. The habit it records is the one
that still applies to the 39 pending cases: an unchecked answer that stays
unchecked is worse than one that fails loudly.
