#!/usr/bin/env python3
"""Recover what was painted back out of the pixels, and hold it to the layout.

The render pass in phase3/render has no oracle. Rendered-output probes are wave
6 in phase3/ROADMAP.md and nothing has recorded a pixel of the real runtime, so
there is no measurement to compare a dump against. What there *is* is a
measurement of every rectangle's geometry -- the corpus verifies the arranged
tree against 1176 of 1177 recorded answers -- and a render pass that claims to
paint exactly those rectangles and nothing else.

So this checks the claim rather than the picture, three ways:

  1. **The geometry is the measurement path's.** The sidecar's per-node slot and
     render size are diffed against the tree `render_cases` writes in the shape
     `measure_cases` writes it, node for node. And the absolute origin the render
     pass accumulated is re-accumulated here from the slots, so a wrong sum is
     caught rather than trusted.
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

Text is checked differently, and less. What a glyph looks like has no oracle at
all and is delegated to the platform's own text output with the real font
selected; what belongs to this project is where the run starts and how wide the
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
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path
from typing import Any, Iterable


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


def round_half_up(value: float) -> int:
    """The layout core's own tie-break -- floor(v + 0.5); see layout.h."""
    return int(math.floor(value + 0.5))


def snap(x: float, y: float, w: float, h: float) -> tuple[int, int, int, int]:
    return (round_half_up(x), round_half_up(y), round_half_up(x + w), round_half_up(y + h))


def absolute_origins(geometry: list[dict[str, Any]]) -> dict[str, tuple[float, float]]:
    """Re-accumulates every node's absolute origin from the recorded columns.

    The sidecar reports an absolute origin of its own; this ignores it and adds
    the chain up again from `slot`, which is the column the corpus records as
    `offset`. The two are compared afterwards, so a render pass that accumulated
    wrongly is caught rather than believed.
    """
    origins: dict[str, tuple[float, float]] = {}
    for node in geometry:
        path = node["path"]
        parent = path.rsplit("/", 1)[0]
        px, py = origins.get(parent, (0.0, 0.0))
        origins[path] = (px + node["slot"][0], py + node["slot"][1])
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
    rects: list[tuple[tuple[int, int, int, int], bytes]],
) -> bytearray:
    """Re-rasterises the expected rectangles, in paint order, in Python."""
    frame = bytearray(backdrop * (width * height))
    for (left, top, right, bottom), colour in rects:
        left = max(left, 0)
        top = max(top, 0)
        right = min(right, width)
        bottom = min(bottom, height)
        if right <= left or bottom <= top:
            continue
        span = colour * (right - left)
        for y in range(top, bottom):
            base = (y * width + left) * 3
            frame[base : base + (right - left) * 3] = span
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
) -> tuple[list[str], bool]:
    """Returns (problems, ink_was_checked). No problems means the round trip is exact."""
    problems: list[str] = []
    width, height, pixels = read_ppm(dump)
    if [width, height] != sidecar["surface"]:
        return ([f"the dump is {width}x{height}, the sidecar says {sidecar['surface']}"], False)

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

    # 2. Every absolute origin follows from the recorded chain.
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
    expected: list[tuple[tuple[int, int, int, int], bytes]] = []
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
        expected.append((box, parse_color(op["color"])))

    text_boxes = [tuple(op["pixels"]) for op in sidecar["texts"]]

    # 4. Re-rasterise and compare, byte for byte, outside the text boxes.
    frame = paint_expected(width, height, backdrop, expected)
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
    for left, top, right, bottom, colour in extract_rects(width, height, pixels, backdrop):
        box = (left, top, right, bottom)
        if any(
            left >= b[0] and top >= b[1] and right <= b[2] and bottom <= b[3] for b in text_boxes
        ):
            continue  # inside a run's box: ink, checked below rather than here
        if any(box == other and colour == other_colour for other, other_colour in expected):
            continue
        if any(
            colour == other_colour
            and left >= other[0]
            and top >= other[1]
            and right <= other[2]
            and bottom <= other[3]
            for other, other_colour in expected
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
    return (problems, ink_checked)


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
    return sorted(p for p in dumps.glob("*.json"))


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
    failures: list[dict[str, Any]] = []
    refusal_features: dict[str, int] = {}

    for sidecar_path in iter_cases(args.dumps):
        sidecar = json.loads(sidecar_path.read_text())
        case_id = sidecar["case_id"]
        if "load_error" in sidecar:
            not_laid_out += 1
            continue
        if sidecar["refusals"] or sidecar["text_failures"]:
            refused += 1
            for refusal in sidecar["refusals"]:
                refusal_features[refusal["feature"]] = (
                    refusal_features.get(refusal["feature"], 0) + 1
                )
            for failure in sidecar["text_failures"]:
                refusal_features[failure] = refusal_features.get(failure, 0) + 1
            continue
        dump = args.dumps / f"{case_id}.ppm"
        tree_path = trees / f"{case_id}.json"
        tree = json.loads(tree_path.read_text()) if tree_path.exists() else None
        try:
            problems, checked = check_case(sidecar, dump, tree)
        except DumpError as error:
            problems, checked = ([str(error)], False)
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
        "schema_version": 1,
        "painted_exact": painted_exact,
        "refused": refused,
        "failed": len(failures),
        "not_laid_out": not_laid_out,
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
