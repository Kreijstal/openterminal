#!/usr/bin/env python3
"""Finding the font metrics in a downloaded artifact, and checking them.

Nothing here talks to GitHub. What is under test is the part that runs after
the download: which directory in the artifact holds the metrics, and whether
what arrived agrees with the numbers the corpus solved for itself. A harvest
that quietly disagrees with the recorded measurements is the failure worth
catching, because it turns into pixel widths that are slightly off everywhere
rather than into an error.
"""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT_DIRECTORY = Path(__file__).resolve().parents[1] / "scripts"
sys.path.insert(0, str(SCRIPT_DIRECTORY))

from fetch_measurements import fonts_directory, verify_fonts  # noqa: E402

DERIVED = (Path(__file__).resolve().parents[1]
           / "xaml-db" / "fonts" / "derived" / "segoe-ui.json")

# Segoe UI's, as the harvester would read them out of the font: the same
# baseline-to-baseline distance the corpus derives, split the way a real hhea
# splits it.
SEGOE_UI = {
    "schema_version": 1,
    "family": "Segoe UI",
    "provenance": "harvested",
    "units_per_em": 2048,
    "hhea": {"ascender": 2210, "descender": -514, "line_gap": 0},
    "advances": {"77": 1839, "84": 1169},
}


class FontsDirectoryTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        (self.root / "xaml-measurements-10.0.26100.33158").mkdir()

    def test_finds_the_one_font_artifact(self) -> None:
        wanted = self.root / "xaml-fonts-10.0.26100.33158"
        wanted.mkdir()
        self.assertEqual(fonts_directory(self.root), wanted)

    def test_a_run_without_one_is_not_an_error(self) -> None:
        self.assertIsNone(fonts_directory(self.root))

    def test_two_font_artifacts_are_refused(self) -> None:
        (self.root / "xaml-fonts-10.0.26100.33158").mkdir()
        (self.root / "xaml-fonts-10.0.26100.99999").mkdir()
        with self.assertRaises(SystemExit):
            fonts_directory(self.root)


class VerifyFontsTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.fonts = Path(self.tmp.name) / "fonts"
        self.fonts.mkdir()

    def write(self, name: str, metrics: dict) -> None:
        (self.fonts / name).write_text(json.dumps(metrics), encoding="utf-8")

    def test_a_harvest_that_agrees_passes(self) -> None:
        self.write("segoe-ui.json", SEGOE_UI)
        self.assertEqual(verify_fonts(self.fonts, DERIVED.parent), [])

    def test_a_harvest_of_the_wrong_font_is_caught(self) -> None:
        # Liberation Sans' numbers under Segoe UI's name: metrically unlike it,
        # which is what the cross-check exists to notice.
        wrong = dict(SEGOE_UI, hhea={"ascender": 1854, "descender": -434, "line_gap": 67},
                     advances={"77": 1706})
        self.write("segoe-ui.json", wrong)
        problems = verify_fonts(self.fonts, DERIVED.parent)
        self.assertTrue(any("baseline to baseline" in p for p in problems))
        self.assertTrue(all("Segoe UI" in p for p in problems))

    def test_an_empty_artifact_is_caught(self) -> None:
        problems = verify_fonts(self.fonts, DERIVED.parent)
        self.assertEqual(len(problems), 1)
        self.assertIn("no font metrics", problems[0])

    def test_a_derived_file_in_the_harvest_artifact_is_caught(self) -> None:
        # The artifact is supposed to be readings taken off the font. A derived
        # file arriving in it would pass every cross-check trivially, being the
        # thing the cross-check compares against.
        self.write("segoe-ui.json", dict(SEGOE_UI, provenance="derived"))
        problems = verify_fonts(self.fonts, DERIVED.parent)
        self.assertEqual(len(problems), 1)
        self.assertIn("provenance", problems[0])

    def test_a_family_nothing_can_check_is_said_out_loud(self) -> None:
        self.write("consolas.json", dict(SEGOE_UI, family="Consolas"))
        problems = verify_fonts(self.fonts, DERIVED.parent)
        self.assertEqual(len(problems), 1)
        self.assertIn("Consolas", problems[0])
        self.assertIn("nothing", problems[0])


if __name__ == "__main__":
    unittest.main()
