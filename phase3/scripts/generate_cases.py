#!/usr/bin/env python3
"""Generate the mechanical levels of the XAML behaviour database.

Levels 0 to 4 are a cross product, not a collection of hand-written examples:
each case varies one axis of the layout contract against a fixed environment,
so a mismatch names the primitive that is wrong.

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
) -> dict[str, Any]:
    return {
        "schema_version": SCHEMA_VERSION,
        "id": case_id,
        "level": level,
        "group": group,
        "note": note,
        "requires": requires or [],
        "markup": markup,
        "environment": dict(BASE_ENV, available_size=encode_size(available)),
    }


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


LEVELS = {
    0: [level0],
    1: [level1, level1_shape],
    2: [level2, level2_content],
    3: [level3, level3_canvas],
    4: [level4],
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
