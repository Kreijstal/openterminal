#!/usr/bin/env python3
"""What the XBF loader makes of Terminal's own compiled pages.

The corpus gate proves the loader agrees with the text path on markup this
runtime can already build. Terminal's pages are the other half of the question:
they are the files the shipped application actually feeds the runtime, and they
use most of the format the corpus never reaches -- deferred sections, x:Bind
connection ids, types that only TerminalApp's own metadata provider defines.

Every one of them has to do one of two things and nothing else: load, or fail
with a named reason. This produces that table. The named reasons are the work
list for the wave that clears them, which is why they are reported per file
rather than counted.

The .xbf files come from phase 2's build under Wine and stay under /tmp; none of
them is ever committed. With no build present this says so and stops, rather
than reporting an empty table as a clean sweep.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

DEFAULT_ROOT = Path("/tmp/openterminal-mingw")


def find_pages(root: Path) -> list[Path]:
    return sorted(root.glob("*xaml-compiler-output/*.xbf"))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=DEFAULT_ROOT)
    parser.add_argument("--tool", type=Path, required=True, help="the built xbf_dump binary")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()

    pages = find_pages(args.root)
    if not pages:
        print(
            f"no compiled Terminal pages under {args.root}; run phase2/scripts/"
            "build_winui_xaml.py first",
            file=sys.stderr,
        )
        return 3
    completed = subprocess.run(
        [str(args.tool)] + [str(page) for page in pages], capture_output=True, text=True
    )
    if completed.returncode != 0:
        print(completed.stderr, file=sys.stderr)
        return 4
    entries = json.loads(completed.stdout)

    widest = max(len(entry["file"]) for entry in entries)
    loaded = 0
    for entry in entries:
        outcome = entry.get("tree", entry["container"])
        if outcome == "built":
            loaded += 1
        reason = entry.get("reason") or entry.get("undecoded") or ""
        print(
            f"{entry['file']:<{widest}}  {entry.get('version', '-'):>4}  "
            f"{entry.get('bytes', 0):>7}  {entry.get('nodes', 0):>5}  {outcome:<8}  {reason}"
        )
    print(f"\n{loaded} of {len(entries)} pages load; the rest fail by name.")
    if args.report:
        args.report.write_text(
            json.dumps(entries, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
    # Every file has to have been read as a container and answered for. A file
    # that produced neither a tree nor a reason would be the one real failure.
    unanswered = [
        entry["file"]
        for entry in entries
        if entry.get("tree") != "built" and not (entry.get("reason") or entry.get("undecoded"))
    ]
    for name in unanswered:
        print(f"unanswered: {name}", file=sys.stderr)
    return 1 if unanswered else 0


if __name__ == "__main__":
    raise SystemExit(main())
