#!/usr/bin/env python3
"""Record a font's ink as outlines, checked against the metrics already recorded.

Why this exists
---------------

113 render cases refuse with `DirectWrite could not resolve any requested
family in "Segoe UI"`. They are not layout failures: `xaml-db/fonts/segoe-ui.json`
carries every advance the corpus uses, harvested off the Windows runner, so
those cases measure with real numbers and then reach the draw with nothing to
draw. A widths table cannot be painted. The only honest way to close that
without installing a font we do not have is to record the *shapes* the same way
the widths were recorded: at a documented boundary, on the runner, as reviewable
numbers, with the binary staying where it is.

`phase3/harness/glyph_outline_probe.cpp` is that reading --
`IDWriteFontFace::GetGlyphRunOutline` into a recording geometry sink, in design
units. This script drives it, checks what comes back, and writes the artifact.

What it checks, and why each check is not optional
--------------------------------------------------

An outline harvest that disagrees with the metrics harvest is worse than no
outline harvest: the corpus verified those advances against the recorded
oracle, and painting glyphs from a *different* reading of the font would put
ink at positions the measurements do not describe, which is precisely the
"recoverable back to layout" property the render gate exists to hold. So:

* the family must be the same family;
* `units_per_em` must match, or every coordinate is on a different grid;
* the font file must be the same file -- compared by SHA-256, recomputed here
  from the path the probe reported, against the hash `harvest_font_metrics.py`
  recorded. Two Segoe UIs on one machine is not exotic;
* every codepoint's design advance must equal the harvested advance exactly.
  This is the load-bearing one. It is an independent reading of the same
  number through a different API, so agreement means the outline and the width
  came off the same glyph;
* every codepoint the metrics cover must have an outline. A partial harvest
  would paint some of a string and refuse the rest, which is a worse failure
  than refusing all of it, because it looks like it worked.

Licensing, which this script does not decide
--------------------------------------------

Metrics and outlines are different claims. The typeface design is generally
not copyrightable; the outline data in the font file generally is. Everything
this project records today sits on the metrics side of that line, and
`xaml-db/fonts/README.md` enumerates the tables that may be read -- head, hhea,
OS/2, hmtx, cmap, kern, GPOS -- and lists no outline table.

So this script refuses, by name, to harvest a family nobody has cleared:

    Segoe MDL2 Assets: outline harvesting is not cleared for this family.
    Recording a font's outlines is a different act from recording its
    metrics ... The decision is a licensing question and belongs to a human.

`CLEARED` is the committed list. Cascadia Mono is on it: SIL Open Font
License 1.1, and Terminal ships the file in its own repository, so the
outlines already travel. That is what the pipeline was proven on. Segoe UI is
on it too, and its entry is not a licence: it is a proprietary Microsoft
typeface, and the repository owner directed the harvest (2026-08-11) on the
terms the entry records -- the recording stays in short-lived CI artifacts,
read fresh each run from the runner's font file, and never enters the
repository. Both entries are what an entry has to be: a deliberate,
reviewable commit by somebody with the standing to make that call -- not a
flag a harvest run can pass.

    python3 phase3/scripts/harvest_glyph_outlines.py \\
        --family "Cascadia Mono" \\
        --metrics phase3/xaml-db/fonts/cascadia-mono.json \\
        --probe phase3/harness/glyph_outline_probe.exe \\
        --output phase3/xaml-db/glyph-outlines/cascadia-mono.json
"""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path
from typing import Any

SCHEMA_VERSION = 1

#: Families whose outlines may be recorded, and the reason that says so --
#: a licence that permits it, or a dated human decision to record anyway.
#: Adding to this is a licensing decision. See the module docstring.
CLEARED: dict[str, str] = {
    "Cascadia Mono": "SIL Open Font License 1.1; shipped in microsoft/terminal "
                     "as res/fonts/CascadiaMono.ttf",
    "Cascadia Code": "SIL Open Font License 1.1; shipped in microsoft/terminal "
                     "as res/fonts/CascadiaCode.ttf",
    "Segoe UI": "proprietary Microsoft typeface, not open-licensed; recorded "
                "at the repository owner's direction (2026-08-11); the "
                "recording lives only in short-lived CI artifacts, read fresh "
                "each run from the font file Microsoft installs on the "
                "runner, and never enters the repository",
}


