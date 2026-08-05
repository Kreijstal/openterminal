#!/usr/bin/env python3

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT_DIRECTORY = Path(__file__).resolve().parents[1] / "scripts"
sys.path.insert(0, str(SCRIPT_DIRECTORY))

from report_measurements import report, summarise  # noqa: E402


class ReportTest(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.cases = self.root / "cases"
        self.measurements = self.root / "measurements"
        self.cases.mkdir()
        self.measurements.mkdir()

    def add_case(self, case_id: str, level: int, **extra: object) -> None:
        group = self.cases / f"L{level}-group"
        group.mkdir(exist_ok=True)
        (group / f"{case_id}.json").write_text(
            json.dumps({"id": case_id, "level": level, "markup": "<Grid/>", **extra}),
            encoding="utf-8",
        )

    def add_measurement(self, case_id: str, error: str | None = None) -> None:
        payload: dict[str, object] = {"case_id": case_id}
        if error:
            payload["error"] = error
        else:
            payload["tree"] = []
        (self.measurements / f"{case_id}.json").write_text(
            json.dumps(payload), encoding="utf-8"
        )

    def test_counts_by_level(self) -> None:
        self.add_case("a", 1)
        self.add_case("b", 7)
        self.add_measurement("a")
        self.add_measurement("b")
        payload = report(self.cases, self.measurements)
        self.assertEqual(payload["levels"]["1"]["measured"], 1)
        self.assertEqual(payload["levels"]["7"]["measured"], 1)
        self.assertEqual(payload["authored_levels_failing"], [])

    def test_harvested_failure_is_quarantined_not_fatal(self) -> None:
        self.add_case("h", 7)
        self.add_measurement("h", error="Cannot resolve StaticResource")
        payload = report(self.cases, self.measurements)
        self.assertEqual(payload["authored_levels_failing"], [])
        self.assertEqual([q["id"] for q in payload["quarantine"]], ["h"])
        self.assertIn("Quarantined harvest candidates", summarise(payload))

    def test_authored_failure_is_fatal(self) -> None:
        self.add_case("g", 2)
        self.add_measurement("g", error="boom")
        payload = report(self.cases, self.measurements)
        self.assertEqual(payload["authored_levels_failing"], ["2"])
        self.assertIn("Authored levels failing: L2", summarise(payload))

    def test_authored_failure_is_named_not_just_counted(self) -> None:
        # The fatal failure is the one a reader most needs to identify. Naming
        # only the quarantined harvest left it reported as a bare count.
        self.add_case("g", 2)
        self.add_measurement("g", error="Failed to create a 'Windows.UI.Xaml.Whatever'")
        summary = summarise(report(self.cases, self.measurements))
        self.assertIn("`g`", summary)
        self.assertIn("Windows.UI.Xaml.Whatever", summary)

    def test_missing_authored_case_is_named(self) -> None:
        self.add_case("m", 3)
        payload = report(self.cases, self.measurements)
        self.assertEqual(payload["missing_ids"], ["m"])
        self.assertIn("`m`", summarise(payload))

    def test_missing_measurement_counts_against_authored_levels(self) -> None:
        # A case the probe never wrote is as bad as one it rejected: either way
        # the level is not actually covered.
        self.add_case("m", 3)
        payload = report(self.cases, self.measurements)
        self.assertEqual(payload["levels"]["3"]["missing"], 1)
        self.assertEqual(payload["authored_levels_failing"], ["3"])

    def test_declared_question_is_answered_not_broken(self) -> None:
        # An L5 case that says up front it does not know the behaviour. The
        # runtime refusing it is the answer we asked for.
        self.add_case("q", 5, oracle_decides=True,
                      question="does a forward reference resolve?")
        self.add_measurement("q", error="Cannot find a resource named 'BoxWidth'")
        payload = report(self.cases, self.measurements)
        self.assertEqual(payload["authored_levels_failing"], [])
        self.assertEqual([q["kind"] for q in payload["quarantine"]], ["question"])
        summary = summarise(payload)
        self.assertIn("Open questions the runtime answered", summary)
        self.assertNotIn("Quarantined harvest candidates", summary)

    def test_l5_without_a_question_is_still_fatal(self) -> None:
        # The exemption is per case and has to be declared. Without it, level 5
        # is as accountable as every other level we author.
        self.add_case("r", 5)
        self.add_measurement("r", error="boom")
        payload = report(self.cases, self.measurements)
        self.assertEqual(payload["authored_levels_failing"], ["5"])
        self.assertEqual([q["kind"] for q in payload["quarantine"]], ["authored"])

    def test_a_question_does_not_excuse_its_level(self) -> None:
        # One answered question beside one broken case at the same level: the
        # level still fails, and for the broken one only.
        self.add_case("q", 5, oracle_decides=True, question="?")
        self.add_case("r", 5)
        self.add_measurement("q", error="refused")
        self.add_measurement("r", error="boom")
        payload = report(self.cases, self.measurements)
        self.assertEqual(payload["authored_levels_failing"], ["5"])
        summary = summarise(payload)
        self.assertIn("`r` — boom", summary)
        self.assertIn("`q` — refused", summary)

    def test_oracle_file_is_not_a_measurement(self) -> None:
        self.add_case("a", 1)
        self.add_measurement("a")
        (self.measurements / "oracle.json").write_text("{}", encoding="utf-8")
        payload = report(self.cases, self.measurements)
        self.assertEqual(payload["totals"]["cases"], 1)
        self.assertEqual(payload["totals"]["measured"], 1)


if __name__ == "__main__":
    unittest.main()
