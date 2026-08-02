#!/usr/bin/env python3
"""Harvest deterministic dependency metadata from a Windows Terminal checkout."""

from __future__ import annotations

import argparse
import json
import subprocess
import xml.etree.ElementTree as ET
from pathlib import Path
from typing import Any


def git(repo: Path, *arguments: str) -> str:
    return subprocess.check_output(
        ["git", "-C", str(repo), *arguments], text=True
    ).strip()


def local_name(tag: str) -> str:
    return tag.rsplit("}", 1)[-1]


def load_vcpkg(repo: Path) -> list[dict[str, Any]]:
    manifests: list[dict[str, Any]] = []
    for path in sorted(repo.rglob("vcpkg.json")):
        if ".git" in path.parts:
            continue
        document = json.loads(path.read_text(encoding="utf-8"))
        dependencies: list[dict[str, Any]] = []
        for dependency in document.get("dependencies", []):
            if isinstance(dependency, str):
                dependencies.append({"name": dependency})
            else:
                dependencies.append(dict(sorted(dependency.items())))
        manifests.append(
            {
                "file": path.relative_to(repo).as_posix(),
                "builtin_baseline": document.get("builtin-baseline"),
                "dependencies": sorted(
                    dependencies, key=lambda item: (item.get("name", ""), json.dumps(item))
                ),
                "features": sorted(document.get("features", {}).keys()),
            }
        )
    return manifests


def load_nuget(repo: Path) -> list[dict[str, Any]]:
    references: set[tuple[str, str, str, str]] = set()
    candidates = (
        list(repo.rglob("packages.config"))
        + list(repo.rglob("*.props"))
        + list(repo.rglob("*.targets"))
        + list(repo.rglob("*.csproj"))
        + list(repo.rglob("*.vcxproj"))
    )
    for path in sorted(set(candidates)):
        if ".git" in path.parts:
            continue
        try:
            root = ET.parse(path).getroot()
        except ET.ParseError:
            continue
        for element in root.iter():
            tag = local_name(element.tag)
            if tag == "package":
                package_id = element.get("id")
                version = element.get("version", "")
            elif tag in {"PackageReference", "PackageVersion"}:
                package_id = element.get("Include") or element.get("Update")
                version = element.get("Version", "")
                if not version:
                    version_node = next(
                        (child for child in element if local_name(child.tag) == "Version"),
                        None,
                    )
                    version = version_node.text.strip() if version_node is not None and version_node.text else ""
            else:
                continue
            if package_id:
                references.add(
                    (path.relative_to(repo).as_posix(), package_id, version, tag)
                )
    return [
        {"file": file, "id": package_id, "version": version, "declaration": declaration}
        for file, package_id, version, declaration in sorted(references)
    ]


def load_vendored_entries(repo: Path) -> list[str]:
    dependency_root = repo / "dep"
    if not dependency_root.is_dir():
        return []
    return sorted(path.relative_to(repo).as_posix() for path in dependency_root.iterdir())


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("repository", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    repo = args.repository.resolve()
    if not (repo / ".git").exists():
        parser.error(f"not a Git checkout: {repo}")

    inventory = {
        "schema_version": 1,
        "source": {
            "repository": git(repo, "remote", "get-url", "origin"),
            "commit": git(repo, "rev-parse", "HEAD"),
            "commit_date": git(repo, "show", "-s", "--format=%cI", "HEAD"),
        },
        "vcpkg_manifests": load_vcpkg(repo),
        "nuget_references": load_nuget(repo),
        "vendored_entries": load_vendored_entries(repo),
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(inventory, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )


if __name__ == "__main__":
    main()
