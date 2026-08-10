#!/usr/bin/env python3
"""Prove that a keystroke typed into the real terminal window reaches the shell.

Every gate before this one stops at the window: the boot gate asks whether the
process lives, the visible-UI gate asks whether the application's own markup is
on the screen, and the ConPTY gate asks whether a pseudoconsole's bytes reach
TerminalCore in a process with no window at all. None of them touches the
direction that matters to a person using the thing -- from the keyboard, through
the host's message loop, the XAML island, the focused control, the connection,
the pseudoconsole, and into the shell.

This gate measures that direction, and it measures it by *effect* rather than by
appearance. The pane is not required to paint: what is typed is a shell command
that writes a file, and the check is that the file exists with the bytes the
command would have produced. Nothing but the shell can have written it -- the
harness never runs a shell itself, and the injected keystrokes are the only way
the characters reach one.

Three facts are collected from one launch:

  1. the shell's own readiness file, created by the profile's command line, so
     the moment of typing is chosen by the shell having started rather than by
     a clock;
  2. a separate Win32 process's record of the window it found, the focus it
     saw, the keystrokes it injected and the desktop pixels inside the window
     before and after typing;
  3. the sentinel file the typed command would have written, read from the
     prefix afterwards.

The negative control (``--mechanism none``) runs the identical session with the
injection step removed and must produce no sentinel. Without that this gate
would pass for a prefix in which something else wrote the file.

The pixel-level question -- does the pane show what was typed -- is answered by
comparing the probe's two grids. It is enforced when the pane paints anything
at all in response, and *skipped by name* when the measurement shows the pane
is blank both before and after, which is what a runtime whose SwapChainPanel
pane has no content yet actually produces. It is also skipped, by a different
name, when the hosting window is not on screen at all: the grid is read from
the desktop, so those pixels would not be the pane's.

Two injection mechanisms are offered because which one a Wine/Xvfb desktop
delivers is a measurement, not an assumption. ``sendinput`` exercises the whole
path -- desktop input queue, foreground and focus, the host's message loop --
and is what a person's keyboard does. ``postmessage`` posts the same messages
straight to the XAML island's child window, skipping the desktop queue, so a
run that succeeds only that way says the loss is above the island rather than
inside XAML. ``none`` is the negative control.

    python3 -B phase4/scripts/check_input_roundtrip.py \\
        --xaml-dll /tmp/openterminal-xamlcore/openxaml.dll \\
        --executable /tmp/openterminal-mingw/native-build/WindowsTerminal.exe \\
        --input-probe /tmp/openterminal-xamlcore/terminal_input_probe.exe \\
        --prefix /tmp/openterminal-input/prefix

Wine refuses a prefix whose immediate parent is not the user's, so the prefix
must be nested rather than sitting directly under /tmp.
"""

from __future__ import annotations

import argparse
import getpass
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPOSITORY / "phase3" / "scripts"))
sys.path.insert(0, str(REPOSITORY / "phase4" / "scripts"))

# The profile the gate types into. A fresh prefix has no PowerShell and Wine's
# dynamic profile generators find nothing, so which shell runs has to be pinned
# rather than discovered -- otherwise the command typed below would be typed
# into whatever happened to start. This is environment provisioning of the same
# kind as installing the fonts Terminal deploys.
PROFILE_GUID = "{9a8b7c6d-5e4f-4a3b-2c1d-0e9f8a7b6c5d}"

INPUT_WINDOW = re.compile(
    r"input window found=(true|false)(?: hwnd=(\S+) class=(\S+) "
    r"client=(\d+)x(\d+) visible=(\d))? attempts=(\d+)")
INPUT_ISLAND = re.compile(r"input island hwnd=(\S+) class=(\S+)")
INPUT_MECHANISM = re.compile(
    r"input mechanism=(\S+) text_length=(\d+) enter=(\d)")
