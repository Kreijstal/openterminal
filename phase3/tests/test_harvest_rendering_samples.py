from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


SCRIPTS = Path(__file__).resolve().parents[1] / "scripts"
sys.path.insert(0, str(SCRIPTS))

from harvest_rendering_samples import (  # noqa: E402
    HarvestError,
    check_inventory,
    load_manifest,
    materialize,
)


class RenderingSampleHarvestTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def repository(self, files: dict[str, bytes]) -> tuple[Path, str]:
        root = self.root / f"repo-{len(list(self.root.glob('repo-*')))}"
        root.mkdir()
        subprocess.run(["git", "init", "-q", str(root)], check=True)
        subprocess.run(["git", "-C", str(root), "config", "user.name", "Test"], check=True)
        subprocess.run(
            ["git", "-C", str(root), "config", "user.email", "test@example.invalid"],
            check=True,
        )
        for relative, data in files.items():
            path = root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        subprocess.run(["git", "-C", str(root), "add", "."], check=True)
        subprocess.run(["git", "-C", str(root), "commit", "-qm", "fixture"], check=True)
        commit = subprocess.check_output(
            ["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
        return root, commit

    @staticmethod
    def manifest(commit: str, paths: list[str]) -> dict:
        return {
            "schema_version": 1,
            "repositories": {
                "sample": {
                    "url": "https://example.invalid/sample",
                    "commit": commit,
                    "license": "MIT",
                }
            },
            "source_sets": {
                "window": {"repository": "sample", "paths": paths}
            },
            "programs": [
                {
                    "name": "d2d_window",
                    "purpose": "fixture",
                    "source_sets": ["window"],
                }
            ],
        }

    def test_materializes_only_pinned_text_with_stable_inventory(self) -> None:
        root, commit = self.repository({
            "LICENSE": b"MIT\n",
            "sample/main.cpp": b"int main() { return 0; }\n",
        })
        output = self.root / "output"
        inventory = materialize(
            self.manifest(commit, ["LICENSE", "sample/main.cpp"]),
            {"sample": root},
            output,
        )
        self.assertEqual(len(inventory["files"]), 2)
        self.assertEqual(
            (output / "sources/sample/sample/main.cpp").read_text(encoding="utf-8"),
            "int main() { return 0; }\n",
        )
        written = json.loads((output / "inventory.json").read_text(encoding="utf-8"))
        self.assertEqual(written, inventory)
        record = next(item for item in inventory["files"] if item["path"].endswith("main.cpp"))
        self.assertEqual(record["lines"], 1)
        self.assertEqual(record["used_by"], ["d2d_window"])
        self.assertNotIn(str(self.root), json.dumps(inventory))

        expected = self.root / "expected.json"
        expected.write_text(json.dumps(inventory), encoding="utf-8")
        check_inventory(inventory, expected)
        expected.write_text("{}\n", encoding="utf-8")
        with self.assertRaisesRegex(HarvestError, "harvest differs"):
            check_inventory(inventory, expected)

    def test_refuses_a_checkout_at_a_different_commit(self) -> None:
        root, commit = self.repository({"sample.cpp": b"one\n"})
        (root / "sample.cpp").write_text("two\n", encoding="utf-8")
        subprocess.run(["git", "-C", str(root), "add", "sample.cpp"], check=True)
        subprocess.run(["git", "-C", str(root), "commit", "-qm", "second"], check=True)
        with self.assertRaisesRegex(HarvestError, "expected .* checkout is"):
            materialize(
                self.manifest(commit, ["sample.cpp"]),
                {"sample": root},
                self.root / "output",
            )

    def test_refuses_binary_or_unlisted_file_types(self) -> None:
        root, commit = self.repository({"sample.dll": b"MZ\0binary"})
        with self.assertRaisesRegex(HarvestError, "allowed textual source"):
            materialize(
                self.manifest(commit, ["sample.dll"]),
                {"sample": root},
                self.root / "output",
            )

    def test_committed_manifest_names_all_eight_programs(self) -> None:
        path = Path(__file__).resolve().parents[2] / "research" / "rendering-programs" / "manifest.json"
        manifest = load_manifest(path)
        self.assertEqual(
            [program["name"] for program in manifest["programs"]],
            [
                "d2d_window",
                "d2d_display_list",
                "d2d_tree",
                "d2d_alpha",
                "dwrite_text",
                "dcomp_window",
                "mini_xaml",
                "nested_xaml",
            ],
        )


if __name__ == "__main__":
    unittest.main()
