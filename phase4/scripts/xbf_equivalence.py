#!/usr/bin/env python3
"""Hold the XBF loader to the text-XAML path over the whole corpus.

The corpus is ~1087 markup documents whose layout results are already verified
against recorded oracle measurements. The same markup can reach the runtime two
ways: as text through `XamlReader`, or compiled by the SDK's `genxbf.dll` and
read back by our XBF loader. Both must produce the same tree of numbers, byte
for byte. That makes a self-checking gate out of data that already exists: no
new oracle run, no new expectations to bless, and no way for a decode mistake
to look like a pass.

Documents `genxbf` itself rejects are not skipped -- they are recorded with the
compiler's own error text, which is the only honest description of why the two
paths cannot be compared for them.

Steps:
  1. export each case's markup to /tmp as a .xaml document;
  2. compile them with the pinned compiler under Wine (genxbf_driver);
  3. run the native `xbf_equivalence` tool, which measures each case from the
     text markup and from the .xbf and diffs the two;
  4. run it a second time and require both runs agree, so a pass cannot come
     from an unstable decode.
"""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import genxbf_driver

REPO = Path(__file__).resolve().parents[2]
DEFAULT_CASES = REPO / "phase3" / "xaml-db" / "cases"
# The metrics the corpus solved for itself, which are committed. The harvested
# ones are a CI artifact; either directory works, and mixing them is refused.
DEFAULT_FONTS = REPO / "phase3" / "xaml-db" / "fonts" / "derived"
DEFAULT_WORK = Path("/tmp/openterminal-xbf-gate")


def export_cases(cases_dir: Path, pages_dir: Path) -> dict[str, Path]:
    """Write every case's markup out as a .xaml document named for its id."""
    if pages_dir.exists():
        shutil.rmtree(pages_dir)
    pages_dir.mkdir(parents=True)
    exported: dict[str, Path] = {}
    for case_file in sorted(cases_dir.glob("*/*.json")):
        case = json.loads(case_file.read_text(encoding="utf-8"))
        page = pages_dir / f"{case['id']}.xaml"
        page.write_text(case["markup"], encoding="utf-8", newline="\n")
        exported[case["id"]] = page
    if not exported:
        raise SystemExit(
            f"no cases under {cases_dir}; run python3 -B phase3/scripts/generate_cases.py first"
        )
    return exported


def run_gate(tool: Path, cases_dir: Path, xbf_dir: Path, fonts: Path) -> dict:
    completed = subprocess.run(
        [str(tool), str(cases_dir), str(xbf_dir), str(fonts)], capture_output=True, text=True
    )
    if completed.returncode not in (0, 1):
        raise SystemExit(
            f"{tool} failed ({completed.returncode}):\n{completed.stdout}\n{completed.stderr}"
        )
    return json.loads(completed.stdout)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cases", type=Path, default=DEFAULT_CASES)
    parser.add_argument("--fonts", type=Path, default=DEFAULT_FONTS)
    parser.add_argument("--work", type=Path, default=DEFAULT_WORK)
    parser.add_argument(
        "--tool", type=Path, required=True, help="the built native xbf_equivalence binary"
    )
    parser.add_argument("--report", type=Path, help="write the gate result as JSON")
    parser.add_argument(
        "--reuse-xbf", action="store_true",
        help="skip the Wine compile and use the .xbf already under the work directory",
    )
    args = parser.parse_args()

    pages = args.work / "pages"
    xbf_dir = args.work / "xbf"

    if args.reuse_xbf:
        if not xbf_dir.exists():
            raise SystemExit(f"--reuse-xbf given but {xbf_dir} does not exist")
        compile_result = json.loads((args.work / "compile.json").read_text(encoding="utf-8"))
    else:
        export_cases(args.cases, pages)
        try:
            tools = genxbf_driver.toolchain()
        except genxbf_driver.ToolchainMissing as error:
            print(str(error), file=sys.stderr)
            return 3
        if xbf_dir.exists():
            shutil.rmtree(xbf_dir)
        compile_result = genxbf_driver.compile_directory(pages, xbf_dir, tools=tools, verbose=True)
        (args.work / "compile.json").write_text(
            json.dumps(compile_result, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )

    # Twice, and required to agree: a gate whose own answer moves between runs
    # is not evidence of anything.
    first = run_gate(args.tool, args.cases, xbf_dir, args.fonts)
    second = run_gate(args.tool, args.cases, xbf_dir, args.fonts)
    deterministic = first == second

    rejected = {
        name.removesuffix(".xaml"): messages
        for name, messages in compile_result["errors"].items()
    }
    result = {
        "platform_version": compile_result["platform_version"],
        "cases": first["cases"],
        "compiled_by_genxbf": len(compile_result["compiled"]),
        "rejected_by_genxbf": rejected,
        "identical": first["identical"],
        "mismatched": first["mismatched"],
        "loader_refused": first["loader_refused"],
        "deterministic": deterministic,
    }
    text = json.dumps(result, indent=2, sort_keys=True)
    if args.report:
        args.report.write_text(text + "\n", encoding="utf-8")
    print(text)

    ok = (
        deterministic
        and not first["mismatched"]
        and not first["loader_refused"]
        and first["identical"] == len(compile_result["compiled"])
    )
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
