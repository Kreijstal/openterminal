#!/usr/bin/env python3
"""Rewrite unsupported API-set imports to ReactOS host DLLs in a PE image."""

from __future__ import annotations

import argparse
from pathlib import Path

import pefile


REPLACEMENTS = {
    b"api-ms-win-core-libraryloader-l1-2-1.dll": b"kernel32.dll",
    b"api-ms-win-core-localization-l1-2-2.dll": b"kernel32.dll",
    b"api-ms-win-core-processthreads-l1-1-3.dll": b"kernel32.dll",
    b"api-ms-win-core-registry-l1-1-0.dll": b"advapi32.dll",
    b"api-ms-win-core-version-l1-1-0.dll": b"version.dll",
    b"api-ms-win-core-wow64-l1-1-1.dll": b"kernelbase.dll",
    b"api-ms-win-security-base-l1-1-0.dll": b"advapi32.dll",
    b"api-ms-win-security-sddl-l1-1-0.dll": b"advapi32.dll",
    b"api-ms-win-service-management-l1-1-0.dll": b"advapi32.dll",
}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--partial", action="store_true",
                        help="rewrite only mappings imported by this image")
    parser.add_argument("--collapse-api-sets", action="store_true",
                        help="map remaining core/CRT API sets to their host DLLs")
    args = parser.parse_args()

    image = pefile.PE(str(args.source), fast_load=False)
    imports = {entry.dll.lower(): entry for entry in image.DIRECTORY_ENTRY_IMPORT}
    replacements = dict(REPLACEMENTS)
    if args.collapse_api_sets:
        for name in imports:
            if name.startswith(b"api-ms-win-crt-"):
                replacements.setdefault(name, b"ucrtbase.dll")
            elif name.startswith(b"api-ms-win-core-registry-"):
                replacements.setdefault(name, b"advapi32.dll")
            elif name.startswith(b"api-ms-win-core-"):
                replacements.setdefault(name, b"kernel32.dll")
    missing = sorted(name.decode() for name in replacements if name not in imports)
    if missing and not args.partial:
        raise RuntimeError(f"expected imports are missing: {missing}")

    data = bytearray(args.source.read_bytes())
    rewritten = 0
    for source, destination in replacements.items():
        if source not in imports:
            continue
        offset = image.get_offset_from_rva(imports[source].struct.Name)
        if len(destination) > len(source):
            raise RuntimeError(f"replacement is too long: {destination!r}")
        data[offset : offset + len(source) + 1] = destination.ljust(len(source) + 1, b"\0")
        rewritten += 1

    args.destination.parent.mkdir(parents=True, exist_ok=True)
    args.destination.write_bytes(data)
    print(f"rewrote {rewritten} imports: {args.destination}")


if __name__ == "__main__":
    main()
