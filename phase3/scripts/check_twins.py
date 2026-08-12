#!/usr/bin/env python3
"""Check that a case measures the same as its inlined twin.

Some levels are authored before the oracle has ever run against them. L5 is the
first: resource dictionaries, `x:Key` and `{StaticResource}` went in with no
recorded measurement to hold them to, and inventing one would have been making
the answer up.

A twin is the check that can be made in the meantime. Every L5 case that hides
a value behind a resource is emitted alongside a second case with the same
value written inline, and the case names it in a `twin` field. The two describe
the same layout by construction, so any difference between them is the resource
system resolving to something other than what it was handed -- the whole failure
mode of a lookup, caught without an oracle.

"By construction" is the corpus author's claim, and the oracle is entitled to
refute it. Thirteen of the fifty-eight pairs it has now measured are refuted:
`L5-styles-implicit-target-type` declares an implicit Style and writes two
Borders under it, its twin writes the sizes inline, and the runtime records the
first pair of Borders unstyled -- so the two halves are not the same layout and
never were. `--expected` is where those recordings go. A pair the oracle records
as different is not a self-consistency check any more and is reported apart
from the ones that are, because holding an implementation to a premise the
runtime has already denied would be holding it to the wrong answer.

What this is not:

- It is **not** a check that the layout is right. Both halves of a pair can be
  wrong together and this will call them equal. Only `check_layout.py` against
  the recorded measurements says anything about correctness.
- It is **not** a check that the markup is valid XAML. Both halves can be
  rejected by the real runtime. Only a measurement run says that.

So a passing twin check is reported as self-consistency and never as coverage.
Pairs where both halves failed to load are counted separately for the same
reason: two identical failures agree about nothing.

    python3 phase3/scripts/check_twins.py --cases phase3/xaml-db/cases \\
        --results /tmp/layout-results \\
        --expected phase3/xaml-db/measurements/<build>
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parent))

from check_layout import DEFAULT_TOLERANCE, close  # noqa: E402


def load_cases(cases: Path) -> dict[str, dict[str, Any]]:
    found: dict[str, dict[str, Any]] = {}
    for path in sorted(cases.rglob("*.json")):
        case = json.loads(path.read_text(encoding="utf-8"))
        found[case["id"]] = case
    return found


def load_results(results: Path) -> dict[str, dict[str, Any]]:
    found: dict[str, dict[str, Any]] = {}
    for path in sorted(results.glob("*.json")):
        if path.name in ("oracle.json", "report.json"):
            continue
        payload = json.loads(path.read_text(encoding="utf-8"))
        found[payload.get("case_id", path.stem)] = payload
    return found


def compare(subject: dict[str, Any], twin: dict[str, Any], tolerance: float) -> list[str]:
    """Every way the two results disagree. Empty means they are the same tree."""
    problems: list[str] = []
    subject_nodes = {n["path"]: n for n in subject.get("tree", [])}
    twin_nodes = {n["path"]: n for n in twin.get("tree", [])}

    for path in sorted(set(twin_nodes) - set(subject_nodes)):
        problems.append(f"{path}: in the twin but not in the resolved case")
    for path in sorted(set(subject_nodes) - set(twin_nodes)):
        problems.append(f"{path}: in the resolved case but not in the twin")

    for path in sorted(set(subject_nodes) & set(twin_nodes)):
        mine, theirs = subject_nodes[path], twin_nodes[path]
        if mine["type"] != theirs["type"]:
            problems.append(f"{path}: type {mine['type']} but the twin has {theirs['type']}")
        for field in ("desired", "actual", "offset"):
            a, b = mine[field], theirs[field]
            if len(a) != len(b) or not all(close(x, y, tolerance) for x, y in zip(a, b)):
                problems.append(f"{path}: {field} {a} but the twin has {b}")
    return problems


def refuted_by(expected: dict[str, dict[str, Any]], case_id: str, twin_id: str,
               tolerance: float) -> list[str]:
    """How the oracle says the two halves differ. Empty means it agrees they pair."""
    subject, twin = expected.get(case_id), expected.get(twin_id)
    if subject is None or twin is None:
        return []
    subject_error, twin_error = subject.get("error"), twin.get("error")
    if subject_error and twin_error:
        # Two recorded failures agree about nothing, including about the
        # pairing, so the pair stays an ordinary twin check.
        return []
    if subject_error or twin_error:
        # One half loaded and the other did not: in the recorded environment
        # one of the halves is not a layout at all, which denies the pairing
        # more loudly than any differing number could. The L5-theme pairs are
        # the live case -- the probe host has no WinUI 2 dictionary, so the
        # resolved half fails by name while the inline half measures.
        # Demanding self-consistency here would demand resolving a key the
        # recorded runtime could not.
        which = "the resolved case" if subject_error else "the twin"
        return [f"{which} did not load in the recorded environment: "
                f"{subject_error or twin_error}"]
    return compare(subject, twin, tolerance)


def check(cases: Path, results: Path, tolerance: float,
          expected: Path | None = None) -> dict[str, Any]:
    by_id = load_cases(cases)
    measured = load_results(results)
    recorded = load_results(expected) if expected else {}

    matched: list[str] = []
    unverified: list[dict[str, str]] = []
    refuted: list[dict[str, str]] = []
    awaiting: list[dict[str, str]] = []
    mismatches: list[dict[str, str]] = []

    for case_id, case in sorted(by_id.items()):
        twin_id = case.get("twin")
        if not twin_id:
            continue

        def problem(detail: str) -> dict[str, str]:
            return {"id": case_id, "twin": twin_id, "detail": detail}

        # Asked before anything about the implementation, because it decides
        # whether there is a question to put to it at all.
        denied = refuted_by(recorded, case_id, twin_id, tolerance)
        if denied:
            refuted.append(problem(denied[0]))
            continue

        # A case carrying `oracle_decides` was authored saying the recording,
        # not self-consistency, is its judge -- its twin may legitimately
        # differ until the oracle has measured both halves. Holding such a
        # pair to self-consistency now would demand an answer the corpus
        # deliberately left open. Once both halves are recorded, the pair is
        # judged exactly like any other: refuted above, or compared below.
        if case.get("oracle_decides") and (case_id not in recorded
                                           or twin_id not in recorded):
            awaiting.append(problem("declares `oracle_decides` and the oracle "
                                    "has not recorded both halves yet"))
            continue

        if twin_id not in by_id:
            mismatches.append(problem(f"names a twin {twin_id} that is not in the corpus"))
            continue

        subject, twin = measured.get(case_id), measured.get(twin_id)
        if subject is None or twin is None:
            missing = case_id if subject is None else twin_id
            mismatches.append(problem(f"no result was produced for {missing}"))
            continue

        subject_error, twin_error = subject.get("error"), twin.get("error")
        if subject_error and twin_error:
            # Both refused for the same reason -- most often a level below this
            # one that is not satisfied locally, such as missing font metrics.
            # Identical failures are not evidence of anything, so they are
            # counted apart from the pairs that actually agree on a number.
            if subject_error == twin_error:
                unverified.append({"id": case_id, "twin": twin_id, "detail": subject_error})
            else:
                mismatches.append(problem(
                    f"failed differently: {subject_error!r} against {twin_error!r}"))
            continue
        if subject_error or twin_error:
            which = "the resolved case" if subject_error else "the twin"
            mismatches.append(problem(f"{which} failed to load: {subject_error or twin_error}"))
            continue

        problems = compare(subject, twin, tolerance)
        if problems:
            mismatches.extend(problem(detail) for detail in problems)
        else:
            matched.append(case_id)

    return {
        "schema_version": 1,
        "tolerance": tolerance,
        "matched": matched,
        "unverified": unverified,
        "refuted": refuted,
        "awaiting": awaiting,
        "mismatches": mismatches,
        "totals": {
            "pairs": (len(matched) + len(unverified) + len(refuted)
                      + len(awaiting) + len({m["id"] for m in mismatches})),
            "matched": len(matched),
            "unverified": len(unverified),
            "refuted": len(refuted),
            "awaiting": len(awaiting),
            "mismatched": len({m["id"] for m in mismatches}),
        },
    }


def summarise(result: dict[str, Any], limit: int = 25) -> str:
    totals = result["totals"]
    lines = [
        "| twin pairs | agree | not verified here | refuted by the oracle "
        "| awaiting the oracle | disagree |",
        "|---:|---:|---:|---:|---:|---:|",
        f"| {totals['pairs']} | {totals['matched']} | {totals['unverified']} "
        f"| {totals.get('refuted', 0)} | {totals.get('awaiting', 0)} "
        f"| {totals['mismatched']} |",
        "",
        "Self-consistency only: a pair that agrees says the value reached the "
        "property, not that the layout is right. The oracle says that.",
    ]
    if result.get("refuted"):
        lines += ["", f"### Refuted by the oracle ({len(result['refuted'])})", "",
                  "The recorded measurement has the two halves differing, so they "
                  "are not the same layout and the pair asks nothing. "
                  "`check_layout.py` holds both halves to the recording instead.",
                  ""]
        for item in result["refuted"][:limit]:
            lines.append(f"- `{item['id']}` vs `{item['twin']}` — {item['detail']}")
    if result.get("awaiting"):
        lines += ["", f"### Awaiting the oracle ({len(result['awaiting'])})", "",
                  "These pairs declare `oracle_decides`: the corpus authored "
                  "them as questions, and until the oracle records both halves "
                  "their disagreement is the open question restated, not a "
                  "defect.", ""]
        for item in result["awaiting"][:limit]:
            lines.append(f"- `{item['id']}` vs `{item['twin']}` — {item['detail']}")
    if result["unverified"]:
        lines += ["", f"### Not verified here ({len(result['unverified'])})", "",
                  "Both halves failed to load, identically, so the pair proves "
                  "nothing either way.", ""]
        for item in result["unverified"][:limit]:
            lines.append(f"- `{item['id']}` — {item['detail']}")
    if result["mismatches"]:
        lines += ["", f"### Disagreements ({len(result['mismatches'])})", ""]
        for item in result["mismatches"][:limit]:
            lines.append(f"- `{item['id']}` vs `{item['twin']}` — {item['detail']}")
        if len(result["mismatches"]) > limit:
            lines.append(f"- …and {len(result['mismatches']) - limit} more")
    return "\n".join(lines) + "\n"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cases", type=Path, required=True)
    parser.add_argument("--results", type=Path, required=True,
                        help="results from the implementation under test")
    parser.add_argument("--expected", type=Path,
                        help="recorded measurements; a pair the oracle records as "
                             "differing is reported apart rather than demanded of "
                             "the implementation")
    parser.add_argument("--tolerance", type=float, default=DEFAULT_TOLERANCE)
    parser.add_argument("--output", type=Path, help="write the full result as JSON")
    parser.add_argument("--summary", type=Path, help="where to append the markdown summary")
    args = parser.parse_args(argv)

    result = check(args.cases, args.results, args.tolerance, args.expected)
    text = summarise(result)

    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=1, sort_keys=True) + "\n",
                               encoding="utf-8")
    print(text, end="")
    if args.summary:
        with args.summary.open("a", encoding="utf-8") as handle:
            handle.write(text)

    if not result["totals"]["pairs"]:
        print("no twin pairs found; check --cases", file=sys.stderr)
        return 2
    return 1 if result["totals"]["mismatched"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
