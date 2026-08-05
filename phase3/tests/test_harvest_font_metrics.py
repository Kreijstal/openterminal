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


def build_font(*, units_per_em: int, ascender: int, descender: int, line_gap: int,
               advances: list[int], ranges: list[tuple[int, int, int]],
               long_metrics: int | None = None) -> bytes:
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
