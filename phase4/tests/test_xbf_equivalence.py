"""The XBF loader agrees with the text path, and its driver says what it did.

Two kinds of test live here, and they are kept apart on purpose.

The *unit* tests need nothing but the repository: they hold the genxbf driver's
project generation and its diagnostic parsing to what they claim, because those
are the two places where a silent mistake would turn "the compiler rejected this
page" into "no page was ever compiled".

The *gate* is the whole corpus through the real compiler and back through the
loader. It needs the harvested Windows SDK XAML compiler, a Wine prefix with
.NET 4, and the native build -- none of which is in the repository. When any of
them is absent the test skips **by name**, saying which one; it never passes
vacuously, because a vacuous pass here would be indistinguishable from a loader
that decodes nothing.
"""

import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPOSITORY / "phase4" / "scripts"))

import genxbf_driver  # noqa: E402
import xbf_equivalence  # noqa: E402

CASES = REPOSITORY / "phase3" / "xaml-db" / "cases"
FONTS = REPOSITORY / "phase3" / "xaml-db" / "fonts" / "derived"
TOOL = Path(
    os.environ.get("OPENXAML_XBF_EQUIVALENCE",
                   "/tmp/openterminal-xbf-build/xbf_equivalence"))
WORK = Path(os.environ.get("OPENXAML_XBF_WORK", "/tmp/openterminal-xbf-gate"))


class Driver(unittest.TestCase):
    """The parts of the genxbf driver that do not need genxbf."""

    def test_diagnostics_are_attributed_to_the_page_that_caused_them(self):
        log = (
            "Build started.\n"
            r"Z:\tmp\pages\L5-xdirectives-uid.xaml(1,42): XAML error WMC0001: "
            "Unknown member 'Nope'.\n"
            r"Z:\tmp\pages\other.xaml(3,7): XAML warning WMC1502: ignore me\n"
            r"Z:\tmp\pages\L5-xdirectives-uid.xaml(1,42): XAML error WMC0001: "
            "Unknown member 'Nope'.\n"
        )
        found = genxbf_driver.parse_diagnostics(log)
        self.assertEqual(list(found), ["L5-xdirectives-uid.xaml"])
        # Warnings are not errors, and the same error twice is one error.
        self.assertEqual(found["L5-xdirectives-uid.xaml"],
                         ["WMC0001: Unknown member 'Nope'."])

    def test_a_page_with_no_class_is_marked_as_a_standalone_dictionary(self):
        # Without this the compiler's second pass dereferences a null class
        # name. It is the single fact that makes corpus markup compilable at
        # all, so it is checked rather than trusted to a comment.
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            page = work / "Case.xaml"
            page.write_text("<Border xmlns='urn:x'/>", encoding="utf-8")
            anchor = work / "anchor.proj"
            anchor.write_text(
                "<Project>\n  <ItemGroup>\n  </ItemGroup>\n"
                f'  <Target Name="Pass1"><Message Text="Z:{work}\\out" /></Target>\n'
                "</Project>\n",
                encoding="utf-8",
            )
            tools = {"anchor_project": anchor, "root": work}
            # The anchor's output directory is retargeted by plain substitution,
            # so the driver has to find it there.
            anchor.write_text(
                anchor.read_text(encoding="utf-8").replace(
                    f"Z:{work}\\out", genxbf_driver.wine_path(work / genxbf_driver.ANCHOR_OUTPUT)),
                encoding="utf-8",
            )
            project = work / "generated.proj"
            genxbf_driver.write_project(project, [page], tools, work / "out")
            text = project.read_text(encoding="utf-8")
            self.assertIn("<Type>DefaultStyle</Type>", text)
            self.assertIn(genxbf_driver.wine_path(page), text)
            self.assertIn(genxbf_driver.wine_path(work / "out"), text)
            self.assertNotIn(genxbf_driver.ANCHOR_OUTPUT, text)

    def test_a_missing_toolchain_is_named_rather_than_reported_as_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(genxbf_driver.ToolchainMissing) as raised:
                genxbf_driver.toolchain(root=Path(directory))
            self.assertIn("anchor_project", str(raised.exception))


class Equivalence(unittest.TestCase):
    """The corpus through genxbf and back, or a named skip."""

    def _requirements(self):
        if not TOOL.is_file():
            raise unittest.SkipTest(
                f"native xbf_equivalence absent: {TOOL} does not exist "
                "(cmake -S phase3/layout -B <build> && cmake --build <build>)")
        if not CASES.is_dir() or not any(CASES.glob("*/*.json")):
            raise unittest.SkipTest(
                f"corpus absent: run python3 -B phase3/scripts/generate_cases.py")
        compiled = WORK / "xbf"
        report = WORK / "compile.json"
        if not compiled.is_dir() or not report.is_file():
            try:
                genxbf_driver.toolchain()
            except genxbf_driver.ToolchainMissing as error:
                raise unittest.SkipTest(str(error))
            raise unittest.SkipTest(
                f"the corpus has not been compiled yet: {compiled} is empty "
                "(run python3 -B phase4/scripts/xbf_equivalence.py --tool <build>/xbf_equivalence)")
        return compiled, json.loads(report.read_text(encoding="utf-8"))

    def test_every_compiled_case_measures_identically_through_both_paths(self):
        compiled, driver_report = self._requirements()
        result = subprocess.run(
            [str(TOOL), str(CASES), str(compiled), str(FONTS)],
            capture_output=True, text=True)
        self.assertIn(result.returncode, (0, 1), result.stderr)
        report = json.loads(result.stdout)
        self.assertEqual(report["mismatched"], [],
                         "the two markup paths measured differently")
        self.assertEqual(report["loader_refused"], [],
                         "the XBF loader refused a case the text path measured")
        self.assertEqual(report["identical"], report["cases"])
        # The gate is only as strong as its coverage: if the compile produced
        # almost nothing, an empty mismatch list means nothing.
        self.assertEqual(report["cases"], len(driver_report["compiled"]),
                         "the gate saw a different number of cases than the compiler produced")
        self.assertGreater(report["cases"], 900,
                           "too few cases compiled for this to be a corpus-wide gate")
        # Agreeing on a refusal is agreement, but it is not a measurement. A run
        # where most of the corpus stopped measuring would still show an empty
        # mismatch list, so the measured half is asserted separately.
        self.assertGreater(report["identical_measured"], 800,
                           "most of the corpus agreed on a refusal rather than on a measurement")

    def test_the_gate_is_deterministic(self):
        compiled, _ = self._requirements()
        runs = []
        for _ in range(2):
            result = subprocess.run(
                [str(TOOL), str(CASES), str(compiled), str(FONTS)],
                capture_output=True, text=True)
            self.assertIn(result.returncode, (0, 1), result.stderr)
            runs.append(result.stdout)
        self.assertEqual(runs[0], runs[1],
                         "two runs of the equivalence gate disagreed")

    def test_the_cases_genxbf_rejected_are_recorded_with_its_own_error(self):
        _, driver_report = self._requirements()
        for name, messages in driver_report["errors"].items():
            self.assertTrue(messages, f"{name} was excluded with no reason")
            for message in messages:
                self.assertNotEqual(message, "", f"{name} has an empty reason")


if __name__ == "__main__":
    unittest.main()
