#!/usr/bin/env python3
"""Build WPF's open architecture-neutral XAML tasks from an exact commit."""

from __future__ import annotations

import argparse
import json
import os
import shlex
import subprocess
from pathlib import Path
from typing import Any


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
UPSTREAMS_FILE = REPOSITORY_ROOT / "phase2" / "upstreams.json"


def run(command: list[str], *, cwd: Path | None = None, env: dict[str, str] | None = None) -> None:
    print(f"+ {shlex.join(command)}", flush=True)
    subprocess.run(command, cwd=cwd, env=env, check=True)


def output(command: list[str], *, cwd: Path | None = None) -> str:
    return subprocess.check_output(command, cwd=cwd, text=True).strip()


def load_pin() -> dict[str, Any]:
    with UPSTREAMS_FILE.open(encoding="utf-8") as stream:
        return json.load(stream)["wpf"]


def validate_root(root: Path) -> Path:
    resolved = root.resolve()
    temporary_root = Path("/tmp").resolve()
    if resolved == temporary_root or temporary_root not in resolved.parents:
        raise ValueError("--root must name a directory below /tmp")
    resolved.mkdir(parents=True, exist_ok=True)
    return resolved


def checkout_pin(destination: Path, pin: dict[str, Any]) -> None:
    git_directory = destination / ".git"
    if git_directory.is_dir():
        actual = output(["git", "rev-parse", "HEAD"], cwd=destination)
        if actual != pin["commit"]:
            raise RuntimeError(
                f"{destination} is at {actual}; expected pinned WPF commit {pin['commit']}"
            )
        return

    if destination.exists() and any(destination.iterdir()):
        raise RuntimeError(f"refusing to replace non-checkout directory {destination}")

    destination.mkdir(parents=True, exist_ok=True)
    run(["git", "init", str(destination)])
    run(["git", "-C", str(destination), "remote", "add", "origin", pin["repository"]])
    run(
        [
            "git",
            "-C",
            str(destination),
            "fetch",
            "--depth",
            "1",
            "origin",
            pin["commit"],
        ]
    )
    run(["git", "-C", str(destination), "checkout", "--detach", pin["commit"]])


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--root",
        type=Path,
        default=Path("/tmp/openterminal-wpf"),
        help="temporary source/build root (must be below /tmp)",
    )
    args = parser.parse_args()

    root = validate_root(args.root)
    source = root / "wpf"
    checkout_pin(source, load_pin())

    environment = os.environ.copy()
    environment.update(
        {
            "DOTNET_CLI_HOME": str(root / "dotnet-home"),
            "DOTNET_CLI_TELEMETRY_OPTOUT": "1",
            "DOTNET_NOLOGO": "1",
            "NUGET_PACKAGES": str(root / "nuget-packages"),
        }
    )
    project = (
        source
        / "packaging"
        / "Microsoft.NET.Sdk.WindowsDesktop"
        / "Microsoft.NET.Sdk.WindowsDesktop.ArchNeutral.csproj"
    )
    run(
        [
            str(source / "eng" / "common" / "dotnet.sh"),
            "build",
            str(project),
            "--configuration",
            "Release",
            "--nologo",
            "--verbosity",
            "minimal",
        ],
        cwd=source,
        env=environment,
    )
    print(f"WPF architecture-neutral XAML tasks built under {root}")


if __name__ == "__main__":
    main()
