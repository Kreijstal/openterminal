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
    DerivationError, Sample, check_kerning, collect, derive, run_width,
    solve_advances, solve_line_spacing, solve_pairs,
)

# Segoe UI's, harvested. The pair solver needs advances it cannot itself derive
# -- a word constrains their sum and not any one of them -- so it is given them,
# which is exactly the direction the checked-not-trusted bargain runs in.
SEGOE_UI_ADVANCES = {
    ord(" "): 561, ord("T"): 1073, ord("W"): 1913, ord("Y"): 1132, ord("a"): 1042, ord("b"): 1204, ord("c"): 946,
    ord("d"): 1206, ord("e"): 1071, ord("f"): 641, ord("g"): 1206, ord("h"): 1159,
    ord("i"): 496, ord("j"): 496, ord("k"): 1018, ord("l"): 496, ord("m"): 1764,
    ord("n"): 1159, ord("o"): 1200, ord("p"): 1204, ord("q"): 1206, ord("r"): 712,
    ord("s"): 869, ord("t"): 694, ord("u"): 1159, ord("v"): 981, ord("w"): 1480,
    ord("x"): 940, ord("y"): 991, ord("z"): 926,
}

# What the runtime answered for the unwrapped runs, at the three sizes the
# corpus records. "Terminal" is 200 design units narrower than its advances add
# up to; the pangram is exactly what they add up to.
PANGRAM = "The quick brown fox jumps over the lazy dog"
RECORDED_RUNS = {
    ("Terminal", 12.0): 44.6133, ("Terminal", 14.0): 52.04, ("Terminal", 24.0): 89.2167,
    (PANGRAM, 12.0): 237.88, (PANGRAM, 14.0): 277.5133, (PANGRAM, 24.0): 475.7767,
}

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
        # W and Y arrived with the L4-kern probes and the corpus still only
        # ever measures them inside a pair, so they stay unsolved on purpose.
        self.assertIn(87, derived["unsolved"])
        self.assertIn(89, derived["unsolved"])

    def test_the_committed_file_is_labelled_as_not_harvested(self) -> None:
        # measure_cases and harvest_font_metrics both read this file. Nothing
        # downstream may mistake it for a reading taken from the font itself.
        derived = json.loads(DERIVED.read_text(encoding="utf-8"))
        self.assertEqual(derived["provenance"], "derived")
        self.assertIn("NOT harvested", derived["derivation"])


