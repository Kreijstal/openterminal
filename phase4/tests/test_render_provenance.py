"""The dump root says what it was made from, and a drifted input is named.

This is the half of the Wine render gate that never needs Wine. The gate itself
reads a scratch directory nothing regenerates on its behalf, and through waves 5
and 6 it reported green over dumps written before either wave -- the layout core
had learned to place an element inside its slot by alignment, the case corpus
had been regenerated, and the test asked neither question. So the record and its
comparison are gated here, synthesised end to end, where there is nothing to
skip and nothing to be vacuous about.
"""

import json
import sys
import tempfile
import unittest
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPOSITORY / "phase3" / "scripts"))

import render_provenance  # noqa: E402


class Digests(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.root = Path(self.dir.name)
        (self.root / "src").mkdir()
        (self.root / "src" / "a.cpp").write_text("int a();\n")
        (self.root / "src" / "b.h").write_text("inline int b() { return 1; }\n")

    def tearDown(self):
        self.dir.cleanup()

    def test_the_same_tree_digests_the_same_twice(self):
        first = render_provenance.source_digest(self.root, [self.root / "src"])
        second = render_provenance.source_digest(self.root, [self.root / "src"])
        self.assertEqual(first, second)

    def test_a_changed_header_moves_the_digest(self):
        # The rule this gate went blind to lives in a header, not in any file a
        # compile list names: AlignmentOffset in phase3/layout/src/layout.h.
        before = render_provenance.source_digest(self.root, [self.root / "src"])
        (self.root / "src" / "b.h").write_text("inline int b() { return 2; }\n")
        self.assertNotEqual(
            before, render_provenance.source_digest(self.root, [self.root / "src"]))

    def test_build_output_is_not_part_of_the_digest(self):
        before = render_provenance.source_digest(self.root, [self.root / "src"])
        (self.root / "src" / "build").mkdir()
        (self.root / "src" / "build" / "generated.h").write_text("whatever\n")
        self.assertEqual(
            before, render_provenance.source_digest(self.root, [self.root / "src"]))

    def test_a_renamed_file_moves_the_digest(self):
        before = render_provenance.source_digest(self.root, [self.root / "src"])
        (self.root / "src" / "a.cpp").rename(self.root / "src" / "c.cpp")
        self.assertNotEqual(
            before, render_provenance.source_digest(self.root, [self.root / "src"]))

    def test_a_missing_directory_digests_to_nothing_rather_than_to_empty(self):
        self.assertIsNone(render_provenance.tree_digest(self.root / "not-here"))


class RecordAndCompare(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.root = Path(self.dir.name)
        self.sources = self.root / "sources"
        self.sources.mkdir()
        (self.sources / "display_list.cpp").write_text("// the render pass\n")
        self.cases = self.root / "cases"
        (self.cases / "L0-props").mkdir(parents=True)
        (self.cases / "L0-props" / "L0-props-local.json").write_text('{"id": "local"}\n')
        self.dumps = self.root / "gdi-dumps"
        self.dumps.mkdir()

    def tearDown(self):
        self.dir.cleanup()

    def make(self, **overrides):
        arguments = dict(
            repository=self.root, sources=[self.sources], cases=self.cases,
            fonts=None, theme_resources=None, sidecar_schema=2)
        arguments.update(overrides)
        return render_provenance.record(**arguments)

    def test_a_record_of_the_inputs_it_was_written_from_agrees(self):
        render_provenance.write(self.dumps, self.make())
        recorded = render_provenance.read(self.dumps)
        self.assertEqual(render_provenance.disagreements(recorded, self.make()), [])

    def test_the_record_lands_inside_the_dumps_it_describes(self):
        render_provenance.write(self.dumps, self.make())
        self.assertTrue((self.dumps / render_provenance.PROVENANCE_NAME).is_file())

    def test_a_dump_root_with_no_record_is_not_readable_as_one(self):
        self.assertIsNone(render_provenance.read(self.dumps))

    def test_a_changed_source_is_named(self):
        recorded = self.make()
        (self.sources / "display_list.cpp").write_text("// the render pass, changed\n")
        problems = render_provenance.disagreements(recorded, self.make())
        self.assertTrue(any("harness sources changed" in p for p in problems), problems)

    def test_a_regenerated_corpus_is_named(self):
        recorded = self.make()
        (self.cases / "L0-props" / "L0-props-local.json").write_text('{"id": "moved"}\n')
        problems = render_provenance.disagreements(recorded, self.make())
        self.assertTrue(any("case corpus changed" in p for p in problems), problems)

    def test_an_added_case_is_named(self):
        recorded = self.make()
        (self.cases / "L0-props" / "L0-props-new.json").write_text('{"id": "new"}\n')
        problems = render_provenance.disagreements(recorded, self.make())
        self.assertTrue(any("case corpus changed" in p for p in problems), problems)

    def test_a_corpus_that_is_gone_is_unverifiable_rather_than_fine(self):
        recorded = self.make()
        for path in sorted(self.cases.rglob("*"), reverse=True):
            path.unlink() if path.is_file() else path.rmdir()
        self.cases.rmdir()
        problems = render_provenance.disagreements(recorded, self.make())
        self.assertTrue(any("cannot be shown to correspond" in p for p in problems), problems)

    def test_a_different_corpus_directory_is_named(self):
        recorded = self.make()
        other = self.root / "other-cases"
        (other / "L0-props").mkdir(parents=True)
        (other / "L0-props" / "L0-props-local.json").write_text('{"id": "local"}\n')
        problems = render_provenance.disagreements(recorded, self.make(cases=other))
        self.assertTrue(any("were generated from" in p for p in problems), problems)

    def test_an_older_sidecar_schema_is_named(self):
        recorded = self.make(sidecar_schema=1)
        problems = render_provenance.disagreements(recorded, self.make())
        self.assertTrue(any("sidecar schema" in p for p in problems), problems)

    def test_an_older_provenance_schema_is_named(self):
        recorded = self.make()
        recorded["schema_version"] = render_provenance.SCHEMA_VERSION - 1
        problems = render_provenance.disagreements(recorded, self.make())
        self.assertTrue(any("provenance schema" in p for p in problems), problems)

    def test_the_record_is_json_a_human_can_read_in_a_diff(self):
        path = render_provenance.write(self.dumps, self.make())
        payload = json.loads(path.read_text())
        for key in ("generator_digest", "cases_digest", "cases_path", "sidecar_schema"):
            self.assertIn(key, payload)


if __name__ == "__main__":
    unittest.main()
