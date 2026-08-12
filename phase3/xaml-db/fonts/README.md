# Font metrics

Text measurement needs numbers that live inside a font file — how tall a line
is, and how wide each character is. There are two files' worth of them here and
they are not the same kind of thing, so they are kept apart:

| | `fonts/*.json` | `fonts/derived/*.json` |
|---|---|---|
| where the numbers come from | read out of the font | solved out of the recorded measurements |
| how many | every advance the family covers | the three things the corpus measures directly |
| in the repository | no — CI artifact | yes — committed text |
| written by | [`harvest_font_metrics.py`](../../scripts/harvest_font_metrics.py) | [`derive_font_metrics.py`](../../scripts/derive_font_metrics.py) |

Both declare a `provenance`, and `measure_cases` refuses a metrics file that
does not — an unlabelled one is exactly the file whose origin you would have to
guess at. It also refuses two files claiming one family, so a derived file
dropped next to a harvested one is an error rather than a coin toss. Pass one
directory or the other, never a mixture.

Kerning is the one metric the two files spell differently, and on purpose. A
harvest writes `font_kerning` — the font's `GPOS` and legacy `kern` tables, kept
apart — and the layout core reads it. A derived file writes `kerning`, the pairs
the recordings witnessed one at a time, and that is the committed statement CI
holds the harvest to. A harvest claiming `kerning` is rejected on sight: it is
the shape of a run that predates the `L4-kern` series, and the two are not the
same claim.

Why the tables are kept apart rather than merged is the whole of "Which pairs
the runtime applies" below, and it is not a detail: the two behave differently.

## The harvest, which is not committed

Segoe UI is not ours to redistribute and a `.ttf` is a binary either way, so the
font never leaves the machine that has it. Only the metrics travel:
`harvest_font_metrics.py` reads `head`, `hhea`, `OS/2`, `hmtx`, `cmap` and the
pair kerning in `kern` and `GPOS` on the Windows runner — the same runner, in
the same job, as the oracle those metrics are checked against — and writes one
JSON file per family.

That list of tables is exhaustive on purpose, and no outline table is on it.
`glyf`, `loca`, `CFF ` and `CFF2` are the shapes, and reading shapes is a
heavier claim than reading widths — the typeface design is generally not
copyrightable, the outline data in the file generally is. Recording them is
what will let the 113 cases refusing `DirectWrite could not resolve any
requested family in "Segoe UI"` actually paint, and the machinery to do it
exists in `../glyph-outlines/`, gated on a committed per-family clearance
list. Segoe UI is on that list by the repository owner's recorded decision
(2026-08-11), artifact-only like everything else harvested; see that
directory's README.

The kern tables are read as **evidence**, into `font_kerning`, and the two
sources are kept apart — `{"gpos": {…}, "kern": {…}}` — rather than merged.
Nothing in the layout core reads either. Only the `kern` feature is taken out of
`GPOS`, since a font keeps pair adjustments there for other purposes and capital
spacing is off unless a shaper asks for it, and only for pairs of codepoints
that were asked for, so the block stays as reviewable as the advances. An icon
font's is empty, which is a reading and not a gap.

Why evidence and not metrics is the open question below.

Those files are CI output, on the same terms as `../measurements/`: they are a
reading taken off the runner's copy of a font we do not own, they are
regenerated deterministically by every run, and what pins them is committed
instead of them. For the measurements that is `../oracles/<os-build>.json`; for
the font it is the `font_segoeui` SHA-256 in the same digest, which moves for
exactly the reason a metric would — the font being serviced.

## The icon fonts

Four families are harvested, not one:

| family | file read | codepoints asked for |
|---|---|---|
| Segoe UI | `segoeui.ttf` on the runner | U+0020–U+007E, the corpus's text |
| Segoe MDL2 Assets | `segmdl2.ttf` on the runner | the 44 glyphs Terminal's markup names |
| Segoe Fluent Icons | `SegoeIcons.ttf` on the runner | the same 44 |
| Cascadia Mono | `res/fonts/CascadiaMono.ttf` in the Terminal checkout | U+0020–U+007E |

