#!/usr/bin/env python3
"""The font metrics harvester, against a font built here byte by byte.

Synthesising the font rather than reading one off the machine means the test
knows the right answer independently, and runs the same way on a runner with
no fonts installed as on a developer's box.
"""

from __future__ import annotations

import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT_DIRECTORY = Path(__file__).resolve().parents[1] / "scripts"
sys.path.insert(0, str(SCRIPT_DIRECTORY))

from harvest_font_metrics import (  # noqa: E402
    Font, FontError, check_against, harvest, line_spacing,
)

# The numbers the corpus can answer on its own, solved by
# derive_font_metrics.py and committed. The harvest is checked against them.
DERIVED = (Path(__file__).resolve().parents[1]
           / "xaml-db" / "fonts" / "derived" / "segoe-ui.json")


def cmap_format4(ranges: list[tuple[int, int, int]]) -> bytes:
    """A format 4 subtable mapping each (first, last, first_glyph) span."""
    segments = [(first, last, (glyph - first) & 0xFFFF) for first, last, glyph in ranges]
    segments.append((0xFFFF, 0xFFFF, 1))
    count = len(segments)

    body = struct.pack(">HHH", 4, 0, 0)  # format, length (patched), language
    body += struct.pack(">HHHH", count * 2, 0, 0, 0)
    body += struct.pack(f">{count}H", *[last for _, last, _ in segments])
    body += struct.pack(">H", 0)  # reservedPad
    body += struct.pack(f">{count}H", *[first for first, _, _ in segments])
    body += struct.pack(f">{count}H", *[delta for _, _, delta in segments])
    body += struct.pack(f">{count}H", *([0] * count))  # idRangeOffset
    return body[:2] + struct.pack(">H", len(body)) + body[4:]


def kern_table(pairs: dict[tuple[int, int], int]) -> bytes:
    """A `kern` table, version 0, one format 0 subtable."""
    body = struct.pack(">HHHH", len(pairs), 0, 0, 0)  # nPairs, and the unused search hints
    for (left, right), value in sorted(pairs.items()):
        body += struct.pack(">HHh", left, right, value)
    subtable = struct.pack(">HHH", 0, 6 + len(body), 0x0001) + body  # horizontal, format 0
    return struct.pack(">HH", 0, 1) + subtable


def coverage(glyphs: list[int]) -> bytes:
    """Coverage format 1: the glyphs a lookup applies to, in order."""
    return struct.pack(">HH", 1, len(glyphs)) + struct.pack(f">{len(glyphs)}H", *glyphs)


def class_def(assignments: dict[int, int]) -> bytes:
    """ClassDef format 2, one range per glyph, which needs no contiguity."""
    ranges = sorted(assignments.items())
    body = struct.pack(">HH", 2, len(ranges))
    for glyph, klass in ranges:
        body += struct.pack(">HHH", glyph, glyph, klass)
    return body


def pair_pos_format1(pairs: dict[tuple[int, int], int]) -> bytes:
    """PairPos format 1: a pair set per first glyph, XAdvance only."""
    firsts = sorted({left for left, _ in pairs})
    sets = []
    for first in firsts:
        seconds = sorted((right, value) for (left, right), value in pairs.items()
                         if left == first)
        body = struct.pack(">H", len(seconds))
        for right, value in seconds:
            body += struct.pack(">Hh", right, value)
        sets.append(body)

    offset = 10 + len(firsts) * 2
    offsets = b""
    for body in sets:
        offsets += struct.pack(">H", offset)
        offset += len(body)
    # Coverage goes after the pair sets, so its offset is where they end.
    header = struct.pack(">HHHHH", 1, offset, 0x0004, 0, len(firsts))
    return header + offsets + b"".join(sets) + coverage(firsts)


def pair_pos_format2(classes1: dict[int, int], classes2: dict[int, int],
                     values: dict[tuple[int, int], int],
                     class1_count: int, class2_count: int) -> bytes:
    """PairPos format 2: a value per pair of glyph classes."""
    records = b""
    for first in range(class1_count):
        for second in range(class2_count):
            records += struct.pack(">h", values.get((first, second), 0))
    header = struct.pack(">HHHHHHHH", 2, 0, 0x0004, 0, 0, 0, class1_count, class2_count)
    covered = coverage(sorted(classes1))
    first_def = class_def(classes1)
    second_def = class_def(classes2)

    coverage_at = len(header) + len(records)
    first_at = coverage_at + len(covered)
    second_at = first_at + len(first_def)
    header = struct.pack(">HHHHHHHH", 2, coverage_at, 0x0004, 0,
                         first_at, second_at, class1_count, class2_count)
    return header + records + covered + first_def + second_def


