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

One metric crosses the line, and it is the interesting one. **Kerning is
derived, never harvested**, even when the layout core is measuring against a
harvest. The font's pair table is not what the runtime applies — see the open
question below — so `measure_cases` takes the pairs from the committed file:

    measure_cases <cases> <out> <harvested-fonts> \
        --kerning phase3/xaml-db/fonts/derived/segoe-ui.json

and a harvested file that carries a `kerning` key is rejected on sight rather
than believed. What the font's own tables say is harvested anyway, under
`font_kerning`, where nothing reads it.

## The harvest, which is not committed

Segoe UI is not ours to redistribute and a `.ttf` is a binary either way, so the
font never leaves the machine that has it. Only the metrics travel:
`harvest_font_metrics.py` reads `head`, `hhea`, `OS/2`, `hmtx`, `cmap` and the
pair kerning in `kern` and `GPOS` on the Windows runner — the same runner, in
the same job, as the oracle those metrics are checked against — and writes one
JSON file per family.

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

Three families are harvested, not one:

| family | file on the runner | codepoints asked for |
|---|---|---|
| Segoe UI | `segoeui.ttf` | U+0020–U+007E, the corpus's text |
| Segoe MDL2 Assets | `segmdl2.ttf` | the 44 glyphs Terminal's markup names |
| Segoe Fluent Icons | `SegoeIcons.ttf` | the same 44 |

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

**What a weight the metrics were not read at adds.** `FontWeight="Black"` on a
square icon is recorded 11 wide at size 10 and 15 at size 14, where the
unweighted glyph is 10 and 14. More than one rule reproduces both — a whole DIP,
a twenty-fourth of the em, two percent of it — so two observations do not pin it.
A third size, or a harvest of the bold face, would.

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

That gets 46 of the 147 level 4 cases — every one whose answer needs only a line
height and the advance of `M`, including the six `FontIcon` cases deliberately
written in Segoe UI so the icon sizing rule could be pinned before any icon
metric was trusted. The text cases that remain measure `Terminal` and a pangram,
which constrain the *sum* of several advances and not any one of them, so those
characters are absent from the derived file on purpose and the cases that need
them fail by name:

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

## Open question: which pairs the runtime actually applies

The font's kern table is **not** the list of pairs the runtime applies, and the
corpus is what proves it. Segoe UI on build 10.0.26100.33158 kerns five pairs
among the characters the recorded runs contain:

| pair | the font says | the runtime applied it |
|---|---:|---|
| `Te` | −200 | yes |
| `ro` | −27 | no |
| `ox` | −25 | no |
| `ve` | −12 | no |
| `rm` | −4 | no |

`Terminal` is 200 design units narrower than its advances add up to at all three
recorded sizes — exactly `Te` and nothing else, so `rm` was not applied — and
the pangram, which contains `ro`, `ox` and `ve`, measures the raw sum of its
advances at all three. That is not a near miss: applying the whole table puts
the pangram 0.38 DIPs out at size 12 and 0.75 at size 24.

So a rule picks the survivors, and **we do not know what it is.** Three
candidates the data does not separate:

* **the source table.** `Te` may live in `GPOS` and the other four in the
  legacy `kern` table, or the reverse. This is why `font_kerning` records the
  two apart instead of merging them, and the next measurement run answers it
  for free — no new cases needed, just a look at which side each pair is on.
* **magnitude.** −200 survived and −4 to −27 did not. A threshold is a poor
  explanation for a text engine, but four observations cannot rule it out.
* **the glyph classes.** `Te` is uppercase-then-lowercase and the other four
  are lowercase pairs, so a subtable or class restriction would separate them
  exactly as observed.

Nothing here guesses between them. The layout core applies the committed list
and only that, so a case whose answer depends on an unwitnessed pair is wrong
by the amount the runtime is wrong by, and not by an invented rule.

What would settle it: short runs holding exactly one candidate pair each, at
several sizes, so every pair is witnessed on its own instead of inside a word
with six others. Those are authored in
[`generate_cases.py`](../../scripts/generate_cases.py) as the `L4-kern` group.
