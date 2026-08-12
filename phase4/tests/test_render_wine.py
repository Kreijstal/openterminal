"""The Wine side of the render gate, when there is a Wine side to check.

The round trip itself is checked without Wine -- the render core is plain C++17
and phase3/render's own ctest and test_check_render.py cover it. What needs Wine
is what only a platform can do: put glyphs on a surface, and put a surface in a
window. Neither is in the repository, so this is an integration gate like
test_boot_frontier.py beside it, and it *skips by name* rather than passing
vacuously when the dumps are not there.

    python3 phase3/scripts/build_render.py --cases phase3/xaml-db/cases \\
        --fonts "$(python3 phase3/scripts/fetch_measurements.py --fonts)" \\
        --theme-resources <the extracted WinUI dictionary> \\
        --window-case L7-terminal-0e66f8e18d-s0

produces everything this reads.

Nothing here regenerates the dumps, and that is the hazard this file has to
answer for: through waves 5 and 6 it reported green over a dump root written
before either wave, because "the dumps are there" was the only question it knew
how to ask. It now asks a second one first -- were these dumps written by *this*
checkout, from *this* corpus -- and fails when the answer is no. An absent dump
root is still a skip by name; a present one that cannot be shown to be current
is a failure, because that is the state that produced two waves of false green.
"""

import json
import os
import subprocess
import sys
import unittest
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parents[2]
CHECKER = REPOSITORY / "phase4" / "scripts" / "check_render.py"
ROOT = Path(os.environ.get("OPENTERMINAL_RENDER_ROOT", "/tmp/openterminal-render"))
DUMPS = ROOT / "gdi-dumps"

sys.path.insert(0, str(REPOSITORY / "phase3" / "scripts"))

import render_provenance  # noqa: E402

PHASE3 = REPOSITORY / "phase3"
SIDECAR_SCHEMA = 3


def provenance_problems() -> list[str]:
    """Every reason these dumps do not correspond to this checkout's inputs."""
    recorded = render_provenance.read(DUMPS)
    if recorded is None:
        return [
            f"{DUMPS} has no {render_provenance.PROVENANCE_NAME}: it was written by a "
            "build_render.py that did not record one, so nothing can say which sources "
            "or cases it came from"
        ]
    current = render_provenance.record(
        repository=REPOSITORY,
        sources=render_provenance.source_roots(PHASE3),
        cases=Path(recorded.get("cases_path") or (PHASE3 / "xaml-db" / "cases")),
        fonts=None,
        theme_resources=None,
        sidecar_schema=SIDECAR_SCHEMA,
    )
    return render_provenance.disagreements(recorded, current)


