import importlib.util
import unittest
from pathlib import Path


SCRIPT = Path(__file__).resolve().parents[1] / "scripts" / "check_pane_render.py"
SPEC = importlib.util.spec_from_file_location("check_pane_render", SCRIPT)
gate = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
SPEC.loader.exec_module(gate)


# The shapes below are the ones a real launch produces. They are quoted from
# measured logs rather than invented: the application's markers arrive inside
# an OutputDebugStringA payload that Wine escapes, and the runtime's arrive the
# same way, so both are embedded in that framing here.
def _debug(payload: str) -> str:
    return f'01dc:warn:seh:OutputDebugStringA "{payload}\\n"\n'


def producer_log(present_failure: str | None = None, tabs: int = 1) -> str:
    lines = [
        _debug("OpenTerminal startup event=open-tab result=0x00000000"),
        _debug(f"OpenTerminal startup event=complete tabs={tabs}"),
        _debug("OpenTerminal Control event=initialize-terminal-entered"),
        _debug("OpenTerminal Control event=connection-start-complete"),
        _debug("OpenTerminal Control event=enable-painting-complete"),
        _debug("OpenTerminal Atlas event=present-entered"),
        _debug("OpenTerminal Atlas event=create-swap-chain"),
        _debug("OpenTerminal Atlas event=create-composition-swap-chain"),
    ]
    if present_failure:
        lines.append(_debug(f"OpenTerminal Atlas event=present-failed "
                            f"error={present_failure}"))
    return "".join(lines)


def binding_log(bound: bool = True, result: str = "0x00000000",
                node_id: int = 74) -> str:
    return _debug(f"OpenXaml external source=composition-surface-handle "
                  f"result={result} generation=1 kind=1 "
                  f"bound={'true' if bound else 'false'} node_id={node_id}")


def frame_log(external: int = 1, node_id: int = 74,
              rect: tuple[float, float, float, float] = (0.0, 36.0, 1113.0, 590.0),
              generation: int = 7) -> str:
    left, top, width, height = rect
    return (
        _debug(f"OpenXaml frame event=scene-node reason=render-invalidation "
               f"generation={generation} index=26 id={node_id} "
               f"type=Windows.UI.Xaml.Controls.Grid layout=true visible=true "
               f"slot=0.000,0.000,1113.000,626.000 "
               f"actual={width:.3f},{height:.3f} origin={left:.3f},{top:.3f} "
               f"opacity=1.000000 z=0 commands=1 "
               f"path=/Windows.UI.Xaml.Controls.Grid/SwapChainPanel[0]")
        + _debug(f"OpenXaml frame event=scene-stats reason=render-invalidation "
                 f"backend=dcomp generation={generation} nodes=36 "
                 f"visible_nodes=33 commands=3 fills=2 image_brushes=0 text=0 "
                 f"external={external} visuals_created=39 nodes_reused=0 "
                 f"cpu_uploaded=2 cpu_reused=0 "
                 f"external_imported={external} external_reused=0")
    )


def probe_log(colours: list[str] | None = None, displacement: str = "+0,+0",
              inside: int = 6) -> str:
    colours = colours or ["0c0c0c", "cccccc"]
    lines = [
        f"probe displacement={displacement}\n",
        "probe window found=true client=1113x626 origin=100,50 attempts=40 "
        "observations=30\n",
        # One sample in the tab row, which is never inside the pane.
        "probe sample index=0 x=40 y=10 sx=140 sy=60 rgb=333333\n",
    ]
    for index in range(inside):
        colour = colours[index % len(colours)]
        y = 100 + index * 10
        lines.append(f"probe sample index={index + 1} x=200 y={y} "
                     f"sx=300 sy={y + 50} rgb={colour}\n")
    return "".join(lines)


class ProducerEvidence(unittest.TestCase):
    def test_atlas_markers_are_read(self):
        producer = gate.producer_evidence(producer_log())
        self.assertTrue(producer["created_composition_swap_chain"])
        self.assertTrue(producer["entered_present"])
        self.assertTrue(producer["enabled_painting"])
        self.assertEqual(producer["present_failures"], [])
        self.assertEqual(producer["tabs"], 1)

    def test_a_refused_present_keeps_its_hresult(self):
        producer = gate.producer_evidence(producer_log("0x80004001"))
        self.assertEqual(producer["present_failures"], ["0x80004001"])
        self.assertEqual(producer["present_failure_count"], 1)

    def test_a_log_without_the_application_reports_nothing_rather_than_guessing(self):
        producer = gate.producer_evidence("nothing here\n")
        self.assertFalse(producer["created_composition_swap_chain"])
        self.assertIsNone(producer["tabs"])


