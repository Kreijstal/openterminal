"""Fingerprint a set of measurements.

The measurements themselves are a CI artifact and are not committed -- they are
regenerated deterministically in about two minutes. What is committed is this
digest, a few hundred bytes per oracle.

The point is drift. The expectations come from the real Windows.UI.Xaml, which
is serviced underneath us. Without a stored fingerprint a servicing update
silently moves every expectation: the next fetch re-baselines against the new
answers and a genuine regression in our own implementation is masked by the
shift. With one, the change shows up as a reviewable diff that says the runtime
started answering differently, which is what the os_build and font hash were
recorded to catch in the first place.

The digest covers what the runtime said, not how the probe spelled it, so
reformatting the probe's output does not invalidate a stored oracle.
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Any, Iterable

SCHEMA_VERSION = 1

# Files in a measurement directory that are not measurements.
NOT_A_MEASUREMENT = {"oracle.json", "report.json"}


def level_of(case_id: str) -> str:
    """L3-grid-0001 -> L3. The level is the part the report splits on."""
    return case_id.split("-", 1)[0]


def _number(value: Any) -> str:
    # Infinity and NaN are carried as strings because JSON cannot spell them.
    if isinstance(value, str):
        return value
    return f"{value:.4f}"


def canonical_lines(measurement: dict[str, Any]) -> Iterable[str]:
    """The semantic content of one measurement, as stable text."""
    case_id = measurement["case_id"]
    if "error" in measurement:
        # Included verbatim. A reworded rejection is still the runtime
        # answering differently, and for a drift detector that is worth
        # surfacing rather than smoothing over.
        yield f"{case_id}\terror\t{measurement['error']}"
        return
    for node in measurement.get("tree", []):
        fields = "\t".join(
            _number(v)
            for key in ("desired", "actual", "offset")
            for v in node[key]
        )
        yield f"{case_id}\t{node['path']}\t{node['type']}\t{fields}"


def load_measurements(directory: Path) -> dict[str, dict[str, Any]]:
    out: dict[str, dict[str, Any]] = {}
    for path in sorted(directory.glob("*.json")):
        if path.name in NOT_A_MEASUREMENT:
            continue
        data = json.loads(path.read_text(encoding="utf-8"))
        out[data["case_id"]] = data
    return out


def digest(measurements: dict[str, dict[str, Any]]) -> dict[str, Any]:
    """Overall and per-level digests.

    Per-level as well as overall so a drifted oracle says *where* it drifted;
    a single hash only ever says "something moved".
    """
    per_level: dict[str, hashlib._Hash] = {}
    overall = hashlib.sha256()
    counts: dict[str, int] = {}

    for case_id in sorted(measurements):
        level = level_of(case_id)
        counts[level] = counts.get(level, 0) + 1
        per_level.setdefault(level, hashlib.sha256())
        for line in canonical_lines(measurements[case_id]):
            blob = (line + "\n").encode("utf-8")
            overall.update(blob)
            per_level[level].update(blob)

    return {
        "cases": len(measurements),
        "sha256": overall.hexdigest(),
        "levels": {
            level: {"cases": counts[level], "sha256": per_level[level].hexdigest()}
            for level in sorted(per_level)
        },
    }


def oracle_record(measurements_dir: Path) -> dict[str, Any]:
    """The committed fingerprint: who answered, and what they said."""
    oracle = json.loads((measurements_dir / "oracle.json").read_text(encoding="utf-8"))
    body = digest(load_measurements(measurements_dir))
    return {
        "schema_version": SCHEMA_VERSION,
        "os_build": oracle["os_build"],
        "font_segoeui": oracle["font_segoeui"],
        "xaml": oracle.get("xaml", ""),
        "digest": body,
    }


def _main(argv: list[str] | None = None) -> int:
    """Check a freshly measured directory against the committed digest."""
    import argparse

    parser = argparse.ArgumentParser(description=_main.__doc__)
    parser.add_argument("--measurements", type=Path, required=True)
    parser.add_argument("--oracles", type=Path, required=True)
    parser.add_argument("--write", action="store_true",
                        help="record the digest for an oracle that has none yet")
    args = parser.parse_args(argv)

    fresh = oracle_record(args.measurements)
    args.oracles.mkdir(parents=True, exist_ok=True)
    stored_path = args.oracles / f"{fresh['os_build']}.json"

    if not stored_path.exists():
        # Runner images move; a new build is normal. Not a failure, but said
        # out loud, because nothing is checking this oracle until it lands.
        print(f"::warning::no committed digest for os_build {fresh['os_build']}; "
              f"nothing is verifying these measurements yet")
        if args.write:
            stored_path.write_text(json.dumps(fresh, indent=1, sort_keys=True) + "\n",
                                   encoding="utf-8")
            print(f"wrote {stored_path}")
        return 0

    problems = compare(json.loads(stored_path.read_text(encoding="utf-8")), fresh)
    if problems:
        print(f"::error::the runtime answers differently than {stored_path.name} records")
        for problem in problems:
            print(f"  {problem}")
        return 1

    print(f"{fresh['digest']['cases']} measurements match {stored_path.name}")
    return 0


def compare(stored: dict[str, Any], fresh: dict[str, Any]) -> list[str]:
    """Human-readable differences, most specific first. Empty means identical."""
    problems: list[str] = []
    for field in ("os_build", "font_segoeui"):
        if stored.get(field) != fresh.get(field):
            problems.append(f"{field}: stored {stored.get(field)!r}, got {fresh.get(field)!r}")

    stored_levels = stored.get("digest", {}).get("levels", {})
    fresh_levels = fresh.get("digest", {}).get("levels", {})
    for level in sorted(set(stored_levels) | set(fresh_levels)):
        was, now = stored_levels.get(level), fresh_levels.get(level)
        if was is None:
            problems.append(f"{level}: new level, {now['cases']} cases")
        elif now is None:
            problems.append(f"{level}: level disappeared, was {was['cases']} cases")
        elif was != now:
            if was["cases"] != now["cases"]:
                problems.append(
                    f"{level}: {was['cases']} cases -> {now['cases']} cases"
                )
            else:
                problems.append(
                    f"{level}: same {now['cases']} cases, different answers "
                    f"({was['sha256'][:12]} -> {now['sha256'][:12]})"
                )
    return problems


if __name__ == "__main__":
    raise SystemExit(_main())
