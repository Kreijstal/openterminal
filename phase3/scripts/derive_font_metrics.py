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
    # A wrapped run's recorded width is its longest line, not the whole run, so
    # it constrains nothing about the pairs that fell on another line.
    wraps: bool = False
    # Whether the text arrived as the Text property rather than as element
    # content. The runtime measures the two differently -- rule 7 in
    # text.cpp -- so the solver has to know which it is looking at.
    from_property: bool = False


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
            wraps=element.get("TextWrapping", "NoWrap") != "NoWrap",
            from_property=element.get("Text") is not None,
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


def run_width(text: str, advances: dict[int, int], kerning: dict[tuple[str, str], int],
              units_per_em: int, font_size: float,
              anywhere: set[tuple[str, str]] | None = None,
              snaps: bool = True) -> float:
    """What a single line of `text` measures, per the rules in text.cpp.

    A pair adjustment joins the first glyph's advance in design units and
    snaps with it. Snapping it on its own and adding it afterwards is a
    different computation -- at size 12 the two land 1/300 of a DIP apart --
    and the corpus records the first.

    `anywhere` is the subset of pairs that move a run wherever they occur,
    which is the subset the font's GPOS carries; everything else moves only the
    run's first pair. `snaps` is false for text that arrived as the Text
    property, which the runtime measures unsnapped. Both are rules 5 and 7.
    """
    reach = set() if anywhere is None else anywhere
    width = 0.0
    for index, character in enumerate(text):
        advance = advances[ord(character)]
        if index + 1 < len(text):
            pair = (character, text[index + 1])
            if index == 0 or pair in reach:
                advance += kerning.get(pair, 0)
        step = advance * font_size / units_per_em
        width = float32(width + (snap(step) if snaps else step))
    return width


