#!/usr/bin/env python3
"""Download an exact NuGet package from a V3 feed with optional hash pinning."""

from __future__ import annotations

import argparse
import hashlib
import json
import urllib.parse
import urllib.request
from pathlib import Path


def read_url(url: str) -> bytes:
    request = urllib.request.Request(url, headers={"User-Agent": "OpenTerminal-harvester/1"})
    with urllib.request.urlopen(request) as response:
        return response.read()


def package_base_address(source: str) -> str:
    index = json.loads(read_url(source))
    for resource in index.get("resources", []):
        resource_type = resource.get("@type", "")
        if isinstance(resource_type, str) and resource_type.startswith("PackageBaseAddress"):
            return resource["@id"].rstrip("/") + "/"
    raise RuntimeError(f"NuGet V3 source has no PackageBaseAddress resource: {source}")


def package_url(source: str, package: str, version: str) -> str:
    package_lower = package.lower()
    version_lower = version.lower()
    filename = f"{package_lower}.{version_lower}.nupkg"
    return "".join(
        (
            package_base_address(source),
            urllib.parse.quote(package_lower, safe=""),
            "/",
            urllib.parse.quote(version_lower, safe=""),
            "/",
            urllib.parse.quote(filename, safe=""),
        )
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True)
    parser.add_argument("--package", required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--expected-sha256")
    args = parser.parse_args()

    url = package_url(args.source, args.package, args.version)
    payload = read_url(url)
    digest = hashlib.sha256(payload).hexdigest()
    if args.expected_sha256 and digest.lower() != args.expected_sha256.lower():
        raise RuntimeError(
            f"SHA-256 mismatch for {args.package} {args.version}: "
            f"expected {args.expected_sha256.lower()}, got {digest}"
        )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(payload)
    print(json.dumps({"package_url": url, "sha256": digest}, sort_keys=True))


if __name__ == "__main__":
    main()
