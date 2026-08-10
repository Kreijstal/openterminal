import importlib.util
import unittest
from pathlib import Path


SCRIPT = Path(__file__).resolve().parents[1] / "scripts" / "check_xbf_ui_render.py"
SPEC = importlib.util.spec_from_file_location("check_xbf_ui_render", SCRIPT)
gate = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
SPEC.loader.exec_module(gate)


_ROTATION = (
    "Windows.UI.Xaml.Controls.Grid",
    "Windows.UI.Xaml.Controls.Border",
    "Windows.UI.Xaml.Controls.StackPanel",
    "Windows.UI.Xaml.Controls.ContentPresenter",
    "Windows.UI.Xaml.Controls.TextBlock",
    "Windows.UI.Xaml.Controls.Grid",
)


def scene_log(generation: int = 7) -> str:
    """One committed frame: a page root, a painted grid, and a text run."""
    return (
        f'OpenXaml xbf event=loaded page="TerminalApp/TerminalPage.xbf" '
        f"root=Windows.UI.Xaml.Controls.Page objects=278\n"
        f'OpenXaml xbf event=type page="TerminalApp/TerminalPage.xbf" '
        f"name=Windows.UI.Xaml.Controls.Page count=1\n"
        f'OpenXaml xbf event=type page="TerminalApp/TerminalPage.xbf" '
        f"name=Windows.UI.Xaml.Controls.Grid count=4\n"
        f'OpenXaml xbf event=type page="TerminalApp/TerminalPage.xbf" '
        f"name=Windows.UI.Xaml.Controls.Border count=2\n"
        f'OpenXaml xbf event=type page="TerminalApp/TerminalPage.xbf" '
        f"name=Windows.UI.Xaml.Controls.TextBlock count=3\n"
        f'OpenXaml xbf event=type page="TerminalApp/TerminalPage.xbf" '
        f"name=Windows.UI.Xaml.Controls.StackPanel count=2\n"
        f'OpenXaml xbf event=type page="TerminalApp/TerminalPage.xbf" '
        f"name=Windows.UI.Xaml.Controls.ContentPresenter count=2\n"
        + "".join(
            f"OpenXaml frame event=scene-node reason=content generation={generation} "
            f"index={index} id={index + 1} type={_ROTATION[index % len(_ROTATION)]} "
            f"layout=true visible=true slot=0.000,0.000,640.000,480.000 "
            f"actual=640.000,480.000 origin=0.000,0.000 opacity=1.000000 z=0 "
            f"commands=1 path=/{_ROTATION[index % len(_ROTATION)]}[{index}]\n"
            for index in range(24)
        )
        + f"OpenXaml frame event=scene-node reason=content generation={generation} "
          f"index=24 id=25 type=Windows.UI.Xaml.Controls.Page layout=true "
          f"visible=true slot=0.000,0.000,640.000,480.000 actual=640.000,480.000 "
          f"origin=0.000,0.000 opacity=1.000000 z=0 commands=0 "
          f"path=/Windows.UI.Xaml.Controls.Grid/Windows.UI.Xaml.Controls.Page[0]\n"
        + f"OpenXaml frame event=scene-node reason=content generation={generation} "
          f"index=25 id=26 type=Windows.UI.Xaml.Controls.TextBlock layout=true "
          f"visible=true slot=0.000,0.000,80.000,20.000 actual=80.000,20.000 "
          f"origin=0.000,0.000 opacity=1.000000 z=0 commands=1 "
          f"path=/Windows.UI.Xaml.Controls.Grid/Windows.UI.Xaml.Controls.TextBlock[1]\n"
        + f"OpenXaml frame event=scene-fill reason=content generation={generation} "
          f"index=0 node=1 what=background rect=0.000,0.000,640.000,480.000 "
          f"color=ff0c0c0c path=/Windows.UI.Xaml.Controls.Grid[0]\n"
        + f"OpenXaml frame event=scene-text reason=content generation={generation} "
          f"index=0 node=26 rect=0.000,0.000,80.000,20.000 "
          f"path=/Windows.UI.Xaml.Controls.Grid/Windows.UI.Xaml.Controls.TextBlock[1]\n"
        + f"OpenXaml frame event=present-origin generation={generation} "
          f"screen=100,50 extent=640x480\n"
    )


def probe_log(displacement: str = "+0,+0", rgb: str = "0c0c0c") -> str:
    return (
        f"probe displacement={displacement}\n"
        "probe window found=true client=640x480 origin=100,50 attempts=40 "
        "observations=30\n"
        f"probe sample index=0 x=320 y=240 sx=420 sy=290 rgb={rgb}\n"
        "probe sample index=1 x=40 y=10 sx=140 sy=60 rgb=112233\n"
        "probe done\n"
    )


