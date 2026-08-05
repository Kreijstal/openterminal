#!/usr/bin/env python3

"""The extractor's merge model, and the one thing the corpus copies out of it.

Two different jobs in one file, because they are two halves of the same claim.

The first half is the merge: WinUI's theme dictionary does not exist as a file,
it is built by ``BatchMergeXaml`` out of a hundred-odd fragments, and the rules
that decide which entry survives are the whole reason this cannot be a union of
parsed files. Those rules are exercised here against fragments written for the
purpose, so a change to the model fails on a five-line document instead of on a
two-megabyte diff.

The second half is drift. ``generate_cases.py`` quotes five literals out of the
pinned WinUI source into its L5-theme twins, because a generator that had to
read the extract would need a WinUI checkout to run. A quoted literal can go
stale, and a stale one is worse than useless: the resolved case and its twin
would agree with each other and both be wrong about WinUI. So when the extract
has been materialized, every quoted literal is checked against it. When it has
not -- a bare checkout, no WinUI clone -- the check skips and says so, rather
than passing quietly.
"""

from __future__ import annotations

import json
import sys
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

SCRIPT_DIRECTORY = Path(__file__).resolve().parents[1] / "scripts"
sys.path.insert(0, str(SCRIPT_DIRECTORY))

from extract_winui_theme_resources import (  # noqa: E402
    API_CONTRACT,
    PINNED_COMMIT,
    Merged,
    classify,
    follow_aliases,
    strip_conditional,
)
from generate_cases import THEME_RESOURCE_CASES  # noqa: E402

PRESENTATION = "http://schemas.microsoft.com/winfx/2006/xaml/presentation"
XAML_X = "http://schemas.microsoft.com/winfx/2006/xaml"

DATABASE = (Path(__file__).resolve().parents[1] / "xaml-db" / "theme-resources"
            / "winui-2.8.4.json")


def dictionary(body: str) -> str:
    return (f'<ResourceDictionary xmlns="{PRESENTATION}" xmlns:x="{XAML_X}">'
            f"{body}</ResourceDictionary>")


def merge(*bodies: str) -> Merged:
    merged = Merged()
    for body in bodies:
        root = ET.fromstring(dictionary(body))
        for node in list(root):
            merged._add(node)  # noqa: SLF001 -- the merge step is what is under test
    return merged


def entries(merged: Merged, theme: str | None = None) -> dict[str, dict]:
    source = merged.root if theme is None else merged.themes[theme]
    return {key: classify(key, node) for key, node in source.items()}


class MergeSemantics(unittest.TestCase):
    def test_a_later_entry_replaces_an_earlier_one(self):
        """Last writer wins, which is why the merge order has to be modelled."""
        merged = merge('<x:Double x:Key="W">10</x:Double>',
                       '<x:Double x:Key="W">20</x:Double>')
        self.assertEqual(entries(merged)["W"]["text"], "20")

    def test_a_theme_entry_removes_the_root_entry(self):
        """MergedDictionary.RemoveAncestorNodesWithKey.

        Without it the same key would sit in both dictionaries and a lookup
        would have to choose, which the real merge never leaves it to do.
        """
        merged = merge('<x:Double x:Key="W">10</x:Double>',
                       '<ResourceDictionary.ThemeDictionaries>'
                       '<ResourceDictionary x:Key="Default">'
                       '<x:Double x:Key="W">20</x:Double>'
                       "</ResourceDictionary></ResourceDictionary.ThemeDictionaries>")
        self.assertNotIn("W", merged.root)
        self.assertEqual(entries(merged, "Default")["W"]["text"], "20")

    def test_dark_folds_into_default(self):
        """Both cannot coexist: Dark would shadow Default entirely."""
        merged = merge('<ResourceDictionary.ThemeDictionaries>'
                       '<ResourceDictionary x:Key="Dark">'
                       '<x:Double x:Key="W">20</x:Double>'
                       "</ResourceDictionary></ResourceDictionary.ThemeDictionaries>")
        self.assertEqual(list(merged.themes), ["Default"])

    def test_an_unkeyed_entry_is_counted_and_dropped(self):
        """An implicit style is real content that no key reaches."""
        merged = merge('<Style TargetType="Button"/>')
        self.assertEqual(merged.unkeyed, 1)
        self.assertEqual(merged.root, {})

    def test_merged_dictionaries_are_refused(self):
        with self.assertRaises(SystemExit):
            merge("<ResourceDictionary.MergedDictionaries/>")


