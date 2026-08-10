import json
import sys
import tempfile
import unittest
from pathlib import Path


SCRIPTS = Path(__file__).parents[1] / "scripts"
sys.path.insert(0, str(SCRIPTS))
import generate_render_cases as render_cases  # noqa: E402


class GenerateRenderCasesTests(unittest.TestCase):
    def test_corpus_is_small_unique_and_covers_each_boundary(self):
        ids = [case["id"] for case in render_cases.CASES]
        boundaries = {item for case in render_cases.CASES for item in case["boundaries"]}
        self.assertEqual(8, len(ids))
        self.assertEqual(len(ids), len(set(ids)))
        self.assertTrue(
            {"pixels", "transform-to-root", "clip", "opacity", "z-index", "text-rasterization"}
            <= boundaries
        )

    def test_generation_is_byte_deterministic_and_valid(self):
        with tempfile.TemporaryDirectory() as first, tempfile.TemporaryDirectory() as second:
            render_cases.generate(Path(first))
            render_cases.generate(Path(second))
            first_files = sorted(Path(first).glob("*.json"))
            second_files = sorted(Path(second).glob("*.json"))
            self.assertEqual([p.name for p in first_files], [p.name for p in second_files])
            for a, b in zip(first_files, second_files):
                self.assertEqual(a.read_bytes(), b.read_bytes())
                doc = json.loads(a.read_text(encoding="utf-8"))
                self.assertEqual(1, doc["schema_version"])
                self.assertEqual(2, len(doc["render_size"]))
                self.assertTrue(all(isinstance(value, int) and value > 0 for value in doc["render_size"]))
                self.assertEqual(doc["render_size"], doc["environment"]["available_size"])
                self.assertEqual(1.0, doc["environment"]["dpi_scale"])
                self.assertIn("http://schemas.microsoft.com/winfx/2006/xaml/presentation", doc["markup"])

    def test_generation_removes_only_stale_json(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            (out / "stale.json").write_text("{}", encoding="utf-8")
            (out / "keep.bin").write_bytes(b"local artifact")
            render_cases.generate(out)
            self.assertFalse((out / "stale.json").exists())
            self.assertEqual(b"local artifact", (out / "keep.bin").read_bytes())


if __name__ == "__main__":
    unittest.main()
