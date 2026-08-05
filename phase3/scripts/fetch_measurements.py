#!/usr/bin/env python3
"""Fetch the measurement artifact and check it against the committed oracle.

The measurements are CI output and are not in the repository. They come from
the artifact of a successful "Phase 3 - XAML behaviour measurements" run and
land in a cache directory, by default under the system temp directory.

What *is* in the repository is a digest per oracle, and this refuses to hand
back measurements that do not match it. A servicing update to Windows changes
what the runtime answers; without that check the next fetch would quietly
re-baseline every expectation.

    python3 phase3/scripts/fetch_measurements.py            # latest green run
    python3 phase3/scripts/fetch_measurements.py --run-id N
    python3 phase3/scripts/fetch_measurements.py --accept-new-oracle
    python3 phase3/scripts/fetch_measurements.py --fonts    # the font metrics

The font metrics ride in their own artifact and get the same treatment: what
comes back is checked against `xaml-db/fonts/derived`, the numbers the corpus
solved for itself, so a harvest of the wrong font is an error rather than
pixel widths that are slightly off everywhere.

Needs the gh CLI, authenticated.
"""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from harvest_font_metrics import check_against  # noqa: E402
from measurement_digest import compare, oracle_record  # noqa: E402

WORKFLOW = "Phase 3 - XAML behaviour measurements"
ORACLES = Path(__file__).resolve().parents[1] / "xaml-db" / "oracles"
# The numbers the corpus solved for itself, which the harvest is checked
# against. See xaml-db/fonts/README.md.
DERIVED_FONTS = Path(__file__).resolve().parents[1] / "xaml-db" / "fonts" / "derived"


def gh(*args: str) -> str:
    proc = subprocess.run(("gh",) + args, capture_output=True, text=True)
    if proc.returncode != 0:
        raise SystemExit(f"gh {' '.join(args)} failed:\n{proc.stderr.strip()}")
    return proc.stdout.strip()


def latest_successful_run(repo: str | None, branch: str | None) -> str:
    args = ["run", "list", "--workflow", WORKFLOW, "--status", "success",
            "--limit", "1", "--json", "databaseId"]
    if repo:
        args += ["-R", repo]
    if branch:
        args += ["--branch", branch]
    runs = json.loads(gh(*args) or "[]")
    if not runs:
        raise SystemExit(f"no successful run of {WORKFLOW!r} to fetch")
    return str(runs[0]["databaseId"])


def download(run_id: str, repo: str | None, into: Path) -> Path:
    """Download the run's artifacts and return the measurements directory."""
    if into.exists():
        shutil.rmtree(into)
    into.mkdir(parents=True)
    args = ["run", "download", run_id, "--dir", str(into)]
    if repo:
        args += ["-R", repo]
    gh(*args)

    # The artifact is named for the build and holds one directory per oracle;
    # find it by the file that identifies the oracle rather than by guessing
    # at the layout.
    oracles = sorted(into.rglob("oracle.json"))
    if len(oracles) != 1:
        raise SystemExit(
            f"expected exactly one oracle.json in the artifact, found {len(oracles)}"
        )
    return oracles[0].parent


def fonts_directory(into: Path) -> Path | None:
    """The harvested font metrics, which ride in their own artifact.

    Absent from runs made before text measurement existed, so a missing one is
    reported rather than fatal: everything below L4 works without it.
    """
    found = sorted(path for path in into.iterdir()
                   if path.is_dir() and path.name.startswith("xaml-fonts-"))
    if len(found) > 1:
        raise SystemExit(f"expected one font artifact, found {len(found)}")
    return found[0] if found else None


