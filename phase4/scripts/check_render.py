#!/usr/bin/env python3
"""Recover what was painted back out of the pixels, and hold it to the layout.

This remains the renderer's independent self-consistency gate. The focused
native oracle is compared separately by check_render_oracle.py; it does not
replace this check, because reproducing one native image would not prove the
display list honestly follows layout. What there *is* here is a
measurement of every rectangle's geometry -- the corpus verifies the arranged
tree against 1176 of 1177 recorded answers -- and a render pass that claims to
paint exactly those rectangles and nothing else.

So this checks the claim rather than the picture, three ways:

  1. **The geometry is the measurement path's.** The sidecar's per-node slot and
     render size are diffed against the tree `render_cases` writes in the shape
     `measure_cases` writes it, node for node. Then each node's *parent-local
     visual origin* is re-derived here -- from the measured slot, the render
     size and the declared margin and alignments -- rather than believed, and
     the absolute origin the render pass accumulated is re-accumulated from the
     re-derived chain, so a wrong sum is caught rather than trusted.

     The local origin is not the slot origin. `FrameworkElement::ArrangeCore`
     positions the arranged ink inside the margin-reduced slot according to the
     alignments, so a 120-wide `Border` in a 400-wide `Stretch` slot sits at
     x=140 with its slot origin still 0. A checker that accumulated slot origins
     would be asking the render pass for a number the layout core stopped
     producing; this one implements `AlignmentOffset` and `RoundLayoutValue`
     from phase3/layout/src/layout.h against the sidecar's declared inputs
     instead, which is an independent route to the same answer.
  2. **The pixels are re-derived independently.** Every expected rectangle is
     re-snapped here, in Python, from the geometry table -- not from the render
     pass's own rect list -- painted into a framebuffer with the same
     round-half-up edge rule the layout core rounds sizes with, and compared to
     the dump byte for byte. Exactly: no tolerance. Solid axis-aligned rects at
     whole-pixel edges are exactly invertible, which is the entire reason the
     render pass is restricted to them. An off-by-one or a stray anti-aliased
     pixel is a bug in the render pass, not a window to widen.
  3. **The rectangles are extracted back out.** Each dump is cut into maximal
     runs of one colour and the runs are coalesced into rectangles; every
     recovered rectangle has to be one the layout asked for, or a piece of one
     that a later rectangle painted over. A painted shape that was not a clean
     rectangle reassembles as several and fails.

Text is checked differently, and less in this self-consistency pass. Native
glyph pixels and DirectWrite runs are checked by check_render_oracle.py; here,
what belongs to this project is where the run starts and how wide the
measurement path says it is. So the ink check is containment: every pixel that
is not what the solid pass would have left must fall inside a text run's box,
and a non-empty run must have ink in it. Containment on the right edge is the
part that carries weight -- it holds the advances the corpus verifies to
actually covering the glyphs the platform drew.

The box read from a run's `pixels` is every pixel its measured rectangle
overlaps, not the rectangle snapped the way a fill is; the two differ by up to
half a pixel on each edge and only the first is a fair question to ask of ink.
See `TouchedRect` in phase3/render/src/surface.h.

A dump written without a glyph backend (the native, Wine-free run) has no ink to
check. That is reported by name as unchecked rather than counted as a pass.

Sidecars carry a `schema_version`, and this refuses one older than
`REQUIRED_SIDECAR_SCHEMA` by name instead of checking what it can. A dump set
written before the visual-origin column existed cannot have its origins
re-derived at all, and reporting the half that still works as a pass is how a
stale dump root stayed green across two waves.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path
from typing import Any, Iterable


# The sidecar shape this checker knows how to read. Bumped when a column it
# depends on is added, so that a dump root written by an older render pass is
# refused by name instead of half-checked. Kept beside the writer in
# phase3/render/src/case_runner.cpp.
REQUIRED_SIDECAR_SCHEMA = 2

# Not a case sidecar; see phase3/scripts/render_provenance.py. Spelled out here
# rather than imported so this checker keeps running from a bare phase4.
PROVENANCE_NAME = "provenance.json"


class DumpError(Exception):
    """A dump that cannot be read at all, which is never a passing case."""


def read_ppm(path: Path) -> tuple[int, int, bytes]:
    """Reads the binary P6 the render pass writes.

    Deliberately not Pillow: the point of the dump format is that it needs no
    third-party module and no compressor, so two runs are byte-identical and
    this checker runs anywhere the corpus does.
    """
    blob = path.read_bytes()
    if not blob.startswith(b"P6\n"):
        raise DumpError(f"{path.name} is not the P6 the render pass writes")
    fields: list[bytes] = []
    index = 3
    while len(fields) < 3:
        while index < len(blob) and blob[index : index + 1].isspace():
            index += 1
        start = index
        while index < len(blob) and not blob[index : index + 1].isspace():
            index += 1
        if start == index:
            raise DumpError(f"{path.name} has a truncated header")
        fields.append(blob[start:index])
    index += 1  # the single whitespace byte that ends the header
    width, height, maxval = (int(f) for f in fields)
    if maxval != 255:
        raise DumpError(f"{path.name} is not 8 bits per channel")
    pixels = blob[index:]
    if len(pixels) != width * height * 3:
        raise DumpError(
            f"{path.name} holds {len(pixels)} bytes of pixels, not {width * height * 3}"
        )
    return width, height, pixels


def parse_color(text: str) -> bytes:
    """#aarrggbb as the sidecar writes it, reduced to the three bytes a PPM holds."""
    return bytes((int(text[3:5], 16), int(text[5:7], 16), int(text[7:9], 16)))


