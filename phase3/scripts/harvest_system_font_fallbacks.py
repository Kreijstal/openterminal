#!/usr/bin/env python3
"""Harvest DirectWrite system fallback for FontIcon glyphs in a case corpus.

The ordinary font harvest records explicitly named families. This pass finds
FontIcon glyphs none of those families cover, asks a Windows-only DirectWrite
probe which local face the system maps, and attaches that face's sfnt metrics
to the first named family's reviewable JSON. Desired sizes and oracle output
are deliberately not inputs.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import xml.etree.ElementTree as ET
from pathlib import Path

from harvest_font_metrics import FontError, attach_system_fallbacks


def local_name(name: str) -> str:
    return name.rsplit("}", 1)[-1]


def literal(value: str | None) -> str | None:
    if value is None or value.startswith("{"):
        return None
    return value


def requested_fallbacks(cases: Path, fonts: dict[str, dict]) -> set[tuple[str, int, str]]:
    """Return (source family, codepoint, locale) tuples not covered by named fonts."""
    requests: set[tuple[str, int, str]] = set()
    for path in sorted(cases.rglob("*.json")):
        document = json.loads(path.read_text(encoding="utf-8"))
        markup = document.get("markup")
        if not isinstance(markup, str):
            continue
        try:
            root = ET.fromstring(markup)
        except ET.ParseError as error:
            raise FontError(f"{path}: cannot inspect FontIcon fallback: {error}") from error
        locale = str(document.get("environment", {}).get("language", "en-US"))
        for element in root.iter():
            if local_name(element.tag) != "FontIcon":
                continue
            attributes = {local_name(key): value for key, value in element.attrib.items()}
            family_value = literal(attributes.get("FontFamily")) or "Segoe MDL2 Assets"
            glyph = literal(attributes.get("Glyph"))
            if not glyph:
                continue
            families = [part.strip() for part in family_value.split(",") if part.strip()]
            available = [family for family in families if family in fonts]
            if not available:
                continue
            for codepoint in map(ord, glyph):
                if any(str(codepoint) in fonts[family].get("advances", {})
                       for family in available):
                    continue
                requests.add((available[0], codepoint, locale))
    return requests


def harvest(cases: Path, fonts_dir: Path, probe: Path) -> int:
    paths: dict[str, Path] = {}
    fonts: dict[str, dict] = {}
    for path in sorted(fonts_dir.glob("*.json")):
        document = json.loads(path.read_text(encoding="utf-8"))
        family = document.get("family")
        if not isinstance(family, str):
            continue
        if family in fonts:
            raise FontError(f"{path}: duplicate metrics for {family!r}")
        paths[family] = path
        fonts[family] = document

    grouped: dict[str, dict] = {}
    for family, codepoint, locale in sorted(requested_fallbacks(cases, fonts)):
        completed = subprocess.run(
            [str(probe), family, f"{codepoint:X}", locale],
            check=False, capture_output=True, text=True, encoding="utf-8")
        if completed.returncode:
            raise FontError(
                f"DirectWrite fallback probe failed for {family!r} U+{codepoint:04X}: "
                f"{completed.stderr.strip()}")
        description = json.loads(completed.stdout)
        mapping = description.get("mappings", {}).get(str(codepoint))
        if not isinstance(mapping, dict):
            raise FontError(
                f"DirectWrite fallback probe returned no U+{codepoint:04X} mapping")
        destination = grouped.setdefault(family, {
            "schema_version": 1,
            "source_family": family,
            "mappings": {},
        })
        mapping = dict(mapping)
        mapping["locale"] = locale
        previous = destination["mappings"].get(str(codepoint))
        if previous is not None and previous != mapping:
            raise FontError(
                f"DirectWrite mapped {family!r} U+{codepoint:04X} differently by locale")
        destination["mappings"][str(codepoint)] = mapping

    count = 0
    for family, description in sorted(grouped.items()):
        attach_system_fallbacks(fonts[family], description)
        path = paths[family]
        path.write_text(json.dumps(fonts[family], indent=1, sort_keys=True) + "\n",
                        encoding="utf-8")
        count += len(description["mappings"])
    return count


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cases", type=Path, required=True)
    parser.add_argument("--fonts", type=Path, required=True)
    parser.add_argument("--probe", type=Path, required=True)
    args = parser.parse_args()
    if not args.cases.is_dir():
        raise SystemExit(f"no case directory: {args.cases}")
    if not args.fonts.is_dir():
        raise SystemExit(f"no font metrics directory: {args.fonts}")
    if not args.probe.is_file():
        raise SystemExit(f"no DirectWrite fallback probe: {args.probe}")
    try:
        count = harvest(args.cases, args.fonts, args.probe)
    except (FontError, OSError, json.JSONDecodeError) as error:
        raise SystemExit(str(error)) from error
    print(f"harvested {count} DirectWrite system-font fallback mapping(s)")


if __name__ == "__main__":
    main()
