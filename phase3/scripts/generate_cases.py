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


# --- L3: ScrollViewer ---------------------------------------------------------
# The one type in the corpus whose recorded cases contradict each other.
#
# Terminal supplies three ScrollViewer subtrees and the oracle measured all
# three. Two of them report their content's desired size plus their own padding
# and then stretch the content to the viewport -- what a viewer with both scroll
# directions off does. The third asks for sixteen more pixels in each axis,
# exactly one scroll bar's worth per axis, and arranges its content at the
# content's own desired size -- what a viewer with both directions on does.
# Neither sets a scroll bar visibility, and no property in the markup separates
# them, so implementing either reading breaks the other. Nine L7 cases are
# blocked on that and the layout core refuses ScrollViewer rather than guess.
#
# This series exists to make the guess unnecessary. The axes are everything a
# reading of that contradiction could turn on:
#
#   * HorizontalScrollBarVisibility x VerticalScrollBarVisibility, all sixteen
#     combinations of Disabled/Auto/Hidden/Visible -- the properties that are
#     documented to decide it and that the recorded cases leave at their default
#   * HorizontalScrollMode x VerticalScrollMode, which decide scrollability
#     independently of whether a bar is shown, so they separate "a bar is in the
#     layout" from "the content may exceed the viewport"
#   * content smaller than, exactly equal to, and larger than the viewport in
#     each axis independently, which is what an Auto bar's computed visibility
#     is supposed to turn on -- and the boundary is where an off-by-one lives
#   * an explicitly sized viewer against an unconstrained one, because the first
#     asks what arrange does with the reservation and the second asks whether
#     measure adds it
#   * padding on the viewer, which the recorded cases show reaching the desired
#     size, so the question is whether the reservation lands inside or outside it
#   * six replicas of the three Terminal shapes built out of Border, which is
#     what tells us whether the disagreement is a property of the viewer or of
#     the TextBlock that only the odd one out contains
#
# Every child is a Border, so nothing here needs a font: a text metric in the
# middle of the answer would put the ScrollViewer question and the DirectWrite
# question in the same number.
#
# None of these carries `oracle_decides`. Every property set below is a plain
# enum value on a documented property, so a rejection would be a finding about
# the corpus rather than an answer we asked for, and declaring the question in
# advance would only make that finding quiet.

SCROLL_BAR_VISIBILITIES = ["Disabled", "Auto", "Hidden", "Visible"]

# Auto is deprecated on ScrollMode and documented as "do not use", so the axis
# is the two values that mean something.
SCROLL_MODES = ["Disabled", "Enabled"]

# A viewer of this size holds `small` with room to spare and cannot hold
# `large` in either axis. `equal-*` are the two candidate viewports: 200x150 if
# no bar is in the layout, and 184x134 if one is in each axis.
SCROLL_VIEWER_SIZE = (200.0, 150.0)
SCROLL_CONTENTS = {
    "small": (60.0, 40.0),
    "large": (300.0, 260.0),
}


def scroll_viewer(attributes: str, child: str) -> str:
    head = f'<ScrollViewer xmlns="{XMLNS}"'
    if attributes:
        head += " " + attributes
    return f"{head}>{child}</ScrollViewer>"


def sized_border(size: tuple[float, float]) -> str:
    return f'<Border Width="{size[0]:g}" Height="{size[1]:g}"/>'


def visibility_attributes(horizontal: str, vertical: str) -> str:
    # The direct properties, not the ScrollViewer.* attached spelling. They are
    # the same dependency property in the runtime, but only the direct form is
    # what a ScrollViewer's own markup would ever say, and a case should ask its
    # question in the spelling the answer will be used in.
    return (f'HorizontalScrollBarVisibility="{horizontal}" '
            f'VerticalScrollBarVisibility="{vertical}"')