def parse_argb(text: str) -> tuple[int, bytes]:
    """#aarrggbb kept whole: (alpha, rgb). A brush's alpha is not decoration.

    Terminal's own markup fills with translucent theme brushes -- a 6%-black
    divider, for one -- so a checker that dropped the alpha byte would demand
    solid black where the runtime blended, and be wrong by exactly the amount
    the brush asked for.
    """
    return (int(text[1:3], 16), parse_color(text))


def multiply_byte(value: int, scale: int) -> int:
    """MultiplyByte, ported from phase3/render/src/surface.h."""
    return (value * scale + 127) // 255


def source_over_opaque(alpha: int, source: bytes, destination: bytes) -> bytes:
    """Premultiplied source-over onto an opaque destination.

    `Surface::BlendRect` composites `SourceOver(Pack(colour), destination)`, and
    every dump starts from the opaque backdrop and never stops being opaque --
    the alpha channel of an opaque destination comes back 255 for any source --
    so a premultiplied destination with alpha 255 is its own straight colour and
    the three channels below are the whole of it. Integer arithmetic throughout,
    the same rounding, so this is equality and not a tolerance. See
    phase3/render/src/surface.cpp.
    """
    if alpha == 0:
        return destination
    if alpha == 0xFF:
        return source
    inverse = 0xFF - alpha
    return bytes(
        min(multiply_byte(source[i], alpha) + multiply_byte(destination[i], inverse), 255)
        for i in range(3)
    )


def round_half_up(value: float) -> int:
    """The layout core's own tie-break -- floor(v + 0.5); see layout.h."""
    return int(math.floor(value + 0.5))


def snap(x: float, y: float, w: float, h: float) -> tuple[int, int, int, int]:
    return (round_half_up(x), round_half_up(y), round_half_up(x + w), round_half_up(y + h))


def are_close(a: float, b: float) -> bool:
    """DoubleUtil::AreClose, ported from phase3/layout/src/layout.h."""
    if a == b:
        return True
    epsilon = (abs(a) + abs(b) + 10.0) * 2.2204460492503131e-16
    return -epsilon < a - b < epsilon


def round_layout_value(value: float, dpi_scale: float) -> float:
    """RoundLayoutValue, ported from phase3/layout/src/layout.h."""
    if not are_close(dpi_scale, 1.0):
        scaled = math.floor(value * dpi_scale + 0.5) / dpi_scale
        if math.isnan(scaled) or math.isinf(scaled):
            return value
        return scaled
    return math.floor(value + 0.5)


def alignment_offset(alignment: str, client: float, ink: float) -> float:
    """AlignmentOffset, ported from phase3/layout/src/layout.h.

    One function for both axes: the two differ only in the names of the near
    and far ends, and this is the arithmetic they share.
    """
    if alignment == "Stretch" and ink > client:
        return 0.0  # degenerates to Left/Top rather than hiding both edges
    if alignment in ("Center", "Stretch"):
        return (client - ink) / 2.0
    if alignment in ("Right", "Bottom"):
        return client - ink
    if alignment in ("Left", "Top"):
        return 0.0
    raise DumpError(f"unknown alignment {alignment!r} in the sidecar")


