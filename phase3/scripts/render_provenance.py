#!/usr/bin/env python3
"""What a GDI dump root was made from, so a stale one cannot pass for a fresh one.

The Wine render gate reads dumps out of a scratch directory that nothing
regenerates on its behalf: `phase3/scripts/build_render.py` writes them and
`phase4/tests/test_render_wine.py` reads whatever is there. That is how the gate
stayed green through waves 5 and 6 while the dumps predated the sources -- the
test had no way to tell a dump root written by this checkout from one written by
a different one, so it asked the only question it could and got the wrong
answer confidently.

So the writer now records what it built from, next to what it built, and the
reader verifies it:

* **the generator** -- a digest over every source file the harness is compiled
  from, so a change to the layout core, the render core or the GDI backend
  invalidates the dumps that came out of the previous one;
* **the cases** -- a digest over the case corpus, which is generator output and
  not committed, so a regenerated corpus invalidates the dumps too.

Both are content digests rather than a version counter, because nothing would
remember to bump a counter -- that is the same failure this exists to stop.

The fonts and the theme resources are recorded but not verified: they are
fetched artifacts that live outside the repository, may legitimately be absent
at test time, and cannot make a dump root disagree with the *sources* the gate
speaks for. What they can do is change what a case measures, so their identity
travels in the record and the report prints it.
"""

from __future__ import annotations

import hashlib
import json
import os
import subprocess
from pathlib import Path
from typing import Any, Iterable

SCHEMA_VERSION = 1
PROVENANCE_NAME = "provenance.json"


def _digest_of_files(root: Path, files: Iterable[Path]) -> str:
    """A digest over (path relative to root, content) for a sorted file list.

    The path is in the hash, so a renamed or removed file moves the digest even
    when the bytes elsewhere are untouched.
    """
    hasher = hashlib.sha256()
    for path in sorted(files, key=lambda p: str(p).replace(os.sep, "/")):
        try:
            relative = path.relative_to(root)
        except ValueError:
            relative = Path(path.name)
        hasher.update(str(relative).replace(os.sep, "/").encode("utf-8"))
        hasher.update(b"\0")
        hasher.update(hashlib.sha256(path.read_bytes()).digest())
    return hasher.hexdigest()


# Directories that hold build output rather than source. A digest that walked
# into one would move on every rebuild and never agree with itself.
_NOT_SOURCE = {"build", "__pycache__", ".git", "CMakeFiles"}
_SOURCE_SUFFIXES = {".cpp", ".h", ".hpp", ".inl", ".txt", ".cmake"}


def source_digest(repository: Path, roots: Iterable[Path]) -> str:
    """The digest of everything the harness is compiled from.

    Whole directories rather than the compile list, because the compile list is
    `.cpp` files and the rule this gate broke on lives in a header: the
    alignment placement that moved a Border to x=140 is an inline function in
    `phase3/layout/src/layout.h`, which no source list names.
    """
    files: list[Path] = []
    for root in roots:
        root = Path(root).resolve()
        if root.is_file():
            files.append(root)
            continue
        for path in root.rglob("*"):
            if not path.is_file():
                continue
            if _NOT_SOURCE & set(path.relative_to(root).parts):
                continue
            if path.suffix.lower() in _SOURCE_SUFFIXES:
                files.append(path)
    return _digest_of_files(repository, files)


def tree_digest(directory: Path, pattern: str = "*") -> str | None:
    """The digest of a directory of inputs, or None when it is not there."""
    directory = Path(directory)
    if not directory.is_dir():
        return None
    return _digest_of_files(
        directory, [p for p in directory.rglob(pattern) if p.is_file()]
    )


def _git_head(repository: Path) -> str | None:
    try:
        out = subprocess.run(
            ["git", "-C", str(repository), "rev-parse", "HEAD"],
            capture_output=True, text=True, check=False)
    except OSError:
        return None
    return out.stdout.strip() or None


def source_roots(phase3: Path) -> list[Path]:
    """The trees whose contents decide what a dump looks like."""
    return [Path(phase3) / "layout" / "src", Path(phase3) / "render"]


