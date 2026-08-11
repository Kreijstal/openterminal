#!/usr/bin/env python3
"""Recording a font's ink, and everything that must stop it.

Nothing here runs the probe: it is Windows-only and builds nowhere else, so
what is under test is the part that decides whether what it printed may be
believed and may be kept. Two independent gates, and neither is a formality:

* the licence gate, which refuses a family nobody has cleared. It is the whole
  reason this pipeline is not simply pointed at Segoe UI and run.
* the agreement gate, which refuses an outline reading that disagrees with the
  metrics reading the corpus already verified against the oracle. Painting
  from a second, different reading of a font would put ink where the recorded
  measurements do not describe it.

The one thing this file cannot do is prove an outline harvest works, because
that needs a Windows runner. The end-to-end absence is asserted as a named
skip in `RecordedOutlinesAreNotAvailableYetTest`, so it reads as pending and
not as passing.
"""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT_DIRECTORY = Path(__file__).resolve().parents[1] / "scripts"
sys.path.insert(0, str(SCRIPT_DIRECTORY))

import harvest_glyph_outlines as outlines  # noqa: E402

PHASE3 = Path(__file__).resolve().parents[1]

# A metrics harvest as harvest_font_metrics.py writes one, cut to two glyphs.
METRICS = {
    "schema_version": 1,
    "family": "Cascadia Mono",
    "provenance": "harvested",
    "units_per_em": 2048,
    "source": {"file": "CascadiaMono.ttf", "sha256": "a" * 64},
    "advances": {"77": 1229, "84": 1229},
}

# What the probe prints for it: the same two glyphs, the same widths, read
# through GetGlyphRunOutline instead of hmtx.
PROBED = {
    "schema_version": 1,
    "boundary": "IDWriteFontFace::GetGlyphRunOutline -> ID2D1SimplifiedGeometrySink",
    "family": "Cascadia Mono",
    "units_per_em": 2048,
    "source": {"file": "C:/Windows/Fonts/CascadiaMono.ttf"},
    "outlines": {
        "77": {"glyph_index": 48, "advance": 1229, "fill_mode": "winding",
               "contours": [{"start": [0.0, 0.0],
                             "segments": [["l", 100.0, 0.0],
                                          ["c", 1.0, 2.0, 3.0, 4.0, 5.0, 6.0]]}]},
        "84": {"glyph_index": 55, "advance": 1229, "fill_mode": "winding",
               "contours": [{"start": [0.0, 0.0], "segments": [["l", 50.0, 0.0]]}]},
    },
}


class LicenceGateTest(unittest.TestCase):
    """The gate that is the point of the whole exercise."""

    def test_segoe_ui_is_refused_by_name(self) -> None:
        refusal = outlines.licence_refusal("Segoe UI")
        self.assertIsNotNone(refusal)
        self.assertIn("Segoe UI", refusal)
        self.assertIn("belongs to a human", refusal)

    def test_the_refusal_says_what_would_change_it(self) -> None:
        # A refusal that does not say how to lift it is an obstacle, not a
        # decision point.
        refusal = outlines.licence_refusal("Segoe UI")
        self.assertIn("CLEARED", refusal)
        self.assertIn("Cascadia Mono", refusal)

    def test_a_cleared_family_passes(self) -> None:
        self.assertIsNone(outlines.licence_refusal("Cascadia Mono"))

    def test_every_cleared_family_names_its_licence(self) -> None:
        # The list is the record of the decision; an entry with no reason is
        # not a decision anybody can review.
        for family, licence in outlines.CLEARED.items():
            with self.subTest(family=family):
                self.assertTrue(licence.strip(), f"{family} is cleared by nothing")
                self.assertIn("License", licence)

    def test_segoe_ui_is_not_quietly_on_the_list(self) -> None:
        # Guarding the guard: this is the assertion that fails loudly if a
        # later change adds the family without the licensing conversation.
        self.assertNotIn("Segoe UI", outlines.CLEARED)


class AgreementWithTheMetricsTest(unittest.TestCase):
    def test_a_matching_pair_has_no_problems(self) -> None:
        self.assertEqual(outlines.check_against_metrics(PROBED, METRICS), [])

    def test_a_different_advance_is_caught(self) -> None:
        # The load-bearing check: two independent readings of the same number
        # through different APIs. Disagreement means different glyphs.
        wrong = json.loads(json.dumps(PROBED))
        wrong["outlines"]["77"]["advance"] = 1230
        problems = outlines.check_against_metrics(wrong, METRICS)
        self.assertEqual(len(problems), 1)
        self.assertIn("U+004D", problems[0])
        self.assertIn("not the same glyph", problems[0])

    def test_a_different_units_per_em_is_caught(self) -> None:
        wrong = dict(PROBED, units_per_em=1000)
        problems = outlines.check_against_metrics(wrong, METRICS)
        self.assertTrue(any("different grid" in p for p in problems))

    def test_a_different_font_file_is_caught(self) -> None:
        # Two Segoe UIs on one machine is not exotic, and the outlines of the
        # other one would be silently wrong rather than obviously wrong.
        problems = outlines.check_against_metrics(PROBED, METRICS, file_sha256="b" * 64)
        self.assertTrue(any("two different fonts" in p for p in problems))

    def test_a_missing_glyph_is_caught(self) -> None:
        partial = json.loads(json.dumps(PROBED))
        del partial["outlines"]["84"]
        problems = outlines.check_against_metrics(partial, METRICS)
        self.assertEqual(len(problems), 1)
        self.assertIn("U+0054", problems[0])
        self.assertIn("partial harvest", problems[0])

    def test_outlines_the_metrics_do_not_cover_are_caught(self) -> None:
        extra = json.loads(json.dumps(PROBED))
        extra["outlines"]["65"] = dict(PROBED["outlines"]["77"])
        problems = outlines.check_against_metrics(extra, METRICS)
        self.assertTrue(any("the metrics do not" in p for p in problems))

    def test_the_wrong_family_is_caught(self) -> None:
        problems = outlines.check_against_metrics(dict(PROBED, family="Consolas"),
                                                  METRICS)
        self.assertTrue(any("family:" in p for p in problems))


