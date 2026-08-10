"""What the input round-trip gate concludes from a run's own records.

The gate itself needs a Wine prefix, a built Terminal and a display; these
tests need none of those. They pin the reading: which combinations of probe
record and sentinel file count as a keystroke having reached the shell, which
count as the negative control having proved something, and which are refused
rather than quietly passed.

The integration half lives in ``test_input_roundtrip_wine.py`` beside this,
which skips by name when the build it needs is absent.
"""

import importlib.util
import unittest
from pathlib import Path


SCRIPT = Path(__file__).resolve().parents[1] / "scripts" / "check_input_roundtrip.py"
SPEC = importlib.util.spec_from_file_location("check_input_roundtrip", SCRIPT)
gate = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
SPEC.loader.exec_module(gate)


TYPED = "echo INPUT_GATE_1138>C:\\input_gate_1138.txt"


def probe_log(mechanism: str = "sendinput", injected: int = 44,
              expected: int = 44, visible: int = 1, ready: str = "true",
              before: str = "0c0c0c", after: str = "0c0c0c") -> str:
    """One probe run, reduced to the lines the checker reads."""
    keys = "".join(
        f"input key index={index} char=97 vk=0 down=1 up=1\n"
        for index in range(injected))
    pixels = "".join(
        f"input pixel phase=before index={index} x=10 y=10 rgb={before}\n"
        f"input pixel phase=after index={index} x=10 y=10 rgb={after}\n"
        for index in range(6))
    return (
        f"input mechanism={mechanism} text_length={len(TYPED)} enter=1\n"
        "input class name=CASCADIA_HOSTING_WINDOW_CLASS\n"
        "input window found=true hwnd=0x60080 class=CASCADIA_HOSTING_WINDOW_CLASS "
        f"client=1113x627 visible={visible} attempts=12\n"
        f"input shell_ready={ready} waits=31 file=C:\\shell_ready_1138.txt\n"
        "input island hwnd=0x60084 class=OpenXaml.DesktopWindowXamlSource\n"
        "input child depth=1 hwnd=0x60084 class=OpenXaml.DesktopWindowXamlSource "
        "visible=1 rect=0,0,1113,627\n"
        "input foreground requested=1 before=0x0 before_class=(null) "
        "after=0x60080 after_class=CASCADIA_HOSTING_WINDOW_CLASS\n"
        "input focus thread=492 hwnd=0x60084 "
        "class=OpenXaml.DesktopWindowXamlSource\n"
        + pixels + keys
        + "input focus_after hwnd=0x60084 class=OpenXaml.DesktopWindowXamlSource\n"
        f"input done injected={injected} of={expected}\n")


class ProbeEvidence(unittest.TestCase):
    def test_the_records_the_probe_wrote_are_read_back(self):
        probe = gate.probe_evidence(probe_log())
        self.assertTrue(probe["window_found"])
        self.assertTrue(probe["window_visible"])
        self.assertTrue(probe["shell_ready"])
        self.assertEqual(probe["client"], [1113, 627])
        self.assertEqual(probe["island_class"],
                         "OpenXaml.DesktopWindowXamlSource")
        self.assertEqual(probe["injected"], 44)
        self.assertEqual(probe["expected_keys"], 44)
        self.assertEqual(len(probe["keys"]), 44)

    def test_a_probe_that_found_nothing_reports_nothing_rather_than_guessing(self):
        probe = gate.probe_evidence(
            "input mechanism=sendinput text_length=43 enter=1\n"
            "input window found=false attempts=858\n"
            "input done injected=0 of=0\n")
        self.assertFalse(probe["window_found"])
        self.assertFalse(probe["window_visible"])
        self.assertIsNone(probe["client"])
        self.assertEqual(probe["injected"], 0)