def verify_fonts(fonts: Path, derived: Path = DERIVED_FONTS) -> list[str]:
    """Check a downloaded harvest against what the corpus implies on its own.

    The same check CI runs at harvest time, repeated here, because the artifact
    that arrives is not necessarily the one that job built -- it is whichever
    run was picked, and a metrics file that disagrees with the measurements
    turns into pixel widths that are slightly off everywhere rather than into
    an error. Comparing it against the committed derived numbers is cheap and
    it is the only independent statement about the font we have.
    """
    solved = {}
    for path in sorted(derived.glob("*.json")):
        metrics = json.loads(path.read_text(encoding="utf-8"))
        solved[metrics["family"]] = metrics

    problems: list[str] = []
    harvested = sorted(fonts.glob("*.json"))
    if not harvested:
        return [f"no font metrics in {fonts}: the artifact is empty"]

    for path in harvested:
        metrics = json.loads(path.read_text(encoding="utf-8"))
        family = metrics.get("family", path.name)
        if metrics.get("provenance") != "harvested":
            problems.append(
                f"{family}: {path.name} has provenance "
                f"{metrics.get('provenance')!r}, but this artifact holds readings "
                f"taken off the font")
            continue
        expected = solved.get(family)
        if expected is None:
            problems.append(
                f"{family}: nothing in xaml-db/fonts/derived covers this family, "
                f"so nothing checks what was harvested for it")
            continue
        problems += [f"{family}: {problem}" for problem in check_against(metrics, expected)]
    return problems


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run-id", help="default: the latest successful run")
    parser.add_argument("--repo", help="owner/name; default is the current checkout")
    parser.add_argument("--branch", help="restrict the run search to a branch")
    parser.add_argument("--dest", type=Path,
                        default=Path(tempfile.gettempdir()) / "openterminal-xaml-measurements",
                        help="cache directory for the download")
    parser.add_argument("--accept-new-oracle", action="store_true",
                        help="write the digest for an oracle that has none yet")
    parser.add_argument("--fonts", action="store_true",
                        help="print the harvested font metrics directory instead")
    args = parser.parse_args(argv)

    run_id = args.run_id or latest_successful_run(args.repo, args.branch)
    measurements = download(run_id, args.repo, args.dest)
    fonts = fonts_directory(args.dest)
    fresh = oracle_record(measurements)

    if args.fonts:
        if fonts is None:
            raise SystemExit(
                f"run {run_id} has no font metrics artifact; it either predates "
                f"text measurement or its harvest step did not run.\n"
                f"phase3/xaml-db/fonts/derived is committed and covers the text "
                f"cases that need only a line height and the advance of 'M'.")
        problems = verify_fonts(fonts)
        if problems:
            print(f"the harvested metrics in run {run_id} disagree with what the "
                  f"recorded measurements imply:", file=sys.stderr)
            for problem in problems:
                print(f"  {problem}", file=sys.stderr)
            return 1
        print("verified the harvested metrics against xaml-db/fonts/derived",
              file=sys.stderr)
    wanted = fonts if args.fonts else measurements

    ORACLES.mkdir(parents=True, exist_ok=True)
    stored_path = ORACLES / f"{fresh['os_build']}.json"

    if not stored_path.exists():
        # A new Windows build is expected -- runner images move -- so this is
        # not a failure, but it is not silent either: nothing is being checked
        # until the digest is committed.
        if args.accept_new_oracle:
            stored_path.write_text(json.dumps(fresh, indent=1, sort_keys=True) + "\n",
                                   encoding="utf-8")
            print(f"wrote new oracle digest {stored_path.relative_to(Path.cwd())}"
                  if stored_path.is_relative_to(Path.cwd()) else
                  f"wrote new oracle digest {stored_path}")
        else:
            print(f"WARNING: no committed digest for os_build {fresh['os_build']}; "
                  f"nothing is verifying these measurements.\n"
                  f"         re-run with --accept-new-oracle and commit "
                  f"{stored_path.name} to start checking for drift.",
                  file=sys.stderr)
        print(wanted)
        return 0

    stored = json.loads(stored_path.read_text(encoding="utf-8"))
    problems = compare(stored, fresh)
    if problems:
        print(f"the oracle for {fresh['os_build']} answers differently than "
              f"{stored_path.name} records:", file=sys.stderr)
        for problem in problems:
            print(f"  {problem}", file=sys.stderr)
        print("\nthis is drift, not a test failure: the runtime changed under us.\n"
              "review it, then update the digest deliberately.", file=sys.stderr)
        return 1

    print(f"verified {fresh['digest']['cases']} measurements against "
          f"{stored_path.name}", file=sys.stderr)
    print(wanted)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