def gpos_table(lookups: list[tuple[int, bytes]], *, feature: str = "kern") -> bytes:
    """A GPOS carrying one feature and the lookups it names."""
    script_list = struct.pack(">H", 0)

    feature_table = struct.pack(">HH", 0, len(lookups))
    feature_table += struct.pack(f">{len(lookups)}H", *range(len(lookups)))
    feature_list = struct.pack(">H", 1) + struct.pack(
        ">4sH", feature.encode("latin-1"), 2 + 6)
    feature_list += feature_table

    tables = []
    for lookup_type, subtable in lookups:
        body = struct.pack(">HHH", lookup_type, 0, 1) + struct.pack(">H", 8)
        tables.append(body + subtable)
    lookup_list = struct.pack(">H", len(tables))
    offset = 2 + len(tables) * 2
    for table in tables:
        lookup_list += struct.pack(">H", offset)
        offset += len(table)
    lookup_list += b"".join(tables)

    header_size = 10
    script_at = header_size
    feature_at = script_at + len(script_list)
    lookup_at = feature_at + len(feature_list)
    return (struct.pack(">HHHHH", 1, 0, script_at, feature_at, lookup_at)
            + script_list + feature_list + lookup_list)


def extension(lookup_type: int, subtable: bytes) -> bytes:
    """A type 9 lookup wrapping another one, as a large font does."""
    return struct.pack(">HHI", 1, lookup_type, 8) + subtable


def build_font(*, units_per_em: int, ascender: int, descender: int, line_gap: int,
               advances: list[int], ranges: list[tuple[int, int, int]],
               long_metrics: int | None = None,
               extra: dict[str, bytes] | None = None) -> bytes:
    """A minimal sfnt carrying only the five tables the harvester reads."""
    long_metrics = len(advances) if long_metrics is None else long_metrics

    head = bytearray(54)
    struct.pack_into(">H", head, 18, units_per_em)

    hhea = bytearray(36)
    struct.pack_into(">hhh", hhea, 4, ascender, descender, line_gap)
    struct.pack_into(">H", hhea, 34, long_metrics)

    os2 = bytearray(78)
    struct.pack_into(">hhh", os2, 68, 700, -300, 90)
    struct.pack_into(">HH", os2, 74, 1000, 200)

    hmtx = b"".join(struct.pack(">Hh", advance, 0) for advance in advances)

    subtable = cmap_format4(ranges)
    cmap = struct.pack(">HH", 0, 1) + struct.pack(">HHI", 3, 1, 12) + subtable

    tables = {"OS/2": bytes(os2), "cmap": cmap, "head": bytes(head),
              "hhea": bytes(hhea), "hmtx": hmtx}
    tables.update(extra or {})

    offset = 12 + len(tables) * 16
    directory = b""
    payload = b""
    for tag, content in sorted(tables.items()):
        directory += struct.pack(">4sIII", tag.encode("latin-1"), 0,
                                 offset + len(payload), len(content))
        padded = content + b"\0" * (-len(content) % 4)
        payload += padded
    return struct.pack(">IHHHH", 0x00010000, len(tables), 0, 0, 0) + directory + payload


class HarvestFontMetricsTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)

    def write(self, data: bytes) -> Path:
        path = self.root / "synthetic.ttf"
        path.write_bytes(data)
        return path

    def test_reads_the_metrics_it_was_given(self) -> None:
        # 'A'..'C' map to glyphs 1..3, whose advances are 1111, 2222, 3333.
        path = self.write(build_font(
            units_per_em=2048, ascender=2210, descender=-514, line_gap=0,
            advances=[500, 1111, 2222, 3333], ranges=[(0x41, 0x43, 1)]))

        metrics = harvest(path, "Synthetic", [0x41, 0x42, 0x43])
        self.assertEqual(metrics["units_per_em"], 2048)
        self.assertEqual(metrics["hhea"],
                         {"ascender": 2210, "descender": -514, "line_gap": 0})
        self.assertEqual(metrics["advances"], {"65": 1111, "66": 2222, "67": 3333})
        self.assertEqual(metrics["os2"]["typo_ascender"], 700)
        self.assertEqual(metrics["os2"]["win_descent"], 200)
        self.assertEqual(line_spacing(metrics), 2724)

    def test_records_the_exact_file_it_read(self) -> None:
        data = build_font(units_per_em=1000, ascender=800, descender=-200, line_gap=0,
                          advances=[500, 600], ranges=[(0x41, 0x41, 1)])
        metrics = harvest(self.write(data), "Synthetic", [0x41])
        import hashlib
        self.assertEqual(metrics["source"]["sha256"], hashlib.sha256(data).hexdigest())

    def test_a_glyph_past_the_metric_run_repeats_the_last_advance(self) -> None:
        # hmtx stores two paired entries; glyph 3 falls in the monospaced tail
        # and takes the advance of the last pair rather than being absent.
        path = self.write(build_font(
            units_per_em=1000, ascender=800, descender=-200, line_gap=0,
            advances=[500, 600], ranges=[(0x41, 0x42, 2)], long_metrics=2))
        metrics = harvest(path, "Synthetic", [0x41, 0x42])
        self.assertEqual(metrics["advances"], {"65": 600, "66": 600})

    def test_rejects_something_that_is_not_a_font(self) -> None:
        with self.assertRaises(FontError):
            Font(b"this is not a font at all, not even close")

    def test_a_codepoint_the_font_does_not_cover_is_recorded(self) -> None:
        # An icon font is allowed not to have a glyph -- Segoe Fluent Icons and
        # Segoe MDL2 Assets do not cover the same set, which is why Terminal
        # writes them as a fallback list. The harvest has to say which ones are
        # absent rather than pretend the request was satisfied.
        path = self.write(build_font(
            units_per_em=2048, ascender=2210, descender=-514, line_gap=0,
            advances=[500, 1111], ranges=[(0xE710, 0xE710, 1)]))
        metrics = harvest(path, "Synthetic Icons", [0xE710, 0xE74D, 0xF5B0])
        self.assertEqual(metrics["advances"], {"59152": 1111})
        self.assertEqual(metrics["missing"], [0xE74D, 0xF5B0])

    def test_a_covered_set_records_nothing_missing(self) -> None:
        path = self.write(build_font(
            units_per_em=1000, ascender=800, descender=-200, line_gap=0,
            advances=[500, 600], ranges=[(0x41, 0x41, 1)]))
        self.assertEqual(harvest(path, "Synthetic", [0x41])["missing"], [])

    def kerning(self, **tables: bytes) -> dict[str, dict[str, int]]:
        path = self.write(build_font(
            units_per_em=1000, ascender=800, descender=-200, line_gap=0,
            advances=[500, 600, 700], ranges=[(0x41, 0x42, 1)], extra=tables))
        return harvest(path, "Synthetic", [0x41, 0x42])["font_kerning"]

    def test_a_font_with_no_kerning_at_all_says_so(self) -> None:
        # Neither table is required, and an absent one is a font that kerns
        # nothing rather than a harvest that failed to look.
        self.assertEqual(self.kerning(), {"gpos": {}, "kern": {}})

    def test_the_two_tables_are_recorded_apart(self) -> None:
        # The reason the split exists. The first run to read a real font found
        # the runtime applying some of what it says and not the rest, and which
        # table a pair came from is the first thing anyone will want to check.
        # Merging them would have thrown that away before it could be asked.
        found = self.kerning(kern=kern_table({(1, 2): -75}),
                             GPOS=gpos_table([(2, pair_pos_format1({(2, 1): -200}))]))
        self.assertEqual(found, {"gpos": {"66,65": -200}, "kern": {"65,66": -75}})

    def test_a_pair_in_both_tables_is_reported_in_both(self) -> None:
        found = self.kerning(kern=kern_table({(1, 2): -75}),
                             GPOS=gpos_table([(2, pair_pos_format1({(1, 2): -75}))]))
        self.assertEqual(found, {"gpos": {"65,66": -75}, "kern": {"65,66": -75}})

    def test_reads_the_legacy_kern_table(self) -> None:
        # 'A' and 'B' are glyphs 1 and 2; the pair moves by -75.
        self.assertEqual(self.kerning(kern=kern_table({(1, 2): -75}))["kern"],
                         {"65,66": -75})

    def test_reads_a_gpos_pair_adjustment(self) -> None:
        found = self.kerning(GPOS=gpos_table([(2, pair_pos_format1({(1, 2): -200}))]))
        self.assertEqual(found["gpos"], {"65,66": -200})

    def test_reads_a_class_based_pair_adjustment(self) -> None:
        # Format 2 is how a real font stores most of its kerning: a value per
        # pair of classes rather than per pair of glyphs.
        subtable = pair_pos_format2({1: 1}, {2: 1}, {(1, 1): -120}, 2, 2)
        self.assertEqual(self.kerning(GPOS=gpos_table([(2, subtable)]))["gpos"],
                         {"65,66": -120})

    def test_reads_through_an_extension_lookup(self) -> None:
        # A font whose GPOS is over 64k puts its lookups behind type 9, which
        # is every font this project actually harvests.
        wrapped = extension(2, pair_pos_format1({(1, 2): -40}))
        self.assertEqual(self.kerning(GPOS=gpos_table([(9, wrapped)]))["gpos"],
                         {"65,66": -40})

    def test_only_the_kern_feature_is_read(self) -> None:
        # A GPOS holds many features. Capital spacing is a pair adjustment too
        # and is off by default, so taking every PairPos in the table would
        # record spacing the kern feature never asked for.
        found = self.kerning(GPOS=gpos_table(
            [(2, pair_pos_format1({(1, 2): -200}))], feature="cpsp"))
        self.assertEqual(found["gpos"], {})

    def test_a_pair_outside_the_harvested_set_is_not_recorded(self) -> None:
        # The harvest asks for the codepoints the corpus uses. A pair reaching
        # a glyph outside that set is a fact about the font and not about
        # anything this repository measures, so it stays out of the file.
        self.assertEqual(self.kerning(kern=kern_table({(1, 2): -75, (2, 3): -10}))["kern"],
                         {"65,66": -75})

    def test_a_zero_adjustment_is_not_a_pair(self) -> None:
        self.assertEqual(self.kerning(kern=kern_table({(1, 2): 0}))["kern"], {})

    def test_the_harvest_never_claims_which_pairs_apply(self) -> None:
        # "kerning" is what the layout core reads and it is solved from the
        # measurements, so a harvest must not write it. fonts.cpp refuses a
        # harvested file that does; this is the other end of the same rule.
        metrics = harvest(self.write(build_font(
            units_per_em=1000, ascender=800, descender=-200, line_gap=0,
            advances=[500, 600, 700], ranges=[(0x41, 0x42, 1)],
            extra={"kern": kern_table({(1, 2): -75})})), "Synthetic", [0x41, 0x42])
        self.assertNotIn("kerning", metrics)

    def test_reports_a_missing_table(self) -> None:
        data = build_font(units_per_em=1000, ascender=800, descender=-200, line_gap=0,
                          advances=[500], ranges=[(0x41, 0x41, 1)])
        # Blank out the cmap tag in the directory; the table is still there but
        # nothing points at it any more.
        broken = data.replace(b"cmap", b"XXXX", 1)
        with self.assertRaisesRegex(FontError, "cmap"):
            harvest(self.write(broken), "Synthetic", [0x41])


