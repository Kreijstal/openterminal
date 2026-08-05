#!/usr/bin/env python3
"""Extract WinUI 2's theme ResourceDictionary from the pinned open source.

Terminal's markup names 1,500-odd resource keys and defines about a fifth of
them itself. The rest come from a dictionary it never writes down: the one
``Application.Resources`` holds. Part of that dictionary is WinUI 2's, published
under an MIT licence in ``microsoft/microsoft-ui-xaml``; the rest belongs to the
OS's ``Windows.UI.Xaml`` and is not published at all. This script produces the
half that is, and — by producing it — makes the other half countable, which is
the number that decides what to do next.

**This is not a XAML parser for the file on disk.** WinUI's theme dictionary
does not exist as a file: it is merged at build time out of ~120 per-control
XAML fragments by ``tools/CustomTasks/BatchMergeXaml.cs``, in an order the
project files decide, once per target OS version, with conditional-XAML
namespaces stripped per API contract. Reading the fragments and unioning them
would produce a dictionary the runtime never has — the merge is last-writer-wins
per key, so the union's answer for a key several fragments define is a coin
toss. So the merge is reimplemented here, from those three files:

``dev/dll/Microsoft.UI.Xaml.vcxproj``
    which ``.vcxitems`` are imported, hence which ``<Page>`` items exist, and
    the ``ControlsResourcesVersion`` -> ``ControlsResourcesVersion1/2`` mapping
    the merge filters on.
``dev/dll/Microsoft.UI.Xaml.Common.targets``
    the ordering: ``Priority`` 1..8 then unprioritised, then split by
    ``Version`` (RS1..21H1) and ``Type`` (ThemeResources/DefaultStyle).
``tools/CustomTasks/BatchMergeXaml.cs`` and ``MergedDictionary.cs``
    the merge itself: each OS version's output is the base of the next, a
    later entry replaces an earlier one with the same key, an entry landing in
    a theme dictionary removes the same key from the root dictionary, and a
    ``Dark`` theme dictionary is folded into ``Default``.
``tools/CustomTasks/StripNamespaces.cs``
    which conditional-XAML elements survive at a given API contract version.

Feature flags: a release build sets every ``Feature*Enabled`` to ``true``
(``FeatureAreas.props``), so every import is taken. Only the innerloop solution
turns any of them off, and that is not what ships.

What comes out
--------------

A key can be one of four things, and the difference is the whole point of the
output — a database that answered "3,000 keys" without saying which ones carry
a number would unblock nothing:

``value``
    a literal this project can hand to a property parser: ``x:Double``,
    ``x:Int32``, ``x:String``, ``x:Boolean``, ``Thickness``, ``CornerRadius``,
    ``GridLength``, ``Color``, ``Duration``, ``FontFamily``.
``alias``
    a ``<StaticResource x:Key="A" ResourceKey="B"/>`` entry. The chain is kept
    verbatim *and* followed within the same theme; ``resolved`` names where it
    landed, or the reason it did not.
``brush``
    a ``SolidColorBrush`` and friends. Typed, with its ``Color`` kept as the
    literal or the reference it is written as, and — where that reference leads
    to a ``Color`` in the same theme — with the colour it lands on. A brush is
    not a value in the sense a ``Thickness`` is; what makes it usable anyway is
    that the attribute form of a brush *is* a colour, so a resolved colour is
    exactly the text ``Background="#FF1F1F1F"`` would carry. ``Opacity`` is
    recorded and not folded in: nothing downstream paints, and pretending a
    75%-opaque brush is a different colour would be inventing a number.
``opaque``
    a ``Style``, ``ControlTemplate``, ``Storyboard``, ``KeySpline``, a
    converter from a ``using:`` namespace — recorded by key and type, and
    deliberately not dropped. A key that exists as a template is a different
    fact from a key that does not exist, and the classifier downstream needs to
    tell them apart.

Determinism: the output is sorted and formatted fixedly, the source commit is
verified before anything is read, and the merge order is derived from the
project files rather than from directory listing order.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import xml.etree.ElementTree as ET
from collections import Counter
from pathlib import Path
from typing import Any, Iterator

SCHEMA_VERSION = 1

# The WinUI 2.8.4 tree this script is written against. Every fact above is a
# fact about this commit; run it against another and the merge model may not
# hold, so it refuses rather than producing a plausible-looking wrong answer.
PINNED_COMMIT = "4aa80ad6d272241a6a603f85507063e9fb6bcf92"

PRESENTATION = "http://schemas.microsoft.com/winfx/2006/xaml/presentation"
XAML_X = "http://schemas.microsoft.com/winfx/2006/xaml"
MSBUILD_NS = "http://schemas.microsoft.com/developer/msbuild/2003"

# StripNamespaces.universalApiContractVersionMapping.
API_CONTRACT: dict[str, int] = {
    "RS1": 3, "RS2": 4, "RS3": 5, "RS4": 6, "RS5": 7, "19H1": 8, "21H1": 14,
}
# BatchMergeXaml.Execute() runs the stages in this order and feeds each one's
# output into the next.
VERSIONS = ["RS1", "RS2", "RS3", "RS4", "RS5", "19H1", "21H1"]

CONDITIONAL = re.compile(r"^(.*)\?IsApiContract(Not)?Present\("
                         r"Windows\.Foundation\.UniversalApiContract,\s*(\d+)\)$")

# --- what a key can hold ------------------------------------------------------

# Types whose content is a literal a property parser can read. The value is the
# name this project's own resource system uses for the same shape, so the
# database drops straight into it.
VALUE_TYPES: dict[str, str] = {
    f"{{{XAML_X}}}Double": "x:Double",
    f"{{{XAML_X}}}Int32": "x:Int32",
    f"{{{XAML_X}}}String": "x:String",
    f"{{{XAML_X}}}Boolean": "x:Boolean",
    f"{{{PRESENTATION}}}Thickness": "Thickness",
    f"{{{PRESENTATION}}}CornerRadius": "CornerRadius",
    f"{{{PRESENTATION}}}GridLength": "GridLength",
    f"{{{PRESENTATION}}}Color": "Color",
    f"{{{PRESENTATION}}}Duration": "Duration",
    f"{{{PRESENTATION}}}FontFamily": "FontFamily",
    f"{{{PRESENTATION}}}FontWeight": "FontWeight",
}

# Brushes: typed, colour kept, but not a value. Listed rather than matched on
# the name ending in "Brush" so that a type added upstream shows up as opaque
# and gets looked at, instead of being silently classified.
BRUSH_TYPES = {
    "SolidColorBrush", "AcrylicBrush", "LinearGradientBrush", "RevealBorderBrush",
    "RevealBackgroundBrush", "RadialGradientBrush", "ImageBrush", "XamlCompositionBrushBase",
}

MARKUP_EXTENSION = re.compile(r"^\{\s*(\w+)\s+(?:ResourceKey\s*=\s*)?([^}]*?)\s*\}$")


def clark(tag: str) -> tuple[str, str]:
    if tag.startswith("{"):
        uri, _, local = tag[1:].partition("}")
        return uri, local
    return "", tag


# --- msbuild: which pages, in which order -------------------------------------
def imported_vcxitems(vcxproj: Path) -> list[Path]:
    """The shared item projects the product DLL imports, in import order.

    Every ``Condition`` on these is a ``Feature*Enabled`` check, and a release
    build sets all of them, so the conditions are not evaluated -- but they are
    counted, so that a condition of some other shape upstream would be noticed
    here rather than silently including a page that does not ship.
    """
    text = vcxproj.read_text(encoding="utf-8-sig")
    items: list[Path] = []
    for match in re.finditer(r'<Import\s+Project="([^"]+)"[^>]*Label="Shared"([^>]*)>', text):
        project, rest = match.group(1), match.group(2)
        condition = re.search(r'Condition="([^"]*)"', rest)
        if condition and "Feature" not in condition.group(1):
            raise SystemExit(
                f"{vcxproj.name}: import of {project} has a condition this script does not "
                f"model: {condition.group(1)}"
            )
        items.append((vcxproj.parent / project.replace("\\", "/")).resolve())
    if not items:
        raise SystemExit(f"{vcxproj}: no shared item projects imported")
    return items


class Page:
    __slots__ = ("path", "version", "type", "priority", "resources_version", "origin")

    def __init__(self, path: Path, meta: dict[str, str], origin: str) -> None:
        self.path = path
        self.version = meta.get("Version", "Undefined")
        self.type = meta.get("Type", "Undefined")
        self.priority = meta.get("Priority", "Undefined")
        # ControlsResourcesVersion2 is set unless the page says Version1, and
        # ControlsResourcesVersion1 unless it says Version2 -- so an unmarked or
        # "Any" page is in both. (Microsoft.UI.Xaml.vcxproj, SharedPage Update.)
        self.resources_version = meta.get("ControlsResourcesVersion", "Any")
        self.origin = origin


def pages_from(vcxitems: Path) -> Iterator[Page]:
    root = ET.parse(vcxitems).getroot()
    directory = vcxitems.parent
    for item in root.iter(f"{{{MSBUILD_NS}}}Page"):
        include = item.get("Include", "")
        # $(MSBuildThisFileDirectory) is the only property these use.
        relative = include.replace("$(MSBuildThisFileDirectory)", "").replace("\\", "/")
        meta = {clark(child.tag)[1]: (child.text or "").strip() for child in item}
        yield Page(directory / relative, meta, vcxitems.name)


def selected_pages(winui: Path, flavour: str, page_type: str) -> list[Page]:
    """Pages for one merge stream, in the order BatchMergeXaml would see them."""
    pages: list[Page] = []
    for vcxitems in imported_vcxitems(winui / "dev/dll/Microsoft.UI.Xaml.vcxproj"):
        if not vcxitems.exists():
            raise SystemExit(f"imported item project is missing: {vcxitems}")
        pages.extend(pages_from(vcxitems))

    excluded = "Version1" if flavour == "Version2" else "Version2"
    wanted = [p for p in pages if p.type == page_type and p.resources_version != excluded]

    # CategorizeSharedPages: Priority 1..8, then everything unprioritised. A
    # stable sort keeps declaration order inside each bucket, which is what the
    # sequence of Condition'd ItemGroups produces.
    order = {str(n): n for n in range(1, 9)}
    return sorted(wanted, key=lambda p: order.get(p.priority, 99))


# --- conditional XAML ---------------------------------------------------------
def strip_conditional(element: ET.Element, api_version: int) -> None:
    """StripNamespaces.StripNamespaceForAPIVersion, as a tree walk.

    Two steps in the original, and the difference between them matters:

    1. elements and attributes whose namespace is in the removal list are
       deleted. That list is built from the *presentation* URI only, so a
       ``using:...?IsApiContractNotPresent(...)`` namespace is never removed
       however the condition reads;
    2. a regex then strips the ``contractNPresent:`` prefix from what is left.
       It is case-sensitive, and the ``using:`` conditionals in this tree are
       bound to prefixes spelled ``muxContract7Present`` and
       ``primitiveContract7Present``, which it does not match -- so those keep
       their conditional namespace and are classified as foreign types, which
       is what they are.

    Stripping the prefix from an element leaves it in the document's default
    namespace, which is the presentation one; stripping it from an attribute
    leaves the attribute with no namespace at all. Hence the two different
    rewrites below.
    """
    def verdict(uri: str) -> tuple[bool, str | None]:
        """(survives, rewritten base) -- a base of None means leave it alone."""
        match = CONDITIONAL.match(uri)
        if not match or match.group(1) != PRESENTATION:
            return True, None
        negated, version = bool(match.group(2)), int(match.group(3))
        present = api_version >= version
        return (not present if negated else present), PRESENTATION

    for parent in list(element.iter()):
        for child in list(parent):
            uri, local = clark(child.tag)
            survives, base = verdict(uri)
            if not survives:
                parent.remove(child)
            elif base is not None:
                child.tag = f"{{{base}}}{local}"
        for name in list(parent.attrib):
            uri, local = clark(name)
            if not uri:
                continue
            survives, base = verdict(uri)
            if base is None:
                continue
            value = parent.attrib.pop(name)
            if survives:
                parent.attrib[local] = value


# --- the merge ----------------------------------------------------------------
class Merged:
    """MergedDictionary, restricted to what this extraction reads.

    One root dictionary plus one dictionary per theme key. Entries are keyed by
    ``x:Key``; a later entry replaces an earlier one in the same dictionary, and
    an entry that lands in a theme dictionary removes the same key from the
    root -- both straight out of ``MergedDictionary.AddNode``.
    """

    def __init__(self) -> None:
        self.root: dict[str, ET.Element] = {}
        self.themes: dict[str, dict[str, ET.Element]] = {}
        self.unkeyed = 0

    def merge_file(self, path: Path) -> None:
        root = ET.parse(path).getroot()
        for node in list(root):
            self._add(node)

    def _add(self, node: ET.Element, theme: str | None = None) -> None:
        uri, local = clark(node.tag)
        if uri == PRESENTATION and local == "ResourceDictionary.ThemeDictionaries":
            for dictionary in node:
                key = dictionary.get(f"{{{XAML_X}}}Key")
                if not key:
                    continue
                # A Dark dictionary and a Default one cannot coexist in a merged
                # dictionary -- Dark would shadow Default entirely. The build
                # task folds Dark into Default; so does this.
                key = "Default" if key == "Dark" else key
                for entry in dictionary:
                    self._add(entry, key)
            return
        if uri == PRESENTATION and local == "ResourceDictionary.MergedDictionaries":
            raise SystemExit("a MergedDictionaries section appeared; the merge model does not "
                             "cover it, and quietly ignoring it would lose keys")

        key = node.get(f"{{{XAML_X}}}Key")
        if not key:
            # An unkeyed <Style TargetType="..."/> is an implicit style: real
            # content, but not reachable by key, which is all this database is
            # about. Counted so the total is honest.
            self.unkeyed += 1
            return
        if theme is None:
            self.root[key] = node
        else:
            self.themes.setdefault(theme, {})[key] = node
            self.root.pop(key, None)


# --- classification -----------------------------------------------------------
def parse_extension(text: str) -> tuple[str, str] | None:
    match = MARKUP_EXTENSION.match(text.strip())
    if not match or text.strip().startswith("{}"):
        return None
    return match.group(1), match.group(2)


def classify(key: str, node: ET.Element) -> dict[str, Any]:
    uri, local = clark(node.tag)
    qualified = f"{{{uri}}}{local}" if uri else local

    if uri == PRESENTATION and local == "StaticResource":
        target = node.get("ResourceKey", "")
        return {"status": "alias", "type": "StaticResource", "key": target}

    if qualified in VALUE_TYPES:
        text = "".join(node.itertext()).strip()
        if not text:
            return {"status": "opaque", "type": VALUE_TYPES[qualified], "reason": "empty content"}
        return {"status": "value", "type": VALUE_TYPES[qualified], "text": text}

    if uri == PRESENTATION and local in BRUSH_TYPES:
        entry: dict[str, Any] = {"status": "brush", "type": local}
        colour = node.get("Color")
        if colour is not None:
            extension = parse_extension(colour)
            if extension:
                entry["color_ref"] = {"extension": extension[0], "key": extension[1]}
            else:
                entry["color"] = colour.strip()
        opacity = node.get("Opacity")
        if opacity is not None:
            entry["opacity"] = opacity.strip()
        return entry

    if not uri:
        return {"status": "opaque", "type": local, "reason": "no namespace"}
    if uri == XAML_X:
        return {"status": "opaque", "type": f"x:{local}", "reason": "x-namespace type"}
    if uri != PRESENTATION:
        return {"status": "opaque", "type": f"{uri}#{local}", "reason": "type from another namespace"}
    return {"status": "opaque", "type": local, "reason": "no textual value"}


def follow_aliases(themes: dict[str, dict[str, Any]]) -> None:
    """Resolve alias chains and brush colour references inside each theme.

    Resolution is kept *alongside* what the source wrote rather than replacing
    it: the chain is a fact about the pinned commit, and a consumer that wants
    the value should not have to trust this function to have walked it
    correctly. Both directions are therefore in the output.

    A theme dictionary is looked up before the theme-independent one, which is
    the order the merge already guarantees is unambiguous -- an entry that
    reached a theme dictionary was removed from the root one.
    """
    shared = themes.get("Shared", {})

    def walk(entries: dict[str, Any], start: str) -> dict[str, Any]:
        seen = [start]
        current = start
        while True:
            target = entries.get(current) or shared.get(current)
            if target is None:
                return {"status": "dangling", "key": current}
            if target["status"] != "alias":
                return dict(target, key=current)
            current = target["key"]
            if current in seen:
                return {"status": "cycle", "chain": seen + [current]}
            seen.append(current)

    for entries in themes.values():
        for key, entry in entries.items():
            if entry["status"] == "alias":
                chain = walk(entries, entry["key"])
                entry["resolved"] = ({"status": "cycle", "chain": [key] + chain["chain"]}
                                     if chain["status"] == "cycle" else chain)
            elif entry["status"] == "brush" and "color_ref" in entry:
                landed = walk(entries, entry["color_ref"]["key"])
                if landed["status"] == "value" and landed["type"] == "Color":
                    entry["color"] = landed["text"]
                    entry["color_from"] = landed["key"]
                else:
                    entry["color_unresolved"] = landed["status"]

    # A brush reached through an alias should report the colour the brush has,
    # not just the type -- otherwise following the chain leaves the caller one
    # step short of the only field it can use.
    for entries in themes.values():
        for entry in entries.values():
            resolved = entry.get("resolved")
            if not resolved or resolved.get("status") != "brush":
                continue
            target = entries.get(resolved["key"]) or shared.get(resolved["key"])
            if target and "color" in target:
                resolved["color"] = target["color"]


def extract(winui: Path, flavour: str, os_version: str) -> dict[str, Any]:
    api_version = API_CONTRACT[os_version]
    merged = Merged()
    files: list[dict[str, str]] = []

    # Theme resources and default styles are two separate BatchMergeXaml runs
    # producing two separate files, but both land in Application.Resources and
    # a lookup does not know which it came from, so they merge into one database
    # here -- theme resources first, which is the order XamlControlsResources
    # merges them in.
    for page_type in ("ThemeResources", "DefaultStyle"):
        selected = selected_pages(winui, flavour, page_type)
        for version in VERSIONS[: VERSIONS.index(os_version) + 1]:
            for page in selected:
                if page.version != version or not page.path.exists():
                    continue
                merged.merge_file(page.path)
                files.append({
                    "file": page.path.relative_to(winui).as_posix(),
                    "type": page_type,
                    "version": version,
                    "priority": page.priority,
                })

    # Conditional XAML is resolved once, after the merge, because that is when
    # the real pipeline resolves it: each stage feeds the *unstripped* file into
    # the next, and only the final output is stripped. An entry whose element
    # does not survive loses its key entirely, so the strip has to be able to
    # take keys out of the dictionary rather than only rewrite them.
    def strip(entries: dict[str, ET.Element]) -> dict[str, Any]:
        holder = ET.Element("holder")
        for node in entries.values():
            holder.append(node)
        strip_conditional(holder, api_version)
        surviving = {id(node) for node in holder}
        return {key: classify(key, node)
                for key, node in entries.items() if id(node) in surviving}

    themes: dict[str, dict[str, Any]] = {"Shared": strip(merged.root)}
    for theme, entries in sorted(merged.themes.items()):
        themes[theme] = strip(entries)
    follow_aliases(themes)

    by_status: dict[str, Counter] = {}
    for theme, entries in themes.items():
        counter: Counter = Counter()
        for entry in entries.values():
            counter[entry["status"]] += 1
        by_status[theme] = counter

    return {
        "schema_version": SCHEMA_VERSION,
        "source": {
            "repository": "https://github.com/microsoft/microsoft-ui-xaml",
            "commit": PINNED_COMMIT,
            "controls_resources_version": flavour,
            "target_os_version": os_version,
            "api_contract_version": api_version,
            "merged_files": len(files),
        },
        "totals": {
            "keys": sum(len(v) for v in themes.values()),
            "unkeyed_entries_skipped": merged.unkeyed,
            "by_theme": {theme: dict(sorted(counter.items()))
                         for theme, counter in sorted(by_status.items())},
        },
        "files": files,
        "themes": {theme: dict(sorted(entries.items()))
                   for theme, entries in sorted(themes.items())},
    }


def head_commit(repo: Path) -> str:
    return subprocess.run(["git", "-C", str(repo), "rev-parse", "HEAD"],
                          check=True, capture_output=True, text=True).stdout.strip()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("winui", type=Path, help="microsoft-ui-xaml checkout at the pinned commit")
    parser.add_argument("--out", type=Path, required=True, help="where to write the database")
    parser.add_argument("--flavour", default="Version2", choices=["Version1", "Version2"],
                        help="ControlsResourcesVersion; Version2 is what XamlControlsResources "
                             "defaults to from WinUI 2.6 on, and what Terminal gets")
    parser.add_argument("--os-version", default="21H1", choices=VERSIONS,
                        help="the merge stage to emit; 21H1 is what a current Windows runs")
    parser.add_argument("--allow-unpinned", action="store_true",
                        help="read a checkout at some other commit, accepting that the merge "
                             "model may no longer describe it")
    args = parser.parse_args()

    winui = args.winui.resolve()
    if not (winui / "dev/dll/Microsoft.UI.Xaml.vcxproj").exists():
        parser.error(f"not a microsoft-ui-xaml checkout: {winui}")
    commit = head_commit(winui)
    if commit != PINNED_COMMIT and not args.allow_unpinned:
        parser.error(f"{winui} is at {commit}, not the pinned {PINNED_COMMIT}; "
                     f"pass --allow-unpinned to read it anyway")

    database = extract(winui, args.flavour, args.os_version)
    if commit != PINNED_COMMIT:
        database["source"]["commit"] = commit
        database["source"]["unpinned"] = True

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(database, indent=1, sort_keys=True) + "\n", encoding="utf-8")

    totals = database["totals"]
    print(f"  merged files     {database['source']['merged_files']:>5}")
    print(f"  keys             {totals['keys']:>5}")
    print(f"  unkeyed skipped  {totals['unkeyed_entries_skipped']:>5}")
    for theme, counts in totals["by_theme"].items():
        detail = "  ".join(f"{status} {count}" for status, count in counts.items())
        print(f"    {theme:<14} {sum(counts.values()):>5}   {detail}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
