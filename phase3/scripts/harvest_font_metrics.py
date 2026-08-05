#!/usr/bin/env python3
"""Read a TrueType font's layout metrics and emit them as reviewable JSON.

Text measurement needs numbers that live inside a font file: how tall a line
is, and how wide each character is. The font itself cannot be committed -- it
is a binary, and Segoe UI is not ours to redistribute -- but the handful of
integers layout actually reads are text, and text is reviewable.

So the font stays on the machine that has it (the Windows runner, which is
also where the oracle runs) and only its metrics travel. That keeps the same
property the rest of phase 3 has: what is committed is a measurement, pinned
to the exact artefact it came from, and a servicing update shows up as a diff.

Only the tables layout depends on are read -- `head`, `hhea`, `OS/2`, `hmtx`
and `cmap`. Nothing here rasterises, hints, or shapes.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

# The corpus is ASCII. Emitting a declared range rather than every codepoint
# the font covers keeps the output small enough to read, and makes the covered
# set a stated decision instead of a side effect of which font was harvested.
DEFAULT_CODEPOINTS = range(0x20, 0x7F)


class FontError(Exception):
    pass


class Font:
    """Just enough of the TrueType container to reach five tables."""

    def __init__(self, data: bytes):
        self.data = data
        self.tables: dict[str, tuple[int, int]] = {}

        if len(data) < 12:
            raise FontError("not a font: too short for a table directory")
        tag = data[:4]
        if tag == b"ttcf":
            # A collection puts the real directories elsewhere; take the first
            # face rather than guessing which one was meant.
            if len(data) < 16:
                raise FontError("truncated TrueType collection header")
            offset = struct.unpack_from(">I", data, 12)[0]
        elif tag in (b"\x00\x01\x00\x00", b"true", b"OTTO"):
            offset = 0
        else:
            raise FontError(f"unrecognised sfnt tag {tag!r}")

        count = struct.unpack_from(">H", data, offset + 4)[0]
        for index in range(count):
            entry = offset + 12 + index * 16
            if entry + 16 > len(data):
                raise FontError("table directory runs past the end of the file")
            name, _checksum, start, length = struct.unpack_from(">4sIII", data, entry)
            self.tables[name.decode("latin-1")] = (start, length)

    def table(self, name: str) -> bytes:
        if name not in self.tables:
            raise FontError(f"the font has no {name!r} table")
        start, length = self.tables[name]
        if start + length > len(self.data):
            raise FontError(f"the {name!r} table runs past the end of the file")
        return self.data[start:start + length]


def read_cmap(font: Font, codepoints: list[int]) -> dict[int, int]:
    """Codepoint -> glyph id, through the best Unicode subtable available.

    Format 4 is what a Windows font uses for the BMP; format 12 appears when
    the font also covers astral planes. Both are read because picking one and
    hoping is how a harvester silently returns glyph 0 for everything.
    """
    table = font.table("cmap")
    count = struct.unpack_from(">H", table, 2)[0]

    best: tuple[int, int] | None = None  # (rank, offset)
    for index in range(count):
        platform, encoding, offset = struct.unpack_from(">HHI", table, 4 + index * 8)
        # Windows/UCS-4, Windows/BMP, then Unicode-platform, in that order.
        rank = {(3, 10): 3, (3, 1): 2, (0, 4): 1, (0, 3): 1, (0, 6): 1}.get(
            (platform, encoding), 0)
        if rank and (best is None or rank > best[0]):
            best = (rank, offset)
    if best is None:
        raise FontError("the cmap has no Unicode subtable")

    subtable = table[best[1]:]
    fmt = struct.unpack_from(">H", subtable, 0)[0]
    mapping: dict[int, int] = {}

    if fmt == 4:
        segments = struct.unpack_from(">H", subtable, 6)[0] // 2
        ends = struct.unpack_from(f">{segments}H", subtable, 14)
        starts_at = 14 + segments * 2 + 2
        starts = struct.unpack_from(f">{segments}H", subtable, starts_at)
        deltas = struct.unpack_from(f">{segments}h", subtable, starts_at + segments * 2)
        ranges_at = starts_at + segments * 4
        ranges = struct.unpack_from(f">{segments}H", subtable, ranges_at)
        for codepoint in codepoints:
            if codepoint > 0xFFFF:
                continue
            for segment in range(segments):
                if starts[segment] <= codepoint <= ends[segment]:
                    if ranges[segment] == 0:
                        glyph = (codepoint + deltas[segment]) & 0xFFFF
                    else:
                        at = (ranges_at + segment * 2 + ranges[segment]
                              + (codepoint - starts[segment]) * 2)
                        glyph = struct.unpack_from(">H", subtable, at)[0]
                        if glyph:
                            glyph = (glyph + deltas[segment]) & 0xFFFF
                    if glyph:
                        mapping[codepoint] = glyph
                    break
    elif fmt == 12:
        groups = struct.unpack_from(">I", subtable, 12)[0]
        spans = [struct.unpack_from(">III", subtable, 16 + i * 12) for i in range(groups)]
        for codepoint in codepoints:
            for start, end, glyph in spans:
                if start <= codepoint <= end:
                    mapping[codepoint] = glyph + (codepoint - start)
                    break
    else:
        raise FontError(f"unsupported cmap subtable format {fmt}")

    return mapping


def read_advances(font: Font, glyphs: set[int]) -> dict[int, int]:
    """Glyph id -> advance width, in design units.

    `hmtx` stores a run of paired metrics followed by advance-less entries for
    the monospaced tail, so a glyph past the run repeats the last advance in
    it rather than being absent.
    """
    hhea = font.table("hhea")
    long_metrics = struct.unpack_from(">H", hhea, 34)[0]
    if long_metrics == 0:
        raise FontError("hhea claims zero horizontal metrics")
    hmtx = font.table("hmtx")

    advances: dict[int, int] = {}
    for glyph in sorted(glyphs):
        index = min(glyph, long_metrics - 1)
        at = index * 4
        if at + 2 > len(hmtx):
            raise FontError(f"hmtx has no entry for glyph {glyph}")
        advances[glyph] = struct.unpack_from(">H", hmtx, at)[0]
    return advances


def harvest(path: Path, family: str, codepoints: list[int]) -> dict:
    data = path.read_bytes()
    font = Font(data)

    head = font.table("head")
    units_per_em = struct.unpack_from(">H", head, 18)[0]
    if not units_per_em:
        raise FontError("head claims zero units per em")

    hhea = font.table("hhea")
    ascender, descender, line_gap = struct.unpack_from(">hhh", hhea, 4)

    # OS/2 carries a second, sometimes different, opinion about vertical
    # extent. It is recorded but not chosen between here: which one a text
    # stack uses is a question for the implementation, and having both in the
    # file means answering it does not need another harvest.
    os2 = font.table("OS/2")
    typo_ascender, typo_descender, typo_line_gap = struct.unpack_from(">hhh", os2, 68)
    win_ascent, win_descent = struct.unpack_from(">HH", os2, 74)

    mapping = read_cmap(font, codepoints)
    advances = read_advances(font, set(mapping.values()))

    return {
        "schema_version": 1,
        "family": family,
        # Read out of the font itself, as opposed to the file
        # derive_font_metrics.py solves out of the recorded measurements. The
        # two are not interchangeable and the consumer is told which it has.
        "provenance": "harvested",
        "source": {
            "file": path.name,
            "sha256": hashlib.sha256(data).hexdigest(),
        },
        "units_per_em": units_per_em,
        "hhea": {
            "ascender": ascender,
            "descender": descender,
            "line_gap": line_gap,
        },
        "os2": {
            "typo_ascender": typo_ascender,
            "typo_descender": typo_descender,
            "typo_line_gap": typo_line_gap,
            "win_ascent": win_ascent,
            "win_descent": win_descent,
        },
        # Keyed by decimal codepoint, because JSON object keys are strings and
        # a decimal one sorts and diffs predictably.
        "advances": {str(cp): advances[glyph] for cp, glyph in sorted(mapping.items())},
    }


def line_spacing(metrics: dict) -> int:
    """Baseline to baseline, in design units."""
    hhea = metrics["hhea"]
    return hhea["ascender"] - hhea["descender"] + hhea["line_gap"]


def check_against(metrics: dict, expected: dict) -> list[str]:
    """Compare a harvest against numbers solved out of the recorded oracle.

    This is what makes the text measurement model falsifiable. The advance for
    'M' and the baseline-to-baseline distance were derived by asking which
    values could produce the measurements the runtime recorded; reading the
    same numbers out of the font is an independent route to them. If the two
    disagree, the model in layout/src/text.cpp is wrong -- not the font.
    """
    problems: list[str] = []
    if metrics["units_per_em"] != expected["units_per_em"]:
        problems.append(f"units per em: font says {metrics['units_per_em']}, "
                        f"the oracle implies {expected['units_per_em']}")
    if line_spacing(metrics) != line_spacing(expected):
        problems.append(f"baseline to baseline: font says {line_spacing(metrics)}, "
                        f"the oracle implies {line_spacing(expected)}")
    for codepoint, advance in sorted(expected["advances"].items(), key=lambda kv: int(kv[0])):
        found = metrics["advances"].get(codepoint)
        if found != advance:
            problems.append(f"advance for U+{int(codepoint):04X}: font says {found}, "
                            f"the oracle implies {advance}")
    return problems


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("font", type=Path)
    parser.add_argument("--family", required=True,
                        help="the FontFamily name this file answers to")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--codepoints", default=None,
                        help="inclusive hex range, e.g. 20-7E (default: 20-7E)")
    parser.add_argument("--expect", type=Path, default=None,
                        help="metrics solved from the oracle; fail if the font disagrees")
    args = parser.parse_args()

    if args.codepoints:
        low, _, high = args.codepoints.partition("-")
        codepoints = list(range(int(low, 16), int(high or low, 16) + 1))
    else:
        codepoints = list(DEFAULT_CODEPOINTS)

    metrics = harvest(args.font, args.family, codepoints)
    missing = [cp for cp in codepoints if str(cp) not in metrics["advances"]]
    if missing:
        raise SystemExit(
            f"{args.font}: no glyph for {len(missing)} requested codepoints, "
            f"first is U+{missing[0]:04X}")

    if args.expect:
        expected = json.loads(args.expect.read_text(encoding="utf-8"))
        if expected["family"] == args.family:
            problems = check_against(metrics, expected)
            if problems:
                raise SystemExit(
                    f"{args.font} disagrees with what the recorded measurements imply:\n  "
                    + "\n  ".join(problems))
            print(f"{args.family}: agrees with {args.expect.name} on "
                  f"{len(expected['advances']) + 2} derived values", file=sys.stderr)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    # Trailing newline and sorted keys so the file is stable across runs and
    # reviewable as a diff.
    args.output.write_text(json.dumps(metrics, indent=1, sort_keys=True) + "\n",
                           encoding="utf-8")
    print(f"{args.family}: {len(metrics['advances'])} advances, "
          f"upem {metrics['units_per_em']}, "
          f"hhea {metrics['hhea']['ascender']}/{metrics['hhea']['descender']}",
          file=sys.stderr)


if __name__ == "__main__":
    main()