def solve_pairs(samples: list[Sample], advances: dict[int, int],
                units_per_em: int) -> dict[tuple[str, str], int]:
    """One adjustment per recorded two-character run.

    Two characters is what makes a pair solvable. A longer run constrains the
    sum of everything in it, which is why the corpus grew a case per pair
    rather than being asked to separate them afterwards; with exactly two
    glyphs the recorded width names one integer and the search confirms it is
    the only one.

    The advances still have to come from the harvest -- a run of two constrains
    a pair only once both its advances are known -- which is the direction the
    checked-not-trusted bargain runs in.
    """
    usable = [s for s in samples
              if not s.wraps and len(s.text) == 2
              and all(ord(c) in advances for c in s.text)]
    if not usable:
        raise DerivationError(
            "no recorded two-character run uses only harvested characters; "
            "nothing constrains any pair")

    # A kern past half an em is not a kern, it is a different glyph.
    bound = units_per_em // 2
    solved: dict[tuple[str, str], int] = {}
    for sample in usable:
        pair = (sample.text[0], sample.text[1])
        candidates = [
            value for value in range(-bound, bound + 1)
            if agrees(run_width(sample.text, advances, {pair: value}, units_per_em,
                                sample.font_size, snaps=not sample.from_property),
                      sample.width)
        ]
        value = _sole_candidate(candidates, f"adjustment for the pair {''.join(pair)!r}", 1)
        if pair in solved and solved[pair] != value:
            raise DerivationError(
                f"the pair {''.join(pair)!r} is recorded at two different adjustments, "
                f"{solved[pair]} and {value}; the measurements contradict each other")
        solved[pair] = value
    # A pair the font does not move is not a pair. Recording the zeroes would
    # make the file say the corpus had found something where it found nothing.
    return {pair: value for pair, value in sorted(solved.items()) if value}


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
           units_per_em: int = DEFAULT_UNITS_PER_EM,
           harvested_advances: dict[int, int] | None = None) -> dict:
    samples = collect(cases, measurements, family)
    if not samples:
        raise DerivationError(
            f"no recorded lone-TextBlock case uses the font family {family!r}")

    spacing = solve_line_spacing(samples, units_per_em)
    advances, unsolved = solve_advances(samples, units_per_em)

    kerning = None
    if harvested_advances is not None:
        kerning = solve_pairs(samples, harvested_advances, units_per_em)

    metrics = {
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
    if kerning is not None:
        # Which pairs the runtime applies, which is a measurement and not a
        # reading. The font's own table says more than this and the recorded
        # runs prove the extra was not applied, so the layout core is given
        # this and never the font's -- see phase3/xaml-db/fonts/README.md.
        metrics["kerning"] = {pair_key(left, right): value
                              for (left, right), value in sorted(kerning.items())}
    return metrics


def render(metrics: dict) -> str:
    return json.dumps(metrics, indent=1, sort_keys=True) + "\n"


# --- the committed kerning, held to fresh measurements ------------------------


def pair_key(left: str, right: str) -> str:
    """How a metrics file spells a pair: two decimal codepoints, comma joined."""
    return f"{ord(left)},{ord(right)}"


def pair_of(key: str) -> tuple[str, str]:
    """"84,101" -> ("T", "e")."""
    left, right = (chr(int(part)) for part in key.split(","))
    return (left, right)


def read_pairs(kerning: dict[str, int]) -> dict[tuple[str, str], int]:
    """The stored spelling back into character pairs."""
    out = {}
    for key, value in kerning.items():
        left, right = (chr(int(part)) for part in key.split(","))
        out[(left, right)] = value
    return out


def check_kerning(cases: Path, measurements: Path, harvest: dict, family: str,
                  committed: dict[tuple[str, str], int]) -> list[str]:
    """Hold the committed pair adjustments to fresh measurements, both ways.

    Three checks, and each one has caught something:

      * the pairs the recorded two-character runs imply are exactly the
        committed ones, at the same values -- a missing pair and an extra one
        are both failures;
      * every committed value is what the font's own tables say, GPOS winning
        where they disagree, which is what makes the harvest a check on the
        recordings rather than a second opinion nobody compares;
      * and the whole model reproduces every recorded run, which is the only
        thing that tests rule 5 -- that a legacy-table pair moves the front of
        a run and nothing else. The run that found it, "{StaticResource
        NotAKey}", is a level 5 case and would otherwise be checked by nothing
        here.
    """
    advances = {int(codepoint): advance
                for codepoint, advance in harvest["advances"].items()}
    units_per_em = harvest["units_per_em"]
    tables = harvest.get("font_kerning", {})
    gpos = {pair_of(key) for key in tables.get("gpos", {})}
    font_pairs = {}
    for table in ("kern", "gpos"):          # gpos last, so it wins
        for key, value in tables.get(table, {}).items():
            font_pairs[pair_of(key)] = value

    samples = collect(cases, measurements, family)
    if not samples:
        return [f"no recorded lone-TextBlock case uses {family!r}"]

    problems: list[str] = []
    try:
        implied = solve_pairs(samples, advances, units_per_em)
    except DerivationError as failure:
        return [str(failure)]

    for pair in sorted(set(implied) | set(committed)):
        name = "".join(pair)
        if implied.get(pair) != committed.get(pair):
            problems.append(f"pair {name}: committed {committed.get(pair)}, "
                            f"the recorded runs imply {implied.get(pair)}")
        elif font_pairs.get(pair) != committed.get(pair):
            problems.append(f"pair {name}: committed {committed.get(pair)}, "
                            f"the font says {font_pairs.get(pair)}")

    for sample in samples:
        if sample.wraps or len(sample.text) < 2:
            continue
        if not all(ord(c) in advances for c in sample.text):
            continue
        predicted = run_width(sample.text, advances, font_pairs, units_per_em,
                              sample.font_size, anywhere=gpos,
                              snaps=not sample.from_property)
        if not agrees(predicted, sample.width):
            problems.append(
                f"{sample.case_id}: {sample.text[:32]!r} at {sample.font_size} measures "
                f"{sample.width}, the model predicts {predicted:.4f}")
    return problems


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
                             "imply, and change nothing. The kerning is left to "
                             "--check-kerning, which needs the harvest")
    parser.add_argument("--advances-from", type=Path, default=None,
                        help="a harvested metrics file. Kerning needs it: a word "
                             "constrains the sum of its advances and not any one of "
                             "them, so there is nothing to solve a pair against until "
                             "the harvest supplies them")
    parser.add_argument("--check-kerning", action="store_true",
                        help="hold --output's committed pair adjustments to the "
                             "recorded runs, in both directions, and derive nothing. "
                             "Requires --advances-from")
    args = parser.parse_args(argv)

    harvested_advances = None
    if args.advances_from:
        harvest = json.loads(args.advances_from.read_text(encoding="utf-8"))
        harvested_advances = {int(codepoint): advance
                              for codepoint, advance in harvest["advances"].items()}

    if args.check_kerning:
        if harvested_advances is None:
            print("--check-kerning needs --advances-from: without the advances a "
                  "recorded word says nothing about any pair", file=sys.stderr)
            return 2
        stored = json.loads(args.output.read_text(encoding="utf-8"))
        problems = check_kerning(args.cases, args.measurements, harvest, args.family,
                                 read_pairs(stored.get("kerning", {})))
        if problems:
            print(f"::error::{args.output} is not the kerning the recorded "
                  f"measurements imply", file=sys.stderr)
            for problem in problems:
                print(f"  {problem}", file=sys.stderr)
            return 1
        print(f"{args.family}: the committed pair adjustments reproduce every "
              f"recorded run and every one of them is witnessed", file=sys.stderr)
        return 0

    try:
        metrics = derive(args.cases, args.measurements, args.family, args.units_per_em,
                         harvested_advances)
    except DerivationError as failure:
        print(f"cannot derive metrics for {args.family!r}: {failure}", file=sys.stderr)
        return 1

    if args.check:
        if not args.output.exists():
            print(f"{args.output} does not exist; the derived metrics are committed",
                  file=sys.stderr)
            return 1
        stored = json.loads(args.output.read_text(encoding="utf-8"))
        # Kerning has its own gate, because it is the one field that cannot be
        # derived without a harvest and this check has to work on a machine with
        # no font. Carrying the stored value through compares everything else
        # byte for byte, which is what pins the formatting too.
        compared = dict(metrics)
        if "kerning" in stored and "kerning" not in compared:
            compared["kerning"] = stored["kerning"]
        body = render(compared)
        if args.output.read_text(encoding="utf-8") != body:
            print(f"::error::{args.output} is not what the recorded measurements imply",
                  file=sys.stderr)
            for field in ("units_per_em", "hhea", "advances", "unsolved", "kerning"):
                if field in compared and stored.get(field) != compared[field]:
                    print(f"  {field}: committed {stored.get(field)}, "
                          f"measurements imply {compared[field]}", file=sys.stderr)
            return 1
        print(f"{args.output.name} matches what the measurements imply", file=sys.stderr)
        return 0

    body = render(metrics)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(body, encoding="utf-8")
    print(f"{args.family}: baseline to baseline {metrics['hhea']['ascender']}, "
          f"{len(metrics['advances'])} advances solved, "
          f"{len(metrics['unsolved'])} characters left to the harvest",
          file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
