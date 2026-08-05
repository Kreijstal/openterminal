# XAML behaviour database

A corpus of XAML layout cases and the measurements a real WinUI 2 runtime
produces for them. The corpus is authored on Linux; the measurements are filled
in by CI on `windows-latest`, because the oracle — `Windows.UI.Xaml` — only
runs on Windows.

The database exists so that a reimplementation can be built bottom-up and
checked continuously, instead of being judged all at once against a finished
Terminal page.

## Why not snapshot Terminal's pages directly

Terminal's 44 `.xaml` files are the acceptance target, not the development
target. A 500-element page that mismatches says only "something is wrong". It
does not say which primitive is wrong, it cannot be bisected, and it cannot be
made to pass incrementally.

The corpus is therefore layered, and each layer is only allowed to depend on
the ones below it:

| level | subject | authored |
|-------|---------------------------------------------|-----------|
| L0 | property system: defaults, local values, inheritance, precedence | generated |
| L1 | one element, no children: explicit size, margin, padding; `Path`, `PathIcon`, `Image` | generated |
| L2 | one parent, one child: alignment × margin × sizing; `ContentPresenter` content alignment | generated |
| L3 | panels: `StackPanel`, `Grid` (Auto/Star/Pixel, spans), `Canvas`, `ScrollViewer` | generated |
| L4 | text: `TextBlock` with a pinned font, `FontIcon` in an icon font | generated |
| L5 | resources, the `x:` directives, styles, templates, precedence | authored |
| L6 | visual states and storyboards, sampled at t=0 and t=end | authored |
| L7 | Terminal's own pages | harvested |

L0–L4 are *generated*, not hand-written. A few hundred lines of generator emits
the cross product, which is where the coverage comes from. Only L5–L6 need
authoring, and L7 is a harvest of the real pages.

## L5, before the oracle has seen it

Resource lookup is the first thing in the corpus that was authored *after* the
last oracle run, styles are the second and the `x:` directives followed them, so
all 113 of L5's cases — 40 in `L5-resources`, 50 in `L5-styles`, 17 in
`L5-xprimitives`, 6 in `L5-xdirectives` — are written and pending rather than
measured. That is a different state from the levels above it and is worth
naming, because "pending" is easy to read as "passing".

They are also the first cases with no axis to sweep. `{StaticResource W}`
resolves to the same literal at every available size, so the cross product that
gives L1–L4 their coverage would buy nothing but oracle time here. What varies
instead is the rule: where the key is declared, how far the lookup walks, which
dictionary wins when two declare the same key, which of the three spellings the
reference uses, and which primitive type the value has. `L5-styles` varies the
same kind of thing one layer up — where the style is declared, how it is
referenced, what its setters name, what it is based on, and which of the four
value slots wins when two of them have something to say.

Two things stand in for the oracle in the meantime, and neither pretends to be
one:

**Twins.** Every case that hides a value behind a resource or a style — or
behind a primitive written as an object element — names a `twin`: a second case
describing the same layout with the value written inline. The two
have to measure identically, and
[`check_twins.py`](../scripts/check_twins.py) checks that against our own
output, on a laptop, today. It says nothing about whether the layout is right
or whether the markup is valid XAML; both halves can be wrong together. What it
does catch is a lookup resolving to the wrong value, which is the entire
failure mode of a resource system.

For styles it catches something narrower and more useful. A resource that
resolves wrongly lands on a wrong number, which any single case would show. A
style applied at the wrong *precedence* lands on a number that is right in the
case that was written and wrong in the one that was not — so the pairs go in
both directions on purpose: a local value that has to beat the style, a style
setter that has to beat the default, and a style setter that has to beat a
value inherited from an ancestor. Only having all three catches an
implementation that stored a setter as a local value, which is otherwise
indistinguishable.

