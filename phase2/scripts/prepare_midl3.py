#!/usr/bin/env python3
"""Preprocess Terminal's MIDL 3 inputs with an open C preprocessor.

The harvested SDK ``midlrt.exe`` normally starts ``cl.exe`` for files that use
``#include`` and X-macros.  MinGW builds do not have cl.exe, so this helper
performs that deterministic text-only stage with Clang and leaves MIDLRT to do
the actual MIDL 3/WinMD compilation.

All output is generated below the caller-selected build directory.  No source
checkout is modified.
"""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import tempfile
from pathlib import Path


COMMA_TOKEN = "__OPENTERMINAL_MIDL_COMMA__"


def require_directory(path: Path, description: str) -> Path:
    resolved = path.resolve()
    if not resolved.is_dir():
        raise RuntimeError(f"missing {description}: {resolved}")
    return resolved


def preprocess_idl(clang: str, source: Path, source_dir: Path, output: Path) -> None:
    # Terminal uses ``COMMA`` to hide the comma in generic types from MSVC's
    # traditional preprocessor. GCC and Clang rescan it while expanding the
    # outer macro and consequently see an extra argument. Keep the comma
    # opaque through preprocessing, then restore it in the MIDL text.
    text = source.read_text(encoding="utf-8")
    text = re.sub(
        r"(?m)^(\s*#\s*define\s+COMMA\s+),\s*$",
        rf"\1{COMMA_TOKEN}",
        text,
    )

    with tempfile.TemporaryDirectory(prefix="openterminal-midl3-") as temp:
        staged = Path(temp) / source.name
        staged.write_text(text, encoding="utf-8", newline="\n")
        result = subprocess.run(
            [
                clang,
                "-E",
                "-P",
                "-x",
                "c",
                "-fms-extensions",
                "-I",
                str(source_dir),
                str(staged),
            ],
            check=True,
            capture_output=True,
            text=True,
        )

    prepared = result.stdout.replace(COMMA_TOKEN, ",")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(prepared, encoding="utf-8", newline="\n")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--clang", default="clang")
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    clang = shutil.which(args.clang)
    if clang is None:
        raise RuntimeError(f"required C preprocessor is not on PATH: {args.clang}")

    source_dir = require_directory(args.source, "MIDL 3 source directory")
    output_dir = args.output.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    idls = sorted(source_dir.glob("*.idl"))
    if not idls:
        raise RuntimeError(f"no MIDL 3 inputs found below {source_dir}")

    for source in idls:
        preprocess_idl(clang, source, source_dir, output_dir / source.name)

    print(f"Prepared {len(idls)} MIDL 3 inputs in {output_dir}")


if __name__ == "__main__":
    main()