def derived_local_origin(node: dict[str, Any]) -> tuple[float, float] | None:
    """Re-derives one node's parent-local visual origin from the layout inputs.

    This is `FrameworkElement::ArrangeCore`'s placement step, and nothing here
    reads the origin the render pass wrote. Returns None for a node the rule
    does not cover -- an element with no layout storage never went through
    Arrange at all, and its visual comes from one of the two exceptions in
    `GeometryOf` (phase3/render/src/display_list.cpp) instead: the Canvas one,
    and the measured root with an explicit extent, whose size the runtime
    answers out of the specified size rather than out of Arrange. Those are
    counted and named in the report rather than quietly folded into the passes.
    """
    if not node.get("layout_storage", False):
        return None
    slot = node["slot"]
    if not node.get("visible", True):
        # A collapsed element records its slot and stops; no margin, no
        # alignment. Element::Arrange returns there.
        return (slot[0], slot[1])
    actual = node["actual"]
    margin = node["margin"]
    dpi_x, dpi_y = node["dpi_scale"]
    rounding = node["layout_rounding"]

    margin_width = margin[0] + margin[2]
    margin_height = margin[1] + margin[3]
    if rounding:
        margin_width = round_layout_value(margin_width, dpi_x)
        margin_height = round_layout_value(margin_height, dpi_y)
    client_width = max(0.0, slot[2] - margin_width)
    client_height = max(0.0, slot[3] - margin_height)
    if rounding:
        client_width = round_layout_value(client_width, dpi_x)
        client_height = round_layout_value(client_height, dpi_y)

    x = slot[0] + margin[0] + alignment_offset(node["h_align"], client_width, actual[0])
    y = slot[1] + margin[1] + alignment_offset(node["v_align"], client_height, actual[1])
    if rounding:
        x = round_layout_value(x, dpi_x)
        y = round_layout_value(y, dpi_y)
    return (x, y)


def absolute_origins(geometry: list[dict[str, Any]]) -> dict[str, tuple[float, float]]:
    """Re-accumulates every node's absolute origin from the parent-local ones.

    The sidecar reports an absolute origin of its own; this ignores it and adds
    the chain up again, so a render pass that accumulated wrongly is caught
    rather than believed. The step added at each node is the local origin the
    caller re-derived, not the one the sidecar carries -- see
    `derived_local_origin` -- except where the rule does not cover the node, and
    there the sidecar's own column is used and the node is reported as
    not re-derived.
    """
    origins: dict[str, tuple[float, float]] = {}
    for node in geometry:
        path = node["path"]
        parent = path.rsplit("/", 1)[0]
        px, py = origins.get(parent, (0.0, 0.0))
        local = derived_local_origin(node)
        if local is None:
            local = tuple(node["origin"])  # type: ignore[assignment]
        origins[path] = (px + local[0], py + local[1])
    return origins


def row_runs(row: bytes, width: int) -> list[tuple[int, int, bytes]]:
    """Cuts one row into maximal runs of a single colour.

    The uniform-row fast path is not an optimisation detail: nearly every row of
    nearly every dump is one colour, and a per-pixel Python loop over the whole
    corpus would take hours. The doubling search below keeps the general case at
    C speed too.
    """
    # A zero-width surface has no pixels and therefore no runs. Cases arranged
    # at an available size of [0, 0] really do dump one, and an empty slice
    # would otherwise read back as a run of the empty colour.
    if width == 0:
        return []
    first = row[0:3]
    if row == first * width:
        return [(0, width, first)]
    runs: list[tuple[int, int, bytes]] = []
    start = 0
    while start < width:
        colour = row[start * 3 : start * 3 + 3]
        span = 1
        while start + span * 2 <= width and row[start * 3 : (start + span * 2) * 3] == colour * (
            span * 2
        ):
            span *= 2
        step = span
        while step > 1:
            step //= 2
            if start + span + step <= width and row[
                (start + span) * 3 : (start + span + step) * 3
            ] == colour * step:
                span += step
        runs.append((start, start + span, colour))
        start += span
    return runs


