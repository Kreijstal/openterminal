#!/usr/bin/env python3

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

SCRIPT_DIRECTORY = Path(__file__).resolve().parents[1] / "scripts"
sys.path.insert(0, str(SCRIPT_DIRECTORY))

from harvest_terminal_xaml import (  # noqa: E402
    Metadata,
    build,
    canonical_remote,
    candidates,
    encode_size,
    harvest,
    markup_extension_name,
    out_of_scope,
    serialise,
    subtree_blockers,
    uses_x_directives,
)

PRESENTATION = "http://schemas.microsoft.com/winfx/2006/xaml/presentation"

# Enough of the real metadata shape to exercise every classification. The real
# file is 1751 types; the rules under test do not care how many there are.
METADATA = {
    "class_local_names": {
        name: [f"Windows.UI.Xaml.{name}"]
        for name in (
            "Grid", "StackPanel", "Border", "TextBlock", "Button",
            "ColumnDefinition", "Style", "Setter", "ControlTemplate",
            "AutomationProperties",
        )
    },
    "ui_element_local_names": ["Grid", "StackPanel", "Border", "TextBlock", "Button"],
    "event_only_names": ["Click", "Loaded"],
    "property_names": [
        "Background", "Content", "Height", "Margin", "Orientation", "Padding",
        "Text", "Width", "ColumnProperty", "AccessibilityViewProperty",
    ],
    "attached_property_names": ["Column", "AccessibilityView"],
}


def meta() -> Metadata:
    return Metadata(METADATA)


def node(markup: str):
    return build(ET.fromstring(markup), "/root")


def kinds(markup: str) -> set[str]:
    return {b["kind"] for b in subtree_blockers(node(markup), meta())}


def details(markup: str) -> set[str]:
    return {b["detail"] for b in subtree_blockers(node(markup), meta())}


class MarkupExtensionTest(unittest.TestCase):
    def test_names_the_extension(self) -> None:
        self.assertEqual(markup_extension_name("{StaticResource Foo}"), "StaticResource")
        self.assertEqual(markup_extension_name("{x:Bind Path}"), "x:Bind")
        self.assertEqual(markup_extension_name(" {ThemeResource A} "), "ThemeResource")

    def test_escaped_brace_is_a_literal(self) -> None:
        # "{}" is XAML's escape, so this is the literal text "{guid here}".
        self.assertIsNone(markup_extension_name("{}{guid here}"))

    def test_plain_values_are_not_extensions(self) -> None:
        self.assertIsNone(markup_extension_name("Horizontal"))
        self.assertIsNone(markup_extension_name(""))


class CanonicalRemoteTest(unittest.TestCase):
    """Provenance must not record which machine ran the harvest."""

    CANONICAL = "https://github.com/microsoft/terminal"

    def test_every_spelling_agrees(self) -> None:
        for url in (
            "https://github.com/microsoft/terminal",
            "https://github.com/microsoft/terminal.git",
            "https://github.com/microsoft/terminal/",
            "git@github.com:microsoft/terminal.git",
            "git@github.com:microsoft/terminal",
            "ssh://git@github.com/microsoft/terminal.git",
            "  https://github.com/microsoft/terminal.git  ",
        ):
            with self.subTest(url=url):
                self.assertEqual(canonical_remote(url), self.CANONICAL)

    def test_leaves_an_unrecognised_remote_alone(self) -> None:
        self.assertEqual(
            canonical_remote("file:///srv/mirrors/terminal"),
            "file:///srv/mirrors/terminal",
        )


