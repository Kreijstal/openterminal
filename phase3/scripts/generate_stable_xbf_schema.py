#!/usr/bin/env python3
"""Generate stable XBF index names from the pinned MIT WinUI source."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


ENUM = re.compile(r"^\s*([A-Za-z0-9_]+)\s*=\s*([0-9]+),\s*$")
TYPE = re.compile(
    r'\{ StableXbfTypeIndex\.([A-Za-z0-9_]+), new XamlTypeInfo\("([^"]+)"'
)
PROPERTY = re.compile(
    r'\{ StableXbfPropertyIndex\.([A-Za-z0-9_]+), new XamlPropertyInfo\("([^"]+)"'
)


def windows_namespace(name: str) -> str:
    # The published table is from the WinUI 3 namespace migration. Stable XBF
    # indices retain their meaning; Windows Terminal's WinUI 2 XBF resolves the
    # framework namespace to Windows.UI.Xaml.
    if name.startswith("Microsoft.UI.Xaml"):
        return "Windows.UI.Xaml" + name[len("Microsoft.UI.Xaml"):]
    if name.startswith("Microsoft.UI."):
        return "Windows.UI." + name[len("Microsoft.UI."):]
    return name


def enum_values(text: str, enum_name: str) -> dict[str, int]:
    marker = f"public enum {enum_name}"
    start = text.find(marker)
    if start < 0:
        raise RuntimeError(f"missing {marker}")
    body_start = text.find("{", start)
    body_end = text.find("\n    }", body_start)
    values: dict[str, int] = {}
    for line in text[body_start + 1:body_end].splitlines():
        match = ENUM.match(line)
        if match:
            values[match.group(1)] = int(match.group(2))
    if not values:
        raise RuntimeError(f"empty {enum_name}")
    return values


def make_table(text: str, values: dict[str, int], pattern: re.Pattern[str]) -> list[str]:
    result = [""] * (max(values.values()) + 1)
    for token, name in pattern.findall(text):
        if token not in values:
            raise RuntimeError(f"mapped token absent from enum: {token}")
        result[values[token]] = windows_namespace(name)
    return result


def quote(value: str) -> str:
    if not value:
        return "nullptr"
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    text = args.source.read_text(encoding="utf-8-sig")
    types = make_table(text, enum_values(text, "StableXbfTypeIndex"), TYPE)
    properties = make_table(text, enum_values(text, "StableXbfPropertyIndex"), PROPERTY)
    content = """// Generated from the exact source revision in phase2/upstreams.json.
#pragma once
#include <cstddef>
#include <cstdint>

namespace openxaml::xbf::generated {
inline constexpr const char* kTypes[] = {
"""
    content += "".join(f"    {quote(value)},\n" for value in types)
    content += """};
inline constexpr const char* kProperties[] = {
"""
    content += "".join(f"    {quote(value)},\n" for value in properties)
    content += """};
inline const char* TypeName(std::uint16_t index) {
    return index < sizeof(kTypes) / sizeof(kTypes[0]) ? kTypes[index] : nullptr;
}
inline const char* PropertyName(std::uint16_t index) {
    return index < sizeof(kProperties) / sizeof(kProperties[0]) ? kProperties[index] : nullptr;
}
}  // namespace openxaml::xbf::generated
"""
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(content, encoding="utf-8", newline="\n")
    print(f"generated {len(types)} stable types and {len(properties)} stable properties")


if __name__ == "__main__":
    main()