class ConditionalXaml(unittest.TestCase):
    """StripNamespaces, at the API contract version 21H1 pins."""

    API = API_CONTRACT["21H1"]

    def strip(self, body: str) -> ET.Element:
        root = ET.fromstring(dictionary(body))
        holder = ET.Element("holder")
        for node in list(root):
            holder.append(node)
        strip_conditional(holder, self.API)
        return holder

    def test_a_present_condition_that_holds_survives_unprefixed(self):
        contract = (f"{PRESENTATION}?IsApiContractPresent("
                    "Windows.Foundation.UniversalApiContract,7)")
        holder = self.strip(f'<x:Double xmlns:c="{contract}" x:Key="W" c:Foo="1">10</x:Double>')
        self.assertEqual(holder[0].attrib.get("Foo"), "1",
                         "a surviving conditional attribute keeps its name without the prefix")

    def test_a_notpresent_condition_that_fails_is_removed(self):
        contract = (f"{PRESENTATION}?IsApiContractNotPresent("
                    "Windows.Foundation.UniversalApiContract,7)")
        holder = self.strip(f'<c:Thing xmlns:c="{contract}" x:Key="W"/>')
        self.assertEqual(len(holder), 0)


class Classification(unittest.TestCase):
    def classify_one(self, body: str, key: str = "K") -> dict:
        node = ET.fromstring(dictionary(body))[0]
        return classify(key, node)

    def test_a_primitive_is_a_value(self):
        entry = self.classify_one('<Thickness x:Key="K">1,2,3,4</Thickness>')
        self.assertEqual(entry, {"status": "value", "type": "Thickness", "text": "1,2,3,4"})

    def test_a_brush_keeps_its_colour_reference(self):
        entry = self.classify_one(
            '<SolidColorBrush x:Key="K" Color="{ThemeResource SystemAccentColor}" Opacity="0.9"/>')
        self.assertEqual(entry["status"], "brush")
        self.assertEqual(entry["color_ref"], {"extension": "ThemeResource",
                                              "key": "SystemAccentColor"})
        self.assertEqual(entry["opacity"], "0.9")

    def test_a_style_is_opaque_and_not_dropped(self):
        """A key that exists as a template is a different fact from a key that
        does not exist, and the classifier downstream needs both."""
        entry = self.classify_one('<Style x:Key="K" TargetType="Button"/>')
        self.assertEqual(entry["status"], "opaque")
        self.assertEqual(entry["type"], "Style")

    def test_a_converter_from_another_namespace_is_opaque(self):
        entry = ET.fromstring(
            f'<ResourceDictionary xmlns="{PRESENTATION}" xmlns:x="{XAML_X}" '
            f'xmlns:p="using:Microsoft.UI.Xaml.Controls.Primitives">'
            f'<p:CornerRadiusFilterConverter x:Key="K"/></ResourceDictionary>')[0]
        self.assertEqual(classify("K", entry)["status"], "opaque")