INPUT_READY = re.compile(r"input shell_ready=(true|false) waits=(\d+)")
INPUT_FOCUS = re.compile(r"input focus thread=(\d+) hwnd=(\S+) class=(\S+)")
INPUT_FOREGROUND = re.compile(
    r"input foreground requested=(\d) before=(\S+) before_class=(\S+) "
    r"after=(\S+) after_class=(\S+)")
INPUT_KEY = re.compile(
    r"input key index=(\d+) char=(\d+) vk=(\d+) down=(\d+) up=(\d+)")
INPUT_DONE = re.compile(r"input done injected=(\d+)(?: of=(\d+))?")
INPUT_CHILD = re.compile(
    r"input child depth=(\d+) hwnd=(\S+) class=(\S+) visible=(\d) rect=(\S+)")
INPUT_PIXEL = re.compile(
    r"input pixel phase=(before|after) index=(\d+) x=(-?\d+) y=(-?\d+) "
    r"rgb=(none|[0-9a-f]{6})")

# A loader that aborts the process at a function it does not implement never
# lets the window appear; that is a property of the loader, named rather than
# read as a statement about input routing.
LOADER_GAP = re.compile(r"[Uu]nimplemented function (\S+) called")

# The runtime's own record of the input path, written under
# OPENXAML_TRACE_INPUT. Between them these say whether a keystroke reached the
# island's window procedure at all, and whether the island had anyone to route
# it to -- three different failures that all look like "nothing happened".
RUNTIME_HOST_FOCUS = re.compile(
    r"OpenXaml input event=host-focus focused=(\d) was=(\d) sink=(\d)")
RUNTIME_KEY = re.compile(
    r"OpenXaml input event=key message=0x([0-9a-f]+) vk=(\d+) down=(\d) "
    r"host_focused=(\d) sink=(\d) notified=(\d)")
RUNTIME_CHAR = re.compile(
    r"OpenXaml input event=char message=0x([0-9a-f]+) char=(\d+) "
    r"host_focused=(\d) sink=(\d) notified=(\d)")
RUNTIME_KEY_ROUTE = re.compile(
    r"OpenXaml input event=key-route vk=(\d+) focused=(\d) targets=(\d+)")
RUNTIME_CHAR_ROUTE = re.compile(
    r"OpenXaml input event=char-route char=(\d+) focused=(\d) targets=(\d+)")

PANE_PAINT_NOTE = ("the pane's own pixels are not painted yet; this check "
                   "flips to enforced by itself as soon as typing changes any "
                   "sampled pixel")


def probe_evidence(text: str) -> dict:
    """What the injecting process says it found and did."""
    window = INPUT_WINDOW.search(text)
    island = INPUT_ISLAND.search(text)
    mechanism = INPUT_MECHANISM.search(text)
    ready = INPUT_READY.search(text)
    focus = INPUT_FOCUS.search(text)
    foreground = INPUT_FOREGROUND.search(text)
    done = INPUT_DONE.search(text)
    keys = [{"index": int(index), "char": int(character), "vk": int(vk),
             "down": int(down), "up": int(up)}
            for index, character, vk, down, up in INPUT_KEY.findall(text)]
    children = [{"depth": int(depth), "hwnd": hwnd, "class": name,
                 "visible": visible == "1", "rect": rect}
                for depth, hwnd, name, visible, rect in INPUT_CHILD.findall(text)]
    pixels: dict[str, dict[int, str | None]] = {"before": {}, "after": {}}
    for phase, index, _x, _y, rgb in INPUT_PIXEL.findall(text):
        pixels[phase][int(index)] = None if rgb == "none" else rgb
    return {
        "mechanism": mechanism.group(1) if mechanism else None,
        "text_length": int(mechanism.group(2)) if mechanism else 0,
        "enter": bool(mechanism) and mechanism.group(3) == "1",
        "window_found": bool(window) and window.group(1) == "true",
        "window_class": window.group(3) if window and window.group(3) else None,
        "client": ([int(window.group(4)), int(window.group(5))]
                   if window and window.group(4) else None),
        "window_visible": bool(window) and window.group(6) == "1",
        "attempts": int(window.group(7)) if window else 0,
        "island_class": island.group(2) if island else None,
        "island_hwnd": island.group(1) if island else None,
        "shell_ready": bool(ready) and ready.group(1) == "true",
        "ready_waits": int(ready.group(2)) if ready else 0,
        "focus_class": focus.group(3) if focus else None,
        "focus_hwnd": focus.group(2) if focus else None,
        "foreground_class": foreground.group(5) if foreground else None,
        "foreground_requested": bool(foreground) and foreground.group(1) == "1",
        "children": children,
        "keys": keys,
        "injected": int(done.group(1)) if done else 0,
        "expected_keys": int(done.group(2)) if done and done.group(2) else 0,
        "finished": bool(done),
        "pixels": pixels,
    }


