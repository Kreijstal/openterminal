#!/usr/bin/env python3
"""Rebuild the theme-resource database locally, from the pinned MIT sources.

The database is CI output. `phase3_xaml_measure.yml` checks out
`microsoft/microsoft-ui-xaml` twice, at two pinned commits, runs the two
extractors, and uploads the result as `xaml-theme-resources-<os_build>`. That
artifact is what a local render run is supposed to point `--theme-resources`
at, and nothing in this repository can conjure it.

Except that it can, because both halves come out of public MIT source at a
pinned commit and the extractors are committed here. That is what this script
is: the same two checkouts and the same two extractor invocations the workflow
performs, runnable on a machine with git and a network, for the case where the
artifact is not available.

    python3 phase3/scripts/regenerate_theme_resources.py --out DIR

It exists because of a specific hole. The latest green measurement run
(31234396062) predates the workflow's "Extract the default-style database"
step, so its artifact carries `winui-2.8.4.json` and nothing else; every run
that *does* build `default-styles.json` has failed for an unrelated reason and
a failed run's artifact is not what a frontier should be measured against.
Without that file the `GlobalThemeResources` layer is missing, and the two
cases that name a key only the framework's own generic.xaml defines --
`L5-defaults-framework-only-key`, `L5-defaults-autofontfamily` -- refuse to
lay out. The refusal is honest; being unable to do anything about it locally
is not.

What this is not
----------------

Not a substitute for the artifact, and the output says so itself: each
extractor stamps its own provenance, naming the repository and the commit it
read. A regenerated file and the CI artifact's file agree when they come from
the same pinned commits -- the extractors take no input but the tree -- which
is the property that makes this a fallback rather than a second source of
truth. Two things check that:

`--compare-with DIR`
    holds a regeneration against a downloaded artifact and says exactly how
    they differ. Run against the artifacts of the moment, it reports:

        winui-2.8.4.json: identical apart from line terminators (77424 CRLF
          in the artifact, LF here); the parsed content is byte-for-byte equal
        default-styles.json: identical apart from line terminators (72818 ...)

    which is the whole difference: Python opened the output in text mode on a
    Windows runner. The line terminator is not content -- both parse to the
    same document, and the runtime reads JSON, not lines. It is called out
    rather than normalised away because "identical" and "identical apart from
    X" are different claims and only one of them is true.

determinism
    each half is extracted twice into separate files and compared, the same
    check CI makes. An extractor that is not reproducible is a defect whether
    it runs here or there.

Never write the output into the repository. It is several megabytes of
somebody else's content, materialized at build time, on the same rule as the
case corpus: the script and the pinned commit are the source.
"""

from __future__ import annotations

import argparse
import filecmp
import shutil
import subprocess
import sys
from pathlib import Path
from typing import NamedTuple

sys.path.insert(0, str(Path(__file__).resolve().parent))

import extract_default_styles as styles  # noqa: E402
import extract_winui_theme_resources as themes  # noqa: E402

SCRIPTS = Path(__file__).resolve().parent
UPSTREAM = "https://github.com/microsoft/microsoft-ui-xaml"


class Checkout(NamedTuple):
    """One pinned tree, and the least of it that has to be fetched.

    Sparse and blobless for the same reason the workflow is: the paths that
    matter are a few megabytes of a repository that is about a gigabyte, and a
    fallback nobody can afford to run is not a fallback.
    """

    name: str
    commit: str
    sparse: tuple[str, ...]


# The two commits, named by the extractors rather than repeated here, so a
# re-pin cannot leave this script pointing at the old tree.
WINUI = Checkout("winui", themes.PINNED_COMMIT, ("dev", "tools", "FeatureAreas.props"))
DXAML = Checkout("dxaml", styles.PINNED_DXAML_COMMIT, ("dxaml/xcp/dxaml/themes",))


class Product(NamedTuple):
    """One file of the database, and how to make it."""

    filename: str
    script: str
    #: argv after the script, with `{winui}` / `{dxaml}` / `{out}` substituted.
    arguments: tuple[str, ...]
    needs: tuple[Checkout, ...]


PRODUCTS = (
    Product("winui-2.8.4.json", "extract_winui_theme_resources.py",
            ("{winui}", "--out", "{out}"), (WINUI,)),
    Product("default-styles.json", "extract_default_styles.py",
            ("--dxaml", "{dxaml}", "--winui", "{winui}", "--out", "{out}"),
            (DXAML, WINUI)),
)


def run(arguments: list[str], cwd: Path | None = None) -> None:
    printable = " ".join(str(a) for a in arguments)
    print(f"$ {printable}", flush=True)
    subprocess.run(arguments, cwd=cwd, check=True)


def head_of(tree: Path) -> str | None:
    """The commit a checkout is on, or None if it is not a git tree."""
    proc = subprocess.run(["git", "-C", str(tree), "rev-parse", "HEAD"],
                          capture_output=True, text=True)
    return proc.stdout.strip() if proc.returncode == 0 else None


def checkout_action(found: str | None, wanted: str) -> str:
    """What a cached tree at `found` needs: reuse, refetch, or clone.

    Its own function because it is the part with a wrong answer available. A
    cache left over from an earlier pin looks exactly like a usable one, and
    extracting from it would produce a database that names the pinned commit
    in its provenance and was read from a different tree.
    """
    if found is None:
        return "clone"
    return "reuse" if found == wanted else "refetch"


