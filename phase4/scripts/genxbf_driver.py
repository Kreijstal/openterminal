#!/usr/bin/env python3
"""Compile XAML documents to XBF with the pinned SDK XAML compiler under Wine.

The XBF container is not documented; it is defined by whatever `genxbf.dll`
emits. This module is the minimal reusable way to obtain that ground truth:
hand it a directory of `.xaml` documents and it reports, per document, either
the produced `.xbf` or the compiler's own error text. Nothing is invented and
nothing is silently dropped.

Why an anchor project
---------------------
`genxbf.dll` is not callable on its own: its three exports (`Write`,
`WriteToStreams`, `Dump`) take COM metadata providers that the managed
`CompileXaml` MSBuild task supplies. So the markup has to go through that task,
and the task's second pass needs a project that has at least one `x:Class` page
and the local assembly that page's type lives in -- given a project of nothing
but classless pages it fails with

    Xaml Internal Error error WMC9999: Object reference not set to an instance
    of an object.

which was reproduced here against four corpus documents before this approach
was adopted. Rather than fabricate a class and a WinMD, this driver reuses the
project phase 2 already proved: `terminalapp-xaml.proj`, exactly as
`phase2/scripts/build_winui_xaml.py` wrote it, with the corpus documents
appended as classless `DefaultStyle` pages and the output directory retargeted.
TerminalApp's own pages recompile alongside and are ignored.

Every path this touches stays under /tmp: the harvested SDK, the Wine prefix,
the generated project and the produced XBF. None of it is ever committed.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

# The one XAML compiler this repository pins. A different SDK emits a different
# XBF version, and the loader refuses versions it was not written against, so a
# silent toolchain swap has to be visible here too.
PINNED_PLATFORM_VERSION = "10.0.26100.0"

DEFAULT_ROOT = Path("/tmp/openterminal-mingw")
DEFAULT_WINE_PREFIX = Path("/tmp/openterminal-xaml-wine-prefix")

MSBUILD = r"C:\windows\Microsoft.NET\Framework64\v4.0.30319\MSBuild.exe"


class ToolchainMissing(RuntimeError):
    """The harvested compiler is not on this machine.

    Raised rather than returned so a caller cannot mistake "the toolchain is
    absent" for "every document failed to compile".
    """


def wine_path(path: Path) -> str:
    return "Z:" + str(path).replace("/", "\\")


def toolchain(
    root: Path = DEFAULT_ROOT,
    wine_prefix: Path = DEFAULT_WINE_PREFIX,
    platform_version: str = PINNED_PLATFORM_VERSION,
) -> dict[str, Path]:
    """Locate every input the compile needs, or name the one that is missing."""
    sdk_root = root / "windows-sdk-cpp" / "extracted" / "c"
    required = {
        "anchor_project": root / "terminalapp-xaml.proj",
        "task_dll": sdk_root / "bin" / platform_version / "XamlCompiler"
        / "Microsoft.Windows.UI.Xaml.Build.Tasks.dll",
        "genxbf": sdk_root / "bin" / platform_version / "XamlCompiler" / "x64" / "genxbf.dll",
        "muxaml": root / "winui-metadata" / "Microsoft.UI.Xaml.winmd",
        "msbuild": wine_prefix / "drive_c" / "windows" / "Microsoft.NET" / "Framework64"
        / "v4.0.30319" / "MSBuild.exe",
    }
    missing = sorted(name for name, path in required.items() if not path.exists())
    if missing:
        raise ToolchainMissing(
            "harvested XAML compiler unavailable; missing: "
            + ", ".join(f"{name} ({required[name]})" for name in missing)
        )
    if shutil.which("wine") is None:
        raise ToolchainMissing("harvested XAML compiler unavailable; missing: wine on PATH")
    required["root"] = root
    required["wine_prefix"] = wine_prefix
    return required


# The output directory the anchor project was written with. Retargeting it is a
# plain string substitution because the phase-2 writer emits the same absolute
# path in every attribute that names it.
ANCHOR_OUTPUT = "terminalapp-xaml-compiler-output"


def write_project(project: Path, pages: list[Path], tools: dict[str, Path], output_dir: Path) -> None:
    anchor = tools["anchor_project"].read_text(encoding="utf-8")
    old_output = wine_path(tools["root"] / ANCHOR_OUTPUT)
    if old_output not in anchor:
        raise ToolchainMissing(
            f"anchor project {tools['anchor_project']} does not write to {old_output}; "
            "it was not produced by phase2/scripts/build_winui_xaml.py"
        )
    text = anchor.replace(old_output, wine_path(output_dir))
    injected = "".join(
        f'    <Page Include="{wine_path(page)}"><Link>{page.name}</Link>'
        f"<Type>DefaultStyle</Type></Page>\n"
        for page in pages
    )
    marker = "  <ItemGroup>\n"
    if marker not in text:
        raise ToolchainMissing("anchor project has no ItemGroup to inject pages into")
    text = text.replace(marker, marker + injected, 1)
    project.write_text(text, encoding="utf-8", newline="\n")


# MSBuild reports a XAML diagnostic as "<file>(line,col): XAML error WMC0011: ...".
_DIAGNOSTIC = re.compile(
    r"^(?P<file>[A-Za-z]:\\[^(]+)\((?P<line>\d+),(?P<column>\d+)\):\s*"
    r"(?P<origin>[\w ]*?)\s*(?P<severity>error|warning) (?P<code>\w+):\s*(?P<message>.*?)\s*$",
    re.MULTILINE,
)


def parse_diagnostics(log: str) -> dict[str, list[str]]:
    """Group the compiler's own error text by the page it was reported on."""
    per_page: dict[str, list[str]] = {}
    for match in _DIAGNOSTIC.finditer(log):
        if match.group("severity") != "error":
            continue
        name = match.group("file").rsplit("\\", 1)[-1]
        message = f"{match.group('code')}: {match.group('message')}"
        per_page.setdefault(name, [])
        if message not in per_page[name]:
            per_page[name].append(message)
    return per_page


