import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch


SCRIPTS = Path(__file__).parents[1] / "scripts"
sys.path.insert(0, str(SCRIPTS))
import harvest_dwrite_glyph_runs as glyphs  # noqa: E402


class HarvestDwriteGlyphRunsTests(unittest.TestCase):
    def test_extracts_only_explicit_text_from_generated_corpus(self):
        import generate_render_cases

        with tempfile.TemporaryDirectory() as temporary:
            cases = Path(temporary)
            generate_render_cases.generate(cases)
            runs = glyphs.text_runs(cases)
            self.assertEqual(4, len(runs))
            self.assertEqual("Hamburgefontsiv 0123", runs[0].text)
            self.assertTrue(all(run.family == "Segoe UI" for run in runs))

    def test_harvest_is_data_driven_and_has_no_machine_path(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            cases = root / "cases"
            cases.mkdir()
            (cases / "one.json").write_text(
                json.dumps(
                    {
                        "id": "one",
                        "markup": '<TextBlock xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" Text="abc" FontFamily="Face" FontSize="12"/>',
                    }
                ),
                encoding="utf-8",
            )
            native = {
                "schema_version": 1,
                "family": "Face",
                "font_size": 12,
                "locale": "en-US",
                "text": "abc",
                "runs": [{"glyph_indices": [1, 2, 3]}],
            }
            completed = subprocess_result(stdout=json.dumps(native))
            output = root / "out" / "glyphs.json"
            with patch.object(glyphs.subprocess, "run", return_value=completed) as run:
                glyphs.harvest(cases, Path("probe.exe"), output)
            self.assertEqual(
                ["probe.exe", "Face", "12", "en-US", "abc"], run.call_args.args[0]
            )
            self.assertNotIn(str(root), output.read_text(encoding="utf-8"))

    def test_rejects_non_normal_text_until_probe_models_it(self):
        with tempfile.TemporaryDirectory() as temporary:
            cases = Path(temporary)
            (cases / "one.json").write_text(
                json.dumps(
                    {
                        "id": "one",
                        "markup": '<TextBlock xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" Text="abc" FontFamily="Face" FontSize="12" FontWeight="Bold"/>',
                    }
                ),
                encoding="utf-8",
            )
            with self.assertRaisesRegex(glyphs.GlyphHarvestError, "FontWeight"):
                glyphs.text_runs(cases)


def subprocess_result(*, stdout: str, returncode: int = 0, stderr: str = ""):
    class Result:
        pass

    result = Result()
    result.stdout = stdout
    result.stderr = stderr
    result.returncode = returncode
    return result


if __name__ == "__main__":
    unittest.main()