class ShapeStructureTest(unittest.TestCase):
    def test_a_well_formed_recording_passes(self) -> None:
        self.assertEqual(outlines.check_shapes(PROBED), [])

    def test_notdef_is_refused(self) -> None:
        # The refusal the renderer makes, made one stage earlier: a .notdef
        # box is not the font's shape for a character, it is the absence of
        # one, and recording it would turn a missing glyph into a drawn box.
        wrong = json.loads(json.dumps(PROBED))
        wrong["outlines"]["77"]["glyph_index"] = 0
        problems = outlines.check_shapes(wrong)
        self.assertTrue(any(".notdef" in p for p in problems))

    def test_a_missing_fill_mode_is_refused(self) -> None:
        wrong = json.loads(json.dumps(PROBED))
        del wrong["outlines"]["77"]["fill_mode"]
        problems = outlines.check_shapes(wrong)
        self.assertTrue(any("counter is a hole or ink" in p for p in problems))

    def test_a_cubic_with_the_wrong_arity_is_refused(self) -> None:
        wrong = json.loads(json.dumps(PROBED))
        wrong["outlines"]["77"]["contours"][0]["segments"][1] = ["c", 1.0, 2.0]
        problems = outlines.check_shapes(wrong)
        self.assertTrue(any("coordinate(s), expected 6" in p for p in problems))

    def test_an_unknown_segment_kind_is_refused(self) -> None:
        wrong = json.loads(json.dumps(PROBED))
        wrong["outlines"]["84"]["contours"][0]["segments"][0] = ["q", 1.0, 2.0, 3.0, 4.0]
        problems = outlines.check_shapes(wrong)
        self.assertTrue(any("neither a line nor a cubic" in p for p in problems))


class RefusalsFromTheDriverTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.metrics = self.root / "metrics.json"
        self.metrics.write_text(json.dumps(METRICS), encoding="utf-8")

    def test_an_uncleared_family_never_reaches_the_probe(self) -> None:
        # /nonexistent as the probe: if the licence gate did not come first,
        # this would fail for the wrong reason.
        exit_code = outlines.main(["--family", "Segoe UI",
                                   "--metrics", str(self.metrics),
                                   "--probe", "/nonexistent/probe.exe",
                                   "--output", str(self.root / "out.json")])
        self.assertEqual(exit_code, 1)
        self.assertFalse((self.root / "out.json").exists())

    def test_derived_metrics_are_not_something_to_check_against(self) -> None:
        # fonts/derived holds numbers solved out of the measurements, not a
        # reading of the file. Checking a reading against a solution would be
        # comparing the outline to an inference.
        self.metrics.write_text(json.dumps(dict(METRICS, provenance="derived")),
                                encoding="utf-8")
        exit_code = outlines.main(["--family", "Cascadia Mono",
                                   "--metrics", str(self.metrics),
                                   "--probe", "/nonexistent/probe.exe",
                                   "--output", str(self.root / "out.json")])
        self.assertEqual(exit_code, 1)


class RecordedOutlinesAreNotAvailableYetTest(unittest.TestCase):
    """Pending, said out loud, so it cannot read as passing.

    Every piece above is exercised. What is not, and cannot be from here, is
    the probe itself and therefore the artifact: `glyph_outline_probe.cpp`
    builds only on Windows, and no CI run has produced a glyph-outlines
    artifact because no workflow step has ever run it. Until the lead pushes
    the workflow change and a run goes green, this is a named skip.
    """

    def test_no_glyph_outline_artifact_is_being_consumed_yet(self) -> None:
        recorded = PHASE3 / "xaml-db" / "glyph-outlines"
        harvested = sorted(recorded.glob("*.json")) if recorded.is_dir() else []
        if not harvested:
            raise unittest.SkipTest(
                "no recorded glyph outlines: phase3/xaml-db/glyph-outlines holds "
                "no harvest. The probe builds only on Windows and the workflow "
                "step that runs it has never executed, so there is no artifact "
                "to consume and the 113 Segoe UI refusals stand. This is the "
                "skip that must turn into a test when a run produces one.")
        for path in harvested:
            with self.subTest(path.name):
                document = json.loads(path.read_text(encoding="utf-8"))
                self.assertEqual(outlines.check_shapes(document), [])
                self.assertIsNone(outlines.licence_refusal(document["family"]),
                                  f"{path.name} records outlines for a family "
                                  f"that is not cleared")


if __name__ == "__main__":
    unittest.main()
