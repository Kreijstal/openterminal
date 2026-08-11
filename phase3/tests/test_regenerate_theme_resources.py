#!/usr/bin/env python3
"""The local fallback for the theme-resource database.

Nothing here clones anything. What is under test is everything around the
clone: that the commits come from the extractors rather than being copied,
that a cached tree at the wrong commit is not quietly extracted from, that a
non-reproducible extractor is refused rather than believed, and that filling
in a half does not overwrite the half a CI artifact already provided.
"""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT_DIRECTORY = Path(__file__).resolve().parents[1] / "scripts"
sys.path.insert(0, str(SCRIPT_DIRECTORY))

import extract_default_styles as styles  # noqa: E402
import extract_winui_theme_resources as themes  # noqa: E402
import regenerate_theme_resources as regen  # noqa: E402


class PinsComeFromTheExtractorsTest(unittest.TestCase):
    """A re-pin must not be able to leave this script on the old tree."""

    def test_the_winui_pin_is_the_extractor_s(self) -> None:
        self.assertEqual(regen.WINUI.commit, themes.PINNED_COMMIT)

    def test_the_dxaml_pin_is_the_extractor_s(self) -> None:
        self.assertEqual(regen.DXAML.commit, styles.PINNED_DXAML_COMMIT)

    def test_the_dxaml_tree_is_the_one_holding_the_system_generic_xaml(self) -> None:
        # The sparse path is the whole reason the fallback is affordable, and
        # getting it wrong produces an empty checkout rather than an error.
        self.assertIn("dxaml/xcp/dxaml/themes", regen.DXAML.sparse)

    def test_default_styles_needs_both_trees(self) -> None:
        # It reconstructs the framework's generic.xaml *and* the muxc default
        # styles; a run with only one tree would silently produce half a
        # database.
        product = next(p for p in regen.PRODUCTS if p.filename == "default-styles.json")
        self.assertEqual({c.name for c in product.needs}, {"dxaml", "winui"})


class CheckoutActionTest(unittest.TestCase):
    PIN = "188f602b27cdb47572b28c380e9c087b02e1ccee"

    def test_no_tree_means_clone(self) -> None:
        self.assertEqual(regen.checkout_action(None, self.PIN), "clone")

    def test_the_pinned_tree_is_reused(self) -> None:
        self.assertEqual(regen.checkout_action(self.PIN, self.PIN), "reuse")

    def test_a_tree_at_another_commit_is_refetched_not_used(self) -> None:
        # The failure this exists to prevent: a cache from an earlier pin
        # extracted from happily, producing a database whose provenance names
        # a commit it was not read from.
        self.assertEqual(regen.checkout_action("4aa80ad6" + "0" * 32, self.PIN),
                         "refetch")


class ExtractIsCheckedForDeterminismTest(unittest.TestCase):
    """The same check CI makes: run it twice, compare, refuse a disagreement."""

    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.scripts = self.root / "scripts"
        self.scripts.mkdir()
        self._real_scripts = regen.SCRIPTS
        regen.SCRIPTS = self.scripts
        self.addCleanup(setattr, regen, "SCRIPTS", self._real_scripts)

    def write_extractor(self, name: str, body: str) -> None:
        (self.scripts / name).write_text(body, encoding="utf-8")

    def product(self, name: str) -> regen.Product:
        return regen.Product("thing.json", name, ("--tree", "{winui}", "--out", "{out}"),
                             (regen.WINUI,))

    def test_a_reproducible_extractor_leaves_one_file(self) -> None:
        self.write_extractor("stable.py", "import sys, pathlib\n"
                                          "pathlib.Path(sys.argv[4]).write_text('{}')\n")
        out, check = self.root / "out.json", self.root / "check.json"
        regen.extract(self.product("stable.py"), {"winui": self.root}, out, check)
        self.assertEqual(out.read_text(), "{}")
        self.assertFalse(check.exists(), "the second copy is not left lying around")

    def test_a_non_reproducible_extractor_is_refused(self) -> None:
        self.write_extractor("wobbly.py",
                             "import sys, pathlib, time\n"
                             "pathlib.Path(sys.argv[4]).write_text(str(time.time_ns()))\n")
        out, check = self.root / "out.json", self.root / "check.json"
        with self.assertRaises(SystemExit) as raised:
            regen.extract(self.product("wobbly.py"), {"winui": self.root}, out, check)
        self.assertIn("not deterministic", str(raised.exception))

    def test_an_extractor_that_fails_is_not_swallowed(self) -> None:
        self.write_extractor("broken.py", "import sys; sys.exit(3)\n")
        with self.assertRaises(subprocess.CalledProcessError):
            regen.extract(self.product("broken.py"), {"winui": self.root},
                          self.root / "out.json", self.root / "check.json")


