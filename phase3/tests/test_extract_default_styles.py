#!/usr/bin/env python3

"""The default-style reconstruction: how a setter is read, and what it refuses.

The extractor's job is small and its failure mode is quiet, which is the worst
combination to leave unchecked. It turns a `<Setter Property="Padding"
Value="{ThemeResource ButtonPadding}"/>` into a record the runtime hands to its
ordinary attribute parser -- so a classification that got one case wrong would
not crash, it would hand a property the *string* `{ThemeResource ButtonPadding}`
and produce a number with nothing to trace it back to. Every branch of that
classification is exercised here against documents written for the purpose, so
a change to the model fails on a five-line fragment instead of on a two-megabyte
one.

Prefix resolution gets the same treatment for a different reason. A
`TargetType` is an attribute *value*: nothing in an XML parser resolves the
prefix in `local:TabView`, and the WinUI fragments bind `local:` while the
system `generic.xaml` binds nothing at all. A table that guessed would be right
for one tree and silently wrong for the other, and the symptom would be a style
filed under the wrong type -- which the runtime then applies to a type that is
spelled the same and is not the same type.

The reproducibility check needs the pinned sources and skips by name without
them, on the same rule as the sibling test: a bare checkout has no
microsoft-ui-xaml clone, and a check that passed quietly in that case would be
worth nothing.
"""

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

from extract_default_styles import (  # noqa: E402
    GENERIC_XAML,
    PINNED_DXAML_COMMIT,
    PINNED_WINUI_COMMIT,
    classify_value,
    head_commit,
    qualify_target_type,
    read_setters,
    read_style,
    split_styles,
    styles_in,
)

PRESENTATION = "http://schemas.microsoft.com/winfx/2006/xaml/presentation"
XAML_X = "http://schemas.microsoft.com/winfx/2006/xaml"
MUXC = "using:Microsoft.UI.Xaml.Controls"


def dictionary(body: str, namespaces: str = "") -> ET.Element:
    return ET.fromstring(f'<ResourceDictionary xmlns="{PRESENTATION}" xmlns:x="{XAML_X}" '
                         f'{namespaces}>{body}</ResourceDictionary>')


def setter(attributes: str, body: str = "") -> ET.Element:
    style = dictionary(f'<Style TargetType="Button"><Setter {attributes}>'
                       f"{body}</Setter></Style>")[0]
    return style


def one_value(attributes: str, body: str = "") -> dict:
    setters, skipped = read_setters(setter(attributes, body))
    assert skipped == 0, skipped
    assert len(setters) == 1, setters
    return setters[0]["value"]


class SetterValues(unittest.TestCase):
    """Every shape a `Setter.Value` can have, and what each one is recorded as."""

    def test_a_plain_attribute_is_a_literal(self):
        self.assertEqual(one_value('Property="Padding" Value="8,4,8,5"/>'.replace("/>", ""),
                                   "</Setter>".replace("</Setter>", "")),
                         {"kind": "literal", "text": "8,4,8,5"})

    def test_a_theme_resource_keeps_its_reference(self):
        # Not resolved here, and that is the point: resolving it would bake one
        # theme into the database, and the theme is a property of the case.
        self.assertEqual(one_value('Property="Padding" Value="{ThemeResource ButtonPadding}"'),
                         {"kind": "resource", "extension": "ThemeResource",
                          "key": "ButtonPadding"})

    def test_a_static_resource_is_the_same_shape(self):
        self.assertEqual(one_value('Property="Foreground" Value="{StaticResource Brand}"'),
                         {"kind": "resource", "extension": "StaticResource", "key": "Brand"})

    def test_the_brace_escape_is_not_an_extension(self):
        # `{}` is XAML's escape for a leading brace. Reading it as an extension
        # would turn a literal string into a lookup for a key nobody defines.
        self.assertEqual(one_value('Property="Text" Value="{}{guid here}"'),
                         {"kind": "literal", "text": "{guid here}"})

    def test_a_binding_is_refused_rather_than_taken_as_text(self):
        # A `{Binding}` in a setter is not the string "{Binding Foo}". Storing
        # it as one would hand a property a value the runtime never gives it.
        value = one_value('Property="Width" Value="{Binding Foo}"')
        self.assertEqual(value["kind"], "unsupported")
        self.assertEqual(value["extension"], "Binding")

    def test_an_object_element_primitive_is_a_literal(self):
        self.assertEqual(
            one_value('Property="MinWidth"',
                      f'<Setter.Value xmlns="{PRESENTATION}">'
                      f'<Double xmlns="{XAML_X}">64</Double></Setter.Value>'),
            {"kind": "literal", "type": "x:Double", "text": "64"})

    def test_a_static_resource_element_is_a_reference(self):
        self.assertEqual(
            one_value('Property="Foreground"',
                      f'<Setter.Value xmlns="{PRESENTATION}">'
                      f'<StaticResource ResourceKey="Brand"/></Setter.Value>'),
            {"kind": "resource", "extension": "StaticResource", "key": "Brand"})

    def test_a_control_template_is_opaque_and_is_not_dropped(self):
        # A key that exists as a template is a different fact from a key that
        # does not exist, and the loader downstream reports it as such.
        self.assertEqual(
            one_value('Property="Template"',
                      f'<Setter.Value xmlns="{PRESENTATION}">'
                      f'<ControlTemplate TargetType="Button"/></Setter.Value>'),
            {"kind": "opaque", "type": "ControlTemplate"})

    def test_an_unknown_value_element_is_unsupported_rather_than_guessed(self):
        value = one_value('Property="Content"',
                          f'<Setter.Value xmlns="{PRESENTATION}">'
                          f'<SomethingNew xmlns="{MUXC}"/></Setter.Value>')
        self.assertEqual(value["kind"], "unsupported")
        self.assertIn("SomethingNew", value["type"])

    def test_a_value_with_no_kind_at_all_is_unsupported(self):
        self.assertEqual(classify_value(None, [])["kind"], "unsupported")


