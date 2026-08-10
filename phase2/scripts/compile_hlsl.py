#!/usr/bin/env python3
"""Compile one Terminal HLSL shader to a C header with a pinned SDK fxc."""

from __future__ import annotations

import argparse
import os
import subprocess
from pathlib import Path


def wine_path(path: Path) -> str:
    return "Z:" + str(path.resolve()).replace("/", "\\")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--wine", type=Path, required=True)
    parser.add_argument("--fxc", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--profile", choices=("ps_4_0", "vs_4_0"), required=True)
    parser.add_argument("--variable")
    args = parser.parse_args()

    fxc = args.fxc.resolve()
    source = args.source.resolve()
    output = args.output.resolve()
    variable = args.variable or source.stem

    if not fxc.is_file():
        raise RuntimeError(f"fxc is missing: {fxc}")
    if not source.is_file():
        raise RuntimeError(f"shader source is missing: {source}")
    if not variable.isidentifier():
        raise RuntimeError(f"shader variable is not a C identifier: {variable}")

    output.parent.mkdir(parents=True, exist_ok=True)
    binary_output = output.with_suffix(".dxbc")
    binary_partial = output.with_name(f"{output.stem}.partial.dxbc")
    binary_partial.unlink(missing_ok=True)
    environment = os.environ.copy()
    environment["WINEDEBUG"] = "-all"
    environment["WINEPATH"] = wine_path(fxc.parent)
    # Never share the caller's desktop Wine server. A separately built Wine
    # may be running there, which makes the pinned SDK compiler fail with a
    # client/server protocol mismatch. Keep fxc's prefix with build outputs.
    environment["WINEPREFIX"] = str(output.parent / ".wine-fxc")
    command = [
        str(args.wine),
        str(fxc),
        "/nologo",
        "/T",
        args.profile,
        "/E",
        "main",
        "/Fo",
        wine_path(binary_partial),
        "/WX",
        "/Zi",
        "/O3",
        "/Zsb",
        "/Qstrip_debug",
        "/Qstrip_reflect",
        "/all_resources_bound",
        wine_path(source),
    ]
    subprocess.run(command, cwd=source.parent, env=environment, check=True)
    if not binary_partial.is_file():
        raise RuntimeError(f"fxc did not generate the expected DXBC: {binary_partial}")

    shader = binary_partial.read_bytes()
    if len(shader) < 32 or not shader.startswith(b"DXBC"):
        raise RuntimeError(f"fxc generated invalid DXBC: {binary_partial}")
    binary_partial.replace(binary_output)

    lines = ["#pragma once", "", f"const unsigned char {variable}[] =", "{"]
    for offset in range(0, len(shader), 12):
        values = ", ".join(f"0x{value:02x}" for value in shader[offset : offset + 12])
        lines.append(f"    {values},")
    lines.extend(("};", ""))

    header_partial = output.with_name(f"{output.name}.partial")
    header_partial.write_text("\n".join(lines), encoding="utf-8", newline="\n")
    header_partial.replace(output)


if __name__ == "__main__":
    main()