class BindingEvidence(unittest.TestCase):
    def test_a_live_binding_names_its_node_and_kind(self):
        binding = gate.binding_evidence(binding_log())
        self.assertEqual(binding["node_id"], 74)
        self.assertEqual(binding["source"], "composition-surface-handle")
        self.assertEqual(len(binding["live"]), 1)

    def test_a_failed_binding_is_recorded_but_never_counted_as_live(self):
        binding = gate.binding_evidence(
            binding_log(bound=False, result="0x80004005"))
        self.assertEqual(len(binding["bindings"]), 1)
        self.assertEqual(binding["live"], [])
        self.assertIsNone(binding["node_id"])


class FrameEvidence(unittest.TestCase):
    def test_the_bound_node_is_where_the_scene_says_it_is(self):
        log = binding_log() + frame_log()
        frame = gate.frame_evidence(log, gate.binding_evidence(log)["node_id"])
        self.assertEqual(frame["node_rect"], [0.0, 36.0, 1113.0, 590.0])
        self.assertEqual(frame["frames_carrying_external"], 1)
        self.assertEqual(frame["imported"], 1)
        self.assertEqual(frame["backends"], ["dcomp"])

    def test_a_frame_without_an_external_surface_is_not_counted(self):
        log = frame_log(external=0)
        frame = gate.frame_evidence(log, 74)
        self.assertEqual(frame["frames_carrying_external"], 0)

    def test_an_unknown_node_is_not_placed_from_some_other_node(self):
        frame = gate.frame_evidence(frame_log(node_id=74), 99)
        self.assertIsNone(frame["node_rect"])


class PaneSamples(unittest.TestCase):
    def test_only_samples_inside_the_pane_are_taken(self):
        log = binding_log() + frame_log()
        frame = gate.frame_evidence(log, 74)
        probe = gate.probe_evidence(probe_log())
        inside = gate.pane_samples(frame, probe)
        self.assertEqual(len(inside), 6)
        self.assertNotIn(0, [sample["index"] for sample in inside])

    def test_without_a_pane_rectangle_no_sample_is_claimed_to_be_in_it(self):
        probe = gate.probe_evidence(probe_log())
        self.assertEqual(gate.pane_samples({"node_rect": None}, probe), [])


class Evaluate(unittest.TestCase):
    def test_a_rendered_pane_passes_every_check(self):
        result = gate.evaluate(producer_log() + binding_log() + frame_log(),
                               probe_log())
        self.assertEqual(result["failed"], [])
        self.assertEqual(result["skipped"], [])
        self.assertTrue(result["success"])

    def test_a_refused_present_fails_by_name_rather_than_skipping(self):
        result = gate.evaluate(
            producer_log("0x80004001") + binding_log() + frame_log(),
            probe_log())
        self.assertIn("app-present-not-refused", result["failed"])
        self.assertEqual(result["skipped"], [])
        detail = next(check["detail"] for check in result["checks"]
                      if check["name"] == "app-present-not-refused")
        self.assertIn("0x80004001", detail)

    def test_a_flat_pane_is_blank_however_bright_it_is(self):
        result = gate.evaluate(producer_log() + binding_log() + frame_log(),
                               probe_log(colours=["cccccc"]))
        self.assertIn("pane-is-not-blank", result["failed"])

    def test_an_unbound_surface_fails_the_binding_and_the_pane(self):
        result = gate.evaluate(producer_log() + frame_log(external=0),
                               probe_log(colours=["0c0c0c"]))
        self.assertIn("runtime-bound-producer-surface", result["failed"])
        self.assertIn("committed-frame-carries-producer-surface", result["failed"])

    def test_a_displaced_layered_child_skips_the_pixels_by_name(self):
        result = gate.evaluate(producer_log() + binding_log() + frame_log(),
                               probe_log(displacement="+0,-36"))
        self.assertIn("pane-is-not-blank", result["skipped"])
        self.assertNotIn("pane-is-not-blank", result["failed"])

    def test_too_few_samples_in_the_pane_is_a_failure_not_a_pass(self):
        result = gate.evaluate(producer_log() + binding_log() + frame_log(),
                               probe_log(inside=1))
        self.assertIn("pane-is-not-blank", result["failed"])


if __name__ == "__main__":
    unittest.main()
