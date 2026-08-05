#!/usr/bin/env python3
"""Report what the real runtime made of the corpus, per level.

Three kinds of case are measured, and a failure means something different for
each.

Levels 0 to 5 are written by us against the documented contract. If the runtime
refuses one, the case is wrong and the corpus is broken, so that is an error.

Except where the case says otherwise. A case may carry `oracle_decides`, which
means we did not know what the runtime would do and wrote the case to find out
-- the resource cases at L5 include several, because WinUI 2's parser is not
documented to the depth those questions need and WPF's behaviour is not
evidence about it. A rejection there is the answer, so it is named rather than
fatal. The field is not a way to make a failing case quiet: it has to be
written into the case, next to the question, before the run.

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
AUTHORED_LEVELS = range(0, 6)


def level_of(case: dict[str, Any]) -> int:
    return int(case.get("level", -1))


def outcome_kind(case: dict[str, Any]) -> str:
    """What a rejection of this case would mean.

    `authored` is fatal, the other two are findings.
    """
    if case.get("oracle_decides"):
        return "question"
    return "authored" if level_of(case) in AUTHORED_LEVELS else "harvest"


def load_cases(cases: Path) -> dict[str, dict[str, Any]]:
    found = {}
    for path in sorted(cases.rglob("*.json")):
        try:
            case = json.loads(path.read_text(encoding="utf-8"))
            case_id = case["id"]
        except (json.JSONDecodeError, KeyError, TypeError) as failure:
            # The corpus is generator output, so this is a bug in a generator
            # rather than a finding about the runtime. Still fatal -- but named,
            # because a bare KeyError says nothing about which file.
            raise SystemExit(f"{path}: not a case file ({failure})") from failure
        found[case_id] = case
    return found


def load_measurements(measurements: Path) -> dict[str, dict[str, Any]]:
    found = {}
    for path in sorted(measurements.glob("*.json")):
        if path.name in ("oracle.json", "report.json"):
            continue
        try:
            payload = json.loads(path.read_text(encoding="utf-8"))
        except (json.JSONDecodeError, UnicodeDecodeError) as failure:
            # A measurement the probe started and did not finish -- it writes
            # one file per case, so a process that dies mid-run leaves exactly
            # one of these. Reading it as a rejection keeps every other answer
            # in the report; raising here would throw away the whole run over
            # one truncated file, which is the opposite of what a report is for.
            payload = {
                "case_id": path.stem,
                "error": f"the measurement file could not be read: {failure}",
            }
        found[payload.get("case_id", path.stem)] = payload
    return found


def report(cases: Path, measurements: Path) -> dict[str, Any]:
    by_id = load_cases(cases)
    measured = load_measurements(measurements)

    levels: dict[int, dict[str, Any]] = defaultdict(
        lambda: {"cases": 0, "measured": 0, "errored": 0, "missing": 0}
    )
    quarantine: list[dict[str, str]] = []
    missing_ids: list[str] = []
    answered: list[dict[str, Any]] = []

    for case_id, case in sorted(by_id.items()):
        level = level_of(case)
        entry = levels[level]
        entry["cases"] += 1
        result = measured.get(case_id)
        if result is None:
            entry["missing"] += 1
            missing_ids.append(case_id)
            continue
        if "error" in result:
            entry["errored"] += 1
            quarantine.append({
                "id": case_id,
                "level": str(level),
                "kind": outcome_kind(case),
                "error": result["error"],
                "markup": case.get("markup", ""),
                "question": case.get("question", ""),
            })
        else:
            entry["measured"] += 1
            if case.get("oracle_decides"):
                # A declared question the runtime answered with a number rather
                # than a refusal. Without this it would be indistinguishable
                # from any other measured case, and the answer we asked for
                # would be sitting in an artifact nobody opened. The root node
                # is enough to read the answer off; the artifact has the rest.
                nodes = result.get("tree", [])
                root_node = nodes[0] if nodes else {}
                answered.append({
                    "id": case_id,
                    "level": str(level),
                    "question": case.get("question", ""),
                    "nodes": len(nodes),
                    "desired": root_node.get("desired", []),
                    "actual": root_node.get("actual", []),
                    "offset": root_node.get("offset", []),
                })

    # Per case rather than per level, because one level now holds both kinds:
    # a broken L5 case is fatal and an answered L5 question is not.
    broken_levels = {int(item["level"]) for item in quarantine if item["kind"] == "authored"}
    missing_levels = {
        level for level in levels
        if level in AUTHORED_LEVELS and levels[level]["missing"]
    }
    failures = sorted(broken_levels | missing_levels)

    return {
        "schema_version": 1,
        "levels": {str(level): dict(levels[level]) for level in sorted(levels)},
        "quarantine": quarantine,
        "answered": answered,
        "missing_ids": missing_ids,
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

    refused = [q for q in payload["quarantine"] if q.get("kind") == "question"]
    if refused:
        lines += ["", f"### Open questions the runtime refused ({len(refused)})", "",
                  "Cases written because we did not know the behaviour. A "
                  "rejection here is the finding, not a broken corpus.", ""]
        for item in refused:
            error = re.sub(r"\s+", " ", item["error"]).strip()[:200]
            lines.append(f"- `{item['id']}` — refused: {error}")

    # The other half of the same story, and the half that used to go unsaid: a
    # declared question the runtime answered by measuring. Reporting only the
    # refusals meant an answer arrived as an ordinary row in the level table and
    # had to be dug out of the artifact to be read at all.
    answered = payload.get("answered", [])
    if answered:
        lines += ["", f"### Open questions the runtime measured ({len(answered)})", "",
                  "Cases written because we did not know the behaviour, which "
                  "the runtime answered with a number. The root node is shown; "
                  "the full tree is in the measurement artifact.", ""]
        for item in answered:
            question = re.sub(r"\s+", " ", item.get("question", "")).strip()[:160]
            lines.append(
                f"- `{item['id']}` — desired {item['desired']}, "
                f"actual {item['actual']}, offset {item['offset']} "
                f"({item['nodes']} node(s)) — {question}"
            )

    quarantined = [q for q in payload["quarantine"] if q.get("kind") == "harvest"]
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
        lines += ["", f"**Authored levels failing: {levels}**", ""]
        # These are the fatal ones, so they are the ones worth naming. Listing
        # only the quarantined harvest left the failure that actually stops the
        # run identified by a count alone.
        broken = [q for q in payload["quarantine"] if q.get("kind") == "authored"]
        for item in broken:
            error = re.sub(r"\s+", " ", item["error"]).strip()[:300]
            lines.append(f"- `{item['id']}` — {error or '(no message)'}")
        missing = payload.get("missing_ids", [])
        for case_id in missing:
            lines.append(f"- `{case_id}` — no measurement was written")
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
