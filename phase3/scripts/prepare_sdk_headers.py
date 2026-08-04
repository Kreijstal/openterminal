#!/usr/bin/env python3
"""Build a GCC-consumable shadow of the pinned Windows SDK WinRT headers.

The SDK ships MSVC-only sources. Two things stop GCC, both mechanical:

  * case. The SDK is authored on a case-insensitive filesystem, so a header
    says #include "AsyncInfo.h" for a file named asyncinfo.h.
  * `typedef enum E : int E;` -- MSVC's opaque-enum-typedef extension. The
    C++11 spelling is an opaque declaration followed by a plain typedef.
  * `#pragma region` inside an enumerator list. It is an editor folding
    marker with no semantics, but GCC will not accept a pragma in that
    position, so the lines are dropped rather than translated.
  * `MIDL_CONST_ID IID& IID_IFoo = __uuidof(IFoo);` -- GCC has no
    `__declspec(uuid)`, so `__uuidof` cannot be evaluated and each of these
    becomes an unresolved symbol at link time. They are convenience aliases;
    the IIDs this project needs are harvested from the IDL instead, by
    phase3/scripts/harvest_xaml_iids.py. Dropping them removes the only
    thing in these headers that depends on a compiler extension GCC lacks.

Files needing no rewrite are symlinked, so the shadow stays cheap and it is
obvious which headers were touched. Nothing here is committed; the shadow is
build scratch, and the SDK payload it points at lives under /tmp.
"""
import re, sys, pathlib

QUOTED = re.compile(r'#\s*include\s*[<"]([^">]+)[">]')
# typedef enum Name : int Name;  ->  enum Name : int; typedef enum Name Name;
OPAQUE_ENUM = re.compile(r'typedef enum (\w+) : ([\w ]+?) (\1);')
FOLD_MARKER = re.compile(r'^[ \t]*#[ \t]*pragma[ \t]+(region|endregion)\b.*$\n?', re.M)
UUIDOF_ALIAS = re.compile(
    r'^[ \t]*MIDL_CONST_ID\s+IID\s*&\s*IID_\w+\s*=\s*__uuidof\([^)]*\);[ \t]*$\n?', re.M)


def rewrite(text):
    text = OPAQUE_ENUM.sub(r'enum \1 : \2; typedef enum \1 \1;', text)
    text = FOLD_MARKER.sub('', text)
    return UUIDOF_ALIAS.sub('', text)


def main():
    sdk, out = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
    for sub in ("winrt", "um", "shared", "ucrt"):
        src = sdk / sub
        if not src.is_dir():
            continue
        dst = out / sub
        dst.mkdir(parents=True, exist_ok=True)
        actual, patched = {}, 0
        for f in sorted(src.iterdir()):
            if not f.is_file():
                continue
            actual[f.name.lower()] = f
            link = dst / f.name
            if link.exists() or link.is_symlink():
                continue
            text = None
            if f.suffix.lower() in (".h", ".hpp"):
                raw = f.read_text(encoding="utf-8-sig", errors="surrogateescape")
                fixed = rewrite(raw)
                if fixed != raw:
                    text = fixed
            if text is None:
                link.symlink_to(f)
            else:
                link.write_text(text, encoding="utf-8", errors="surrogateescape")
                patched += 1
        # Aliases for every case variant any header actually asks for. Only
        # names that resolve to a real file, so a genuinely missing include
        # still fails loudly instead of becoming a dangling link.
        wanted = set()
        for f in src.iterdir():
            if f.suffix.lower() not in (".h", ".hpp", ".idl"):
                continue
            raw = f.read_text(encoding="utf-8-sig", errors="surrogateescape")
            wanted.update(m.replace("\\", "/").rsplit("/", 1)[-1] for m in QUOTED.findall(raw))
        aliases = 0
        for name in sorted(wanted):
            target = actual.get(name.lower())
            if target is None:
                continue
            link = dst / name
            if link.exists() or link.is_symlink():
                continue
            link.symlink_to((dst / target.name).resolve())
            aliases += 1
        print(f"{sub}: {len(actual)} headers, {patched} rewritten, {aliases} case aliases")


if __name__ == "__main__":
    main()