class OutlineHarvestError(Exception):
    """Something that must stop the harvest rather than be recorded."""


def licence_refusal(family: str) -> str | None:
    """Why this family may not be harvested, or None if it may."""
    if family in CLEARED:
        return None
    return (f"{family}: outline harvesting is not cleared for this family. "
            f"Recording a font's outlines is a different act from recording "
            f"its metrics -- the metrics this project already keeps are not "
            f"generally copyrightable and the outline data generally is -- and "
            f"phase3/xaml-db/fonts/README.md authorises no outline table. "
            f"The decision is a licensing question and belongs to a human: add "
            f"the family to CLEARED in this script, in a reviewable commit, "
            f"with the licence that permits the recording or the recorded "
            f"decision to make it -- never an invented licence. Cleared today: "
            f"{', '.join(sorted(CLEARED))}.")


def sha256_of(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def check_against_metrics(outlines: dict[str, Any], metrics: dict[str, Any],
                          file_sha256: str | None = None) -> list[str]:
    """Every way the outline reading can disagree with the metrics reading.

    Empty means the two are readings of the same glyphs of the same file. See
    the module docstring for why each of these is fatal rather than a warning.
    """
    problems: list[str] = []
    family = metrics.get("family")
    if outlines.get("family") != family:
        problems.append(f"family: outlines say {outlines.get('family')!r}, "
                        f"metrics say {family!r}")
    if outlines.get("units_per_em") != metrics.get("units_per_em"):
        problems.append(
            f"units_per_em: outlines say {outlines.get('units_per_em')!r}, metrics "
            f"say {metrics.get('units_per_em')!r}; every coordinate is on a "
            f"different grid")

    recorded_hash = (metrics.get("source") or {}).get("sha256")
    if file_sha256 is not None and recorded_hash and file_sha256 != recorded_hash:
        problems.append(
            f"source: the probe read a file whose sha256 is {file_sha256}, but the "
            f"metrics were taken off {recorded_hash}; these are two different fonts")

    advances = metrics.get("advances") or {}
    shapes = outlines.get("outlines") or {}
    for codepoint in sorted(advances, key=int):
        shape = shapes.get(codepoint)
        if shape is None:
            problems.append(
                f"U+{int(codepoint):04X}: the metrics cover this codepoint and the "
                f"outline harvest does not; a partial harvest paints part of a "
                f"string and refuses the rest")
            continue
        if shape.get("advance") != advances[codepoint]:
            problems.append(
                f"U+{int(codepoint):04X}: design advance {shape.get('advance')!r} "
                f"from the outline reading, {advances[codepoint]!r} from the "
                f"metrics reading; these are not the same glyph")
    extra = sorted(set(shapes) - set(advances), key=int)
    if extra:
        # Not fatal in the same way -- an outline nothing measures is dead
        # weight rather than a contradiction -- but it means the two harvests
        # were pointed at different codepoint sets, which is a mistake.
        problems.append(
            f"outlines cover {len(extra)} codepoint(s) the metrics do not: "
            + ", ".join(f"U+{int(c):04X}" for c in extra[:8])
            + ("..." if len(extra) > 8 else ""))
    return problems


def check_shapes(outlines: dict[str, Any]) -> list[str]:
    """Structural checks on the recording itself, independent of any metrics."""
    problems: list[str] = []
    if outlines.get("schema_version") != SCHEMA_VERSION:
        problems.append(f"schema_version {outlines.get('schema_version')!r}, "
                        f"expected {SCHEMA_VERSION}")
    for codepoint, shape in sorted((outlines.get("outlines") or {}).items(),
                                   key=lambda item: int(item[0])):
        where = f"U+{int(codepoint):04X}"
        if shape.get("glyph_index") == 0:
            problems.append(f"{where}: glyph index 0 is .notdef; the font does not "
                            f"have this glyph and a box is not its shape")
        if shape.get("fill_mode") not in ("winding", "alternate"):
            problems.append(f"{where}: fill_mode {shape.get('fill_mode')!r}; which "
                            f"winding rule fills the glyph decides whether a "
                            f"counter is a hole or ink")
        for index, contour in enumerate(shape.get("contours") or []):
            start = contour.get("start")
            if not (isinstance(start, list) and len(start) == 2):
                problems.append(f"{where} contour {index}: start is not a point")
            for segment in contour.get("segments") or []:
                if not segment or segment[0] not in ("l", "c"):
                    problems.append(f"{where} contour {index}: segment kind "
                                    f"{segment[:1]!r} is neither a line nor a cubic")
                    continue
                wanted = 7 if segment[0] == "c" else 3
                if len(segment) != wanted:
                    problems.append(
                        f"{where} contour {index}: a {segment[0]!r} segment carries "
                        f"{len(segment) - 1} coordinate(s), expected {wanted - 1}")
    return problems


def probe(executable: Path, family: str, codepoints: list[int],
          font_file: Path | None = None) -> dict[str, Any]:
    """Run the Windows probe once and parse what it printed.

    `font_file` is what makes this exercisable: Cascadia Mono is the cleared
    family and it is not installed on the runner, it is a file in Terminal's
    own checkout. Naming the file skips family resolution entirely, so there
    is nothing that could resolve to a different font than the one meant.
    """
    argument = ",".join(f"{codepoint:x}" for codepoint in codepoints)
    command = [str(executable), family, argument]
    if font_file is not None:
        command.append(str(font_file))
    completed = subprocess.run(command, capture_output=True, text=True)
    if completed.returncode != 0:
        raise OutlineHarvestError(
            f"{executable.name} refused {family!r}: "
            f"{completed.stderr.strip() or f'exit {completed.returncode}'}")
    try:
        return json.loads(completed.stdout)
    except json.JSONDecodeError as error:
        raise OutlineHarvestError(
            f"{executable.name} printed something that is not JSON: {error}") from error


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--family", required=True)
    parser.add_argument("--metrics", type=Path, required=True,
                        help="the harvested metrics for this family, which the "
                             "outlines are checked against")
    parser.add_argument("--probe", type=Path, required=True,
                        help="phase3/harness/glyph_outline_probe.exe")
    parser.add_argument("--font-file", type=Path,
                        help="read the face from this file instead of resolving "
                             "the family through the system collection; this is "
                             "how a family that ships as a file rather than as "
                             "an installed face is reached")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)

    refusal = licence_refusal(args.family)
    if refusal:
        print(f"::error::{refusal}", file=sys.stderr)
        return 1

    metrics = json.loads(args.metrics.read_text(encoding="utf-8"))
    if metrics.get("provenance") != "harvested":
        print(f"::error::{args.metrics} has provenance "
              f"{metrics.get('provenance')!r}; outlines are checked against a "
              f"reading taken off the font, not against numbers solved from the "
              f"measurements", file=sys.stderr)
        return 1

    codepoints = sorted(int(key) for key in (metrics.get("advances") or {}))
    if not codepoints:
        print(f"::error::{args.metrics} covers no codepoints", file=sys.stderr)
        return 1

    try:
        outlines = probe(args.probe, args.family, codepoints, args.font_file)
    except OutlineHarvestError as error:
        print(f"::error::{error}", file=sys.stderr)
        return 1

    source_path = args.font_file or Path((outlines.get("source") or {}).get("file", ""))
    file_sha256 = sha256_of(source_path) if source_path.is_file() else None
    if file_sha256 is None:
        print(f"::warning::the probe read {source_path} and it is not readable "
              f"from here, so the outlines are not checked against the metrics' "
              f"source hash", file=sys.stderr)
    else:
        outlines.setdefault("source", {})["sha256"] = file_sha256

    outlines["provenance"] = "harvested"
    outlines["licence"] = CLEARED[args.family]

    problems = check_shapes(outlines) + check_against_metrics(outlines, metrics,
                                                              file_sha256)
    if problems:
        print(f"::error::the outline harvest of {args.family!r} disagrees with "
              f"{args.metrics}:", file=sys.stderr)
        for problem in problems:
            print(f"  {problem}", file=sys.stderr)
        return 1

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(outlines, indent=1, sort_keys=True) + "\n",
                           encoding="utf-8")
    glyphs = len(outlines.get("outlines") or {})
    contours = sum(len(shape.get("contours") or [])
                   for shape in (outlines.get("outlines") or {}).values())
    print(f"{args.output}: {glyphs} glyph(s), {contours} contour(s), checked "
          f"against every advance in {args.metrics.name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
