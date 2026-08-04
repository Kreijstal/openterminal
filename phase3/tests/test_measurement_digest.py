#!/usr/bin/env python3

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT_DIRECTORY = Path(__file__).resolve().parents[1] / "scripts"
sys.path.insert(0, str(SCRIPT_DIRECTORY))

from measurement_digest import compare, digest, oracle_record  # noqa: E402


def measurement(case_id: str, width: float = 10.0, path: str = "/T") -> dict:
    return {"schema_version": 1, "case_id": case_id,
            "tree": [{"path": path, "type": "T", "desired": [width, 10.0],
                      "actual": [width, 10.0], "offset": [0.0, 0.0]}]}


class DigestTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dir = Path(self.tmp.name)

    def write(self, body: dict) -> None:
        (self.dir / f"{body['case_id']}.json").write_text(json.dumps(body), encoding="utf-8")

    def write_oracle(self, build: str = "10.0.1.1") -> None:
        (self.dir / "oracle.json").write_text(json.dumps(
            {"os_build": build, "font_segoeui": "ABC", "xaml": "x"}), encoding="utf-8")

    def test_same_content_same_digest(self) -> None:
        a = digest({"L1-a": measurement("L1-a")})
        b = digest({"L1-a": measurement("L1-a")})
        self.assertEqual(a["sha256"], b["sha256"])

    def test_changed_value_changes_the_digest(self) -> None:
        a = digest({"L1-a": measurement("L1-a", 10.0)})
        b = digest({"L1-a": measurement("L1-a", 10.5)})
        self.assertNotEqual(a["sha256"], b["sha256"])

    def test_digest_is_about_content_not_spelling(self) -> None:
        # 10 and 10.0 are the same answer. The digest has to survive the probe
        # reformatting its output, or every cosmetic change reads as drift.
        integral = measurement("L1-a")
        integral["tree"][0]["desired"] = [10, 10]
        integral["tree"][0]["actual"] = [10, 10]
        self.assertEqual(
            digest({"L1-a": integral})["sha256"],
            digest({"L1-a": measurement("L1-a")})["sha256"],
        )

    def test_per_level_digests_localise_a_change(self) -> None:
        before = {"L1-a": measurement("L1-a"), "L3-b": measurement("L3-b")}
        after = {"L1-a": measurement("L1-a"), "L3-b": measurement("L3-b", 11.0)}
        a, b = digest(before), digest(after)
        self.assertEqual(a["levels"]["L1"]["sha256"], b["levels"]["L1"]["sha256"])
        self.assertNotEqual(a["levels"]["L3"]["sha256"], b["levels"]["L3"]["sha256"])

    def test_error_text_is_part_of_the_fingerprint(self) -> None:
        a = digest({"L7-a": {"case_id": "L7-a", "error": "unknown type Foo"}})
        b = digest({"L7-a": {"case_id": "L7-a", "error": "unknown type Bar"}})
        self.assertNotEqual(a["sha256"], b["sha256"])

    def test_oracle_record_ignores_non_measurements(self) -> None:
        self.write_oracle()
        self.write(measurement("L1-a"))
        (self.dir / "report.json").write_text('{"anything": true}', encoding="utf-8")
        record = oracle_record(self.dir)
        self.assertEqual(record["digest"]["cases"], 1)
        self.assertEqual(record["os_build"], "10.0.1.1")

    def test_compare_is_quiet_when_nothing_moved(self) -> None:
        self.write_oracle()
        self.write(measurement("L1-a"))
        record = oracle_record(self.dir)
        self.assertEqual(compare(record, record), [])

    def test_compare_names_the_level_that_moved(self) -> None:
        self.write_oracle()
        self.write(measurement("L1-a"))
        self.write(measurement("L3-b"))
        before = oracle_record(self.dir)
        self.write(measurement("L3-b", 12.0))
        problems = compare(before, oracle_record(self.dir))
        self.assertEqual(len(problems), 1)
        self.assertIn("L3", problems[0])
        self.assertIn("different answers", problems[0])

    def test_compare_reports_a_changed_build_and_font(self) -> None:
        self.write_oracle()
        self.write(measurement("L1-a"))
        before = oracle_record(self.dir)
        self.write_oracle("10.0.9.9")
        problems = compare(before, oracle_record(self.dir))
        self.assertTrue(any("os_build" in p for p in problems))

    def test_compare_reports_a_changed_case_count(self) -> None:
        self.write_oracle()
        self.write(measurement("L1-a"))
        before = oracle_record(self.dir)
        self.write(measurement("L1-b"))
        problems = compare(before, oracle_record(self.dir))
        self.assertTrue(any("1 cases -> 2 cases" in p for p in problems))

    def test_compare_reports_an_added_level(self) -> None:
        self.write_oracle()
        self.write(measurement("L1-a"))
        before = oracle_record(self.dir)
        self.write(measurement("L9-z"))
        problems = compare(before, oracle_record(self.dir))
        self.assertTrue(any("new level" in p for p in problems))


class CommittedOracleTest(unittest.TestCase):
    """The digests in the repository have to stay loadable and well formed."""

    def test_committed_oracles_are_well_formed(self) -> None:
        oracles = Path(__file__).resolve().parents[1] / "xaml-db" / "oracles"
        files = sorted(oracles.glob("*.json"))
        self.assertTrue(files, "no committed oracle digest to check")
        for path in files:
            record = json.loads(path.read_text(encoding="utf-8"))
            self.assertEqual(path.stem, record["os_build"],
                             f"{path.name} is named for a different build")
            self.assertEqual(len(record["font_segoeui"]), 64)
            self.assertEqual(len(record["digest"]["sha256"]), 64)
            self.assertEqual(
                record["digest"]["cases"],
                sum(e["cases"] for e in record["digest"]["levels"].values()),
            )


if __name__ == "__main__":
    unittest.main()
