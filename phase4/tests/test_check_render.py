"""The round-trip checker catches what it claims to catch.

check_render.py is the gate over phase3/render, and a gate is only worth its
green: a checker that passed everything would report the render pass as exact
without ever having looked. So this builds dumps by hand -- one correct, and one
for each way the render pass could be wrong -- and holds the checker to failing
every wrong one.

No Wine, no fonts, no corpus. Everything here is synthesised, so this test never
skips and never passes vacuously.
"""

import json
import sys
import tempfile
import unittest
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPOSITORY / "phase4" / "scripts"))

import check_render  # noqa: E402

BACKDROP = (0x00, 0x80, 0x80)
RED = (0xFF, 0x00, 0x00)
BLUE = (0x00, 0x00, 0xFF)
INK = (0xFF, 0x00, 0xFF)


def ppm(width: int, height: int, pixels: list[tuple[int, int, int]]) -> bytes:
    body = bytearray()
    for rgb in pixels:
        body.extend(rgb)
    return f"P6\n{width} {height}\n255\n".encode() + bytes(body)


def surface(width: int, height: int, rects) -> list[tuple[int, int, int]]:
    out = [BACKDROP] * (width * height)
    for (left, top, right, bottom), rgb in rects:
        for y in range(top, bottom):
            for x in range(left, right):
                out[y * width + x] = rgb
    return out


def sidecar(width, height, rects, texts=(), geometry=None):
    if geometry is None:
        geometry = [
            {
                "path": "/Grid",
                "type": "Windows.UI.Xaml.Controls.Grid",
                "slot": [0.0, 0.0, float(width), float(height)],
                "actual": [float(width), float(height)],
                "abs": [0.0, 0.0],
                "layout_storage": True,
                "visible": True,
            }
        ]
    return {
        "schema_version": 1,
        "case_id": "synthetic",
        "backend": "test",
        "surface": [width, height],
        "backdrop": "#ff008080",
        "probe_ink": "#ffff00ff",
        "geometry": geometry,
        "rects": list(rects),
        "texts": list(texts),
        "refusals": [],
        "text_failures": [],
    }


def rect_op(path, what, bounds, colour):
    return {"path": path, "what": what, "bounds": list(bounds), "pixels": [], "color": colour}


class RowRuns(unittest.TestCase):
    def test_a_uniform_row_is_one_run(self):
        row = bytes(BACKDROP) * 8
        self.assertEqual(check_render.row_runs(row, 8), [(0, 8, bytes(BACKDROP))])

    def test_runs_are_maximal_and_exact(self):
        row = bytes(BACKDROP) * 2 + bytes(RED) * 3 + bytes(BLUE) * 1 + bytes(BACKDROP) * 2
        self.assertEqual(
            check_render.row_runs(row, 8),
            [
                (0, 2, bytes(BACKDROP)),
                (2, 5, bytes(RED)),
                (5, 6, bytes(BLUE)),
                (6, 8, bytes(BACKDROP)),
            ],
        )

    def test_a_zero_width_row_has_no_runs(self):
        # Cases arranged at an available size of [0, 0] really do dump one.
        self.assertEqual(check_render.row_runs(b"", 0), [])


class ExtractRects(unittest.TestCase):
    def test_a_solid_rectangle_comes_back_whole(self):
        pixels = ppm(10, 6, surface(10, 6, [((2, 1, 7, 4), RED)]))[len("P6\n10 6\n255\n") :]
        found = check_render.extract_rects(10, 6, pixels, bytes(BACKDROP))
        self.assertEqual(found, [(2, 1, 7, 4, bytes(RED))])

    def test_a_shape_that_is_not_a_rectangle_comes_back_as_several(self):
        # An L. If this reassembled as one rectangle the checker could be fooled
        # by a render pass that painted the wrong shape.
        pixels = ppm(6, 6, surface(6, 6, [((0, 0, 2, 6), RED), ((0, 4, 6, 6), RED)]))[
            len("P6\n6 6\n255\n") :
        ]
        found = check_render.extract_rects(6, 6, pixels, bytes(BACKDROP))
        self.assertGreater(len(found), 1)

    def test_two_colours_never_merge(self):
        pixels = ppm(4, 4, surface(4, 4, [((0, 0, 4, 2), RED), ((0, 2, 4, 4), BLUE)]))[
            len("P6\n4 4\n255\n") :
        ]
        found = sorted(check_render.extract_rects(4, 4, pixels, bytes(BACKDROP)))
        self.assertEqual(found, [(0, 0, 4, 2, bytes(RED)), (0, 2, 4, 4, bytes(BLUE))])


