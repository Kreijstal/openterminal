# Harvested font metrics

Empty in the repository, on purpose. What belongs here is CI output, the same
as `../measurements/`.

Text measurement needs numbers that live inside a font file — how tall a line
is, and how wide each character is. Segoe UI is not ours to redistribute and a
`.ttf` is a binary either way, so the font never leaves the machine that has
it. Only the metrics travel:
[`harvest_font_metrics.py`](../../scripts/harvest_font_metrics.py) reads
`head`, `hhea`, `OS/2`, `hmtx` and `cmap` on the Windows runner — the same
runner, in the same job, as the oracle those metrics are checked against — and
writes one JSON file per family.

To fill it in locally:

    python3 phase3/scripts/fetch_measurements.py --fonts   # prints a directory

and pass that to `measure_cases` as its third argument, or copy it here, which
is where `measure_cases` looks by default.

Without it, everything below level 4 works unchanged and the text cases fail
with `no harvested metrics for the font family "Segoe UI"` — naming what is
missing rather than measuring against whatever font happened to be installed.

## Why the harvest is checked, not trusted

The rules the layout core measures text with were derived from the recorded
measurements rather than ported from a source, so they need to be falsifiable.
[`font-metrics-from-oracle.json`](../../tests/data/font-metrics-from-oracle.json)
holds the two numbers those rules imply — Segoe UI's baseline-to-baseline
distance, and the advance width of `M` — solved out of the measurements alone.
The CI harvest is run with `--expect` pointing at it, so reading the same
numbers out of the font is an independent route to them. If the font and the
oracle disagree, the model is wrong and the job fails there, rather than
turning into pixel widths that are slightly off everywhere.