def level3_scroll() -> Iterator[dict[str, Any]]:
    needs = ["L2-align", "L3-grid"]
    sized = f'Width="{SCROLL_VIEWER_SIZE[0]:g}" Height="{SCROLL_VIEWER_SIZE[1]:g}"'

    # --- the cross product, in a viewer whose size is not in question ---------
    # With the viewer pinned at 200x150 the only thing left to vary is where the
    # sixteen pixels go, so this is the series that says when a bar is in the
    # layout at all.
    for horizontal in SCROLL_BAR_VISIBILITIES:
        for vertical in SCROLL_BAR_VISIBILITIES:
            for content, size in sorted(SCROLL_CONTENTS.items()):
                attributes = f"{sized} {visibility_attributes(horizontal, vertical)}"
                yield case(
                    f"L3-scroll-vis-{horizontal.lower()}-{vertical.lower()}-{content}",
                    3, "scroll", scroll_viewer(attributes, sized_border(size)),
                    [400.0, 300.0],
                    f"ScrollViewer 200x150 h={horizontal} v={vertical}, "
                    f"{content} content {size[0]:g}x{size[1]:g}",
                    requires=needs,
                )

    # --- the same cross product with nothing pinning the viewer ---------------
    # This is the half the three recorded cases actually disagree about: what a
    # ScrollViewer *asks* for. Three available sizes, including one that cannot
    # hold the content and one that is infinite in both axes, which is what a
    # StackPanel would hand it.
    for horizontal in SCROLL_BAR_VISIBILITIES:
        for vertical in SCROLL_BAR_VISIBILITIES:
            for index, avail in enumerate([[400.0, 300.0], [100.0, 50.0],
                                           [float("inf"), float("inf")]]):
                yield case(
                    f"L3-scroll-free-{horizontal.lower()}-{vertical.lower()}-a{index}",
                    3, "scroll",
                    scroll_viewer(visibility_attributes(horizontal, vertical),
                                  sized_border(SCROLL_CONTENTS["small"])),
                    avail,
                    f"unconstrained ScrollViewer h={horizontal} v={vertical}, "
                    f"60x40 content",
                    requires=needs,
                )

    # --- scroll mode against scroll bar visibility ----------------------------
    # Two properties that both claim to decide scrolling. If the sizing rule
    # follows the bar, these change nothing; if it follows the mode, they change
    # everything, and the recorded contradiction is a default-mode question
    # rather than a default-visibility one.
    for horizontal_mode in SCROLL_MODES:
        for vertical_mode in SCROLL_MODES:
            for visibility in ["Disabled", "Auto", "Visible"]:
                for content, size in sorted(SCROLL_CONTENTS.items()):
                    attributes = (
                        f"{sized} {visibility_attributes(visibility, visibility)} "
                        f'HorizontalScrollMode="{horizontal_mode}" '
                        f'VerticalScrollMode="{vertical_mode}"'
                    )
                    yield case(
                        f"L3-scroll-mode-{horizontal_mode.lower()}-"
                        f"{vertical_mode.lower()}-{visibility.lower()}-{content}",
                        3, "scroll", scroll_viewer(attributes, sized_border(size)),
                        [400.0, 300.0],
                        f"ScrollViewer 200x150 bars={visibility}, "
                        f"hmode={horizontal_mode} vmode={vertical_mode}, "
                        f"{content} content",
                        requires=needs,
                    )

    # --- padding, inside or outside the reservation ---------------------------
    # One of the recorded cases has Padding="3" and reports its content plus six
    # pixels, so padding reaches the desired size. Whether the scroll bar sits
    # inside the padding or beside it is a separate number, and only an
    # asymmetric padding can tell which edge each one came off.
    for padding_name, padding in [("none", "0"), ("asym", "8,4,12,6")]:
        for visibility in ["Disabled", "Auto", "Visible"]:
            for content, size in sorted(SCROLL_CONTENTS.items()):
                for box, box_attributes in [("sized", sized), ("free", "")]:
                    attributes = (f'Padding="{padding}" '
                                  f"{visibility_attributes(visibility, visibility)}")
                    if box_attributes:
                        attributes = f"{box_attributes} {attributes}"
                    yield case(
                        f"L3-scroll-pad-{padding_name}-{visibility.lower()}-"
                        f"{content}-{box}",
                        3, "scroll", scroll_viewer(attributes, sized_border(size)),
                        [400.0, 300.0],
                        f"ScrollViewer {box} padding={padding} bars={visibility}, "
                        f"{content} content",
                        requires=needs,
                    )

    # --- the overflow boundary, one axis at a time ----------------------------
    # An Auto bar is supposed to appear only when that axis overflows, so the
    # interesting content sizes are the two candidate viewports themselves.
    # Crossing the widths against the heights separates a per-axis rule from a
    # rule that reserves both bars as soon as either axis overflows -- and that
    # second reading is exactly what the odd recorded case looks like.
    widths = [140.0, 184.0, 200.0, 260.0]
    heights = [110.0, 134.0, 150.0, 220.0]
    for width in widths:
        for height in heights:
            attributes = f"{sized} {visibility_attributes('Auto', 'Auto')}"
            yield case(
                f"L3-scroll-fit-{width:.0f}x{height:.0f}-auto", 3, "scroll",
                scroll_viewer(attributes, sized_border((width, height))),
                [400.0, 300.0],
                f"ScrollViewer 200x150 bars=Auto, content {width:g}x{height:g}",
                requires=needs,
            )
    # The same four content sizes with the bars forced on, as the control: a
    # Visible bar is in the layout whether or not anything overflows, so these
    # say what "reserved" costs before Auto has to decide anything.
    for width, height in zip(widths, heights):
        attributes = f"{sized} {visibility_attributes('Visible', 'Visible')}"
        yield case(
            f"L3-scroll-fit-{width:.0f}x{height:.0f}-visible", 3, "scroll",
            scroll_viewer(attributes, sized_border((width, height))),
            [400.0, 300.0],
            f"ScrollViewer 200x150 bars=Visible, content {width:g}x{height:g}",
            requires=needs,
        )

    # --- the three recorded shapes, rebuilt out of Border ---------------------
    # Same skeleton, same available sizes as the harvest, no text anywhere. If
    # these three split the way the recorded three did, the rule is in the
    # ScrollViewer; if they do not, it is in the TextBlock the odd one contains,
    # and the last three isolate the two attributes that only that case carries.
    shapes = [
        ("maxheight-margin", 'MaxHeight="100" Margin="0,8,0,0"', '<Border Height="16"/>',
         "the shape of the recorded case that asks for 16 more pixels per axis"),
        ("padded-stretch",
         'Padding="3" HorizontalAlignment="Stretch" VerticalAlignment="Stretch" '
         'Background="Transparent" BringIntoViewOnFocusChange="True" '
         'IsVerticalScrollChainingEnabled="True"',
         '<Border Padding="16" HorizontalAlignment="Stretch" Background="Transparent"/>',
         "the shape of the recorded case that sizes to content plus padding"),
        ("bare-padded-child", "", '<Border Padding="16,0,16,48"/>',
         "the shape of the recorded case whose child carries all the padding"),
        ("maxheight-only", 'MaxHeight="100"', '<Border Height="16"/>',
         "MaxHeight on its own, which only the odd recorded case sets"),
        ("margin-only", 'Margin="0,8,0,0"', '<Border Height="16"/>',
         "Margin on its own, the other attribute only that case sets"),
        ("zero-width-child", "", '<Border Height="16"/>',
         "neither, so a zero-width child under a bare viewer"),
    ]
    for name, attributes, child, note in shapes:
        for index, avail in enumerate([[400.0, 300.0], [100.0, 50.0],
                                       [float("inf"), 300.0]]):
            yield case(
                f"L3-scroll-shape-{name}-a{index}", 3, "scroll",
                scroll_viewer(attributes, child), avail, note, requires=needs,
            )


# --- L4: FontIcon -------------------------------------------------------------
# The largest single L7 blocker: fifteen harvested cases are one FontIcon each,
# and every one of them is blocked on glyph metrics for an icon font rather than
# on layout. Whether a FontIcon measures the glyph it was given or simply
# reports a FontSize square is not recorded anywhere, and the two are
# indistinguishable in an icon font where every glyph is exactly one em wide --
# which most of Segoe MDL2 Assets is.
#
# So the series is deliberately not only icon fonts. A FontIcon in Segoe UI
# holding "M" measures a glyph whose advance the corpus already solved out of
# its own L4 measurements, and Segoe UI's "M" is not one em. That case alone
# separates the two readings, without needing the harvest to have run.
#
# It sits at L4 because its answer depends on a font, which is the property
# every other level is kept free of.

# The five glyphs the fifteen blocked L7 cases actually use, plus the two
# Terminal reaches for most often across its markup (U+E710 "Add" and U+E74D
# "Delete", eleven uses each). Extracted from the checkout by
# phase3/scripts/harvest_icon_glyphs.py, which is also what tells the CI font
# harvest which codepoints to read; the list is pinned here because the
# generator has no checkout to read.
ICON_GLYPHS = [0xE710, 0xE74C, 0xE74D, 0xE76C, 0xE8BB, 0xE8E5, 0xE932]

# The one held fixed while another axis moves. U+E76C is the chevron one of the
# blocked L7 cases uses. Written as a codepoint rather than as the character
# itself: a private-use glyph pasted into source shows as an empty box in most
# editors, and a case corpus that cannot be read is a case corpus that cannot be
# reviewed.
ICON_SAMPLE = 0xE76C

# Every FontSize a FontIcon in Terminal's markup is given, literal or through a
# resource: 10, 11, 12, 16 and 32 are written inline, 14 arrives twice through
# x:Double resources (StandardIconSize, EditButtonIconSize), and 28 of the 94
# FontIcons set none at all -- so "unset" is a value here, and the one that
# measures the property's default.
ICON_FONT_SIZES: list[str | None] = [None, "10", "11", "12", "14", "16", "32"]

# The families Terminal names, spelled exactly as its markup spells them. The
# unset case matters most: the majority of its FontIcons take whatever the
# default style supplies, and which font that is on this runner is a
# measurement rather than a fact we have.
ICON_FAMILIES: list[tuple[str, str | None]] = [
    ("default", None),
    ("mdl2", "Segoe MDL2 Assets"),
    ("fluent", "Segoe Fluent Icons"),
    ("fallback", "Segoe UI, Segoe Fluent Icons, Segoe MDL2 Assets"),
]


def font_icon(glyph: str, family: str | None = None,
              font_size: str | None = None, extra: str = "") -> str:
    attributes = ""
    if family is not None:
        attributes += f' FontFamily="{family}"'
    if font_size is not None:
        attributes += f' FontSize="{font_size}"'
    if extra:
        attributes += " " + extra
    return f'<FontIcon xmlns="{XMLNS}"{attributes} Glyph="{glyph}"/>'


