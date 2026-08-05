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


# --- L0: the property system --------------------------------------------------
# A dependency property's effective value is chosen from a precedence chain --
# a local value, then one inherited from an ancestor, then the default it was
# registered with -- and none of that is visible in a plain field.
#
# What a case here can assert is narrower than what the property system does,
# and the limit is the probe: it records DesiredSize, ActualWidth/Height and the
# layout slot, and nothing else. A property has to reach a number to be
# measurable at all. Two consequences, stated rather than left to be
# rediscovered:
#
#  * FontSize is the inherited property the corpus can see, because an empty
#    TextBlock is exactly one line tall and that line is the font's
#    baseline-to-baseline distance scaled by it. FontFamily and Foreground
#    inherit by the same mechanism and neither moves a number -- Foreground
#    paints, and telling one FontFamily from another would need a second
#    family's metrics harvested. Their cases are here and say plainly that they
#    only show the markup loads.
#
#  * Clearing a local value has no attribute syntax, so the one thing a field
#    genuinely cannot do -- go back to reading its ancestor -- cannot be a case
#    at all. phase3/layout/tests/property_test.cpp is where that is checked.


def content_control(attributes: str, inner: str) -> str:
    return f'<ContentControl xmlns="{XMLNS}" {attributes}>{inner}</ContentControl>'