def record(
    repository: Path,
    sources: Iterable[Path],
    cases: Path,
    fonts: Path | None,
    theme_resources: Path | None,
    sidecar_schema: int,
    glyph_outlines: Path | None = None,
) -> dict[str, Any]:
    """Builds the record. Pure, so a test can compute one without writing dumps."""
    cases = Path(cases)
    return {
        "schema_version": SCHEMA_VERSION,
        "sidecar_schema": sidecar_schema,
        "generator_digest": source_digest(repository, sources),
        "cases_path": str(cases.resolve()),
        "cases_digest": tree_digest(cases, "*.json"),
        "cases_count": sum(1 for _ in cases.rglob("*.json")) if cases.is_dir() else 0,
        "fonts_path": str(Path(fonts).resolve()) if fonts else None,
        "fonts_digest": tree_digest(fonts) if fonts else None,
        "theme_resources_path": (
            str(Path(theme_resources).resolve()) if theme_resources else None
        ),
        "theme_resources_digest": tree_digest(theme_resources) if theme_resources else None,
        # Recorded but, like the fonts, not verified against the sources: the
        # outlines are a fetched artifact. What the identity buys is the pin --
        # the Wine gate reads this path back and holds every run in an
        # outline-backed family to painting or a named refusal, so a dump root
        # painted with the artifact cannot be re-checked without it.
        "glyph_outlines_path": (
            str(Path(glyph_outlines).resolve()) if glyph_outlines else None
        ),
        "glyph_outlines_digest": tree_digest(glyph_outlines) if glyph_outlines else None,
        "repository_head": _git_head(repository),
    }


def write(dumps: Path, payload: dict[str, Any]) -> Path:
    """Writes the record inside the dump directory it describes.

    Inside, not beside: build_render.py deletes and recreates the dump
    directory for every run, so a record that lives in it cannot outlive the
    dumps it speaks for. `check_render.iter_cases` skips it by name.
    """
    path = Path(dumps) / PROVENANCE_NAME
    path.write_text(json.dumps(payload, indent=1, sort_keys=True) + "\n", encoding="utf-8")
    return path


def read(dumps: Path) -> dict[str, Any] | None:
    path = Path(dumps) / PROVENANCE_NAME
    if not path.is_file():
        return None
    return json.loads(path.read_text(encoding="utf-8"))


def disagreements(recorded: dict[str, Any], current: dict[str, Any]) -> list[str]:
    """Names every way the dumps fail to correspond to the current inputs.

    An empty list is the only thing that means "these dumps are this
    checkout's". Anything unverifiable is named too, because a dump root that
    cannot be shown to be current is exactly the one that must not be believed.
    """
    problems: list[str] = []
    if recorded.get("schema_version") != SCHEMA_VERSION:
        problems.append(
            f"the record is provenance schema {recorded.get('schema_version')!r}, not "
            f"{SCHEMA_VERSION}"
        )
    if recorded.get("sidecar_schema") != current.get("sidecar_schema"):
        problems.append(
            f"the dumps carry sidecar schema {recorded.get('sidecar_schema')!r}, this "
            f"checkout writes {current.get('sidecar_schema')!r}"
        )
    if recorded.get("generator_digest") != current.get("generator_digest"):
        problems.append(
            "the harness sources changed since these dumps were written "
            f"(generator {str(recorded.get('generator_digest'))[:12]} -> "
            f"{str(current.get('generator_digest'))[:12]})"
        )
    if recorded.get("cases_path") != current.get("cases_path"):
        problems.append(
            f"these dumps were generated from {recorded.get('cases_path')!r}, not "
            f"{current.get('cases_path')!r}"
        )
    elif current.get("cases_digest") is None:
        problems.append(
            f"the case corpus at {recorded.get('cases_path')!r} is gone, so the dumps "
            "cannot be shown to correspond to it"
        )
    elif recorded.get("cases_digest") != current.get("cases_digest"):
        problems.append(
            "the case corpus changed since these dumps were written "
            f"({recorded.get('cases_count')} -> {current.get('cases_count')} case files, "
            f"digest {str(recorded.get('cases_digest'))[:12]} -> "
            f"{str(current.get('cases_digest'))[:12]})"
        )
    return problems
