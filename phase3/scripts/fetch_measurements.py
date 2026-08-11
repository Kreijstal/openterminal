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

That check only exists for families the corpus has derived something for. The
harvest legitimately carries families it has not -- the icon fonts, above all:
nothing has solved Segoe MDL2 Assets out of the recorded measurements, because
until the L4-icon series there was nothing to solve from. Those families are
*named* rather than either refused or swallowed. Refusing them would block
every family that can be checked over families where nothing disagreed;
swallowing them would let a harvest of the wrong file through in silence.
`--require-derived-coverage` turns the naming into a refusal for a caller who
wants that, and the names are printed either way.

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
from typing import NamedTuple

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

    return measurements_directory(into)


def measurements_directory(into: Path) -> Path:
    """The oracle directory inside the measurements artifact.

    The measurements artifact is named for the build and holds one directory
    per oracle; find that directory by the file that identifies the oracle
    rather than by guessing at the layout. Scoped to the one artifact, not the
    whole download: other artifacts of the same run carry an oracle.json as
    their own identity stamp -- the render boundaries do -- and a download-wide
    search would refuse the first run that produced them.
    """
    artifact = artifact_directory(into, "xaml-measurements-")
    if artifact is None:
        raise SystemExit("the run has no xaml-measurements-* artifact")
    oracles = sorted(artifact.rglob("oracle.json"))
    if len(oracles) != 1:
        raise SystemExit(
            f"expected exactly one oracle.json in {artifact.name}, "
            f"found {len(oracles)}"
        )
    return oracles[0].parent


def artifact_directory(into: Path, prefix: str) -> Path | None:
    """The one downloaded artifact directory whose name starts with `prefix`.

    `gh run download` with no `--name` fetches every artifact of the run, each
    into a directory named after it, so all of them are already on disk and
    this is only a matter of saying which one was asked for. A run that has
    none is reported rather than fatal -- runs predate the steps that build
    these -- and a run with two is refused, because picking one would be a
    guess about which build the caller meant.
    """
    found = sorted(path for path in into.iterdir()
                   if path.is_dir() and path.name.startswith(prefix))
    if len(found) > 1:
        raise SystemExit(f"expected one {prefix}* artifact, found {len(found)}")
    return found[0] if found else None


def fonts_directory(into: Path) -> Path | None:
    """The harvested font metrics, which ride in their own artifact.

    Absent from runs made before text measurement existed, so a missing one is
    reported rather than fatal: everything below L4 works without it.
    """
    return artifact_directory(into, "xaml-fonts-")


def theme_resources_directory(into: Path) -> Path | None:
    """The dictionaries the corpus was measured against."""
    return artifact_directory(into, "xaml-theme-resources-")


def glyph_outlines_directory(into: Path) -> Path | None:
    """The recorded glyph outlines, if any run has ever produced them."""
    return artifact_directory(into, "xaml-glyph-outlines-")


#: The dictionaries `build_render.py --theme-resources` wants, and what is lost
#: without each. Named individually because "1 dictionar(ies)" is what a
#: missing half looks like today, and that reads like a working directory.
THEME_DICTIONARIES = {
    "winui-2.8.4.json":
        "the XamlControlsResources layer; without it every case naming a WinUI "
        "resource key refuses to lay out",
    "default-styles.json":
        "the GlobalThemeResources layer, the framework's own generic.xaml; "
        "without it L5-defaults-framework-only-key and "
        "L5-defaults-autofontfamily refuse to lay out. Regenerate it from "
        "pinned MIT source with phase3/scripts/regenerate_theme_resources.py",
}


def report_theme_resources(directory: Path) -> list[str]:
    """One line per dictionary: present, or absent and what that costs."""
    lines: list[str] = []
    for name, consequence in THEME_DICTIONARIES.items():
        if (directory / name).is_file():
            lines.append(f"{name}: present")
        else:
            lines.append(f"{name}: ABSENT -- {consequence}")
    unexpected = sorted(path.name for path in directory.glob("*.json")
                        if path.name not in THEME_DICTIONARIES)
    lines += [f"{name}: present, and not one this script knows about"
              for name in unexpected]
    return lines


class FontCheck(NamedTuple):
    """What checking a harvest actually established, split three ways.

    `problems` are disagreements: a family the corpus derived numbers for whose
    harvest does not match them, or an artifact that is broken outright. Those
    always block -- they are the failure this check was written for.

    `unchecked` are families nothing in `xaml-db/fonts/derived` covers. Nothing
    disagreed there because nothing could; the honest report is the list of
    names, and whether it blocks is the caller's explicit decision.

    `verified` are the families that were actually compared and matched. Kept
    so a run can say what it checked rather than only that it found nothing
    wrong -- an empty `problems` list over an empty `verified` list is not a
    verification, and without this the two read identically.
    """

    problems: list[str]
    unchecked: list[str]
    verified: list[str]

    def blocking(self, require_coverage: bool = False) -> list[str]:
        """The lines that mean this harvest must be refused."""
        return self.problems + (self.unchecked if require_coverage else [])