class BlockerTest(unittest.TestCase):
    def test_clean_markup_has_no_blockers(self) -> None:
        self.assertEqual(kinds(f'<Grid xmlns="{PRESENTATION}" Margin="8"/>'), set())

    def test_markup_extension_blocks(self) -> None:
        markup = f'<Grid xmlns="{PRESENTATION}" Background="{{ThemeResource Foo}}"/>'
        self.assertIn("markup-extension", kinds(markup))
        self.assertIn("ThemeResource", details(markup))

    def test_escaped_brace_does_not_block(self) -> None:
        self.assertEqual(
            kinds(f'<TextBlock xmlns="{PRESENTATION}" Text="{{}}{{literal}}"/>'), set()
        )

    def test_event_attribute_blocks(self) -> None:
        self.assertIn("event-attribute", kinds(f'<Button xmlns="{PRESENTATION}" Click="OnClick"/>'))

    def test_unknown_attribute_blocks(self) -> None:
        # A WPF-only member, or anything else the framework metadata does not
        # carry, must not pass as loadable.
        self.assertIn("unknown-attribute", kinds(f'<Grid xmlns="{PRESENTATION}" ScrollChanged="X"/>'))

    def test_attached_property_is_recognised(self) -> None:
        # Metadata spells these "<Name>Property"; markup spells them Owner.Name.
        self.assertEqual(kinds(f'<Grid xmlns="{PRESENTATION}" Grid.Column="1"/>'), set())
        self.assertEqual(
            kinds(f'<Grid xmlns="{PRESENTATION}" AutomationProperties.AccessibilityView="Raw"/>'),
            set(),
        )

    def test_x_directives(self) -> None:
        x = 'xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"'
        self.assertEqual(kinds(f'<Grid xmlns="{PRESENTATION}" {x} x:Name="Root"/>'), set())
        self.assertIn("x-directive", kinds(f'<Grid xmlns="{PRESENTATION}" {x} x:Uid="U"/>'))
        self.assertIn("x-directive", kinds(f'<Grid xmlns="{PRESENTATION}" {x} x:Key="K"/>'))

    def test_an_x_primitive_where_a_value_belongs_does_not_block(self) -> None:
        # <ToggleButton.Tag><x:Int32>17</x:Int32> is the form Terminal writes
        # for a property whose type is `object`; the parser reads it, so the
        # element is no longer a blocker on its own account.
        x = 'xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"'
        markup = (
            f'<Grid xmlns="{PRESENTATION}" {x}><Grid.Margin>'
            "<x:Double>8</x:Double></Grid.Margin></Grid>"
        )
        self.assertEqual(kinds(markup), set())

    def test_an_x_primitive_in_a_dictionary_blocks_on_its_key_alone(self) -> None:
        x = 'xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"'
        markup = (
            f'<Grid xmlns="{PRESENTATION}" {x}><Grid.Resources>'
            '<x:Double x:Key="W">60</x:Double></Grid.Resources></Grid>'
        )
        found = kinds(markup)
        # The type is understood; what is missing is a dictionary an x:Key can
        # land in, which is counted with every other x:Key rather than under a
        # name that says the type was the problem.
        self.assertNotIn("x-element", found)
        self.assertIn("x-directive", found)
        self.assertIn("x:Key", details(markup))

    def test_an_x_primitive_as_a_child_element_still_blocks(self) -> None:
        x = 'xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"'
        markup = f'<Grid xmlns="{PRESENTATION}" {x}><x:Double>8</x:Double></Grid>'
        self.assertIn("x-element", kinds(markup))

    def test_an_x_element_outside_the_implemented_set_still_blocks(self) -> None:
        x = 'xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"'
        markup = (
            f'<Grid xmlns="{PRESENTATION}" {x}><Grid.Margin>'
            "<x:Null/></Grid.Margin></Grid>"
        )
        self.assertIn("x-element", kinds(markup))
        self.assertIn("x:Null", details(markup))

    def test_foreign_type_blocks_and_stops_there(self) -> None:
        markup = (
            f'<Grid xmlns="{PRESENTATION}" xmlns:mux="using:Microsoft.UI.Xaml.Controls">'
            '<mux:TabView CanDragTabs="True"/></Grid>'
        )
        found = kinds(markup)
        self.assertIn("foreign-type", found)
        # CanDragTabs is a property of the foreign type. Reporting it as unknown
        # would add noise without adding information.
        self.assertNotIn("unknown-attribute", found)

    def test_resource_element_blocks(self) -> None:
        markup = (
            f'<Grid xmlns="{PRESENTATION}">'
            '<StaticResource ResourceKey="Foo"/></Grid>'
        )
        found = kinds(markup)
        self.assertIn("resource-element", found)
        self.assertNotIn("unknown-attribute", found)

    def test_property_element_is_not_an_unknown_type(self) -> None:
        markup = (
            f'<Grid xmlns="{PRESENTATION}"><Grid.ColumnDefinitions>'
            '<ColumnDefinition Width="40"/></Grid.ColumnDefinitions></Grid>'
        )
        self.assertEqual(kinds(markup), set())

    def test_blockers_carry_the_path(self) -> None:
        markup = (
            f'<Grid xmlns="{PRESENTATION}"><StackPanel><Button Click="X"/></StackPanel></Grid>'
        )
        found = subtree_blockers(node(markup), meta())
        self.assertEqual([b["path"] for b in found], ["/root/StackPanel[0]/Button[0]"])