class WineRenderGate(unittest.TestCase):
    def test_the_gdi_dumps_were_written_by_this_checkout(self):
        if not DUMPS.is_dir():
            raise unittest.SkipTest(
                f"no GDI dumps: {DUMPS} does not exist; run phase3/scripts/build_render.py")
        problems = provenance_problems()
        self.assertEqual(
            problems, [],
            "these dumps are stale and the round-trip result over them means nothing:\n  "
            + "\n  ".join(problems)
            + "\nregenerate with phase3/scripts/build_render.py",
        )

    def test_the_gdi_dumps_round_trip_exactly(self):
        if not DUMPS.is_dir():
            raise unittest.SkipTest(
                f"no GDI dumps: {DUMPS} does not exist; run phase3/scripts/build_render.py")
        # Not a skip: a dump root that exists but cannot be shown to be this
        # checkout's is precisely the false green this gate is here to stop.
        problems = provenance_problems()
        self.assertEqual(
            problems, [],
            "refusing to round-trip stale dumps:\n  " + "\n  ".join(problems))
        report = ROOT / "gdi-render-report.json"
        arguments = [sys.executable, "-B", str(CHECKER), "--dumps", str(DUMPS),
                     "--output", str(report)]
        # When the dumps were painted with recorded glyph outlines, the checker
        # must see the same directory, so its artifact-present-family pin holds
        # every outline-backed run to painting or a named refusal. A recorded
        # directory that is gone is a failure, not a quiet unpinned pass: the
        # pin is the difference between the flip being enforced and observed.
        recorded = render_provenance.read(DUMPS)
        outlines = recorded.get("glyph_outlines_path")
        if outlines is not None:
            self.assertTrue(
                Path(outlines).is_dir(),
                f"these dumps were painted from recorded outlines at {outlines}, "
                "which no longer exists; the outline pin cannot be checked, so the "
                "round trip must not be believed -- re-fetch the artifact or "
                "regenerate the dumps")
            arguments += ["--glyph-outlines", outlines]
        completed = subprocess.run(arguments, capture_output=True, text=True, check=False)
        self.assertTrue(report.is_file(), completed.stdout + completed.stderr)
        numbers = json.loads(report.read_text())
        self.assertEqual(
            numbers["failed"], 0,
            "the GDI dumps do not re-derive from the layout:\n"
            + json.dumps(numbers["failures"][:3], indent=1))
        # A run that painted nothing at all would report zero failures too.
        self.assertGreater(numbers["painted_exact"], 0, "no case painted; the gate is vacuous")
        # And a run that stopped halfway would report zero failures over the
        # part it reached. The corpus size is recorded beside the dumps, so the
        # count is checkable rather than assumed.
        recorded = render_provenance.read(DUMPS)
        self.assertEqual(
            numbers["total"], recorded["cases_count"],
            f"the dumps account for {numbers['total']} of the corpus's "
            f"{recorded['cases_count']} cases; the harness did not finish")
        print(f"\nGDI dumps: {numbers['painted_exact']} exact, {numbers['refused']} refused by "
              f"name, {numbers['not_laid_out']} not laid out, "
              f"{numbers['origins_not_re_derived']} visual origins not re-derived")

    def test_a_case_painted_in_a_window_is_the_case_painted_offscreen(self):
        windows = sorted(ROOT.glob("*.window.ppm"))
        if not windows:
            raise unittest.SkipTest(
                f"no window read-back under {ROOT}; run build_render.py --window-case")
        for window_dump in windows:
            case_id = window_dump.name[: -len(".window.ppm")]
            offscreen = DUMPS / f"{case_id}.ppm"
            with self.subTest(case=case_id):
                if not offscreen.is_file():
                    raise unittest.SkipTest(f"no offscreen dump for {case_id}")
                self.assertEqual(
                    window_dump.read_bytes(), offscreen.read_bytes(),
                    f"{case_id}: the window's pixels are not the offscreen dump's")

    def test_the_ink_samples_report_whether_the_derived_box_contains_the_glyphs(self):
        ink = ROOT / "ink-dumps"
        if not ink.is_dir():
            raise unittest.SkipTest(
                f"no ink samples under {ink}; run build_render.py --ink-font")
        report = ROOT / "ink-report.json"
        # Removed first: a report left behind by an earlier run would otherwise
        # answer for a checker invocation that refused to run at all, which is
        # the same trick a stale dump root plays on the gate above.
        report.unlink(missing_ok=True)
        completed = subprocess.run(
            [sys.executable, "-B", str(CHECKER), "--dumps", str(ink),
             "--trees", str(ink / "no-trees-here"), "--output", str(report)],
            capture_output=True, text=True, check=False)
        self.assertIn(
            completed.returncode, (0, 1),
            "the checker refused these ink samples rather than checking them:\n"
            + completed.stdout + completed.stderr)
        self.assertTrue(report.is_file(), completed.stdout + completed.stderr)
        numbers = json.loads(report.read_text())
        # A sample that refused to paint has no ink, so it has no text, so the
        # equality below holds at zero and the whole check evaporates. That is
        # not hypothetical: through wave 7 this test reported green over a
        # single dumped sample that had refused with `DirectWrite could not
        # resolve any requested family in "Cascadia Mono"`. Refusals are named
        # here, before anything is counted.
        self.assertEqual(
            numbers["refused"], 0,
            "these ink samples refused to paint rather than being checked:\n"
            + json.dumps(numbers["refusal_features"], indent=1))
        self.assertGreater(numbers["cases_with_text"], 0,
                           "no ink sample carried a text run; the check is vacuous")
        # Every sample must have had its ink looked at. Whether the ink fits is
        # the open question -- an antialiased glyph is allowed to spread past
        # the advance that placed it -- so this asserts the check *ran*, and the
        # failure count is reported rather than demanded to be zero. See
        # phase3/render/README.md; closing it needs a rendered-output probe.
        self.assertEqual(numbers["ink_checked"], numbers["cases_with_text"],
                         "an ink sample was dumped without its ink being checked")
        print(f"\nink samples: {numbers['painted_exact']} contained, "
              f"{numbers['failed']} with ink outside the derived box")

    def test_the_recorded_outlines_reproduce_directwrites_coverage(self):
        """The recorded-versus-live gate, read off outline_compare's report.

        Containment proves the outline painter's ink lands in the measured
        box; only this comparison can say the shapes are the recorded font's.
        build_render.py runs the tool when given both --ink-font and
        --glyph-outlines and fails the run on a mismatch; this reads the
        report so the claim is also enforced wherever the test suite runs
        over the same root, and says by name when no comparison was made.
        """
        report = ROOT / "outline-comparison.json"
        if not report.is_file():
            raise unittest.SkipTest(
                f"no outline comparison under {report}; run build_render.py with "
                "--ink-font and --glyph-outlines")
        numbers = json.loads(report.read_text())
        self.assertGreater(numbers["outlines_checked"], 0,
                           "no recorded outline was checked; the gate is vacuous")
        self.assertEqual(
            numbers["outline_mismatches"], [],
            "the recording is not what GetGlyphRunOutline answers over the same "
            "file here")
        self.assertGreater(numbers["compared"], 0,
                           "the comparison compared nothing; the gate is vacuous")
        self.assertEqual(
            numbers["failed"], 0,
            "the recorded outlines do not reproduce DirectWrite's coverage over "
            "the same font file:\n" + json.dumps(
                [s for s in numbers["samples"] if not s["passed"]][:5], indent=1))
        print(f"\noutline comparison: {numbers['outlines_checked']} outline(s) "
              f"matched the live boundary and {numbers['compared']} painted "
              f"sample(s) in \"{numbers['family']}\" agreed within "
              f"{numbers['tolerance_px']}px")


if __name__ == "__main__":
    unittest.main()
