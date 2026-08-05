#!/usr/bin/env python3
"""Solve a font's layout metrics back out of the recorded measurements.

The harvested metrics in `phase3/xaml-db/fonts/` come off the Windows runner,
which is the only machine that has the font. Nothing below level 4 needs them,
but a developer with no Segoe UI and no artifact still cannot run *any* text
case -- and part of level 4 does not actually need the font, because the corpus
measured it directly.

An empty `TextBlock` occupies exactly one line, so its recorded height is the
font's baseline-to-baseline distance at that size. A `TextBlock` holding one
character occupies one glyph, so its recorded width is that character's
advance. Both are recorded at three font sizes, and the rounding the runtime
applies (see `layout/src/text.cpp`) throws away enough that no single size
pins either number -- but the three together admit exactly one integer each.
That is what this solves for, by search: candidates are tried against every
recorded observation, and the answer is only accepted when exactly one
survives.

The result is *not* a harvest. It carries two numbers where a font carries
hundreds, the ascender/descender/line-gap split is unknowable from a line
height alone, and a character the corpus never measures on its own stays
missing on purpose so that the cases needing it fail by name. It is written to
`phase3/xaml-db/fonts/derived/`, never to `phase3/xaml-db/fonts/` itself, so
the two can never be confused for one another.

    python3 phase3/scripts/derive_font_metrics.py \\
        --measurements "$(python3 phase3/scripts/fetch_measurements.py)" \\
        --output phase3/xaml-db/fonts/derived/segoe-ui.json

    python3 phase3/scripts/derive_font_metrics.py --measurements ... --check
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
import xml.etree.ElementTree as ElementTree
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from measurement_digest import NOT_A_MEASUREMENT  # noqa: E402

XAML_NAMESPACE = "http://schemas.microsoft.com/winfx/2006/xaml/presentation"
TEXT_BLOCK = f"{{{XAML_NAMESPACE}}}TextBlock"

# Measurements are recorded to four decimals, so a prediction is consistent
# with one when it lands within half of that last digit. Doubled, to leave room
# for the float32 arithmetic on both sides.
RECORDED_PRECISION = 1e-4

# Advances and line heights snap to this fraction of a DIP. Derived from the
# corpus, not from a specification -- text.cpp states the evidence.
TEXT_UNITS_PER_DIP = 300.0

# Nothing in a text font is four ems wide or four ems tall. Bounding the search
# keeps "no candidate" a real answer rather than a timeout.
SEARCH_EMS = 4

# Segoe UI's, and the value essentially every Windows TrueType font uses. It is
# an input rather than a result: the measurements only ever constrain a metric
# divided by this, so any units-per-em admits a consistent answer. Reading the
# real one off the font is what the CI cross-check does.
DEFAULT_UNITS_PER_EM = 2048


class DerivationError(Exception):
    """The measurements do not pin a number down, or contradict each other."""


@dataclass(frozen=True)
class Sample:
    """One recorded `TextBlock`: what it held, and what it measured."""

    case_id: str
    text: str
    font_size: float
    width: float
    height: float


def float32(value: float) -> float:
    """The layout engine carries these as 32-bit floats; so does the model."""
    return struct.unpack("<f", struct.pack("<f", value))[0]


def snap(value: float) -> float:
    return round(value * TEXT_UNITS_PER_DIP) / TEXT_UNITS_PER_DIP


def agrees(predicted: float, recorded: float) -> bool:
    return abs(predicted - recorded) <= RECORDED_PRECISION


def describe(codepoint: int) -> str:
    return f"U+{codepoint:04X}"


# --- reading the corpus -------------------------------------------------------


def _lone_text_block(markup: str) -> ElementTree.Element | None:
    """The single `TextBlock` a case is, or None if it is anything else.

    Only a case that is one bare `TextBlock` can be read as a statement about
    the font: as soon as there is a parent or a sibling, the recorded size is
    the layout's answer rather than the text's.
    """
    try:
        root = ElementTree.fromstring(markup)
    except ElementTree.ParseError:
        return None
    if root.tag != TEXT_BLOCK or len(root):
        return None
    return root


def collect(cases: Path, measurements: Path, family: str) -> list[Sample]:
    """Every recorded lone-`TextBlock` case that used `family`."""
    recorded = {}
    for path in sorted(measurements.glob("*.json")):
        if path.name in NOT_A_MEASUREMENT:
            continue
        data = json.loads(path.read_text(encoding="utf-8"))
        recorded[data["case_id"]] = data

    samples: list[Sample] = []
    for path in sorted(cases.rglob("*.json")):
        case = json.loads(path.read_text(encoding="utf-8"))
        element = _lone_text_block(case.get("markup", ""))
        if element is None:
            continue

        environment = case.get("environment", {})
        if element.get("FontFamily", environment.get("font_family")) != family:
            continue

        measurement = recorded.get(case["id"])
        # A case the oracle refused to load says nothing about the font, and a
        # corpus measured before this case existed simply has no answer for it.
        if measurement is None or "error" in measurement:
            continue
        nodes = measurement.get("tree", [])
        if len(nodes) != 1:
            continue

        width, height = nodes[0]["actual"]
        samples.append(Sample(
            case_id=case["id"],
            text=element.text or "",
            font_size=float(element.get("FontSize", environment.get("font_size", 0.0))),
            width=float(width),
            height=float(height),
        ))
    return samples


# --- solving ------------------------------------------------------------------


def _sole_candidate(candidates: list[int], what: str, observations: int) -> int:
    if not candidates:
        raise DerivationError(
            f"no integer {what} reproduces every recorded measurement of it "
            f"({observations} of them); either the measurements disagree with the "
            f"model in layout/src/text.cpp, or the units per em is not what was "
            f"assumed")
    if len(candidates) > 1:
        raise DerivationError(
            f"the {what} is not pinned down: {len(candidates)} candidates "
            f"({candidates[0]}..{candidates[-1]}) reproduce every recorded "
            f"measurement of it ({observations} of them); a larger font size "
            f"would separate them")
    return candidates[0]


def solve_line_spacing(samples: list[Sample], units_per_em: int) -> int:
    """Baseline to baseline, in design units.

    Two rules, because the corpus records both: an empty `TextBlock` keeps the
    unsnapped line height, and one holding text gets the snapped one. Only
    single-glyph text is used for the second, since anything longer may have
    wrapped onto a second line and then the height is a multiple.
    """
    observations = [(s, len(s.text) == 1) for s in samples if len(s.text) <= 1]
    if not observations:
        raise DerivationError("no empty or single-character TextBlock was recorded; "
                              "nothing constrains the line spacing")

    candidates = []
    for spacing in range(1, SEARCH_EMS * units_per_em):
        if all(agrees(float32(snap(spacing * s.font_size / units_per_em))
                      if snapped else float32(spacing * s.font_size / units_per_em),
                      s.height)
               for s, snapped in observations):
            candidates.append(spacing)
    return _sole_candidate(candidates, "baseline-to-baseline distance", len(observations))


def solve_advances(samples: list[Sample],
                   units_per_em: int) -> tuple[dict[int, int], list[int]]:
    """Advance width per character, plus the characters that stay unsolved.

    A run of several characters constrains only the sum of their advances, so
    a character the corpus never measures on its own is reported rather than
    guessed at. That is the whole point: the cases that need it then fail by
    name instead of being measured against an invention.
    """
    alone: dict[int, list[Sample]] = {}
    for sample in samples:
        if len(sample.text) == 1:
            alone.setdefault(ord(sample.text), []).append(sample)
    if not alone:
        raise DerivationError("no single-character TextBlock was recorded; "
                              "nothing constrains any advance")

    solved: dict[int, int] = {}
    for codepoint, observations in sorted(alone.items()):
        candidates = [
            advance
            for advance in range(SEARCH_EMS * units_per_em)
            if all(agrees(float32(snap(advance * s.font_size / units_per_em)), s.width)
                   for s in observations)
        ]
        solved[codepoint] = _sole_candidate(
            candidates, f"advance for {describe(codepoint)}", len(observations))

    seen = {ord(character) for sample in samples for character in sample.text}
    return solved, sorted(seen - set(solved))


# --- the file -----------------------------------------------------------------


DERIVATION = (
    "NOT harvested from a font. Every number here was solved out of the recorded "
    "L{level} measurements by phase3/scripts/derive_font_metrics.py: an empty "
    "TextBlock's height is the baseline-to-baseline distance at that font size, and "
    "a one-character TextBlock's width is that character's advance, each pinned by "
    "being the only integer that reproduces every size the corpus records. What a "
    "line height cannot say is how the distance splits between ascender, descender "
    "and line gap, so the whole sum is carried on the ascender. Characters the "
    "corpus never measures alone are absent on purpose, so the cases needing them "
    "fail by name rather than against a guess. phase3/xaml-db/fonts is where real "
    "harvested metrics go; this file is not one and must not be used as one."
)


def derive(cases: Path, measurements: Path, family: str,
           units_per_em: int = DEFAULT_UNITS_PER_EM) -> dict:
    samples = collect(cases, measurements, family)
    if not samples:
        raise DerivationError(
            f"no recorded lone-TextBlock case uses the font family {family!r}")

    spacing = solve_line_spacing(samples, units_per_em)
    advances, unsolved = solve_advances(samples, units_per_em)

    return {
        "schema_version": 1,
        "family": family,
        "provenance": "derived",
        "derivation": DERIVATION.format(level=4),
        "units_per_em": units_per_em,
        # The split is not observable, only the sum. Putting all of it on the
        # ascender is a placement, not a measurement; LineSpacing() is the only
        # thing layout reads.
        "hhea": {"ascender": spacing, "descender": 0, "line_gap": 0},
        # Keyed by decimal codepoint, as the harvester writes them, so the two
        # kinds of file read the same way.
        "advances": {str(codepoint): advance for codepoint, advance in advances.items()},
        # What the corpus measures only inside a word, and so cannot answer.
        # This is the list of characters the real harvest is needed for.
        "unsolved": unsolved,
    }


def render(metrics: dict) -> str:
    return json.dumps(metrics, indent=1, sort_keys=True) + "\n"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--measurements", type=Path, required=True,
                        help="a recorded measurement directory (fetch_measurements.py)")
    parser.add_argument("--cases", type=Path,
                        default=Path(__file__).resolve().parents[1] / "xaml-db" / "cases")
    parser.add_argument("--family", default="Segoe UI")
    parser.add_argument("--units-per-em", type=int, default=DEFAULT_UNITS_PER_EM,
                        help="the design grid the answer is expressed on")
    parser.add_argument("--output", type=Path,
                        default=(Path(__file__).resolve().parents[1] / "xaml-db"
                                 / "fonts" / "derived" / "segoe-ui.json"))
    parser.add_argument("--check", action="store_true",
                        help="verify --output already holds what the measurements "
                             "imply, and change nothing")
    args = parser.parse_args(argv)

    try:
        metrics = derive(args.cases, args.measurements, args.family, args.units_per_em)
    except DerivationError as failure:
        print(f"cannot derive metrics for {args.family!r}: {failure}", file=sys.stderr)
        return 1

    body = render(metrics)
    if args.check:
        if not args.output.exists():
            print(f"{args.output} does not exist; the derived metrics are committed",
                  file=sys.stderr)
            return 1
        if args.output.read_text(encoding="utf-8") != body:
            print(f"::error::{args.output} is not what the recorded measurements imply",
                  file=sys.stderr)
            stored = json.loads(args.output.read_text(encoding="utf-8"))
            for field in ("units_per_em", "hhea", "advances", "unsolved"):
                if stored.get(field) != metrics[field]:
                    print(f"  {field}: committed {stored.get(field)}, "
                          f"measurements imply {metrics[field]}", file=sys.stderr)
            return 1
        print(f"{args.output.name} matches what the measurements imply", file=sys.stderr)
        return 0

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(body, encoding="utf-8")
    print(f"{args.family}: baseline to baseline {metrics['hhea']['ascender']}, "
          f"{len(metrics['advances'])} advances solved, "
          f"{len(metrics['unsolved'])} characters left to the harvest",
          file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