class KeepExistingTest(unittest.TestCase):
    """Filling in the half the artifact lacks must not touch the half it has."""

    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.out = Path(self.tmp.name) / "theme-resources"
        self.out.mkdir()
        # As a CI artifact from run 31234396062 arrives: one half only.
        (self.out / "winui-2.8.4.json").write_text(
            json.dumps({"themes": {}, "source": "the artifact"}), encoding="utf-8")

    def test_nothing_to_do_is_said_and_not_done(self) -> None:
        exit_code = regen.main(["--out", str(self.out),
                                "--only", "winui-2.8.4.json", "--keep-existing"])
        self.assertEqual(exit_code, 0)
        # Untouched: no clone was attempted, and the artifact's copy survives.
        self.assertIn("the artifact",
                      (self.out / "winui-2.8.4.json").read_text(encoding="utf-8"))

    def test_comparing_still_happens_when_there_is_nothing_to_regenerate(self) -> None:
        # The state --compare-with is most often used in: an existing
        # regeneration held against a freshly downloaded artifact. An early
        # return on "nothing to regenerate" silently produced no report.
        artifact = Path(self.tmp.name) / "artifact"
        artifact.mkdir()
        (artifact / "winui-2.8.4.json").write_bytes(b"{}")
        import io
        import contextlib
        captured = io.StringIO()
        with contextlib.redirect_stdout(captured):
            exit_code = regen.main(["--out", str(self.out), "--only",
                                    "winui-2.8.4.json", "--keep-existing",
                                    "--compare-with", str(artifact)])
        self.assertEqual(exit_code, 0)
        self.assertIn("winui-2.8.4.json:", captured.getvalue())


class CompareWithTheArtifactTest(unittest.TestCase):
    """"Identical" and "identical apart from line terminators" are two claims."""

    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.mine = self.root / "mine.json"
        self.theirs = self.root / "mine.json.artifact"

    def test_byte_identical_is_said_plainly(self) -> None:
        self.mine.write_bytes(b'{\n "a": 1\n}\n')
        self.theirs.write_bytes(b'{\n "a": 1\n}\n')
        self.assertIn("byte-for-byte identical",
                      regen.compare(self.mine, self.theirs))

    def test_crlf_only_is_neither_identical_nor_a_difference(self) -> None:
        # What a Windows runner actually produces: the same document, opened
        # in text mode. Reporting it as "differs" sends a reader hunting for a
        # content change that is not there; reporting it as "identical" is a
        # lie about the bytes.
        self.mine.write_bytes(b'{\n "a": 1\n}\n')
        self.theirs.write_bytes(b'{\r\n "a": 1\r\n}\r\n')
        message = regen.compare(self.mine, self.theirs)
        self.assertIn("apart from line terminators", message)
        self.assertIn("3 CRLF", message)
        self.assertNotIn("DIFFERS", message)

    def test_a_real_content_difference_is_loud(self) -> None:
        self.mine.write_bytes(b'{\n "a": 1\n}\n')
        self.theirs.write_bytes(b'{\n "a": 2\n}\n')
        message = regen.compare(self.mine, self.theirs)
        self.assertIn("DIFFERS in content at byte", message)

    def test_a_content_difference_hiding_behind_crlf_is_still_caught(self) -> None:
        # The case that would make the CRLF arm dangerous if it were a
        # normalise-and-compare: line endings must not launder a changed value.
        self.mine.write_bytes(b'{\n "a": 1\n}\n')
        self.theirs.write_bytes(b'{\r\n "a": 2\r\n}\r\n')
        self.assertIn("DIFFERS", regen.compare(self.mine, self.theirs))

    def test_a_file_the_artifact_lacks_is_named(self) -> None:
        # Exactly the shape of the hole this script exists for: the green run
        # 31234396062 carries winui-2.8.4.json and no default-styles.json.
        self.mine.write_bytes(b"{}")
        self.assertIn("not in the artifact",
                      regen.compare(self.mine, self.root / "absent.json"))


if __name__ == "__main__":
    unittest.main()