class SolvePairsFromRecordedTest(unittest.TestCase):
    """What the corpus says each pair of adjacent glyphs does to the advance.

    These are the runner's real answers, written out as literals, and they are
    the falsifiable half of the kerning rules: nothing here reads a font. It
    takes the advances the harvest found, asks what each recorded
    two-character run implies, and the values have to be the ones the font
    turns out to carry.
    """

    # Every isolated pair the L4-kern series recorded, at size 14, and what the
    # runtime measured for it. Five come from Segoe UI's GPOS and seven only
    # from its legacy kern table -- see PAIRS_BY_TABLE below.
    RECORDED_PAIRS = {
        "Te": 13.2867, "Ta": 12.8867, "To": 14.17, "Wa": 19.6533, "Ya": 13.63,
        "ry": 12.2, "vo": 14.8267, "yo": 14.91, "ox": 14.46, "rm": 16.9,
        "ro": 12.8867, "ve": 13.9433,
        "nn": 15.8467, "wa": 17.24,          # the controls: no pair at all
    }
    EXPECTED = {
        ("T", "e"): -200, ("T", "a"): -230, ("T", "o"): -200, ("W", "a"): -80,
        ("Y", "a"): -180, ("r", "y"): 82, ("v", "o"): -12, ("y", "o"): -10,
        ("o", "x"): -25, ("r", "m"): -4, ("r", "o"): -27, ("v", "e"): -12,
    }

    def samples(self):
        return [Sample(f"L4-kern-{text}-14", text, 14.0, width, 0.0)
                for text, width in self.RECORDED_PAIRS.items()]

    def test_every_recorded_pair_solves_to_one_integer(self) -> None:
        self.assertEqual(solve_pairs(self.samples(), SEGOE_UI_ADVANCES, 2048),
                         self.EXPECTED)

    def test_a_pair_the_font_does_not_move_is_not_recorded(self) -> None:
        # "nn" and "wa" measure the raw sum of their advances, so they solve to
        # zero, and a zero is the absence of a pair rather than a finding.
        solved = solve_pairs(self.samples(), SEGOE_UI_ADVANCES, 2048)
        self.assertNotIn(("n", "n"), solved)
        self.assertNotIn(("w", "a"), solved)

    def test_a_pair_recorded_twice_at_different_values_is_refused(self) -> None:
        samples = self.samples() + [Sample("contradiction", "Te", 14.0, 14.0, 0.0)]
        with self.assertRaisesRegex(DerivationError, "contradict"):
            solve_pairs(samples, SEGOE_UI_ADVANCES, 2048)

    def test_a_longer_run_constrains_no_pair_on_its_own(self) -> None:
        # It constrains the sum of everything in it, which is why the corpus
        # grew a case per pair. Only the two-character runs are read.
        only_words = [Sample("word", "Terminal", 14.0, 52.04, 0.0),
                      Sample("pangram", PANGRAM, 14.0, 277.5133, 0.0)]
        with self.assertRaisesRegex(DerivationError, "no recorded two-character run"):
            solve_pairs(only_words, SEGOE_UI_ADVANCES, 2048)

    def test_the_adjustment_joins_the_advance_before_the_snap(self) -> None:
        # Snapping -200 on its own and adding it afterwards is a different
        # computation, and at size 12 it lands 1/300 of a DIP away.
        kerned = run_width("Te", SEGOE_UI_ADVANCES, {("T", "e"): -200}, 2048, 12.0)
        separate = run_width("Te", SEGOE_UI_ADVANCES, {}, 2048, 12.0) + (
            round(-200 * 12.0 / 2048 * 300) / 300)
        self.assertNotAlmostEqual(kerned, separate, places=4)

    def test_only_gpos_pairs_reach_past_the_front_of_a_run(self) -> None:
        # "Terminal" is short by Te and not by rm, and rm is legacy-only.
        gpos = {("T", "e")}
        self.assertAlmostEqual(
            run_width("Terminal", SEGOE_UI_ADVANCES,
                      {("T", "e"): -200, ("r", "m"): -4}, 2048, 14.0, anywhere=gpos),
            52.04, places=4)
        # With rm allowed to reach, it lands 1/300 of a DIP low -- which is the
        # difference the recording refuses.
        self.assertAlmostEqual(
            run_width("Terminal", SEGOE_UI_ADVANCES,
                      {("T", "e"): -200, ("r", "m"): -4}, 2048, 14.0,
                      anywhere=gpos | {("r", "m")}),
            52.0133, places=4)

    def test_text_set_as_a_property_is_not_snapped(self) -> None:
        # Rule 7. The same text, the same font, the same size, two spellings.
        self.assertAlmostEqual(
            run_width("Terminal", SEGOE_UI_ADVANCES, {("T", "e"): -200}, 2048, 14.0,
                      snaps=True), 52.04, places=4)
        self.assertAlmostEqual(
            run_width("Terminal", SEGOE_UI_ADVANCES, {("T", "e"): -200}, 2048, 14.0,
                      snaps=False), 52.042, places=4)

    def test_the_committed_file_holds_every_pair_the_corpus_witnessed(self) -> None:
        derived = json.loads(DERIVED.read_text(encoding="utf-8"))
        committed = {tuple(chr(int(c)) for c in key.split(",")): value
                     for key, value in derived["kerning"].items()}
        self.assertEqual(committed, self.EXPECTED)