def verify_fonts(fonts: Path, derived: Path = DERIVED_FONTS) -> FontCheck:
    """Check a downloaded harvest against what the corpus implies on its own.

    The same check CI runs at harvest time, repeated here, because the artifact
    that arrives is not necessarily the one that job built -- it is whichever
    run was picked, and a metrics file that disagrees with the measurements
    turns into pixel widths that are slightly off everywhere rather than into
    an error. Comparing it against the committed derived numbers is cheap and
    it is the only independent statement about the font we have.

    A family the corpus never derived is a third outcome, not a disagreement.
    See `FontCheck`.
    """
    solved = {}
    for path in sorted(derived.glob("*.json")):
        metrics = json.loads(path.read_text(encoding="utf-8"))
        solved[metrics["family"]] = metrics

    problems: list[str] = []
    unchecked: list[str] = []
    verified: list[str] = []
    harvested = sorted(fonts.glob("*.json"))
    if not harvested:
        return FontCheck([f"no font metrics in {fonts}: the artifact is empty"], [], [])

    for path in harvested:
        # A file that will not parse says nothing about coverage: it is a
        # broken artifact. Reporting it as an unchecked family would let a
        # truncated download read as "nothing had derived numbers for this".
        try:
            metrics = json.loads(path.read_text(encoding="utf-8"))
        except (json.JSONDecodeError, UnicodeDecodeError) as error:
            problems.append(f"{path.name}: not readable as font metrics: {error}")
            continue
        family = metrics.get("family", path.name)
        if metrics.get("provenance") != "harvested":
            problems.append(
                f"{family}: {path.name} has provenance "
                f"{metrics.get('provenance')!r}, but this artifact holds readings "
                f"taken off the font")
            continue
        expected = solved.get(family)
        if expected is None:
            unchecked.append(
                f"{family}: nothing in xaml-db/fonts/derived covers this family, "
                f"so nothing checks what was harvested for it")
            continue
        disagreements = [f"{family}: {problem}"
                         for problem in check_against(metrics, expected)]
        if disagreements:
            problems += disagreements
        else:
            verified.append(family)
    return FontCheck(problems, unchecked, verified)


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
    parser.add_argument("--theme-resources", action="store_true",
                        help="print the theme-resource directory instead, and "
                             "report which dictionaries it actually carries")
    parser.add_argument("--glyph-outlines", action="store_true",
                        help="print the recorded glyph-outline directory instead; "
                             "no run has produced one yet")
    parser.add_argument("--require-derived-coverage", action="store_true",
                        help="refuse a harvest carrying a family that "
                             "xaml-db/fonts/derived does not cover, instead of "
                             "naming it and continuing")
    args = parser.parse_args(argv)
    chosen = [name for name, on in (("--fonts", args.fonts),
                                    ("--theme-resources", args.theme_resources),
                                    ("--glyph-outlines", args.glyph_outlines)) if on]
    if len(chosen) > 1:
        parser.error(f"{' and '.join(chosen)} each print a different directory; "
                     f"ask for one")

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
        checked = verify_fonts(fonts)
        if checked.problems:
            print(f"the harvested metrics in run {run_id} disagree with what the "
                  f"recorded measurements imply:", file=sys.stderr)
            for problem in checked.problems:
                print(f"  {problem}", file=sys.stderr)
            return 1
        # Printed before the refusal decision, and printed on success too: the
        # list of families nothing checked is the interesting half of this
        # report whether or not it is being treated as fatal.
        if checked.unchecked:
            print(f"{len(checked.unchecked)} famil(ies) in run {run_id} are not "
                  f"checked by anything in xaml-db/fonts/derived:", file=sys.stderr)
            for line in checked.unchecked:
                print(f"  {line}", file=sys.stderr)
            if args.require_derived_coverage:
                print("--require-derived-coverage was given, so that is a refusal.",
                      file=sys.stderr)
                return 1
            print("  these are carried anyway; nothing independent vouches for them.\n"
                  "  pass --require-derived-coverage to refuse them instead.",
                  file=sys.stderr)
        if checked.verified:
            print(f"verified {len(checked.verified)} famil(ies) against "
                  f"xaml-db/fonts/derived: {', '.join(checked.verified)}",
                  file=sys.stderr)
        else:
            # No disagreements and no comparisons is not a verification, and it
            # must not print like one.
            print("nothing in this harvest was checked against "
                  "xaml-db/fonts/derived", file=sys.stderr)
    wanted = fonts if args.fonts else measurements

    if args.theme_resources:
        theme = theme_resources_directory(args.dest)
        if theme is None:
            raise SystemExit(
                f"run {run_id} has no theme-resource artifact.\n"
                f"regenerate the database from pinned MIT source instead:\n"
                f"  python3 phase3/scripts/regenerate_theme_resources.py --out DIR")
        # The half-empty directory is the failure mode here, not the missing
        # one: "1 dictionar(ies)" is what a missing GlobalThemeResources layer
        # looks like, and it reads exactly like a working directory.
        for line in report_theme_resources(theme):
            print(f"  {line}", file=sys.stderr)
        wanted = theme

    if args.glyph_outlines:
        recorded = glyph_outlines_directory(args.dest)
        if recorded is None:
            raise SystemExit(
                f"run {run_id} has no glyph-outline artifact. No run has ever "
                f"produced one: the workflow step that builds "
                f"phase3/harness/glyph_outline_probe.cpp and runs "
                f"phase3/scripts/harvest_glyph_outlines.py is committed but has "
                f"not been pushed and run yet.\n"
                f"Until it has, the 113 cases refusing "
                f"'DirectWrite could not resolve any requested family in "
                f"\"Segoe UI\"' stay refused, and that is the honest state.")
        wanted = recorded

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