def level0() -> Iterator[dict[str, Any]]:
    probes: list[tuple[str, str, str]] = [
        # --- where a value comes from, with no font involved ------------------
        ("default", f'<Border xmlns="{XMLNS}"/>',
         "Width unset stays Auto, and an Auto Border stretches to its slot"),
        ("local", f'<Border xmlns="{XMLNS}" Width="120"/>',
         "a local Width wins over the default"),
        ("local-opacity", f'<Border xmlns="{XMLNS}" Opacity="0.5"/>',
         "a local Opacity on a UIElement, which changes no number -- it shows the "
         "property is accepted where it is declared"),

        # UseLayoutRounding is the one property outside sizing whose value a
        # measurement can see: with it on a fractional width snaps to a whole
        # pixel, and with it off it does not. The pair pins the default and the
        # effect at once.
        ("rounding-default", f'<Border xmlns="{XMLNS}" Width="120.25"/>',
         "UseLayoutRounding defaults on, so a fractional Width snaps"),
        ("rounding-off", f'<Border xmlns="{XMLNS}" Width="120.25" UseLayoutRounding="False"/>',
         "a local False leaves the fraction alone"),
        # Whether it inherits is genuinely open. WPF's is an inherited property;
        # WinUI documents the effect as reaching the subtree without saying what
        # carries it, and nothing measured so far separates "inherited" from
        # "the parent rounded and the child had no fraction to round".
        ("rounding-inherited",
         f'<Border xmlns="{XMLNS}" UseLayoutRounding="False"><Border Width="120.25"/></Border>',
         "does UseLayoutRounding reach a child that never set it"),
        # Deliberately a half, and the only case in the corpus that is one.
        # Layout rounding is a Math.Round in the ported source, which breaks
        # ties to even -- 120.5 to 120 rather than 121 -- and nothing measured
        # has ever landed on a tie to say whether the runtime agrees.
        ("rounding-half", f'<Border xmlns="{XMLNS}" Width="120.5"/>',
         "an exact half, which is where round-half-to-even and round-half-away part"),

        # --- inheritance ------------------------------------------------------
        # FontSize has to be set on a Control. This was written on a StackPanel
        # first, which is a WPF habit -- there TextElement.FontSize is an
        # inherited attached property that any FrameworkElement takes. WinUI has
        # no such attached property: FontSize is declared on Control and on
        # TextBlock and nowhere else, so the runtime rejected the markup with
        # "The property 'FontSize' was not found in type 'StackPanel'".
        ("inherited-fontsize",
         '<ContentControl xmlns="%s" FontSize="22"><TextBlock x:Name="t" '
         'xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"/></ContentControl>' % XMLNS,
         "FontSize inherits from a Control down to its content"),

        # Nothing sets FontSize anywhere, so the line height is the registered
        # default and nothing else. That default is taken from the SDK today,
        # and is the last number in the property system with no measurement
        # behind it.
        ("inherited-default", content_control("", "<TextBlock/>"),
         "no FontSize anywhere, so the line height reports the default"),

        ("local-beats-inherited", content_control('FontSize="22"', '<TextBlock FontSize="11"/>'),
         "a local FontSize wins over the one above it"),
        # The case the precedence chain turns on: 14 is also the default, so an
        # implementation that treats "written out" as "never set" answers 22
        # here and is right everywhere else.
        ("local-default-beats-inherited",
         content_control('FontSize="22"', '<TextBlock FontSize="14"/>'),
         "a local value equal to the default is still a local value"),

        # An element with no FontSize of its own must not stop one passing
        # through it: inheritance walks the tree, it does not hop between the
        # types that happen to declare the property.
        ("inherits-through-border",
         content_control('FontSize="22"', "<Border><TextBlock/></Border>"),
         "an inherited value passes through a Border, which has no FontSize"),
        ("inherits-through-stackpanel",
         content_control('FontSize="22"', "<StackPanel><TextBlock/></StackPanel>"),
         "and through a StackPanel, which the runtime refuses to let one be set on"),

        ("inherits-nearest-ancestor",
         content_control('FontSize="22"',
                         '<ContentControl FontSize="33"><TextBlock/></ContentControl>'),
         "the nearest ancestor with a value wins, not the outermost"),
        ("inherits-past-a-silent-control",
         content_control('FontSize="22"',
                         "<Border><ContentControl><TextBlock/></ContentControl></Border>"),
         "a Control that sets no FontSize passes the outer one through"),

        # Not a property-system question, but the one thing a ContentControl
        # does that the inheritance cases above cannot see. Their content is an
        # empty TextBlock at the origin, which is where Left/Top and Stretch
        # both put it. A Border that asks for nothing is not: stretched it
        # fills the control, and left alone it stays at zero. Every case above
        # depends on the answer and none of them reveals it.
        ("content-stretch", content_control("", "<Border/>"),
         "does a ContentControl stretch its content or leave it at its desired size"),

        # --- inherited, and invisible to a measurement ------------------------
        ("inherited-fontfamily", content_control('FontFamily="Segoe UI"', "<TextBlock/>"),
         "FontFamily inherits, but only one family's metrics are harvested, so no "
         "number separates this from the default -- it shows the markup loads"),
        ("inherited-foreground", content_control('Foreground="Red"', "<TextBlock/>"),
         "Foreground inherits and paints rather than measures, so no number can see "
         "it -- it shows the markup loads"),
    ]
    for name, markup, note in probes:
        yield case(f"L0-props-{name}", 0, "props", markup, [400.0, 300.0], note)


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


# --- L1: shapes and images ----------------------------------------------------
# Leaf elements whose size comes from their own content rather than from
# children, which is what puts them at level 1 next to the lone Border.
#
# The geometries are chosen to separate three things an implementation can get
# wrong and still look right on a figure that starts at the origin: that a
# shape's desired size is the *right and bottom* of its bounds rather than
# their width and height, that a figure in negative space does not push those
# edges outwards, and that a curve's extreme is not the extreme of its control
# points. The two curved samples both have a control point at 20 and neither
# curve reaches it.
GEOMETRIES = [
    ("origin-line", "M0,0 L10,10"),
    ("offset-line", "M20,5 L30,25"),
    ("negative-start", "M-10,-10 L5,5"),
    ("cubic-arch", "M0,0 C0,20 20,20 20,0 Z"),
    ("quadratic-arch", "M0,0 Q10,20 20,0 Z"),
    ("terminal-marker", "M 0 0 L 4 5.5996094 L 4 14 L 5 14 L 5 0 L 0 0 z"),
]

