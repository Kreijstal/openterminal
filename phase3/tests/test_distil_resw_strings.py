#!/usr/bin/env python3

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT_DIRECTORY = Path(__file__).resolve().parents[1] / "scripts"
sys.path.insert(0, str(SCRIPT_DIRECTORY))

from distil_resw_strings import (  # noqa: E402
    canonical_remote,
    distil,
    read_file,
    split_key,
)


def resw(entries: list[tuple[str, str]]) -> str:
    body = "".join(
        f'<data name="{name}" xml:space="preserve"><value>{value}</value></data>'
        for name, value in entries
    )
    return f"<root>{body}</root>"


class SplitKeyTest(unittest.TestCase):
    def test_uid_and_property(self) -> None:
        self.assertEqual(split_key("SaveButton.Content"), ("SaveButton", "Content"))

    def test_using_clause_is_stripped(self) -> None:
        # It names the metadata the property comes from and is not part of the
        # spelling an attribute would use.
        self.assertEqual(
            split_key("Foo.[using:Windows.UI.Xaml.Automation]AutomationProperties.Name"),
            ("Foo", "AutomationProperties.Name"),
        )

    def test_attached_property_without_a_using_clause(self) -> None:
        self.assertEqual(
            split_key("Foo.ToolTipService.ToolTip"), ("Foo", "ToolTipService.ToolTip")
        )

    def test_a_bare_key_names_no_uid(self) -> None:
        # A string a code path asks for by name. It sets nothing in markup.
        self.assertIsNone(split_key("ErrorTitle"))

    def test_a_property_half_that_is_not_a_property_path(self) -> None:
        # Would have to mean a uid containing a dot, which the split cannot
        # tell from a three-part property path. Reported, not guessed at.
        self.assertIsNone(split_key("A.B.C.D"))

    def test_an_empty_uid_half(self) -> None:
        self.assertIsNone(split_key(".Content"))


class ReadFileTest(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)

    def write(self, text: str) -> Path:
        path = self.root / "Resources.resw"
        path.write_text(text, encoding="utf-8")
        return path

    def test_values_are_taken_verbatim(self) -> None:
        # xml:space="preserve" is the whole point of a .resw: a trailing space
        # in a label is a translator's decision, and a string tidied here would
        # measure differently from the one Terminal ships.
        path = self.write(resw([("A.Text", "  padded  ")]))
        self.assertEqual(read_file(path), [("A.Text", "  padded  ")])

    def test_an_empty_value_is_an_empty_string(self) -> None:
        path = self.write('<root><data name="A.Text"><value/></data></root>')
        self.assertEqual(read_file(path), [("A.Text", "")])

    def test_entities_are_decoded(self) -> None:
        path = self.write(resw([("A.Text", "a &amp; b")]))
        self.assertEqual(read_file(path), [("A.Text", "a & b")])