class XbfEvidence(unittest.TestCase):
    def test_pages_and_declared_types_are_read(self):
        evidence = gate.xbf_evidence(scene_log())
        self.assertEqual(sorted(evidence["pages"]), ["TerminalApp/TerminalPage.xbf"])
        page = evidence["pages"]["TerminalApp/TerminalPage.xbf"]
        self.assertEqual(page["root"], "Windows.UI.Xaml.Controls.Page")
        self.assertEqual(page["objects"], 278)
        self.assertIn("Windows.UI.Xaml.Controls.Grid", evidence["declared_types"])
        self.assertEqual(evidence["roots"], ["Windows.UI.Xaml.Controls.Page"])

    def test_no_xbf_evidence_is_empty_not_an_error(self):
        evidence = gate.xbf_evidence("nothing to see here\n")
        self.assertEqual(evidence["pages"], {})
        self.assertEqual(evidence["roots"], [])


class SceneEvidence(unittest.TestCase):
    def test_newest_generation_wins(self):
        log = scene_log(3) + scene_log(9)
        scene = gate.scene_evidence(log)
        self.assertEqual(scene["generation"], 9)
        self.assertEqual(len(scene["nodes"]), 26)
        self.assertEqual(len(scene["fills"]), 1)
        self.assertEqual(scene["fills"][0]["rgb"], "0c0c0c")
        self.assertEqual(scene["fills"][0]["alpha"], 255)
        self.assertIn("Windows.UI.Xaml.Controls.Page", scene["types"])

    def test_absent_dump_reports_nothing_rather_than_guessing(self):
        scene = gate.scene_evidence("OpenXaml frame event=commit reason=content\n")
        self.assertIsNone(scene["generation"])
        self.assertEqual(scene["nodes"], [])


class PixelExpectation(unittest.TestCase):
    def test_sample_inside_the_opaque_fill_is_chosen(self):
        scene = gate.scene_evidence(scene_log())
        present = gate.present_evidence(scene_log())
        probe = gate.probe_evidence(probe_log())
        expectation = gate.pixel_expectation(scene, present, probe)
        self.assertIsNotNone(expectation)
        self.assertEqual(expectation["fill"]["rgb"], "0c0c0c")
        self.assertEqual(expectation["sample"]["index"], 0)

    def test_a_sample_under_a_text_run_is_never_predicted(self):
        scene = gate.scene_evidence(scene_log())
        present = gate.present_evidence(scene_log())
        probe = gate.probe_evidence(
            "probe displacement=+0,+0\n"
            "probe window found=true client=640x480 origin=100,50 attempts=1 "
            "observations=1\n"
            "probe sample index=0 x=40 y=10 sx=140 sy=60 rgb=ffff00\n"
            "probe done\n")
        self.assertIsNone(gate.pixel_expectation(scene, present, probe))

    def test_without_a_present_origin_no_prediction_is_made(self):
        scene = gate.scene_evidence(scene_log())
        probe = gate.probe_evidence(probe_log())
        self.assertIsNone(gate.pixel_expectation(
            scene, {"generation": None, "screen": None, "extent": None}, probe))


class EscapedRecords(unittest.TestCase):
    def test_a_wine_escaped_capture_reads_the_same_as_a_raw_record(self):
        raw = scene_log()
        escaped = raw.replace('page="', 'page=\\"').replace('.xbf"', '.xbf\\"')
        self.assertEqual(gate.xbf_evidence(raw)["pages"].keys(),
                         gate.xbf_evidence(escaped)["pages"].keys())
        self.assertEqual(gate.xbf_evidence(escaped)["roots"],
                         ["Windows.UI.Xaml.Controls.Page"])