class TargetTypes(unittest.TestCase):
    """Prefix resolution, which no XML parser does for an attribute value."""

    def test_an_unprefixed_target_type_is_the_presentation_namespace(self):
        self.assertEqual(qualify_target_type("Button", {}), ("Button", PRESENTATION))

    def test_a_prefixed_target_type_resolves_through_the_document(self):
        self.assertEqual(qualify_target_type("local:TabView", {"local": MUXC}),
                         ("TabView", MUXC))

    def test_an_undeclared_prefix_is_refused(self):
        # Rather than stripped. A `TabView` filed under the presentation
        # namespace because its prefix was thrown away is a muxc style
        # masquerading as a framework one.
        with self.assertRaises(SystemExit):
            qualify_target_type("mystery:TabView", {})


class Styles(unittest.TestCase):
    def test_an_unkeyed_style_is_implicit_and_a_keyed_one_is_not(self):
        document = dictionary(
            '<Style TargetType="Button"><Setter Property="MinWidth" Value="64"/></Style>'
            '<Style x:Key="Loud" TargetType="Button">'
            '<Setter Property="MinWidth" Value="200"/></Style>')
        implicit, keyed, collisions = split_styles(list(styles_in(document, {}, "test")))
        self.assertEqual(sorted(implicit), ["Button"])
        self.assertEqual(sorted(keyed), ["Loud"])
        self.assertEqual(collisions, 0)
        self.assertEqual(implicit["Button"]["setters"][0]["value"]["text"], "64")

    def test_the_last_style_for_a_type_wins_and_the_collision_is_counted(self):
        # Last-writer-wins is `MergedDictionary.AddNode`'s rule, and the reason
        # the input order is the project files' rather than a directory
        # listing's. A reader that saw only the winner could not tell a clean
        # table from one that resolved a conflict by position.
        document = dictionary(
            '<Style TargetType="Button"><Setter Property="MinWidth" Value="64"/></Style>'
            '<Style TargetType="Button"><Setter Property="MinWidth" Value="99"/></Style>')
        implicit, _, collisions = split_styles(list(styles_in(document, {}, "test")))
        self.assertEqual(collisions, 1)
        self.assertEqual(implicit["Button"]["setters"][0]["value"]["text"], "99")

    def test_setter_order_is_kept(self):
        # A style may legally set the same property twice and the last one
        # wins; sorting the setters would change which.
        document = dictionary(
            '<Style TargetType="Button">'
            '<Setter Property="MinWidth" Value="1"/>'
            '<Setter Property="MinHeight" Value="2"/>'
            '<Setter Property="MinWidth" Value="3"/></Style>')
        style = read_style(document[0], {}, "test")
        self.assertEqual([s["property"] for s in style["setters"]],
                         ["MinWidth", "MinHeight", "MinWidth"])

    def test_a_style_nested_in_a_template_is_not_a_default_style(self):
        # Root level only. Hoisting a Style out of a ControlTemplate would
        # invent an implicit style the runtime does not have.
        document = dictionary(
            '<Style TargetType="Button"><Setter Property="Template">'
            '<Setter.Value><ControlTemplate TargetType="Button">'
            '<Style TargetType="Border"><Setter Property="Width" Value="9"/></Style>'
            '</ControlTemplate></Setter.Value></Setter></Style>')
        implicit, _, _ = split_styles(list(styles_in(document, {}, "test")))
        self.assertEqual(sorted(implicit), ["Button"])

    def test_based_on_is_kept_as_the_reference_it_is_written_as(self):
        document = dictionary(
            '<Style TargetType="Button" BasedOn="{StaticResource BaseButtonStyle}"/>')
        style = read_style(document[0], {}, "test")
        self.assertEqual(style["based_on"],
                         {"kind": "resource", "extension": "StaticResource",
                          "key": "BaseButtonStyle"})

    def test_a_style_with_no_target_type_is_refused(self):
        document = dictionary('<Style/>')
        with self.assertRaises(SystemExit):
            read_style(document[0], {}, "test")

    def test_a_non_setter_child_is_counted_rather_than_ignored(self):
        document = dictionary(
            '<Style TargetType="Button">'
            '<Setter Property="MinWidth" Value="64"/>'
            f'<VisualStateManager.VisualStateGroups xmlns="{PRESENTATION}"/></Style>')
        setters, skipped = read_setters(document[0])
        self.assertEqual(len(setters), 1)
        self.assertEqual(skipped, 1)