class AliasAndColourResolution(unittest.TestCase):
    def test_an_alias_chain_is_followed_and_kept(self):
        themes = {"Shared": {}, "Default": {
            "A": {"status": "alias", "type": "StaticResource", "key": "B"},
            "B": {"status": "alias", "type": "StaticResource", "key": "C"},
            "C": {"status": "value", "type": "x:Double", "text": "7"},
        }}
        follow_aliases(themes)
        self.assertEqual(themes["Default"]["A"]["key"], "B", "the chain as written survives")
        self.assertEqual(themes["Default"]["A"]["resolved"]["text"], "7")

    def test_a_dangling_alias_says_so(self):
        themes = {"Shared": {}, "Default": {
            "A": {"status": "alias", "type": "StaticResource", "key": "Missing"}}}
        follow_aliases(themes)
        self.assertEqual(themes["Default"]["A"]["resolved"],
                         {"status": "dangling", "key": "Missing"})

    def test_a_cycle_is_reported_rather_than_looped(self):
        themes = {"Shared": {}, "Default": {
            "A": {"status": "alias", "type": "StaticResource", "key": "B"},
            "B": {"status": "alias", "type": "StaticResource", "key": "A"},
        }}
        follow_aliases(themes)
        self.assertEqual(themes["Default"]["A"]["resolved"]["status"], "cycle")

    def test_a_brush_colour_is_followed_into_the_shared_dictionary(self):
        themes = {
            "Shared": {"Ink": {"status": "value", "type": "Color", "text": "#FF102030"}},
            "Default": {"Fill": {"status": "brush", "type": "SolidColorBrush",
                                 "color_ref": {"extension": "StaticResource", "key": "Ink"}}},
        }
        follow_aliases(themes)
        self.assertEqual(themes["Default"]["Fill"]["color"], "#FF102030")
        self.assertEqual(themes["Default"]["Fill"]["color_from"], "Ink")

    def test_a_brush_on_a_colour_we_cannot_see_is_left_unresolved(self):
        """SystemAccentColor is the OS's, and no amount of WinUI source has it."""
        themes = {"Shared": {}, "Default": {
            "Fill": {"status": "brush", "type": "SolidColorBrush",
                     "color_ref": {"extension": "ThemeResource", "key": "SystemAccentColor"}}}}
        follow_aliases(themes)
        self.assertNotIn("color", themes["Default"]["Fill"])
        self.assertEqual(themes["Default"]["Fill"]["color_unresolved"], "dangling")


class QuotedLiterals(unittest.TestCase):
    """The L5-theme twins say what WinUI holds. This is what keeps that true."""

    @classmethod
    def setUpClass(cls):
        if not DATABASE.exists():
            raise unittest.SkipTest(
                f"{DATABASE} has not been materialized; run "
                f"extract_winui_theme_resources.py against a WinUI 2.8.4 checkout. "
                f"Nothing is checking the literals quoted into generate_cases.py "
                f"until it has been.")
        cls.database = json.loads(DATABASE.read_text(encoding="utf-8"))

    def test_the_database_is_the_pinned_commit(self):
        self.assertEqual(self.database["source"]["commit"], PINNED_COMMIT)

    def test_every_quoted_literal_is_what_winui_holds(self):
        # The theme the harvested cases declare, which is the one the twins are
        # written against.
        themes = self.database["themes"]
        lookup = {**themes["Shared"], **themes["Light"]}
        for slug, key, literal, prop, _ in THEME_RESOURCE_CASES:
            with self.subTest(key=key):
                entry = lookup.get(key)
                self.assertIsNotNone(entry, f"{key} is not in the extracted dictionary")
                self.assertEqual(entry["status"], "value",
                                 f"{key} is a {entry['status']}, which no twin can inline")
                self.assertEqual(entry["text"], literal,
                                 f"L5-theme-{slug} inlines {literal!r} for {prop}, but WinUI "
                                 f"2.8.4 holds {entry['text']!r}")

    def test_the_brush_twin_matches_too(self):
        themes = self.database["themes"]
        lookup = {**themes["Shared"], **themes["Light"]}
        entry = lookup["SystemControlTransparentBrush"]
        landed = entry.get("resolved", entry) if entry["status"] == "alias" else entry
        self.assertEqual(landed["color"], "Transparent")


if __name__ == "__main__":
    unittest.main()