The last of those is not an icon font and is [its own
section](#the-fourth-family-which-terminal-ships-itself) below.

A `FontIcon`'s size is a glyph measured in an icon font, and fifteen level 7
cases are one `FontIcon` each — every one of them blocked on these numbers
rather than on layout. So the harvest reads the icon fonts too.

Which codepoints is not a decision made here. `harvest_icon_glyphs.py` reads
them out of the Terminal checkout — every literal `Glyph` attribute in WinUI
markup, markup extensions excluded because a `{TemplateBinding}` names a
codepoint only the binding knows — and writes them beside the vocabulary
inventory, at
[`research/windows-terminal/<commit>/icon-glyphs.json`](../../../research/windows-terminal/).
That file is committed and CI holds it to reproducing, exactly like the
inventory: the set is research data about Terminal, and a hand-typed list would
be wrong the first time Terminal changed an icon.

One `L4-icon` case is still an open question rather than a gap in the harvest,
and it is a refusal in the layout core rather than a number:

**Where the runtime goes when no named family has the glyph.**
`L4-icon-rule-mdl2-latin-14` asks Segoe MDL2 Assets for `M`, which it does not
have, and the oracle answers 10 wide by 14 tall. The 14 is the icon font's line
box, so that half is settled; the 10 is neither its em, nor Segoe UI's `M` at
that size (12.57), nor Cascadia Mono's (8.2) now that a fourth family is
harvested. So the runtime fell back past every family the markup names, to one
chosen by rules nothing here records. Harvesting more families would not answer
it — knowing *which* font it picked is the missing measurement. The Windows
workflow now uses DirectWrite's system fallback mapper for every uncovered
FontIcon glyph discovered in the generated corpus, then harvests the mapped
file through the same deterministic sfnt reader. The current downloaded
artifact predates that addition, so this case remains an explicit refusal until
the next oracle run.

The other one, what a simulated weight adds, was measured and is answered
below.

## The icon fonts

Four families are harvested, not one:

| family | file read | codepoints asked for |
|---|---|---|
| Segoe UI | `segoeui.ttf` on the runner | U+0020–U+007E, the corpus's text |
| Segoe MDL2 Assets | `segmdl2.ttf` on the runner | the 44 glyphs Terminal's markup names |
| Segoe Fluent Icons | `SegoeIcons.ttf` on the runner | the same 44 |
| Cascadia Mono | `res/fonts/CascadiaMono.ttf` in the Terminal checkout | U+0020–U+007E |

The last of those is not an icon font and is [its own
section](#the-fourth-family-which-terminal-ships-itself) below.

A `FontIcon`'s size is a glyph measured in an icon font, and fifteen level 7
cases are one `FontIcon` each — every one of them blocked on these numbers
rather than on layout. So the harvest reads the icon fonts too.

Which codepoints is not a decision made here. `harvest_icon_glyphs.py` reads
them out of the Terminal checkout — every literal `Glyph` attribute in WinUI
markup, markup extensions excluded because a `{TemplateBinding}` names a
codepoint only the binding knows — and writes them beside the vocabulary
inventory, at
[`research/windows-terminal/<commit>/icon-glyphs.json`](../../../research/windows-terminal/).
That file is committed and CI holds it to reproducing, exactly like the
inventory: the set is research data about Terminal, and a hand-typed list would
be wrong the first time Terminal changed an icon.

Two of the `L4-icon` cases are open questions rather than gaps in the harvest,
and both are refusals in the layout core rather than numbers:

**Where the runtime goes when no named family has the glyph.**
`L4-icon-rule-mdl2-latin-14` asks Segoe MDL2 Assets for `M`, which it does not
have, and the oracle answers 10 wide by 14 tall. The 14 is the icon font's line
box, so that half is settled; the 10 is neither its em nor Segoe UI's `M` at
that size, which is 12.57. So the runtime fell back past every family the markup
names, to one chosen by rules nothing here records. Harvesting more families
would not answer it — knowing *which* font it picked is the missing measurement.
The fallback probe added to the Windows workflow records that identity and its
DirectWrite scale without using the case's desired size as input.

**What a weight the metrics were not read at adds.** `FontWeight="Black"` on a
square icon is recorded 11 wide at size 10 and 15 at size 14, where the
unweighted glyph is 10 and 14. More than one rule reproduces both — a whole DIP,
a twenty-fourth of the em, two percent of it — so two observations do not pin it.
A third size, or a harvest of the bold face, would.

The third size is now asked for. Five `L4-icon-rule-mdl2-*` cases were added for
it, and they are the only thing in this directory that has never been measured:

| case | a whole DIP | a twenty-fourth | two per cent |
|---|---:|---:|---:|
| `mdl2-weight-100` | 101 | 104.17 | 102 |
| `mdl2-weight-200` | 201 | 208.33 | 204 |

Three distinct answers at each size, whichever way a fraction becomes a whole
number, and two sizes rather than one so the surviving rule is confirmed instead
of fitted. `mdl2-plain-100` and `mdl2-plain-200` measure the same glyph with no
weight beside them, so the difference is read off two recordings rather than off
this harvest's claim that the glyph is exactly one em wide. `mdl2-bold-100` asks
the other half of it: Bold is 700 where Black is 900, so a rule that scales with
the weight answers differently there and a rule that does not, does not.

That run has happened, and "Open question: what a simulated weight adds" below
is what it answered. The refusal in [`icon.cpp`](../../layout/src/icon.cpp) is
gone for `Bold` and `Black` and stands for every other weight, because those two
are the only ones anything measured.

Two things work differently for an icon font, and both are deliberate:

**A missing glyph is a finding, not a failure.** The two icon families do not
cover the same set — which is why Terminal writes
`FontFamily="Segoe UI, Segoe Fluent Icons, Segoe MDL2 Assets"` rather than
naming one — so `--missing record` puts the absent codepoints in the file's
`missing` list instead of failing the harvest. For Segoe UI the default stays
`--missing fail`: an ASCII glyph absent from a text font means the wrong file
was read. A font covering *none* of what was asked for fails either way.

**Nothing checks them yet.** The Segoe UI harvest runs with `--expect`, against
numbers solved out of the recorded measurements; there is no equivalent for an
icon font because until the `L4-icon` cases are measured there was nothing to
solve from. Those cases exist to close that gap — `L4-icon-rule-segoeui-m-*`
in particular measures a `FontIcon` in a font whose advance the corpus already
knows, so the sizing rule can be pinned before any icon metric is trusted. Until
`derive_font_metrics.py` learns to solve an icon advance out of a `FontIcon`
measurement, the icon halves of this directory are read but unchecked, and their
identity travels as the `fonts` map in `../measurements/<build>/oracle.json`
rather than in the committed digest.

To fill this directory in locally:

    python3 phase3/scripts/fetch_measurements.py --fonts   # prints a directory

and pass that to `measure_cases` as its third argument, or copy it here, which
is where `measure_cases` looks by default.

## The fourth family, which Terminal ships itself

`L7-terminal-4edb490008` is a `ScrollViewer` around
`<TextBlock FontFamily="Cascadia Mono"/>`, and the layout core used to refuse all
three sizes of it with

    no harvested metrics for the font family "Cascadia Mono"

The oracle answered anyway, so the first thing to rule out is that the runner did
not have the font either and the runtime fell back to one that *is* harvested. It
did not. The `TextBlock` is empty, so its recorded height is one line box, and
the recording says 16.2695 at font size 14 — a ratio of 1.162109. None of the
three families above is anywhere near it:

| family | line box ÷ em | at 14 |
|---|---:|---:|
| Segoe UI | 1.330078 | 18.6211 |
| Segoe MDL2 Assets | 1.0 | 14 |
| Segoe Fluent Icons | 1.0 | 14 |
| **what was recorded** | **1.162109** | **16.2695** |

1.162109 is 2380 ÷ 2048, and `res/fonts/CascadiaMono.ttf` in the Terminal
checkout the level 7 cases are harvested from reads `hhea` 1900 / −480 at
2048 units per em: 2380, to the digit. So the runtime measured in Cascadia Mono,
and the only thing missing was a reading of a font file that the pinned checkout
already contains.

So it is read, from that checkout rather than from the runner image — the file
the recording matches is the one Terminal ships, and its identity travels with
the commit the whole level 7 harvest is pinned to. Nothing is solved out of the
measurement: the recording is what the harvest is then checked against, and with
the metrics in place the case reproduces every number in it, not only the line
box that identified the font.

## The derived two, which are

`derived/segoe-ui.json` holds the numbers the corpus answers on its own. An
empty `TextBlock` occupies one line, so its recorded height is the
baseline-to-baseline distance; a `TextBlock` holding one character occupies one
glyph, so its recorded width is that character's advance. `derive_font_metrics.py`
searches for the integer design-unit value that reproduces every recorded size,
and accepts it only when exactly one does.

Two numbers of committed text, solved from measurements the repository already
depends on, is the same bargain `../oracles/` strikes and it is worth making for
the same reason: it is small, it is reviewable as a diff, and without it nothing
checks the harvest. So this half is committed and the harvest is not.

    python3 phase3/scripts/derive_font_metrics.py \
        --measurements "$(python3 phase3/scripts/fetch_measurements.py)"

    phase3/layout/build/measure_cases phase3/xaml-db/cases /tmp/results \
        phase3/xaml-db/fonts/derived

That gets the level 4 cases whose answer needs only a line height and the advance
of `M`, including the six `FontIcon` cases deliberately written in Segoe UI so the
icon sizing rule could be pinned before any icon metric was trusted. The rest
measure `Terminal`, a pangram and the `L4-kern` pairs, which need advances the
corpus never sees alone, so those characters are absent from the derived file on
purpose and the cases that need them fail by name:

    the metrics derived from the recorded measurements have no advance for U+0054

Without either file, everything below level 4 works unchanged and all 72 text
cases fail with `no harvested metrics for the font family "Segoe UI"` — naming
what is missing rather than measuring against whatever font happened to be
installed.

## Why the harvest is checked, not trusted

The rules the layout core measures text with were derived from the recorded
measurements rather than ported from a source, so they need to be falsifiable.
The derived file is the check: reading the same two numbers out of the font is
an independent route to them, and CI runs the harvest with `--expect` pointing
at it. If the font and the oracle disagree, the model in
[`text.cpp`](../../layout/src/text.cpp) is wrong and the job fails there, rather
than turning into pixel widths that are slightly off everywhere.

The check runs in both directions. CI also re-derives the file from the
measurements it has just recorded (`derive_font_metrics.py --check`), so the
committed numbers cannot drift away from the measurements they were solved from.

Kerning is committed like the other two and checked unlike them, because it is
the one metric that cannot be solved from the corpus alone: a word constrains
the sum of its advances and not any one of them, so there is nothing to solve
against until a harvest supplies them. So its gate is separate, and takes the
advances from the harvest:

    python3 phase3/scripts/derive_font_metrics.py \
        --measurements "$(python3 phase3/scripts/fetch_measurements.py)" \
        --advances-from phase3/xaml-db/fonts/segoe-ui.json \
        --check-kerning

It runs in both directions, and both have already caught something:

* every recorded run has to come out right **with** the committed pairs
  applied, which catches a pair that is missing or has drifted;
* and every committed pair has to be **load-bearing** — droppable pairs are
  reported, because a pair no recorded number moves for is not something the
  measurements imply, whatever the font says about it.

Only one pair is ever solved for when the gate has to suggest a fix: a corpus
needing two at once could not separate them from the sums it records, and it
says so rather than choosing.

Both directions are Segoe UI's alone. The icon families have neither half yet,
and the section above says so rather than letting three harvested files look
equally well checked.

## Which pairs the runtime applies, answered

The font's kern table is not, on its own, the list of pairs the runtime applies
— but it is not the wrong list either. It took the `L4-kern` series to say how
the two differ, and the answer is about *reach*, not about which pairs count.

Every pair, measured on its own in a two-character run, moved the run by exactly
what the font says. All twelve, at every size recorded:

| pair | GPOS | legacy `kern` | measured | pair | GPOS | legacy `kern` | measured |
|---|---:|---:|---:|---|---:|---:|---:|
| `Te` | −200 | −211 | **−200** | `ry` | — | 82 | **82** |
| `Ta` | −230 | −217 | **−230** | `vo` | — | −12 | **−12** |
| `To` | −200 | −211 | **−200** | `yo` | — | −10 | **−10** |
| `Wa` | −80 | −76 | **−80** | `ox` | — | −25 | **−25** |
| `Ya` | −180 | −199 | **−180** | `rm` | — | −4 | **−4** |
| | | | | `ro` | — | −27 | **−27** |
| | | | | `ve` | — | −12 | **−12** |

Two things fall out of it. **Where the tables disagree, GPOS wins** — five pairs
disagree and the runtime took the GPOS value every time. And the split between
the two tables is exactly the split in behaviour: **a GPOS pair moves a run
wherever it sits; a pair only the legacy table carries moves the run's first
pair and nothing else.**

That is what reconciles the isolated runs with the words:

* `Terminal` is short by `Te` and not by `rm`. `rm` is legacy-only and sits at
  index 2.
* the pangram holds `ro`, `ox` and `ve`, all legacy-only and all mid-word, and
  measures the raw sum of its advances at all three sizes.
* `{StaticResource NotAKey}` is short by 153 units, which is `St` + `Re` + `Ke`
  at indices 1, 7 and 20 — three GPOS pairs, deep in the run — while `rc` at
  index 12, its one legacy-only pair, moves nothing.

So the three candidates the earlier evidence admitted are all settled:
**magnitude** is dead (`rm` at −4 applies in isolation while −27 does not apply
mid-word), **glyph class** is dead (seven lowercase pairs apply in isolation),
and **the source table** is the answer — though not in the shape it was posed.

### What is still open

**Why the legacy table is read at the front of a run at all.** "the first pair"
and "a run of exactly two glyphs" fit every recording equally well, because no
recorded run of three or more begins with a legacy-only pair. The layout core
implements the first. A three-character run starting with `ox`, `ro`, `ve` or
`rm` would separate them in one measurement.

**Whether the harvest sees all of GPOS.** `read_gpos_kerning` skips a
class-2 pair whose second glyph falls in class 0, so the GPOS column above may
be short. It cannot be short in a way that changes the rules — a pair missing
from it would have to apply everywhere and the pangram says these do not — but
a fuller reader may move pairs from the right-hand column to the left.

## Open question: what a simulated weight adds

`FontWeight` on a family that ships one face is simulated, and the five
`L4-icon-rule-mdl2-*` probes settled the shape of it: the simulation adds a
fixed fraction of the em to every advance, the line box is untouched, and the
fraction does not depend on how much heavier the weight was — `Bold` and `Black`
both measure 103 where the plain glyph measures 100.

What they do not settle is the fraction. Black is 103 at size 100, 205 at 200
and 15 at 14, which bounds it to (2%, 2.5%] of the em and no tighter: a
`FontIcon`'s desired width is a ceiling, so every value in that interval
produces those same three integers. The three rules the two smaller sizes once
admitted are all dead — a whole DIP gives 101/201, a twenty-fourth of the em
104.17/208.33, two per cent 102/204 — so the probes did their job; the interval
is simply where the ceiling stops.

`text.cpp` implements the closed end of the interval and says so. Nothing the
corpus records can tell the difference. What would: a `TextBlock` in an icon
font at `Black`, written as `Text=` so the recording keeps the unsnapped width
(see the next section), at two sizes.

## The two spellings of a TextBlock's text are not one measurement

The `L4-source` twins were authored to check that `Text="M"` and
`<TextBlock>M</TextBlock>` are the same thing. They are not:

| | width | height |
|---|---:|---:|
| `<TextBlock Text="M"/>` | 12.5713 | 18.6211 |
| `<TextBlock>M</TextBlock>` | 12.57 | 18.62 |

Inline content snaps every advance and the line height to 1/300 of a DIP; the
`Text` property keeps them unsnapped. The same split appears at `Terminal`
(52.042 against 52.04) and at `{StaticResource NotAKey}` (156.2285 against
156.2167), where the accumulated difference finally grows past the corpus's
tolerance and stops being a curiosity. Content becomes an implicit `Run` in the
`Inlines` collection and the property does not, so they are different text
sources in the runtime; that they are also different arithmetic is the finding.