def compile_directory(
    pages_dir: Path,
    output_dir: Path,
    *,
    tools: dict[str, Path] | None = None,
    batch: int = 120,
    verbose: bool = False,
) -> dict[str, object]:
    """Compile every .xaml under `pages_dir`; return the per-document outcome.

    Documents go in batches because one rejected page fails the whole task
    invocation. A batch that fails is retried one page at a time, so an error is
    attributed to the page that caused it rather than to its neighbours.
    """
    tools = tools or toolchain()
    pages = sorted(pages_dir.glob("*.xaml"))
    if not pages:
        raise RuntimeError(f"no .xaml documents under {pages_dir}")
    output_dir.mkdir(parents=True, exist_ok=True)

    env = os.environ.copy()
    env["WINEPREFIX"] = str(tools["wine_prefix"])
    env["WINEDEBUG"] = "-all"

    produced: dict[str, Path] = {}
    errors: dict[str, list[str]] = {}

    def run_batch(subset: list[Path], work: Path) -> tuple[bool, str]:
        work.mkdir(parents=True, exist_ok=True)
        project = work / "corpus-xbf.proj"
        write_project(project, subset, tools, work)
        log = ""
        for target in ("Pass1", "Pass2"):
            completed = subprocess.run(
                ["wine", MSBUILD, wine_path(project), "/verbosity:normal", f"/target:{target}"],
                env=env,
                capture_output=True,
                text=True,
                errors="replace",
            )
            log += completed.stdout + completed.stderr
            if completed.returncode != 0:
                return False, log
        return True, log

    scratch = output_dir / "work"
    shutil.rmtree(scratch, ignore_errors=True)
    index = 0
    batch_number = 0
    while index < len(pages):
        subset = pages[index : index + batch]
        index += batch
        batch_number += 1
        work = scratch / f"batch{batch_number}"
        ok, log = run_batch(subset, work)
        blamed = parse_diagnostics(log)
        if ok:
            for page in subset:
                xbf = work / (page.stem + ".xbf")
                if xbf.exists():
                    shutil.move(str(xbf), output_dir / xbf.name)
                    produced[page.name] = output_dir / xbf.name
                else:
                    errors[page.name] = blamed.get(
                        page.name, ["no XBF emitted and no error reported"]
                    )
            if verbose:
                print(f"batch {batch_number}: {len(subset)} pages ok", file=sys.stderr)
            shutil.rmtree(work, ignore_errors=True)
            continue
        if verbose:
            print(f"batch {batch_number} of {len(subset)} failed; retrying individually",
                  file=sys.stderr)
        shutil.rmtree(work, ignore_errors=True)
        for position, page in enumerate(subset):
            single = scratch / f"single{batch_number}-{position}"
            ok_one, log_one = run_batch([page], single)
            xbf = single / (page.stem + ".xbf")
            if ok_one and xbf.exists():
                shutil.move(str(xbf), output_dir / xbf.name)
                produced[page.name] = output_dir / xbf.name
            else:
                messages = parse_diagnostics(log_one).get(page.name) or blamed.get(page.name)
                errors[page.name] = messages or ["compiler failed without a XAML diagnostic"]
            shutil.rmtree(single, ignore_errors=True)
    shutil.rmtree(scratch, ignore_errors=True)

    return {
        "platform_version": PINNED_PLATFORM_VERSION,
        "compiled": {name: str(path) for name, path in sorted(produced.items())},
        "errors": {name: messages for name, messages in sorted(errors.items())},
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pages", type=Path, help="directory of .xaml documents")
    parser.add_argument("output", type=Path, help="directory to write .xbf into")
    parser.add_argument("--report", type=Path, help="write the outcome table as JSON")
    parser.add_argument("--batch", type=int, default=120)
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()

    try:
        tools = toolchain()
    except ToolchainMissing as error:
        print(str(error), file=sys.stderr)
        return 3
    result = compile_directory(
        args.pages, args.output, tools=tools, batch=args.batch, verbose=args.verbose
    )
    text = json.dumps(result, indent=2, sort_keys=True)
    if args.report:
        args.report.write_text(text + "\n", encoding="utf-8")
    total = len(result["compiled"]) + len(result["errors"])
    print(f"compiled {len(result['compiled'])} of {total} documents")
    for name, messages in result["errors"].items():
        print(f"  rejected {name}: {'; '.join(messages)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
