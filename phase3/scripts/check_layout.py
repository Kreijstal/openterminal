#!/usr/bin/env python3
"""Compare an implementation's layout results against the recorded oracle.

Both halves of the contract are checked. An implementation can arrange the
right final rect while measuring wrongly -- that only surfaces later, under a
panel that consults DesiredSize -- so a wrong `desired` is a failure even when
`actual` lands exactly.

Input on both sides is the shape the probe emits:

    {"case_id": "...", "tree": [{"path", "type", "desired", "actual", "offset"}]}

so the same file format serves as expectation and as result, and a subject
under test only has to write what the probe writes.

Exits non-zero if anything mismatches.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parent))

from measurement_digest import level_of, load_measurements  # noqa: E402

# Recorded at four decimals, so anything at or below this is spelling, not
# disagreement. Deliberately tight: layout bugs are pixels, not fractions.
DEFAULT_TOLERANCE = 0.01


def close(expected: Any, actual: Any, tolerance: float) -> bool:
    # Infinity and NaN arrive as strings; they compare as tokens, since NaN is
    # not equal to itself and an epsilon around infinity means nothing.
    if isinstance(expected, str) or isinstance(actual, str):
        return expected == actual
    return abs(expected - actual) <= tolerance


class Mismatch(dict):
    """One disagreement, in a form that prints and serialises."""

    def __str__(self) -> str:
        where = f"{self['case_id']} {self['path']}" if self.get("path") else self["case_id"]
        return f"{where}: {self['detail']}"


def compare_case(
    case_id: str,
    expected: dict[str, Any],
    actual: dict[str, Any] | None,
    tolerance: float,
) -> list[Mismatch]:
    def note(detail: str, path: str = "") -> Mismatch:
        return Mismatch(case_id=case_id, path=path, detail=detail)

    if actual is None:
        return [note("no result produced")]

    # A case the oracle could not load is not an expectation about layout, so
    # there is nothing to hold an implementation to.
    if "error" in expected:
        return []
    if "error" in actual:
        return [note(f"implementation failed: {actual['error']}")]

    exp_nodes = {n["path"]: n for n in expected.get("tree", [])}
    act_nodes = {n["path"]: n for n in actual.get("tree", [])}

    problems: list[Mismatch] = []
    for path in sorted(set(exp_nodes) - set(act_nodes)):
        problems.append(note("node missing from the result", path))
    for path in sorted(set(act_nodes) - set(exp_nodes)):
        problems.append(note("node not in the oracle", path))

    for path in sorted(set(exp_nodes) & set(act_nodes)):
        want, got = exp_nodes[path], act_nodes[path]
        if want["type"] != got["type"]:
            problems.append(
                note(f"type {want['type']} but result has {got['type']}", path)
            )
        for field in ("desired", "actual", "offset"):
            w, g = want[field], got[field]
            if len(w) != len(g) or not all(
                close(a, b, tolerance) for a, b in zip(w, g)
            ):
                problems.append(note(f"{field} {w} but result has {g}", path))
    return problems


def check(
    expected_dir: Path,
    actual_dir: Path,
    levels: set[str] | None,
    tolerance: float,
) -> dict[str, Any]:
    expected = load_measurements(expected_dir)
    actual = load_measurements(actual_dir)

    if levels:
        expected = {k: v for k, v in expected.items() if level_of(k) in levels}

    by_level: dict[str, dict[str, int]] = {}
    mismatches: list[Mismatch] = []

    for case_id in sorted(expected):
        level = level_of(case_id)
        entry = by_level.setdefault(level, {"cases": 0, "passed": 0, "failed": 0})
        entry["cases"] += 1
        problems = compare_case(case_id, expected[case_id], actual.get(case_id), tolerance)
        if problems:
            entry["failed"] += 1
            mismatches.extend(problems)
        else:
            entry["passed"] += 1

    return {
        "schema_version": 1,
        "tolerance": tolerance,
        "levels": by_level,
        "mismatches": [dict(m) for m in mismatches],
        "totals": {
            "cases": sum(e["cases"] for e in by_level.values()),
            "passed": sum(e["passed"] for e in by_level.values()),
            "failed": sum(e["failed"] for e in by_level.values()),
        },
    }


def summarise(result: dict[str, Any], limit: int = 25) -> str:
    lines = ["| level | cases | passed | failed |", "|---|---:|---:|---:|"]
    for level, entry in sorted(result["levels"].items()):
        lines.append(f"| {level} | {entry['cases']} | {entry['passed']} | {entry['failed']} |")
    mismatches = result["mismatches"]
    if mismatches:
        lines += ["", f"### Mismatches ({len(mismatches)})", ""]
        for item in mismatches[:limit]:
            lines.append(f"- {Mismatch(item)}")
        if len(mismatches) > limit:
            lines.append(f"- …and {len(mismatches) - limit} more")
    return "\n".join(lines) + "\n"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--expected", type=Path, required=True,
                        help="oracle measurements (see fetch_measurements.py)")
    parser.add_argument("--actual", type=Path, required=True,
                        help="results from the implementation under test")
    parser.add_argument("--levels", default="",
                        help="comma-separated, e.g. L1,L2,L3; default is every level")
    parser.add_argument("--tolerance", type=float, default=DEFAULT_TOLERANCE)
    parser.add_argument("--output", type=Path, help="write the full result as JSON")
    args = parser.parse_args(argv)

    levels = {part.strip() for part in args.levels.split(",") if part.strip()}
    result = check(args.expected, args.actual, levels or None, args.tolerance)

    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=1, sort_keys=True) + "\n",
                               encoding="utf-8")
    print(summarise(result), end="")

    if not result["totals"]["cases"]:
        print("no cases compared; check --expected and --levels", file=sys.stderr)
        return 2
    return 1 if result["totals"]["failed"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
