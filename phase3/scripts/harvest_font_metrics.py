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

Only the tables layout depends on are read -- `head`, `hhea`, `OS/2`, `hmtx`,
`cmap`, and the pair kerning in `kern` and `GPOS`. Nothing here rasterises,
hints, or shapes.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

# The corpus's text is ASCII. Emitting a declared range rather than every
# codepoint the font covers keeps the output small enough to read, and makes the
# covered set a stated decision instead of a side effect of which font was
# harvested.
#
# An icon font is the other case and is not a range at all: what it is asked for
# is the set of glyphs Terminal's markup names, which
# `harvest_icon_glyphs.py` extracts from the checkout and `--codepoints-from`
# reads. An icon font is also allowed not to have one of them, which is what
# `--missing record` is for -- a Fluent glyph absent from MDL2 is the reason
# Terminal writes a fallback list, not a bad harvest.
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

    def optional(self, name: str) -> bytes | None:
        """A table a font is allowed not to have. See read_kerning."""
        return self.table(name) if name in self.tables else None


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


# --- kerning ------------------------------------------------------------------
#
# A pair of adjacent glyphs can move the first one's advance, and the corpus
# says the runtime honours it: "Terminal" in Segoe UI measures 200 design units
# narrower than its advances add up to, at every size recorded. So the pair
# adjustments are part of the metrics layout reads, and are harvested with them.
#
# Both of the places a font can keep them are read. `kern` is the older table
# and is what a font without OpenType layout uses; `GPOS` is where a modern one
# puts them, behind the `kern` feature. Only that feature is taken: a GPOS holds
# pair adjustments for other purposes -- capital spacing is one, and is off
# unless asked for -- and harvesting those would widen text nothing widens.


def read_coverage(table: bytes, offset: int) -> dict[int, int]:
    """Glyph id -> coverage index, for the glyphs a lookup applies to."""
    fmt = struct.unpack_from(">H", table, offset)[0]
    covered: dict[int, int] = {}
    if fmt == 1:
        count = struct.unpack_from(">H", table, offset + 2)[0]
        for index in range(count):
            glyph = struct.unpack_from(">H", table, offset + 4 + index * 2)[0]
            covered[glyph] = index
    elif fmt == 2:
        count = struct.unpack_from(">H", table, offset + 2)[0]
        for index in range(count):
            start, end, first = struct.unpack_from(">HHH", table, offset + 4 + index * 6)
            for step, glyph in enumerate(range(start, end + 1)):
                covered[glyph] = first + step
    else:
        raise FontError(f"unsupported coverage format {fmt}")
    return covered


def read_class_def(table: bytes, offset: int) -> dict[int, int]:
    """Glyph id -> class. Absent means class 0, which the caller relies on."""
    fmt = struct.unpack_from(">H", table, offset)[0]
    classes: dict[int, int] = {}
    if fmt == 1:
        start, count = struct.unpack_from(">HH", table, offset + 2)
        for index in range(count):
            classes[start + index] = struct.unpack_from(">H", table, offset + 6 + index * 2)[0]
    elif fmt == 2:
        count = struct.unpack_from(">H", table, offset + 2)[0]
        for index in range(count):
            start, end, klass = struct.unpack_from(">HHH", table, offset + 4 + index * 6)
            for glyph in range(start, end + 1):
                classes[glyph] = klass
    else:
        raise FontError(f"unsupported class definition format {fmt}")
    return classes


# A value record is a bitfield of optional int16s. Only XAdvance moves layout;
# the rest still have to be counted so the next record starts in the right
# place.
X_ADVANCE = 0x0004


def value_size(value_format: int) -> int:
    return 2 * bin(value_format & 0xFFFF).count("1")


def x_advance(table: bytes, offset: int, value_format: int) -> int:
    if not value_format & X_ADVANCE:
        return 0
    # The fields appear in bit order, so XAdvance sits past the lower ones.
    at = offset + 2 * bin(value_format & (X_ADVANCE - 1)).count("1")
    return struct.unpack_from(">h", table, at)[0]


