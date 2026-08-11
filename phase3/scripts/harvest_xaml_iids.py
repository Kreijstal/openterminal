#!/usr/bin/env python3
"""Emit IID definitions for the WinRT interfaces the layout DLL implements.

GCC does not implement `__declspec(uuid)`, so `__uuidof` -- the way the SDK's
own headers hand out interface IDs -- is unavailable under MinGW. The IIDs are
therefore harvested from the pinned SDK's `.idl` sources, where they are
written out literally, and emitted as ordinary constants.

Deriving them beats transcribing them. A mistyped IID does not fail to build;
it produces a `QueryInterface` that silently answers `E_NOINTERFACE` for the
one interface a caller needed, which reads as "the class is not implemented".

Only the interfaces named on the command line are emitted, so the generated
header states exactly which contracts the DLL binds to.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

# Two spellings, both meaning the same thing: `[uuid(...)]` alone on a line,
# and a bare `uuid(...),` entry inside a multi-line attribute block.
UUID = re.compile(r"^\s*\[?\s*uuid\(([0-9A-Fa-f-]{36})\)\s*(?:\]|,)?\s*$")
INTERFACE = re.compile(r"^\s*interface\s+(I\w+)\b")
# The SDK IDL nests one component per line -- `namespace Windows` then
# `namespace UI` -- and puts the opening brace on the line after.
NAMESPACE = re.compile(r"^\s*namespace\s+([\w.]+)\s*\{?\s*$")
ENDS_ATTRIBUTES = {"runtimeclass", "delegate", "struct", "enum", "apicontract",
                   "attribute", "interface", "}"}


def harvest(idl_files: list[Path]) -> dict[str, str]:
    """Map fully qualified interface name -> IID, from `uuid` attributes.

    Namespace scope is tracked by pairing each `namespace` keyword with the
    next `{` and remembering the depth it opened at, rather than by assuming
    the brace shares the line. Interface and runtimeclass bodies open braces
    of their own, so only the recorded depth says when a namespace has ended.
    """
    found: dict[str, str] = {}
    for path in sorted(idl_files):
        scopes: list[tuple[str, int]] = []
        depth = 0
        pending_uuid: str | None = None
        pending_namespace: str | None = None

        for line in path.read_text(encoding="utf-8-sig", errors="replace").splitlines():
            stripped = line.strip()

            match = UUID.match(line)
            if match:
                pending_uuid = match.group(1).upper()
                continue

            match = NAMESPACE.match(line)
            if match:
                pending_namespace = match.group(1)
            else:
                match = INTERFACE.match(line)
                if match and pending_uuid is not None:
                    qualified = ".".join([name for name, _ in scopes] + [match.group(1)])
                    previous = found.get(qualified)
                    if previous is not None and previous != pending_uuid:
                        raise SystemExit(
                            f"{qualified}: two different IIDs, {previous} and {pending_uuid}"
                        )
                    found[qualified] = pending_uuid
                # Only another declaration ends the block. Everything else in
                # it -- pointer_default, contract, exclusiveto, the closing
                # bracket -- is still the same block and keeps the uuid.
                if stripped.split(" ", 1)[0] in ENDS_ATTRIBUTES:
                    pending_uuid = None

            for char in line:
                if char == "{":
                    if pending_namespace is not None:
                        scopes.append((pending_namespace, depth))
                        pending_namespace = None
                    depth += 1
                elif char == "}":
                    depth -= 1
                    while scopes and depth <= scopes[-1][1]:
                        scopes.pop()
    return found


# A parameterized interface -- IVector<UIElement> and friends -- has no `uuid`
# in the IDL. Its IID is computed from the type signature, and the SDK's
# generated headers are where the computed value is written down:
#
#   struct __declspec(uuid("b4c1e3ac-8768-5b9d-a661-f63330b8507b"))
#   IVector<ABI::Windows::UI::Xaml::UIElement*> : IVector_impl<...>
#
# The block ends with a typedef for the mangled name, which is the name the
# rest of the SDK -- and this DLL -- refers to the specialization by.
SPECIALIZATION = re.compile(
    r'struct __declspec\(uuid\("(?P<iid>[0-9a-fA-F-]{36})"\)\)\s*\n'
    r"\s*(?P<template>\w+)<(?P<argument>[^\n]*?)>\s*:.*?"
    r"typedef (?P=template)<(?P=argument)> (?P<mangled>__F\w+)_t;",
    re.S,
)


def harvest_specializations(headers: list[Path]) -> dict[str, str]:
    """Map the mangled specialization name -> IID, from generated headers."""
    found: dict[str, str] = {}
    for path in sorted(headers):
        text = path.read_text(encoding="utf-8", errors="replace")
        for match in SPECIALIZATION.finditer(text):
            name = match.group("mangled")
            iid = match.group("iid").upper()
            previous = found.get(name)
            if previous is not None and previous != iid:
                raise SystemExit(f"{name}: two different IIDs, {previous} and {iid}")
            found[name] = iid
    return found


def as_initialiser(iid: str) -> str:
    """`{0x676d0be9,0xb65c,0x41c6,{0xba,0x40,...}}` -- a GUID aggregate."""
    a, b, c, d, e = iid.lower().split("-")
    rest = d + e
    tail = ",".join(f"0x{rest[i:i + 2]}" for i in range(0, len(rest), 2))
    return f"{{0x{a},0x{b},0x{c},{{{tail}}}}}"


def emit(wanted: list[str], found: dict[str, str], source: str) -> str:
    lines = [
        "// Generated by phase3/scripts/harvest_xaml_iids.py. Do not edit.",
        f"// Source: {source}",
        "//",
        "// The IIDs of the WinRT interfaces the layout DLL implements, taken from",
        "// the pinned SDK IDL because GCC has no __uuidof.",
        "",
        "#ifndef OPENXAML_GENERATED_IIDS_H",
        "#define OPENXAML_GENERATED_IIDS_H",
        "",
        "#include <guiddef.h>",
        "",
        "namespace openxaml::iid {",
        "",
    ]
    missing = [name for name in wanted if name not in found]
    if missing:
        raise SystemExit("not found in the pinned IDL: " + ", ".join(missing))
    for name in wanted:
        # The SDK #defines the mangled specialization name as a macro aliasing
        # the real type, so a constant of that exact spelling would be
        # substituted away. Parameterized IIDs therefore get a distinct prefix.
        symbol = ("PIID_" + name.lstrip("_")) if name.startswith("__F") \
            else name.replace(".", "_")
        lines.append(f"// {name}")
        lines.append(
            f"inline constexpr GUID {symbol} = {as_initialiser(found[name])};"
        )
    lines += ["", "}  // namespace openxaml::iid", "", "#endif", ""]
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--idl-dir", type=Path, required=True)
    parser.add_argument("--include-dir", type=Path, required=True,
                        help="generated SDK headers, for parameterized interfaces")
    parser.add_argument("--interfaces", type=Path, required=True,
                        help="one fully qualified interface name per line")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    idl_files = sorted(args.idl_dir.glob("windows.ui.xaml*.idl"))
    colors_idl = args.idl_dir / "windows.ui.idl"
    if colors_idl.is_file():
        idl_files.append(colors_idl)
    text_idl = args.idl_dir / "windows.ui.text.idl"
    if text_idl.is_file():
        idl_files.append(text_idl)
    # The thread's CoreWindow. A XAML island thread has one, and callers read
    # the keyboard through it, so its statics are part of the surface this DLL
    # has to answer for.
    core_idl = args.idl_dir / "windows.ui.core.idl"
    if core_idl.is_file():
        idl_files.append(core_idl)
    idl_files += sorted(args.idl_dir.glob("windows.foundation*.idl"))
    # The desktop-XAML bootstrap also probes DispatcherQueue before it creates
    # WindowsXamlManager. Keep that adjacent platform contract pinned to the
    # same SDK rather than transcribing its IID.
    system_idl = args.idl_dir / "windows.system.idl"
    if system_idl.is_file():
        idl_files.append(system_idl)
    resources_idl = args.idl_dir / "windows.applicationmodel.resources.core.idl"
    if resources_idl.is_file():
        idl_files.append(resources_idl)
    # The two interfaces every WinRT object implements. Their IIDs are fixed
    # and famous, which is exactly why they should be derived rather than
    # typed in from memory.
    idl_files += [args.idl_dir / name
                  for name in ("inspectable.idl", "activation.idl")
                  if (args.idl_dir / name).is_file()]
    if not idl_files:
        raise SystemExit(f"no windows.ui.xaml*.idl under {args.idl_dir}")

    wanted = [
        line.split("#", 1)[0].strip()
        for line in args.interfaces.read_text(encoding="utf-8").splitlines()
    ]
    wanted = [name for name in wanted if name]

    found = harvest(idl_files)
    found.update(harvest_specializations(
        sorted(args.include_dir.glob("windows.ui.xaml*.h"))))
    text = emit(wanted, found, args.idl_dir.name)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.output.exists() or args.output.read_text(encoding="utf-8") != text:
        args.output.write_text(text, encoding="utf-8")
    print(f"{len(wanted)} IIDs harvested from {len(idl_files)} IDL files", file=sys.stderr)


if __name__ == "__main__":
    main()