**Declared questions.** Seventeen cases carry `oracle_decides` and a `question`,
because WinUI 2's parser is not documented to the depth they need and WPF's
behaviour is not evidence about it. Four are about resources: does a forward
reference resolve? Is an element's own dictionary in scope for its own
attributes? Can a `GridLength` be declared as an object element? Does an
`x:Double` satisfy a property whose type is `GridLength`? Six are about styles,
and are listed with this implementation's provisional answer in [the layout
README](../layout/README.md#what-is-still-open). Seven are about the `x:`
namespace: does an `x:String` convert to an enum, or is it assigned as a string
object? Does a runtime load honour `x:Load` at all — and if it does, is the
element absent from the tree or present at zero size? Does it tolerate an
`x:Uid` it has no resource map for? Those have no inline twin — writing one
would be inventing the answer — and a rejection of them by the runtime is the
finding rather than a broken corpus. `report_measurements.py` treats them
accordingly, and only because the case said so before the run.

Everything else at L5 is accountable in the ordinary way: L5 is an authored
level, and a case there that the runtime refuses without having declared a
question fails CI exactly as an L1 case would.

## The two questions the last run left open

A measurement run does not only fill cases in; it also says which of them the
runtime answered in a way nothing predicted. The last one left exactly two, and
both now have a generated series aimed at them rather than a note saying they
are hard.

### `ScrollViewer` sizes itself two different ways

Terminal supplies three `ScrollViewer` subtrees. Two of them report their
content's desired size plus their own padding and then stretch the content to
the viewport — what a viewer with both scroll directions off does. The third
asks for sixteen more pixels in each axis, exactly one scroll bar's worth per
axis, and arranges its content at the content's own desired size — what a
viewer with both directions *on* does. None of the three sets a scroll bar
visibility, and no property in the markup separates them. Implementing either
reading makes six of the nine cases pass and three produce wrong numbers.

`L3-scroll` is 166 cases whose only job is to make that guess unnecessary:

| series | cases | axis |
|---|---:|---|
| `vis` | 32 | `HorizontalScrollBarVisibility` × `VerticalScrollBarVisibility`, all sixteen of Disabled/Auto/Hidden/Visible, × content smaller and larger than the viewport, in a viewer pinned at 200×150 |
| `free` | 48 | the same sixteen, in a viewer with no size at all, at three available sizes including infinity in both axes |
| `mode` | 24 | `HorizontalScrollMode` × `VerticalScrollMode` against three visibility settings and two content sizes — the properties that decide scrollability independently of whether a bar is shown |
| `pad` | 24 | padding on the viewer, symmetric and asymmetric, sized and unconstrained: does the reservation land inside the padding or beside it |
| `fit` | 20 | content width × height across 140/184/200/260 and 110/134/150/220, which are both candidate viewports and one either side — where an `Auto` bar's overflow test either fires per axis or does not |
| `shape` | 18 | six Border-only replicas of the three recorded shapes, at the harvest's own three available sizes |

Every child is a `Border`, so no answer here is entangled with a text metric.
The `shape` series is the bisection: the three replicas reproduce the recorded
skeletons, and the other three isolate `MaxHeight` and `Margin`, which only the
odd case out sets. If the replicas split the way the recorded three did, the
rule belongs to the `ScrollViewer`; if they do not, it belongs to the
`TextBlock` that only the odd one contains.

What the series deliberately does *not* ask: nothing here touches
`ZoomMode`, `IsScrollInertiaEnabled`, snap points, or a `ScrollViewer` used as a
control template part, and no case puts a second child or a `ScrollViewer`
inside another. Those are real parts of the type and none of them is what the
three recorded cases disagree about.

None of the 166 carries `oracle_decides`. Every property set is a plain enum
value on a documented property, so a rejection would be a finding about the
corpus rather than an answer we asked for — and declaring the question in
advance would only make that finding quiet.

### `FontIcon` measures a glyph nobody has metrics for

Fifteen L7 cases are one `FontIcon` each, in `Segoe MDL2 Assets` or
`Segoe Fluent Icons`, and neither font's metrics were harvested. That is now
three changes rather than one:

**The probe was throwing the glyphs away.** Python's `json` writes every
non-ASCII character as a `\uXXXX` escape, and the probe's unescaper dropped
every codepoint at or above U+0080 instead of encoding it. A `FontIcon`'s
`Glyph` therefore reached `XamlReader.Load` empty — and an empty `Glyph`
measures perfectly happily, so all fifteen recorded measurements are of an icon
that was never there. It shows in the numbers once you know to look:
`L7-terminal-88c43239e4` records a desired width of 28 and has `Margin="20,0,8,0"`,
so the glyph contributed nothing at all. Nothing failed, which is why it stood
for a whole measurement run.

The fix moved that code to
[`phase3/harness/json_text.h`](../harness/json_text.h), dependency-free, so the
Linux test suite can hold it — the probe itself builds only on Windows, which is
the whole reason the bug survived. `ctest`'s `probe_text` case is that check.
The consequence for the next run is a *genuine* L7 drift:

    L7: same 69 cases, different answers

which for once is not the corpus changing and not Windows being serviced. It is
fifteen cases that were measuring the wrong thing starting to measure the right
one.

**The harvest reads the icon fonts.** For the 44 codepoints Terminal's markup
actually names, extracted from the checkout by
[`harvest_icon_glyphs.py`](../scripts/harvest_icon_glyphs.py) and committed
beside the vocabulary inventory. See [the fonts directory](fonts/) for why a
missing glyph is recorded rather than fatal there.

**`L4-icon` is 69 cases** that pin the sizing rule instead of assuming it: the
seven glyphs the blocked cases use across both families and two sizes, the seven
`FontSize` values Terminal writes including none at all, the four family
spellings it uses including its fallback list, and eleven cases whose job is to
separate "measures the glyph" from "reports a `FontSize` square". The sharpest of
those is a `FontIcon` in Segoe UI holding `M` — an advance the corpus already
solved out of its own measurements, and not one em, which is what most of an
icon font is. It answers the question without the harvest having run at all.

A third, smaller gap closed at the same time: `L4-source` is six cases pairing
`Text="..."` against the same text written as element content. The corpus has
always written both and never checked they are the same thing, and the
`brace-escape` case at L5 crosses the two — so a disagreement there today would
be read as the escape failing when it might be the spelling.

## Harvesting L7 from Terminal's markup

`phase3/scripts/harvest_terminal_xaml.py` reads a Terminal checkout and emits
both an inventory of everything the markup uses and the subtrees a bare
`XamlReader.Load` can realise on its own.

Most of Terminal's markup cannot be realised on its own, and the reasons are
the interesting output. Each one is recorded as a *blocker* rather than being
silently dropped, so the inventory answers "what would we have to implement to
make more of this measurable". Against commit `e74649d5`:

| blocker | count | what it would take |
|---|---:|---|
| `markup-extension` | 2,043 | `{StaticResource}`, `{x:Bind}`, `{ThemeResource}`, `{TemplateBinding}` |
| `x-directive` | 762 | `x:Key`, `x:Uid`, `x:DataType`, `x:Load` |
| `foreign-type` | 288 | WinUI 2 (`muxc:`) and Terminal's own controls |
| `event-attribute` | 180 | a code-behind to hang handlers on |
| `resource-element` | 149 | `<StaticResource>` lookup against a dictionary |
| `foreign-attribute` | 13 | an attribute from a namespace we do not model |

That leaves 23 unique loadable subtrees, emitted at three available sizes each:
69 cases. It is a small number, and it is the honest one — it is what can be
measured today without a resource system, a binding engine, or a code-behind.
The table above is the roadmap for raising it.

### The resource system did not raise it, and why

The three blockers a resource system addresses — `markup-extension` where the
extension is `{StaticResource}`, `x:Key`, and `resource-element` — account for
much of that table, so implementing lookup looks like it should unlock
subtrees. It unlocks none, and the classifier was left alone because of it.

The measurement: relax exactly those three, but only where the key referenced
is defined by an `x:Key` somewhere in the same file, and re-run the extraction.
The result is **23 unique candidates — the same 23**. Every subtree that would
have been freed by resource lookup is held by something else as well: an
`{x:Bind}`, an `x:Uid`, a `muxc:` type, an event handler.

Relax the same three blockers *without* requiring the key to be defined in the
file — that is, assume every reference resolves — and the count goes to **91**.
The difference between those two numbers is the whole answer. What Terminal's
markup needs is not the lookup mechanism, which now exists, but the dictionary
the lookups land in: 1,054 of its 1,563 resource references name keys it never
defines, because they are WinUI 2's own theme resources and live in
`Application.Resources`. Loading those is the next thing that would move the
number, and it is a content problem rather than a parser one.

Until then the classifier keeps calling those subtrees blocked, which is
accurate: a standalone `XamlReader.Load` has no `Application`.

### `x-element` is gone from the table, and it bought nothing either

`x:String` and friends written as elements were 37 occurrences and are now
implemented, so the row is zero. The blockers table is a roadmap, and a
capability that exists should not appear on it — but the number that matters is
the one below it, and that one did not move: **23 unique candidates, the same
23.** Every subtree that held an `x:Double` held something else too.

Where the 37 went is worth stating, because 26 of them are still blocked and the
table no longer says so under that name. The rule the classifier applies now is
that a primitive is a *value*, so it is understood where a value belongs — the
content of a property element, or an entry in a `<ResourceDictionary>` — and is
judged on its attributes like anything else. The 11 Terminal writes as property
content (`<ToggleButton.Tag>` with an `<x:Int32>`,
`<DiscreteObjectKeyFrame.Value>` with an `<x:Boolean>`) carry no attributes and
are clean. The 26 in dictionaries carry an `x:Key`, so they moved into the
`x-directive` count — 736 to 762 — which is where every other `x:Key` already
was and is a truer statement of what they need: not the type, the dictionary.

### `x:Uid` would raise it, and is not relaxed anyway

This one is not zero, and it is the largest single number this measurement has
produced. Allow `x:Uid` on an element — nothing else — and the harvest goes from
23 unique candidates to **54**, from 69 cases to 162. Three of the original 69
disappear into a larger parent that the directive no longer blocks.

The classifier still calls it blocked, and the reason is not doubt about whether
the markup loads. It is what the markup would *mean*. A uid is a key into a
localised string table; a standalone load has no resource map, so those 31 new
subtrees would realise with none of the text that sizes them — a `TextBlock`
with nothing in it, a `Button` with no content. They would measure cleanly and
they would not be measurements of anything Terminal shows. That is a worse
outcome than a blocked subtree, because it looks like coverage.

What it would take is therefore not the directive, which is implemented, but a
resource map in the measurement environment on *both* sides — and the oracle
probe has none either. [`distil_resw_strings.py`](../scripts/distil_resw_strings.py)
is the half that exists: 579 uids and 747 properties out of the pinned
checkout's seven `en-US` tables. Of those 747, one property kind — `Text`, 113
of them — reaches a layout the corpus can measure; 72 are accessibility metadata
that changes no number by design; and the remaining 562 set `Content`, `Header`,
`HelpText` and `PlaceholderText` on controls that are not implemented, and fail
by name rather than silently. So even with the map in hand, the 31 subtrees are
waiting on the control set, not on the directive.

`x:Load` is the third case and is simply worth nothing today: relaxing it moves
the count from 23 to 23. It stays blocked for a different reason — whether a
runtime load honours it at all is the open question `L5-xdirectives-*` exists to
ask, and a subtree whose realised shape depends on an unanswered question is not
a prediction anyone can make. The moment the runtime answers, that is a one-line
change with a measured cost of nothing.

Classification is metadata-driven, not guessed: element names, property names,
attached-property stems, event names and the `UIElement` derivation chain all
come from the SDK contract WinMD, harvested to
`research/nuget/microsoft.windows.sdk.contracts/`. Applied to Terminal's markup
those sets leave no unknown element type and no unknown attribute, which is the
check that they are scoped correctly.

WPF markup is excluded. WPF and WinUI share the presentation namespace URI, so
the markup alone cannot tell them apart — but the owning project can: Terminal's
WinUI markup is compiled by C++ projects and its WPF markup by C# ones. Without
that rule a WPF page yields candidates that look clean and then fail to load,
because WPF-only members are absent from the WinUI metadata that would have
flagged them.

### The runtime is the arbiter

A blocker-free subtree is a *prediction* that it will load. The real runtime
decides. A candidate it rejects comes back with an `error`, and
`report_measurements.py` quarantines and names it rather than letting it pass as
coverage. The asymmetry is deliberate:

- a case **we wrote** (L0–L5) that fails to load is a broken corpus, and fails
  CI — unless it declared the question it was asking, in which case the
  rejection is the answer;
- a case **we harvested** (L7) that fails to load is a blocker the harvester did
  not model, and is reported as work to do.

Text is quarantined at L4 on purpose. XAML layout bottoms out in DirectWrite
for every `TextBlock`, and text-measurement error would otherwise contaminate
every enclosing panel in L1–L3. Cases below L4 use only zero-text elements
(`Border`, `Rectangle`, `Grid`, `StackPanel` over fixed-size children), so a
failure there is unambiguously a layout failure.

## What a measurement records

Both halves of the layout contract, not just the visible result:

- `Measure(available)` → `DesiredSize`
- `Arrange(final)` → `ActualWidth`/`ActualHeight` and offset from the parent

Recording only the arranged result hides a whole class of bug: an
implementation can land final positions correctly while its measure pass is
wrong, and the error surfaces only once the element is placed under a panel
that actually consults `DesiredSize`.

Available size is swept across zero, finite, and `Infinity`. Infinity is where
most measure bugs live — it is the case a `StackPanel` passes down its stacking
axis and a `ScrollViewer` passes to its content.

## Determinism

Every value that can move the numbers is pinned in the case, and every value
that identifies the oracle is recorded in the measurement:

pinned in the case
: available size, DPI scale, theme, font family, font size, language

recorded in the measurement
: Windows build (including UBR), SHA-256 of `segoeui.ttf`

DirectWrite metrics change when a font is serviced. A TTF carries no VersionInfo
resource, so the file hash is what identifies it; it moves for exactly the same
reason a version would, and cannot come back empty without the run failing.

CI regenerates every measurement twice and byte-compares, the same determinism
gate `phase0` and `phase1` already apply to their snapshots.

## Using the measurements

The measurements are CI output, not repository content: 1,104 files regenerated
deterministically in a few minutes. Fetch them from the artifact of a green
run. The digest committed under `oracles/` still records 541, which is the
corpus the last run saw — the 113 L5 cases, the 14 newer `L0-props` ones, the
195 in `L1-shape`, `L2-content` and `L3-canvas`, and the 241 in `L3-scroll`,
`L4-icon` and `L4-source` beside them, were all authored after it.

    python3 phase3/scripts/fetch_measurements.py          # prints the directory
    python3 phase3/scripts/check_layout.py \
        --expected <that directory> --actual <your results> --levels L0,L1,L2,L3

`check_layout.py` takes results in the same shape the probe writes, so an
implementation under test only has to emit what the probe emits. It compares
`desired`, `actual` and `offset` per node against a tolerance, and treats a
case the *oracle* could not load as no expectation at all.

`check_twins.py` takes the same shape but needs no oracle at all: it compares
results against *other* results, pairing each case with the `twin` it names.
That is what covers L5 until a measurement run reaches it.

    python3 phase3/scripts/check_twins.py \
        --cases phase3/xaml-db/cases --results <your results>

What **is** committed is `oracles/<os-build>.json`: a digest of what the runtime
answered, about a kilobyte per build, with a per-level hash so a change says
where it happened. Windows is serviced underneath us, and without a stored
fingerprint an update silently moves every expectation — the next fetch
re-baselines against the new answers and a real regression in our own code is
masked by the shift. Both the fetch and CI refuse to proceed when the live
oracle disagrees with the committed digest. That is drift, not a test failure:
it is reviewed, then the digest is updated deliberately.

A build with no committed digest yet warns rather than fails, because runner
images move on their own schedule — but it says plainly that nothing is
verifying those measurements until the digest lands.

**Growing the corpus trips the same gate, on purpose.** The committed digest for
`10.0.26100.33158` covers 541 cases and no L5; the corpus now has 1,104 and
does. The first measurement run after that will stop with

    L0: 4 cases -> 18 cases
    L1: 72 cases -> 132 cases
    L2: 192 cases -> 264 cases
    L3: 132 cases -> 361 cases
    L4: 72 cases -> 147 cases
    L5: new level, 113 cases
    L7: same 69 cases, different answers

Every line but the last is the corpus asking questions the digest has never
seen, rather than the runtime answering differently. The last one *is* a
different answer, and it has a cause on our side: fifteen of those cases were
being measured with an empty `Glyph`, because the probe dropped it — see
[above](#fonticon-measures-a-glyph-nobody-has-metrics-for).

The gate cannot tell any of those apart on its own and does not try to: it
stops, and a person decides. The resolution is to take that run's measurement
artifact, confirm the answers are what the cases were written to ask — and, for
L7, that the fifteen that moved are the fifteen `FontIcon` ones — then commit the
regenerated digest. Nothing is hidden by that: every level is compared case by
case in the same run, so a real regression would be reported beside the
addition.

**And the gated levels will go red with it.** `L3-scroll` lands in L3, which the
layout job gates on, and the layout core does not implement `ScrollViewer` — so
the run after this one reports 166 L3 failures reading
`the type 'ScrollViewer' is not implemented`. That is the intended cost. The
series exists to make implementing it possible, the failures name the one type
that is missing rather than a wrong number, and a gate that stayed green while
the answer went unused would be worth less than one that says what to do next.
`L4-icon` does the same for `FontIcon` at L4, which is reported rather than
gated, so that half is quieter by construction.

## Layout

    phase3/xaml-db/
      oracles/<os-build>.json              committed digest of what the runtime answered
      fonts/derived/<family>.json          committed metrics solved from the measurements
      schema/                              JSON Schema for both file kinds

    (generated, not committed)
      cases/L<n>-<group>/<id>.json         emitted by generate_cases.py and
                                           harvest_terminal_xaml.py; CI generates
                                           twice and diffs the runs, and the
                                           measured corpus travels to the layout
                                           job as an artifact
      <anywhere>/strings.json              the x:Uid table, emitted by
                                           distil_resw_strings.py from a pinned
                                           Terminal checkout. Not part of the
                                           corpus and never loaded by default:
                                           the oracle has no resource map, so a
                                           run that used one would measure
                                           markup the oracle cannot

    (CI artifact, not committed)
      measurements/<os-build>/<id>.json    filled in by CI on windows-latest
      measurements/<os-build>/report.json  per-level outcome and quarantine
      measurements/<os-build>/oracle.json  build and font identity of that run
      fonts/<family>.json                  metrics read off the runner's font

The vocabulary inventory, and the icon-font codepoints beside it, are research
data rather than corpus data, so they live with the other pinned snapshots:

    research/windows-terminal/<commit>/xaml-inventory.json
    research/windows-terminal/<commit>/icon-glyphs.json
    research/nuget/microsoft.windows.sdk.contracts/<version>/xaml-members.json

## Progress metric

A Linux job runs the reimplementation against the same corpus and diffs.
"N of M cases matching", per level, is the project's progress number — and
unlike a page screenshot it says exactly what to fix next.

Against build `10.0.26100.33158`, [`phase3/layout`](../layout/) matches all of
L0–L3 and 36 of the 69 L7 cases. L4 is implemented and matches 36 of 72 on a
bare checkout, against the two numbers [solved out of the
measurements](fonts/) themselves; the other 36 need [the harvested font
metrics](fonts/), which are CI output rather than repository content, and say
so by name until those arrive. L5's resource, style and `x:` halves are all
implemented and have no measurements at
all yet, so they contribute nothing to this number and are reported separately by
`check_twins.py`, where 49 of 51 pairs agree and the other two are unmeasurable
without the harvested metrics — which is the point of keeping the
two reports apart. What is
still red fails with `the type 'ScrollViewer' is not implemented` and the like,
rather than with wrong numbers, which is the distinction the metric is there to
preserve — [the layout README](../layout/README.md) ranks the remaining
blockers.

"All of L0" is four cases. L0 has eighteen, and the fourteen written since the
last oracle run have no measurement to match — a case with no measurement is no
expectation, so it counts neither way. The next run will report L0 as drifted,
`4 cases -> 18 cases`, which is the corpus-changed signal doing its job: the
digest is reviewed and updated deliberately, and fourteen answers arrive with
it. Three of them are answers this implementation does not have — whether
`UseLayoutRounding` inherits, how a tie at exactly `.5` breaks, and what a
`ContentControl` does with content it is not stretching.

A further 549 generated cases in `L1-shape`, `L2-content`, `L3-canvas`,
`L3-scroll`, `L4-icon`, `L4-source` and all four L5 groups are newer than the
last oracle run for the same reason. They do not move the numbers above in
either direction until CI fills them in; they exist because the answers they
cover currently rest on one witness each, on three that contradict each other,
or on none at all.