class Snapping(unittest.TestCase):
    def test_the_tie_break_is_the_layout_cores(self):
        # floor(v + 0.5). Half-to-even would answer 120 for the first.
        self.assertEqual(check_render.round_half_up(120.5), 121)
        self.assertEqual(check_render.round_half_up(0.5), 1)
        self.assertEqual(check_render.round_half_up(0.4), 0)

    def test_a_whole_rect_maps_to_itself(self):
        self.assertEqual(check_render.snap(10.0, 20.0, 30.0, 40.0), (10, 20, 40, 60))

    def test_a_half_pixel_rect_keeps_its_size(self):
        left, top, right, bottom = check_render.snap(0.5, 0.5, 10.0, 10.0)
        self.assertEqual((right - left, bottom - top), (10, 10))


class Origins(unittest.TestCase):
    def test_the_chain_is_re_accumulated_not_trusted(self):
        geometry = [
            {"path": "/Grid", "slot": [0.0, 0.0, 100.0, 100.0]},
            {"path": "/Grid/Border[0]", "slot": [10.0, 20.0, 50.0, 50.0]},
            {"path": "/Grid/Border[0]/TextBlock[0]", "slot": [1.0, 2.0, 10.0, 10.0]},
        ]
        origins = check_render.absolute_origins(geometry)
        self.assertEqual(origins["/Grid/Border[0]"], (10.0, 20.0))
        self.assertEqual(origins["/Grid/Border[0]/TextBlock[0]"], (11.0, 22.0))


