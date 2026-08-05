#!/usr/bin/env python3
"""Solving font metrics back out of the recorded measurements.

Two kinds of test here. The synthetic ones invent a font, forward-model what a
TextBlock in it would measure, and check the solver recovers the numbers it
started from -- that exercises the search, the uniqueness rules and the error
paths. The recorded ones use the numbers the real runtime actually answered,
written out as literals, and are the reason the derived file in the repository
is worth anything: they fail if the model in layout/src/text.cpp and the
measurements part company, without needing the measurement artifact present.
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

from derive_font_metrics import (  # noqa: E402
    DerivationError, Sample, collect, derive, solve_advances, solve_line_spacing,
)

DERIVED = (Path(__file__).resolve().parents[1]
           / "xaml-db" / "fonts" / "derived" / "segoe-ui.json")

# What the runtime answered for the cases that need one number each. Empty text
# keeps the unsnapped line height, a single glyph gets the snapped one, and the
# width of that glyph is its advance.
RECORDED_EMPTY = {12.0: 15.9609, 14.0: 18.6211, 24.0: 31.9219}
RECORDED_M = {12.0: (10.7767, 15.96), 14.0: (12.57, 18.62), 24.0: (21.55, 31.9233)}


def f32(value: float) -> float:
    return struct.unpack("<f", struct.pack("<f", value))[0]


def forward(units_per_em: int, line_spacing: int, advance: int, size: float):
    """What a one-glyph TextBlock measures, per the rules in text.cpp."""
    unsnapped = f32(line_spacing * size / units_per_em)
    snapped = f32(round(line_spacing * size / units_per_em * 300) / 300)
    width = f32(round(advance * size / units_per_em * 300) / 300)
    return unsnapped, snapped, width


def recorded_samples() -> list[Sample]:
    samples = [Sample(f"empty-{size}", "", size, 0.0, height)
               for size, height in RECORDED_EMPTY.items()]
    samples += [Sample(f"m-{size}", "M", size, width, height)
                for size, (width, height) in RECORDED_M.items()]
    return samples


class SolveFromRecordedTest(unittest.TestCase):
    """The two numbers the corpus can answer on its own."""

    def test_line_spacing_is_the_only_one_that_fits_every_size(self) -> None:
        self.assertEqual(solve_line_spacing(recorded_samples(), 2048), 2724)

    def test_the_advance_of_m_is_the_only_one_that_fits_every_size(self) -> None:
        solved, unsolved = solve_advances(recorded_samples(), 2048)
        self.assertEqual(solved, {ord("M"): 1839})
        self.assertEqual(unsolved, [])

    def test_every_recorded_size_has_to_agree(self) -> None:
        # Each of the three sizes is on its own enough to name one integer --
        # the design grid is finer than the 1/300 DIP snap at every size the
        # corpus uses. What the other two add is corroboration: an advance that
        # fits one size and not the others is refused rather than averaged.
        samples = recorded_samples()
        samples[-1] = Sample(samples[-1].case_id, "M", 24.0, 21.56, 31.9233)
        with self.assertRaisesRegex(DerivationError, "no integer advance for U\\+004D"):
            solve_advances(samples, 2048)

    def test_a_size_too_small_to_separate_advances_is_refused(self) -> None:
        # Below about seven points the snap is coarser than the design grid, so
        # several advances produce the same width. Guessing one would invent
        # precision the measurement does not have.
        tiny = [Sample("tiny", "M", 2.0, f32(round(1839 * 2 / 2048 * 300) / 300), 2.66)]
        with self.assertRaisesRegex(DerivationError, "U\\+004D.*candidates"):
            solve_advances(tiny, 2048)

    def test_the_committed_file_says_what_the_measurements_imply(self) -> None:
        derived = json.loads(DERIVED.read_text(encoding="utf-8"))
        hhea = derived["hhea"]
        self.assertEqual(derived["family"], "Segoe UI")
        self.assertEqual(derived["units_per_em"], 2048)
        self.assertEqual(hhea["ascender"] - hhea["descender"] + hhea["line_gap"], 2724)
        self.assertEqual(derived["advances"], {"77": 1839})

    def test_the_committed_file_is_labelled_as_not_harvested(self) -> None:
        # measure_cases and harvest_font_metrics both read this file. Nothing
        # downstream may mistake it for a reading taken from the font itself.
        derived = json.loads(DERIVED.read_text(encoding="utf-8"))
        self.assertEqual(derived["provenance"], "derived")
        self.assertIn("NOT harvested", derived["derivation"])


class SolveSyntheticTest(unittest.TestCase):
    """A font invented here, so the answer is known before the solver runs."""

    UNITS_PER_EM = 1000
    LINE_SPACING = 1213
    ADVANCE = 617

    def samples(self, sizes=(11.0, 17.0, 29.0)) -> list[Sample]:
        out = []
        for size in sizes:
            unsnapped, snapped, width = forward(
                self.UNITS_PER_EM, self.LINE_SPACING, self.ADVANCE, size)
            out.append(Sample(f"empty-{size}", "", size, 0.0, unsnapped))
            out.append(Sample(f"x-{size}", "X", size, width, snapped))
        return out

    def test_round_trips_line_spacing_and_advance(self) -> None:
        self.assertEqual(solve_line_spacing(self.samples(), self.UNITS_PER_EM),
                         self.LINE_SPACING)
        solved, _ = solve_advances(self.samples(), self.UNITS_PER_EM)
        self.assertEqual(solved, {ord("X"): self.ADVANCE})

    def test_contradictory_observations_are_refused(self) -> None:
        samples = self.samples()
        samples[0] = Sample(samples[0].case_id, "", samples[0].font_size, 0.0, 99.5)
        with self.assertRaisesRegex(DerivationError, "no integer"):
            solve_line_spacing(samples, self.UNITS_PER_EM)

    def test_a_character_only_seen_in_a_word_is_left_unsolved(self) -> None:
        # A multi-character run constrains the sum of its advances, not any one
        # of them. Reporting them as unsolved is what keeps the other 36 cases
        # failing loudly instead of measuring against a guess.
        samples = self.samples() + [Sample("word", "Xyz", 11.0, 30.0, 13.343)]
        solved, unsolved = solve_advances(samples, self.UNITS_PER_EM)
        self.assertEqual(sorted(solved), [ord("X")])
        self.assertEqual(unsolved, [ord("y"), ord("z")])

    def test_nothing_solvable_is_an_error_not_an_empty_font(self) -> None:
        with self.assertRaisesRegex(DerivationError, "no single-character"):
            solve_advances([Sample("word", "Xyz", 11.0, 30.0, 13.343)],
                           self.UNITS_PER_EM)


class CollectTest(unittest.TestCase):
    """Reading the samples out of a corpus and a measurement directory."""

    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.cases = self.root / "cases" / "L4-text"
        self.measurements = self.root / "measurements"
        self.cases.mkdir(parents=True)
        self.measurements.mkdir(parents=True)

    def write_case(self, case_id: str, text: str, size: float, *,
                   family: str = "Segoe UI", level: int = 4) -> None:
        markup = (f'<TextBlock xmlns="http://schemas.microsoft.com/winfx/2006/xaml/'
                  f'presentation" FontFamily="{family}" FontSize="{size}" '
                  f'TextWrapping="NoWrap">{text}</TextBlock>')
        (self.cases / f"{case_id}.json").write_text(json.dumps({
            "schema_version": 1, "id": case_id, "level": level, "group": "text",
            "markup": markup,
            "environment": {"font_family": family, "font_size": size},
        }), encoding="utf-8")

    def write_measurement(self, case_id: str, node: dict) -> None:
        (self.measurements / f"{case_id}.json").write_text(json.dumps({
            "schema_version": 1, "case_id": case_id, "tree": [node],
        }), encoding="utf-8")

    def text_node(self, width: float, height: float) -> dict:
        return {"path": "/Windows.UI.Xaml.Controls.TextBlock",
                "type": "Windows.UI.Xaml.Controls.TextBlock",
                "desired": [width, height], "actual": [width, height],
                "offset": [0.0, 0.0]}

    def test_pairs_each_case_with_what_the_runtime_answered(self) -> None:
        self.write_case("L4-text-0001", "", 12.0)
        self.write_measurement("L4-text-0001", self.text_node(0.0, 15.9609))
        samples = collect(self.root / "cases", self.measurements, "Segoe UI")
        self.assertEqual(samples, [Sample("L4-text-0001", "", 12.0, 0.0, 15.9609)])

    def test_another_family_is_not_mixed_in(self) -> None:
        self.write_case("L4-text-0001", "M", 12.0, family="Consolas")
        self.write_measurement("L4-text-0001", self.text_node(10.0, 15.0))
        self.assertEqual(collect(self.root / "cases", self.measurements, "Segoe UI"), [])

    def test_a_case_the_oracle_rejected_is_not_an_observation(self) -> None:
        self.write_case("L4-text-0001", "M", 12.0)
        (self.measurements / "L4-text-0001.json").write_text(json.dumps({
            "schema_version": 1, "case_id": "L4-text-0001",
            "error": "the runtime would not load it"}), encoding="utf-8")
        self.assertEqual(collect(self.root / "cases", self.measurements, "Segoe UI"), [])

    def test_a_case_with_no_measurement_is_skipped(self) -> None:
        self.write_case("L4-text-0001", "M", 12.0)
        self.assertEqual(collect(self.root / "cases", self.measurements, "Segoe UI"), [])

    def test_derive_writes_something_measure_cases_can_load(self) -> None:
        spacing, advance, upem = 1213, 617, 1000
        for index, (text, size) in enumerate(
                [("", 11.0), ("", 17.0), ("", 29.0),
                 ("X", 11.0), ("X", 17.0), ("X", 29.0)], start=1):
            case_id = f"L4-text-{index:04d}"
            self.write_case(case_id, text, size)
            unsnapped, snapped, width = forward(upem, spacing, advance, size)
            if text:
                self.write_measurement(case_id, self.text_node(width, snapped))
            else:
                self.write_measurement(case_id, self.text_node(0.0, unsnapped))

        metrics = derive(self.root / "cases", self.measurements, "Segoe UI", upem)
        self.assertEqual(metrics["family"], "Segoe UI")
        self.assertEqual(metrics["units_per_em"], upem)
        self.assertEqual(metrics["hhea"]["ascender"], spacing)
        self.assertEqual(metrics["advances"], {"88": advance})
        self.assertEqual(metrics["provenance"], "derived")


if __name__ == "__main__":
    unittest.main()