def runtime_evidence(log: str) -> dict:
    """What the runtime says arrived at the island and where it was routed."""
    keys = RUNTIME_KEY.findall(log)
    characters = RUNTIME_CHAR.findall(log)
    key_routes = RUNTIME_KEY_ROUTE.findall(log)
    character_routes = RUNTIME_CHAR_ROUTE.findall(log)
    focus = RUNTIME_HOST_FOCUS.findall(log)
    return {
        "traced": bool(focus or keys or characters),
        "host_focus_gained": any(gained == "1" for gained, _was, _sink in focus),
        "keys": len(keys),
        "characters": len(characters),
        "keys_with_a_sink": sum(1 for record in keys if record[5] == "1"),
        "characters_with_a_sink": sum(1 for record in characters
                                      if record[4] == "1"),
        "routed_keys": sum(1 for _vk, _focused, targets in key_routes
                           if int(targets) > 0),
        "routed_characters": sum(1 for _char, _focused, targets
                                 in character_routes if int(targets) > 0),
        "key_routes": len(key_routes),
        "character_routes": len(character_routes),
    }


def pixel_change(probe: dict) -> dict:
    """How many sampled points differ between before and after typing."""
    before = probe["pixels"]["before"]
    after = probe["pixels"]["after"]
    shared = sorted(set(before) & set(after))
    changed = [index for index in shared if before[index] != after[index]]
    distinct = sorted({value for value in list(before.values()) +
                       list(after.values()) if value is not None})
    return {"compared": len(shared), "changed": changed,
            "distinct_colours": distinct}