class PixelChange(unittest.TestCase):
    def test_identical_grids_report_no_change(self):
        change = gate.pixel_change(gate.probe_evidence(probe_log()))
        self.assertEqual(change["compared"], 6)
        self.assertEqual(change["changed"], [])

    def test_a_different_grid_names_the_points_that_moved(self):
        change = gate.pixel_change(
            gate.probe_evidence(probe_log(after="ffffff")))
        self.assertEqual(len(change["changed"]), 6)


def runtime_log(keys: int = 44, routed: int = 44) -> str:
    """The runtime's own record of what reached the island and was routed."""
    lines = ['OpenXaml input event=island-message name=setfocus island=0x60084 '
             'host=0x60080 host_visible=1 host_style=14cf0000',
             "OpenXaml input event=host-focus focused=1 was=0 sink=1"]
    for index in range(keys):
        lines.append("OpenXaml input event=key message=0x100 vk=65 down=1 "
                     "host_focused=1 sink=1 notified=1")
        lines.append("OpenXaml input event=char message=0x102 char=97 "
                     "host_focused=1 sink=1 notified=1")
        targets = 3 if index < routed else 0
        lines.append(f"OpenXaml input event=key-route vk=65 focused=1 "
                     f"targets={targets}")
        lines.append(f"OpenXaml input event=char-route char=97 focused=1 "
                     f"targets={targets}")
    return "\n".join(lines) + "\n"


class RuntimeEvidence(unittest.TestCase):
    def test_delivery_and_routing_are_counted_separately(self):
        runtime = gate.runtime_evidence(runtime_log(keys=4, routed=2))
        self.assertTrue(runtime["traced"])
        self.assertTrue(runtime["host_focus_gained"])
        self.assertEqual(runtime["keys"], 4)
        self.assertEqual(runtime["key_routes"], 4)
        self.assertEqual(runtime["routed_keys"], 2)

    def test_a_log_without_the_trace_says_so_rather_than_zero(self):
        runtime = gate.runtime_evidence("nothing here\n")
        self.assertFalse(runtime["traced"])
        self.assertEqual(runtime["keys"], 0)


