# Font metrics

Text measurement needs numbers that live inside a font file — how tall a line
is, and how wide each character is. There are two files' worth of them here and
they are not the same kind of thing, so they are kept apart:

| | `fonts/*.json` | `fonts/derived/*.json` |
|---|---|---|
| where the numbers come from | read out of the font | solved out of the recorded measurements |
| how many | every advance the family covers | the two the corpus measures directly |
| in the repository | no — CI artifact | yes — committed text |
| written by | [`harvest_font_metrics.py`](../../scripts/harvest_font_metrics.py) | [`derive_font_metrics.py`](../../scripts/derive_font_metrics.py) |

Both declare a `provenance`, and `measure_cases` refuses a metrics file that
does not — an unlabelled one is exactly the file whose origin you would have to
guess at. It also refuses two files claiming one family, so a derived file
dropped next to a harvested one is an error rather than a coin toss. Pass one
directory or the other, never a mixture.

## The harvest, which is not committed

Segoe UI is not ours to redistribute and a `.ttf` is a binary either way, so the
font never leaves the machine that has it. Only the metrics travel:
`harvest_font_metrics.py` reads `head`, `hhea`, `OS/2`, `hmtx` and `cmap` on the
Windows runner — the same runner, in the same job, as the oracle those metrics
are checked against — and writes one JSON file per family.

Those files are CI output, on the same terms as `../measurements/`: they are a
reading taken off the runner's copy of a font we do not own, they are
regenerated deterministically by every run, and what pins them is committed
instead of them. For the measurements that is `../oracles/<os-build>.json`; for
the font it is the `font_segoeui` SHA-256 in the same digest, which moves for
exactly the reason a metric would — the font being serviced.

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

That gets 36 of the 72 level 4 cases — every one whose answer needs only a line
height and the advance of `M`. The other 36 measure `Terminal` and a pangram,
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