class CheckCase(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.root = Path(self.dir.name)

    def tearDown(self):
        self.dir.cleanup()

    def run_check(self, width, height, pixels, card):
        dump = self.root / "synthetic.ppm"
        dump.write_bytes(ppm(width, height, pixels))
        return check_render.check_case(card, dump, None)[0]

    def test_an_exact_round_trip_has_no_problems(self):
        card = sidecar(20, 10, [rect_op("/Grid", "background", (0, 0, 20, 10), "#ffff0000")])
        pixels = surface(20, 10, [((0, 0, 20, 10), RED)])
        self.assertEqual(self.run_check(20, 10, pixels, card), [])

    def test_a_rectangle_off_by_one_pixel_fails(self):
        card = sidecar(
            20,
            10,
            [rect_op("/Grid/Border[0]", "background", (2, 2, 6, 4), "#ffff0000")],
            geometry=[
                {
                    "path": "/Grid",
                    "type": "Grid",
                    "slot": [0.0, 0.0, 20.0, 10.0],
                    "actual": [20.0, 10.0],
                    "abs": [0.0, 0.0],
                    "layout_storage": True,
                    "visible": True,
                },
                {
                    "path": "/Grid/Border[0]",
                    "type": "Border",
                    "slot": [2.0, 2.0, 6.0, 4.0],
                    "actual": [6.0, 4.0],
                    "abs": [2.0, 2.0],
                    "layout_storage": True,
                    "visible": True,
                },
            ],
        )
        # The layout says [2,2)-[8,6); the dump paints it one pixel right.
        pixels = surface(20, 10, [((3, 2, 9, 6), RED)])
        problems = self.run_check(20, 10, pixels, card)
        self.assertTrue(problems, "a one-pixel shift has to fail")

    def test_a_single_stray_pixel_fails(self):
        card = sidecar(20, 10, [rect_op("/Grid", "background", (0, 0, 20, 10), "#ffff0000")])
        pixels = surface(20, 10, [((0, 0, 20, 10), RED)])
        pixels[5 * 20 + 5] = BLUE
        problems = self.run_check(20, 10, pixels, card)
        self.assertTrue(problems, "one wrong pixel has to fail")

    def test_a_wrong_colour_fails(self):
        card = sidecar(20, 10, [rect_op("/Grid", "background", (0, 0, 20, 10), "#ffff0000")])
        pixels = surface(20, 10, [((0, 0, 20, 10), BLUE)])
        self.assertTrue(self.run_check(20, 10, pixels, card))

    def test_a_rectangle_nothing_asked_for_fails(self):
        card = sidecar(20, 10, [])
        pixels = surface(20, 10, [((4, 4, 8, 8), RED)])
        self.assertTrue(self.run_check(20, 10, pixels, card))

    def test_a_lying_absolute_origin_fails(self):
        card = sidecar(20, 10, [])
        card["geometry"][0]["abs"] = [3.0, 0.0]
        pixels = surface(20, 10, [])
        problems = self.run_check(20, 10, pixels, card)
        self.assertTrue(any("accumulated" in p for p in problems))

    def test_geometry_is_diffed_against_the_measured_tree(self):
        card = sidecar(20, 10, [])
        dump = self.root / "synthetic.ppm"
        dump.write_bytes(ppm(20, 10, surface(20, 10, [])))
        tree = {
            "tree": [
                {
                    "path": "/Grid",
                    "type": "Windows.UI.Xaml.Controls.Grid",
                    "desired": [20.0, 10.0],
                    "actual": [19.0, 10.0],  # the measurement path disagrees
                    "offset": [0.0, 0.0],
                }
            ]
        }
        problems = check_render.check_case(card, dump, tree)[0]
        self.assertTrue(any("measured" in p for p in problems))

    def test_ink_inside_a_run_box_is_accepted_and_ink_outside_is_not(self):
        text = {
            "path": "/Grid/TextBlock[0]",
            "bounds": [2.0, 2.0, 8.0, 4.0],
            "pixels": [2, 2, 10, 6],
            "font_family": "Cascadia Mono",
            "font_size": 14.0,
            "text": "Hi",
        }
        card = sidecar(20, 10, [], texts=[text])
        inside = surface(20, 10, [((3, 3, 5, 5), INK)])
        self.assertEqual(self.run_check(20, 10, inside, card), [])

        outside = surface(20, 10, [((12, 3, 14, 5), INK)])
        self.assertTrue(self.run_check(20, 10, outside, card))

    def test_a_run_with_no_ink_at_all_fails_once_a_backend_drew_something(self):
        runs = [
            {
                "path": "/Grid/TextBlock[0]",
                "bounds": [0.0, 0.0, 4.0, 4.0],
                "pixels": [0, 0, 4, 4],
                "font_family": "Cascadia Mono",
                "font_size": 14.0,
                "text": "A",
            },
            {
                "path": "/Grid/TextBlock[1]",
                "bounds": [8.0, 0.0, 4.0, 4.0],
                "pixels": [8, 0, 12, 4],
                "font_family": "Cascadia Mono",
                "font_size": 14.0,
                "text": "B",
            },
        ]
        card = sidecar(20, 10, [], texts=runs)
        # The first run inked, the second did not.
        pixels = surface(20, 10, [((1, 1, 3, 3), INK)])
        problems = self.run_check(20, 10, pixels, card)
        self.assertTrue(any("painted no ink" in p for p in problems))

    def test_a_dump_with_no_glyph_backend_reports_its_ink_unchecked(self):
        text = {
            "path": "/Grid/TextBlock[0]",
            "bounds": [2.0, 2.0, 8.0, 4.0],
            "pixels": [2, 2, 10, 6],
            "font_family": "Segoe UI",
            "font_size": 14.0,
            "text": "Hi",
        }
        card = sidecar(20, 10, [], texts=[text])
        dump = self.root / "synthetic.ppm"
        dump.write_bytes(ppm(20, 10, surface(20, 10, [])))
        problems, ink_checked = check_render.check_case(card, dump, None)
        self.assertEqual(problems, [])
        self.assertFalse(ink_checked, "an unchecked run must not read as a checked one")


class ReadPpm(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.root = Path(self.dir.name)

    def tearDown(self):
        self.dir.cleanup()

    def test_a_truncated_dump_is_an_error_and_not_a_pass(self):
        path = self.root / "short.ppm"
        path.write_bytes(b"P6\n4 4\n255\n" + b"\x00" * 10)
        with self.assertRaises(check_render.DumpError):
            check_render.read_ppm(path)

    def test_a_dump_round_trips(self):
        path = self.root / "ok.ppm"
        path.write_bytes(ppm(3, 2, [RED] * 6))
        width, height, pixels = check_render.read_ppm(path)
        self.assertEqual((width, height), (3, 2))
        self.assertEqual(pixels, bytes(RED) * 6)


if __name__ == "__main__":
    unittest.main()
