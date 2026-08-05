#!/usr/bin/env python3
"""The icon glyph extractor, over a miniature checkout.

The set this produces is what the CI font harvest is told to read out of Segoe
MDL2 Assets and Segoe Fluent Icons, so a codepoint it drops is a FontIcon case
that has no metrics and a codepoint it invents is a harvest that fails on a
glyph nothing asked for.
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

from harvest_icon_glyphs import harvest, scan  # noqa: E402

PRESENTATION = "http://schemas.microsoft.com/winfx/2006/xaml/presentation"
XAML_X = "http://schemas.microsoft.com/winfx/2006/xaml"


class ScanTest(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.repo = Path(self.directory.name)
        self.addCleanup(self.directory.cleanup)

    def write(self, relative: str, text: str) -> Path:
        path = self.repo / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
        return path

    def page(self, body: str, name: str = "app/Page.xaml") -> None:
        self.write("app/App.vcxproj", "<Project/>")
        self.write(name, f'<Grid xmlns="{PRESENTATION}" xmlns:x="{XAML_X}">{body}</Grid>')

    def test_reads_a_glyph_as_a_codepoint(self) -> None:
        self.page('<FontIcon FontFamily="Segoe MDL2 Assets" FontSize="12" '
                  'Glyph="&#xE710;"/>')
        found = scan(self.repo)
        self.assertEqual(found["codepoints"], [0xE710])
        self.assertEqual(found["glyphs"]["U+E710"]["files"], ["app/Page.xaml"])
        self.assertEqual(found["font_families"], {"Segoe MDL2 Assets": 1})
        self.assertEqual(found["font_sizes"], {"12": 1})
        self.assertEqual(found["element_types"], {"FontIcon": 1})

    def test_the_same_glyph_in_two_files_is_one_codepoint(self) -> None:
        self.page('<FontIcon Glyph="&#xE710;"/>')
        self.write("app/Other.xaml",
                   f'<Grid xmlns="{PRESENTATION}"><FontIcon Glyph="&#xE710;"/></Grid>')
        found = scan(self.repo)
        self.assertEqual(found["codepoints"], [0xE710])
        self.assertEqual(found["glyphs"]["U+E710"]["count"], 2)
        self.assertEqual(found["glyphs"]["U+E710"]["files"],
                         ["app/Other.xaml", "app/Page.xaml"])

    def test_a_glyph_behind_a_markup_extension_is_deferred_not_guessed(self) -> None:
        # `Glyph="{TemplateBinding FontIconGlyph}"` names a codepoint only the
        # binding knows. Reading the braces as characters would put U+007B into
        # the harvest and fail it on a glyph no icon font has.
        self.page('<FontIcon Glyph="{TemplateBinding FontIconGlyph}"/>'
                  '<FontIcon Glyph="&#xE74D;"/>')
        found = scan(self.repo)
        self.assertEqual(found["codepoints"], [0xE74D])
        self.assertEqual(found["deferred_extensions"], {"TemplateBinding": 1})

    def test_an_escaped_brace_is_the_character_after_the_escape(self) -> None:
        # "{}" is XAML's escape for a leading brace: the value is "{", one
        # character. Taking the raw attribute would harvest three.
        self.page('<FontIcon Glyph="{}{"/>')
        self.assertEqual(scan(self.repo)["codepoints"], [0x7B])

    def test_an_escaped_extension_is_a_literal_and_not_a_lookup(self) -> None:
        # The escape has to be recognised after the extension test, not before,
        # or this reads as a {StaticResource} and drops out of the set.
        self.page('<FontIcon Glyph="{}{StaticResource X}"/>')
        found = scan(self.repo)
        self.assertEqual(found["deferred_extensions"], {})
        # Every character of the literal, the leading brace included and the
        # escape itself gone.
        self.assertIn(0x7B, found["codepoints"])
        self.assertEqual(found["codepoints"].count(0x7B), 1)

    def test_wpf_markup_is_out_of_scope(self) -> None:
        # The same rule the level 7 harvest uses: only the owning project tells
        # WPF and WinUI apart, and a WPF page's icons are not what this corpus
        # measures.
        self.write("wpf/App.csproj", "<Project/>")
        self.write("wpf/Window.xaml",
                   f'<Grid xmlns="{PRESENTATION}"><FontIcon Glyph="&#xE710;"/></Grid>')
        found = scan(self.repo)
        self.assertEqual(found["codepoints"], [])
        self.assertEqual(found["totals"]["files_out_of_scope"], 1)

    def test_a_multi_character_glyph_yields_every_codepoint(self) -> None:
        self.page('<FontIcon Glyph="&#xE710;&#xE74D;"/>')
        self.assertEqual(scan(self.repo)["codepoints"], [0xE710, 0xE74D])

    def test_an_unparsable_file_is_named_and_the_rest_are_read(self) -> None:
        self.page('<FontIcon Glyph="&#xE710;"/>')
        self.write("app/Broken.xaml", "<Grid>")
        found = scan(self.repo)
        self.assertEqual(found["codepoints"], [0xE710])
        self.assertEqual([f["file"] for f in found["parse_failures"]],
                         ["app/Broken.xaml"])


class HarvestTest(unittest.TestCase):
    """End to end, including the provenance the output has to carry."""

    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.repo = Path(self.directory.name)
        self.addCleanup(self.directory.cleanup)

        (self.repo / "app").mkdir()
        (self.repo / "app" / "App.vcxproj").write_text("<Project/>", encoding="utf-8")
        (self.repo / "app" / "Page.xaml").write_text(
            f'<Grid xmlns="{PRESENTATION}">'
            '<FontIcon FontSize="12" Glyph="&#xE76C;"/>'
            '<FontIcon FontFamily="Segoe Fluent Icons" Glyph="&#xE710;"/>'
            "</Grid>",
            encoding="utf-8",
        )
        for command in (
            ["init", "-q"],
            ["remote", "add", "origin", "git@example.invalid:owner/repo.git"],
            ["-c", "user.email=t@example.invalid", "-c", "user.name=t",
             "commit", "-q", "--allow-empty", "-m", "seed"],
        ):
            subprocess.run(["git", "-C", str(self.repo), *command], check=True,
                           capture_output=True)
        self.payload = harvest(self.repo)

    def test_records_where_the_set_came_from(self) -> None:
        # Canonical, so two clones of the same repository produce one string.
        self.assertEqual(self.payload["source"]["repository"],
                         "https://example.invalid/owner/repo")
        self.assertEqual(len(self.payload["source"]["commit"]), 40)

    def test_codepoints_are_sorted_and_serialisable(self) -> None:
        self.assertEqual(self.payload["codepoints"], [0xE710, 0xE76C])
        json.dumps(self.payload, allow_nan=False)

    def test_two_runs_agree(self) -> None:
        again = harvest(self.repo)
        self.assertEqual(json.dumps(self.payload, sort_keys=True),
                         json.dumps(again, sort_keys=True))

    def test_the_output_is_what_the_font_harvest_reads(self) -> None:
        # harvest_font_metrics.py --codepoints-from reads exactly this key, as
        # a list of decimal integers.
        self.assertTrue(all(isinstance(cp, int) for cp in self.payload["codepoints"]))


if __name__ == "__main__":
    unittest.main()
