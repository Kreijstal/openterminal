#!/usr/bin/env python3
"""Extract the icon-font codepoints Terminal's markup actually asks for.

A `FontIcon` is a glyph in an icon font, so its size is a text measurement in
`Segoe MDL2 Assets` or `Segoe Fluent Icons` — and neither font's metrics are in
the corpus, which is why fifteen level 7 cases are blocked on it and the layout
core refuses `FontIcon` rather than invent an advance.

The font harvest can read those metrics, but it has to be told *which*
codepoints. An icon font covers thousands and the corpus needs a few dozen, so
emitting the whole coverage would bury the answer in noise and make the file
unreviewable. Emitting a hand-typed list would make it wrong the first time
Terminal changed an icon.

So the set is extracted, from the same checkout the level 7 cases are harvested
from, by the same scoping rules: WinUI markup only, markup extensions excluded,
everything sorted. The output is an input to `harvest_font_metrics.py` and a
statement of provenance for the pinned lists in `generate_cases.py`.

    python3 phase3/scripts/harvest_icon_glyphs.py <terminal-checkout> \\
        --output phase3/xaml-db/fonts/icon-glyphs.json
"""

from __future__ import annotations

import argparse
import json
import sys
import xml.etree.ElementTree as ET
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parent))

from harvest_terminal_xaml import (  # noqa: E402
    PRESENTATION,
    canonical_remote,
    git,
    markup_extension_name,
    out_of_scope,
    split_tag,
)

SCHEMA_VERSION = 1

# The attribute that names a glyph by codepoint. `SymbolIcon` names one by enum
# member instead (`Symbol="Cancel"`), which is a mapping the framework owns
# rather than one the markup states; Terminal's markup uses none, so resolving
# that mapping is not needed to cover it and guessing at it would put an
# invented codepoint into a harvest.
GLYPH_ATTRIBUTE = "Glyph"


def scan(repo: Path) -> dict[str, Any]:
    """Every literal `Glyph` in the checkout's WinUI markup, with its context."""
    per_codepoint: defaultdict[int, Counter[str]] = defaultdict(Counter)
    families: Counter[str] = Counter()
    font_sizes: Counter[str] = Counter()
    element_types: Counter[str] = Counter()
    deferred: Counter[str] = Counter()
    skipped: list[dict[str, str]] = []
    parse_failures: list[dict[str, str]] = []
    files_scanned = 0

    for path in sorted(p for p in repo.rglob("*.xaml") if ".git" not in p.parts):
        source = path.relative_to(repo).as_posix()
        reason = out_of_scope(path, repo)
        if reason:
            skipped.append({"file": source, "reason": reason})
            continue
        try:
            tree = ET.parse(path)
        except ET.ParseError as error:
            parse_failures.append({"file": source, "error": str(error)})
            continue
        files_scanned += 1

        for element in tree.iter():
            if not isinstance(element.tag, str):
                continue
            uri, local = split_tag(element.tag)
            if uri != PRESENTATION:
                continue
            attributes = {
                name: value for name, value in
                ((split_tag(key)[1], value) for key, value in element.attrib.items())
            }
            if GLYPH_ATTRIBUTE not in attributes:
                continue
            glyph = attributes[GLYPH_ATTRIBUTE]

            extension = markup_extension_name(glyph)
            if extension:
                # `Glyph="{TemplateBinding FontIconGlyph}"` names a codepoint we
                # cannot know without resolving the binding. Counted, so the
                # output says how much of the markup this set does not cover.
                deferred[extension] += 1
                continue
            if glyph.startswith("{}"):
                # XAML's escape for a leading brace, and only meaningful once
                # the value is known not to be an extension -- stripping first
                # would turn the literal "{}{StaticResource X}" into a lookup.
                # Taking the raw string instead would put U+007B into a harvest
                # of an icon font that has no such glyph.
                glyph = glyph[2:]

            element_types[local] += 1
            families[attributes.get("FontFamily", "")] += 1
            font_sizes[attributes.get("FontSize", "")] += 1
            for character in glyph:
                per_codepoint[ord(character)][source] += 1

    return {
        "codepoints": sorted(per_codepoint),
        "glyphs": {
            f"U+{codepoint:04X}": {
                "codepoint": codepoint,
                "count": sum(files.values()),
                "files": sorted(files),
            }
            for codepoint, files in sorted(per_codepoint.items())
        },
        # Context, so the generated FontIcon cases can be checked against what
        # the markup really says rather than against a memory of it.
        "font_families": dict(sorted(families.items())),
        "font_sizes": dict(sorted(font_sizes.items())),
        "element_types": dict(sorted(element_types.items())),
        "deferred_extensions": dict(sorted(deferred.items())),
        "totals": {
            "files_scanned": files_scanned,
            "files_out_of_scope": len(skipped),
            "glyph_attributes": sum(element_types.values()),
            "codepoints": len(per_codepoint),
            "deferred": sum(deferred.values()),
        },
        "skipped_files": skipped,
        "parse_failures": parse_failures,
    }


def harvest(repo: Path) -> dict[str, Any]:
    payload = {
        "schema_version": SCHEMA_VERSION,
        "source": {
            "repository": canonical_remote(git(repo, "remote", "get-url", "origin")),
            "commit": git(repo, "rev-parse", "HEAD"),
            "commit_date": git(repo, "show", "-s", "--format=%cI", "HEAD"),
        },
    }
    payload.update(scan(repo))
    return payload


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("repository", type=Path, help="Windows Terminal checkout")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)

    repo = args.repository.resolve()
    if not (repo / ".git").exists():
        parser.error(f"not a Git checkout: {repo}")

    payload = harvest(repo)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    # Sorted keys and a trailing newline, like every other harvest here, so two
    # runs are byte-identical and a change is reviewable as a diff.
    args.output.write_text(json.dumps(payload, indent=1, sort_keys=True) + "\n",
                           encoding="utf-8")

    totals = payload["totals"]
    print(f"  files scanned    {totals['files_scanned']:>5}"
          f" ({totals['files_out_of_scope']} out of scope)")
    print(f"  Glyph attributes {totals['glyph_attributes']:>5}"
          f" ({totals['deferred']} behind a markup extension)")
    print(f"  codepoints       {totals['codepoints']:>5}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