class DistilTest(unittest.TestCase):
    """End to end over a miniature checkout."""

    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.repo = Path(self.directory.name)
        subprocess.run(["git", "init", "-q", str(self.repo)], check=True)
        subprocess.run(
            ["git", "-C", str(self.repo), "remote", "add", "origin",
             "git@github.com:microsoft/terminal.git"],
            check=True,
        )
        subprocess.run(
            ["git", "-C", str(self.repo), "commit", "-q", "--allow-empty",
             "-m", "empty"],
            check=True,
            env={"GIT_AUTHOR_NAME": "t", "GIT_AUTHOR_EMAIL": "t@t",
                 "GIT_COMMITTER_NAME": "t", "GIT_COMMITTER_EMAIL": "t@t",
                 "PATH": "/usr/bin:/bin"},
        )

    def write(self, project: str, locale: str, entries: list[tuple[str, str]]) -> None:
        path = self.repo / "src" / "cascadia" / project / "Resources" / locale / "Resources.resw"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(resw(entries), encoding="utf-8")

    def test_splits_a_table_into_its_three_kinds(self) -> None:
        self.write("App", "en-US", [
            ("SaveButton.Content", "Save"),
            ("SaveButton.[using:Windows.UI.Xaml.Automation]AutomationProperties.Name", "Save"),
            ("Caption.Text", "Hello"),
            ("ErrorTitle", "Something went wrong"),
        ])
        table = distil(self.repo, "en-US")
        self.assertEqual(table["strings"], {
            "SaveButton": {"AutomationProperties.Name": "Save", "Content": "Save"},
            "Caption": {"Text": "Hello"},
        })
        self.assertEqual(list(table["code_keys"]), ["ErrorTitle"])
        self.assertEqual(table["totals"],
                         {"uids": 2, "uid_properties": 3, "code_keys": 1, "unsplittable": 0})

    def test_merges_projects_and_records_every_file(self) -> None:
        self.write("App", "en-US", [("A.Text", "one")])
        self.write("Control", "en-US", [("B.Text", "two")])
        table = distil(self.repo, "en-US")
        self.assertEqual(sorted(table["strings"]), ["A", "B"])
        self.assertEqual(len(table["source"]["files"]), 2)

    def test_only_the_named_locale_is_read(self) -> None:
        self.write("App", "en-US", [("A.Text", "one")])
        self.write("App", "de-DE", [("A.Text", "eins")])
        self.assertEqual(distil(self.repo, "en-US")["strings"], {"A": {"Text": "one"}})
        self.assertEqual(distil(self.repo, "de-DE")["strings"], {"A": {"Text": "eins"}})

    def test_the_same_key_defined_differently_twice_is_fatal(self) -> None:
        # Deterministic order would hide the conflict rather than settle it:
        # whichever file sorts first is not an answer to which map a page gets.
        self.write("App", "en-US", [("A.Text", "one")])
        self.write("Control", "en-US", [("A.Text", "two")])
        with self.assertRaises(SystemExit) as caught:
            distil(self.repo, "en-US")
        self.assertIn("A.Text", str(caught.exception))

    def test_the_same_key_defined_identically_twice_is_not(self) -> None:
        self.write("App", "en-US", [("A.Text", "one")])
        self.write("Control", "en-US", [("A.Text", "one")])
        self.assertEqual(distil(self.repo, "en-US")["strings"], {"A": {"Text": "one"}})

    def test_an_unsplittable_key_is_named_and_not_assigned(self) -> None:
        self.write("App", "en-US", [("A.B.C.D", "?"), ("Fine.Text", "yes")])
        table = distil(self.repo, "en-US")
        self.assertEqual(table["strings"], {"Fine": {"Text": "yes"}})
        self.assertEqual(table["unsplittable"],
                         [{"key": "A.B.C.D", "file": "src/cascadia/App/Resources/en-US/Resources.resw"}])

    def test_a_resw_outside_src_cascadia_is_not_read(self) -> None:
        path = self.repo / "dep" / "Resources" / "en-US" / "Resources.resw"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(resw([("A.Text", "vendored")]), encoding="utf-8")
        self.write("App", "en-US", [("B.Text", "ours")])
        self.assertEqual(list(distil(self.repo, "en-US")["strings"]), ["B"])

    def test_no_tables_at_all_is_fatal(self) -> None:
        with self.assertRaises(SystemExit):
            distil(self.repo, "en-US")

    def test_output_is_byte_identical_across_runs(self) -> None:
        # The table is generated and untracked, so a run that differed from the
        # last one would be indistinguishable from Terminal having changed.
        self.write("App", "en-US", [("B.Text", "two"), ("A.Text", "one")])
        first = json.dumps(distil(self.repo, "en-US"), indent=1, sort_keys=True)
        second = json.dumps(distil(self.repo, "en-US"), indent=1, sort_keys=True)
        self.assertEqual(first, second)

    def test_records_the_commit_it_read(self) -> None:
        self.write("App", "en-US", [("A.Text", "one")])
        source = distil(self.repo, "en-US")["source"]
        self.assertEqual(source["repository"], "https://github.com/microsoft/terminal")
        self.assertEqual(len(source["commit"]), 40)
        self.assertEqual(source["locale"], "en-US")


class CanonicalRemoteTest(unittest.TestCase):
    def test_every_spelling_agrees(self) -> None:
        expected = "https://github.com/microsoft/terminal"
        for url in (
            "git@github.com:microsoft/terminal.git",
            "https://github.com/microsoft/terminal",
            "https://github.com/microsoft/terminal.git/",
            "ssh://git@github.com/microsoft/terminal.git",
        ):
            self.assertEqual(canonical_remote(url), expected)


if __name__ == "__main__":
    unittest.main()
