#!/usr/bin/env python3
"""Catch, on this machine, the one class of harness defect mingw cannot.

The Windows-only probes under `phase3/harness/` are compiled by `cl` on the
CI runner and by nobody here -- but they *are* compiled here, by mingw, when a
developer checks their work, and mingw accepts things the Windows SDK does not.
The gap is not academic. `phase3/harness/font_fallback_probe.cpp` was written
against `IID_IDWriteTextAnalysisSource`, `IID_IDWriteLocalFontFileLoader` and
`IID_IDWriteFactory2`; mingw-w64's `dwrite` headers define those symbols, the
Windows SDK's do not, and the file compiled cleanly on every developer machine
while every CI run since it landed died with

    error C2065: 'IID_IDWriteFactory2': undeclared identifier

taking the DirectWrite system-fallback harvest with it -- which is why
`L4-icon-rule-mdl2-latin-14` still has no fallback metrics to lay out from.

Six runs failed on a defect a compiler on this machine could not see. So the
rule gets written down instead: DirectWrite (and Direct2D, and DirectComposition)
declare their interfaces with `DECLSPEC_UUID` and publish no `IID_I*` constants,
so `__uuidof(Interface)` is the only spelling that resolves under both
toolchains.

This is a source rule, not a compile. It cannot prove the probes build under
`cl`; only a Windows run does that. It proves this specific failure is not the
reason the next one dies.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

HARNESS = Path(__file__).resolve().parents[1] / "harness"

# The interface families whose SDK headers use DECLSPEC_UUID and ship no
# IID_ constants. IUnknown is deliberately not here: `IID_IUnknown` is a real
# exported symbol in every SDK, so naming it is legal -- it is only the
# Direct* interfaces that are not.
UUID_ONLY = re.compile(r"\bIID_(IDWrite|ID2D1|IDComposition|IDXGI)[A-Za-z0-9_]*")

# Anything after `//` on a line: the rule is about code, and the comments that
# explain the rule necessarily quote the spelling it forbids.
COMMENT = re.compile(r"//.*$", re.MULTILINE)


class DirectXInterfacesAreNamedByUuidof(unittest.TestCase):
    def test_no_harness_source_names_an_IID_constant(self) -> None:
        offenders: list[str] = []
        sources = sorted(HARNESS.glob("*.cpp")) + sorted(HARNESS.glob("*.h"))
        self.assertTrue(sources, f"no harness sources under {HARNESS}")
        for source in sources:
            body = COMMENT.sub("", source.read_text(encoding="utf-8"))
            for line_number, line in enumerate(body.splitlines(), start=1):
                for match in UUID_ONLY.finditer(line):
                    offenders.append(
                        f"{source.name}:{line_number}: {match.group(0)} does not "
                        f"exist in the Windows SDK headers; write "
                        f"__uuidof({match.group(0)[4:]}) instead. mingw defines "
                        f"it, cl does not, and CI is where that is discovered.")
        self.assertEqual(offenders, [], "\n" + "\n".join(offenders))


if __name__ == "__main__":
    unittest.main()