SHAPE_SIZES = [[400.0, 300.0], [10.0, 10.0], [float("inf"), 300.0]]


def level1_shape() -> Iterator[dict[str, Any]]:
    n = 0
    for name, data in GEOMETRIES:
        for sized in [False, True]:
            for avail in SHAPE_SIZES:
                n += 1
                attrs = ' Width="40" Height="40"' if sized else ""
                markup = f'<Path xmlns="{XMLNS}" Data="{data}"{attrs}/>'
                yield case(
                    f"L1-shape-{n:04d}", 1, "shape", markup, avail,
                    f"Path {name} sized={sized}",
                    requires=["L1-sizing"],
                )

    # A PathIcon is a one-child host around a Path, so it should report exactly
    # what the Path reports -- and it is the icon element whose size does not
    # depend on a font, which is why it is here and FontIcon is not.
    for name, data in GEOMETRIES:
        for avail in SHAPE_SIZES:
            n += 1
            markup = f'<PathIcon xmlns="{XMLNS}" Data="{data}"/>'
            yield case(
                f"L1-shape-{n:04d}", 1, "shape", markup, avail,
                f"PathIcon {name}",
                requires=["L1-sizing"],
            )

    # An Image with no Source has no natural bounds, so both passes return
    # nothing. The sized variant is the interesting one: an explicit Width
    # reaches the desired size through the outer half of the contract but never
    # reaches ArrangeOverride, so the two should disagree.
    for sized in [False, True]:
        for avail in SHAPE_SIZES:
            n += 1
            attrs = ' Width="40" Height="40"' if sized else ""
            markup = f'<Image xmlns="{XMLNS}"{attrs}/>'
            yield case(
                f"L1-shape-{n:04d}", 1, "shape", markup, avail,
                f"Image with no Source sized={sized}",
                requires=["L1-sizing"],
            )


# --- L2: a content host and its content ---------------------------------------
# ContentPresenter has a second pair of alignment properties that position the
# content inside the presenter, and they are not the ones that position the
# presenter inside its parent. The cross product is over those, because the
# content's layout slot is the only place their effect is visible.
def level2_content() -> Iterator[dict[str, Any]]:
    n = 0
    for hca in H_ALIGN:
        for vca in V_ALIGN:
            for padding in ["0", "8,4,12,6"]:
                for avail in [[400.0, 300.0], [100.0, 50.0]]:
                    n += 1
                    markup = (f'<ContentPresenter xmlns="{XMLNS}" Padding="{padding}" '
                              f'HorizontalContentAlignment="{hca}" '
                              f'VerticalContentAlignment="{vca}">'
                              '<Border Width="40" Height="20"/></ContentPresenter>')
                    yield case(
                        f"L2-content-{n:04d}", 2, "content", markup, avail,
                        f"ContentPresenter content {hca}/{vca} padding={padding}",
                        requires=["L2-align"],
                    )

    # With neither alignment set, so that the defaults are measured rather than
    # assumed. Nothing else in the corpus pins them.
    for padding in ["0", "8,4,12,6"]:
        for child in ['Width="40" Height="20"', 'Margin="4"']:
            for avail in [[400.0, 300.0], [100.0, 50.0]]:
                n += 1
                markup = (f'<ContentPresenter xmlns="{XMLNS}" Padding="{padding}">'
                          f'<Border {child}/></ContentPresenter>')
                yield case(
                    f"L2-content-{n:04d}", 2, "content", markup, avail,
                    f"ContentPresenter default content alignment padding={padding}",
                    requires=["L2-align"],
                )