def level4_icon() -> Iterator[dict[str, Any]]:
    needs = ["L1-sizing", "L4-text"]

    # --- one glyph, two icon fonts, two sizes --------------------------------
    # Two sizes rather than one because the whole question is whether the
    # answer scales with FontSize; two families because the harvest reads both
    # and the L7 cases name both.
    for codepoint in ICON_GLYPHS:
        for slug, family in [("mdl2", "Segoe MDL2 Assets"),
                             ("fluent", "Segoe Fluent Icons")]:
            for font_size in ["12", "32"]:
                yield case(
                    f"L4-icon-glyph-{codepoint:04x}-{slug}-{font_size}", 4, "icon",
                    font_icon(chr(codepoint), family, font_size), [400.0, 300.0],
                    f"FontIcon U+{codepoint:04X} in {family} at {font_size}",
                    requires=needs,
                )

    # --- the FontSize axis, including no FontSize at all ----------------------
    # Swept at three available sizes as well, because an icon is a leaf and a
    # leaf that changes size with the space around it would be news.
    for font_size in ICON_FONT_SIZES:
        for index, avail in enumerate([[400.0, 300.0], [0.0, 0.0],
                                       [float("inf"), float("inf")]]):
            name = font_size or "unset"
            yield case(
                f"L4-icon-size-{name}-a{index}", 4, "icon",
                font_icon(chr(ICON_SAMPLE), None, font_size), avail,
                f"FontIcon U+E76C, default family, FontSize {name}",
                requires=needs,
            )

    # --- the family axis, including the fallback list Terminal writes ---------
    for slug, family in ICON_FAMILIES:
        for font_size in ["12", "32"]:
            yield case(
                f"L4-icon-family-{slug}-{font_size}", 4, "icon",
                font_icon(chr(ICON_SAMPLE), family, font_size), [400.0, 300.0],
                f"FontIcon U+E76C at {font_size} in "
                f"{family or 'whatever the default style supplies'}",
                requires=needs,
            )

    # --- what the sizing rule actually is ------------------------------------
    # The disambiguators. Everything above measures icon fonts we have no
    # independent numbers for; these are chosen so that the answer can be read
    # against something already known.
    rules: list[tuple[str, str, str]] = [
        # Segoe UI's 'M' is the one advance the corpus solved for itself, and it
        # is not one em. Glyph-bounds and FontSize-square give different widths
        # here, and only here.
        ("segoeui-m-12", font_icon("M", "Segoe UI", "12"),
         "FontIcon of 'M' in Segoe UI at 12, whose advance the corpus derived"),
        ("segoeui-m-14", font_icon("M", "Segoe UI", "14"),
         "the same at 14, the environment's own font size"),
        ("segoeui-m-24", font_icon("M", "Segoe UI", "24"),
         "and at 24, so the width either tracks the advance or stays square"),
        ("segoeui-mm-14", font_icon("MM", "Segoe UI", "14"),
         "two characters in Glyph: one glyph, or a run of them"),
        ("segoeui-empty-14", font_icon("", "Segoe UI", "14"),
         "no glyph at all, which says what the FontSize alone contributes"),
        ("segoeui-sized-14", font_icon("M", "Segoe UI", "14",
                                       'Width="40" Height="40"'),
         "an explicit size on the icon, which should reach it before the glyph does"),
        # A Latin letter asked of an icon font. Whether it falls back to a font
        # that has one, draws the notdef box, or measures nothing is three
        # different numbers, and Terminal's fallback list only makes sense if
        # the answer is the first.
        ("mdl2-latin-14", font_icon("M", "Segoe MDL2 Assets", "14"),
         "a Latin letter asked of an icon font: fallback, notdef, or nothing"),
        ("mdl2-margin-14", font_icon(chr(ICON_SAMPLE), "Segoe MDL2 Assets", "14",
                                     'Margin="4,8,12,16"'),
         "margin on the icon, so the reported desired size splits into two parts"),
        ("mdl2-weight-14", font_icon(chr(ICON_SAMPLE), "Segoe MDL2 Assets", "14",
                                     'FontWeight="Black"'),
         "FontWeight on an icon font, which one of the L7 cases sets"),
        # What that weight *adds*, which the two sizes measuring it cannot say.
        # `mdl2-weight-14` above answers 15 where the plain glyph answers 14,
        # and L7-terminal-88c43239e4 answers 11 at size 10 where the plain
        # glyph answers 10. A whole extra DIP, a twenty-fourth of the em and
        # two per cent of it all reproduce both, so the pair pins nothing and
        # the layout core refuses rather than choosing -- see
        # phase3/xaml-db/fonts/README.md.
        #
        # These sizes separate the three. At 100 they answer 101, 104.17 and
        # 102; at 200, 201, 208.33 and 204 -- distinct whichever way a fraction
        # becomes a whole number, and distinct again at the second size, so the
        # rule is confirmed rather than fitted to one observation. The plain
        # glyph is measured at both sizes beside them so the difference is read
        # off two recordings instead of off the harvest's claim that this glyph
        # is exactly one em wide.
        ("mdl2-plain-100", font_icon(chr(ICON_SAMPLE), "Segoe MDL2 Assets", "100"),
         "the unweighted glyph at 100, which is what the weighted ones below "
         "are a difference from"),
        ("mdl2-weight-100", font_icon(chr(ICON_SAMPLE), "Segoe MDL2 Assets", "100",
                                      'FontWeight="Black"'),
         "FontWeight=Black at 100, where a whole DIP, a twenty-fourth of the em "
         "and two per cent of it no longer agree"),
        ("mdl2-plain-200", font_icon(chr(ICON_SAMPLE), "Segoe MDL2 Assets", "200"),
         "the unweighted glyph at 200"),
        ("mdl2-weight-200", font_icon(chr(ICON_SAMPLE), "Segoe MDL2 Assets", "200",
                                      'FontWeight="Black"'),
         "and Black at 200, so whichever rule the pair above picks is confirmed "
         "at a second size rather than fitted to one"),
        # And whether the amount is a property of the weight or of there being
        # one at all: Bold is 700 where Black is 900, so a rule that scales with
        # the weight answers differently here and a rule that does not, does not.
        ("mdl2-bold-100", font_icon(chr(ICON_SAMPLE), "Segoe MDL2 Assets", "100",
                                    'FontWeight="Bold"'),
         "a lighter weight at the same size: does the amount track which weight "
         "it is, or only that there is one"),
        ("mdl2-near-14", font_icon(chr(ICON_SAMPLE), "Segoe MDL2 Assets", "14",
                                   'HorizontalAlignment="Left" VerticalAlignment="Top"'),
         "pinned to the corner, so an icon that stretches is visible as one"),
    ]
    for slug, markup, note in rules:
        yield case(f"L4-icon-rule-{slug}", 4, "icon", markup, [400.0, 300.0],
                   note, requires=needs)

    # In a container and nothing else -- no Viewbox, which would scale the icon
    # and make the number a Viewbox measurement instead. Padding on the Border
    # so the icon's contribution to its parent is separable from the parent's.
    for slug, attributes in [("bare", ""), ("padded", ' Padding="8,4,12,6"')]:
        yield case(
            f"L4-icon-in-border-{slug}", 4, "icon",
            f'<Border xmlns="{XMLNS}"{attributes}>'
            f'<FontIcon FontFamily="Segoe MDL2 Assets" FontSize="14" '
            f'Glyph="{chr(ICON_SAMPLE)}"/></Border>',
            [400.0, 300.0],
            f"FontIcon inside a {slug} Border, which is the only container here "
            f"-- a Viewbox would scale it and measure itself instead",
            requires=needs,
        )


