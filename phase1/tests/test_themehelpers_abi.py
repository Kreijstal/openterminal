#!/usr/bin/env python3

from __future__ import annotations

import sys
import unittest
from pathlib import Path

SCRIPT_DIRECTORY = Path(__file__).resolve().parents[1] / "scripts"
sys.path.insert(0, str(SCRIPT_DIRECTORY))

from harvest_themehelpers_abi import (  # noqa: E402
    header_declarations,
    parse_import_library_symbols,
    parse_llvm_readobj,
    targets_metadata,
)


LLVM_OUTPUT = """\
Format: COFF-i386
Arch: i386
AddressSize: 32bit
ImageFileHeader {
  Machine: IMAGE_FILE_MACHINE_I386 (0x14C)
}
Import {
  Name: api-ms-win-core-winrt-l1-1-0.dll
  Symbol: RoGetActivationFactory (1)
}
Export {
  Ordinal: 2
  Name: TerminalTrySetTransparentBackground
  RVA: 0x1010
}
Export {
  Ordinal: 1
  Name: TerminalTrySetAutoCompleteAnimationsWhenOccluded
  RVA: 0x11C0
}
"""

HEADER = """\
#define TERMINALTHEMEHELPERS_EXPORT extern "C"
TERMINALTHEMEHELPERS_EXPORT HRESULT TerminalTrySetTransparentBackground(const bool);
TERMINALTHEMEHELPERS_EXPORT HRESULT TerminalTrySetWindowAssociatedProcesses(HWND, DWORD, PHANDLE);
"""

TARGETS = """\
<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <PropertyGroup>
    <Native-Platform Condition="'$(Platform)' == 'Win32'">x86</Native-Platform>
  </PropertyGroup>
  <ItemDefinitionGroup><Link>
    <AdditionalDependencies>TerminalThemeHelpers.lib;%(AdditionalDependencies)</AdditionalDependencies>
  </Link></ItemDefinitionGroup>
  <ItemGroup><ReferenceCopyLocalPaths Include="TerminalThemeHelpers.dll" /></ItemGroup>
</Project>
"""


class ThemeHelpersAbiTests(unittest.TestCase):
    def test_parses_pe_exports_imports_and_machine(self) -> None:
        result = parse_llvm_readobj(LLVM_OUTPUT)
        self.assertEqual(result["format"], "COFF-i386")
        self.assertEqual(result["address_size"], 32)
        self.assertEqual(result["machine"], "IMAGE_FILE_MACHINE_I386")
        self.assertEqual(result["imports"][0]["symbols"], ["RoGetActivationFactory"])
        self.assertEqual(
            [entry["ordinal"] for entry in result["exports"]], [1, 2]
        )

    def test_parses_header_and_import_library_decoration(self) -> None:
        functions = header_declarations(HEADER)
        self.assertEqual(len(functions), 2)
        self.assertEqual(functions[0]["return_type"], "HRESULT")
        self.assertEqual(
            functions[1]["parameters"], ["HWND", "DWORD", "PHANDLE"]
        )
        symbols = parse_import_library_symbols(
            "_TerminalTrySetTransparentBackground\n"
            "__imp__TerminalTrySetTransparentBackground\n"
            "__IMPORT_DESCRIPTOR_TerminalThemeHelpers\n"
        )
        self.assertEqual(
            symbols,
            [
                "_TerminalTrySetTransparentBackground",
                "__imp__TerminalTrySetTransparentBackground",
            ],
        )

    def test_parses_msbuild_integration(self) -> None:
        result = targets_metadata(TARGETS)
        self.assertEqual(result["platform_mapping"][0]["value"], "x86")
        self.assertEqual(result["link_dependencies"][0], "TerminalThemeHelpers.lib;%(AdditionalDependencies)")
        self.assertEqual(result["copy_local"], ["TerminalThemeHelpers.dll"])


if __name__ == "__main__":
    unittest.main()
