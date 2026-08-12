import importlib.util
import subprocess
import tempfile
import unittest
import unittest.mock
from pathlib import Path


SCRIPT = Path(__file__).resolve().parents[1] / "scripts" / "run_terminal_integration.py"
SPEC = importlib.util.spec_from_file_location("run_terminal_integration", SCRIPT)
integration = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
SPEC.loader.exec_module(integration)


class TerminalIntegrationResult(unittest.TestCase):
    def test_cli_default_uses_a_shell_present_in_wine(self):
        self.assertEqual(("cmd.exe", "/k"), integration.DEFAULT_TERMINAL_ARGUMENTS)

    def test_clean_bounded_timeout_is_success(self):
        log = (
            "OpenXaml frame event=commit reason=layout-invalidation "
            "generation=7 ready=true extent=640x480 transparency=false "
            "refusals=0 text_failures=0 render_issues=0\n"
            "OpenXaml frame event=present generation=7 ready=true "
            "extent=640x480 transparency=false refusals=0 "
            "text_failures=0 render_issues=0\n"
        )
        result = integration.evaluate(124, log, 30)
        self.assertTrue(result["success"])
        self.assertTrue(result["timed_out"])
        self.assertEqual(result["frame_backend"], "cpu")
        self.assertEqual(result["frame_extent"], "640x480")

    def test_timeout_without_authored_frame_is_not_success(self):
        result = integration.evaluate(124, "ordinary diagnostic\n", 30)
        self.assertFalse(result["success"])
        self.assertFalse(result["frame_committed"])

    def test_attach_probe_does_not_count_as_authored_frame(self):
        log = (
            "OpenXaml frame event=commit reason=attach generation=1 "
            "ready=true extent=1x1 transparency=true refusals=0 "
            "text_failures=0 render_issues=0\n"
        )
        self.assertFalse(integration.evaluate(124, log, 30)["success"])

    def test_dirty_cpu_frame_is_reported_but_rejected(self):
        log = (
            "OpenXaml frame event=commit reason=resize generation=3 "
            "ready=true extent=640x480 transparency=false refusals=2 "
            "text_failures=0 render_issues=0\n"
            "OpenXaml frame event=present generation=3 ready=true "
            "extent=640x480 transparency=false refusals=2 "
            "text_failures=0 render_issues=0\n"
        )
        result = integration.evaluate(124, log, 30)
        self.assertTrue(result["frame_committed"])
        self.assertTrue(result["frame_presented"])
        self.assertFalse(result["frame_clean"])
        self.assertEqual(result["frame_refusals"], 2)
        self.assertFalse(result["success"])

    def test_nonempty_dcomp_commit_is_presentation_evidence(self):
        log = (
            "OpenXaml frame event=commit reason=resize backend=dcomp "
            "generation=4 extent=640x480 refusals=0 render_issues=0 "
            "dcomp_issues=0\n"
            "OpenXaml frame event=scene-stats reason=resize backend=dcomp "
            "generation=4 nodes=12 visible_nodes=10 commands=8 fills=4 "
            "image_brushes=0 text=2 external=1\n"
        )
        result = integration.evaluate(124, log, 30)
        self.assertTrue(result["success"])
        self.assertEqual(result["frame_backend"], "dcomp")
        self.assertEqual(result["frame_nodes"], 12)
        self.assertEqual(result["frame_commands"], 8)

    def test_early_exit_is_not_a_live_success(self):
        result = integration.evaluate(0, "", 30)
        self.assertFalse(result["success"])
        self.assertFalse(result["timed_out"])

    def test_named_runtime_failures_override_timeout(self):
        samples = (
            'err:module:import_dll Library missing.dll (which is needed by L"app")',
            'RoGetActivationFactory Failed to find library for L"Missing.Class"',
            "terminate called after throwing an instance of 'hresult_error'",
            "OpenXaml: E_NOTIMPL Windows.UI.Xaml.Controls.Button.Focus",
            "OpenXaml: XBF object graph failed",
            "Unhandled exception 0xc0000005",
            "wine: Unhandled page fault on read access to 0000000000000000",
            "err:winediag:nodrv_CreateWindow no display driver",
        )
        for sample in samples:
            with self.subTest(sample=sample):
                self.assertFalse(integration.evaluate(124, sample, 30)["success"])

    def test_wine_paths_are_absolute_and_stable(self):
        self.assertEqual(integration.wine_path(Path("/tmp/a b/openxaml.dll")),
                         r"Z:\tmp\a b\openxaml.dll")


REQUIRED_RUNTIME_DLLS = (
    "libstdc++-6.dll", "libgcc_s_seh-1.dll", "libwinpthread-1.dll")


