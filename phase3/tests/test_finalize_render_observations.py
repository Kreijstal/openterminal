import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path


SCRIPTS = Path(__file__).parents[1] / "scripts"
sys.path.insert(0, str(SCRIPTS))
import finalize_render_observations as observations  # noqa: E402


class FinalizeRenderObservationsTests(unittest.TestCase):
    def _write_capture(self, directory: Path, pixels: bytes = b"\x01\x02\x03\x00\x10\x20\x30\xff"):
        (directory / "case.bgra").write_bytes(pixels)
        (directory / "case.json").write_text(
            json.dumps(
                {
                    "schema_version": 1,
                    "case_id": "case",
                    "capture": {
                        "width": 2,
                        "height": 1,
                        "stride": 8,
                        "pixel_format": "BGRA8",
                        "alpha_mode": "premultiplied",
                        "pixels_file": "case.bgra",
                    },
                    "tree": [{"path": "/Grid"}],
                }
            ),
            encoding="utf-8",
        )

    def test_writes_reviewable_hash_and_pixel_statistics(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            pixels = b"\x01\x02\x03\x00\x10\x20\x30\xff"
            self._write_capture(directory, pixels)
            manifest = observations.finalize(directory)
            capture = manifest["captures"][0]
            self.assertEqual(hashlib.sha256(pixels).hexdigest(), capture["sha256"])
            self.assertEqual(1, capture["transparent_pixels"])
            self.assertEqual(1, capture["opaque_pixels"])
            self.assertEqual(0, capture["partial_alpha_pixels"])
            self.assertEqual(2, capture["distinct_bgra_values"])
            self.assertNotIn(str(directory), (directory / "manifest.json").read_text())

    def test_rejects_truncated_capture(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            self._write_capture(directory, b"short")
            with self.assertRaisesRegex(ValueError, "expected 8"):
                observations.finalize(directory)

    def test_includes_the_text_boundary_without_treating_it_as_pixels(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            self._write_capture(directory)
            glyphs = {
                "schema_version": 1,
                "observations": [{"case_id": "case", "directwrite": {"runs": []}}],
            }
            data = (json.dumps(glyphs) + "\n").encode()
            (directory / "dwrite-glyph-runs.json").write_bytes(data)
            manifest = observations.finalize(directory)
            self.assertEqual(1, manifest["directwrite_glyph_runs"]["observations"])
            self.assertEqual(
                hashlib.sha256(data).hexdigest(),
                manifest["directwrite_glyph_runs"]["sha256"],
            )

    def test_rejects_machine_path_in_observation(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            self._write_capture(directory)
            path = directory / "case.json"
            doc = json.loads(path.read_text())
            doc["capture"]["pixels_file"] = "C:/runner/case.bgra"
            path.write_text(json.dumps(doc), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "basename"):
                observations.finalize(directory)


if __name__ == "__main__":
    unittest.main()
