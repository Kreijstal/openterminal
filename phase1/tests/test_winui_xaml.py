#!/usr/bin/env python3

from __future__ import annotations

import sys
import unittest
from pathlib import Path

SCRIPT_DIRECTORY = Path(__file__).resolve().parents[1] / "scripts"
sys.path.insert(0, str(SCRIPT_DIRECTORY))

from harvest_winui_xaml import (  # noqa: E402
    appx_manifest_metadata,
    decode_guid_attribute,
    parse_llvm_readobj,
    xaml_metadata,
)


LLVM_OUTPUT = """\
Format: COFF-x86-64
Arch: x86_64
AddressSize: 64bit
ImageFileHeader {
  Machine: IMAGE_FILE_MACHINE_AMD64 (0x8664)
}
Import {
  Name: OLEAUT32.dll
  Symbol:  (200)
  Symbol: SysFreeString (7)
}
Export {
  Ordinal: 1
  Name: DllGetActivationFactory
}
"""

APPX_MANIFEST = b"""\
<Package xmlns="http://schemas.microsoft.com/appx/manifest/foundation/windows10">
  <Identity Name="Microsoft.UI.Xaml.2.8" ProcessorArchitecture="x64" Version="8.0.0.0" />
  <Properties><Framework>true</Framework></Properties>
  <Dependencies><TargetDeviceFamily Name="Windows.Universal" MinVersion="10.0.17763.0" /></Dependencies>
  <Extensions><Extension Category="windows.activatableClass.inProcessServer">
    <InProcessServer><Path>Microsoft.UI.Xaml.dll</Path>
      <ActivatableClass ActivatableClassId="Microsoft.UI.Xaml.Controls.TabView" ThreadingModel="both" />
    </InProcessServer>
  </Extension></Extensions>
</Package>
"""

GENERIC_XAML = b"""\
<ResourceDictionary xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
 xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
 xmlns:controls="using:Microsoft.UI.Xaml.Controls">
  <Style x:Key="TabStyle" TargetType="controls:TabView">
    <Setter Property="Template"><Setter.Value>
      <ControlTemplate TargetType="controls:TabView"><Grid x:Name="Root" /></ControlTemplate>
    </Setter.Value></Setter>
  </Style>
</ResourceDictionary>
"""


class WinuiXamlTests(unittest.TestCase):
    def test_decodes_winrt_guid_attribute(self) -> None:
        payload = bytes.fromhex("0100b6ee49f9eab3ad58b62bb7255bcc04df0000")
        self.assertEqual(
            decode_guid_attribute(payload),
            "f949eeb6-b3ea-58ad-b62b-b7255bcc04df",
        )

    def test_parses_named_and_ordinal_imports(self) -> None:
        result = parse_llvm_readobj(LLVM_OUTPUT)
        self.assertEqual(result["machine"], "IMAGE_FILE_MACHINE_AMD64")
        self.assertEqual(result["imports"][0]["ordinals"], [200])
        self.assertEqual(result["imports"][0]["symbols"], ["SysFreeString"])
        self.assertEqual(result["exports"][0]["name"], "DllGetActivationFactory")

    def test_parses_appx_activation_contract(self) -> None:
        result = appx_manifest_metadata(APPX_MANIFEST)
        self.assertEqual(result["identity"]["ProcessorArchitecture"], "x64")
        self.assertEqual(result["in_process_servers"], ["Microsoft.UI.Xaml.dll"])
        self.assertEqual(
            result["activatable_classes"][0]["id"],
            "Microsoft.UI.Xaml.Controls.TabView",
        )

    def test_summarizes_generic_xaml(self) -> None:
        result = xaml_metadata(GENERIC_XAML)
        self.assertEqual(result["resource_keys"], ["TabStyle"])
        self.assertEqual(result["style_targets"], {"controls:TabView": 1})
        self.assertEqual(result["control_template_targets"], {"controls:TabView": 1})
        self.assertEqual(result["named_elements"], ["Root"])


if __name__ == "__main__":
    unittest.main()