class KerningGateTest(unittest.TestCase):
    """What CI holds the committed pair adjustments to, in both directions."""

    PAIRS_BY_TABLE = {
        "gpos": {"84,101": -200, "84,97": -230, "84,111": -200, "87,97": -80,
                 "89,97": -180},
        # The legacy table disagrees about all five of those, and carries seven
        # the GPOS does not. Both facts are the runner's real Segoe UI.
        "kern": {"84,101": -211, "84,97": -217, "84,111": -211, "87,97": -76,
                 "89,97": -199, "114,121": 82, "118,111": -12, "121,111": -10,
                 "111,120": -25, "114,109": -4, "114,111": -27, "118,101": -12},
    }

    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.cases = self.root / "cases" / "L4-kern"
        self.measurements = self.root / "measurements"
        self.cases.mkdir(parents=True)
        self.measurements.mkdir(parents=True)
        for index, (text, width) in enumerate(
                SolvePairsFromRecordedTest.RECORDED_PAIRS.items(), start=1):
            self.write(f"L4-kern-{index:04d}", text, 14.0, width)
        self.harvest = {
            "family": "Segoe UI", "units_per_em": 2048,
            "advances": {str(k): v for k, v in SEGOE_UI_ADVANCES.items()},
            "font_kerning": self.PAIRS_BY_TABLE,
        }

    def write(self, case_id: str, text: str, size: float, width: float) -> None:
        markup = ('<TextBlock xmlns="http://schemas.microsoft.com/winfx/2006/xaml/'
                  f'presentation" FontFamily="Segoe UI" FontSize="{size}" '
                  f'TextWrapping="NoWrap">{text}</TextBlock>')
        (self.cases / f"{case_id}.json").write_text(json.dumps({
            "schema_version": 1, "id": case_id, "level": 4, "group": "kern",
            "markup": markup,
            "environment": {"font_family": "Segoe UI", "font_size": size},
        }), encoding="utf-8")
        (self.measurements / f"{case_id}.json").write_text(json.dumps({
            "schema_version": 1, "case_id": case_id,
            "tree": [{"path": "/Windows.UI.Xaml.Controls.TextBlock",
                      "type": "Windows.UI.Xaml.Controls.TextBlock",
                      "desired": [width, 19.0], "actual": [width, 19.0],
                      "offset": [0.0, 0.0]}],
        }), encoding="utf-8")

    def check(self, committed):
        return check_kerning(self.root / "cases", self.measurements, self.harvest,
                             "Segoe UI", committed)

    def test_the_committed_pairs_pass(self) -> None:
        self.assertEqual(self.check(SolvePairsFromRecordedTest.EXPECTED), [])

    def test_a_missing_pair_is_caught(self) -> None:
        short = dict(SolvePairsFromRecordedTest.EXPECTED)
        del short[("o", "x")]
        problems = self.check(short)
        self.assertTrue(any("pair ox" in p for p in problems))

    def test_a_pair_with_the_wrong_value_is_caught(self) -> None:
        wrong = dict(SolvePairsFromRecordedTest.EXPECTED)
        wrong[("T", "e")] = -211      # what the legacy table says, not GPOS
        problems = self.check(wrong)
        self.assertTrue(any("pair Te" in p for p in problems))

    def test_a_pair_no_run_witnesses_is_caught(self) -> None:
        extra = dict(SolvePairsFromRecordedTest.EXPECTED)
        extra[("W", "A")] = -150
        problems = self.check(extra)
        self.assertTrue(any("pair WA" in p for p in problems))

    def test_a_run_the_model_gets_wrong_is_caught(self) -> None:
        # The half of the gate that tests rule 5 rather than the values: a
        # recording the whole model cannot reproduce is a failure even when
        # every pair is right on its own.
        self.write("L4-kern-9001", "Term", 14.0, 30.1867)   # rm allowed to reach
        problems = self.check(SolvePairsFromRecordedTest.EXPECTED)
        self.assertTrue(any("L4-kern-9001" in p for p in problems))

    def test_a_family_the_corpus_never_measured_is_an_error(self) -> None:
        problems = check_kerning(self.root / "cases", self.measurements, self.harvest,
                                 "Consolas", {})
        self.assertEqual(len(problems), 1)
        self.assertIn("Consolas", problems[0])


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
