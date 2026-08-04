#!/usr/bin/env python3
"""Report what the real runtime made of the corpus, per level.

Two kinds of case are measured, and a failure means different things for each.

Levels 0 to 4 are written by us against the documented contract. If the runtime
refuses one, the case is wrong and the corpus is broken, so that is an error.

Level 7 is harvested from Terminal's markup, and whether a harvested subtree can
be loaded standalone is a *prediction* the harvester makes from type metadata.
The runtime is the arbiter. A rejected candidate is not a broken corpus, it is a
blocker the harvester failed to anticipate -- so it is quarantined and named,
which is what turns it into a fix rather than a mystery.
"""

from __future__ import annotations

import argparse
import json
import re
from collections import defaultdict
from pathlib import Path
from typing import Any

# Levels the corpus authors, and is therefore accountable for.
AUTHORED_LEVELS = range(0, 5)


def level_of(case: dict[str, Any]) -> int:
    return int(case.get("level", -1))


def load_cases(cases: Path) -> dict[str, dict[str, Any]]:
    found = {}
    for path in sorted(cases.rglob("*.json")):
        case = json.loads(path.read_text(encoding="utf-8"))
        found[case["id"]] = case
    return found


def load_measurements(measurements: Path) -> dict[str, dict[str, Any]]:
    found = {}
    for path in sorted(measurements.glob("*.json")):
        if path.name == "oracle.json":
            continue
        payload = json.loads(path.read_text(encoding="utf-8"))
        found[payload.get("case_id", path.stem)] = payload
    return found


def report(cases: Path, measurements: Path) -> dict[str, Any]:
    by_id = load_cases(cases)
    measured = load_measurements(measurements)

    levels: dict[int, dict[str, Any]] = defaultdict(
        lambda: {"cases": 0, "measured": 0, "errored": 0, "missing": 0}
    )
    quarantine: list[dict[str, str]] = []

    for case_id, case in sorted(by_id.items()):
        level = level_of(case)
        entry = levels[level]
        entry["cases"] += 1
        result = measured.get(case_id)
        if result is None:
            entry["missing"] += 1
            continue
        if "error" in result:
            entry["errored"] += 1
            quarantine.append({
                "id": case_id,
                "level": str(level),
                "error": result["error"],
                "markup": case.get("markup", ""),
            })
        else:
            entry["measured"] += 1

    failures = sorted(
        level for level in levels
        if level in AUTHORED_LEVELS
        and (levels[level]["errored"] or levels[level]["missing"])
    )

    return {
        "schema_version": 1,
        "levels": {str(level): dict(levels[level]) for level in sorted(levels)},
        "quarantine": quarantine,
        "authored_levels_failing": [str(level) for level in failures],
        "totals": {
            "cases": sum(e["cases"] for e in levels.values()),
            "measured": sum(e["measured"] for e in levels.values()),
            "errored": sum(e["errored"] for e in levels.values()),
            "missing": sum(e["missing"] for e in levels.values()),
        },
    }


def summarise(payload: dict[str, Any]) -> str:
    lines = ["## XAML behaviour measurements", "",
             "| level | cases | measured | errored | missing |",
             "|---|---:|---:|---:|---:|"]
    for level, entry in payload["levels"].items():
        lines.append(
            f"| L{level} | {entry['cases']} | {entry['measured']} "
            f"| {entry['errored']} | {entry['missing']} |"
        )

    quarantined = [q for q in payload["quarantine"] if int(q["level"]) not in AUTHORED_LEVELS]
    if quarantined:
        lines += ["", f"### Quarantined harvest candidates ({len(quarantined)})", "",
                  "Predicted loadable, refused by the runtime. Each one names a "
                  "blocker the harvester does not yet model.", ""]
        for item in quarantined[:20]:
            error = re.sub(r"\s+", " ", item["error"]).strip()[:160]
            lines.append(f"- `{item['id']}` — {error}")
        if len(quarantined) > 20:
            lines.append(f"- …and {len(quarantined) - 20} more")

    if payload["authored_levels_failing"]:
        levels = ", ".join("L" + n for n in payload["authored_levels_failing"])
        lines += ["", f"**Authored levels failing: {levels}**"]
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cases", required=True, type=Path)
    parser.add_argument("--measurements", required=True, type=Path)
    parser.add_argument("--output", type=Path, help="where to write the JSON report")
    parser.add_argument("--summary", type=Path, help="where to append the markdown summary")
    args = parser.parse_args()

    payload = report(args.cases, args.measurements)

    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(
            json.dumps(payload, indent=1, sort_keys=True) + "\n", encoding="utf-8"
        )
    text = summarise(payload)
    print(text)
    if args.summary:
        with args.summary.open("a", encoding="utf-8") as handle:
            handle.write(text)

    # A harvested case the runtime rejects is expected feedback. A case we wrote
    # ourselves that it rejects is a broken corpus.
    return 1 if payload["authored_levels_failing"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