def evaluate(log: str, probe_output: str, sentinel_text: str | None,
             sentinel: str, injecting: bool) -> dict:
    probe = probe_evidence(probe_output)
    runtime = runtime_evidence(log)
    pixels = pixel_change(probe)
    checks: list[dict] = []

    def record(name: str, passed: bool, detail: str) -> None:
        checks.append({"name": name, "status": "pass" if passed else "fail",
                       "detail": detail})

    def skip(name: str, detail: str) -> None:
        checks.append({"name": name, "status": "skip", "detail": detail})

    loader_gaps = sorted(set(LOADER_GAP.findall(log)))

    record("probe-observed-window", probe["window_found"],
           f"found={probe['window_found']} class={probe['window_class']} "
           f"client={probe['client']} attempts={probe['attempts']} "
           f"loader_gaps={loader_gaps}")

    # Separate from the one above on purpose. A window that exists but was
    # never shown is exactly what a starved ShowWindow looks like, and it is
    # the one condition under which the desktop's own input queue cannot
    # deliver a keystroke to this process at all.
    record("hosting-window-visible", probe["window_visible"],
           f"visible={probe['window_visible']} client={probe['client']}; a "
           "window without WS_VISIBLE can neither take the foreground nor "
           "receive injected keyboard input")

    record("island-window-present", probe["island_class"] not in
           (None, "(null)", "(unknown)"),
           f"island={probe['island_class']} hwnd={probe['island_hwnd']}")

    record("shell-started", probe["shell_ready"],
           f"the profile's own command line created its readiness file: "
           f"ready={probe['shell_ready']} waits={probe['ready_waits']}")

    record("island-window-focused",
           probe["focus_class"] is not None and
           probe["focus_class"] not in ("(null)", "(unknown)"),
           f"focus={probe['focus_class']} hwnd={probe['focus_hwnd']} "
           f"foreground={probe['foreground_class']} "
           f"children={[child['class'] for child in probe['children']]}")

    if injecting:
        record("keystrokes-injected",
               probe["finished"] and probe["expected_keys"] > 0 and
               probe["injected"] == probe["expected_keys"],
               f"injected={probe['injected']} of={probe['expected_keys']} "
               f"mechanism={probe['mechanism']}")
        # Where a keystroke that produced no sentinel actually died. The
        # runtime writes these itself, so "never delivered" and "delivered and
        # dropped" are different answers rather than the same silence.
        record("keystrokes-reached-the-island",
               runtime["keys"] > 0 or runtime["characters"] > 0,
               f"island received keys={runtime['keys']} "
               f"characters={runtime['characters']} "
               f"(with a focused sink: {runtime['keys_with_a_sink']}/"
               f"{runtime['characters_with_a_sink']}); host focus gained="
               f"{runtime['host_focus_gained']}, runtime trace present="
               f"{runtime['traced']}")
        record("keystrokes-routed-to-an-element",
               runtime["routed_keys"] > 0 or runtime["routed_characters"] > 0,
               f"routed keys={runtime['routed_keys']}/{runtime['key_routes']} "
               f"characters={runtime['routed_characters']}/"
               f"{runtime['character_routes']}; a route of zero targets means "
               "nothing in the visual tree had XAML focus")

        record("sentinel-file-written", sentinel_text is not None,
               "the typed command's output file "
               + ("exists" if sentinel_text is not None else "does not exist"))
        record("sentinel-content-matches",
               sentinel_text is not None and sentinel in sentinel_text,
               f"expected={sentinel!r} read={sentinel_text!r}")
    else:
        # The negative control. Everything above still has to hold -- a control
        # that never found the window or never started a shell proves nothing
        # about the injection path it removed.
        record("no-keystrokes-injected",
               probe["finished"] and probe["injected"] == 0,
               f"injected={probe['injected']} mechanism={probe['mechanism']}")
        record("no-sentinel-without-injection", sentinel_text is None,
               "with the injection step removed the sentinel file "
               + ("does not exist, as it must not"
                  if sentinel_text is None
                  else f"exists anyway and reads {sentinel_text!r}; this gate "
                       "is not measuring the input path"))

    if injecting:
        if not probe["window_visible"]:
            # The grid is read from the desktop. With the window not on screen
            # those pixels belong to whatever is behind it, so a difference
            # between the two readings says nothing about the pane.
            skip("typed-text-changes-the-pane",
                 "the hosting window is not on screen, so the sampled desktop "
                 f"pixels are not the pane's; measured {pixels['compared']} "
                 f"points, {len(pixels['changed'])} of them different")
        elif pixels["compared"] == 0:
            record("typed-text-changes-the-pane", False,
                   "no pixel grid was read at all, so nothing can be said "
                   "about what the pane shows")
        elif pixels["changed"]:
            record("typed-text-changes-the-pane", True,
                   f"{len(pixels['changed'])} of {pixels['compared']} sampled "
                   f"points changed after typing: {pixels['changed'][:8]}")
        else:
            skip("typed-text-changes-the-pane",
                 f"{PANE_PAINT_NOTE}; measured {pixels['compared']} points, "
                 f"none changed, colours seen {pixels['distinct_colours']}")

    failures = [check for check in checks if check["status"] == "fail"]
    return {
        "checks": checks,
        "failed": [check["name"] for check in failures],
        "skipped": [check["name"] for check in checks
                    if check["status"] == "skip"],
        "loader_gaps": loader_gaps,
        "pixels": pixels,
        "runtime": runtime,
        "probe": {key: probe[key] for key in
                  ("mechanism", "window_found", "window_visible",
                   "window_class", "client", "attempts", "shell_ready",
                   "ready_waits", "focus_class", "foreground_class",
                   "island_class", "island_hwnd", "injected", "expected_keys",
                   "finished", "children")},
        "sentinel_text": sentinel_text,
        "success": not failures,
    }