def pinned_checkouts() -> tuple[Path, Path] | None:
    """The two checkouts, if a machine has them at the pinned commits."""
    candidates = [Path("/tmp/microsoft-ui-xaml"), Path("/tmp/microsoft-ui-xaml-2.8.4"),
                  Path("/tmp/winui-284")]
    dxaml = winui = None
    for path in candidates:
        if not (path / ".git").exists():
            continue
        try:
            commit = head_commit(path)
        except subprocess.CalledProcessError:
            continue
        if commit == PINNED_DXAML_COMMIT and (path / GENERIC_XAML).exists():
            dxaml = path
        elif commit == PINNED_WINUI_COMMIT:
            winui = path
    return (dxaml, winui) if dxaml and winui else None


class Reproducibility(unittest.TestCase):
    """Two runs are byte-identical, or the artifact cannot be a build product."""

    def test_two_runs_agree_byte_for_byte(self):
        checkouts = pinned_checkouts()
        if checkouts is None:
            self.skipTest(f"no microsoft-ui-xaml checkouts at {PINNED_DXAML_COMMIT[:8]} and "
                          f"{PINNED_WINUI_COMMIT[:8]}; the reconstruction cannot be run")
        dxaml, winui = checkouts
        script = SCRIPT_DIRECTORY / "extract_default_styles.py"
        with tempfile.TemporaryDirectory(prefix="openterminal-trackG-repro") as scratch:
            outputs = []
            for run in ("a", "b"):
                out = Path(scratch) / f"{run}.json"
                subprocess.run([sys.executable, str(script), "--dxaml", str(dxaml),
                                "--winui", str(winui), "--out", str(out)],
                               check=True, capture_output=True)
                outputs.append(out.read_bytes())
            self.assertEqual(outputs[0], outputs[1],
                             "the reconstruction is not reproducible")

            database = json.loads(outputs[0])
            # The layer the reader files these dictionaries under. Getting it
            # wrong puts the framework's floor on top of XamlControlsResources
            # and silently answers 56 layout-visible keys with the wrong value.
            self.assertEqual(database["layer"], "GlobalThemeResources")
            self.assertEqual(database["sources"]["system"]["commit"], PINNED_DXAML_COMMIT)
            self.assertEqual(database["sources"]["muxc"]["commit"], PINNED_WINUI_COMMIT)
            # The framework half is what makes the previously-unreachable keys
            # reachable; if it ever came out empty the whole thing would still
            # "work" and answer nothing.
            self.assertGreater(len(database["default_styles"]["system"]), 50)
            self.assertGreater(len(database["default_styles"]["muxc"]), 30)
            self.assertIn("SymbolThemeFontFamily", database["themes"]["Light"])


if __name__ == "__main__":
    unittest.main()