class MinGWRuntimeFinder(unittest.TestCase):
    """The finder must survive Debian's split posix-threads layout.

    On ubuntu-24.04 with g++-mingw-w64-x86-64-posix no single directory
    holds all three runtime DLLs: libstdc++-6.dll and libgcc_s_seh-1.dll
    live in the GCC library directory while libwinpthread-1.dll ships with
    the mingw-w64 sysroot. These tests run against fake trees with the
    compiler mocked, so they need no MinGW installation.
    """

    def setUp(self):
        self.root = Path(tempfile.mkdtemp(prefix="mingw-finder-"))
        self.addCleanup(lambda: __import__("shutil").rmtree(self.root))
        self.triple = "x86_64-w64-mingw32"
        self.compiler = self.root / "usr" / "bin" / f"{self.triple}-g++"
        self.compiler.parent.mkdir(parents=True)
        self.compiler.write_text("", encoding="utf-8")

    def place(self, directory: Path, *names: str) -> Path:
        directory.mkdir(parents=True, exist_ok=True)
        for name in names:
            (directory / name).write_bytes(b"MZ")
        return directory

    def locate(self, print_file_name_answers: dict[str, Path] | None = None):
        answers = print_file_name_answers or {}

        def fake_run(command, capture_output, text, check):
            self.assertEqual(command[0], str(self.compiler))
            argument = command[1]
            if argument == "-dumpmachine":
                output = self.triple
            elif argument.startswith("-print-file-name="):
                name = argument.split("=", 1)[1]
                output = str(answers.get(name, name))
            else:
                self.fail(f"unexpected compiler invocation: {command}")
            return subprocess.CompletedProcess(command, 0, f"{output}\n", "")

        with unittest.mock.patch.object(
                integration, "require_tool", return_value=str(self.compiler)), \
                unittest.mock.patch.object(
                    integration.subprocess, "run", side_effect=fake_run):
            return integration.find_mingw_runtime()

    def test_one_directory_with_all_dlls_stays_a_single_answer(self):
        bindir = self.place(self.root / "usr" / self.triple / "bin",
                            *REQUIRED_RUNTIME_DLLS)
        runtime = self.locate()
        self.assertEqual(runtime, [bindir.resolve()])

    def test_split_layout_returns_every_covering_directory_in_order(self):
        gcc_libdir = self.place(
            self.root / "usr" / "lib" / "gcc" / self.triple / "13-posix",
            "libstdc++-6.dll", "libgcc_s_seh-1.dll")
        sysroot_lib = self.place(self.root / "usr" / self.triple / "lib",
                                 "libwinpthread-1.dll")
        runtime = self.locate({
            "libstdc++-6.dll": gcc_libdir / "libstdc++-6.dll",
            "libgcc_s_seh-1.dll": gcc_libdir / "libgcc_s_seh-1.dll",
            "libwinpthread-1.dll": sysroot_lib / "libwinpthread-1.dll",
        })
        self.assertEqual(runtime, [gcc_libdir.resolve(), sysroot_lib.resolve()])

    def test_split_layout_is_found_without_compiler_answers(self):
        # A compiler whose -print-file-name only resolves what sits in its
        # own library directory: the sysroot fallback must still cover
        # libwinpthread-1.dll.
        gcc_libdir = self.place(
            self.root / "usr" / "lib" / "gcc" / self.triple / "13-posix",
            "libstdc++-6.dll", "libgcc_s_seh-1.dll")
        sysroot_lib = self.place(self.root / "usr" / self.triple / "lib",
                                 "libwinpthread-1.dll")
        runtime = self.locate({
            "libstdc++-6.dll": gcc_libdir / "libstdc++-6.dll",
            "libgcc_s_seh-1.dll": gcc_libdir / "libgcc_s_seh-1.dll",
        })
        self.assertEqual(runtime, [gcc_libdir.resolve(), sysroot_lib.resolve()])

    def test_missing_dll_is_refused_by_name(self):
        self.place(self.root / "usr" / self.triple / "bin",
                   "libstdc++-6.dll", "libgcc_s_seh-1.dll")
        with self.assertRaises(SystemExit) as refusal:
            self.locate()
        self.assertIn("libwinpthread-1.dll", str(refusal.exception))
        self.assertNotIn("libstdc++-6.dll", str(refusal.exception))

    def test_wine_search_path_joins_directories_with_semicolons(self):
        joined = integration.wine_search_path(
            [Path("/tmp/gcc libdir"), Path("/tmp/sysroot/lib")])
        self.assertEqual(joined, r"Z:\tmp\gcc libdir;Z:\tmp\sysroot\lib")

    def test_wine_search_path_of_one_directory_has_no_separator(self):
        self.assertEqual(integration.wine_search_path([Path("/tmp/runtime")]),
                         r"Z:\tmp\runtime")


if __name__ == "__main__":
    unittest.main()