class CandidateTest(unittest.TestCase):
    def extract(self, markup: str) -> list[str]:
        return [n.local for n in candidates(node(markup), meta())]

    def test_clean_root_is_the_only_candidate(self) -> None:
        markup = f'<Grid xmlns="{PRESENTATION}"><StackPanel><Border/></StackPanel></Grid>'
        # Maximal: the children are inside the Grid already.
        self.assertEqual(self.extract(markup), ["Grid"])

    def test_descends_past_a_blocked_root(self) -> None:
        markup = (
            f'<Grid xmlns="{PRESENTATION}" Background="{{ThemeResource X}}">'
            "<StackPanel><Border/></StackPanel></Grid>"
        )
        self.assertEqual(self.extract(markup), ["StackPanel"])

    def test_finds_siblings_under_a_blocked_parent(self) -> None:
        markup = (
            f'<Grid xmlns="{PRESENTATION}" Background="{{ThemeResource X}}">'
            "<StackPanel/><Border/></Grid>"
        )
        self.assertEqual(self.extract(markup), ["StackPanel", "Border"])

    def test_non_ui_element_is_never_a_root(self) -> None:
        # Style and ColumnDefinition are framework classes but not UIElements,
        # so they cannot be measured or arranged.
        markup = f'<Style xmlns="{PRESENTATION}"><Setter/></Style>'
        self.assertEqual(self.extract(markup), [])

    def test_descends_through_property_elements(self) -> None:
        markup = (
            f'<Button xmlns="{PRESENTATION}" Click="X"><Button.Content>'
            "<StackPanel/></Button.Content></Button>"
        )
        self.assertEqual(self.extract(markup), ["StackPanel"])


class SerialiseTest(unittest.TestCase):
    def test_emits_namespace_only_on_the_root(self) -> None:
        markup = serialise(node(f'<Grid xmlns="{PRESENTATION}"><Border/></Grid>'), root=True)
        self.assertEqual(markup, f'<Grid xmlns="{PRESENTATION}"><Border/></Grid>')

    def test_declares_x_only_when_used(self) -> None:
        x = 'xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"'
        source = node(f'<Grid xmlns="{PRESENTATION}" {x} x:Name="R"/>')
        self.assertTrue(uses_x_directives(source))
        self.assertIn('xmlns:x=', serialise(source, root=True, uses_x=True))

        plain = node(f'<Grid xmlns="{PRESENTATION}"/>')
        self.assertFalse(uses_x_directives(plain))

    def test_round_trips_through_the_parser(self) -> None:
        source = f'<Grid xmlns="{PRESENTATION}" Margin="4,8"><TextBlock>hi</TextBlock></Grid>'
        again = serialise(node(serialise(node(source), root=True)), root=True)
        self.assertEqual(again, source)

    def test_escapes_attribute_values_and_text(self) -> None:
        markup = serialise(
            node(f'<TextBlock xmlns="{PRESENTATION}" Text="a &amp; b">x &lt; y</TextBlock>'),
            root=True,
        )
        self.assertIn("&amp;", markup)
        self.assertIn("&lt;", markup)
        ET.fromstring(markup)  # still well formed

    def test_normalises_formatting_so_equal_markup_collapses(self) -> None:
        a = node(f'<Grid xmlns="{PRESENTATION}">\n  <Border />\n</Grid>')
        b = node(f'<Grid xmlns="{PRESENTATION}"><Border/></Grid>')
        self.assertEqual(serialise(a, root=True), serialise(b, root=True))


class EncodeSizeTest(unittest.TestCase):
    def test_infinity_becomes_a_string(self) -> None:
        self.assertEqual(encode_size([float("inf"), 300.0]), ["Infinity", 300.0])

    def test_stays_json_serialisable_without_nan(self) -> None:
        json.dumps(encode_size([float("inf"), float("inf")]), allow_nan=False)