# --- L4: Text= against element content -----------------------------------------
# The corpus writes a TextBlock's text both ways and has never checked that the
# two are the same thing. L4-text uses element content; the L5 string-resource
# and brace-escape cases use the Text attribute, and the brace-escape twin
# deliberately crosses the two -- so a disagreement there today would be read as
# the escape failing when it might be the spelling.
#
# These pairs are the unconfounded version: same text, same font, one written
# each way, twinned so check_twins.py holds them together with no oracle at all.
def level4_source() -> Iterator[dict[str, Any]]:
    attributes = 'FontFamily="Segoe UI" FontSize="14"'
    for slug, text in [("empty", ""), ("one", "M"), ("word", "Terminal")]:
        case_id = f"L4-source-{slug}-attribute"
        twin_id = f"L4-source-{slug}-content"
        yield case(
            case_id, 4, "source",
            f'<TextBlock xmlns="{XMLNS}" {attributes} Text="{text}"/>',
            [400.0, 300.0],
            f"TextBlock {text!r} written as Text=",
            requires=["L4-text"], twin=twin_id,
        )
        yield case(
            twin_id, 4, "source",
            f'<TextBlock xmlns="{XMLNS}" {attributes}>{text}</TextBlock>',
            [400.0, 300.0],
            f"TextBlock {text!r} written as element content",
            requires=["L4-text"],
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


# --- L5: styles ---------------------------------------------------------------
# The precedence layer between a local value and the default, and the second
# thing at L5 that is authored rather than crossed. Same shape as the resource
# series above and for the same reasons: a style resolves to the same value at
# every available size, so what varies is the rule rather than an axis, and
# every scenario we are confident about is emitted twice -- once behind a
# style, once with the same values written on the element -- so that the pair
# can be held to measuring identically before the oracle has seen either.
#
# That twinning is worth more here than it was for resources. A resource that
# resolves wrongly lands on a wrong number; a style that is applied at the wrong
# *precedence* lands on a number that is right in the case that was written and
# wrong in the one that was not. The pairs below deliberately include both
# directions -- a local value that must beat the style, and a style that must
# beat both the default and an inherited value -- because only the pair catches
# an implementation that stored a setter as a local value and looked correct.
#
# The value precedence the pairs assume, highest first:
#
#     local  >  style setter  >  inherited  >  registered default
#
# which is the core's own list -- "1. Local value 2. Style 3. Built-in style
# 4. Default value" in CDependencyObject::EvaluateBaseValue -- with inheritance
# sitting where the core puts it, as a read-time walk that stops at the first
# ancestor whose value is not still the default. A style setter makes a value
# non-default, so it both beats an inherited value and is itself inherited by
# descendants. There is no built-in-style layer here: nothing in this corpus has
# a control template, so nothing has a generic.xaml style to carry.


def l5_style(target: str, setters: str, key: str | None = None,
             based_on: str | None = None, wrapper: bool = False) -> str:
    head = f'<Style TargetType="{target}"'
    if key:
        head += f' x:Key="{key}"'
    if based_on:
        head += f' BasedOn="{{StaticResource {based_on}}}"'
    body = f"<Style.Setters>{setters}</Style.Setters>" if wrapper else setters
    return f"{head}>{body}</Style>"


def setter(prop: str, value: str) -> str:
    return f'<Setter Property="{prop}" Value="{value}"/>'


# (slug, note, requires, styled markup, inline twin or None, question)
def l5_style_scenarios() -> list[tuple[str, str, list[str], str, str | None, str | None]]:
    text_attrs = 'FontFamily="Segoe UI"'
    columns = ('<Grid.ColumnDefinitions><ColumnDefinition Width="40"/>'
               '<ColumnDefinition Width="*"/></Grid.ColumnDefinitions>')

    return [
        (
            "explicit-key",
            "a Style behind an x:Key, referenced by Style=\"{StaticResource ...}\"",
            ["L1-sizing"],
            l5_document("Grid", l5_resources("Grid", l5_style(
                "Border", setter("Width", "60") + setter("Height", "30"), key="Boxy"))
                + '<Border Style="{StaticResource Boxy}"/>'),
            l5_document("Grid", '<Border Width="60" Height="30"/>'),
            None,
        ),
        (
            "implicit-target-type",
            "a Style with a TargetType and no x:Key reaches every element of "
            "that type in the subtree below the dictionary",
            ["L3-stack"],
            l5_document("StackPanel", l5_resources("StackPanel", l5_style(
                "Border", setter("Width", "60") + setter("Height", "30")))
                + "<Border/><Border/>", attributes='Orientation="Horizontal"'),
            l5_document("StackPanel",
                        '<Border Width="60" Height="30"/><Border Width="60" Height="30"/>',
                        attributes='Orientation="Horizontal"'),
            None,
        ),
        (
            "setters-layout-properties",
            "one style setting five of the properties layout reads: Width, "
            "Height, Margin, Padding and HorizontalAlignment",
            ["L2-align"],
            l5_document("Grid", l5_resources("Grid", l5_style("Border",
                        setter("Width", "80") + setter("Height", "40")
                        + setter("Margin", "4,8,12,16") + setter("Padding", "6")
                        + setter("HorizontalAlignment", "Right"), key="Chrome"))
                + '<Border Style="{StaticResource Chrome}">'
                  '<Border Width="10" Height="10"/></Border>'),
            l5_document("Grid",
                        '<Border Width="80" Height="40" Margin="4,8,12,16" Padding="6"'
                        ' HorizontalAlignment="Right"><Border Width="10" Height="10"/></Border>'),
            None,
        ),
        (
            "local-beats-style",
            "a value written on the element wins over the style's setter for "
            "the same property, and only for that property",
            ["L1-sizing"],
            l5_document("Grid", l5_resources("Grid", l5_style(
                "Border", setter("Width", "60") + setter("Height", "30"), key="Boxy"))
                + '<Border Style="{StaticResource Boxy}" Width="20"/>'),
            l5_document("Grid", '<Border Width="20" Height="30"/>'),
            None,
        ),
        (
            "style-beats-default",
            "a setter beats the registered default, and a property no setter "
            "names keeps it -- Width stays Auto and the Border measures empty",
            ["L1-sizing"],
            l5_document("Grid", l5_resources("Grid", l5_style(
                "Border", setter("Height", "30") + setter("HorizontalAlignment", "Left"),
                key="Short")) + '<Border Style="{StaticResource Short}"/>'),
            l5_document("Grid", '<Border Height="30" HorizontalAlignment="Left"/>'),
            None,
        ),
        (
            "based-on-inherited-setter",
            "a BasedOn style carries its base's setters as well as its own",
            ["L1-sizing"],
            l5_document("Grid", l5_resources("Grid",
                        l5_style("Border", setter("Width", "60"), key="Base")
                        + l5_style("Border", setter("Height", "24"), key="Derived",
                                   based_on="Base"))
                + '<Border Style="{StaticResource Derived}"/>'),
            l5_document("Grid", '<Border Width="60" Height="24"/>'),
            None,
        ),
        (
            "based-on-overridden-setter",
            "a BasedOn style's own setter replaces the base's for the same "
            "property, and leaves the base's other setters alone",
            ["L1-sizing"],
            l5_document("Grid", l5_resources("Grid",
                        l5_style("Border", setter("Width", "60") + setter("Height", "24"),
                                 key="Base")
                        + l5_style("Border", setter("Width", "100"), key="Derived",
                                   based_on="Base"))
                + '<Border Style="{StaticResource Derived}"/>'),
            l5_document("Grid", '<Border Width="100" Height="24"/>'),
            None,
        ),
        (
            "based-on-chain",
            "three styles deep, with an override in the middle and another at "
            "the end",
            ["L1-sizing"],
            l5_document("Grid", l5_resources("Grid",
                        l5_style("Border", setter("Width", "60"), key="A")
                        + l5_style("Border", setter("Height", "24"), key="B", based_on="A")
                        + l5_style("Border", setter("Width", "100") + setter("Margin", "4"),
                                   key="C", based_on="B"))
                + '<Border Style="{StaticResource C}"/>'),
            l5_document("Grid", '<Border Width="100" Height="24" Margin="4"/>'),
            None,
        ),
        (
            "implicit-shadowing",
            "an inner dictionary's implicit style wins for its subtree and "
            "only for its subtree",
            ["L3-stack"],
            l5_document("Grid", l5_resources("Grid", l5_style(
                "Border", setter("Width", "60") + setter("Height", "20")))
                + '<StackPanel HorizontalAlignment="Left">'
                + l5_resources("StackPanel", l5_style(
                    "Border", setter("Width", "100") + setter("Height", "20")))
                + "<Border/></StackPanel>"
                + '<Border HorizontalAlignment="Right" VerticalAlignment="Bottom"/>'),
            l5_document("Grid",
                        '<StackPanel HorizontalAlignment="Left">'
                        '<Border Width="100" Height="20"/></StackPanel>'
                        '<Border Width="60" Height="20" HorizontalAlignment="Right"'
                        ' VerticalAlignment="Bottom"/>'),
            None,
        ),
        (
            "implicit-scope-limit",
            "an implicit style declared on a panel does not reach a sibling of "
            "that panel",
            ["L3-stack"],
            l5_document("Grid", '<StackPanel HorizontalAlignment="Left">'
                        + l5_resources("StackPanel", l5_style(
                            "Border", setter("Width", "100") + setter("Height", "20")))
                        + "<Border/></StackPanel>"
                        + '<Border Width="30" Height="20" HorizontalAlignment="Right"/>'),
            l5_document("Grid",
                        '<StackPanel HorizontalAlignment="Left">'
                        '<Border Width="100" Height="20"/></StackPanel>'
                        '<Border Width="30" Height="20" HorizontalAlignment="Right"/>'),
            None,
        ),
        (
            "explicit-beats-implicit",
            "an element with a Style= is not also given the implicit style for "
            "its type; the two do not merge, and the sibling without one still "
            "gets the implicit style",
            ["L2-align"],
            l5_document("Grid", l5_resources("Grid",
                        l5_style("Border", setter("Width", "60") + setter("Height", "30")
                                 + setter("Margin", "8"))
                        + l5_style("Border", setter("Width", "20"), key="Slim"))
                + '<Border Style="{StaticResource Slim}"/>'
                + '<Border VerticalAlignment="Bottom"/>'),
            l5_document("Grid", '<Border Width="20"/>'
                        '<Border Width="60" Height="30" Margin="8" VerticalAlignment="Bottom"/>'),
            None,
        ),
        (
            "setter-value-element",
            "Setter.Value written as a property element rather than as an "
            "attribute, beside one written as an attribute",
            ["L1-sizing"],
            l5_document("Grid", l5_resources("Grid", l5_style("Border",
                        '<Setter Property="Width"><Setter.Value>60</Setter.Value></Setter>'
                        + setter("Height", "30"), key="Boxy"))
                + '<Border Style="{StaticResource Boxy}"/>'),
            l5_document("Grid", '<Border Width="60" Height="30"/>'),
            None,
        ),
        (
            "setter-value-staticresource",
            "a setter whose value is a {StaticResource}, declared beside the "
            "style in the same dictionary",
            ["L1-sizing"],
            l5_document("Grid", l5_resources("Grid",
                        '<x:Double x:Key="BoxWidth">60</x:Double>'
                        + l5_style("Border",
                                   setter("Width", "{StaticResource BoxWidth}")
                                   + setter("Height", "30"), key="Boxy"))
                + '<Border Style="{StaticResource Boxy}"/>'),
            l5_document("Grid", '<Border Width="60" Height="30"/>'),
            None,
        ),
        (
            "setter-value-resource-element",
            "the same reference written as a <StaticResource> element inside "
            "<Setter.Value>",
            ["L1-sizing"],
            l5_document("Grid", l5_resources("Grid",
                        '<x:Double x:Key="BoxWidth">60</x:Double>'
                        + l5_style("Border",
                                   '<Setter Property="Width"><Setter.Value>'
                                   '<StaticResource ResourceKey="BoxWidth"/>'
                                   "</Setter.Value></Setter>"
                                   + setter("Height", "30"), key="Boxy"))
                + '<Border Style="{StaticResource Boxy}"/>'),
            l5_document("Grid", '<Border Width="60" Height="30"/>'),
            None,
        ),
        (
            "setters-wrapper",
            "the same style written with the optional <Style.Setters> wrapper",
            ["L1-sizing"],
            l5_document("Grid", l5_resources("Grid", l5_style(
                "Border", setter("Width", "60") + setter("Height", "30"), key="Boxy",
                wrapper=True)) + '<Border Style="{StaticResource Boxy}"/>'),
            l5_document("Grid", '<Border Width="60" Height="30"/>'),
            None,
        ),
        (
            "chrome-setters",
            "Padding and BorderThickness from a style, where both deflate the "
            "content rect and the child's offset shows it",
            ["L2-align"],
            l5_document("Grid", l5_resources("Grid", l5_style("Border",
                        setter("Padding", "6") + setter("BorderThickness", "2,4")
                        + setter("HorizontalAlignment", "Left")
                        + setter("VerticalAlignment", "Top"), key="Chrome"))
                + '<Border Style="{StaticResource Chrome}">'
                  '<Border Width="40" Height="20"/></Border>'),
            l5_document("Grid",
                        '<Border Padding="6" BorderThickness="2,4" HorizontalAlignment="Left"'
                        ' VerticalAlignment="Top"><Border Width="40" Height="20"/></Border>'),
            None,
        ),
        (
            "attached-property-setter",
            "a setter for an attached property, whose owner is not the "
            "TargetType",
            ["L3-grid"],
            l5_document("Grid", l5_resources("Grid", l5_style(
                "Border", setter("Grid.Column", "1") + setter("Height", "20"), key="Second"))
                + columns + '<Border Style="{StaticResource Second}"/>'),
            l5_document("Grid", columns + '<Border Grid.Column="1" Height="20"/>'),
            None,
        ),
        (
            "panel-setters",
            "an implicit style on a panel, setting the two properties that "
            "decide how it stacks",
            ["L3-stack"],
            l5_document("Grid", l5_resources("Grid", l5_style(
                "StackPanel", setter("Orientation", "Horizontal") + setter("Spacing", "6")))
                + '<StackPanel><Border Width="20" Height="10"/>'
                  '<Border Width="20" Height="10"/></StackPanel>'),
            l5_document("Grid", '<StackPanel Orientation="Horizontal" Spacing="6">'
                        '<Border Width="20" Height="10"/>'
                        '<Border Width="20" Height="10"/></StackPanel>'),
            None,
        ),
        (
            "fontsize-implicit",
            "a style sets FontSize on a TextBlock, so the value has to reach "
            "text measurement and not just a layout slot",
            ["L4-text"],
            l5_document("Border", l5_resources("Border", l5_style(
                "TextBlock", setter("FontSize", "24")))
                + f'<TextBlock {text_attrs} Text="M"/>'),
            l5_document("Border", f'<TextBlock {text_attrs} FontSize="24" Text="M"/>'),
            None,
        ),
        (
            "fontsize-inherits-from-style",
            "a style sets FontSize on a ContentControl and a TextBlock inside "
            "it, which has no FontSize of its own, measures at that size -- a "
            "style-provided value is inherited exactly as a written one is",
            ["L4-text"],
            l5_document("Grid", l5_resources("Grid", l5_style(
                "ContentControl", setter("FontSize", "24"), key="Big"))
                + '<ContentControl Style="{StaticResource Big}">'
                + f'<TextBlock {text_attrs} Text="M"/></ContentControl>'),
            l5_document("Grid", '<ContentControl FontSize="24">'
                        f'<TextBlock {text_attrs} Text="M"/></ContentControl>'),
            None,
        ),
        (
            "style-beats-inherited",
            "a TextBlock's implicit style beats a FontSize inherited from the "
            "control above it -- the slot the style occupies is above "
            "inheritance and below a local value",
            ["L4-text"],
            l5_document("Grid", l5_resources("Grid", l5_style(
                "TextBlock", setter("FontSize", "10")))
                + '<ContentControl FontSize="22">'
                + f'<TextBlock {text_attrs} Text="M"/></ContentControl>'),
            l5_document("Grid", '<ContentControl FontSize="22">'
                        f'<TextBlock {text_attrs} FontSize="10" Text="M"/></ContentControl>'),
            None,
        ),
        (
            "implicit-two-types",
            "one dictionary declaring implicit styles for two different types",
            ["L4-text"],
            l5_document("StackPanel", l5_resources("StackPanel",
                        l5_style("Border", setter("Width", "60") + setter("Height", "20"))
                        + l5_style("TextBlock", setter("FontSize", "24")))
                + "<Border/>" + f'<TextBlock {text_attrs} Text="M"/>'),
            l5_document("StackPanel", '<Border Width="60" Height="20"/>'
                        f'<TextBlock {text_attrs} FontSize="24" Text="M"/>'),
            None,
        ),

        # --- questions ---------------------------------------------------------
        (
            "implicit-own-dictionary",
            "an element whose own dictionary declares an implicit style for "
            "its own type",
            ["L1-sizing"],
            l5_document("Grid", '<Border HorizontalAlignment="Left">'
                        + l5_resources("Border", l5_style(
                            "Border", setter("Width", "60") + setter("Height", "30")))
                        + "<Border/></Border>"),
            None,
            "The core's implicit lookup walks the tree starting at the element "
            "itself -- ResolveImplicitStyleKeyImpl begins with `current = "
            "element` and only then goes to the parent -- so the outer Border "
            "should be styled by its own dictionary as well as the inner one, "
            "and both should measure 60x30. This implementation agrees, which "
            "makes the implicit route wider than {StaticResource}'s in the same "
            "parser: an element's own dictionary is not in scope for its own "
            "attributes, because those are read when the start tag is. If the "
            "runtime scopes the two the same way, only the inner Border is "
            "styled.",
        ),
        (
            "implicit-forward-dictionary",
            "an implicit style in a dictionary written below the elements it "
            "targets",
            ["L1-sizing"],
            l5_document("Grid", "<Border/>" + l5_resources("Grid", l5_style(
                "Border", setter("Width", "60") + setter("Height", "30")))),
            None,
            "The core applies styles after the object is built -- "
            "CFrameworkElement::ApplyStyle runs at CreationComplete and again "
            "on entering a live tree -- so an implicit style should be found "
            "however late in the document its dictionary is written, and the "
            "Border should measure 60x30. This implementation resolves at the "
            "element's closing tag instead, so it finds nothing and the Border "
            "measures empty. That is the same forward-reference question "
            "L5-resources-forward-reference-child asks about {StaticResource}, "
            "and the two need not have the same answer: one is a parse-time "
            "ambient lookup and the other is a tree walk.",
        ),
        (
            "setter-value-resource-scope",
            "a setter whose value is a {StaticResource} for a key that two "
            "dictionaries declare differently -- the style's, and the styled "
            "element's",
            ["L3-stack"],
            l5_document("Grid", l5_resources("Grid",
                        '<x:Double x:Key="BoxWidth">60</x:Double>'
                        + l5_style("Border",
                                   setter("Width", "{StaticResource BoxWidth}")
                                   + setter("Height", "30"), key="Boxy"))
                        + '<StackPanel HorizontalAlignment="Left">'
                        + l5_resources("StackPanel",
                                       '<x:Double x:Key="BoxWidth">100</x:Double>')
                        + '<Border Style="{StaticResource Boxy}"/></StackPanel>'),
            None,
            "This implementation resolves a setter's value where the style is "
            "written, against the dictionaries in scope there, so the Border "
            "measures 60 wide. The core defers a setter's {StaticResource} "
            "until the value is first needed -- OptimizedStyle::EnsureValue"
            "Realized -- which leaves open whether the deferred lookup still "
            "carries the style's parse context or picks up the styled "
            "element's, in which case it is 100. Terminal's markup never "
            "declares one key twice at two depths, so nothing in the harvest "
            "settles it.",
        ),
        (
            "duplicate-setter",
            "a style that sets the same property twice",
            ["L1-sizing"],
            l5_document("Grid", l5_resources("Grid", l5_style(
                "Border", setter("Width", "60") + setter("Width", "100")
                + setter("Height", "30"), key="Boxy"))
                + '<Border Style="{StaticResource Boxy}"/>'),
            None,
            "CStyle::GetPropertyValue searches a style's setters in reverse "
            "and says why -- \"if a property is duplicated, the last setter "
            "wins\" -- so this should measure 100 wide. This implementation "
            "does the same. What that source does not say is whether WinUI 2's "
            "parser or SetterBaseCollection::ValidateItem rejects the "
            "duplicate before it ever reaches that lookup, which would make "
            "the case a load failure instead.",
        ),
        (
            "implicit-derived-type",
            "an implicit Style whose TargetType is a base class of the element",
            ["L4-text"],
            l5_document("Grid", l5_resources("Grid", l5_style(
                "Control", setter("FontSize", "24")))
                + "<ContentControl>"
                + f'<TextBlock {text_attrs} Text="M"/></ContentControl>'),
            None,
            "Both references key an implicit style by the element's exact "
            "type -- FindImplicitStyleResource(this, this.GetType()) in WPF, "
            "ResolveImplicitStyleKeyImpl(element, element->GetClassName()) in "
            "the core -- with no walk up the base types, so a TargetType of "
            "Control should not reach a ContentControl implicitly and the "
            "TextBlock should measure at the default 14. This implementation "
            "cannot say that: it has no abstract Control type at all, so it "
            "refuses the markup by name at load. Either answer settles it.",
        ),
        (
            "explicit-derived-target",
            "the same Style, but assigned explicitly rather than found "
            "implicitly",
            ["L4-text"],
            l5_document("Grid", l5_resources("Grid", l5_style(
                "Control", setter("FontSize", "24"), key="Big"))
                + '<ContentControl Style="{StaticResource Big}">'
                + f'<TextBlock {text_attrs} Text="M"/></ContentControl>'),
            None,
            "The explicit route validates the target type with an is-a check "
            "rather than an exact match -- CFrameworkElement::ValidateTarget"
            "Type asks OfTypeByIndex, WPF's Style.CheckTargetType asks "
            "IsAssignableFrom -- so this should apply where the implicit twin "
            "of it does not, and the TextBlock should measure at 24. It is "
            "paired with L5-styles-implicit-derived-type on purpose: the "
            "interesting finding is the two disagreeing, and a runtime that "
            "answered both the same way would mean one of the two rules is not "
            "what the source says. This implementation refuses both, for the "
            "same reason: no abstract Control type.",
        ),
    ]


def level5_styles() -> Iterator[dict[str, Any]]:
    for slug, note, requires, markup, inline, question in l5_style_scenarios():
        case_id = f"L5-styles-{slug}"
        twin_id = f"{case_id}-inline" if inline else None
        yield case(case_id, 5, "styles", markup, [400.0, 300.0], note,
                   requires=requires, twin=twin_id, question=question)
        if inline:
            yield case(twin_id, 5, "styles", inline, [400.0, 300.0],
                       f"inline twin of {case_id}: {note}",
                       requires=requires)


# --- L5: the x-namespace primitives as object elements -------------------------
# XAML lets a value be written as an element instead of an attribute:
# <Border.Width><x:Double>60</x:Double></Border.Width> says what Width="60"
# says. Terminal writes eleven of these outside a resource dictionary, all on
# properties whose declared type is `object` -- <ToggleButton.Tag> and
# <DiscreteObjectKeyFrame.Value> -- where an attribute would give the property a
# string and the code reading it back wants an Int32 or a Boolean.
#
# The layout core has no object-typed property to reproduce that on, so the
# cases below put the same primitives on typed layout properties instead. What
# they check is the mechanism -- that a primitive written as an element reaches
# the property, carries its type with it, and is refused when the type is wrong
# -- which is the half of Terminal's usage that is not about Tag.
#
# Every one of these is twinned, because the attribute form of the same value is
# not a guess: it is the same assignment written the way the rest of the corpus
# writes it, and the two have to measure identically. The one exception is
# int32-width: run 31019336758 recorded the runtime refusing the typed object
# (0x802b000a) while the attribute spelling loads, so the two provably do not
# describe the same tree. Its former twin stays in the corpus as a plain case
# -- the measurement digest pins the case count -- but nothing links them.

def x_primitive_scenarios() -> list[tuple[str, str, list[str], str, str | None, str | None]]:
    text_attrs = 'FontFamily="Segoe UI" FontSize="14"'

    def wrap(inner: str) -> str:
        return l5_document("Border", inner)

    return [
        (
            "double-width",
            "an x:Double written as the value of <Border.Width>",
            ["L1-sizing"],
            wrap('<Border Height="30"><Border.Width><x:Double>60</x:Double>'
                 "</Border.Width></Border>"),
            wrap('<Border Height="30" Width="60"/>'),
            None,
        ),
        (
            "int32-width",
            "an x:Int32 on a property whose type is a Double, which is the "
            "widening XAML is usually said to perform on the way in",
            ["L1-sizing"],
            wrap('<Border Height="30"><Border.Width><x:Int32>60</x:Int32>'
                 "</Border.Width></Border>"),
            None,
            "Whether the widening happens at all. An attribute's value is a "
            "string that Double's converter reads; an object element produces "
            "an Int32 *object*, which is assigned rather than converted, so a "
            "Double-typed Width may simply refuse it. Run 31019336758 -- the "
            "first with per-case error hygiene, so its messages are its own -- "
            "recorded the refusal: 0x802b000a. The layout core refuses the "
            "assignment the same way, so the question is answered; the marker "
            "stays because a refusal is still this case's expected outcome, "
            "and the report should keep reading it as the answer rather than "
            "as an authored level failing.",
        ),
        (
            "int32-width-inline",
            "the attribute spelling of the assignment x:Int32 is refused for: "
            "a string through Double's converter, kept as the measured witness "
            "that only the typed object form is refused. Formerly the twin of "
            "L5-xprimitives-int32-width; unlinked because the runtime answers "
            "the two differently, which is the finding",
            ["L1-sizing"],
            wrap('<Border Height="30" Width="60"/>'),
            None,
            None,
        ),
        (
            "double-indented",
            "the same value written across lines, because a dictionary indents "
            "its entries and XAML trims what the indentation adds",
            ["L1-sizing"],
            wrap('<Border Height="30"><Border.Width><x:Double>\n      60\n    '
                 "</x:Double></Border.Width></Border>"),
            wrap('<Border Height="30" Width="60"/>'),
            None,
        ),
        (
            "double-minmax",
            "two primitives on two properties of one element, where both clamps "
            "bite so a value landing on the wrong property is visible",
            ["L1-sizing"],
            wrap('<Border Width="20" Height="90">'
                 "<Border.MinWidth><x:Double>80</x:Double></Border.MinWidth>"
                 "<Border.MaxHeight><x:Double>40</x:Double></Border.MaxHeight>"
                 "</Border>"),
            wrap('<Border Width="20" Height="90" MinWidth="80" MaxHeight="40"/>'),
            None,
        ),
        (
            "double-fontsize",
            "an x:Double on TextBlock.FontSize, so the value has to reach text "
            "measurement rather than only a layout slot",
            ["L4-text"],
            wrap('<TextBlock FontFamily="Segoe UI" Text="M">'
                 "<TextBlock.FontSize><x:Double>24</x:Double></TextBlock.FontSize>"
                 "</TextBlock>"),
            wrap('<TextBlock FontFamily="Segoe UI" FontSize="24" Text="M"/>'),
            None,
        ),
        (
            "string-text",
            "an x:String on TextBlock.Text, which is the primitive Terminal "
            "puts in its dictionaries most often",
            ["L4-text"],
            wrap(f"<TextBlock {text_attrs}><TextBlock.Text><x:String>M</x:String>"
                 "</TextBlock.Text></TextBlock>"),
            wrap(f'<TextBlock {text_attrs} Text="M"/>'),
            None,
        ),
        (
            "double-spacing",
            "an x:Double on StackPanel.Spacing, where three children make a "
            "wrong value show up as a wrong height twice over",
            ["L3-stack"],
            l5_document("StackPanel",
                        "<StackPanel.Spacing><x:Double>6</x:Double></StackPanel.Spacing>"
                        '<Border Width="30" Height="18"/>'
                        '<Border Width="30" Height="18"/>'
                        '<Border Width="30" Height="18"/>'),
            l5_document("StackPanel",
                        '<Border Width="30" Height="18"/>'
                        '<Border Width="30" Height="18"/>'
                        '<Border Width="30" Height="18"/>',
                        attributes='Spacing="6"'),
            None,
        ),
        (
            "boolean-rounding",
            "an x:Boolean on UseLayoutRounding, at a fractional size so the "
            "property has somewhere to show",
            ["L0-props"],
            wrap('<Border Width="60.5" Height="30.5">'
                 "<Border.UseLayoutRounding><x:Boolean>False</x:Boolean>"
                 "</Border.UseLayoutRounding></Border>"),
            wrap('<Border Width="60.5" Height="30.5" UseLayoutRounding="False"/>'),
            None,
        ),
        (
            "string-into-enum",
            "an x:String on a property whose type is an enumeration",
            ["L2-align"],
            wrap('<Border Width="20" Height="20">'
                 "<Border.HorizontalAlignment><x:String>Left</x:String>"
                 "</Border.HorizontalAlignment></Border>"),
            None,
            "An attribute's value is a string that the enum's converter reads. "
            "An x:String is a String *object*, which is assigned rather than "
            "converted, so the conversion may not happen at all. Terminal never "
            "writes one, and the two are indistinguishable in every case that "
            "uses the attribute form. This implementation accepts it, because "
            "it has no declared shape for an enum property and so does not "
            "refuse anything; if the runtime rejects it, the permissiveness is "
            "the bug.",
        ),
    ]


def level5_x_primitives() -> Iterator[dict[str, Any]]:
    for slug, note, requires, markup, inline, question in x_primitive_scenarios():
        case_id = f"L5-xprimitives-{slug}"
        twin_id = f"{case_id}-inline" if inline else None
        yield case(case_id, 5, "xprimitives", markup, [400.0, 300.0], note,
                   requires=requires, twin=twin_id, question=question)
        if inline:
            yield case(twin_id, 5, "xprimitives", inline, [400.0, 300.0],
                       f"inline twin of {case_id}: {note}",
                       requires=requires)


# --- L5: x:Load, x:DeferLoadStrategy and x:Uid ---------------------------------
# Every case here carries a question, and none of them has a twin.
#
# The reason is the same in both halves of the group. x:Load and
# x:DeferLoadStrategy are realised by the XAML *compiler*: it emits a stub that a
# code-behind fills in later, by name. A XamlReader.Load has no compiler and no
# code-behind, so what it does with the directive is not a thing the
# documentation covers -- it may honour it, ignore it, or refuse the markup, and
# those three produce three different trees. Writing an inline twin for one of
# them would be choosing the answer and then checking that we chose it.
#
# x:Uid is the same shape of unknown for a different reason: it resolves against
# a resource map, and a standalone load has none. Whether "no map" means "sets
# nothing" or "refuses the markup" is what the case asks.
#
# What this implementation does in the meantime is written down in
# phase3/layout/tests/xdirectives_test.cpp rather than left to be inferred: an
# x:Load="False" element is absent, and an x:Uid that resolves to nothing sets
# nothing.

def x_directive_scenarios() -> list[tuple[str, str, list[str], str, str]]:
    sized = 'Width="30" Height="18"'

    return [
        (
            "load-false-sibling",
            "a panel with two children, one of which defers its own creation",
            ["L3-stack"],
            l5_document("StackPanel",
                        f"<Border {sized}/>"
                        f'<Border x:Load="False" {sized}/>'
                        f"<Border {sized}/>"),
            "Three things the runtime might do, and they are three different "
            "trees: leave the element out entirely (the panel is 36 tall), "
            "realise it as an ordinary child (54), or realise it collapsed (36, "
            "but with a node in the tree at zero size). x:Load is a compiled-"
            "markup feature and XamlReader.Load has no compiler, so a fourth "
            "answer -- refusing the markup -- is also live. This implementation "
            "leaves it out.",
        ),
        (
            "load-false-only-child",
            "the single child of a Border defers its own creation, so the "
            "parent has nothing to size to",
            ["L1-sizing"],
            l5_document("Border", f'<Border x:Load="False" {sized}/>'),
            "Whether the outer Border measures as empty or as 30x18 says the "
            "same thing the sibling case asks, with the answer visible on the "
            "parent instead of on a stacking offset -- and a Border with a "
            "deferred child is a Border that must not report its child's size. "
            "This implementation measures it as empty.",
        ),
        (
            "load-true",
            "x:Load=\"True\", which asks for nothing but still has to be "
            "understood",
            ["L1-sizing"],
            l5_document("Border", f'<Border x:Load="True" {sized}/>'),
            "The directive's own no-op case. If XamlReader.Load refuses this "
            "one, it refuses the directive rather than the deferral, which "
            "separates two answers the False cases would confound. This "
            "implementation realises the element normally.",
        ),
        (
            "defer-load-strategy-lazy",
            "the older spelling of the same instruction, which Terminal still "
            "uses once",
            ["L3-stack"],
            l5_document("StackPanel",
                        f"<Border {sized}/>"
                        f'<Border x:DeferLoadStrategy="Lazy" {sized}/>'),
            "x:DeferLoadStrategy predates x:Load and is documented as the same "
            "deferral. Whether a runtime load treats them identically is not "
            "recorded anywhere, and Terminal writes both, so the corpus asks "
            "both. This implementation treats them as one instruction.",
        ),
        (
            "uid-unresolved",
            "an x:Uid with no resource map to resolve against, which is every "
            "standalone load. The uid is one Terminal really declares, so a run "
            "that *does* supply a table refuses this case by name -- "
            "Profile_Name sets a Header, and no type here has one",
            ["L1-sizing"],
            l5_document("Border", f'<Border x:Uid="Profile_Name" {sized}/>'),
            "A uid is a key into a localised map that a XamlReader.Load does "
            "not have. If the load succeeds and measures 30x18, an unresolved "
            "uid sets nothing and the directive is safe to carry -- which is "
            "what the harvester would need before it could stop calling x:Uid a "
            "blocker on 399 attributes. If it refuses, the directive is a "
            "dependency on the map itself. This implementation sets nothing.",
        ),
        (
            "uid-precedence",
            "an x:Uid on an element that also writes the property the uid "
            "would set",
            ["L1-sizing"],
            l5_document("Border", '<Border x:Uid="Profile_Name" Width="30" Height="18"/>'),
            "The precedence question, asked in the only form a standalone load "
            "can ask it: with no map, the local Width is all there is, so a "
            "measurement of 30x18 says the uid did not clear it. The question "
            "this cannot answer is which wins when the map *does* have an entry "
            "-- there is no way to put one in front of the oracle from markup "
            "alone. This implementation writes the uid's value over the local "
            "attribute, on the reading that a directive whose purpose is "
            "translation must beat the author it is translating; that choice is "
            "pinned in xdirectives_test.cpp and is a guess.",
        ),
    ]


def level5_x_directives() -> Iterator[dict[str, Any]]:
    for slug, note, requires, markup, question in x_directive_scenarios():
        yield case(f"L5-xdirectives-{slug}", 5, "xdirectives", markup, [400.0, 300.0],
                   note, requires=requires, question=question)


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
    3: [level3, level3_canvas, level3_scroll],
    4: [level4, level4_icon, level4_source],
    5: [level5, level5_styles, level5_theme, level5_x_primitives, level5_x_directives],
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
