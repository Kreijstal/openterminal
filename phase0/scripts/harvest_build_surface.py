#!/usr/bin/env python3
"""Harvest deterministic Windows build-surface metadata from a source checkout."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import xml.etree.ElementTree as ET
from collections import defaultdict
from pathlib import Path
from typing import Any, Iterable


BUILD_XML_SUFFIXES = {
    ".csproj",
    ".props",
    ".targets",
    ".vcxitems",
    ".vcxproj",
    ".wapproj",
}

SOURCE_INPUT_SUFFIXES = {
    ".appxmanifest",
    ".csproj",
    ".idl",
    ".manifest",
    ".props",
    ".rc",
    ".resw",
    ".sln",
    ".slnx",
    ".targets",
    ".vcxitems",
    ".vcxproj",
    ".wapproj",
    ".xaml",
}

BUILD_PROPERTY_NAMES = {
    "AppContainerApplication",
    "ApplicationType",
    "ApplicationTypeRevision",
    "CppWinRTEnabled",
    "CppWinRTGenerateWindowsMetadata",
    "CppWinRTNamespaceMergeDepth",
    "CppWinRTOptimized",
    "GenerateWindowsMetadata",
    "MinimumVisualStudioVersion",
    "PlatformToolset",
    "TargetFramework",
    "TargetFrameworkProfile",
    "TargetFrameworkVersion",
    "TargetFrameworks",
    "TerminalCppWinrt",
    "TerminalMUX",
    "TerminalThemeHelpers",
    "TerminalVisualStudioSetup",
    "TerminalWinGetInterop",
    "UseWinUI",
    "WindowsAppContainer",
    "WindowsAppSDKSelfContained",
    "WindowsTargetPlatformMinVersion",
    "WindowsTargetPlatformVersion",
}

INPUT_ITEM_NAMES = {
    "ApplicationDefinition",
    "AppxManifest",
    "Manifest",
    "Midl",
    "Page",
    "PRIResource",
    "Resource",
    "ResourceCompile",
    "XamlAppDef",
    "XamlPage",
}

REFERENCE_ITEM_NAMES = {
    "COMReference",
    "ProjectReference",
    "Reference",
    "SDKReference",
}

BUILD_TOOLS = (
    "cl",
    "cppwinrt",
    "dxc",
    "lib",
    "link",
    "makeappx",
    "makepri",
    "mc",
    "midl",
    "midlrt",
    "msbuild",
    "mt",
    "nuget",
    "pdbstr",
    "rc",
    "signtool",
    "srctool",
    "te",
    "vstest.console",
)

TOOL_SCAN_SUFFIXES = {
    ".bat",
    ".cmd",
    ".csproj",
    ".props",
    ".ps1",
    ".targets",
    ".vcxproj",
    ".yaml",
    ".yml",
}


def git(repo: Path, *arguments: str) -> str:
    return subprocess.check_output(
        ["git", "-C", str(repo), *arguments], text=True
    ).strip()


def local_name(tag: str) -> str:
    return tag.rsplit("}", 1)[-1]


def tracked_paths(repo: Path) -> list[Path]:
    output = subprocess.check_output(
        ["git", "-C", str(repo), "ls-files", "-z"]
    )
    return [repo / item.decode("utf-8") for item in output.split(b"\0") if item]


def combine_conditions(parent: str, child: str) -> str:
    if parent and child:
        return f"({parent}) AND ({child})"
    return parent or child


def record(
    file: str,
    element: ET.Element,
    inherited_condition: str,
    **values: Any,
) -> dict[str, Any]:
    result: dict[str, Any] = {"file": file, **values}
    condition = combine_conditions(inherited_condition, element.get("Condition", ""))
    if condition:
        result["condition"] = condition
    return result


def split_dependencies(value: str) -> list[str]:
    return [
        item.strip()
        for item in value.replace("\n", "").split(";")
        if item.strip() and not item.strip().startswith("%(")
    ]


def walk_build_xml(
    file: str,
    element: ET.Element,
    results: dict[str, list[dict[str, Any]]],
    inherited_condition: str = "",
) -> None:
    tag = local_name(element.tag)
    own_condition = element.get("Condition", "")
    effective_condition = combine_conditions(inherited_condition, own_condition)

    if tag in BUILD_PROPERTY_NAMES:
        value = (element.text or "").strip()
        if value:
            results["properties"].append(
                record(
                    file,
                    element,
                    inherited_condition,
                    property=tag,
                    value=value,
                )
            )
    elif tag == "Import":
        project = element.get("Project", "").strip()
        sdk = element.get("Sdk", "").strip()
        if project or sdk:
            values: dict[str, Any] = {}
            if project:
                values["project"] = project
            if sdk:
                values["sdk"] = sdk
            results["imports"].append(
                record(file, element, inherited_condition, **values)
            )
    elif tag in INPUT_ITEM_NAMES:
        include = element.get("Include", "").strip()
        if include:
            results["declared_inputs"].append(
                record(
                    file,
                    element,
                    inherited_condition,
                    item_type=tag,
                    include=include,
                )
            )
    elif tag in REFERENCE_ITEM_NAMES:
        include = element.get("Include", "").strip()
        if include:
            results["references"].append(
                record(
                    file,
                    element,
                    inherited_condition,
                    reference_type=tag,
                    include=include,
                )
            )
    elif tag == "AdditionalDependencies":
        value = (element.text or "").strip()
        if value:
            results["link_dependencies"].append(
                record(
                    file,
                    element,
                    inherited_condition,
                    value=value,
                    tokens=split_dependencies(value),
                )
            )
    elif tag == "Exec":
        command = element.get("Command", "").strip()
        if command:
            results["exec_commands"].append(
                record(
                    file,
                    element,
                    inherited_condition,
                    command=command,
                )
            )
    elif tag == "Target":
        target = {key: element.get(key, "").strip() for key in (
            "Name",
            "BeforeTargets",
            "AfterTargets",
            "DependsOnTargets",
            "Inputs",
            "Outputs",
        )}
        target = {key: value for key, value in target.items() if value}
        if target:
            results["targets"].append(
                record(file, element, inherited_condition, **target)
            )

    child_condition = effective_condition if tag in {
        "Choose",
        "ItemDefinitionGroup",
        "ItemGroup",
        "Otherwise",
        "PropertyGroup",
        "Target",
        "When",
    } else inherited_condition
    for child in element:
        walk_build_xml(file, child, results, child_condition)


def scan_tool_mentions(paths: Iterable[Path], repo: Path) -> list[dict[str, Any]]:
    mentions: dict[str, set[str]] = defaultdict(set)
    patterns = {}
    for tool in BUILD_TOOLS:
        executable_suffix = r"\.exe" if tool in {"cl", "lib", "link", "te"} else r"(?:\.exe)?"
        patterns[tool] = re.compile(
            rf"(?<![A-Za-z0-9_.-]){re.escape(tool)}{executable_suffix}(?![A-Za-z0-9_.-])",
            re.IGNORECASE,
        )
    for path in paths:
        if path.suffix.lower() not in TOOL_SCAN_SUFFIXES:
            continue
        try:
            if path.suffix.lower() in BUILD_XML_SUFFIXES:
                root = ET.parse(path).getroot()
                contents = "\n".join(
                    element.get("Command", "")
                    for element in root.iter()
                    if local_name(element.tag) == "Exec"
                )
            else:
                contents = path.read_text(encoding="utf-8", errors="strict")
        except (OSError, UnicodeDecodeError):
            continue
        except ET.ParseError:
            continue
        relative = path.relative_to(repo).as_posix()
        for tool, pattern in patterns.items():
            if pattern.search(contents):
                mentions[tool].add(relative)
    return [
        {"tool": tool, "files": sorted(files)}
        for tool, files in sorted(mentions.items())
    ]


def harvest(repo: Path) -> dict[str, Any]:
    paths = tracked_paths(repo)
    source_inputs: dict[str, list[str]] = defaultdict(list)
    for path in paths:
        suffix = path.suffix.lower()
        if suffix in SOURCE_INPUT_SUFFIXES:
            source_inputs[suffix].append(path.relative_to(repo).as_posix())

    results: dict[str, list[dict[str, Any]]] = defaultdict(list)
    project_roots: list[dict[str, Any]] = []
    parse_failures: list[dict[str, str]] = []
    for path in paths:
        if path.suffix.lower() not in BUILD_XML_SUFFIXES:
            continue
        relative = path.relative_to(repo).as_posix()
        try:
            root = ET.parse(path).getroot()
        except ET.ParseError as error:
            parse_failures.append({"file": relative, "error": str(error)})
            continue
        root_record: dict[str, Any] = {"file": relative}
        for attribute in ("DefaultTargets", "InitialTargets", "Sdk", "ToolsVersion"):
            value = root.get(attribute, "").strip()
            if value:
                root_record[attribute] = value
        project_roots.append(root_record)
        walk_build_xml(relative, root, results)

    for entries in results.values():
        entries.sort(key=lambda item: json.dumps(item, sort_keys=True))

    library_tokens = sorted(
        {
            token
            for declaration in results["link_dependencies"]
            for token in declaration["tokens"]
            if token.lower().endswith(".lib")
        },
        key=lambda token: (token.lower(), token),
    )

    return {
        "schema_version": 1,
        "source": {
            "repository": git(repo, "remote", "get-url", "origin"),
            "commit": git(repo, "rev-parse", "HEAD"),
            "commit_date": git(repo, "show", "-s", "--format=%cI", "HEAD"),
        },
        "source_inputs": {
            suffix.removeprefix("."): sorted(files)
            for suffix, files in sorted(source_inputs.items())
        },
        "project_roots": sorted(
            project_roots, key=lambda item: json.dumps(item, sort_keys=True)
        ),
        "properties": results["properties"],
        "imports": results["imports"],
        "declared_inputs": results["declared_inputs"],
        "references": results["references"],
        "link_dependencies": results["link_dependencies"],
        "library_tokens": library_tokens,
        "exec_commands": results["exec_commands"],
        "targets": results["targets"],
        "tool_mentions": scan_tool_mentions(paths, repo),
        "parse_failures": sorted(parse_failures, key=lambda item: item["file"]),
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("repository", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    repo = args.repository.resolve()
    if not (repo / ".git").exists():
        parser.error(f"not a Git checkout: {repo}")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(harvest(repo), indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