# --- L3: Canvas ---------------------------------------------------------------
# The panel that does no layout, which makes it the one whose *own* size is in
# question. Every variant here is arranged into a slot bigger than anything it
# contains, so a Canvas that took its slot and a Canvas that took nothing are
# two different numbers in every case.
def level3_canvas() -> Iterator[dict[str, Any]]:
    n = 0
    positions = [("0", "0"), ("25", "10"), ("-5", "-5")]
    variants = [
        ("auto", ""),
        # An explicit size is the sharpest form of the question: the outer half
        # of the contract clamps the arrange size to 200x150 before
        # ArrangeOverride is ever called.
        ("sized", ' Width="200" Height="150"'),
        ("near", ' HorizontalAlignment="Left" VerticalAlignment="Top"'),
    ]
    for left, top in positions:
        for child_fixed in [True, False]:
            for variant, attrs in variants:
                for avail in [[400.0, 300.0], [100.0, 50.0], [float("inf"), 300.0]]:
                    n += 1
                    dim = 'Width="30" Height="20"' if child_fixed else 'Margin="4"'
                    kids = (f'<Border Canvas.Left="{left}" Canvas.Top="{top}" {dim}/>'
                            f'<Border Canvas.Left="40" Canvas.Top="40" {dim}/>')
                    markup = f'<Canvas xmlns="{XMLNS}"{attrs}>{kids}</Canvas>'
                    yield case(
                        f"L3-canvas-{n:04d}", 3, "canvas", markup, avail,
                        f"Canvas {variant} children at {left},{top} fixed={child_fixed}",
                        requires=["L2-align"],
                    )

    for variant, attrs in variants:
        for avail in [[400.0, 300.0], [100.0, 50.0], [float("inf"), 300.0]]:
            n += 1
            markup = f'<Canvas xmlns="{XMLNS}"{attrs}/>'
            yield case(
                f"L3-canvas-{n:04d}", 3, "canvas", markup, avail,
                f"empty Canvas {variant}",
                requires=["L2-align"],
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


# --- L5: the application dictionary -------------------------------------------
# Every case above declares the key it looks up. These do not: they name keys
# WinUI 2 declares, and nothing in the markup says what those keys hold. That
# makes them a different kind of case in three ways.
#
# **They are the only cases whose value comes from outside the corpus.** The
# literals in the twins below are quoted from WinUI 2.8.4 at commit 4aa80ad6,
# where `extract_winui_theme_resources.py` reads them. Quoting them here rather
# than reading the extract keeps this generator dependent on nothing, and
# `phase3/tests/test_theme_resource_cases.py` holds the two to agreeing, so a
# literal that drifts is a failing test instead of a twin that passes by
# comparing two copies of the same mistake.
#
# **They ask the host a question the corpus has never asked.** A resource case
# so far has been answerable by the markup alone. These are answerable only by
# whatever the probe's XAML host has in `Application.Resources`, and that is
# unknown: `WindowsXamlManager` supplies the OS's theme resources, and WinUI 2's
# arrive only when an application merges `XamlControlsResources`, which the
# probe does not. So each carries the question, and a rejection is the answer --
# specifically, the answer to whether the 21 harvested cases that were unblocked
# by the same dictionary can be measured at all.
#
# **They are twinned anyway.** The question is whether the lookup resolves; the
# twin says what it must resolve *to* if it does. Those are separate claims and
# only the first is unknown.

THEME_RESOURCE_CASES: list[tuple[str, str, str, str, list[str]]] = [
    # (slug, key, the literal WinUI 2.8.4 holds for it, property, requires)
    ("double-width", "ToggleSwitchThemeMinWidth", "154", "Width", ["L1-sizing"]),
    ("double-height", "ScrollBarVerticalThumbMinHeight", "30", "Height", ["L1-sizing"]),
    ("thickness-margin", "ToggleSwitchTopHeaderMargin", "0,0,0,4", "Margin", ["L2-align"]),
    ("thickness-padding", "FlyoutContentPadding", "16,15,16,17", "Padding", ["L2-align"]),
    ("thickness-border", "FlyoutBorderThemeThickness", "1", "BorderThickness", ["L2-align"]),
]


def level5_theme() -> Iterator[dict[str, Any]]:
    unknown_host = (
        "Whether a bare XamlReader.Load resolves a key from Application.Resources "
        "at all, and whether the probe's host has WinUI 2's theme dictionary in it "
        "rather than only the OS's. The value is not the question -- the twin "
        "asserts that -- the reachability is. A rejection here says the level 7 "
        "cases unblocked by the same dictionary cannot be measured until the probe "
        "merges XamlControlsResources, which is the finding this case exists for."
    )

    for slug, key, literal, prop, requires in THEME_RESOURCE_CASES:
        # Border, sized on one axis by the theme and pinned on the other, so a
        # lookup that silently resolved to nothing would change the number
        # rather than leaving the case looking plausible.
        other = 'Height="30"' if prop == "Width" else 'Width="50"'
        if prop in ("Margin", "Padding", "BorderThickness"):
            body = (f'<Border {prop}="{{ph}}"><Border Width="20" Height="20"/></Border>')
            inline_body = body.replace("{ph}", literal)
            themed_body = body.replace("{ph}", f"{{ThemeResource {key}}}")
        else:
            themed_body = f'<Border {prop}="{{ThemeResource {key}}}" {other}/>'
            inline_body = f'<Border {prop}="{literal}" {other}/>'

        case_id = f"L5-theme-{slug}"
        twin_id = f"{case_id}-inline"
        note = f"{prop} from the theme key {key}"
        yield case(case_id, 5, "theme", l5_document("Grid", themed_body), [400.0, 300.0],
                   note, requires=requires, twin=twin_id, question=unknown_host)
        yield case(twin_id, 5, "theme", l5_document("Grid", inline_body), [400.0, 300.0],
                   f"inline twin of {case_id}: {prop}=\"{literal}\", the value WinUI 2.8.4 "
                   f"holds for {key}",
                   requires=requires)

    # The two spellings of one lookup. If they resolve differently the corpus
    # has to say which, and no other case in the corpus can: every StaticResource
    # case above resolves against markup, where a ThemeResource would be a
    # different question again.
    key, literal = "ToggleSwitchThemeMinWidth", "154"
    yield case("L5-theme-staticresource-spelling", 5, "theme",
               l5_document("Grid", f'<Border Width="{{StaticResource {key}}}" Height="30"/>'),
               [400.0, 300.0],
               f"the same application-dictionary key reached by {{StaticResource}} instead",
               requires=["L1-sizing"], twin="L5-theme-double-width-inline",
               question=unknown_host)

    # A brush, which cannot move a number and is here for exactly that reason:
    # it is the one resource type the corpus can check the *acceptance* of and
    # not the value, and the application dictionary is mostly brushes.
    yield case("L5-theme-brush-background", 5, "theme",
               l5_document("Grid",
                           '<Border Background="{ThemeResource SystemControlTransparentBrush}"'
                           ' Width="50" Height="30"/>'),
               [400.0, 300.0],
               "a brush from the theme dictionary on Background, which no measurement can see",
               requires=["L1-sizing"], twin="L5-theme-brush-background-inline",
               question=unknown_host)
    yield case("L5-theme-brush-background-inline", 5, "theme",
               l5_document("Grid", '<Border Background="Transparent" Width="50" Height="30"/>'),
               [400.0, 300.0],
               "inline twin of L5-theme-brush-background: Background=\"Transparent\", the colour "
               "WinUI 2.8.4 gives SystemControlTransparentBrush",
               requires=["L1-sizing"])


# A list per level rather than one generator, because a level is a question
# and not a file: level 1 asks what a lone element does, and a Path is as much
# a lone element as a Border is.
LEVELS = {
    0: [level0],
    1: [level1, level1_shape],
    2: [level2, level2_content],
    3: [level3, level3_canvas],
    4: [level4],
    5: [level5, level5_theme],
}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", type=Path,
                    default=Path(__file__).resolve().parents[1] / "xaml-db" / "cases")
    ap.add_argument("--levels", type=int, nargs="*", default=sorted(LEVELS))
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    counts: dict[str, int] = {}
    for level in args.levels:
        for generator in LEVELS[level]:
            for spec in generator():
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
