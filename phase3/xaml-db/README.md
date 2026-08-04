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
| L0 | property system: defaults, local values, precedence | generated |
| L1 | one element, no children: explicit size, margin, padding | generated |
| L2 | one parent, one child: alignment × margin × sizing | generated |
| L3 | panels: `StackPanel`, `Grid` (Auto/Star/Pixel, spans), `Canvas` | generated |
| L4 | text: `TextBlock` with a pinned font | generated |
| L5 | resources, styles, templates, precedence | authored |
| L6 | visual states and storyboards, sampled at t=0 and t=end | authored |
| L7 | Terminal's own pages | harvested |

L0–L4 are *generated*, not hand-written. A few hundred lines of generator emits
the cross product, which is where the coverage comes from. Only L5–L6 need
authoring, and L7 is a harvest of the real pages.

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

- a case **we wrote** (L0–L4) that fails to load is a broken corpus, and fails CI;
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

The measurements are CI output, not repository content: 541 files regenerated
deterministically in about two minutes. Fetch them from the artifact of a green
run.

    python3 phase3/scripts/fetch_measurements.py          # prints the directory
    python3 phase3/scripts/check_layout.py \
        --expected <that directory> --actual <your results> --levels L1,L2,L3

`check_layout.py` takes results in the same shape the probe writes, so an
implementation under test only has to emit what the probe emits. It compares
`desired`, `actual` and `offset` per node against a tolerance, and treats a
case the *oracle* could not load as no expectation at all.

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

## Layout

    phase3/xaml-db/
      cases/L<n>-<group>/<id>.json         generated, authored or harvested case specs
      oracles/<os-build>.json              committed digest of what the runtime answered
      schema/                              JSON Schema for both file kinds

    (CI artifact, not committed)
      measurements/<os-build>/<id>.json    filled in by CI on windows-latest
      measurements/<os-build>/report.json  per-level outcome and quarantine
      measurements/<os-build>/oracle.json  build and font identity of that run

The vocabulary inventory is research data, not corpus data, so it lives with the
other pinned snapshots:

    research/windows-terminal/<commit>/xaml-inventory.json
    research/nuget/microsoft.windows.sdk.contracts/<version>/xaml-members.json

## Progress metric

Once a reimplementation exists, a Linux job runs it against the same corpus and
diffs. "N of M cases matching", per level, is the project's progress number —
and unlike a page screenshot it says exactly what to fix next.