class Evaluate(unittest.TestCase):
    def test_complete_evidence_passes_every_check(self):
        result = gate.evaluate(scene_log(), probe_log(), 24, 6)
        self.assertTrue(result["success"], result["failed"])
        self.assertEqual(result["skipped"], [])

    def test_a_wrong_pixel_fails_rather_than_skips(self):
        result = gate.evaluate(scene_log(), probe_log(rgb="ff0000"), 24, 6)
        self.assertFalse(result["success"])
        self.assertIn("pixel-matches-committed-fill", result["failed"])

    def test_displaced_layered_child_skips_only_the_pixel_checks(self):
        result = gate.evaluate(scene_log(), probe_log(displacement="+4,+30"),
                               24, 6)
        self.assertEqual(sorted(result["skipped"]),
                         ["pixel-is-not-blank", "pixel-matches-committed-fill"])
        self.assertTrue(result["success"], result["failed"])
        detail = [check["detail"] for check in result["checks"]
                  if check["name"] == "pixel-matches-committed-fill"][0]
        self.assertIn("layered-child-update.md", detail)

    def test_an_empty_scene_fails_the_structural_checks(self):
        result = gate.evaluate(
            "OpenXaml frame event=commit reason=content generation=5\n",
            probe_log(), 24, 6)
        self.assertFalse(result["success"])
        for name in ("scene-dump-present", "xbf-page-roots-in-scene",
                     "scene-paints-opaque-fill", "frame-presented"):
            self.assertIn(name, result["failed"])

    def test_a_committed_but_unpresented_frame_is_not_success(self):
        log = scene_log().replace(
            "OpenXaml frame event=present-origin generation=7 "
            "screen=100,50 extent=640x480\n", "")
        result = gate.evaluate(log, probe_log(), 24, 6)
        self.assertFalse(result["success"])
        self.assertIn("frame-presented", result["failed"])

    def test_a_transparent_only_frame_fails_the_paint_check(self):
        log = scene_log().replace("color=ff0c0c0c", "color=000c0c0c")
        result = gate.evaluate(log, probe_log(), 24, 6)
        self.assertFalse(result["success"])
        self.assertIn("scene-paints-opaque-fill", result["failed"])

    def test_a_missing_probe_window_fails_and_does_not_vacuously_pass(self):
        result = gate.evaluate(scene_log(), "probe displacement=+0,+0\nprobe done\n",
                               24, 6)
        self.assertFalse(result["success"])
        self.assertIn("probe-observed-window", result["failed"])
        self.assertIn("pixel-matches-committed-fill", result["failed"])
        self.assertEqual(result["skipped"], [])

    def test_a_loader_that_aborts_before_the_window_is_named_not_guessed(self):
        log = scene_log() + (
            "wine: Unimplemented function dcomp.dll.DCompositionCreateSurfaceHandle "
            "called at address 00006FFFFF32D977 (thread 0220), starting debugger...\n")
        result = gate.evaluate(log, "probe displacement=+0,+0\nprobe done\n", 24, 6)
        self.assertEqual(sorted(result["skipped"]),
                         ["pixel-is-not-blank", "pixel-matches-committed-fill",
                          "probe-observed-window"])
        self.assertEqual(result["loader_gaps"],
                         ["dcomp.dll.DCompositionCreateSurfaceHandle"])
        self.assertTrue(result["success"], result["failed"])
        detail = [check["detail"] for check in result["checks"]
                  if check["name"] == "probe-observed-window"][0]
        self.assertIn("DCompositionCreateSurfaceHandle", detail)

    def test_an_application_rooted_page_is_not_required_in_the_visual_tree(self):
        log = scene_log() + (
            'OpenXaml xbf event=loaded page="TerminalApp/App.xbf" '
            "root=Windows.UI.Xaml.Application objects=41\n")
        result = gate.evaluate(log, probe_log(), 24, 6)
        self.assertTrue(result["success"], result["failed"])
        detail = [check["detail"] for check in result["checks"]
                  if check["name"] == "xbf-page-roots-in-scene"][0]
        self.assertIn("not_visual=['Windows.UI.Xaml.Application']", detail)

    def test_a_page_root_that_never_rendered_still_fails(self):
        log = scene_log() + (
            'OpenXaml xbf event=loaded page="TerminalApp/TabRowControl.xbf" '
            "root=Windows.UI.Xaml.Controls.UserControl objects=12\n")
        result = gate.evaluate(log, probe_log(), 24, 6)
        self.assertFalse(result["success"])
        self.assertIn("xbf-page-roots-in-scene", result["failed"])

    def test_an_abort_before_the_window_outranks_the_displacement_reason(self):
        # Both are true of the stock loader. The reason there was nothing to
        # read is the abort, not where a layered child would have landed.
        log = scene_log() + (
            "wine: Unimplemented function dcomp.dll.DCompositionCreateSurfaceHandle "
            "called at address 0\n")
        probe = ("probe displacement=+4,+30\n"
                 "probe window found=false client=0x0 origin=0,0 attempts=280 "
                 "observations=0\n"
                 "probe sample index=0 x=0 y=0 sx=0 sy=0 rgb=none\n"
                 "probe done\n")
        result = gate.evaluate(log, probe, 24, 6)
        self.assertTrue(result["success"], result["failed"])
        for name in ("probe-observed-window", "pixel-matches-committed-fill"):
            detail = [check["detail"] for check in result["checks"]
                      if check["name"] == name][0]
            self.assertIn("DCompositionCreateSurfaceHandle", detail)
            self.assertNotIn("displaces a layered child", detail)

    def test_a_loader_gap_after_a_good_reading_does_not_skip_anything(self):
        log = scene_log() + "wine: Unimplemented function x.dll.Y called at 0\n"
        result = gate.evaluate(log, probe_log(), 24, 6)
        self.assertEqual(result["skipped"], [])
        self.assertTrue(result["success"], result["failed"])


if __name__ == "__main__":
    unittest.main()
