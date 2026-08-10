#!/usr/bin/env python3
"""Materialize the pinned source programs behind the rendering learning path.

The committed manifest maps eight deliberately small OpenTerminal exercises to
authoritative Microsoft samples.  This harvester verifies every upstream Git
commit, rejects binary/untracked inputs, copies the selected textual sources to
a disposable directory, and writes a deterministic inventory.  It never
builds the samples and never places their binaries in this repository.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path, PurePosixPath


TEXT_SUFFIXES = {
    ".appxmanifest",
    ".cpp",
    ".cs",
    ".h",
    ".manifest",
    ".md",
    ".sln",
    ".txt",
    ".vcxproj",
    ".xaml",
}
TEXT_FILENAMES = {"LICENSE", "LICENSE.txt"}


class HarvestError(Exception):
    """The requested materialization would not be a pinned textual snapshot."""


def git(repo: Path, *arguments: str) -> str:
    try:
        return subprocess.check_output(
            ["git", "-C", str(repo), *arguments],
            text=True,
            stderr=subprocess.STDOUT,
        ).strip()
    except subprocess.CalledProcessError as error:
        detail = error.output.strip() or f"git exited {error.returncode}"
        raise HarvestError(f"{repo}: {detail}") from error


def load_manifest(path: Path) -> dict:
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise HarvestError(f"cannot read {path}: {error}") from error
    if manifest.get("schema_version") != 1:
        raise HarvestError(f"{path}: unsupported manifest schema")
    repositories = manifest.get("repositories")
    source_sets = manifest.get("source_sets")
    programs = manifest.get("programs")
    if not isinstance(repositories, dict) or not repositories:
        raise HarvestError(f"{path}: repositories must be a non-empty object")
    if not isinstance(source_sets, dict) or not source_sets:
        raise HarvestError(f"{path}: source_sets must be a non-empty object")
    if not isinstance(programs, list) or not programs:
        raise HarvestError(f"{path}: programs must be a non-empty array")
    names = [program.get("name") for program in programs if isinstance(program, dict)]
    if len(names) != len(programs) or any(not isinstance(name, str) or not name for name in names):
        raise HarvestError(f"{path}: every program needs a name")
    if len(names) != len(set(names)):
        raise HarvestError(f"{path}: program names are not unique")
    return manifest


def parse_roots(values: list[str]) -> dict[str, Path]:
    roots: dict[str, Path] = {}
    for value in values:
        name, separator, raw_path = value.partition("=")
        if not separator or not name or not raw_path:
            raise HarvestError(f"--repo expects NAME=PATH, not {value!r}")
        if name in roots:
            raise HarvestError(f"repository root {name!r} was supplied twice")
        roots[name] = Path(raw_path).resolve()
    return roots


def checked_relative_path(value: object, where: str) -> PurePosixPath:
    if not isinstance(value, str) or not value:
        raise HarvestError(f"{where}: source path is not a string")
    path = PurePosixPath(value)
    if path.is_absolute() or ".." in path.parts:
        raise HarvestError(f"{where}: source path leaves its repository: {value!r}")
    if path.name not in TEXT_FILENAMES and path.suffix.lower() not in TEXT_SUFFIXES:
        raise HarvestError(f"{where}: {value!r} is not an allowed textual source")
    return path


def verify_repositories(manifest: dict, roots: dict[str, Path]) -> None:
    expected = set(manifest["repositories"])
    if set(roots) != expected:
        missing = sorted(expected - set(roots))
        extra = sorted(set(roots) - expected)
        details = []
        if missing:
            details.append("missing " + ", ".join(missing))
        if extra:
            details.append("unknown " + ", ".join(extra))
        raise HarvestError("repository roots do not match the manifest: " + "; ".join(details))
    for name, description in manifest["repositories"].items():
        root = roots[name]
        if not root.is_dir():
            raise HarvestError(f"{name}: no repository directory at {root}")
        expected_commit = description.get("commit")
        if not isinstance(expected_commit, str) or len(expected_commit) != 40:
            raise HarvestError(f"{name}: the manifest has no full Git commit")
        actual_commit = git(root, "rev-parse", "HEAD")
        if actual_commit != expected_commit:
            raise HarvestError(
                f"{name}: expected {expected_commit}, checkout is {actual_commit}")


def selected_sources(manifest: dict) -> tuple[dict[tuple[str, str], set[str]], dict[str, list[str]]]:
    files: dict[tuple[str, str], set[str]] = {}
    program_sets: dict[str, list[str]] = {}
    source_sets = manifest["source_sets"]
    for program in manifest["programs"]:
        name = program["name"]
        sets = program.get("source_sets")
        if not isinstance(sets, list) or not sets:
            raise HarvestError(f"program {name!r} has no source_sets")
        program_sets[name] = []
        for set_name in sets:
            if set_name not in source_sets:
                raise HarvestError(f"program {name!r} names unknown source set {set_name!r}")
            if set_name not in program_sets[name]:
                program_sets[name].append(set_name)

    for set_name, source_set in source_sets.items():
        if not isinstance(source_set, dict):
            raise HarvestError(f"source set {set_name!r} is not an object")
        repository = source_set.get("repository")
        if repository not in manifest["repositories"]:
            raise HarvestError(
                f"source set {set_name!r} names unknown repository {repository!r}")
        paths = source_set.get("paths")
        if not isinstance(paths, list) or not paths:
            raise HarvestError(f"source set {set_name!r} has no paths")
        used_by = {
            name for name, sets in program_sets.items() if set_name in sets
        }
        if not used_by:
            raise HarvestError(f"source set {set_name!r} is not used by a program")
        for raw_path in paths:
            path = checked_relative_path(raw_path, f"source set {set_name!r}")
            files.setdefault((repository, path.as_posix()), set()).update(used_by)
    return files, program_sets


def source_record(repository: str, relative: str, data: bytes, used_by: set[str]) -> dict:
    if b"\0" in data:
        raise HarvestError(f"{repository}:{relative}: contains NUL bytes")
    encoding = "utf-8-sig" if data.startswith(b"\xef\xbb\xbf") else "utf-8"
    try:
        text = data.decode(encoding)
    except UnicodeDecodeError:
        encoding = "windows-1252"
        try:
            text = data.decode(encoding)
        except UnicodeDecodeError as error:
            raise HarvestError(f"{repository}:{relative}: is not reviewable text") from error
    if any(ord(unit) < 32 and unit not in "\t\n\r\f" for unit in text):
        raise HarvestError(f"{repository}:{relative}: contains binary control bytes")
    lines = data.count(b"\n")
    if data and not data.endswith(b"\n"):
        lines += 1
    return {
        "repository": repository,
        "path": relative,
        "sha256": hashlib.sha256(data).hexdigest(),
        "bytes": len(data),
        "encoding": encoding,
        "lines": lines,
        "used_by": sorted(used_by),
    }


def materialize(manifest: dict, roots: dict[str, Path], output: Path) -> dict:
    verify_repositories(manifest, roots)
    files, _ = selected_sources(manifest)
    if output.exists() and any(output.iterdir()):
        raise HarvestError(f"output directory is not empty: {output}")
    output.mkdir(parents=True, exist_ok=True)

    inventory_files = []
    for (repository, relative), used_by in sorted(files.items()):
        root = roots[repository]
        # Only tracked data from the pinned commit may enter the bundle.
        git(root, "ls-files", "--error-unmatch", "--", relative)
        source = root / Path(relative)
        if not source.is_file():
            raise HarvestError(f"{repository}:{relative}: tracked source is absent")
        data = source.read_bytes()
        record = source_record(repository, relative, data, used_by)
        destination = output / "sources" / repository / Path(relative)
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(data)
        inventory_files.append(record)

    inventory = {
        "schema_version": 1,
        "repositories": {
            name: {
                "url": description["url"],
                "commit": description["commit"],
                "license": description["license"],
            }
            for name, description in sorted(manifest["repositories"].items())
        },
        "programs": manifest["programs"],
        "files": inventory_files,
    }
    rendered = json.dumps(inventory, indent=2, sort_keys=True) + "\n"
    (output / "inventory.json").write_text(rendered, encoding="utf-8")
    return inventory


def check_inventory(actual: dict, expected_path: Path) -> None:
    try:
        expected = json.loads(expected_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise HarvestError(f"cannot read expected inventory {expected_path}: {error}") from error
    if actual != expected:
        raise HarvestError(
            f"harvest differs from {expected_path}; inspect the generated inventory before "
            "accepting a new upstream snapshot")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    default_manifest = (Path(__file__).resolve().parents[2] / "research" /
                        "rendering-programs" / "manifest.json")
    parser.add_argument("--manifest", type=Path, default=default_manifest)
    parser.add_argument(
        "--repo", action="append", default=[], metavar="NAME=PATH",
        help="checkout for one repository named by the manifest; repeat for every repository")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--expect", type=Path,
        help="fail unless the generated inventory equals this committed snapshot")
    arguments = parser.parse_args()
    try:
        manifest = load_manifest(arguments.manifest)
        roots = parse_roots(arguments.repo)
        inventory = materialize(manifest, roots, arguments.output.resolve())
        if arguments.expect:
            check_inventory(inventory, arguments.expect)
    except (HarvestError, OSError) as error:
        raise SystemExit(str(error)) from error
    print(
        f"harvested {len(inventory['files'])} textual source files for "
        f"{len(inventory['programs'])} rendering programs")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
