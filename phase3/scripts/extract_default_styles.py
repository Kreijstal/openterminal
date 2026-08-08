#!/usr/bin/env python3
"""Reconstruct the default-style universe a WinUI 2 application boots into.

An application's `Application.Resources` is not one dictionary. It is a stack,
and until now this project had only the middle of it. What a `Button` in
Terminal's markup actually resolves against, bottom to top:

1. the framework's own `generic.xaml` -- the built-in styles and theme
   resources that `Windows.UI.Xaml.Controls.dll` carries. Every `Control` gets
   its `Template`, `Padding`, `MinHeight` and `FontSize` from here, through
   `Control.DefaultStyleKey`, and nothing in the markup mentions it;
2. `Microsoft.UI.Xaml.Controls.XamlControlsResources`, which Terminal's
   `App.xaml` merges as the first entry of `Application.Resources`. It is a
   `ResourceDictionary` whose `Source` points at WinUI 2's own merged theme
   resources, plus WinUI 2's own default styles for the muxc control set;
3. the application's own dictionaries.

`phase3/scripts/extract_winui_theme_resources.py` reconstructs the *theme
resource* half of (2). This script reconstructs the two halves that were
missing: all of (1), and the *default style* half of (2).

Provenance
----------

Both halves come out of pinned MIT source. Nothing here is reconstructed by
guess and nothing is read out of a closed SDK payload.

``dxaml/xcp/dxaml/themes/generic.xaml`` at ``microsoft/microsoft-ui-xaml``
    ``188f602b`` -- the commit wave 1 found publishing the actual XAML core.
    The system `generic.xaml` exists there as a single 2 MB file, and its own
    closing comment names what it is: "End Windows.UI.Xaml.Controls.dll
    resources". No merge machinery to reimplement -- the build only *splits*
    it, into `Styles.xaml` and `ThemeResources.xaml`, and compiles both to XBF
    (``themes/autogen/SplitGenericXaml.cs``, ``GenAllXbf.csproj``).

    The honest caveat, stated once and carried into the output: this is the
    WinUI 3 lineage of that file, not a byte-for-byte copy of the
    `Windows.UI.Xaml` build the oracle runs (10.0.26100.33158). It is the same
    code lineage and the same authorship, which is the best open source there
    is for it, and it is *not* a measurement. Every number it supplies is a
    candidate that the recorded corpus either confirms or refuses. Where the
    two disagree the corpus wins and the setter stays out -- see
    ``--report`` and the `gaps` section of the output.

``dev/**/*.xaml`` at ``microsoft/microsoft-ui-xaml`` ``4aa80ad6`` (tag 2.8.4)
    the muxc default styles, merged by exactly the machinery
    ``extract_winui_theme_resources.py`` already models -- same project files,
    same ordering, same conditional-XAML resolution, the `DefaultStyle` stream
    instead of the `ThemeResources` one. That module is imported rather than
    copied, so the two extractions cannot drift apart.

What comes out
--------------

Two things, in one JSON document, because they are read by two different parts
of the runtime and neither is useful without the other's provenance:

``themes``
    the theme dictionaries out of the system `generic.xaml`, in exactly the
    schema ``extract_winui_theme_resources.py`` emits, so that
    ``LoadThemeResources`` reads it with no change at all. This is the "OS
    half" that ``research/microsoft-ui-xaml/.../README.md`` recorded as
    missing: of the 28 keys Terminal names that WinUI 2.8.4 does not define,
    the ones that are framework resources rather than user personalisation are
    all here -- ``SymbolThemeFontFamily``, ``ControlContentThemeFontSize``,
    ``ContentControlThemeFontFamily`` and the rest.

``default_styles``
    the built-in style table: `TargetType` -> style, for the styles that carry
    no ``x:Key``. These are the ones ``CControl::GetBuiltInStyle`` finds, and
    they apply at the ``BaseValueSourceBuiltInStyle`` layer -- *below* an
    application's own implicit style, which is the whole reason the two have
    to be kept apart (``docs/design-notes/styles.md``).

``keyed_styles``
    the styles that do carry an ``x:Key``. Reachable only by
    ``Style="{StaticResource ...}"``, never implicitly. Kept because a key
    that exists as a style is a different fact from a key that does not
    exist.

Every setter is classified rather than flattened, on the same principle the
theme-resource extractor uses -- a database that said "539 setters" without
saying which ones carry a number would unblock nothing:

``literal``
    ``Value="8,5,8,6"``, or an object-element primitive. The text a property
    parser can read, spelled exactly as an inline attribute would spell it.
``resource``
    ``Value="{ThemeResource ButtonBackground}"`` or a
    ``<StaticResource ResourceKey="..."/>`` element. Kept as the reference it
    is written as; resolving it is the resource system's job, at the theme
    the case declares, and doing it here would bake one theme into the
    database.
``opaque``
    a ``ControlTemplate``, ``ItemsPanelTemplate``, ``TransitionCollection``,
    ``DataTemplate`` -- an object with no textual form. Recorded by type and
    deliberately not dropped: a style that sets ``Template`` is a different
    fact from one that does not.

Determinism: both commits are verified before anything is read, the merge
order comes from the project files rather than from a directory listing, and
the output is sorted and formatted fixedly. Two runs are byte-identical.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import xml.etree.ElementTree as ET
from collections import Counter
from pathlib import Path
from typing import Any, Iterator

import extract_winui_theme_resources as winui

SCHEMA_VERSION = 1

# The commit publishing the XAML core, found in wave 1 and already the
# algorithm reference of record for this project (phase3/ROADMAP.md).
PINNED_DXAML_COMMIT = "188f602b27cdb47572b28c380e9c087b02e1ccee"
# The WinUI 2.8.4 tree, shared with the theme-resource extraction.
PINNED_WINUI_COMMIT = winui.PINNED_COMMIT

GENERIC_XAML = "dxaml/xcp/dxaml/themes/generic.xaml"

PRESENTATION = winui.PRESENTATION
XAML_X = winui.XAML_X

# The object types a Setter.Value can hold that have no textual form at all.
# Listed rather than matched by suffix so that a type added upstream shows up
# as an unclassified value and gets looked at, instead of being folded in.
OPAQUE_VALUE_TYPES = {
    "ControlTemplate", "ItemsPanelTemplate", "DataTemplate", "TransitionCollection",
    "Style", "Storyboard", "SolidColorBrush", "LinearGradientBrush", "AcrylicBrush",
    "ImageBrush", "RevealBorderBrush", "RevealBackgroundBrush", "FontFamily",
    "TargetPropertyPath", "SetterBaseCollection", "AnimatedIcon", "AnimatedIconSource",
}

# Markup extensions a Setter.Value may carry. Anything else is refused rather
# than treated as a literal: `{Binding Foo}` in a setter is not the string
# "{Binding Foo}", and storing it as one would hand a property a value the
# runtime never gives it.
RESOURCE_EXTENSIONS = {"StaticResource", "ThemeResource"}


def head_commit(repository: Path) -> str:
    return subprocess.run(["git", "-C", str(repository), "rev-parse", "HEAD"],
                          check=True, capture_output=True, text=True).stdout.strip()


def local_name(tag: str) -> str:
    return winui.clark(tag)[1]


# --- namespaces ---------------------------------------------------------------
def namespace_map(path: Path) -> dict[str, str]:
    """prefix -> URI for one document.

    ElementTree resolves namespaces on tags and throws the declarations away,
    but a `TargetType="local:TabView"` is an attribute *value*: nothing
    resolves it, and the prefix means nothing without the document that bound
    it. WinUI's fragments bind `local:` to `Microsoft.UI.Xaml.Controls` and
    `primitives:` to its Primitives, and the system `generic.xaml` binds
    nothing at all -- so a table that guessed would be right for one tree and
    silently wrong for the other.
    """
    mapping: dict[str, str] = {}
    for event, payload in ET.iterparse(path, events=("start-ns", "start")):
        if event == "start-ns":
            prefix, uri = payload
            mapping.setdefault(prefix, uri)
        else:
            # Declarations that matter are on the root; stop before reading 2 MB
            # of body for a table that is already complete.
            break
    return mapping


def qualify_target_type(raw: str, namespaces: dict[str, str]) -> tuple[str, str]:
    """(short name, declaring namespace) for a `TargetType` as written.

    The short name is what this project's style engine keys on -- it is the
    name the markup writes -- and the namespace is what says whether two trees
    that both say `TabView` mean the same type.
    """
    prefix, _, name = raw.rpartition(":")
    if not prefix:
        return name, PRESENTATION
    uri = namespaces.get(prefix)
    if uri is None:
        raise SystemExit(f"TargetType='{raw}' uses a prefix the document does not declare")
    return name, uri


# --- setters ------------------------------------------------------------------
def classify_value(text: str | None, children: list[ET.Element]) -> dict[str, Any]:
    """What one `Setter` puts in the property, as a classified record."""
    if text is not None:
        extension = winui.parse_extension(text)
        if extension is None:
            escaped = text.strip()
            # `{}` is XAML's escape for a leading brace, and the two characters
            # are not part of the value.
            if escaped.startswith("{}"):
                return {"kind": "literal", "text": escaped[2:]}
            return {"kind": "literal", "text": text.strip()}
        name, argument = extension
        if name not in RESOURCE_EXTENSIONS:
            return {"kind": "unsupported", "extension": name, "text": text.strip()}
        return {"kind": "resource", "extension": name, "key": argument}

    if len(children) != 1:
        return {"kind": "unsupported", "reason": f"{len(children)} value elements"}
    value = children[0]
    uri, name = winui.clark(value.tag)

    if uri == PRESENTATION and name == "StaticResource":
        return {"kind": "resource", "extension": "StaticResource",
                "key": value.get("ResourceKey", "")}

    qualified = f"{{{uri}}}{name}" if uri else name
    if qualified in winui.VALUE_TYPES:
        return {"kind": "literal", "type": winui.VALUE_TYPES[qualified],
                "text": "".join(value.itertext()).strip()}
    if uri == PRESENTATION and name in OPAQUE_VALUE_TYPES:
        return {"kind": "opaque", "type": name}
    return {"kind": "unsupported", "reason": "value element", "type": qualified}


def read_setters(style: ET.Element) -> tuple[list[dict[str, Any]], int]:
    """Every `Setter` of one style, in declaration order, plus what was skipped.

    Order is kept because a style may legally set the same property twice and
    the last one wins; sorting them would change which.
    """
    setters: list[dict[str, Any]] = []
    skipped = 0
    for child in style:
        uri, name = winui.clark(child.tag)
        if uri != PRESENTATION or name != "Setter":
            # A `Style` body holds Setters and nothing else in either tree. A
            # `Style.Setters` wrapper or a VisualState would be a shape this
            # does not model, so it is counted rather than ignored.
            skipped += 1
            continue
        prop = child.get("Property")
        if prop is None:
            skipped += 1
            continue
        value_children = [grandchild for element in child
                          if winui.clark(element.tag) == (PRESENTATION, "Setter.Value")
                          for grandchild in element]
        setters.append({"property": prop,
                        "value": classify_value(child.get("Value"), value_children)})
    return setters, skipped


def read_style(style: ET.Element, namespaces: dict[str, str], origin: str) -> dict[str, Any]:
    raw = style.get("TargetType")
    if raw is None:
        raise SystemExit(f"{origin}: a Style with no TargetType, which XAML does not allow")
    target, uri = qualify_target_type(raw, namespaces)
    setters, skipped = read_setters(style)
    record: dict[str, Any] = {
        "target_type": target,
        "target_namespace": uri,
        "setters": setters,
        "source": origin,
    }
    if skipped:
        record["skipped_children"] = skipped
    based_on = style.get("BasedOn")
    if based_on is not None:
        extension = winui.parse_extension(based_on)
        record["based_on"] = ({"kind": "resource", "extension": extension[0], "key": extension[1]}
                              if extension else {"kind": "unsupported", "text": based_on})
    key = style.get(f"{{{XAML_X}}}Key")
    if key:
        record["key"] = key
    return record


# --- the two sources ----------------------------------------------------------
def styles_in(document: ET.Element, namespaces: dict[str, str], origin: str,
              ) -> Iterator[dict[str, Any]]:
    """The `Style` entries of one dictionary, root level only.

    Root level only, and deliberately: a `Style` nested inside a
    `ControlTemplate` or a theme dictionary is not a default style, and
    hoisting it would invent an implicit style the runtime does not have.
    """
    for node in document:
        uri, name = winui.clark(node.tag)
        if uri == PRESENTATION and name == "Style":
            yield read_style(node, namespaces, origin)


def read_system_generic(dxaml: Path) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    """The system `generic.xaml`: its theme dictionaries and its styles.

    One file, no merge: the build splits it rather than assembling it, so
    there is no ordering model to get wrong here. What there is instead is the
    same theme-dictionary rule the merged tree has -- a `Dark` dictionary is
    the `Default` one -- and this file spells it `Default` already.
    """
    path = dxaml / GENERIC_XAML
    if not path.exists():
        raise SystemExit(f"not a microsoft-ui-xaml checkout with the XAML core: {path}")
    namespaces = namespace_map(path)
    document = ET.parse(path).getroot()

    merged = winui.Merged()
    for node in document:
        uri, name = winui.clark(node.tag)
        if uri == PRESENTATION and name == "ResourceDictionary.ThemeDictionaries":
            merged._add(node)  # noqa: SLF001 -- the merge model is the module's, not a copy
        elif node.get(f"{{{XAML_X}}}Key") is not None:
            merged._add(node)  # noqa: SLF001

    themes: dict[str, dict[str, Any]] = {
        "Shared": {key: winui.classify(key, node) for key, node in merged.root.items()},
    }
    for theme, entries in sorted(merged.themes.items()):
        themes[theme] = {key: winui.classify(key, node) for key, node in entries.items()}
    winui.follow_aliases(themes)

    return themes, list(styles_in(document, namespaces, GENERIC_XAML))


def read_muxc_default_styles(winui_root: Path, flavour: str, os_version: str,
                             ) -> tuple[list[dict[str, Any]], list[dict[str, str]]]:
    """WinUI 2's default styles, through the project's existing merge model.

    The pages, their order and their conditional-XAML resolution are all
    ``extract_winui_theme_resources``'s; only the stream differs. Styles are
    read per file rather than through ``Merged``, because ``Merged`` is keyed
    by ``x:Key`` and a default style has none -- the very entries it counts and
    drops (`unkeyed_entries_skipped`) are the ones wanted here. Last writer
    still wins, applied per target type below.
    """
    pages = winui.selected_pages(winui_root, flavour, "DefaultStyle")
    api_version = winui.API_CONTRACT[os_version]
    styles: list[dict[str, Any]] = []
    files: list[dict[str, str]] = []
    for version in winui.VERSIONS[: winui.VERSIONS.index(os_version) + 1]:
        for page in pages:
            if page.version != version or not page.path.exists():
                continue
            origin = page.path.relative_to(winui_root).as_posix()
            namespaces = namespace_map(page.path)
            document = ET.parse(page.path).getroot()
            # The same rule and the same reason as the theme extraction: the
            # merge feeds unstripped files forward and strips only the result,
            # so an element that a contract removes must lose its style, not
            # merely be rewritten.
            winui.strip_conditional(document, api_version)
            styles.extend(styles_in(document, namespaces, origin))
            files.append({"file": origin, "version": version, "priority": page.priority})
    return styles, files


# --- assembling ---------------------------------------------------------------
def split_styles(styles: list[dict[str, Any]]) -> tuple[dict[str, Any], dict[str, Any], int]:
    """(implicit by target type, keyed by x:Key, how many implicit ones collided).

    Last writer wins for both, which is `MergedDictionary.AddNode`'s rule and
    the reason the input order above is the project files' rather than a
    directory listing's. Collisions are counted rather than hidden: a target
    type two fragments both style is a real fact about the merge, and a
    reader that saw only the winner could not tell a clean table from one that
    resolved a conflict by position.
    """
    implicit: dict[str, Any] = {}
    keyed: dict[str, Any] = {}
    collisions = 0
    for style in styles:
        if "key" in style:
            keyed[style["key"]] = style
            continue
        target = style["target_type"]
        if target in implicit:
            collisions += 1
        implicit[target] = style
    return implicit, keyed, collisions


def count_setters(styles: dict[str, Any]) -> Counter:
    counter: Counter = Counter()
    for style in styles.values():
        for setter in style["setters"]:
            counter[setter["value"]["kind"]] += 1
    return counter


def extract(dxaml: Path, winui_root: Path, flavour: str, os_version: str) -> dict[str, Any]:
    themes, system_styles = read_system_generic(dxaml)
    muxc_styles, muxc_files = read_muxc_default_styles(winui_root, flavour, os_version)

    system_implicit, system_keyed, system_collisions = split_styles(system_styles)
    muxc_implicit, muxc_keyed, muxc_collisions = split_styles(muxc_styles)

    # Kept in two tables rather than merged into one. WinUI 2's default style
    # for a type does not replace the system's -- both are found, at two
    # different layers, and a control can end up with setters from each
    # (docs/design-notes/styles.md). Merging them here would decide that
    # question wrongly and invisibly.
    return {
        "schema_version": SCHEMA_VERSION,
        # Which layer of `Application.Resources` the `themes` below are. The
        # framework's own generic.xaml is the floor: XamlControlsResources
        # merges *over* it, so a key WinUI 2 redefines answers from WinUI 2 and
        # one it leaves alone -- three of the `SystemControl*` brushes among
        # them -- falls through to here. The reader refuses to guess; see
        # `LayerOf` in phase3/layout/src/resources.cpp.
        "layer": "GlobalThemeResources",
        "sources": {
            "system": {
                "repository": "https://github.com/microsoft/microsoft-ui-xaml",
                "commit": PINNED_DXAML_COMMIT,
                "file": GENERIC_XAML,
                "provides": "Windows.UI.Xaml built-in styles and theme resources",
                "caveat": "the WinUI 3 lineage of the system generic.xaml, not the "
                          "Windows.UI.Xaml build the oracle records; every value is a "
                          "candidate the corpus confirms or refuses",
            },
            "muxc": {
                "repository": "https://github.com/microsoft/microsoft-ui-xaml",
                "commit": PINNED_WINUI_COMMIT,
                "provides": "Microsoft.UI.Xaml.Controls default styles, DefaultStyle stream",
                "controls_resources_version": flavour,
                "target_os_version": os_version,
                "api_contract_version": winui.API_CONTRACT[os_version],
                "merged_files": len(muxc_files),
            },
        },
        "totals": {
            "system": {
                "implicit_styles": len(system_implicit),
                "keyed_styles": len(system_keyed),
                "implicit_collisions": system_collisions,
                "setters": dict(sorted(count_setters(system_implicit).items())),
                "theme_keys": {theme: len(entries) for theme, entries in sorted(themes.items())},
            },
            "muxc": {
                "implicit_styles": len(muxc_implicit),
                "keyed_styles": len(muxc_keyed),
                "implicit_collisions": muxc_collisions,
                "setters": dict(sorted(count_setters(muxc_implicit).items())),
            },
        },
        "files": muxc_files,
        "themes": {theme: dict(sorted(entries.items())) for theme, entries in sorted(themes.items())},
        "default_styles": {
            "system": dict(sorted(system_implicit.items())),
            "muxc": dict(sorted(muxc_implicit.items())),
        },
        "keyed_styles": {
            "system": dict(sorted(system_keyed.items())),
            "muxc": dict(sorted(muxc_keyed.items())),
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dxaml", type=Path, required=True,
                        help=f"microsoft-ui-xaml checkout at {PINNED_DXAML_COMMIT[:8]}, which "
                             f"publishes the XAML core and the system generic.xaml")
    parser.add_argument("--winui", type=Path, required=True,
                        help=f"microsoft-ui-xaml checkout at {PINNED_WINUI_COMMIT[:8]} (2.8.4)")
    parser.add_argument("--out", type=Path, required=True, help="where to write the database")
    parser.add_argument("--flavour", default="Version2", choices=["Version1", "Version2"],
                        help="ControlsResourcesVersion; Version2 is what XamlControlsResources "
                             "defaults to from WinUI 2.6 on, and what Terminal gets")
    parser.add_argument("--os-version", default="21H1", choices=winui.VERSIONS,
                        help="the merge stage to emit; 21H1 is what a current Windows runs")
    parser.add_argument("--allow-unpinned", action="store_true",
                        help="read checkouts at other commits, accepting that the models here "
                             "may no longer describe them")
    args = parser.parse_args()

    unpinned: dict[str, str] = {}
    for name, path, pin in (("dxaml", args.dxaml, PINNED_DXAML_COMMIT),
                            ("winui", args.winui, PINNED_WINUI_COMMIT)):
        resolved = path.resolve()
        commit = head_commit(resolved)
        if commit != pin:
            if not args.allow_unpinned:
                parser.error(f"{resolved} is at {commit}, not the pinned {pin}; "
                             f"pass --allow-unpinned to read it anyway")
            unpinned[name] = commit

    database = extract(args.dxaml.resolve(), args.winui.resolve(), args.flavour, args.os_version)
    for name, commit in unpinned.items():
        database["sources"][name]["commit"] = commit
        database["sources"][name]["unpinned"] = True

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(database, indent=1, sort_keys=True) + "\n", encoding="utf-8")

    totals = database["totals"]
    for half in ("system", "muxc"):
        counts = totals[half]
        print(f"  {half}")
        print(f"    implicit styles  {counts['implicit_styles']:>5}"
              f"   (collisions {counts['implicit_collisions']})")
        print(f"    keyed styles     {counts['keyed_styles']:>5}")
        detail = "  ".join(f"{kind} {n}" for kind, n in counts["setters"].items())
        print(f"    setters                {detail}")
    for theme, count in totals["system"]["theme_keys"].items():
        print(f"    theme {theme:<14} {count:>5}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
