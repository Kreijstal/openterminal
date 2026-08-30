#!/usr/bin/env python3
"""Compare public TabView behavior from official WinUI and OpenXaml."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any


DIAGNOSTIC_KEYS = {"hosting.foreground", "hosting.island_class"}


def is_diagnostic(key: str) -> bool:
    return (key.startswith("diagnostic.") or key.startswith("pixels.") or
            key in DIAGNOSTIC_KEYS)


def read_document(path: Path) -> dict[str, Any]:
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"cannot read {path}: {error}") from error
    if document.get("schema_version") != 1:
        raise ValueError(f"{path}: unsupported schema_version")
    observations = document.get("observations")
    if not isinstance(observations, dict):
        raise ValueError(f"{path}: observations is not an object")
    return document


def pixel_report(official: Path | None, actual: Path | None) -> dict[str, Any] | None:
    if official is None and actual is None:
        return None
    if official is None or actual is None:
        raise ValueError("both --official-pixels and --actual-pixels are required")
    left = official.read_bytes()
    right = actual.read_bytes()
    report: dict[str, Any] = {
        "official_bytes": len(left),
        "actual_bytes": len(right),
        "official_sha256": hashlib.sha256(left).hexdigest(),
        "actual_sha256": hashlib.sha256(right).hexdigest(),
        "identical": left == right,
    }
    if len(left) == len(right):
        different = [offset for offset in range(0, len(left), 4)
                     if left[offset:offset + 4] != right[offset:offset + 4]]
        report["mismatched_pixels"] = len(different)
        if different:
            first = different[0] // 4
            report["first_mismatch"] = {
                "x": first % 640,
                "y": first // 640,
                "official_bgra": left[different[0]:different[0] + 4].hex(),
                "actual_bgra": right[different[0]:different[0] + 4].hex(),
            }
    return report


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--official", type=Path, required=True)
    parser.add_argument("--actual", type=Path, required=True)
    parser.add_argument("--official-pixels", type=Path)
    parser.add_argument("--actual-pixels", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument(
        "--strict-diagnostics", action="store_true",
        help="also fail on host identity and oracle setup diagnostics")
    args = parser.parse_args()

    official = read_document(args.official)
    actual = read_document(args.actual)
    left = official["observations"]
    right = actual["observations"]
    mismatches: list[dict[str, Any]] = []
    diagnostics: list[dict[str, Any]] = []
    for key in sorted(left.keys() | right.keys()):
        destination = (diagnostics if is_diagnostic(key) and
                       not args.strict_diagnostics else mismatches)
        if key not in left:
            destination.append({"key": key, "problem": "only in OpenXaml",
                                "actual": right[key]})
        elif key not in right:
            destination.append({"key": key, "problem": "only in official WinUI",
                                "official": left[key]})
        elif left[key] != right[key]:
            destination.append({"key": key, "problem": "values differ",
                                "official": left[key], "actual": right[key]})

    pixels = pixel_report(args.official_pixels, args.actual_pixels)
    comparable_keys = {key for key in left.keys() | right.keys()
                       if args.strict_diagnostics or not is_diagnostic(key)}
    report = {
        "schema_version": 1,
        "official_runtime": official.get("runtime"),
        "actual_runtime": actual.get("runtime"),
        "matching_observations": sum(
            1 for key in comparable_keys
            if key in left and key in right and left[key] == right[key]),
        "mismatches": mismatches,
        "diagnostic_differences": diagnostics,
        "pixels": pixels,
    }
    rendered = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.write_text(rendered, encoding="utf-8")
    print(rendered, end="")
    pixel_mismatch = pixels is not None and not pixels["identical"]
    return 1 if mismatches or pixel_mismatch else 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        print(f"check_muxc_tabview: {error}")
        raise SystemExit(2)
