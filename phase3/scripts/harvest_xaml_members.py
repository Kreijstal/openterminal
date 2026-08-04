#!/usr/bin/env python3
"""Harvest the XAML-relevant name sets from a Windows SDK contract winmd.

The case extractor has to answer three questions about a piece of markup, and
none of them can be answered from the markup alone:

  * is ``<Foo>`` a framework type, or one of Terminal's own controls?
  * is ``Click="..."`` a layout property, or an event handler that only a
    code-behind can satisfy?
  * can this element be the root of a standalone load, or is it a
    ``ControlTemplate`` that needs a target?

All three are answered by the type metadata that ships in the Windows SDK
contract winmd, so the answers are harvested once into reviewable JSON rather
than guessed at from a hand-written list that would silently rot.

No package payload is committed: the input is a winmd path, the output is text.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path
from typing import Any, Iterable

# The winmd reader already exists in phase 1; reimplementing it here would mean
# two decoders of the same format drifting apart.
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "phase1" / "scripts"))

from harvest_winui_xaml import winmd_metadata  # noqa: E402

SCHEMA_VERSION = 1

# The root of the visual tree. An element that does not derive from it cannot be
# measured or arranged, so it cannot be the root of a measurement case.
UI_ELEMENT = "Windows.UI.Xaml.UIElement"

# Markup names types from outside Windows.UI.Xaml: Color lives in Windows.UI and
# FontWeight in Windows.UI.Text, and both appear as resource values in Terminal's
# dictionaries, so a narrower scope would report them as unknown types.
TYPE_NAMESPACES = ("Windows.UI",)

# Properties and events are only consulted for XAML elements, so widening this
# the same way would import unrelated names -- Windows.UI.Composition events,
# say -- and weaken the event check rather than strengthen it.
MEMBER_NAMESPACES = ("Windows.UI.Xaml",)

# Markup-nameable kinds. Interfaces and delegates never appear as element names.
MARKUP_KINDS = ("class", "struct", "enum")


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def derived_from(types: dict[str, dict[str, Any]], base: str) -> set[str]:
    """Full names of every class reaching ``base`` through its extends chain."""
    result: set[str] = set()
    for full_name in types:
        seen: set[str] = set()
        current: str | None = full_name
        while current and current not in seen:
            seen.add(current)
            if current == base:
                result.add(full_name)
                break
            entry = types.get(current)
            current = entry.get("extends") if entry else None
    return result


def in_namespace(full_name: str, prefixes: Iterable[str]) -> bool:
    return any(
        full_name == prefix or full_name.startswith(prefix + ".")
        for prefix in prefixes
    )


def harvest(winmd: Path) -> dict[str, Any]:
    metadata = winmd_metadata(winmd)
    in_scope = [
        t for t in metadata["types"] if in_namespace(t["full_name"], TYPE_NAMESPACES)
    ]

    by_full_name = {t["full_name"]: t for t in in_scope}
    markup_types = {
        t["full_name"]: t for t in in_scope if t["kind"] in MARKUP_KINDS
    }
    ui_elements = derived_from(by_full_name, UI_ELEMENT) & set(markup_types)

    event_names: set[str] = set()
    property_names: set[str] = set()
    for entry in in_scope:
        if not in_namespace(entry["full_name"], MEMBER_NAMESPACES):
            continue
        event_names.update(e["name"] for e in entry["events"])
        property_names.update(p["name"] for p in entry["properties"])

    # A handful of names are both an event on one type and a property on
    # another. Treating those as events would reject markup that only sets a
    # property, so they are excluded from the blocker set and reported instead.
    ambiguous = sorted(event_names & property_names)

    # An attached property is carried in metadata as the static
    # DependencyProperty "<Name>Property" and a Get/Set pair, never as a plain
    # property called "<Name>". Markup spells it Grid.Column or
    # AutomationProperties.AccessibilityView, so without recovering the stem
    # every attached property in the corpus would look unknown.
    suffix = "Property"
    attached_names = {
        name[: -len(suffix)]
        for name in property_names
        if name.endswith(suffix) and len(name) > len(suffix)
    }

    local_names: dict[str, list[str]] = {}
    for full_name in markup_types:
        local_names.setdefault(full_name.rsplit(".", 1)[-1], []).append(full_name)

    return {
        "schema_version": SCHEMA_VERSION,
        "source": {
            "winmd": winmd.name,
            "winmd_sha256": sha256(winmd),
            "winmd_size": winmd.stat().st_size,
            "metadata_version": metadata["metadata_version"],
            "type_namespaces": list(TYPE_NAMESPACES),
            "member_namespaces": list(MEMBER_NAMESPACES),
        },
        "counts": {
            "types_in_scope": len(in_scope),
            "markup_types": len(markup_types),
            "ui_elements": len(ui_elements),
            "event_names": len(event_names),
            "property_names": len(property_names),
            "attached_property_names": len(attached_names),
        },
        # local name -> full names. A local name with more than one entry is
        # ambiguous in markup unless the namespace is given explicitly.
        "class_local_names": {
            name: sorted(full) for name, full in sorted(local_names.items())
        },
        "ui_element_local_names": sorted(
            {full.rsplit(".", 1)[-1] for full in ui_elements}
        ),
        "event_only_names": sorted(event_names - property_names),
        "ambiguous_event_names": ambiguous,
        "property_names": sorted(property_names),
        "attached_property_names": sorted(attached_names),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("winmd", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    if not args.winmd.is_file():
        parser.error(f"no such winmd: {args.winmd}")

    payload = harvest(args.winmd)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(payload, indent=1, sort_keys=True) + "\n", encoding="utf-8"
    )
    for name, value in sorted(payload["counts"].items()):
        print(f"  {name:<18} {value:>6}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