class Evaluate(unittest.TestCase):
    def measured(self, **kwargs):
        sentinel_text = kwargs.pop("sentinel_text", "INPUT_GATE_1138\r\n")
        injecting = kwargs.pop("injecting", True)
        log = kwargs.pop("log", runtime_log())
        return gate.evaluate(log, probe_log(**kwargs), sentinel_text,
                             "INPUT_GATE_1138", injecting)

    def test_a_complete_round_trip_passes_with_the_pane_check_skipped(self):
        result = self.measured()
        self.assertTrue(result["success"], result["failed"])
        # The pane does not paint what was typed yet. That is a skip by name,
        # never a pass, and it flips to enforced the moment a pixel moves.
        self.assertEqual(result["skipped"], ["typed-text-changes-the-pane"])

    def test_a_painted_pane_is_enforced_rather_than_skipped(self):
        result = self.measured(after="ffffff")
        self.assertEqual(result["skipped"], [])
        self.assertTrue(result["success"], result["failed"])

    def test_no_sentinel_file_fails(self):
        result = self.measured(sentinel_text=None)
        self.assertFalse(result["success"])
        self.assertIn("sentinel-file-written", result["failed"])
        self.assertIn("sentinel-content-matches", result["failed"])

    def test_a_sentinel_with_the_wrong_bytes_fails(self):
        result = self.measured(sentinel_text="INPUT_GATE_9999\r\n")
        self.assertFalse(result["success"])
        self.assertIn("sentinel-content-matches", result["failed"])
        self.assertNotIn("sentinel-file-written", result["failed"])

    def test_an_invisible_hosting_window_fails_by_name(self):
        result = self.measured(visible=0)
        self.assertFalse(result["success"])
        self.assertIn("hosting-window-visible", result["failed"])
        detail = [check["detail"] for check in result["checks"]
                  if check["name"] == "hosting-window-visible"][0]
        self.assertIn("WS_VISIBLE", detail)

    def test_pixels_read_behind_an_unshown_window_are_never_a_pass(self):
        # Otherwise a changing desktop behind a window that is not on screen
        # would read as "the pane repainted what was typed".
        result = self.measured(visible=0, after="ffffff")
        self.assertIn("typed-text-changes-the-pane", result["skipped"])
        self.assertNotIn("typed-text-changes-the-pane",
                         [check["name"] for check in result["checks"]
                          if check["status"] == "pass"])

    def test_keystrokes_that_never_reached_the_island_fail_by_name(self):
        result = self.measured(log="")
        self.assertFalse(result["success"])
        self.assertIn("keystrokes-reached-the-island", result["failed"])
        self.assertIn("keystrokes-routed-to-an-element", result["failed"])

    def test_keystrokes_routed_to_nothing_fail_separately_from_delivery(self):
        result = self.measured(log=runtime_log(keys=44, routed=0))
        self.assertFalse(result["success"])
        self.assertNotIn("keystrokes-reached-the-island", result["failed"])
        self.assertIn("keystrokes-routed-to-an-element", result["failed"])

    def test_a_shell_that_never_started_fails(self):
        result = self.measured(ready="false")
        self.assertFalse(result["success"])
        self.assertIn("shell-started", result["failed"])

    def test_partly_injected_keystrokes_fail(self):
        result = self.measured(injected=10, expected=44)
        self.assertFalse(result["success"])
        self.assertIn("keystrokes-injected", result["failed"])

    def test_the_negative_control_requires_no_sentinel(self):
        result = self.measured(injecting=False, mechanism="none", injected=0,
                               expected=0, sentinel_text=None)
        self.assertTrue(result["success"], result["failed"])
        self.assertIn("no-sentinel-without-injection",
                      [check["name"] for check in result["checks"]])

    def test_a_negative_control_that_produced_a_sentinel_fails(self):
        result = self.measured(injecting=False, mechanism="none", injected=0,
                               expected=0, sentinel_text="INPUT_GATE_1138\r\n")
        self.assertFalse(result["success"])
        self.assertIn("no-sentinel-without-injection", result["failed"])

    def test_a_negative_control_that_injected_anyway_fails(self):
        result = self.measured(injecting=False, mechanism="none", injected=44,
                               expected=44, sentinel_text=None)
        self.assertFalse(result["success"])
        self.assertIn("no-keystrokes-injected", result["failed"])

    def test_the_negative_control_still_has_to_find_a_window_and_a_shell(self):
        # Otherwise "no sentinel appeared" would be true of a run in which
        # nothing happened at all, and would prove nothing about injection.
        result = self.measured(injecting=False, mechanism="none", injected=0,
                               expected=0, sentinel_text=None, ready="false")
        self.assertFalse(result["success"])
        self.assertIn("shell-started", result["failed"])


class SettingsDocument(unittest.TestCase):
    def test_the_profile_pins_the_shell_and_its_readiness_file(self):
        import json
        document = json.loads(
            gate.settings_document("1138", "C:\\shell_ready_1138.txt"))
        self.assertEqual(document["defaultProfile"], gate.PROFILE_GUID)
        profile = document["profiles"]["list"][0]
        self.assertEqual(profile["guid"], gate.PROFILE_GUID)
        self.assertIn("cmd.exe", profile["commandline"])
        self.assertIn("SHELL_READY_1138", profile["commandline"])
        self.assertIn("C:\\shell_ready_1138.txt", profile["commandline"])


class WindowsPaths(unittest.TestCase):
    def test_a_c_drive_path_maps_into_the_prefix(self):
        mapped = gate.windows_to_host(Path("/tmp/x/prefix"),
                                      "C:\\input_gate_1138.txt")
        self.assertEqual(mapped,
                         Path("/tmp/x/prefix/drive_c/input_gate_1138.txt"))

    def test_a_path_on_another_drive_is_refused_rather_than_guessed(self):
        with self.assertRaises(SystemExit):
            gate.windows_to_host(Path("/tmp/x/prefix"), "D:\\nope.txt")


if __name__ == "__main__":
    unittest.main()
