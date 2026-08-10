#!/usr/bin/env python3
"""Harvest public DirectWrite glyph runs for explicit text in render cases."""

from __future__ import annotations

import argparse
import json
import subprocess
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path


class GlyphHarvestError(ValueError):
    pass


def local_name(name: str) -> str:
    return name.rsplit("}", 1)[-1]


@dataclass(frozen=True)
class TextRun:
    case_id: str
    element_index: int
    family: str
    size: float
    locale: str
    text: str


def text_runs(cases: Path) -> list[TextRun]:
    result: list[TextRun] = []
    for path in sorted(cases.glob("*.json")):
        document = json.loads(path.read_text(encoding="utf-8"))
        case_id = document.get("id")
        markup = document.get("markup")
        if not isinstance(case_id, str) or not isinstance(markup, str):
            raise GlyphHarvestError(f"{path}: render case needs string id and markup")
        try:
            root = ET.fromstring(markup)
        except ET.ParseError as error:
            raise GlyphHarvestError(f"{path}: invalid XAML: {error}") from error
        locale = str(document.get("environment", {}).get("language", "en-US"))
        text_index = 0
        for element in root.iter():
            if local_name(element.tag) != "TextBlock":
                continue
            attributes = {local_name(key): value for key, value in element.attrib.items()}
            text = attributes.get("Text")
            family = attributes.get("FontFamily")
            size = attributes.get("FontSize")
            if not text or not family or not size:
                raise GlyphHarvestError(
                    f"{case_id} TextBlock {text_index}: Text, FontFamily and FontSize must be explicit"
                )
            unsupported = {
                key: attributes[key]
                for key in ("FontWeight", "FontStyle", "FontStretch", "CharacterSpacing")
                if key in attributes and attributes[key] not in {"Normal", "0"}
            }
            if unsupported:
                raise GlyphHarvestError(
                    f"{case_id} TextBlock {text_index}: probe needs support for {unsupported}"
                )
            try:
                numeric_size = float(size)
            except ValueError as error:
                raise GlyphHarvestError(
                    f"{case_id} TextBlock {text_index}: invalid FontSize {size!r}"
                ) from error
            result.append(TextRun(case_id, text_index, family, numeric_size, locale, text))
            text_index += 1
    return result


def harvest(cases: Path, probe: Path, output: Path) -> dict:
    observations = []
    for run in text_runs(cases):
        completed = subprocess.run(
            [str(probe), run.family, format(run.size, ".17g"), run.locale, run.text],
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
        )
        if completed.returncode:
            raise GlyphHarvestError(
                f"{run.case_id} TextBlock {run.element_index}: DirectWrite probe failed: "
                f"{completed.stderr.strip()}"
            )
        try:
            native = json.loads(completed.stdout)
        except json.JSONDecodeError as error:
            raise GlyphHarvestError(
                f"{run.case_id} TextBlock {run.element_index}: invalid probe JSON: {error}"
            ) from error
        if native.get("family") != run.family or native.get("text") != run.text:
            raise GlyphHarvestError(
                f"{run.case_id} TextBlock {run.element_index}: probe echoed different input"
            )
        observations.append(
            {
                "case_id": run.case_id,
                "element_index": run.element_index,
                "directwrite": native,
            }
        )

    if not observations:
        raise GlyphHarvestError("render corpus contains no explicit TextBlock runs")
    document = {
        "schema_version": 1,
        "boundary": "IDWriteTextLayout::Draw -> IDWriteTextRenderer::DrawGlyphRun",
        "observations": observations,
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(document, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return document


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cases", type=Path, required=True)
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        document = harvest(args.cases, args.probe, args.output)
    except (GlyphHarvestError, OSError) as error:
        raise SystemExit(str(error)) from error
    print(f"harvested {len(document['observations'])} DirectWrite glyph run(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
