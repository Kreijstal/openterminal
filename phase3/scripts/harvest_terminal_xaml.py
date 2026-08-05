#!/usr/bin/env python3
"""Harvest Windows Terminal's XAML into the behaviour database.

Two things come out of one parse pass over the checkout.

**An inventory** — every element type, property, attached property, markup
extension and resource key Terminal's markup actually uses, with counts and the
files that use them. This is the demand side of the reimplementation: it says
what has to exist, and how often each thing is reached for, so the work can be
ordered by what the application really depends on rather than by what the
framework happens to document.

**Candidate cases** — the subtrees that a bare ``XamlReader.Load`` can realise
on its own. Most of Terminal's markup cannot be: it is rooted at an ``x:Class``,
it binds with ``{x:Bind}``, it pulls brushes from ``{ThemeResource}``, or it
hangs an event handler that only the code-behind can supply. Those are real
dependencies, not defects, and each one is recorded as a *blocker* so the
inventory answers "what would we have to implement to make more of this
measurable" instead of silently dropping it.

What survives with no blockers is emitted as a level 7 case in the same schema
``generate_cases.py`` uses, so the harvested cases and the generated ones are
measured by one harness and compared the same way.

A blocker-free prediction is still only a prediction. The runtime on Windows is
the arbiter: a candidate it refuses to load comes back with an ``error``, and
the measurement workflow reports those rather than letting them pass as
coverage.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import xml.etree.ElementTree as ET
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any, Iterable, Iterator
from xml.sax.saxutils import escape, quoteattr

SCHEMA_VERSION = 1

PRESENTATION = "http://schemas.microsoft.com/winfx/2006/xaml/presentation"
XAML_X = "http://schemas.microsoft.com/winfx/2006/xaml"

# Design-time and markup-compatibility attributes. A conforming parser drops
# these because Terminal marks them ignorable, so the harvester drops them too
# rather than counting them against a subtree.
IGNORABLE_NS = {
    "http://schemas.microsoft.com/expression/blend/2008",
    "http://schemas.openxmlformats.org/markup-compatibility/2006",
}

# x: directives a standalone load can honour. x:Name is kept because it names
# elements without requiring anything outside the markup. Everything else --
# x:Class, x:Uid, x:Bind, x:Key, x:Load, x:DataType -- makes the element depend
# on a code-behind, a resource file or a data context.
ALLOWED_X_DIRECTIVES = {"Name"}

# Elements that are a resource lookup rather than a type. They resolve against a
# dictionary the load has to be able to reach.
RESOURCE_ELEMENTS = {"StaticResource", "ThemeResource"}

# Extensions that are the same lookup written as an attribute.
RESOURCE_EXTENSIONS = ("StaticResource", "ThemeResource")

# WPF and WinUI share the presentation namespace URI, so markup alone cannot
# tell them apart -- and mistaking one for the other yields cases that look
# clean and then fail to load, because WPF-only members like ScrollChanged are
# absent from the WinUI metadata that would otherwise have flagged them.
# The owning project does distinguish them: Terminal's WinUI markup is compiled
# by C++ projects, its WPF markup by C# ones.
WINUI_PROJECT_SUFFIXES = {".vcxproj", ".vcxitems"}
PROJECT_SUFFIXES = WINUI_PROJECT_SUFFIXES | {".csproj", ".wapproj"}

# WPF's namespace syntax. WinUI spells the same thing "using:".
WPF_NAMESPACE_PREFIX = "clr-namespace:"

# Sizes each harvested subtree is measured at. Infinity is included because a
# subtree lifted out of a page is no longer under whatever constrained it, and
# unconstrained measure is where real markup tends to disagree.
HARVEST_SIZES: list[list[float]] = [
    [400.0, 300.0],
    [100.0, 50.0],
    [float("inf"), 300.0],
]

BASE_ENV: dict[str, Any] = {
    "dpi_scale": 1.0,
    "theme": "Light",
    "font_family": "Segoe UI",
    "font_size": 14.0,
    "language": "en-US",
}

MARKUP_EXTENSION = re.compile(r"^\{\s*([A-Za-z_][\w.:]*)")


class ThemeResources:
    """The keys the application dictionary can actually supply.

    A resource reference blocks a standalone load unless something outside the
    markup answers it. In a running Terminal that something is
    ``Application.Resources``; here it is the database
    ``extract_winui_theme_resources.py`` builds out of the pinned WinUI 2.8.4
    source. Absent, every reference blocks, which is where this harvester
    started and is still the default.

    Only keys with a *representable* value count. That is a narrower set than
    "keys WinUI defines", deliberately:

    - a ``Thickness``, a ``CornerRadius``, an ``x:Double`` and the rest carry a
      literal, and a literal is what an inlined attribute would have carried;
    - a ``SolidColorBrush`` counts only once the extractor has followed its
      colour to a literal, because the attribute form of a brush *is* a colour;
    - a ``Style``, a ``ControlTemplate``, a converter, or a brush built on an
      OS colour we cannot see does **not** count. The key exists -- the
      inventory says so -- and it still cannot be turned into markup that loads.

    An unblocked reference remains a prediction, and a more optimistic one than
    the rest of this file makes: it predicts that the *oracle* can resolve the
    key too. The probe hosts XAML through ``WindowsXamlManager``, which supplies
    the OS's own theme resources and not WinUI 2's -- so a case unblocked by a
    key only WinUI 2 defines will come back with an error until the probe merges
    ``XamlControlsResources``. That is the corpus asking a question, and
    ``report_measurements.py`` already treats a rejected harvested case as work
    to do rather than as coverage.
    """

    def __init__(self, payload: dict[str, Any], theme: str) -> None:
        themes = payload["themes"]
        if theme not in themes:
            raise SystemExit(f"the theme database has no theme {theme!r}; it has "
                             f"{', '.join(sorted(k for k in themes if k != 'Shared'))}")
        self.theme = theme
        self.source = payload["source"]
        self.defined: set[str] = set(themes["Shared"]) | set(themes[theme])
        self.resolvable: set[str] = set()
        for entries in (themes["Shared"], themes[theme]):
            for key, entry in entries.items():
                landed = entry.get("resolved", entry) if entry["status"] == "alias" else entry
                status = landed.get("status")
                if status == "value" or (status == "brush" and "color" in landed):
                    self.resolvable.add(key)

    @classmethod
    def load(cls, path: Path, theme: str) -> "ThemeResources":
        return cls(json.loads(path.read_text(encoding="utf-8")), theme)


# The empty one, so that every call site can pass a database and the default is
# "nothing outside the markup answers a lookup" rather than a None check.
class NoThemeResources:
    theme = None
    source: dict[str, Any] = {}
    defined: set[str] = set()
    resolvable: set[str] = set()


def referenced_key(value: str) -> str | None:
    """The key a ``{StaticResource X}`` or ``{ThemeResource X}`` names."""
    text = value.strip()
    name = markup_extension_name(text)
    if name not in RESOURCE_EXTENSIONS:
        return None
    key = text[text.index(name) + len(name):].strip().rstrip("}").strip()
    if key.startswith("ResourceKey"):
        _, _, key = key.partition("=")
        key = key.strip()
    # A key with a comma in it is a multi-argument extension this harvester
    # does not model; treat it as naming nothing, which leaves it blocking.
    return key if key and "," not in key else None


def encode_size(size: Iterable[float]) -> list[Any]:
    """JSON has no Infinity; the harness maps the string back."""
    return ["Infinity" if v == float("inf") else v for v in size]


def split_tag(tag: str) -> tuple[str, str]:
    """ElementTree Clark notation ``{uri}local`` -> ``(uri, local)``."""
    if tag.startswith("{"):
        uri, _, local = tag[1:].partition("}")
        return uri, local
    return "", tag


def markup_extension_name(value: str) -> str | None:
    """Name of the markup extension in ``value``, if it is one.

    ``{}`` at the start of an attribute is the escape for a literal brace, so
    it is deliberately not an extension.
    """
    text = value.strip()
    if not text.startswith("{") or text.startswith("{}"):
        return None
    match = MARKUP_EXTENSION.match(text)
    return match.group(1) if match else "?"


class Metadata:
    """Framework type facts, from the SDK winmd harvest.

    Without it every classification below would be a guess, so its absence is
    an error rather than a silent downgrade to heuristics.
    """

    def __init__(self, payload: dict[str, Any]) -> None:
        self.classes: set[str] = set(payload["class_local_names"])
        self.ui_elements: set[str] = set(payload["ui_element_local_names"])
        self.events: set[str] = set(payload["event_only_names"])
        self.properties: set[str] = set(payload["property_names"]) | set(
            payload["attached_property_names"]
        )

    @classmethod
    def load(cls, path: Path) -> "Metadata":
        return cls(json.loads(path.read_text(encoding="utf-8")))


class Node:
    """One markup element, with its namespace split out and noise removed."""

    def __init__(self, element: ET.Element, path: str) -> None:
        self.uri, self.local = split_tag(element.tag)
        self.path = path
        self.text = (element.text or "").strip()
        self.attributes: list[tuple[str, str, str]] = []  # (uri, local, value)
        self.ignored = 0
        for name, value in element.attrib.items():
            uri, local = split_tag(name)
            if uri in IGNORABLE_NS:
                self.ignored += 1
                continue
            self.attributes.append((uri, local, value))
        self.children: list[Node] = []

    @property
    def is_property_element(self) -> bool:
        """``<Grid.ColumnDefinitions>`` sets a property; it is not a child."""
        return "." in self.local

    @property
    def owner(self) -> str:
        return self.local.split(".", 1)[0]

    def walk(self) -> Iterator["Node"]:
        yield self
        for child in self.children:
            yield from child.walk()


def build(element: ET.Element, path: str) -> Node:
    node = Node(element, path)
    counts: Counter[str] = Counter()
    for child in element:
        if not isinstance(child.tag, str):
            continue  # comments and processing instructions
        _, local = split_tag(child.tag)
        index = counts[local]
        counts[local] += 1
        node.children.append(build(child, f"{path}/{local}[{index}]"))
    return node


# --- blockers -----------------------------------------------------------------
def element_blockers(node: Node, meta: Metadata,
                     resources: Any = NoThemeResources) -> list[dict[str, str]]:
    """Why this single element cannot be loaded on its own. Empty means it can."""
    found: list[dict[str, str]] = []

    def add(kind: str, detail: str) -> None:
        found.append({"kind": kind, "detail": detail, "path": node.path})

    if node.uri == XAML_X:
        add("x-element", f"x:{node.local}")
        return found
    if node.uri != PRESENTATION:
        # Judging this element's attributes against framework metadata would
        # report every property of the foreign type as unknown, which says
        # nothing beyond what the foreign type itself already says.
        add("foreign-type", f"{node.uri}#{node.local}")
        return found

    if node.local in RESOURCE_ELEMENTS:
        # Its attributes name the key being looked up, not properties of a type.
        keys = [value for uri, local, value in node.attributes
                if not uri and local == "ResourceKey"]
        # An x:Key alongside makes this a dictionary entry rather than a value,
        # and the entry is only reachable through the dictionary -- which the
        # lifted subtree does not carry. It keeps the x:Key blocker below.
        aliasing = any(uri == XAML_X for uri, _, _ in node.attributes)
        if len(keys) == 1 and not aliasing and keys[0] in resources.resolvable:
            return found
        add("resource-element", node.local)
        return found
    if node.is_property_element:
        if node.owner not in meta.classes:
            add("unknown-property-element", node.local)
    elif node.local not in meta.classes:
        add("unknown-type", node.local)

    for uri, local, value in node.attributes:
        if uri == XAML_X:
            if local not in ALLOWED_X_DIRECTIVES:
                add("x-directive", f"x:{local}")
            continue
        if uri:
            add("foreign-attribute", f"{uri}#{local}")
            continue

        extension = markup_extension_name(value)
        if extension:
            key = referenced_key(value)
            # The one extension a dictionary can answer. Everything else --
            # {x:Bind}, {Binding}, {TemplateBinding} -- needs a data context or
            # a template and is unaffected by any amount of resource content.
            #
            # A resolvable key does not exempt the attribute from the checks
            # below: the value being available says nothing about whether the
            # property it is being set on exists. Falling through is the whole
            # point -- an extension used to hide that question.
            if key is None or key not in resources.resolvable:
                add("markup-extension", extension)
                continue

        if "." in local:
            owner, _, member = local.partition(".")
            if owner not in meta.classes:
                add("unknown-attached-property", local)
            elif member in meta.events:
                add("event-attribute", local)
            elif member not in meta.properties:
                add("unknown-attribute", local)
            continue

        if local in meta.events:
            add("event-attribute", local)
        elif local not in meta.properties:
            # Not a property of any framework type. Either it belongs to a
            # framework we are not measuring, or it is an event the metadata
            # does not carry -- both make the element unloadable here.
            add("unknown-attribute", local)

    return found


def subtree_blockers(node: Node, meta: Metadata,
                     resources: Any = NoThemeResources) -> list[dict[str, str]]:
    return [b for element in node.walk() for b in element_blockers(element, meta, resources)]


# --- serialisation ------------------------------------------------------------
def serialise(node: Node, root: bool = False, uses_x: bool = False) -> str:
    """Render a node back to markup.

    Re-emitting from the tree rather than slicing the source text normalises
    formatting, which is what makes identical markup in different files collapse
    to one case instead of many.
    """
    parts = [f"<{node.local}"]
    if root:
        parts.append(f' xmlns="{PRESENTATION}"')
        if uses_x:
            parts.append(f' xmlns:x="{XAML_X}"')
    for uri, local, value in node.attributes:
        name = f"x:{local}" if uri == XAML_X else local
        parts.append(f" {name}={quoteattr(value)}")

    body = escape(node.text) if node.text else ""
    for child in node.children:
        body += serialise(child)
    if not body:
        parts.append("/>")
        return "".join(parts)
    parts.append(">")
    parts.append(body)
    parts.append(f"</{node.local}>")
    return "".join(parts)


def uses_x_directives(node: Node) -> bool:
    return any(uri == XAML_X for element in node.walk() for uri, _, _ in element.attributes)


def subtree_resource_keys(node: Node) -> set[str]:
    """Every key this subtree looks up, in either spelling."""
    keys: set[str] = set()
    for element in node.walk():
        for uri, local, value in element.attributes:
            if uri:
                continue
            if element.local in RESOURCE_ELEMENTS and local == "ResourceKey":
                keys.add(value.strip())
                continue
            key = referenced_key(value)
            if key:
                keys.add(key)
    return keys


# --- candidate extraction -----------------------------------------------------
def candidates(node: Node, meta: Metadata, resources: Any = NoThemeResources) -> Iterator[Node]:
    """Maximal subtrees that can be loaded standalone.

    Top-down and non-descending on success: the largest loadable subtree is the
    interesting one, and every subtree inside it would be redundant with it.
    """
    loadable = (
        node.uri == PRESENTATION
        and not node.is_property_element
        and node.local in meta.ui_elements
        and not subtree_blockers(node, meta, resources)
    )
    if loadable:
        yield node
        return
    for child in node.children:
        yield from candidates(child, meta, resources)


# --- inventory ----------------------------------------------------------------
def new_inventory() -> dict[str, Any]:
    return {
        "element_types": defaultdict(Counter),
        "properties_by_type": defaultdict(Counter),
        "attached_properties": defaultdict(Counter),
        "markup_extensions": defaultdict(Counter),
        "events_by_type": defaultdict(Counter),
        "x_directives": defaultdict(Counter),
        "resource_keys_defined": defaultdict(Counter),
        "resource_keys_referenced": defaultdict(Counter),
        "unknown_types": defaultdict(Counter),
    }


def record(node: Node, meta: Metadata, source: str, inv: dict[str, Any]) -> None:
    if node.is_property_element:
        key = node.local if node.uri == PRESENTATION else f"{node.uri}#{node.local}"
        inv["element_types"][key][source] += 1
    else:
        key = node.local if node.uri == PRESENTATION else f"{node.uri}#{node.local}"
        inv["element_types"][key][source] += 1
        if (
            node.uri == PRESENTATION
            and node.local not in meta.classes
            and node.local not in RESOURCE_ELEMENTS
        ):
            inv["unknown_types"][node.local][source] += 1

    for uri, local, value in node.attributes:
        if uri == XAML_X:
            inv["x_directives"][f"x:{local}"][source] += 1
            if local == "Key":
                inv["resource_keys_defined"][value][source] += 1
            continue
        if uri:
            continue

        extension = markup_extension_name(value)
        if extension:
            inv["markup_extensions"][extension][source] += 1
            if extension in ("StaticResource", "ThemeResource"):
                referenced = value.strip().lstrip("{").strip()
                referenced = referenced[len(extension):].strip().rstrip("}").strip()
                if referenced:
                    inv["resource_keys_referenced"][referenced][source] += 1
            continue

        if "." in local:
            inv["attached_properties"][local][source] += 1
        elif local in meta.events:
            inv["events_by_type"][f"{node.local}.{local}"][source] += 1
        else:
            inv["properties_by_type"][node.local][local] += 1

    # ResourceKey="X" on a <StaticResource> element is a reference too.
    if node.local == "StaticResource":
        for uri, local, value in node.attributes:
            if not uri and local == "ResourceKey":
                inv["resource_keys_referenced"][value][source] += 1


def flatten(counters: dict[str, Counter], by: str) -> dict[str, Any]:
    return {
        name: {"count": sum(counter.values()), by: sorted(counter)}
        for name, counter in sorted(counters.items())
    }


def owning_project(path: Path, repo: Path) -> Path | None:
    """Nearest project file at or above ``path``."""
    for directory in [path.parent, *path.parent.parents]:
        projects = sorted(
            child for child in directory.iterdir()
            if child.suffix in PROJECT_SUFFIXES
        )
        if projects:
            return projects[0]
        if directory == repo:
            break
    return None


def declared_namespaces(path: Path) -> set[str]:
    """Namespace URIs the document declares, prefixed or not."""
    return {uri for _, (_, uri) in ET.iterparse(path, events=("start-ns",))}


def out_of_scope(path: Path, repo: Path) -> str | None:
    """Why this file is not WinUI markup, or None if it is."""
    project = owning_project(path, repo)
    if project is None:
        return "no owning project"
    if project.suffix not in WINUI_PROJECT_SUFFIXES:
        return f"built by {project.name}"
    try:
        namespaces = declared_namespaces(path)
    except ET.ParseError:
        return None  # the parse failure is reported on its own
    if any(uri.startswith(WPF_NAMESPACE_PREFIX) for uri in namespaces):
        return "declares a clr-namespace"
    return None


def git(repo: Path, *args: str) -> str:
    return subprocess.run(
        ["git", "-C", str(repo), *args],
        check=True, capture_output=True, text=True,
    ).stdout.strip()


def canonical_remote(url: str) -> str:
    """One spelling of a remote, whoever cloned it.

    The same repository is called ``git@github.com:microsoft/terminal.git`` by
    one clone and ``https://github.com/microsoft/terminal`` by another --
    actions/checkout drops the suffix, a manual clone keeps it. Recording the
    raw string would put the machine that ran the harvest into the output and
    make the result unreproducible anywhere else.
    """
    text = url.strip().rstrip("/")
    if text.startswith("git@") and ":" in text:
        host, _, path = text[len("git@"):].partition(":")
        text = f"https://{host}/{path}"
    elif text.startswith("ssh://git@"):
        text = "https://" + text[len("ssh://git@"):]
    if text.endswith(".git"):
        text = text[: -len(".git")]
    return text


def theme_resource_report(resources: Any, referenced: dict[str, Counter],
                          defined: dict[str, Counter]) -> dict[str, Any]:
    """How far the supplied dictionary goes against what Terminal asks for.

    The split that matters is not "how many keys does WinUI have" -- it has
    thousands nobody names -- but how the keys *Terminal* names divide between
    the three places they can come from: Terminal's own markup, the WinUI 2
    source we can read, and the OS dictionary we cannot.
    """
    # A key Terminal defines nowhere in its own markup has to come from
    # Application.Resources or from nothing.
    outside = sorted(key for key in referenced if key not in defined)
    resolvable = sorted(key for key in outside if key in resources.resolvable)
    unusable = sorted(key for key in outside
                      if key in resources.defined and key not in resources.resolvable)
    absent = sorted(key for key in outside if key not in resources.defined)
    return {
        "source": resources.source,
        "theme": resources.theme,
        "keys_available": len(resources.defined),
        "keys_representable": len(resources.resolvable),
        "referenced_outside_terminal": {
            "keys": len(outside),
            "references": sum(sum(referenced[key].values()) for key in outside),
            # Split three ways rather than two, because "WinUI defines it" and
            # "we can use it" are different facts and conflating them would
            # overstate what the dictionary unlocks.
            "representable_here": resolvable,
            "defined_but_not_representable": unusable,
            "not_in_this_source": absent,
        },
    }


# --- driver -------------------------------------------------------------------
def harvest(repo: Path, meta: Metadata,
            resources: Any = NoThemeResources) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    files = sorted(
        p for p in repo.rglob("*.xaml") if ".git" not in p.parts
    )

    inv = new_inventory()
    blocker_counts: defaultdict[str, Counter] = defaultdict(Counter)
    per_file: list[dict[str, Any]] = []
    parse_failures: list[dict[str, str]] = []
    skipped: list[dict[str, str]] = []
    found: dict[str, dict[str, Any]] = {}

    for path in files:
        source = path.relative_to(repo).as_posix()
        reason = out_of_scope(path, repo)
        if reason:
            skipped.append({"file": source, "reason": reason})
            continue
        try:
            tree = ET.parse(path)
        except ET.ParseError as error:
            parse_failures.append({"file": source, "error": str(error)})
            continue

        _, root_local = split_tag(tree.getroot().tag)
        root = build(tree.getroot(), f"/{root_local}")

        elements = 0
        for node in root.walk():
            record(node, meta, source, inv)
            elements += 1

        for blocker in subtree_blockers(root, meta, resources):
            blocker_counts[blocker["kind"]][blocker["detail"]] += 1

        extracted = list(candidates(root, meta, resources))
        for node in extracted:
            markup = serialise(node, root=True, uses_x=uses_x_directives(node))
            digest = hashlib.sha256(markup.encode("utf-8")).hexdigest()[:10]
            entry = found.setdefault(digest, {
                "markup": markup,
                "root_type": node.local,
                "elements": sum(1 for _ in node.walk()),
                # Which keys this subtree needs the application dictionary to
                # answer. Empty for a subtree that stands entirely on its own,
                # which is what every candidate was before there was a
                # dictionary -- so the field also says which cases are new and
                # why, without anyone having to diff two harvests.
                "resource_keys": sorted(subtree_resource_keys(node)),
                "occurrences": [],
            })
            entry["occurrences"].append({"file": source, "path": node.path})

        per_file.append({
            "file": source,
            "elements": elements,
            "root": root_local,
            "candidates": len(extracted),
        })

    inventory = {
        "schema_version": SCHEMA_VERSION,
        "source": {
            "repository": canonical_remote(git(repo, "remote", "get-url", "origin")),
            "commit": git(repo, "rev-parse", "HEAD"),
            "commit_date": git(repo, "show", "-s", "--format=%cI", "HEAD"),
        },
        "totals": {
            "files": len(files),
            "files_in_scope": len(files) - len(skipped),
            "files_parsed": len(per_file),
            "elements": sum(f["elements"] for f in per_file),
            "candidates": sum(f["candidates"] for f in per_file),
            "unique_candidates": len(found),
        },
        "files": per_file,
        "skipped_files": skipped,
        "parse_failures": parse_failures,
        "element_types": flatten(inv["element_types"], "files"),
        "properties_by_type": {
            type_name: dict(sorted(counter.items()))
            for type_name, counter in sorted(inv["properties_by_type"].items())
        },
        "attached_properties": flatten(inv["attached_properties"], "files"),
        "events_by_type": flatten(inv["events_by_type"], "files"),
        "markup_extensions": flatten(inv["markup_extensions"], "files"),
        "x_directives": flatten(inv["x_directives"], "files"),
        "resource_keys_defined": flatten(inv["resource_keys_defined"], "files"),
        "resource_keys_referenced": flatten(inv["resource_keys_referenced"], "files"),
        "unknown_types": flatten(inv["unknown_types"], "files"),
        # What the application dictionary was and what it bought. Written
        # whether or not one was supplied, so that "no dictionary" is a recorded
        # state rather than a missing field, and so the unlock is a number in
        # the output instead of something a reader has to infer from a diff.
        "theme_resources": theme_resource_report(resources, inv["resource_keys_referenced"],
                                                 inv["resource_keys_defined"]),
        # Ordered by how much markup each one costs us: the top entry is the
        # single capability that would unlock the most measurable subtrees.
        "blockers": {
            kind: {
                "count": sum(details.values()),
                "details": dict(details.most_common()),
            }
            for kind, details in sorted(
                blocker_counts.items(), key=lambda kv: -sum(kv[1].values())
            )
        },
    }

    cases: list[dict[str, Any]] = []
    for digest in sorted(found):
        entry = found[digest]
        for index, size in enumerate(HARVEST_SIZES):
            first = entry["occurrences"][0]
            harvest_note: dict[str, Any] = {
                "repository": inventory["source"]["repository"],
                "commit": inventory["source"]["commit"],
                "markup_sha256_prefix": digest,
                "occurrences": entry["occurrences"],
            }
            note = (f"{entry['root_type']} subtree, {entry['elements']} element(s), "
                    f"from {first['file']}")
            if entry["resource_keys"]:
                # Said in the note as well as in the metadata, because this is
                # the one thing about a level 7 case that a reader looking at a
                # failure needs first: it did not load standalone before, and
                # whether it loads now is a question about the *host*, not about
                # the markup.
                harvest_note["resource_keys"] = entry["resource_keys"]
                harvest_note["needs_application_resources"] = resources.source
                note += (f"; needs {len(entry['resource_keys'])} key(s) from "
                         f"Application.Resources")
            cases.append({
                "schema_version": SCHEMA_VERSION,
                "id": f"L7-terminal-{digest}-s{index}",
                "level": 7,
                "group": "terminal",
                "note": note,
                "requires": ["L3-grid", "L3-stack", "L4-text"],
                "markup": entry["markup"],
                "environment": dict(BASE_ENV, available_size=encode_size(size)),
                "harvest": harvest_note,
            })
    return inventory, cases


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("repository", type=Path, help="Windows Terminal checkout")
    parser.add_argument("--members", type=Path, required=True,
                        help="xaml-members.json from harvest_xaml_members.py")
    parser.add_argument("--inventory", type=Path,
                        help="where to write the vocabulary inventory")
    parser.add_argument("--cases", type=Path,
                        help="directory to write level 7 case files into")
    parser.add_argument("--theme-resources", type=Path,
                        help="the database extract_winui_theme_resources.py writes. Without it "
                             "every resource reference blocks, which is what this harvester did "
                             "before there was one")
    args = parser.parse_args()

    repo = args.repository.resolve()
    if not (repo / ".git").exists():
        parser.error(f"not a Git checkout: {repo}")

    meta = Metadata.load(args.members)
    resources: Any = NoThemeResources
    baseline: dict[str, Any] | None = None
    if args.theme_resources:
        resources = ThemeResources.load(args.theme_resources, BASE_ENV["theme"])
        # The same harvest without the dictionary, so the delta is measured
        # rather than remembered. It costs one more pass over 43 files and it
        # is the number this whole exercise is judged on -- leaving it to be
        # recovered by diffing two runs is how it stops being reported at all.
        baseline, _ = harvest(repo, meta)

    inventory, cases = harvest(repo, meta, resources)
    if baseline is not None:
        inventory["theme_resources"]["unlock"] = {
            "unique_candidates_without": baseline["totals"]["unique_candidates"],
            "unique_candidates_with": inventory["totals"]["unique_candidates"],
        }

    if args.inventory:
        args.inventory.parent.mkdir(parents=True, exist_ok=True)
        args.inventory.write_text(
            json.dumps(inventory, indent=1, sort_keys=True) + "\n", encoding="utf-8"
        )
    if args.cases:
        args.cases.mkdir(parents=True, exist_ok=True)
        for case in cases:
            (args.cases / f"{case['id']}.json").write_text(
                json.dumps(case, indent=1, sort_keys=True, allow_nan=False) + "\n",
                encoding="utf-8",
            )

    totals = inventory["totals"]
    print(f"  files            {totals['files_parsed']:>5}/{totals['files']}"
          f" ({totals['files'] - totals['files_in_scope']} out of scope)")
    print(f"  elements         {totals['elements']:>5}")
    print(f"  candidates       {totals['candidates']:>5}")
    print(f"  unique subtrees  {totals['unique_candidates']:>5}")
    print(f"  cases written    {len(cases):>5}")
    report = inventory["theme_resources"]
    outside = report["referenced_outside_terminal"]
    if report["theme"] is None:
        print("  application dictionary: none, so every resource reference blocks")
    else:
        print(f"  application dictionary: {report['keys_representable']}"
              f"/{report['keys_available']} keys representable"
              f" ({report['source'].get('controls_resources_version', '?')},"
              f" theme {report['theme']})")
        print(f"    of {outside['keys']} key(s) Terminal names and never defines:"
              f" {len(outside['representable_here'])} usable,"
              f" {len(outside['defined_but_not_representable'])} defined but not usable,"
              f" {len(outside['not_in_this_source'])} absent")
        unlock = report["unlock"]
        print(f"    unique subtrees {unlock['unique_candidates_without']}"
              f" -> {unlock['unique_candidates_with']}")
    print("  top blockers:")
    for kind, entry in list(inventory["blockers"].items())[:6]:
        print(f"    {kind:<28} {entry['count']:>5}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
