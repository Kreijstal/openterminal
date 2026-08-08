#!/usr/bin/env python3

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT_DIRECTORY = Path(__file__).resolve().parents[1] / "scripts"
sys.path.insert(0, str(SCRIPT_DIRECTORY))

from check_twins import check, summarise  # noqa: E402

TREE = [{"path": "/Border", "type": "Border", "desired": [60.0, 30.0],
         "actual": [60.0, 30.0], "offset": [0.0, 0.0]}]


class TwinTest(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        root = Path(self.directory.name)
        self.cases = root / "cases"
        self.results = root / "results"
        self.expected = root / "expected"
        self.cases.mkdir()
        self.results.mkdir()
        self.expected.mkdir()

    def add_case(self, case_id: str, twin: str | None = None) -> None:
        payload: dict[str, object] = {"id": case_id, "level": 5, "markup": "<Border/>"}
        if twin:
            payload["twin"] = twin
        (self.cases / f"{case_id}.json").write_text(json.dumps(payload), encoding="utf-8")

    def add_result(self, case_id: str, tree: list | None = None,
                   error: str | None = None) -> None:
        payload: dict[str, object] = {"case_id": case_id}
        if error:
            payload["error"] = error
        else:
            payload["tree"] = tree if tree is not None else TREE
        (self.results / f"{case_id}.json").write_text(json.dumps(payload), encoding="utf-8")

    def add_expected(self, case_id: str, tree: list | None = None,
                     error: str | None = None) -> None:
        payload: dict[str, object] = {"case_id": case_id}
        if error:
            payload["error"] = error
        else:
            payload["tree"] = tree if tree is not None else TREE
        (self.expected / f"{case_id}.json").write_text(json.dumps(payload), encoding="utf-8")

    def pair(self, **kwargs: object) -> dict:
        self.add_case("a", twin="b")
        self.add_case("b")
        return kwargs  # convenience only

    def test_identical_trees_agree(self) -> None:
        self.pair()
        self.add_result("a")
        self.add_result("b")
        result = check(self.cases, self.results, 0.01)
        self.assertEqual(result["totals"], {"pairs": 1, "matched": 1, "unverified": 0,
                                            "refuted": 0, "mismatched": 0})

    def test_a_different_number_disagrees(self) -> None:
        self.pair()
        self.add_result("a")
        self.add_result("b", tree=[dict(TREE[0], actual=[61.0, 30.0])])
        result = check(self.cases, self.results, 0.01)
        self.assertEqual(result["totals"]["mismatched"], 1)
        self.assertIn("actual", result["mismatches"][0]["detail"])

    def test_one_half_failing_is_a_disagreement(self) -> None:
        # The resource form loading and the inline form not, or the reverse, is
        # the loudest possible mismatch and must not be counted as agreement.
        self.pair()
        self.add_result("a", error="resource 'X' not found")
        self.add_result("b")
        result = check(self.cases, self.results, 0.01)
        self.assertEqual(result["totals"]["mismatched"], 1)
        self.assertIn("failed to load", result["mismatches"][0]["detail"])

    def test_both_failing_identically_proves_nothing(self) -> None:
        # Two identical failures agree about nothing, so they are counted apart
        # from the pairs that actually compared a number.
        self.pair()
        self.add_result("a", error="no harvested metrics")
        self.add_result("b", error="no harvested metrics")
        result = check(self.cases, self.results, 0.01)
        self.assertEqual(result["totals"]["matched"], 0)
        self.assertEqual(result["totals"]["unverified"], 1)
        self.assertIn("Not verified here", summarise(result))

    def test_both_failing_differently_disagrees(self) -> None:
        self.pair()
        self.add_result("a", error="resource 'X' not found")
        self.add_result("b", error="no harvested metrics")
        result = check(self.cases, self.results, 0.01)
        self.assertEqual(result["totals"]["mismatched"], 1)

    def test_a_missing_result_is_a_disagreement(self) -> None:
        self.pair()
        self.add_result("a")
        result = check(self.cases, self.results, 0.01)
        self.assertEqual(result["totals"]["mismatched"], 1)

    def test_a_twin_that_is_not_in_the_corpus_is_reported(self) -> None:
        self.add_case("a", twin="ghost")
        self.add_result("a")
        result = check(self.cases, self.results, 0.01)
        self.assertIn("not in the corpus", result["mismatches"][0]["detail"])

    def test_cases_without_a_twin_are_not_pairs(self) -> None:
        self.add_case("solo")
        self.add_result("solo")
        result = check(self.cases, self.results, 0.01)
        self.assertEqual(result["totals"]["pairs"], 0)

    def test_a_pair_the_oracle_records_as_different_is_not_demanded(self) -> None:
        # The corpus claims the two halves describe one layout. The runtime is
        # entitled to deny it -- thirteen of the fifty-eight pairs it has
        # measured differ -- and an implementation that reproduces the recorded
        # difference must not be failed for it.
        self.pair()
        self.add_result("a", tree=[dict(TREE[0], desired=[0.0, 0.0])])
        self.add_result("b")
        self.add_expected("a", tree=[dict(TREE[0], desired=[0.0, 0.0])])
        self.add_expected("b")
        result = check(self.cases, self.results, 0.01, self.expected)
        self.assertEqual(result["totals"], {"pairs": 1, "matched": 0, "unverified": 0,
                                            "refuted": 1, "mismatched": 0})
        self.assertIn("Refuted by the oracle", summarise(result))

    def test_a_pair_the_oracle_records_as_equal_is_still_demanded(self) -> None:
        self.pair()
        self.add_result("a", tree=[dict(TREE[0], desired=[0.0, 0.0])])
        self.add_result("b")
        self.add_expected("a")
        self.add_expected("b")
        result = check(self.cases, self.results, 0.01, self.expected)
        self.assertEqual(result["totals"]["mismatched"], 1)

    def test_an_oracle_that_failed_one_half_refutes_nothing(self) -> None:
        # Two errors, or one, say nothing about whether the halves describe the
        # same layout, so the pair goes back to being an ordinary twin check.
        self.pair()
        self.add_result("a")
        self.add_result("b")
        self.add_expected("a", error="the type 'Nonsense' is not implemented")
        self.add_expected("b")
        result = check(self.cases, self.results, 0.01, self.expected)
        self.assertEqual(result["totals"]["matched"], 1)
        self.assertEqual(result["totals"]["refuted"], 0)

    def test_without_a_recording_nothing_is_refuted(self) -> None:
        self.pair()
        self.add_result("a", tree=[dict(TREE[0], desired=[0.0, 0.0])])
        self.add_result("b")
        result = check(self.cases, self.results, 0.01)
        self.assertEqual(result["totals"]["refuted"], 0)
        self.assertEqual(result["totals"]["mismatched"], 1)

    def test_the_summary_says_what_it_is_not(self) -> None:
        # The number is easy to read as coverage. It is not, and the text that
        # says so is part of the output rather than a footnote elsewhere.
        self.pair()
        self.add_result("a")
        self.add_result("b")
        self.assertIn("Self-consistency only", summarise(check(self.cases, self.results, 0.01)))


if __name__ == "__main__":
    unittest.main()