class ScopeTest(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.repo = Path(self.directory.name)
        self.addCleanup(self.directory.cleanup)

    def write(self, relative: str, text: str) -> Path:
        path = self.repo / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
        return path

    def test_cpp_project_is_in_scope(self) -> None:
        self.write("winui/App.vcxproj", "<Project/>")
        page = self.write("winui/Page.xaml", f'<Grid xmlns="{PRESENTATION}"/>')
        self.assertIsNone(out_of_scope(page, self.repo))

    def test_csharp_project_is_out_of_scope(self) -> None:
        # WPF and WinUI share the presentation URI; only the project tells them
        # apart, and a WPF page that slipped in would produce cases that cannot
        # load.
        self.write("wpf/App.csproj", "<Project/>")
        page = self.write("wpf/Window.xaml", f'<Grid xmlns="{PRESENTATION}"/>')
        self.assertEqual(out_of_scope(page, self.repo), "built by App.csproj")

    def test_clr_namespace_is_out_of_scope(self) -> None:
        self.write("odd/App.vcxproj", "<Project/>")
        page = self.write(
            "odd/Window.xaml",
            f'<Grid xmlns="{PRESENTATION}" xmlns:l="clr-namespace:Foo"/>',
        )
        self.assertEqual(out_of_scope(page, self.repo), "declares a clr-namespace")

    def test_project_less_file_is_out_of_scope(self) -> None:
        page = self.write("loose/Page.xaml", f'<Grid xmlns="{PRESENTATION}"/>')
        self.assertEqual(out_of_scope(page, self.repo), "no owning project")


class HarvestTest(unittest.TestCase):
    """End to end over a miniature checkout."""

    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.repo = Path(self.directory.name)
        self.addCleanup(self.directory.cleanup)

        (self.repo / "app").mkdir()
        (self.repo / "app" / "App.vcxproj").write_text("<Project/>", encoding="utf-8")
        (self.repo / "app" / "Page.xaml").write_text(
            f'<Grid xmlns="{PRESENTATION}" '
            'xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" '
            'x:Class="App.Page" Background="{ThemeResource Brush}">'
            '<StackPanel Orientation="Horizontal"><Border Width="10"/></StackPanel>'
            '<Button Click="OnClick"/>'
            "</Grid>",
            encoding="utf-8",
        )
        # A second file with markup identical to the first candidate, to prove
        # duplicates collapse into one case with both sites recorded.
        (self.repo / "app" / "Other.xaml").write_text(
            f'<Grid xmlns="{PRESENTATION}" '
            'xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" x:Class="App.Other">'
            '<StackPanel Orientation="Horizontal"><Border Width="10"/></StackPanel>'
            "</Grid>",
            encoding="utf-8",
        )
        for command in (
            ["init", "-q"],
            ["remote", "add", "origin", "https://example.invalid/repo.git"],
            ["-c", "user.email=t@example.invalid", "-c", "user.name=t",
             "commit", "-q", "--allow-empty", "-m", "seed"],
        ):
            subprocess.run(["git", "-C", str(self.repo), *command], check=True,
                           capture_output=True)

        self.inventory, self.cases = harvest(self.repo, meta())

    def test_counts_files_and_candidates(self) -> None:
        totals = self.inventory["totals"]
        self.assertEqual(totals["files"], 2)
        self.assertEqual(totals["files_parsed"], 2)
        self.assertEqual(totals["unique_candidates"], 1)

    def test_duplicate_markup_collapses_and_records_both_sites(self) -> None:
        occurrences = self.cases[0]["harvest"]["occurrences"]
        self.assertEqual(
            sorted(o["file"] for o in occurrences),
            ["app/Other.xaml", "app/Page.xaml"],
        )

    def test_records_provenance(self) -> None:
        # Canonical, not the ".git" spelling the fixture cloned with.
        self.assertEqual(
            self.inventory["source"]["repository"], "https://example.invalid/repo"
        )
        for case in self.cases:
            self.assertEqual(
                case["harvest"]["commit"], self.inventory["source"]["commit"]
            )

    def test_emits_one_case_per_available_size(self) -> None:
        ids = {case["id"] for case in self.cases}
        self.assertEqual(len(ids), len(self.cases))
        self.assertEqual(len(self.cases), 3)  # one unique subtree x 3 sizes

    def test_cases_are_json_serialisable_without_nan(self) -> None:
        for case in self.cases:
            json.dumps(case, allow_nan=False)

    def test_blockers_explain_what_was_rejected(self) -> None:
        blockers = self.inventory["blockers"]
        self.assertEqual(blockers["markup-extension"]["details"], {"ThemeResource": 1})
        self.assertEqual(blockers["event-attribute"]["details"], {"Click": 1})
        self.assertEqual(blockers["x-directive"]["details"], {"x:Class": 2})

    def test_inventory_records_usage(self) -> None:
        self.assertEqual(self.inventory["element_types"]["Border"]["count"], 2)
        self.assertEqual(
            self.inventory["properties_by_type"]["StackPanel"], {"Orientation": 2}
        )
        self.assertIn("Brush", self.inventory["resource_keys_referenced"])


class MetadataFileTest(unittest.TestCase):
    """The committed metadata must still describe Terminal's vocabulary."""

    def test_committed_members_file_parses(self) -> None:
        path = (
            Path(__file__).resolve().parents[2]
            / "research/nuget/microsoft.windows.sdk.contracts/10.0.26100.1/xaml-members.json"
        )
        if not path.exists():
            self.skipTest(f"{path} not harvested yet")
        loaded = Metadata.load(path)
        for name in ("Grid", "StackPanel", "Border", "TextBlock"):
            self.assertIn(name, loaded.ui_elements)
        for name in ("Style", "Setter", "ColumnDefinition"):
            self.assertIn(name, loaded.classes)
            self.assertNotIn(name, loaded.ui_elements)
        self.assertIn("Click", loaded.events)
        self.assertIn("Column", loaded.properties)


if __name__ == "__main__":
    unittest.main()
