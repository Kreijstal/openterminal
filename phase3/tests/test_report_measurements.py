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

    def add_case(self, case_id: str, level: int) -> None:
        group = self.cases / f"L{level}-group"
        group.mkdir(exist_ok=True)
        (group / f"{case_id}.json").write_text(
            json.dumps({"id": case_id, "level": level, "markup": "<Grid/>"}),
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

    def test_missing_measurement_counts_against_authored_levels(self) -> None:
        # A case the probe never wrote is as bad as one it rejected: either way
        # the level is not actually covered.
        self.add_case("m", 3)
        payload = report(self.cases, self.measurements)
        self.assertEqual(payload["levels"]["3"]["missing"], 1)
        self.assertEqual(payload["authored_levels_failing"], ["3"])

    def test_oracle_file_is_not_a_measurement(self) -> None:
        self.add_case("a", 1)
        self.add_measurement("a")
        (self.measurements / "oracle.json").write_text("{}", encoding="utf-8")
        payload = report(self.cases, self.measurements)
        self.assertEqual(payload["totals"]["cases"], 1)
        self.assertEqual(payload["totals"]["measured"], 1)


if __name__ == "__main__":
    unittest.main()
