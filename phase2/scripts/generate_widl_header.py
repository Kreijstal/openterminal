#!/usr/bin/env python3
"""Generate a classic COM header with Wine widl from a pinned Terminal IDL."""

from __future__ import annotations

import argparse
import hashlib
import re
import subprocess
from pathlib import Path


SYSTEM_HANDLE = re.compile(r",\s*system_handle\s*\(\s*[A-Za-z_]\w*\s*\)")


def alphabetic_hash(content: bytes) -> str:
    digest = hashlib.sha256(content).hexdigest()[:16]
    return digest.translate(str.maketrans("0123456789", "ghijklmnop"))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--widl", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--input-label", default="ITerminalHandoff.idl")
    args = parser.parse_args()

    source = args.source.resolve()
    output = args.output.resolve()
    text = source.read_text(encoding="utf-8")
    normalized = SYSTEM_HANDLE.sub("", text)
    if "system_handle" in normalized:
        raise RuntimeError("unhandled system_handle annotation remains in normalized IDL")

    encoded = normalized.encode("utf-8")
    stage = Path("/tmp") / f"openterminal-widl-{alphabetic_hash(encoded)}"
    stage.mkdir(parents=True, exist_ok=True)
    staged_idl = stage / source.name
    if not staged_idl.is_file() or staged_idl.read_bytes() != encoded:
        staged_partial = staged_idl.with_suffix(".idl.partial")
        staged_partial.write_bytes(encoded)
        staged_partial.replace(staged_idl)

    generated = stage / f"{source.stem}.h"
    subprocess.run(
        [str(args.widl), "--win64", "-h", "-H", str(generated), str(staged_idl)],
        check=True,
    )
    if not generated.is_file():
        raise RuntimeError(f"widl did not generate the expected header: {generated}")

    result = generated.read_text(encoding="utf-8")
    result = result.replace(str(staged_idl), args.input_label)
    output.parent.mkdir(parents=True, exist_ok=True)
    output_partial = output.with_name(f"{output.name}.partial")
    output_partial.write_text(result, encoding="utf-8", newline="\n")
    output_partial.replace(output)


if __name__ == "__main__":
    main()