def read_pair_pos(table: bytes, offset: int, pairs: dict[tuple[int, int], int]) -> None:
    fmt = struct.unpack_from(">H", table, offset)[0]
    if fmt == 1:
        coverage_at, first_format, second_format, set_count = struct.unpack_from(
            ">HHHH", table, offset + 2)
        covered = read_coverage(table, offset + coverage_at)
        by_index = {index: glyph for glyph, index in covered.items()}
        stride = 2 + value_size(first_format) + value_size(second_format)
        for index in range(set_count):
            first = by_index.get(index)
            if first is None:
                continue
            set_at = offset + struct.unpack_from(">H", table, offset + 10 + index * 2)[0]
            count = struct.unpack_from(">H", table, set_at)[0]
            for step in range(count):
                record = set_at + 2 + step * stride
                second = struct.unpack_from(">H", table, record)[0]
                value = x_advance(table, record + 2, first_format)
                if value:
                    pairs[(first, second)] = value
    elif fmt == 2:
        (coverage_at, first_format, second_format, first_def_at, second_def_at,
         first_count, second_count) = struct.unpack_from(">HHHHHHH", table, offset + 2)
        covered = read_coverage(table, offset + coverage_at)
        first_classes = read_class_def(table, offset + first_def_at)
        second_classes = read_class_def(table, offset + second_def_at)
        stride = value_size(first_format) + value_size(second_format)
        records_at = offset + 16
        # Every glyph the coverage names, against every glyph the second class
        # definition names. A class-based subtable says nothing about glyphs
        # outside those, which is what makes the caller's filtering enough.
        seconds: dict[int, int] = {glyph: klass for glyph, klass in second_classes.items()}
        for glyph in covered:
            first_class = first_classes.get(glyph, 0)
            if first_class >= first_count:
                continue
            for second_glyph, second_class in seconds.items():
                if second_class >= second_count:
                    continue
                record = records_at + (first_class * second_count + second_class) * stride
                value = x_advance(table, record, first_format)
                if value:
                    pairs[(glyph, second_glyph)] = value
    else:
        raise FontError(f"unsupported pair positioning format {fmt}")


def read_gpos_kerning(gpos: bytes) -> dict[tuple[int, int], int]:
    """Every pair adjustment the `kern` feature names, by glyph id."""
    _major, _minor, _script_at, feature_at, lookup_at = struct.unpack_from(">HHHHH", gpos, 0)

    wanted: set[int] = set()
    count = struct.unpack_from(">H", gpos, feature_at)[0]
    for index in range(count):
        tag, offset = struct.unpack_from(">4sH", gpos, feature_at + 2 + index * 6)
        if tag != b"kern":
            continue
        feature = feature_at + offset
        lookups = struct.unpack_from(">H", gpos, feature + 2)[0]
        for step in range(lookups):
            wanted.add(struct.unpack_from(">H", gpos, feature + 4 + step * 2)[0])

    pairs: dict[tuple[int, int], int] = {}
    lookup_count = struct.unpack_from(">H", gpos, lookup_at)[0]
    for index in sorted(wanted):
        if index >= lookup_count:
            raise FontError(f"the kern feature names lookup {index}, which does not exist")
        lookup = lookup_at + struct.unpack_from(">H", gpos, lookup_at + 2 + index * 2)[0]
        lookup_type, _flag, subtables = struct.unpack_from(">HHH", gpos, lookup)
        for step in range(subtables):
            at = lookup + struct.unpack_from(">H", gpos, lookup + 6 + step * 2)[0]
            if lookup_type == 9:
                # An extension lookup is an indirection a font reaches for once
                # its GPOS outgrows 16-bit offsets, which every real one has.
                _fmt, wrapped, delta = struct.unpack_from(">HHI", gpos, at)
                if wrapped != 2:
                    continue
                read_pair_pos(gpos, at + delta, pairs)
            elif lookup_type == 2:
                read_pair_pos(gpos, at, pairs)
    return pairs


def read_kern_table(kern: bytes) -> dict[tuple[int, int], int]:
    """Every pair in the legacy `kern` table's format 0 subtables."""
    pairs: dict[tuple[int, int], int] = {}
    version, count = struct.unpack_from(">HH", kern, 0)
    if version != 0:
        # Version 1 is Apple's, with a different header and a different
        # coverage word. No font this project harvests uses it.
        return pairs
    at = 4
    for _ in range(count):
        _subversion, length, coverage_word = struct.unpack_from(">HHH", kern, at)
        horizontal = coverage_word & 0x0001
        minimum = coverage_word & 0x0002
        cross_stream = coverage_word & 0x0004
        fmt = coverage_word >> 8
        if fmt == 0 and horizontal and not minimum and not cross_stream:
            pair_count = struct.unpack_from(">H", kern, at + 6)[0]
            for index in range(pair_count):
                left, right, value = struct.unpack_from(">HHh", kern, at + 14 + index * 6)
                if value:
                    pairs[(left, right)] = value
        if not length:
            break
        at += length
    return pairs


