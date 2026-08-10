#!/usr/bin/env python3
"""Fetch pinned open-source runtime fonts and write an OpenXaml alias manifest.

Font binaries are verified and written only to the requested temporary output
directory. They are never copied into the repository.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import tempfile
import urllib.request
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parents[2]
DEFAULT_SPEC = REPOSITORY / "phase3" / "xamlcore" / "runtime_fonts.json"


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def windows_path(path: Path) -> str:
    return "Z:" + str(path.resolve()).replace("/", "\\")


def prepare(spec_path: Path, output_root: Path, path_style: str) -> Path:
    specification = json.loads(spec_path.read_text(encoding="utf-8"))
    if specification.get("schema") != 1:
        raise RuntimeError("runtime font specification has an unsupported schema")
    fonts = specification.get("fonts")
    if not isinstance(fonts, list) or not fonts:
        raise RuntimeError("runtime font specification contains no fonts")

    output_root.mkdir(parents=True, exist_ok=True)
    records = ["openxaml-font-aliases-v1"]
    for font in fonts:
        filename = font.get("file")
        expected = font.get("sha256")
        url = font.get("url")
        family = font.get("family")
        aliases = font.get("aliases")
        if (not isinstance(filename, str) or Path(filename).name != filename or
                not isinstance(expected, str) or len(expected) != 64 or
                not isinstance(url, str) or not isinstance(family, str) or
                not isinstance(aliases, list) or not aliases):
            raise RuntimeError("runtime font specification has an invalid font record")
        destination = output_root / filename
        if not destination.is_file() or digest(destination) != expected:
            descriptor, temporary_name = tempfile.mkstemp(
                prefix=filename + ".", dir=output_root)
            os.close(descriptor)
            temporary = Path(temporary_name)
            try:
                with urllib.request.urlopen(url, timeout=60) as source, \
                        temporary.open("wb") as target:
                    while block := source.read(1024 * 1024):
                        target.write(block)
                actual = digest(temporary)
                if actual != expected:
                    raise RuntimeError(
                        f"runtime font hash mismatch for {filename}: {actual}")
                temporary.replace(destination)
            finally:
                temporary.unlink(missing_ok=True)
        rendered_path = (windows_path(destination) if path_style == "wine"
                         else str(destination.resolve()))
        if any("\t" in value or "\n" in value or "\r" in value
               for value in [rendered_path, family, *aliases]):
            raise RuntimeError("runtime font record contains a manifest delimiter")
        records.append("font\t" + rendered_path)
        records.extend(f"alias\t{alias}\t{family}" for alias in aliases)

    manifest = output_root / "openxaml-font-aliases.tsv"
    manifest.write_text("\n".join(records) + "\n", encoding="utf-8", newline="\n")
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--spec", type=Path, default=DEFAULT_SPEC)
    parser.add_argument("--output-root", type=Path, required=True)
    parser.add_argument("--path-style", choices=("native", "wine"), default="native")
    arguments = parser.parse_args()
    manifest = prepare(arguments.spec.resolve(), arguments.output_root.resolve(),
                       arguments.path_style)
    print(manifest)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
