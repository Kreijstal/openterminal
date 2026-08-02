#!/usr/bin/env python3

from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT_DIRECTORY = Path(__file__).resolve().parents[1] / "scripts"
sys.path.insert(0, str(SCRIPT_DIRECTORY))

from harvest_build_surface import harvest  # noqa: E402


PROJECT = """\
<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003" ToolsVersion="Current">
  <PropertyGroup Condition="'$(Configuration)' == 'Release'">
    <WindowsTargetPlatformVersion>10.0.26100.0</WindowsTargetPlatformVersion>
    <PlatformToolset Condition="'$(VisualStudioVersion)' &gt;= '18.0'">v145</PlatformToolset>
  </PropertyGroup>
  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.targets" />
  <ItemGroup>
    <Midl Include="Example.idl" />
    <SDKReference Include="Microsoft.VCLibs,Version=14.0" />
  </ItemGroup>
  <ItemDefinitionGroup>
    <Link>
      <AdditionalDependencies>WindowsApp.lib;shell32.lib;%(AdditionalDependencies)</AdditionalDependencies>
    </Link>
  </ItemDefinitionGroup>
  <Target Name="GenerateResources" BeforeTargets="Build">
    <Exec Command="makepri.exe new /pr $(ProjectDir)" />
  </Target>
</Project>
"""


class HarvestBuildSurfaceTests(unittest.TestCase):
    def test_harvests_conditioned_build_surface_deterministically(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            repo = Path(directory)
            subprocess.run(["git", "init", "-q", "-b", "main", repo], check=True)
            subprocess.run(
                ["git", "-C", repo, "config", "user.name", "Test"], check=True
            )
            subprocess.run(
                ["git", "-C", repo, "config", "user.email", "test@example.invalid"],
                check=True,
            )
            subprocess.run(
                ["git", "-C", repo, "remote", "add", "origin", "https://example.invalid/terminal"],
                check=True,
            )
            (repo / "Terminal.vcxproj").write_text(PROJECT, encoding="utf-8")
            (repo / "Example.idl").write_text("namespace Example {}\n", encoding="utf-8")
            subprocess.run(["git", "-C", repo, "add", "."], check=True)
            subprocess.run(
                ["git", "-C", repo, "commit", "-q", "-m", "fixture"], check=True
            )

            first = harvest(repo)
            second = harvest(repo)

            self.assertEqual(first, second)
            self.assertEqual(first["source_inputs"]["idl"], ["Example.idl"])
            self.assertEqual(first["library_tokens"], ["shell32.lib", "WindowsApp.lib"])
            self.assertEqual(first["parse_failures"], [])
            self.assertEqual(first["project_roots"][0]["ToolsVersion"], "Current")
            self.assertEqual(first["references"][0]["reference_type"], "SDKReference")
            self.assertEqual(
                first["properties"][1]["condition"],
                "('$(Configuration)' == 'Release') AND "
                "('$(VisualStudioVersion)' >= '18.0')",
            )
            self.assertEqual(first["tool_mentions"][0]["tool"], "makepri")
            self.assertNotIn("condition", first["exec_commands"][0])


if __name__ == "__main__":
    unittest.main()