def ensure_checkout(checkout: Checkout, cache: Path) -> Path:
    """A tree at exactly the pinned commit, fetching only if it is not already."""
    tree = cache / checkout.name
    found = head_of(tree)
    action = checkout_action(found, checkout.commit)
    if action == "reuse":
        print(f"{checkout.name}: reusing {tree} at {checkout.commit}")
        return tree
    if action == "refetch":
        print(f"{checkout.name}: {tree} is at {found}, not the pinned "
              f"{checkout.commit}; refetching")
        shutil.rmtree(tree)

    tree.parent.mkdir(parents=True, exist_ok=True)
    run(["git", "clone", "--filter=blob:none", "--no-checkout", "--depth", "1",
         UPSTREAM, str(tree)])
    run(["git", "-C", str(tree), "sparse-checkout", "set", "--cone",
         *checkout.sparse])
    run(["git", "-C", str(tree), "fetch", "--depth", "1", "origin", checkout.commit])
    run(["git", "-C", str(tree), "checkout", "--detach", checkout.commit])

    landed = head_of(tree)
    if landed != checkout.commit:
        raise SystemExit(f"{checkout.name}: checkout landed on {landed}, "
                         f"wanted {checkout.commit}")
    return tree


def extract(product: Product, trees: dict[str, Path], out: Path, check: Path) -> None:
    """Run one extractor twice and refuse a result that is not reproducible."""
    for destination in (out, check):
        arguments = [sys.executable, "-B", str(SCRIPTS / product.script)]
        arguments += [argument.format(out=destination,
                                      **{name: tree for name, tree in trees.items()})
                      for argument in product.arguments]
        run(arguments)
    if not filecmp.cmp(out, check, shallow=False):
        raise SystemExit(
            f"{product.filename}: two extractor runs disagree; the database is "
            f"not deterministic, so neither copy can be trusted")
    check.unlink()


CR, LF = b"\r", b"\n"


def compare(regenerated: Path, artifact: Path) -> str:
    """How a regenerated file differs from the artifact's copy, in one line.

    Three outcomes, and they are deliberately not collapsed into two. A
    Windows runner opens its output in text mode, so the artifact's copy of a
    file this script writes with LF arrives with CRLF; that is a difference in
    the file and not in the document, and both are true statements. Saying
    "identical" would be a lie and saying "differs" would send a reader
    looking for a content change that is not there.
    """
    if not artifact.exists():
        return f"{regenerated.name}: not in the artifact at all"
    mine, theirs = regenerated.read_bytes(), artifact.read_bytes()
    if mine == theirs:
        return f"{regenerated.name}: byte-for-byte identical to the artifact"
    if mine == theirs.replace(CR + LF, LF):
        return (f"{regenerated.name}: identical apart from line terminators "
                f"({theirs.count(CR)} CRLF in the artifact, LF here); "
                f"the content is byte-for-byte equal")
    offset = next((i for i, (a, b) in enumerate(zip(mine, theirs)) if a != b),
                  min(len(mine), len(theirs)))
    return (f"{regenerated.name}: DIFFERS in content at byte {offset} "
            f"({len(mine)} bytes here, {len(theirs)} in the artifact) -- the "
            f"pinned commits or the extractor are not what produced it")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", type=Path, required=True,
                        help="directory to write the database into; pass it to "
                             "build_render.py --theme-resources. NOT the repository")
    parser.add_argument("--cache", type=Path,
                        default=Path("/tmp") / "openterminal-microsoft-ui-xaml",
                        help="where the pinned checkouts are kept between runs")
    parser.add_argument("--only", choices=[p.filename for p in PRODUCTS],
                        help="regenerate one half only")
    parser.add_argument("--compare-with", type=Path,
                        help="a downloaded xaml-theme-resources-<build> directory; "
                             "report how this regeneration differs from it and "
                             "change nothing")
    parser.add_argument("--keep-existing", action="store_true",
                        help="leave a file that is already in --out alone; use "
                             "this to fill in the half a CI artifact is missing "
                             "without overwriting the half it has")
    args = parser.parse_args(argv)

    wanted = [p for p in PRODUCTS if args.only in (None, p.filename)]
    args.out.mkdir(parents=True, exist_ok=True)

    present = {p.filename for p in wanted if (args.out / p.filename).exists()}
    if args.keep_existing and present:
        print(f"already in {args.out}, left alone: {', '.join(sorted(present))}")
        wanted = [p for p in wanted if p.filename not in present]

    if not wanted:
        print("nothing to regenerate")
    else:
        trees: dict[str, Path] = {}
        for product in wanted:
            for checkout in product.needs:
                if checkout.name not in trees:
                    trees[checkout.name] = ensure_checkout(checkout, args.cache)

        for product in wanted:
            extract(product, trees, args.out / product.filename,
                    args.cache / f"check-{product.filename}")

        landed = sorted(path.name for path in args.out.glob("*.json"))
        print(f"{args.out} now holds {len(landed)} dictionar(ies): {', '.join(landed)}")
        print("this is regenerated from pinned MIT source, not a measurement; "
              "each file names the commit it was read from.")

    # After the early-out too: comparing an existing regeneration against a
    # freshly downloaded artifact is the whole point of --compare-with, and
    # "nothing to regenerate" is the state it is most often used in.
    if args.compare_with:
        print(f"\nagainst the artifact in {args.compare_with}:")
        for product in PRODUCTS:
            regenerated = args.out / product.filename
            if not regenerated.exists():
                continue
            print(f"  {compare(regenerated, args.compare_with / product.filename)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