def extract_rects(
    width: int, height: int, pixels: bytes, backdrop: bytes
) -> list[tuple[int, int, int, int, bytes]]:
    """Recovers every maximal solid rectangle of one colour from the pixels.

    Row-run coalescing: a run is merged with the run directly above it when the
    two share both edges and the colour. That reconstructs an axis-aligned
    rectangle exactly and -- the useful half -- reconstructs anything that is not
    one as several, so a painted shape that was not a clean rectangle cannot be
    mistaken for one.

    Returns (left, top, right, bottom, rgb), right and bottom exclusive.
    """
    closed: list[tuple[int, int, int, int, bytes]] = []
    open_runs: dict[int, tuple[int, int, bytes]] = {}  # left -> (top, right, rgb)

    for y in range(height):
        base = y * width * 3
        current: dict[int, tuple[int, bytes]] = {}
        for left, right, colour in row_runs(pixels[base : base + width * 3], width):
            if colour == backdrop:
                continue
            current[left] = (right, colour)

        still_open: dict[int, tuple[int, int, bytes]] = {}
        for left, (right, colour) in current.items():
            previous = open_runs.get(left)
            if previous is not None and previous[1] == right and previous[2] == colour:
                still_open[left] = previous
            else:
                if previous is not None:
                    closed.append((left, previous[0], previous[1], y, previous[2]))
                still_open[left] = (y, right, colour)
        for left, previous in open_runs.items():
            if left not in current:
                closed.append((left, previous[0], previous[1], y, previous[2]))
        open_runs = still_open

    for left, previous in open_runs.items():
        closed.append((left, previous[0], previous[1], height, previous[2]))
    return closed


