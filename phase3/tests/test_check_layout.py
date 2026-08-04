#!/usr/bin/env python3

from __future__ import annotations

import contextlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT_DIRECTORY = Path(__file__).resolve().parents[1] / "scripts"
sys.path.insert(0, str(SCRIPT_DIRECTORY))

from check_layout import DEFAULT_TOLERANCE, check, main, summarise  # noqa: E402


def node(path: str, type_name: str = "T", desired=(10.0, 10.0),
         actual=(10.0, 10.0), offset=(0.0, 0.0)) -> dict:
    return {"path": path, "type": type_name, "desired": list(desired),
            "actual": list(actual), "offset": list(offset)}


class CheckLayoutTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        root = Path(self.tmp.name)
        self.expected = root / "expected"
        self.actual = root / "actual"
        self.expected.mkdir()
        self.actual.mkdir()

    def write(self, where: Path, case_id: str, tree=None, error: str | None = None) -> None:
        body: dict = {"schema_version": 1, "case_id": case_id}
        if error is not None:
            body["error"] = error
        else:
            body["tree"] = tree if tree is not None else [node("/T")]
        (where / f"{case_id}.json").write_text(json.dumps(body), encoding="utf-8")

    def both(self, case_id: str, tree=None) -> None:
        self.write(self.expected, case_id, tree)
        self.write(self.actual, case_id, tree)

    def run_check(self, levels=None, tolerance=DEFAULT_TOLERANCE) -> dict:
        return check(self.expected, self.actual, levels, tolerance)

    def run_main(self, argv: list[str]) -> int:
        """main() writes a report; a test run should not be buried in it."""
        with contextlib.redirect_stdout(io.StringIO()), \
                contextlib.redirect_stderr(io.StringIO()):
            return main(argv)

    def test_identical_results_pass(self) -> None:
        self.both("L1-a")
        result = self.run_check()
        self.assertEqual(result["totals"], {"cases": 1, "passed": 1, "failed": 0})

    def test_wrong_desired_fails_even_when_actual_matches(self) -> None:
        # The reason both halves are recorded. An implementation can arrange
        # the right rect off a wrong measurement, and it only shows up later
        # under a parent that reads DesiredSize.
        self.write(self.expected, "L1-a", [node("/T", desired=(10.0, 10.0))])
        self.write(self.actual, "L1-a", [node("/T", desired=(17.0, 10.0))])
        result = self.run_check()
        self.assertEqual(result["totals"]["failed"], 1)
        self.assertIn("desired", result["mismatches"][0]["detail"])

    def test_wrong_offset_fails(self) -> None:
        self.write(self.expected, "L1-a", [node("/T", offset=(0.0, 0.0))])
        self.write(self.actual, "L1-a", [node("/T", offset=(30.0, 0.0))])
        self.assertEqual(self.run_check()["totals"]["failed"], 1)

    def test_wrong_type_fails(self) -> None:
        self.write(self.expected, "L1-a", [node("/T", "Border")])
        self.write(self.actual, "L1-a", [node("/T", "Canvas")])
        self.assertIn("Canvas", self.run_check()["mismatches"][0]["detail"])

    def test_missing_and_extra_nodes_are_both_reported(self) -> None:
        self.write(self.expected, "L1-a", [node("/T"), node("/T/A")])
        self.write(self.actual, "L1-a", [node("/T"), node("/T/B")])
        details = [m["detail"] for m in self.run_check()["mismatches"]]
        self.assertIn("node missing from the result", details)
        self.assertIn("node not in the oracle", details)

    def test_absent_case_is_a_failure(self) -> None:
        self.write(self.expected, "L1-a")
        result = self.run_check()
        self.assertEqual(result["totals"]["failed"], 1)
        self.assertEqual(result["mismatches"][0]["detail"], "no result produced")

    def test_implementation_error_is_a_failure(self) -> None:
        self.write(self.expected, "L1-a")
        self.write(self.actual, "L1-a", error="not implemented")
        self.assertIn("not implemented", self.run_check()["mismatches"][0]["detail"])

    def test_case_the_oracle_could_not_load_holds_nobody_to_anything(self) -> None:
        # The runtime refused it, so there is no layout expectation to meet.
        self.write(self.expected, "L7-a", error="unknown type")
        self.write(self.actual, "L7-a", error="something else entirely")
        self.assertEqual(self.run_check()["totals"]["failed"], 0)

    def test_difference_within_tolerance_is_not_a_mismatch(self) -> None:
        self.write(self.expected, "L1-a", [node("/T", actual=(10.0, 10.0))])
        self.write(self.actual, "L1-a", [node("/T", actual=(10.004, 10.0))])
        self.assertEqual(self.run_check()["totals"]["failed"], 0)

    def test_difference_above_tolerance_is_a_mismatch(self) -> None:
        self.write(self.expected, "L1-a", [node("/T", actual=(10.0, 10.0))])
        self.write(self.actual, "L1-a", [node("/T", actual=(10.02, 10.0))])
        self.assertEqual(self.run_check()["totals"]["failed"], 1)

    def test_levels_filter_selects_what_is_compared(self) -> None:
        self.both("L1-a")
        self.both("L4-b")
        self.assertEqual(self.run_check({"L1"})["totals"]["cases"], 1)
        self.assertEqual(self.run_check()["totals"]["cases"], 2)

    def test_infinity_compares_as_a_token(self) -> None:
        self.write(self.expected, "L1-a", [node("/T", desired=("Infinity", 10.0))])
        self.write(self.actual, "L1-a", [node("/T", desired=("Infinity", 10.0))])
        self.assertEqual(self.run_check()["totals"]["failed"], 0)
        self.write(self.actual, "L1-a", [node("/T", desired=(400.0, 10.0))])
        self.assertEqual(self.run_check()["totals"]["failed"], 1)

    def test_comparing_nothing_is_an_error_not_a_pass(self) -> None:
        # An empty selection reporting success would be the worst outcome
        # here: a green run that checked nothing at all.
        self.both("L1-a")
        code = self.run_main(["--expected", str(self.expected), "--actual", str(self.actual),
                              "--levels", "L9"])
        self.assertEqual(code, 2)

    def test_exit_code_follows_the_result(self) -> None:
        self.both("L1-a")
        argv = ["--expected", str(self.expected), "--actual", str(self.actual)]
        self.assertEqual(self.run_main(argv), 0)
        self.write(self.actual, "L1-a", [node("/T", actual=(99.0, 10.0))])
        self.assertEqual(self.run_main(argv), 1)

    def test_summary_lists_mismatches(self) -> None:
        self.write(self.expected, "L1-a", [node("/T", actual=(10.0, 10.0))])
        self.write(self.actual, "L1-a", [node("/T", actual=(99.0, 10.0))])
        text = summarise(self.run_check())
        self.assertIn("L1-a", text)
        self.assertIn("Mismatches", text)


if __name__ == "__main__":
    unittest.main()
