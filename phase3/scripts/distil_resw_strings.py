#!/usr/bin/env python3
"""Distil Windows Terminal's .resw string tables into an x:Uid lookup.

An ``x:Uid`` on an element is a key into a localised resource map. The map is
built from ``.resw`` files, which are flat: one entry per *property*, with both
halves of the assignment encoded in the key.

    <data name="SaveButton.Content">        SaveButton  ->  Content
    <data name="Nav_Appearance.Text">       Nav_Appearance -> Text
    <data name="Foo.[using:Windows.UI.Xaml.Automation]AutomationProperties.Name">
                                            Foo -> AutomationProperties.Name

Keys with no dot are the other kind: a string a *code path* asks for by name
through ``ResourceLoader.GetString``. They set nothing in markup, and they are
listed separately rather than dropped, so the difference between "not a uid
property" and "we missed it" stays visible.

The output is generated, not committed: the pinned checkout and this script are
the source, exactly as the harvested XAML inventory is. Two runs over one commit
produce byte-identical output -- the entries are sorted, the values are taken
verbatim, and the commit is recorded in the file.

    python3 phase3/scripts/distil_resw_strings.py /tmp/windows-terminal \\
        --out /tmp/terminal-strings.json
    measure_cases <cases> <out> <fonts> /tmp/terminal-strings.json

What the table can actually do to a layout is a much smaller thing than its
size suggests, and the summary this prints says so: only a property the markup
parser can read reaches an element at all, and most of a .resw sets ``Content``
or ``Header`` on controls the layout core does not implement yet.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import xml.etree.ElementTree as ET
from collections import Counter
from pathlib import Path
from typing import Any

SCHEMA_VERSION = 1

# Where Terminal keeps them: one directory per locale under each project's
# Resources/. Anchored at src/cascadia so that a stray .resw elsewhere in the
# tree -- a test fixture, a vendored dependency -- cannot silently join the map.
RESW_GLOB = "src/cascadia/*/Resources/{locale}/Resources.resw"

# The namespace qualifier WinUI puts in front of an attached property's owner.
# It names the metadata the property comes from and is not part of the spelling
# an attribute would use, so it is stripped.
USING_CLAUSE = re.compile(r"^\[using:[^\]]+\]")

# A property path, once the using clause is off: "Text", or "Owner.Member" for
# an attached one. Anything else means the key was split in the wrong place --
# see split_key.
PROPERTY_PATH = re.compile(r"^[A-Za-z_][\w]*(\.[A-Za-z_][\w]*)?$")


def git(repo: Path, *args: str) -> str:
    return subprocess.run(
        ["git", "-C", str(repo), *args],
        check=True, capture_output=True, text=True,
    ).stdout.strip()


def canonical_remote(url: str) -> str:
    """One spelling of a remote, whoever cloned it.

    The same rule harvest_terminal_xaml.py applies, for the same reason: the
    machine that ran the distiller must not end up in the output.
    """
    text = url.strip().rstrip("/")
    if text.startswith("git@") and ":" in text:
        host, _, path = text[len("git@"):].partition(":")
        text = f"https://{host}/{path}"
    elif text.startswith("ssh://git@"):
        text = "https://" + text[len("ssh://git@"):]
    if text.endswith(".git"):
        text = text[: -len(".git")]
    return text


def split_key(name: str) -> tuple[str, str] | None:
    """``uid, property`` for a key that names one, else None.

    Split at the *first* dot, because the uid half is a single identifier and
    every dot after it belongs to the property path. That holds for every key in
    Terminal's tables and is checked rather than assumed: a property half that
    does not look like a property path is reported as unsplittable instead of
    being guessed at, since a uid containing a dot would split here silently and
    wrongly.
    """
    uid, dot, rest = name.partition(".")
    if not dot or not uid:
        return None
    prop = USING_CLAUSE.sub("", rest)
    if not PROPERTY_PATH.match(prop):
        return None
    return uid, prop


def read_file(path: Path) -> list[tuple[str, str]]:
    """``(key, value)`` in document order.

    The value is taken verbatim. .resw marks its entries ``xml:space="preserve"``
    and a trailing space in a label is a translator's decision, not whitespace to
    tidy -- and a string that measures differently after being tidied is exactly
    the bug this table exists to avoid.
    """
    root = ET.parse(path).getroot()
    entries = []
    for data in root.findall("data"):
        name = data.get("name")
        if name is None:
            continue
        value = data.find("value")
        entries.append((name, "" if value is None or value.text is None else value.text))
    return entries


def distil(repo: Path, locale: str) -> dict[str, Any]:
    files = sorted(repo.glob(RESW_GLOB.format(locale=locale)))
    if not files:
        raise SystemExit(f"no .resw files under {repo} for locale {locale}")

    strings: dict[str, dict[str, str]] = {}
    code_keys: dict[str, str] = {}
    unsplittable: list[dict[str, str]] = []
    properties: Counter[str] = Counter()
    origin: dict[tuple[str, str], str] = {}

    for path in files:
        source = path.relative_to(repo).as_posix()
        for name, value in read_file(path):
            split = split_key(name)
            if split is None:
                if "." in name:
                    # A dotted key whose halves do not look like uid+property.
                    # Named rather than assigned, because assigning it would put
                    # a property on a uid that does not exist.
                    unsplittable.append({"key": name, "file": source})
                else:
                    code_keys[name] = source
                continue

            uid, prop = split
            # One key, two files. Deterministic order would hide it rather than
            # settle it: whichever file sorts first is not an answer to which
            # resource map a page actually gets.
            previous = origin.get((uid, prop))
            if previous is not None and strings[uid][prop] != value:
                raise SystemExit(
                    f"{uid}.{prop} is defined differently in {previous} and {source}"
                )
            origin[(uid, prop)] = source
            strings.setdefault(uid, {})[prop] = value
            properties[prop] += 1

    return {
        "schema_version": SCHEMA_VERSION,
        "source": {
            "repository": canonical_remote(git(repo, "remote", "get-url", "origin")),
            "commit": git(repo, "rev-parse", "HEAD"),
            "locale": locale,
            "files": [p.relative_to(repo).as_posix() for p in files],
        },
        "totals": {
            "uids": len(strings),
            "uid_properties": sum(len(v) for v in strings.values()),
            "code_keys": len(code_keys),
            "unsplittable": len(unsplittable),
        },
        # uid -> property -> value. What measure_cases reads.
        "strings": {uid: dict(sorted(props.items())) for uid, props in sorted(strings.items())},
        # Not applicable to markup: a code path asks for these by name.
        "code_keys": {name: code_keys[name] for name in sorted(code_keys)},
        "unsplittable": sorted(unsplittable, key=lambda e: (e["key"], e["file"])),
        # Which properties the table actually sets, most-used first. This is the
        # list an implementation has to grow to make the table do anything.
        "properties": dict(properties.most_common()),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("repository", type=Path, help="Windows Terminal checkout")
    parser.add_argument("--locale", default="en-US",
                        help="which locale's tables to read (default en-US)")
    parser.add_argument("--out", type=Path, help="where to write the table")
    args = parser.parse_args()

    repo = args.repository.resolve()
    if not (repo / ".git").exists():
        parser.error(f"not a Git checkout: {repo}")

    table = distil(repo, args.locale)
    text = json.dumps(table, indent=1, sort_keys=True, ensure_ascii=False) + "\n"
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(text, encoding="utf-8")

    totals = table["totals"]
    print(f"  files            {len(table['source']['files']):>5}")
    print(f"  uids             {totals['uids']:>5}")
    print(f"  uid properties   {totals['uid_properties']:>5}")
    print(f"  code-only keys   {totals['code_keys']:>5}")
    print(f"  unsplittable     {totals['unsplittable']:>5}")
    print("  properties set:")
    for prop, count in list(table["properties"].items())[:12]:
        print(f"    {prop:<34} {count:>5}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
