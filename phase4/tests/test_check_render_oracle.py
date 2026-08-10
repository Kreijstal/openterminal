"""The native acceptance gate must fail every renderer mismatch it names."""

import json
import io
import hashlib
import sys
import tempfile
import unittest
from pathlib import Path
from contextlib import redirect_stdout
from unittest.mock import patch


REPOSITORY = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPOSITORY / "phase4" / "scripts"))
import check_render_oracle as oracle  # noqa: E402


TYPE = "Windows.UI.Xaml.Controls.Grid"


class Fixture:
    def __init__(self, root: Path):
        self.oracle = root / "oracle"
        self.actual = root / "actual"
        (self.actual / "trees").mkdir(parents=True)
        self.oracle.mkdir()
        self.pixels = bytes((1, 2, 3, 255, 5, 6, 7, 255))
        (self.oracle / "case.bgra").write_bytes(self.pixels)
        self.glyph_data = (json.dumps({"schema_version": 1, "observations": []}) + "\n").encode()
        (self.oracle / "dwrite-glyph-runs.json").write_bytes(self.glyph_data)
        self.native = {
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
            "tree": [
                {
                    "path": "/Grid",
                    "type": TYPE,
                    "desired": [2.0, 1.0],
                    "actual": [2.0, 1.0],
                    "layout_slot": [0.0, 0.0, 2.0, 1.0],
                    "transform_to_root": {
                        "origin": [0.0, 0.0],
                        "unit_x": [1.0, 0.0],
                        "unit_y": [0.0, 1.0],
                    },
                    "opacity": 1.0,
                    "visibility": 0,
                    "z_index": 0,
                    "clip": None,
                }
            ],
        }
        self.sidecar = {
            "schema_version": 1,
            "case_id": "case",
            "surface": [2, 1],
            "geometry": [
                {
                    "path": "/Grid",
                    "type": TYPE,
                    "slot": [0.0, 0.0, 2.0, 1.0],
                    "actual": [2.0, 1.0],
                    "abs": [0.0, 0.0],
                    "transform_to_root": {
                        "origin": [0.0, 0.0],
                        "unit_x": [1.0, 0.0],
                        "unit_y": [0.0, 1.0],
                    },
                    "opacity": 1.0,
                    "z_index": 0,
                    "clip": None,
                    "visible": True,
                }
            ],
            "texts": [],
            "refusals": [],
            "text_failures": [],
            "render_issues": [],
        }
        self.tree = {
            "schema_version": 1,
            "case_id": "case",
            "tree": [
                {
                    "path": "/Grid",
                    "type": TYPE,
                    "desired": [2.0, 1.0],
                    "actual": [2.0, 1.0],
                    "offset": [0.0, 0.0],
                }
            ],
        }
        self.write()

    def write(self):
        glyph_data = (self.oracle / "dwrite-glyph-runs.json").read_bytes()
        glyph_document = json.loads(glyph_data)
        (self.oracle / "case.json").write_text(json.dumps(self.native), encoding="utf-8")
        (self.oracle / "manifest.json").write_text(
            json.dumps(
                {
                    "schema_version": 1,
                    "pixel_format": "BGRA8",
                    "alpha_mode": "premultiplied",
                    "captures": [
                        {
                            "case_id": "case",
                            "observation_file": "case.json",
                            "pixels_file": "case.bgra",
                            "width": 2,
                            "height": 1,
                            "stride": 8,
                            "sha256": hashlib.sha256(self.pixels).hexdigest(),
                        }
                    ],
                    "directwrite_glyph_runs": {
                        "file": "dwrite-glyph-runs.json",
                        "observations": len(glyph_document["observations"]),
                        "sha256": hashlib.sha256(glyph_data).hexdigest(),
                    },
                }
            ),
            encoding="utf-8",
        )
        (self.oracle / "oracle.json").write_text(
            json.dumps({"schema_version": 1, "os_build": "test"}), encoding="utf-8"
        )
        (self.actual / "case.json").write_text(json.dumps(self.sidecar), encoding="utf-8")
        (self.actual / "case.bgra").write_bytes(self.pixels)
        (self.actual / "trees" / "case.json").write_text(
            json.dumps(self.tree), encoding="utf-8"
        )


class RenderOracleAcceptanceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.fixture = Fixture(Path(self.temporary.name))

    def tearDown(self):
        self.temporary.cleanup()

    def compare(self):
        return oracle.compare(self.fixture.oracle, self.fixture.actual)

    def boundaries(self, report):
        return {problem["boundary"] for problem in report["cases"][0]["problems"]}

    def test_exact_pixels_and_visual_state_pass(self):
        report = self.compare()
        self.assertEqual({"cases": 1, "passed": 1, "failed": 0}, report["summary"])

    def test_one_wrong_pixel_fails_with_exact_location(self):
        pixels = bytearray(self.fixture.pixels)
        pixels[4] ^= 1
        (self.fixture.actual / "case.bgra").write_bytes(pixels)
        report = self.compare()
        self.assertIn("pixels.content", self.boundaries(report))
        metrics = report["cases"][0]["pixel_metrics"]
        self.assertEqual(1, metrics["mismatched_pixels"])
        self.assertEqual([1, 0, 2, 1], metrics["mismatch_bounds"])

    def test_refusal_is_a_failure_even_if_pixels_happen_to_match(self):
        self.fixture.sidecar["refusals"] = [
            {"path": "/Grid", "feature": "Opacity", "reason": "not implemented"}
        ]
        self.fixture.write()
        report = self.compare()
        self.assertIn("renderer.refusal", self.boundaries(report))

    def test_backend_issue_is_a_failure_even_if_pixels_happen_to_match(self):
        self.fixture.sidecar["render_issues"] = [
            {
                "code": "unsupported-opacity",
                "node": 1,
                "command": None,
                "message": "fractional opacity requires a layer",
            }
        ]
        self.fixture.write()
        report = self.compare()
        self.assertIn("renderer.backend-issue", self.boundaries(report))

    def test_effective_transform_is_not_replaced_by_layout_position(self):
        self.fixture.native["tree"][0]["transform_to_root"]["unit_x"] = [0.0, 1.0]
        self.fixture.write()
        report = self.compare()
        self.assertIn("visual.transform", self.boundaries(report))

    def test_missing_visual_observation_is_not_assumed_to_be_a_default(self):
        del self.fixture.sidecar["geometry"][0]["opacity"]
        self.fixture.write()
        report = self.compare()
        self.assertIn("visual.opacity", self.boundaries(report))

    def test_missing_output_cannot_pass_vacuously(self):
        (self.fixture.actual / "case.json").unlink()
        report = self.compare()
        self.assertEqual(0, report["summary"]["passed"])
        self.assertIn("case.missing", self.boundaries(report))

    def test_directwrite_glyph_data_is_part_of_acceptance(self):
        native_text = {
            "schema_version": 1,
            "observations": [
                {
                    "case_id": "case",
                    "element_index": 0,
                    "directwrite": {
                        "text": "A",
                        "family": "Segoe UI",
                        "font_size": 12.0,
                        "runs": [
                            {
                                "baseline": [0.0, 9.0],
                                "glyph_indices": [42],
                                "glyph_advances": [7.5],
                                "glyph_offsets": [[0.0, 0.0]],
                            }
                        ],
                    },
                }
            ],
        }
        (self.fixture.oracle / "dwrite-glyph-runs.json").write_text(
            json.dumps(native_text), encoding="utf-8"
        )
        self.fixture.sidecar["texts"] = [
            {
                "text": "A",
                "font_family": "Segoe UI",
                "font_size": 12.0,
                "baseline": 9.0,
                "advances": [7.5],
                "glyph_indices": [42],
                "glyph_offsets": [[0.0, 0.0]],
            }
        ]
        self.fixture.write()
        self.assertEqual(1, self.compare()["summary"]["passed"])
        self.fixture.sidecar["texts"][0]["advances"] = [7.500001]
        self.fixture.write()
        self.assertIn("text.advances", self.boundaries(self.compare()))
        self.fixture.sidecar["texts"][0]["advances"] = [7.5]
        self.fixture.sidecar["texts"][0]["glyph_indices"] = [41]
        self.fixture.write()
        report = self.compare()
        self.assertIn("text.glyph-indices", self.boundaries(report))

    def test_malformed_oracle_is_an_infrastructure_error(self):
        (self.fixture.oracle / "case.bgra").write_bytes(b"short")
        with self.assertRaisesRegex(oracle.AcceptanceError, "native capture"):
            self.compare()

    def test_cli_writes_the_report_and_exits_nonzero_for_a_mismatch(self):
        (self.fixture.actual / "case.bgra").write_bytes(b"\0" * 8)
        output = Path(self.temporary.name) / "report.json"
        argv = [
            "check_render_oracle.py",
            "--oracle",
            str(self.fixture.oracle),
            "--actual",
            str(self.fixture.actual),
            "--output",
            str(output),
        ]
        with patch.object(sys, "argv", argv), redirect_stdout(io.StringIO()):
            status = oracle.main()
        self.assertEqual(1, status)
        self.assertGreater(json.loads(output.read_text())["summary"]["failed"], 0)


if __name__ == "__main__":
    unittest.main()