class FontModelCheckTest(unittest.TestCase):
    """The cross-check that keeps text.cpp's measurement model falsifiable."""

    def setUp(self) -> None:
        self.expected = json.loads(DERIVED.read_text(encoding="utf-8"))

    def test_the_derived_fixture_says_what_text_cpp_assumes(self) -> None:
        # If either of these moves, the rules documented in text.cpp and the
        # numbers the layout core is checked against have parted company.
        self.assertEqual(self.expected["units_per_em"], 2048)
        self.assertEqual(line_spacing(self.expected), 2724)
        self.assertEqual(self.expected["advances"]["77"], 1839)

    def test_agreeing_metrics_pass(self) -> None:
        agreeing = {
            "units_per_em": 2048,
            "hhea": {"ascender": 2210, "descender": -514, "line_gap": 0},
            "advances": {"77": 1839, "78": 1500},
        }
        self.assertEqual(check_against(agreeing, self.expected), [])

    def test_a_different_font_is_caught(self) -> None:
        # Liberation Sans' actual numbers: metrically unlike Segoe UI, which is
        # what the check exists to notice.
        wrong = {
            "units_per_em": 2048,
            "hhea": {"ascender": 1854, "descender": -434, "line_gap": 67},
            "advances": {"77": 1706},
        }
        problems = check_against(wrong, self.expected)
        self.assertEqual(len(problems), 2)
        self.assertTrue(any("baseline to baseline" in p for p in problems))
        self.assertTrue(any("U+004D" in p for p in problems))

    def test_a_missing_codepoint_is_caught(self) -> None:
        problems = check_against(
            {"units_per_em": 2048,
             "hhea": {"ascender": 2724, "descender": 0, "line_gap": 0},
             "advances": {}},
            self.expected)
        self.assertEqual(len(problems), 1)
        self.assertIn("U+004D", problems[0])


if __name__ == "__main__":
    unittest.main()
