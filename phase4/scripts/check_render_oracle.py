#!/usr/bin/env python3
"""Strict acceptance comparison: native Windows.UI.Xaml versus our renderer.

There are no tolerances and no expected-failure list. Every native case must
load, render, expose the same effective visual geometry and produce identical
premultiplied BGRA8 bytes. The CI step is allowed to fail while Wave 6 is under
construction, but this program itself returns failure for every mismatch.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path
from typing import Any


class AcceptanceError(ValueError):
    """The oracle or actual directory is malformed, rather than mismatched."""


def load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise AcceptanceError(f"cannot read {path}: {error}") from error
    if not isinstance(value, dict):
        raise AcceptanceError(f"{path} is not a JSON object")
    return value


def issue(boundary: str, detail: str) -> dict[str, str]:
    return {"boundary": boundary, "detail": detail}


def pixel_difference(expected: bytes, actual: bytes, width: int, height: int) -> dict[str, Any]:
    count = 0
    first: tuple[int, int, bytes, bytes] | None = None
    left, top, right, bottom = width, height, -1, -1
    max_delta = [0, 0, 0, 0]
    for offset in range(0, len(expected), 4):
        wanted = expected[offset : offset + 4]
        got = actual[offset : offset + 4]
        if wanted == got:
            continue
        pixel = offset // 4
        x, y = pixel % width, pixel // width
        if first is None:
            first = (x, y, wanted, got)
        count += 1
        left, top, right, bottom = min(left, x), min(top, y), max(right, x), max(bottom, y)
        for channel in range(4):
            max_delta[channel] = max(max_delta[channel], abs(wanted[channel] - got[channel]))
    if first is None:
        raise AssertionError("pixel_difference called for equal buffers")
    x, y, wanted, got = first
    return {
        "mismatched_pixels": count,
        "mismatch_bounds": [left, top, right + 1, bottom + 1],
        "first_mismatch": {
            "x": x,
            "y": y,
            "expected_bgra": wanted.hex(),
            "actual_bgra": got.hex(),
        },
        "max_channel_delta_bgra": max_delta,
    }


def compare_pixels(
    oracle_dir: Path,
    actual_dir: Path,
    capture: dict[str, Any],
    actual_sidecar: dict[str, Any],
) -> tuple[list[dict[str, str]], dict[str, Any]]:
    problems: list[dict[str, str]] = []
    case_id = capture["case_id"]
    width, height, stride = capture["width"], capture["height"], capture["stride"]
    if stride != width * 4:
        raise AcceptanceError(f"{case_id}: oracle stride is not tightly packed BGRA8")
    if actual_sidecar.get("surface") != [width, height]:
        problems.append(
            issue(
                "pixels.dimensions",
                f"native is {width}x{height}; renderer reports {actual_sidecar.get('surface')}",
            )
        )
    expected_path = oracle_dir / capture["pixels_file"]
    actual_path = actual_dir / f"{case_id}.bgra"
    try:
        expected = expected_path.read_bytes()
    except OSError as error:
        raise AcceptanceError(f"{case_id}: cannot read native pixels: {error}") from error
    expected_size = stride * height
    if len(expected) != expected_size:
        raise AcceptanceError(
            f"{case_id}: native capture has {len(expected)} bytes, expected {expected_size}"
        )
    expected_digest = hashlib.sha256(expected).hexdigest()
    if capture.get("sha256") != expected_digest:
        raise AcceptanceError(f"{case_id}: native pixels do not match their manifest SHA-256")
    if not actual_path.is_file():
        problems.append(issue("pixels.missing", f"renderer did not write {actual_path.name}"))
        return problems, {
            "expected_sha256": expected_digest,
            "actual_sha256": None,
        }
    actual = actual_path.read_bytes()
    metrics: dict[str, Any] = {
        "expected_sha256": expected_digest,
        "actual_sha256": hashlib.sha256(actual).hexdigest(),
    }
    if len(actual) != expected_size:
        problems.append(
            issue(
                "pixels.byte-count",
                f"renderer wrote {len(actual)} BGRA bytes; native has {expected_size}",
            )
        )
        return problems, metrics
    if expected != actual:
        difference = pixel_difference(expected, actual, width, height)
        metrics.update(difference)
        first = difference["first_mismatch"]
        problems.append(
            issue(
                "pixels.content",
                f"{difference['mismatched_pixels']} pixels differ; first at "
                f"({first['x']},{first['y']}) native={first['expected_bgra']} "
                f"renderer={first['actual_bgra']}",
            )
        )
    return problems, metrics


def by_path(nodes: list[dict[str, Any]], owner: str) -> dict[str, dict[str, Any]]:
    result: dict[str, dict[str, Any]] = {}
    for node in nodes:
        path = node.get("path")
        if not isinstance(path, str):
            raise AcceptanceError(f"{owner}: tree node has no string path")
        if path in result:
            raise AcceptanceError(f"{owner}: duplicate tree path {path}")
        result[path] = node
    return result


def compare_visual_tree(
    native: dict[str, Any], actual: dict[str, Any], actual_tree: dict[str, Any] | None
) -> list[dict[str, str]]:
    problems: list[dict[str, str]] = []
    expected = by_path(native.get("tree", []), f"{native.get('case_id')} native")
    observed = by_path(actual.get("geometry", []), f"{native.get('case_id')} renderer")
    measured = by_path(actual_tree.get("tree", []), "renderer layout tree") if actual_tree else {}
    for path in sorted(expected.keys() | observed.keys()):
        wanted, got = expected.get(path), observed.get(path)
        if wanted is None:
            problems.append(issue("visual.extra-node", f"renderer has extra node {path}"))
            continue
        if got is None:
            problems.append(issue("visual.missing-node", f"renderer has no node {path}"))
            continue
        if wanted.get("type") != got.get("type"):
            problems.append(
                issue("visual.type", f"{path}: native {wanted.get('type')}, renderer {got.get('type')}")
            )
        layout = measured.get(path, {})
        comparisons = (
            ("visual.desired", wanted.get("desired"), layout.get("desired")),
            ("visual.actual", wanted.get("actual"), got.get("actual")),
            ("visual.layout-slot", wanted.get("layout_slot"), got.get("slot")),
            ("visual.transform", wanted.get("transform_to_root"), got.get("transform_to_root")),
            ("visual.opacity", wanted.get("opacity"), got.get("opacity")),
            ("visual.visibility", wanted.get("visibility") == 0, got.get("visible")),
            ("visual.z-index", wanted.get("z_index"), got.get("z_index")),
        )
        for boundary, left, right in comparisons:
            if left != right:
                problems.append(issue(boundary, f"{path}: native {left!r}, renderer {right!r}"))
        wanted_clip = wanted.get("clip")
        got_clip = got.get("clip")
        if wanted_clip != got_clip:
            problems.append(
                issue("visual.clip", f"{path}: native {wanted_clip!r}, renderer {got_clip!r}")
            )
    return problems


def flatten_glyph_field(runs: list[dict[str, Any]], field: str) -> list[Any]:
    return [value for run in runs for value in run.get(field, [])]


def compare_text(
    glyph_observations: list[dict[str, Any]], case_id: str, actual: dict[str, Any]
) -> list[dict[str, str]]:
    problems: list[dict[str, str]] = []
    expected = [item for item in glyph_observations if item.get("case_id") == case_id]
    texts = actual.get("texts", [])
    if len(expected) != len(texts):
        problems.append(
            issue(
                "text.run-count",
                f"native exposes {len(expected)} explicit text runs; renderer exposes {len(texts)}",
            )
        )
    for index, observation in enumerate(expected[: len(texts)]):
        native = observation["directwrite"]
        got = texts[index]
        runs = native.get("runs", [])
        expected_advances = flatten_glyph_field(runs, "glyph_advances")
        expected_indices = flatten_glyph_field(runs, "glyph_indices")
        expected_offsets = flatten_glyph_field(runs, "glyph_offsets")
        fields = (
            ("text.content", native.get("text"), got.get("text")),
            ("text.family", native.get("family"), got.get("font_family")),
            ("text.font-size", native.get("font_size"), got.get("font_size")),
            ("text.advances", expected_advances, got.get("advances")),
            ("text.glyph-indices", expected_indices, got.get("glyph_indices")),
            ("text.glyph-offsets", expected_offsets, got.get("glyph_offsets")),
        )
        for boundary, left, right in fields:
            if left != right:
                problems.append(
                    issue(boundary, f"TextBlock {index}: native {left!r}, renderer {right!r}")
                )
        if runs:
            expected_baseline = runs[0].get("baseline", [None, None])[1]
            if expected_baseline != got.get("baseline"):
                problems.append(
                    issue(
                        "text.baseline",
                        f"TextBlock {index}: native {expected_baseline!r}, "
                        f"renderer {got.get('baseline')!r}",
                    )
                )
    return problems


def compare(oracle_dir: Path, actual_dir: Path) -> dict[str, Any]:
    manifest = load_json(oracle_dir / "manifest.json")
    oracle_identity = load_json(oracle_dir / "oracle.json")
    if manifest.get("pixel_format") != "BGRA8" or manifest.get("alpha_mode") != "premultiplied":
        raise AcceptanceError("native manifest is not premultiplied BGRA8")
    captures = manifest.get("captures")
    if not isinstance(captures, list) or not captures:
        raise AcceptanceError("native manifest contains no captures")
    glyph_description = manifest.get("directwrite_glyph_runs")
    if not isinstance(glyph_description, dict):
        raise AcceptanceError("native manifest has no DirectWrite glyph boundary")
    glyph_path = oracle_dir / glyph_description.get("file", "")
    if not glyph_path.is_file():
        raise AcceptanceError("native DirectWrite glyph boundary is missing")
    glyph_data = glyph_path.read_bytes()
    if hashlib.sha256(glyph_data).hexdigest() != glyph_description.get("sha256"):
        raise AcceptanceError("native DirectWrite glyph boundary does not match its manifest")
    glyphs = load_json(glyph_path).get("observations", [])
    results = []
    for capture in captures:
        case_id = capture.get("case_id")
        if not isinstance(case_id, str):
            raise AcceptanceError("native capture has no string case_id")
        native = load_json(oracle_dir / capture.get("observation_file", f"{case_id}.json"))
        if native.get("case_id") != case_id:
            raise AcceptanceError(f"{case_id}: native observation identifies another case")
        actual_path = actual_dir / f"{case_id}.json"
        problems: list[dict[str, str]] = []
        metrics: dict[str, Any] = {}
        if not actual_path.is_file():
            problems.append(issue("case.missing", "renderer produced no sidecar"))
        else:
            actual = load_json(actual_path)
            if actual.get("case_id") != case_id:
                problems.append(
                    issue("case.identity", f"renderer sidecar identifies {actual.get('case_id')!r}")
                )
            if "load_error" in actual:
                problems.append(issue("case.load", actual["load_error"]))
            else:
                for refusal in actual.get("refusals", []):
                    problems.append(
                        issue(
                            "renderer.refusal",
                            f"{refusal.get('path')}: {refusal.get('feature')}: "
                            f"{refusal.get('reason')}",
                        )
                    )
                for failure in actual.get("text_failures", []):
                    problems.append(issue("renderer.text-failure", str(failure)))
                for render_issue in actual.get("render_issues", []):
                    problems.append(
                        issue(
                            "renderer.backend-issue",
                            f"{render_issue.get('code', 'unknown')} at node "
                            f"{render_issue.get('node')}: {render_issue.get('message', '')}",
                        )
                    )
                pixel_problems, metrics = compare_pixels(
                    oracle_dir, actual_dir, capture, actual
                )
                problems.extend(pixel_problems)
                tree_path = actual_dir / "trees" / f"{case_id}.json"
                actual_tree = load_json(tree_path) if tree_path.is_file() else None
                problems.extend(compare_visual_tree(native, actual, actual_tree))
                problems.extend(compare_text(glyphs, case_id, actual))
        results.append(
            {
                "case_id": case_id,
                "status": "passed" if not problems else "failed",
                "problems": problems,
                "pixel_metrics": metrics,
            }
        )

    passed = sum(result["status"] == "passed" for result in results)
    return {
        "schema_version": 1,
        "comparison": "exact native Windows.UI.Xaml versus OpenXaml renderer",
        "oracle": oracle_identity,
        "summary": {"cases": len(results), "passed": passed, "failed": len(results) - passed},
        "cases": results,
    }


def markdown(report: dict[str, Any]) -> str:
    summary = report["summary"]
    lines = [
        "## Native renderer acceptance",
        "",
        f"**{summary['passed']} / {summary['cases']} cases pixel-and-structure exact; "
        f"{summary['failed']} failed.**",
        "",
        "No tolerance or expected-failure list is applied.",
        "",
    ]
    for case in report["cases"]:
        marker = "PASS" if case["status"] == "passed" else "FAIL"
        lines.append(f"- `{marker}` `{case['case_id']}`")
        for problem in case["problems"][:3]:
            lines.append(f"  - `{problem['boundary']}`: {problem['detail']}")
        if len(case["problems"]) > 3:
            lines.append(f"  - … {len(case['problems']) - 3} more in the JSON report")
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--oracle", type=Path, required=True)
    parser.add_argument("--actual", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--summary", type=Path)
    args = parser.parse_args()
    try:
        report = compare(args.oracle, args.actual)
    except AcceptanceError as error:
        print(f"acceptance infrastructure error: {error}", file=sys.stderr)
        return 2
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    text = markdown(report)
    print(text, end="")
    if args.summary:
        with args.summary.open("a", encoding="utf-8") as stream:
            stream.write(text)
    return 0 if report["summary"]["failed"] == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