def paint_expected(
    width: int,
    height: int,
    backdrop: bytes,
    rects: list[tuple[tuple[int, int, int, int], tuple[int, bytes]]],
) -> bytearray:
    """Re-rasterises the expected rectangles, in paint order, in Python.

    Opaque rects are a span store, exactly as `Surface::FillRect` is; a
    translucent one composites per pixel, exactly as `Surface::BlendRect` does.
    """
    frame = bytearray(backdrop * (width * height))
    for (left, top, right, bottom), (alpha, colour) in rects:
        left = max(left, 0)
        top = max(top, 0)
        right = min(right, width)
        bottom = min(bottom, height)
        if right <= left or bottom <= top or alpha == 0:
            continue
        if alpha == 0xFF:
            span = colour * (right - left)
            for y in range(top, bottom):
                base = (y * width + left) * 3
                frame[base : base + (right - left) * 3] = span
            continue
        # Every pixel under the rect can be a different destination colour, so
        # the blend is per pixel -- but a row of one colour blends to a row of
        # one colour, so the common case still writes a span.
        for y in range(top, bottom):
            base = (y * width + left) * 3
            row = bytes(frame[base : base + (right - left) * 3])
            blended = bytearray()
            index = 0
            while index < len(row):
                destination = row[index : index + 3]
                run = 3
                while index + run < len(row) and row[index + run : index + run + 3] == destination:
                    run += 3
                blended += source_over_opaque(alpha, colour, destination) * (run // 3)
                index += run
            frame[base : base + len(row)] = blended
    return frame


def blank(frame: bytearray, width: int, boxes: list[tuple[int, int, int, int]]) -> None:
    """Erases the text boxes, so a comparison sees only what the solid pass owns."""
    for left, top, right, bottom in boxes:
        left = max(left, 0)
        top = max(top, 0)
        right = min(right, width)
        bottom = min(bottom, len(frame) // (width * 3))
        if right <= left or bottom <= top:
            continue
        span = b"\x00" * ((right - left) * 3)
        for y in range(top, bottom):
            base = (y * width + left) * 3
            frame[base : base + (right - left) * 3] = span


def first_difference(a: bytes, b: bytes, width: int) -> tuple[int, int] | None:
    for index in range(0, len(a), 3):
        if a[index : index + 3] != b[index : index + 3]:
            pixel = index // 3
            return (pixel % width, pixel // width)
    return None


def check_case(
    sidecar: dict[str, Any], dump: Path, tree: dict[str, Any] | None
) -> tuple[list[str], bool, int]:
    """Returns (problems, ink_was_checked, origins_not_re_derived).

    No problems means the round trip is exact.
    """
    problems: list[str] = []
    width, height, pixels = read_ppm(dump)
    if [width, height] != sidecar["surface"]:
        return (
            [f"the dump is {width}x{height}, the sidecar says {sidecar['surface']}"],
            False,
            0,
        )

    backdrop = parse_color(sidecar["backdrop"])
    ink = parse_color(sidecar["probe_ink"])

    # 1. The geometry the render pass used is the measurement path's own.
    if tree is not None:
        recorded = {node["path"]: node for node in tree["tree"]}
        for node in sidecar["geometry"]:
            counterpart = recorded.get(node["path"])
            if counterpart is None:
                problems.append(f"{node['path']}: the measured tree has no such node")
                continue
            if [round(v, 4) for v in node["actual"]] != [
                round(v, 4) for v in counterpart["actual"]
            ]:
                problems.append(
                    f"{node['path']}: painted at {node['actual']}, measured "
                    f"{counterpart['actual']}"
                )
            if [round(node["slot"][0], 4), round(node["slot"][1], 4)] != [
                round(v, 4) for v in counterpart["offset"]
            ]:
                problems.append(
                    f"{node['path']}: slot origin {node['slot'][:2]}, measured offset "
                    f"{counterpart['offset']}"
                )
        if len(recorded) != len(sidecar["geometry"]):
            problems.append(
                f"the render walk saw {len(sidecar['geometry'])} nodes and the measurement "
                f"walk saw {len(recorded)}"
            )

    # 2. Every local origin is the alignment rule applied to the measured slot,
    #    and every absolute origin follows from that chain.
    not_re_derived = 0
    for node in sidecar["geometry"]:
        local = derived_local_origin(node)
        if local is None:
            not_re_derived += 1
            continue
        if [round(v, 4) for v in node["origin"]] != [round(v, 4) for v in local]:
            problems.append(
                f"{node['path']}: visual origin {node['origin']} is not the "
                f"{node['h_align']}/{node['v_align']} placement of a "
                f"{node['actual']} ink in slot {node['slot']} with margin "
                f"{node['margin']}, which is {list(local)}"
            )
    origins = absolute_origins(sidecar["geometry"])
    by_path = {node["path"]: node for node in sidecar["geometry"]}
    for node in sidecar["geometry"]:
        expected = origins[node["path"]]
        if [round(v, 4) for v in node["abs"]] != [round(v, 4) for v in expected]:
            problems.append(
                f"{node['path']}: absolute origin {node['abs']} is not the accumulated "
                f"{list(expected)}"
            )

    # 3. What the layout says to paint, re-derived here rather than copied.
    expected: list[tuple[tuple[int, int, int, int], tuple[int, bytes]]] = []
    for op in sidecar["rects"]:
        node = by_path.get(op["path"])
        if node is None:
            problems.append(f"{op['path']}: a rect for a node the geometry table has not got")
            continue
        origin = origins[node["path"]]
        bounds = op["bounds"]
        if op["what"] == "background":
            # The background is the arranged element exactly, so it is derived
            # from the recorded columns and never read off the op.
            box = snap(origin[0], origin[1], node["actual"][0], node["actual"][1])
        else:
            box = snap(*bounds)
            inside = (
                origin[0] <= bounds[0] + 1e-9
                and origin[1] <= bounds[1] + 1e-9
                and bounds[0] + bounds[2] <= origin[0] + node["actual"][0] + 1e-9
                and bounds[1] + bounds[3] <= origin[1] + node["actual"][1] + 1e-9
            )
            if not inside:
                problems.append(f"{op['path']} {op['what']}: not inside the arranged element")
        expected.append((box, parse_argb(op["color"])))

    text_boxes = [tuple(op["pixels"]) for op in sidecar["texts"]]

    # 4. Re-rasterise and compare, byte for byte, outside the text boxes.
    frame = paint_expected(width, height, backdrop, expected)
    composited = bytes(frame)
    actual = bytearray(pixels)
    if text_boxes:
        blank(frame, width, text_boxes)
        blank(actual, width, text_boxes)
    if bytes(frame) != bytes(actual):
        where = first_difference(bytes(frame), bytes(actual), width)
        problems.append(
            f"the painted surface is not the surface the layout rectangles re-derive; "
            f"first difference at {where}"
        )

    # 5. Extract the rectangles back out of the pixels.
    #
    # The colour a recovered rectangle has to match is the one the
    # re-rasterisation put there, not the brush's own: a translucent brush
    # leaves the composite of itself and whatever it covered, and demanding the
    # brush colour would reject the very pixels step 4 just proved correct.
    for left, top, right, bottom, colour in extract_rects(width, height, pixels, backdrop):
        box = (left, top, right, bottom)
        if any(
            left >= b[0] and top >= b[1] and right <= b[2] and bottom <= b[3] for b in text_boxes
        ):
            continue  # inside a run's box: ink, checked below rather than here
        at = (top * width + left) * 3
        derived = composited[at : at + 3]
        if any(box == other for other, _ in expected) and colour == derived:
            continue
        if colour == derived and any(
            left >= other[0] and top >= other[1] and right <= other[2] and bottom <= other[3]
            for other, _ in expected
        ):
            continue  # a piece of an expected rect that a later one painted over
        problems.append(
            f"a {right - left}x{bottom - top} rectangle at ({left},{top}) in "
            f"#{colour.hex()} was recovered and no layout rectangle asks for it"
        )

    # 6. Text ink: contained, and present.
    ink_checked = False
    if text_boxes:
        drew_ink = any(
            pixels[i : i + 3] == ink for i in range(0, len(pixels), 3 * max(1, width // 64))
        )
        # A dump from a run with no glyph backend has no ink at all; say so
        # rather than passing a check that was never made.
        if drew_ink or _has_ink(pixels, ink):
            ink_checked = True
            for op, box in zip(sidecar["texts"], text_boxes):
                if not op["text"].strip():
                    continue
                if not _box_has_ink(pixels, width, box, ink):
                    problems.append(
                        f"{op['path']}: the run \"{op['text'][:24]}\" painted no ink inside "
                        f"its measured box"
                    )
            # Step 4 already proved no ink escaped a box: every pixel outside
            # every box matched the solid re-rasterisation exactly.
    return (problems, ink_checked, not_re_derived)


def _has_ink(pixels: bytes, ink: bytes) -> bool:
    index = pixels.find(ink)
    while index != -1:
        if index % 3 == 0:
            return True
        index = pixels.find(ink, index + 1)
    return False


def _box_has_ink(pixels: bytes, width: int, box: tuple[int, int, int, int], ink: bytes) -> bool:
    left, top, right, bottom = box
    for y in range(max(top, 0), bottom):
        base = (y * width + max(left, 0)) * 3
        row = pixels[base : base + (right - max(left, 0)) * 3]
        index = row.find(ink)
        while index != -1:
            if index % 3 == 0:
                return True
            index = row.find(ink, index + 1)
    return False


def iter_cases(dumps: Path) -> Iterable[Path]:
    # The provenance record lives in the dump directory so that it cannot
    # outlive the dumps it describes; it is not a case sidecar. See
    # phase3/scripts/render_provenance.py.
    return sorted(p for p in dumps.glob("*.json") if p.name != PROVENANCE_NAME)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--dumps", required=True, type=Path, help="a render_cases output directory"
    )
    parser.add_argument(
        "--trees",
        type=Path,
        help="the measurement-path trees beside them; defaults to <dumps>/trees",
    )
    parser.add_argument("--output", type=Path, help="where to write the machine-readable report")
    parser.add_argument("--summary", type=Path, help="a markdown table to append")
    parser.add_argument("--max-failures", type=int, default=10, help="how many failures to print")
    args = parser.parse_args()

    trees = args.trees or (args.dumps / "trees")

    painted_exact = 0
    refused = 0
    not_laid_out = 0
    ink_checked = 0
    with_text = 0
    origins_not_re_derived = 0
    failures: list[dict[str, Any]] = []
    refusal_features: dict[str, int] = {}
    # Named, not just counted. A case that does not lay out is either the
    # oracle refusing -- recorded, permanent, and correct -- or an artifact
    # this machine does not have. The count spells those identically, so a
    # local gap reads as a settled fact and a regression into this column
    # reads as nothing at all.
    not_laid_out_cases: list[dict[str, Any]] = []

    for sidecar_path in iter_cases(args.dumps):
        sidecar = json.loads(sidecar_path.read_text())
        version = sidecar.get("schema_version")
        if version != REQUIRED_SIDECAR_SCHEMA:
            print(
                f"::error::{sidecar_path.name} is sidecar schema {version!r}, not "
                f"{REQUIRED_SIDECAR_SCHEMA}. These dumps were written by a different "
                f"render pass than the one in this checkout; regenerate them with "
                f"phase3/scripts/build_render.py."
            )
            return 2
        case_id = sidecar["case_id"]
        if "load_error" in sidecar:
            not_laid_out += 1
            not_laid_out_cases.append(
                {"case_id": case_id, "load_error": sidecar["load_error"]})
            continue
        if sidecar["refusals"] or sidecar["text_failures"] or sidecar.get("render_issues", []):
            refused += 1
            for refusal in sidecar["refusals"]:
                refusal_features[refusal["feature"]] = (
                    refusal_features.get(refusal["feature"], 0) + 1
                )
            for failure in sidecar["text_failures"]:
                refusal_features[failure] = refusal_features.get(failure, 0) + 1
            for issue in sidecar.get("render_issues", []):
                feature = f"backend: {issue.get('code', 'unknown')}"
                refusal_features[feature] = refusal_features.get(feature, 0) + 1
            continue
        dump = args.dumps / f"{case_id}.ppm"
        tree_path = trees / f"{case_id}.json"
        tree = json.loads(tree_path.read_text()) if tree_path.exists() else None
        try:
            problems, checked, unre_derived = check_case(sidecar, dump, tree)
        except DumpError as error:
            problems, checked, unre_derived = ([str(error)], False, 0)
        origins_not_re_derived += unre_derived
        if sidecar["texts"]:
            with_text += 1
            if checked:
                ink_checked += 1
        if problems:
            failures.append({"case_id": case_id, "problems": problems})
        else:
            painted_exact += 1

    total = painted_exact + refused + not_laid_out + len(failures)
    print("| outcome | cases |")
    print("|---|---:|")
    print(f"| painted, round trip exact | {painted_exact} |")
    print(f"| refused by name | {refused} |")
    print(f"| failed | {len(failures)} |")
    print(f"| not laid out (no tree to paint) | {not_laid_out} |")
    print(f"| total | {total} |")
    print()
    if origins_not_re_derived:
        print(
            f"visual origins: {origins_not_re_derived} node(s) have no layout storage, so "
            "their origin comes from the Canvas render exception rather than from "
            "ArrangeCore and is not re-derived here; their absolute origins are still "
            "re-accumulated"
        )
        print()
    if with_text:
        unchecked = with_text - ink_checked
        print(
            f"text runs: {with_text} case(s) carry one; ink checked in {ink_checked}"
            + (
                f", {unchecked} dumped without a glyph backend so their ink is unchecked"
                if unchecked
                else ""
            )
        )

    if not_laid_out_cases:
        print()
        print("| not laid out | why |")
        print("|---|---|")
        for entry in sorted(not_laid_out_cases, key=lambda item: item["case_id"]):
            reason = " ".join(str(entry["load_error"]).split())
            if len(reason) > 160:
                reason = reason[:157] + "..."
            print(f"| {entry['case_id']} | {reason} |")

    if refusal_features:
        print()
        print("| named no-draw | elements |")
        print("|---|---:|")
        for feature, count in sorted(refusal_features.items(), key=lambda kv: -kv[1]):
            print(f"| {feature} | {count} |")

    for failure in failures[: args.max_failures]:
        print(f"\n{failure['case_id']}:")
        for problem in failure["problems"][:5]:
            print(f"  {problem}")

    report = {
        "schema_version": 2,
        "origins_not_re_derived": origins_not_re_derived,
        "painted_exact": painted_exact,
        "refused": refused,
        "failed": len(failures),
        "not_laid_out": not_laid_out,
        "not_laid_out_cases": sorted(not_laid_out_cases,
                                     key=lambda item: item["case_id"]),
        "cases_with_text": with_text,
        "ink_checked": ink_checked,
        "total": total,
        "refusal_features": refusal_features,
        "failures": failures,
    }
    if args.output:
        args.output.write_text(json.dumps(report, indent=1, sort_keys=True) + "\n")
    if args.summary:
        with args.summary.open("a", encoding="utf-8") as out:
            out.write(
                f"\n### Render round trip\n\n"
                f"painted exact {painted_exact}, refused by name {refused}, "
                f"failed {len(failures)}, not laid out {not_laid_out}\n"
            )

    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