def read_kerning(font: Font, glyphs: dict[int, int]) -> dict[tuple[int, int], int]:
    """Codepoint pair -> what it adds to the first glyph's advance.

    Restricted to the codepoints that were asked for: a font kerns thousands of
    pairs and the file stays reviewable only if it carries the ones the corpus
    can reach. A pair reaching a glyph outside the set is a fact about the font
    and not about anything this repository measures.
    """
    by_glyph: dict[tuple[int, int], int] = {}
    gpos = font.optional("GPOS")
    if gpos:
        by_glyph.update(read_gpos_kerning(gpos))
    kern = font.optional("kern")
    if kern:
        # GPOS wins where both carry a pair: a font shipping both means the
        # older table for shapers that cannot read the newer one.
        for pair, value in read_kern_table(kern).items():
            by_glyph.setdefault(pair, value)

    wanted = {glyph: codepoint for codepoint, glyph in glyphs.items()}
    return {(wanted[left], wanted[right]): value
            for (left, right), value in by_glyph.items()
            if left in wanted and right in wanted and value}


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
    kerning = read_kerning(font, mapping)

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
        # Keyed by the two decimal codepoints the adjustment sits between, so
        # the block sorts and diffs the way the advances do. An empty one is a
        # font that kerns nothing among the codepoints asked for -- which every
        # icon font is, and which is a reading rather than a gap.
        "kerning": {f"{left},{right}": value
                    for (left, right), value in sorted(kerning.items())},
        # Requested and not covered. For a text font this list has to be empty,
        # and main() still fails when it is not; for an icon font it is the
        # answer to "which of Terminal's glyphs does this family actually have",
        # which is exactly what decides whether its fallback list is doing work.
        "missing": [cp for cp in sorted(set(codepoints)) if cp not in mapping],
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
    parser.add_argument("--codepoints-from", type=Path, default=None,
                        help="a JSON file with a 'codepoints' list of decimal "
                             "integers, as harvest_icon_glyphs.py writes")
    parser.add_argument("--missing", choices=("fail", "record"), default="fail",
                        help="what to do about a requested codepoint the font "
                             "does not cover. 'fail' is right for a text font, "
                             "where an absent ASCII glyph means the wrong file "
                             "was read; 'record' is right for an icon font, "
                             "where not covering a glyph is a fact about the "
                             "font and is what a fallback list exists for")
    parser.add_argument("--expect", type=Path, default=None,
                        help="metrics solved from the oracle; fail if the font disagrees")
    args = parser.parse_args()

    if args.codepoints and args.codepoints_from:
        raise SystemExit("--codepoints and --codepoints-from name two different "
                         "sets; pass one")
    if args.codepoints_from:
        requested = json.loads(args.codepoints_from.read_text(encoding="utf-8"))
        codepoints = sorted({int(cp) for cp in requested["codepoints"]})
        if not codepoints:
            raise SystemExit(f"{args.codepoints_from}: names no codepoints")
    elif args.codepoints:
        low, _, high = args.codepoints.partition("-")
        codepoints = list(range(int(low, 16), int(high or low, 16) + 1))
    else:
        codepoints = list(DEFAULT_CODEPOINTS)

    metrics = harvest(args.font, args.family, codepoints)
    missing = metrics["missing"]
    if missing and args.missing == "fail":
        raise SystemExit(
            f"{args.font}: no glyph for {len(missing)} requested codepoints, "
            f"first is U+{missing[0]:04X}")
    if not metrics["advances"]:
        # Every requested codepoint absent is not a font with a gap, it is the
        # wrong file: a metrics file with no advances would load happily and
        # then fail every case that read it, by the wrong name.
        raise SystemExit(
            f"{args.font}: covers none of the {len(codepoints)} requested "
            f"codepoints, so it is not the font this family names")

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
          f"{len(missing)} requested codepoints not covered, "
          f"upem {metrics['units_per_em']}, "
          f"hhea {metrics['hhea']['ascender']}/{metrics['hhea']['descender']}",
          file=sys.stderr)


if __name__ == "__main__":
    main()
