#!/usr/bin/env python3
"""Install the fonts an application ships beside its executable into a prefix.

Windows Terminal deploys ``CascadiaMono.ttf`` next to ``WindowsTerminal.exe``
and loads it at runtime through ``IDWriteFontSetBuilder::AddFontSet``. Wine
implements that method as a stub -- it logs ``fixme:dwrite:
dwritefontsetbuilder_AddFontSet`` and does nothing -- so the custom collection
comes back empty and the family never resolves. AtlasEngine's ``UpdateFont``
then fails, ``LOG_IF_FAILED`` swallows the failure, ``_api.s->font`` keeps its
default ``cellSize`` of zero, and ``GetViewportInCharacters`` reaches

    viewInPixels.Width() / _api.s->font->cellSize.x

with a zero divisor. That is an unhandled integer division by zero, and it is
where the boot died before this existed.

Installing the same files into the prefix's system font collection -- the path
Wine does implement -- lets the family resolve without patching Wine and
without patching the application. Nothing is downloaded and nothing is
invented: the files installed are the ones the phase-2 build already produced,
and each one is named in the returned manifest with its digest.

The registry value is keyed by the font's own sfnt full name rather than by its
filename, because that is what Windows keys it by. It is a label: Wine indexes
the file itself and matches DirectWrite requests against the family name it
reads out of it, so the value is not what makes resolution work -- it is what
keeps the prefix an honest description of what was installed. A font whose name
table cannot be read is refused by name; no substitute name is guessed for it.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import struct
import subprocess
import sys
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPOSITORY / "phase3" / "scripts"))
from harvest_font_metrics import Font, FontError  # noqa: E402

# What Windows calls the two sfnt flavours in the Fonts key. A collection is
# registered the same way a single TrueType face is.
SUFFIX_TAGS = {".ttf": "TrueType", ".ttc": "TrueType", ".otf": "OpenType"}

# name ID 4 is the full font name: "Cascadia Mono Regular", family and style
# together, which is the string the Fonts key carries. name ID 1 would be the
# family alone.
FULL_NAME_ID = 4

REGISTRY_KEY = ("HKEY_LOCAL_MACHINE\\Software\\Microsoft\\"
                "Windows NT\\CurrentVersion\\Fonts")


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def full_name(font: Font) -> str:
    """The sfnt full name, read from the name table, or a named refusal."""
    table = font.table("name")
    if len(table) < 6:
        raise FontError("the name table is too short for its header")
    count, storage = struct.unpack_from(">HH", table, 2)

    # Windows first, in US English, because that is the record DirectWrite
    # matches and the one Windows writes into the Fonts key. The Macintosh
    # record is the documented fallback for a font that carries no Windows
    # name at all, and nothing beyond those two is guessed at.
    windows: str | None = None
    macintosh: str | None = None
    for index in range(count):
        record = 6 + index * 12
        if record + 12 > len(table):
            raise FontError("the name table runs past the end of the font")
        platform, encoding, language, name_id, length, offset = \
            struct.unpack_from(">HHHHHH", table, record)
        if name_id != FULL_NAME_ID:
            continue
        start = storage + offset
        if start + length > len(table):
            raise FontError("a name record runs past the end of the font")
        raw = table[start:start + length]
        if platform == 3 and encoding in (0, 1) and language == 0x0409:
            windows = raw.decode("utf-16-be", errors="strict")
        elif platform == 1 and encoding == 0 and language == 0:
            macintosh = raw.decode("mac-roman", errors="strict")

    name = windows or macintosh
    if not name:
        raise FontError("the font carries no full name this can read "
                        "(no Windows US-English and no Macintosh name ID 4)")
    return name


def discover(deployment: Path) -> list[Path]:
    """Every font file the deployment directory ships, in a stable order."""
    return sorted(path for path in deployment.iterdir()
                  if path.is_file() and path.suffix.lower() in SUFFIX_TAGS)


def registration(entries: list[dict]) -> str:
    lines = ["Windows Registry Editor Version 5.00", "", f"[{REGISTRY_KEY}]"]
    for entry in entries:
        lines.append(f'"{entry["registry_value"]}"="{entry["file"]}"')
    lines.append("")
    return "\n".join(lines)


def plan(deployment: Path) -> list[dict]:
    """Read every shipped font and say how it would be registered.

    Separate from install() so the decision can be tested without a prefix,
    a Wine, or a copy.
    """
    entries = []
    for path in discover(deployment):
        try:
            name = full_name(Font(path.read_bytes()))
        except FontError as error:
            raise FontError(f"{path.name}: {error}") from error
        entries.append({
            "file": path.name,
            "full_name": name,
            "registry_value": f"{name} ({SUFFIX_TAGS[path.suffix.lower()]})",
            "sha256": digest(path),
        })
    return entries


def install(deployment: Path, prefix: Path, environment: dict,
            wine: str = "wine") -> dict:
    """Copy the shipped fonts into the prefix and register them there.

    ``wine`` is the loader to invoke, because a source-tree build must not be
    mixed with the host one -- the same reason the caller passes it everywhere
    else.
    """
    entries = plan(deployment)
    fonts = prefix / "drive_c" / "windows" / "Fonts"
    fonts.mkdir(parents=True, exist_ok=True)
    for entry in entries:
        shutil.copy2(deployment / entry["file"], fonts / entry["file"])

    if entries:
        registry_file = prefix / "deployment-fonts.reg"
        registry_file.write_text(registration(entries), encoding="utf-8",
                                 newline="\n")
        subprocess.run([wine, "regedit.exe", str(registry_file)],
                       env=environment, check=True,
                       capture_output=True, text=True, errors="replace")

    return {"deployment": str(deployment), "prefix": str(prefix),
            "fonts": entries}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--deployment", type=Path, required=True,
                        help="the directory the executable is deployed in")
    parser.add_argument("--prefix", type=Path, required=True,
                        help="the Wine prefix to install into")
    arguments = parser.parse_args()

    import os
    environment = dict(os.environ)
    environment["WINEPREFIX"] = str(arguments.prefix.resolve())
    manifest = install(arguments.deployment.resolve(),
                       arguments.prefix.resolve(), environment)
    print(json.dumps(manifest, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
