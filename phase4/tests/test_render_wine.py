"""The Wine side of the render gate, when there is a Wine side to check.

The round trip itself is checked without Wine -- the render core is plain C++17
and phase3/render's own ctest and test_check_render.py cover it. What needs Wine
is what only a platform can do: put glyphs on a surface, and put a surface in a
window. Neither is in the repository, so this is an integration gate like
test_boot_frontier.py beside it, and it *skips by name* rather than passing
vacuously when the dumps are not there.

    python3 phase3/scripts/build_render.py --cases phase3/xaml-db/cases \\
        --fonts <a fonts directory> --window-case L7-terminal-0e66f8e18d-s0

produces everything this reads.
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


class WineRenderGate(unittest.TestCase):
    def test_the_gdi_dumps_round_trip_exactly(self):
        if not DUMPS.is_dir():
            raise unittest.SkipTest(
                f"no GDI dumps: {DUMPS} does not exist; run phase3/scripts/build_render.py")
        report = ROOT / "gdi-render-report.json"
        completed = subprocess.run(
            [sys.executable, "-B", str(CHECKER), "--dumps", str(DUMPS),
             "--output", str(report)],
            capture_output=True, text=True, check=False)
        self.assertTrue(report.is_file(), completed.stdout + completed.stderr)
        numbers = json.loads(report.read_text())
        self.assertEqual(
            numbers["failed"], 0,
            "the GDI dumps do not re-derive from the layout:\n"
            + json.dumps(numbers["failures"][:3], indent=1))
        # A run that painted nothing at all would report zero failures too.
        self.assertGreater(numbers["painted_exact"], 0, "no case painted; the gate is vacuous")

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
        subprocess.run(
            [sys.executable, "-B", str(CHECKER), "--dumps", str(ink),
             "--trees", str(ink / "no-trees-here"), "--output", str(report)],
            capture_output=True, text=True, check=False)
        self.assertTrue(report.is_file())
        numbers = json.loads(report.read_text())
        # Every sample must have had its ink looked at. Whether the ink fits is
        # the open question -- GDI rounds a font's metrics to whole pixels and
        # the recorded runtime does not -- so this asserts the check *ran*, and
        # the failure count is reported rather than demanded to be zero. See
        # phase3/render/README.md; closing it needs a rendered-output probe.
        self.assertEqual(numbers["ink_checked"], numbers["cases_with_text"],
                         "an ink sample was dumped without its ink being checked")
        print(f"\nink samples: {numbers['painted_exact']} contained, "
              f"{numbers['failed']} with ink outside the derived box")


if __name__ == "__main__":
    unittest.main()