def settings_document(nonce: str, ready_path: str) -> str:
    """A settings file that pins which shell the gate types into."""
    command = f"cmd.exe /k echo SHELL_READY_{nonce}>{ready_path}"
    document = {
        "$schema": "https://aka.ms/terminal-profiles-schema",
        "defaultProfile": PROFILE_GUID,
        "copyOnSelect": False,
        "profiles": {
            "defaults": {},
            "list": [
                {
                    "guid": PROFILE_GUID,
                    "name": "input-roundtrip-gate",
                    "commandline": command,
                    "startingDirectory": "C:\\",
                    "hidden": False,
                    "historySize": 9001,
                },
            ],
        },
        "schemes": [],
        "actions": [],
    }
    return json.dumps(document, indent=4) + "\n"


def provision_settings(prefix: Path, nonce: str, ready_path: str) -> Path:
    """Write the settings file Terminal reads in an unpackaged prefix."""
    user = os.environ.get("USER") or getpass.getuser()
    directory = (prefix / "drive_c" / "users" / user / "AppData" / "Local" /
                 "Microsoft" / "Windows Terminal")
    directory.mkdir(parents=True, exist_ok=True)
    settings = directory / "settings.json"
    settings.write_text(settings_document(nonce, ready_path), encoding="utf-8",
                        newline="\n")
    return settings


def windows_to_host(prefix: Path, windows_path: str) -> Path:
    """Where a C:\\... path written by the shell lands on this filesystem."""
    if not windows_path.upper().startswith("C:\\"):
        raise SystemExit(f"only C: paths can be read back: {windows_path}")
    return prefix / "drive_c" / windows_path[3:].replace("\\", "/")


