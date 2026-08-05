#!/usr/bin/env python3
"""Generate the mechanical levels of the XAML behaviour database.

Levels 0 to 4 are a cross product, not a collection of hand-written examples:
each case varies one axis of the layout contract against a fixed environment,
so a mismatch names the primitive that is wrong.

Level 5 is authored rather than crossed -- resource lookup has rules, not axes
-- but it is emitted from here all the same, because the corpus is checked in
CI against exactly what this script produces and a hand-written case file would
be deleted by that check.

Cases are markup plus an environment. The measurement harness loads the markup
with XamlReader.Load, so a case costs one text file and no build step.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any, Iterator

SCHEMA_VERSION = 1

XMLNS = "http://schemas.microsoft.com/winfx/2006/xaml/presentation"
XMLNS_X = "http://schemas.microsoft.com/winfx/2006/xaml"

# Available sizes every sizing case is swept over. Infinity is not an edge case
# to add later: it is what a StackPanel passes down its stacking axis and what a
# ScrollViewer passes to its content, so it is where measure bugs concentrate.
AVAILABLE_SIZES: list[list[float]] = [
    [0.0, 0.0],
    [100.0, 50.0],
    [400.0, 300.0],
    [float("inf"), 300.0],
    [400.0, float("inf")],
    [float("inf"), float("inf")],
]

# Pinned so that nothing in the environment can drift between runs.
BASE_ENV: dict[str, Any] = {
    "dpi_scale": 1.0,
    "theme": "Light",
    "font_family": "Segoe UI",
    "font_size": 14.0,
    "language": "en-US",
}

H_ALIGN = ["Left", "Center", "Right", "Stretch"]
V_ALIGN = ["Top", "Center", "Bottom", "Stretch"]
MARGINS = ["0", "8", "4,8,12,16"]
GRID_LENGTHS = ["Auto", "*", "2*", "40"]


def encode_size(size: list[float]) -> list[Any]:
    """JSON has no Infinity, so encode it as a string the harness maps back.

    Writing a bare `Infinity` token would produce a file that only Python's
    json module accepts, which would defeat the point of a portable corpus.
    """
    return ["Infinity" if v == float("inf") else v for v in size]


def case(
    case_id: str,
    level: int,
    group: str,
    markup: str,
    available: list[float],
    note: str,
    requires: list[str] | None = None,
    twin: str | None = None,
    question: str | None = None,
) -> dict[str, Any]:
    spec: dict[str, Any] = {
        "schema_version": SCHEMA_VERSION,
        "id": case_id,
        "level": level,
        "group": group,
        "note": note,
        "requires": requires or [],
        "markup": markup,
        "environment": dict(BASE_ENV, available_size=encode_size(available)),
    }
    if twin:
        # The same layout written without the feature under test. Two cases
        # linked this way have to measure identically, and that can be checked
        # against our own output before the oracle has ever seen either of them.
        spec["twin"] = twin
    if question:
        # We do not know what the runtime does here, and the point of the case
        # is to find out. Recording the question means a rejection is an answer
        # rather than a broken corpus -- see report_measurements.py.
        spec["oracle_decides"] = True
        spec["question"] = question
    return spec


def root(inner: str) -> str:
    return f'<Border xmlns="{XMLNS}">{inner}</Border>'


# --- L0: property system, no layout ------------------------------------------
# A dependency property's effective value comes from a precedence chain. These
# cases read values back without ever running layout, so a failure here cannot
# be blamed on measure or arrange.
def level0() -> Iterator[dict[str, Any]]:
    probes = [
        ("default", '<Border xmlns="%s"/>' % XMLNS, "Width", "unset stays NaN"),
        ("local", '<Border xmlns="%s" Width="120"/>' % XMLNS, "Width", "local value wins over default"),
        ("local-opacity", '<Border xmlns="%s" Opacity="0.5"/>' % XMLNS, "Opacity", "local double"),
        # FontSize has to be set on a Control. This was written on a StackPanel
        # first, which is a WPF habit -- there TextElement.FontSize is an
        # inherited attached property that any FrameworkElement takes. WinUI has
        # no such attached property: FontSize is declared on Control and on
        # TextBlock and nowhere else, so the runtime rejected the markup with
        # "The property 'FontSize' was not found in type 'StackPanel'".
        ("inherited-fontsize",
         '<ContentControl xmlns="%s" FontSize="22"><TextBlock x:Name="t" '
         'xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"/></ContentControl>' % XMLNS,
         "FontSize", "FontSize inherits from a Control down to its content"),
    ]
    for name, markup, prop, note in probes:
        yield case(f"L0-props-{name}", 0, "props", markup, [400.0, 300.0],
                   f"{note}; read {prop} without running layout")


# --- L1: a single element, no children ---------------------------------------
def level1() -> Iterator[dict[str, Any]]:
    n = 0
    for width in ["Auto", "60"]:
        for height in ["Auto", "30"]:
            for margin in MARGINS:
                for avail in AVAILABLE_SIZES:
                    n += 1
                    attrs = f'Margin="{margin}"'
                    if width != "Auto":
                        attrs += f' Width="{width}"'
                    if height != "Auto":
                        attrs += f' Height="{height}"'
                    markup = f'<Border xmlns="{XMLNS}" {attrs}/>'
                    yield case(
                        f"L1-sizing-{n:04d}", 1, "sizing", markup, avail,
                        f"lone Border w={width} h={height} margin={margin}",
                        requires=["L0-props"],
                    )


# --- L2: one parent, one child ------------------------------------------------
def level2() -> Iterator[dict[str, Any]]:
    n = 0
    for ha in H_ALIGN:
        for va in V_ALIGN:
            for margin in ["0", "8"]:
                for child_size in [("Auto", "Auto"), ("40", "20")]:
                    for avail in [[400.0, 300.0], [100.0, 50.0], [float("inf"), 300.0]]:
                        n += 1
                        cw, ch = child_size
                        a = f'HorizontalAlignment="{ha}" VerticalAlignment="{va}" Margin="{margin}"'
                        if cw != "Auto":
                            a += f' Width="{cw}"'
                        if ch != "Auto":
                            a += f' Height="{ch}"'
                        markup = root(f"<Border {a}/>")
                        yield case(
                            f"L2-align-{n:04d}", 2, "align", markup, avail,
                            f"child {ha}/{va} margin={margin} size={cw}x{ch}",
                            requires=["L1-sizing"],
                        )


# --- L3: panels ---------------------------------------------------------------
def level3() -> Iterator[dict[str, Any]]:
    n = 0
    # StackPanel: the stacking axis receives Infinity from the panel, so a child
    # that measures differently under Infinity shows up here and nowhere else.
    for orientation in ["Vertical", "Horizontal"]:
        for count in [0, 1, 3]:
            for child_fixed in [True, False]:
                for avail in [[400.0, 300.0], [100.0, 50.0], [float("inf"), float("inf")]]:
                    n += 1
                    dim = 'Width="30" Height="20"' if child_fixed else 'Margin="4"'
                    kids = "".join(f"<Border {dim}/>" for _ in range(count))
                    markup = f'<StackPanel xmlns="{XMLNS}" Orientation="{orientation}">{kids}</StackPanel>'
                    yield case(
                        f"L3-stack-{n:04d}", 3, "stack", markup, avail,
                        f"StackPanel {orientation} children={count} fixed={child_fixed}",
                        requires=["L2-align"],
                    )
    m = 0
    # Grid: Auto/Star/Pixel is the whole star-distribution algorithm, and spans
    # are where an implementation that only handles single cells breaks.
    for c1 in GRID_LENGTHS:
        for c2 in GRID_LENGTHS:
            for span in [False, True]:
                for avail in [[400.0, 300.0], [100.0, 50.0], [float("inf"), 300.0]]:
                    m += 1
                    defs = (
                        "<Grid.ColumnDefinitions>"
                        f'<ColumnDefinition Width="{c1}"/><ColumnDefinition Width="{c2}"/>'
                        "</Grid.ColumnDefinitions>"
                    )
                    if span:
                        kids = '<Border Grid.Column="0" Grid.ColumnSpan="2" Height="20"/>'
                    else:
                        kids = ('<Border Grid.Column="0" Width="30" Height="20"/>'
                                '<Border Grid.Column="1" Width="50" Height="10"/>')
                    markup = f'<Grid xmlns="{XMLNS}">{defs}{kids}</Grid>'
                    yield case(
                        f"L3-grid-{m:04d}", 3, "grid", markup, avail,
                        f"Grid cols=[{c1},{c2}] span={span}",
                        requires=["L2-align"],
                    )


# --- L4: text -----------------------------------------------------------------
# Isolated deliberately: these are the only generated cases whose expected
# values depend on DirectWrite, so a font update moves these and nothing else.
def level4() -> Iterator[dict[str, Any]]:
    n = 0
    samples = ["", "M", "Terminal", "The quick brown fox jumps over the lazy dog"]
    for text in samples:
        for size in [12.0, 14.0, 24.0]:
            for wrap in ["NoWrap", "Wrap"]:
                for avail in [[400.0, 300.0], [60.0, 300.0], [float("inf"), 300.0]]:
                    n += 1
                    markup = (f'<TextBlock xmlns="{XMLNS}" FontFamily="Segoe UI" '
                              f'FontSize="{size}" TextWrapping="{wrap}">{text}</TextBlock>')
                    yield case(
                        f"L4-text-{n:04d}", 4, "text", markup, avail,
                        f"TextBlock size={size} wrap={wrap} len={len(text)}",
                        requires=["L1-sizing"],
                    )


# --- L5: resources ------------------------------------------------------------
# Authored, not crossed. Resource lookup has rules rather than axes: a
# {StaticResource} resolves to the same literal whatever the available size is,
# so sweeping sizes would buy nothing but oracle time.
#
# What the generator does mechanise is the *twinning*. Every scenario we are
# confident about is emitted twice -- once with the value behind a resource,
# once with the same literal written inline -- and the pair has to measure
# identically. That is a check that runs on a laptop today, against our own
# output, months before the oracle has seen either half. It is not the oracle
# and is never reported as though it were; what it does catch is a resolution
# that lands on the wrong value, which is the entire failure mode of a lookup.
#
# Scenarios we are *not* confident about are emitted once, with no twin and
# with the question written down. Guessing an inline twin for those would be
# manufacturing an expectation instead of asking for one.

def l5_document(root_tag: str, body: str, attributes: str = "") -> str:
    head = f'<{root_tag} xmlns="{XMLNS}" xmlns:x="{XMLNS_X}"'
    if attributes:
        head += " " + attributes
    return f"{head}>{body}</{root_tag}>"


def l5_resources(owner: str, entries: str, explicit: bool = False) -> str:
    """A <X.Resources> section, with or without the optional wrapper."""
    inner = f"<ResourceDictionary>{entries}</ResourceDictionary>" if explicit else entries
    return f"<{owner}.Resources>{inner}</{owner}.Resources>"


# (slug, note, requires, resource-form markup, inline twin or None, question)
def l5_scenarios() -> list[tuple[str, str, list[str], str, str | None, str | None]]:
    double = '<x:Double x:Key="BoxWidth">60</x:Double>'
    child = '<Border Width="{StaticResource BoxWidth}" Height="30"/>'
    child_inline = '<Border Width="60" Height="30"/>'
    text_attrs = 'FontFamily="Segoe UI" FontSize="14"'

    return [
        (
            "parent-lookup",
            "a child reads a key its parent declares",
            ["L1-sizing"],
            l5_document("Border", l5_resources("Border", double) + child),
            l5_document("Border", child_inline),
            None,
        ),
        (
            "explicit-dictionary",
            "the same, written with the optional <ResourceDictionary> wrapper",
            ["L1-sizing"],
            l5_document("Border", l5_resources("Border", double, explicit=True) + child),
            l5_document("Border", child_inline),
            None,
        ),
        (
            "grandparent-lookup",
            "the lookup walks past a panel that has no dictionary",
            ["L3-stack"],
            l5_document("Grid", l5_resources("Grid", double) + f"<StackPanel>{child}</StackPanel>"),
            l5_document("Grid", f"<StackPanel>{child_inline}</StackPanel>"),
            None,
        ),
        (
            "deep-chain",
            "an empty dictionary in the chain is walked through, not stopped at",
            ["L3-stack"],
            l5_document("Grid", l5_resources("Grid", double) + "<StackPanel>"
                        + '<StackPanel.Resources><ResourceDictionary/></StackPanel.Resources>'
                        + f"<Border>{child}</Border></StackPanel>"),
            l5_document("Grid", f"<StackPanel><Border>{child_inline}</Border></StackPanel>"),
            None,
        ),
        (
            "root-dictionary",
            "one dictionary on the root feeds descendants at two depths",
            ["L3-stack"],
            l5_document("Grid", l5_resources("Grid",
                        '<x:Double x:Key="BoxWidth">60</x:Double>'
                        '<x:Double x:Key="BoxHeight">24</x:Double>'
                        '<Thickness x:Key="BoxMargin">4,8,12,16</Thickness>')
                        + "<StackPanel>"
                        + '<Border Width="{StaticResource BoxWidth}"'
                          ' Height="{StaticResource BoxHeight}"'
                          ' Margin="{StaticResource BoxMargin}"/>'
                        + '<Border Width="{StaticResource BoxWidth}"'
                          ' Height="{StaticResource BoxHeight}"/>'
                        + "</StackPanel>"),
            l5_document("Grid", "<StackPanel>"
                        '<Border Width="60" Height="24" Margin="4,8,12,16"/>'
                        '<Border Width="60" Height="24"/>'
                        "</StackPanel>"),
            None,
        ),
        (
            "shadowing",
            "an inner dictionary wins for its subtree and only for its subtree",
            ["L3-stack"],
            l5_document("Grid", l5_resources("Grid", double)
                        + '<StackPanel HorizontalAlignment="Left">'
                        + l5_resources("StackPanel", '<x:Double x:Key="BoxWidth">100</x:Double>')
                        + '<Border Width="{StaticResource BoxWidth}" Height="20"/></StackPanel>'
                        + '<Border Width="{StaticResource BoxWidth}" Height="20"'
                          ' HorizontalAlignment="Right" VerticalAlignment="Bottom"/>'),
            l5_document("Grid",
                        '<StackPanel HorizontalAlignment="Left">'
                        '<Border Width="100" Height="20"/></StackPanel>'
                        '<Border Width="60" Height="20"'
                        ' HorizontalAlignment="Right" VerticalAlignment="Bottom"/>'),
            None,
        ),
        (
            "thickness-margin",
            "a Thickness resource on Margin",
            ["L1-sizing"],
            l5_document("Border", l5_resources("Border",
                        '<Thickness x:Key="BoxMargin">4,8,12,16</Thickness>')
                        + '<Border Width="60" Height="30" Margin="{StaticResource BoxMargin}"/>'),
            l5_document("Border", '<Border Width="60" Height="30" Margin="4,8,12,16"/>'),
            None,
        ),
        (
            "thickness-padding",
            "Thickness resources on Padding and BorderThickness, in one- and "
            "two-number form",
            ["L2-align"],
            l5_document("Border", l5_resources("Border",
                        '<Thickness x:Key="Inset">6</Thickness>'
                        '<Thickness x:Key="Edge">2,4</Thickness>')
                        + '<Border Padding="{StaticResource Inset}"'
                          ' BorderThickness="{StaticResource Edge}">'
                          '<Border Width="40" Height="20"/></Border>'),
            l5_document("Border", '<Border Padding="6" BorderThickness="2,4">'
                                  '<Border Width="40" Height="20"/></Border>'),
            None,
        ),
        (
            "int32-attached",
            "x:Int32 resources on the Grid attached properties",
            ["L3-grid"],
            l5_document("Grid", l5_resources("Grid",
                        '<x:Int32 x:Key="SecondColumn">1</x:Int32>'
                        '<x:Int32 x:Key="BothColumns">2</x:Int32>')
                        + "<Grid.ColumnDefinitions>"
                          '<ColumnDefinition Width="40"/><ColumnDefinition Width="*"/>'
                          "</Grid.ColumnDefinitions>"
                        + '<Border Grid.Column="{StaticResource SecondColumn}" Height="20"/>'
                        + '<Border Grid.Column="0"'
                          ' Grid.ColumnSpan="{StaticResource BothColumns}" Height="10"/>'),
            l5_document("Grid", "<Grid.ColumnDefinitions>"
                        '<ColumnDefinition Width="40"/><ColumnDefinition Width="*"/>'
                        "</Grid.ColumnDefinitions>"
                        '<Border Grid.Column="1" Height="20"/>'
                        '<Border Grid.Column="0" Grid.ColumnSpan="2" Height="10"/>'),
            None,
        ),
        (
            "named-argument",
            "{StaticResource ResourceKey=X} is the same reference as "
            "{StaticResource X}",
            ["L1-sizing"],
            l5_document("Border", l5_resources("Border", double)
                        + '<Border Width="{StaticResource ResourceKey=BoxWidth}" Height="30"/>'),
            l5_document("Border", child_inline),
            None,
        ),
        (
            "resource-element-value",
            "the reference written as an element: <StaticResource ResourceKey=.../>",
            ["L1-sizing"],
            l5_document("Border", l5_resources("Border", double)
                        + '<Border Height="30"><Border.Width>'
                          '<StaticResource ResourceKey="BoxWidth"/></Border.Width></Border>'),
            l5_document("Border", '<Border Height="30" Width="60"/>'),
            None,
        ),
        (
            "element-local",
            "an element reads its own dictionary, declared just above the use",
            ["L1-sizing"],
            l5_document("Border", l5_resources("Border", double)
                        + '<Border.Width><StaticResource ResourceKey="BoxWidth"/></Border.Width>'
                        + '<Border Width="20" Height="30"/>'),
            l5_document("Border", '<Border Width="20" Height="30"/>', attributes='Width="60"'),
            None,
        ),
        (
            "alias-entry",
            "<StaticResource x:Key=... ResourceKey=.../> aliases an earlier key, "
            "which is the form Terminal's theme dictionaries are built from",
            ["L1-sizing"],
            l5_document("Border", l5_resources("Border",
                        '<x:Double x:Key="BaseWidth">60</x:Double>'
                        '<StaticResource x:Key="BoxWidth" ResourceKey="BaseWidth"/>')
                        + child),
            l5_document("Border", child_inline),
            None,
        ),
        (
            "mixed-types",
            "one dictionary of three types, read by three siblings",
            ["L3-stack"],
            l5_document("StackPanel", l5_resources("StackPanel",
                        '<x:Double x:Key="ItemWidth">30</x:Double>'
                        '<x:Double x:Key="ItemHeight">18</x:Double>'
                        '<Thickness x:Key="ItemMargin">2,4</Thickness>', explicit=True)
                        + '<Border Width="{StaticResource ItemWidth}"'
                          ' Height="{StaticResource ItemHeight}"'
                          ' Margin="{StaticResource ItemMargin}"/>'
                        + '<Border Width="{StaticResource ItemWidth}"'
                          ' Height="{StaticResource ItemHeight}"'
                          ' Margin="{StaticResource ItemMargin}"/>'
                        + '<Border Width="{StaticResource ItemWidth}"'
                          ' Height="{StaticResource ItemHeight}"/>',
                        attributes='Orientation="Horizontal"'),
            l5_document("StackPanel",
                        '<Border Width="30" Height="18" Margin="2,4"/>'
                        '<Border Width="30" Height="18" Margin="2,4"/>'
                        '<Border Width="30" Height="18"/>',
                        attributes='Orientation="Horizontal"'),
            None,
        ),
        (
            "min-max",
            "resources on MinWidth and MaxHeight, where both clamps actually bite",
            ["L1-sizing"],
            l5_document("Border", l5_resources("Border",
                        '<x:Double x:Key="Floor">80</x:Double>'
                        '<x:Double x:Key="Ceiling">40</x:Double>')
                        + '<Border MinWidth="{StaticResource Floor}"'
                          ' MaxHeight="{StaticResource Ceiling}" Width="20" Height="90"/>'),
            l5_document("Border",
                        '<Border MinWidth="80" MaxHeight="40" Width="20" Height="90"/>'),
            None,
        ),
        (
            "brace-escape",
            "{} escapes a leading brace, so the value is text and not a lookup. "
            "The twin writes the same text as element content, so a mismatch is "
            "either the escape or Text= differing from content",
            ["L4-text"],
            l5_document("Border",
                        f'<TextBlock {text_attrs} Text="{{}}{{StaticResource NotAKey}}"/>'),
            l5_document("Border",
                        f"<TextBlock {text_attrs}>{{StaticResource NotAKey}}</TextBlock>"),
            None,
        ),
        (
            "string-resource",
            "an x:String resource on TextBlock.Text",
            ["L4-text"],
            l5_document("Border", l5_resources("Border",
                        '<x:String x:Key="Caption">M</x:String>')
                        + f'<TextBlock {text_attrs} Text="{{StaticResource Caption}}"/>'),
            l5_document("Border", f'<TextBlock {text_attrs} Text="M"/>'),
            None,
        ),
        (
            "double-fontsize",
            "an x:Double resource on TextBlock.FontSize, so the resolved value "
            "has to reach text measurement and not just a layout slot",
            ["L4-text"],
            l5_document("Border", l5_resources("Border",
                        '<x:Double x:Key="Big">24</x:Double>')
                        + '<TextBlock FontFamily="Segoe UI"'
                          ' FontSize="{StaticResource Big}" Text="M"/>'),
            l5_document("Border",
                        '<TextBlock FontFamily="Segoe UI" FontSize="24" Text="M"/>'),
            None,
        ),
        (
            "forward-reference-child",
            "a child reads a key its parent declares *after* it",
            ["L1-sizing"],
            l5_document("Border", child + l5_resources("Border", double)),
            None,
            "WPF resolves StaticResource as it parses and fails on a forward "
            "reference. Whether WinUI's parser defers far enough to find a "
            "dictionary written below the use is not recorded anywhere we can "
            "check, and the two behaviours are indistinguishable in markup that "
            "declares its resources first -- which all of Terminal's does.",
        ),
        (
            "forward-reference-self",
            "an element reads a key from its own dictionary, in an attribute "
            "the parser sees before the dictionary",
            ["L1-sizing"],
            l5_document("Border", l5_resources("Border", double)
                        + '<Border Width="20" Height="30"/>',
                        attributes='Width="{StaticResource BoxWidth}"'),
            None,
            "An element's attributes are read when its start tag is, and its "
            "Resources are a child of it. Whether the element's own dictionary "
            "is nonetheless in scope for its own attributes decides whether "
            "'element-local lookup' means anything stronger than 'the subtree "
            "below it'.",
        ),
        (
            "gridlength-resource",
            "a GridLength declared as an object element, on a star column",
            ["L3-grid"],
            l5_document("Grid", l5_resources("Grid", '<GridLength x:Key="Wide">2*</GridLength>')
                        + "<Grid.ColumnDefinitions>"
                          '<ColumnDefinition Width="{StaticResource Wide}"/>'
                          '<ColumnDefinition Width="*"/>'
                          "</Grid.ColumnDefinitions>"
                        + '<Border Grid.Column="0" Height="20"/>'
                          '<Border Grid.Column="1" Height="20"/>'),
            None,
            "A star length has no x-namespace primitive, so the only way to put "
            "one in a dictionary is <GridLength>. Terminal never does it, and "
            "whether WinUI 2 will construct a GridLength from element content is "
            "untested. This implementation accepts it; if the runtime does not, "
            "the permissiveness is the bug.",
        ),
        (
            "gridlength-from-double",
            "an x:Double resource on ColumnDefinition.Width, whose type is "
            "GridLength",
            ["L3-grid"],
            l5_document("Grid", l5_resources("Grid", '<x:Double x:Key="Fixed">40</x:Double>')
                        + "<Grid.ColumnDefinitions>"
                          '<ColumnDefinition Width="{StaticResource Fixed}"/>'
                          '<ColumnDefinition Width="*"/>'
                          "</Grid.ColumnDefinitions>"
                        + '<Border Grid.Column="0" Height="20"/>'
                          '<Border Grid.Column="1" Height="20"/>'),
            None,
            "Width=\"40\" written literally is converted to a GridLength by the "
            "attribute parser. A resource is assigned, not parsed, so the same "
            "conversion may not happen. This implementation rejects it, which is "
            "the guess that a resource is type-checked rather than re-parsed.",
        ),
    ]


def level5() -> Iterator[dict[str, Any]]:
    for slug, note, requires, markup, inline, question in l5_scenarios():
        case_id = f"L5-resources-{slug}"
        twin_id = f"{case_id}-inline" if inline else None
        yield case(case_id, 5, "resources", markup, [400.0, 300.0], note,
                   requires=requires, twin=twin_id, question=question)
        if inline:
            yield case(twin_id, 5, "resources", inline, [400.0, 300.0],
                       f"inline twin of {case_id}: {note}",
                       requires=requires)


LEVELS = {0: level0, 1: level1, 2: level2, 3: level3, 4: level4, 5: level5}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", type=Path,
                    default=Path(__file__).resolve().parents[1] / "xaml-db" / "cases")
    ap.add_argument("--levels", type=int, nargs="*", default=sorted(LEVELS))
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    counts: dict[str, int] = {}
    for level in args.levels:
        for spec in LEVELS[level]():
            group = f"L{level}-{spec['group']}"
            counts[group] = counts.get(group, 0) + 1
            if args.dry_run:
                continue
            d = args.out / group
            d.mkdir(parents=True, exist_ok=True)
            text = json.dumps(spec, indent=1, sort_keys=True, allow_nan=False)
            (d / f"{spec['id']}.json").write_text(text + "\n", encoding="utf-8")

    for group in sorted(counts):
        print(f"  {group:<14} {counts[group]:>5}")
    print(f"  {'total':<14} {sum(counts.values()):>5}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
