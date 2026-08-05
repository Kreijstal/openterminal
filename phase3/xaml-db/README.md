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
| L3 | panels: `StackPanel`, `Grid` (Auto/Star/Pixel, spans), `Canvas` | generated |
| L4 | text: `TextBlock` with a pinned font | generated |
| L5 | resources, styles, templates, precedence | authored |
| L6 | visual states and storyboards, sampled at t=0 and t=end | authored |
| L7 | Terminal's own pages | harvested |

L0–L4 are *generated*, not hand-written. A few hundred lines of generator emits
the cross product, which is where the coverage comes from. Only L5–L6 need
authoring, and L7 is a harvest of the real pages.

## L5, before the oracle has seen it

Resource lookup is the first thing in the corpus that was authored *after* the
last oracle run, so its 40 cases are written and pending rather than measured.
That is a different state from the levels above it and is worth naming, because
"pending" is easy to read as "passing".

They are also the first cases with no axis to sweep. `{StaticResource W}`
resolves to the same literal at every available size, so the cross product that
gives L1–L4 their coverage would buy nothing but oracle time here. What varies
instead is the rule: where the key is declared, how far the lookup walks, which
dictionary wins when two declare the same key, which of the three spellings the
reference uses, and which primitive type the value has.

Two things stand in for the oracle in the meantime, and neither pretends to be
one:

**Twins.** Every case that hides a value behind a resource names a `twin` — a
second case describing the same layout with the value written inline. The two
have to measure identically, and
[`check_twins.py`](../scripts/check_twins.py) checks that against our own
output, on a laptop, today. It says nothing about whether the layout is right
or whether the markup is valid XAML; both halves can be wrong together. What it
does catch is a lookup resolving to the wrong value, which is the entire
failure mode of a resource system.

**Declared questions.** Four cases carry `oracle_decides` and a `question`,
because WinUI 2's parser is not documented to the depth they need and WPF's
behaviour is not evidence about it. Does a forward reference resolve? Is an
element's own dictionary in scope for its own attributes? Can a `GridLength` be
declared as an object element? Does an `x:Double` satisfy a property whose type
is `GridLength`? Those have no inline twin — writing one would be inventing the
answer — and a rejection of them by the runtime is the finding rather than a
broken corpus. `report_measurements.py` treats them accordingly, and only
because the case said so before the run.

Everything else at L5 is accountable in the ordinary way: L5 is an authored
level, and a case there that the runtime refuses without having declared a
question fails CI exactly as an L1 case would.

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
| `x-directive` | 736 | `x:Key`, `x:Uid`, `x:DataType`, `x:Load` |
| `foreign-type` | 288 | WinUI 2 (`muxc:`) and Terminal's own controls |
| `event-attribute` | 180 | a code-behind to hang handlers on |
| `resource-element` | 149 | `<StaticResource>` lookup against a dictionary |
| `x-element` | 37 | `x:String` and friends as elements |

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

The measurements are CI output, not repository content: 581 files regenerated
deterministically in about two minutes. Fetch them from the artifact of a green
run. The digest committed under `oracles/` still records 541, which is the
corpus the last run saw — the 40 L5 cases were authored after it.

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
`10.0.26100.33158` covers 541 cases and no L5; the corpus now has 581 and does.
The first measurement run after that will stop with

    L5: new level, 40 cases

which is not the runtime answering differently — it is the corpus asking a
question the digest has never seen. The gate cannot tell those apart on its own
and does not try to: it stops, and a person decides. The resolution is to take
that run's measurement artifact, confirm the L5 answers are what the cases were
written to ask, and commit the regenerated digest. Every level below L5 is still
compared as usual in the same run, so a real regression would be reported beside
the addition rather than hidden by it.

## Layout

    phase3/xaml-db/
      cases/L<n>-<group>/<id>.json         generated, authored or harvested case specs
      oracles/<os-build>.json              committed digest of what the runtime answered
      fonts/derived/<family>.json          committed metrics solved from the measurements
      schema/                              JSON Schema for both file kinds

    (CI artifact, not committed)
      measurements/<os-build>/<id>.json    filled in by CI on windows-latest
      measurements/<os-build>/report.json  per-level outcome and quarantine
      measurements/<os-build>/oracle.json  build and font identity of that run
      fonts/<family>.json                  metrics read off the runner's font

The vocabulary inventory is research data, not corpus data, so it lives with the
other pinned snapshots:

    research/windows-terminal/<commit>/xaml-inventory.json
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
so by name until those arrive. L5 is implemented and has no measurements at
all yet, so it contributes nothing to this number and is reported separately by
`check_twins.py` — which is the point of keeping the two reports apart. What is
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

A further 235 generated cases in `L1-shape`, `L2-content`, `L3-canvas` and
`L5-resources` are newer than the last oracle run for the same reason. They do
not move the numbers above in either direction until CI fills them in; they
exist because the answers they cover currently rest on one witness each, or on
none.