def launch(dll: Path, executable: Path, xbf_root: Path, prefix: Path,
           input_probe: Path, timeout_seconds: int, log_path: Path,
           probe_log_path: Path, wine: str, runtime_font_spec: Path,
           runtime_font_cache: Path, mechanism: str, typed_text: str,
           ready_windows_path: str, nonce: str,
           settle_ms: int) -> tuple[str, str, dict]:
    import run_terminal_integration as integration
    from build_xamlcore import registration
    from check_xbf_ui_render import provision_environment_registry
    from install_deployment_fonts import install as install_deployment_fonts
    from prepare_runtime_fonts import prepare as prepare_runtime_fonts

    integration.require_tool("timeout")
    runtime = integration.find_mingw_runtime()

    environment = dict(os.environ)
    environment["WINEPREFIX"] = str(prefix)
    environment["WINEARCH"] = "win64"
    environment["WINEDEBUG"] = "err+all,warn+debugstr,fixme+combase"
    environment["WINEDLLOVERRIDES"] = "winedbg.exe=d"
    environment["WINEPATH"] = integration.wine_path(runtime)
    environment["OPENXAML_XBF_ROOT"] = integration.wine_path(xbf_root)
    environment["OPENXAML_TRACE_FRAMES"] = "1"
    environment["OPENXAML_TRACE_INPUT"] = "1"
    font_alias_manifest = prepare_runtime_fonts(
        runtime_font_spec, runtime_font_cache, "wine")
    environment["OPENXAML_FONT_ALIAS_MANIFEST"] = integration.wine_path(
        font_alias_manifest)

    subprocess.run([wine, "wineboot.exe", "-u"], env=environment, check=True)
    registry_file = prefix / "openxaml-input-roundtrip.reg"
    registry_file.write_text(registration(dll), encoding="utf-8", newline="\n")
    subprocess.run([wine, "regedit.exe", str(registry_file)], env=environment,
                   check=True)
    provisioned = provision_environment_registry(prefix, environment, wine)
    fonts = install_deployment_fonts(executable.parent, prefix, environment, wine)
    settings = provision_settings(prefix, nonce, ready_windows_path)

    # The probe starts first and outlives the launch: it has to be watching
    # before the window exists, and it decides when to type from the shell's
    # own readiness file rather than from a clock.
    probe_command = [wine, str(input_probe),
                     "--deadline-ms", str(max(1000, timeout_seconds * 1000 - 4000)),
                     "--settle-ms", str(settle_ms),
                     "--mechanism", mechanism,
                     "--text", typed_text,
                     "--wait-file", ready_windows_path,
                     "--enter"]
    probe_process = subprocess.Popen(
        probe_command, env=environment, stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, text=True, errors="replace")
    terminal = subprocess.run(
        ["timeout", "--signal=TERM", "--kill-after=5", str(timeout_seconds),
         wine, "./" + executable.name],
        env=environment, cwd=executable.parent, capture_output=True, text=True,
        errors="replace", check=False)
    probe_output, _ = probe_process.communicate(timeout=120)

    log = terminal.stderr + terminal.stdout
    log_path.parent.mkdir(parents=True, exist_ok=True)
    log_path.write_text(log, encoding="utf-8", errors="replace", newline="\n")
    probe_log_path.write_text(probe_output, encoding="utf-8", errors="replace",
                              newline="\n")
    return log, probe_output, {
        "deployment_fonts": [font["file"] for font in fonts["fonts"]],
        "provisioned_registry_values": provisioned,
        "settings": str(settings),
        "probe_command": probe_command,
        "terminal_exit_code": terminal.returncode,
        "wine_loader": wine,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--xaml-dll", required=True, type=Path)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--input-probe", required=True, type=Path,
                        help="the built terminal_input_probe.exe")
    parser.add_argument("--xbf-root", type=Path)
    parser.add_argument("--prefix", type=Path)
    parser.add_argument("--timeout", type=int, default=90)
    parser.add_argument("--settle-ms", type=int, default=4000,
                        help="pause between the shell's readiness file and the "
                             "first keystroke")
    parser.add_argument("--mechanism", default="sendinput",
                        choices=("sendinput", "postmessage", "none"),
                        help="'none' is the negative control: find the window, "
                             "wait for the shell, type nothing")
    parser.add_argument("--nonce", default="1138")
    parser.add_argument("--wine-loader", type=Path)
    parser.add_argument("--wineserver", type=Path)
    parser.add_argument("--log", type=Path)
    parser.add_argument("--probe-log", type=Path)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--runtime-font-spec", type=Path)
    parser.add_argument("--runtime-font-cache", type=Path,
                        default=Path(tempfile.gettempdir()) /
                                "openterminal-runtime-fonts")
    parser.add_argument("--no-xvfb", action="store_true")
    arguments = parser.parse_args()
    if arguments.timeout <= 0:
        raise SystemExit("--timeout must be positive")

    if not arguments.no_xvfb and not os.environ.get("DISPLAY"):
        xvfb_run = shutil.which("xvfb-run")
        if not xvfb_run:
            raise SystemExit("required tool is not on PATH: xvfb-run")
        command = [xvfb_run, "--auto-servernum", sys.executable, "-B",
                   str(Path(__file__).resolve()), *sys.argv[1:], "--no-xvfb"]
        return subprocess.run(command, check=False).returncode

    import run_terminal_integration as integration
    from prepare_runtime_fonts import DEFAULT_SPEC as DEFAULT_RUNTIME_FONT_SPEC

    dll = integration.require_file(arguments.xaml_dll, "OpenXaml DLL")
    executable = integration.require_file(arguments.executable,
                                          "Terminal executable")
    input_probe = integration.require_file(arguments.input_probe, "input probe")
    xbf_root = (arguments.xbf_root or executable.parent).resolve()
    if not xbf_root.is_dir():
        raise SystemExit(f"XBF root does not exist: {xbf_root}")
    runtime_font_spec = integration.require_file(
        arguments.runtime_font_spec or DEFAULT_RUNTIME_FONT_SPEC,
        "runtime font specification")
    wine = (str(integration.require_file(arguments.wine_loader, "Wine loader"))
            if arguments.wine_loader else integration.require_tool("wine"))
    wineserver = (str(integration.require_file(arguments.wineserver, "wineserver"))
                  if arguments.wineserver else integration.require_tool("wineserver"))

    temporary: tempfile.TemporaryDirectory[str] | None = None
    if arguments.prefix:
        prefix = integration.ensure_fresh_prefix(arguments.prefix)
    else:
        temporary = tempfile.TemporaryDirectory(prefix="openterminal-input-")
        prefix = Path(temporary.name).resolve()
    log_path = (arguments.log.resolve() if arguments.log
                else prefix / "input-roundtrip.log")
    probe_log_path = (arguments.probe_log.resolve() if arguments.probe_log
                      else prefix / "input-roundtrip-probe.log")

    nonce = arguments.nonce
    sentinel = f"INPUT_GATE_{nonce}"
    sentinel_windows_path = f"C:\\input_gate_{nonce}.txt"
    ready_windows_path = f"C:\\shell_ready_{nonce}.txt"
    # What a person would type to make the shell say the sentinel out loud.
    # No spaces around the redirect: cmd would otherwise write the space too.
    typed_text = f"echo {sentinel}>{sentinel_windows_path}"
    injecting = arguments.mechanism != "none"

    started = time.time()
    try:
        log, probe_output, launch_facts = launch(
            dll, executable, xbf_root, prefix, input_probe, arguments.timeout,
            log_path, probe_log_path, wine, runtime_font_spec,
            arguments.runtime_font_cache.resolve(), arguments.mechanism,
            typed_text, ready_windows_path, nonce, arguments.settle_ms)
        sentinel_file = windows_to_host(prefix, sentinel_windows_path)
        sentinel_text = (sentinel_file.read_text(encoding="utf-8",
                                                 errors="replace")
                         if sentinel_file.is_file() else None)
        result = evaluate(log, probe_output, sentinel_text, sentinel, injecting)
        result.update(launch_facts)
        result.update({
            "elapsed_seconds": round(time.time() - started, 3),
            "executable": str(executable),
            "executable_sha256": integration.sha256(executable),
            "log": str(log_path),
            "mechanism": arguments.mechanism,
            "openxaml_dll": str(dll),
            "openxaml_dll_sha256": integration.sha256(dll),
            "prefix": str(prefix),
            "probe_log": str(probe_log_path),
            "ready_file": ready_windows_path,
            "sentinel": sentinel,
            "sentinel_file": sentinel_windows_path,
            "typed_text": typed_text,
            "xbf_root": str(xbf_root),
        })
        text = json.dumps(result, indent=2, sort_keys=True) + "\n"
        if arguments.report:
            arguments.report.write_text(text, encoding="utf-8", newline="\n")
        sys.stdout.write(text)
        for check in result["checks"]:
            if check["status"] != "pass":
                print(f"{check['status'].upper()}: {check['name']} -- "
                      f"{check['detail']}", file=sys.stderr)
        return 0 if result["success"] else 1
    finally:
        subprocess.run([wineserver, "-k"],
                       env={**os.environ, "WINEPREFIX": str(prefix)}, check=False,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if temporary:
            temporary.cleanup()


if __name__ == "__main__":
    raise SystemExit(main())
