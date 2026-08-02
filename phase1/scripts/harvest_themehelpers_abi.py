#!/usr/bin/env python3
"""Harvest TerminalThemeHelpers ABI metadata without redistributing binaries."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import tempfile
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path
from typing import Any


PACKAGE_ID = "Microsoft.Internal.Windows.Terminal.ThemeHelpers"
ARCHITECTURES = ("x64", "x86", "arm64")
FUNCTION_PATTERN = re.compile(
    r"^TERMINALTHEMEHELPERS_EXPORT\s+(?P<return_type>\S+)\s+"
    r"(?P<name>[A-Za-z_]\w*)\((?P<parameters>.*)\);$"
)


def sha256(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def local_name(tag: str) -> str:
    return tag.rsplit("}", 1)[-1]


def read_text(archive: zipfile.ZipFile, path: str) -> str:
    return archive.read(path).decode("utf-8-sig")


def package_metadata(nuspec: str) -> dict[str, Any]:
    root = ET.fromstring(nuspec)
    metadata_node = next(
        element for element in root.iter() if local_name(element.tag) == "metadata"
    )
    values: dict[str, str] = {}
    for child in metadata_node:
        value = (child.text or "").strip()
        if value:
            values[local_name(child.tag)] = value
    return {
        "id": values.get("id"),
        "version": values.get("version"),
        "title": values.get("title"),
        "authors": values.get("authors"),
        "project_url": values.get("projectUrl"),
        "description": values.get("description"),
        "copyright": values.get("copyright"),
        "require_license_acceptance": values.get("requireLicenseAcceptance"),
        "license": values.get("license"),
        "license_url": values.get("licenseUrl"),
    }


def header_declarations(header: str) -> list[dict[str, Any]]:
    declarations: list[dict[str, Any]] = []
    for line in header.splitlines():
        declaration = line.strip()
        match = FUNCTION_PATTERN.match(declaration)
        if not match:
            continue
        parameters_text = match.group("parameters").strip()
        parameters = (
            [parameter.strip() for parameter in parameters_text.split(",")]
            if parameters_text and parameters_text != "void"
            else []
        )
        declarations.append(
            {
                "declaration": declaration,
                "name": match.group("name"),
                "return_type": match.group("return_type"),
                "parameters": parameters,
            }
        )
    return sorted(declarations, key=lambda item: item["name"])


def targets_metadata(targets: str) -> dict[str, Any]:
    root = ET.fromstring(targets)
    platform_mapping: list[dict[str, str]] = []
    include_directories: list[str] = []
    link_dependencies: list[str] = []
    copy_local: list[str] = []
    for element in root.iter():
        tag = local_name(element.tag)
        value = (element.text or "").strip()
        if tag == "Native-Platform" and value:
            record = {"value": value}
            if element.get("Condition"):
                record["condition"] = element.get("Condition", "")
            platform_mapping.append(record)
        elif tag == "AdditionalIncludeDirectories" and value:
            include_directories.append(value)
        elif tag == "AdditionalDependencies" and value:
            link_dependencies.append(value)
        elif tag == "ReferenceCopyLocalPaths" and element.get("Include"):
            copy_local.append(element.get("Include", ""))
    return {
        "platform_mapping": sorted(platform_mapping, key=lambda item: json.dumps(item, sort_keys=True)),
        "include_directories": sorted(include_directories),
        "link_dependencies": sorted(link_dependencies),
        "copy_local": sorted(copy_local),
    }


def parse_llvm_readobj(output: str) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, pattern in {
        "format": r"^Format:\s+(.+)$",
        "architecture": r"^Arch:\s+(.+)$",
        "address_size": r"^AddressSize:\s+(\d+)bit$",
        "machine": r"^\s*Machine:\s+([^\s(]+)",
    }.items():
        match = re.search(pattern, output, re.MULTILINE)
        if not match:
            raise RuntimeError(f"llvm-readobj output is missing {key}")
        result[key] = int(match.group(1)) if key == "address_size" else match.group(1)

    imports: list[dict[str, Any]] = []
    exports: list[dict[str, Any]] = []
    current_kind: str | None = None
    current: dict[str, Any] = {}
    for raw_line in output.splitlines():
        line = raw_line.strip()
        if line == "Import {":
            current_kind = "import"
            current = {"symbols": []}
        elif line == "Export {":
            current_kind = "export"
            current = {}
        elif current_kind and line == "}":
            if current_kind == "import" and current.get("dll"):
                current["symbols"] = sorted(set(current["symbols"]))
                imports.append(current)
            elif current_kind == "export" and current.get("name"):
                exports.append(current)
            current_kind = None
            current = {}
        elif current_kind == "import" and line.startswith("Name: "):
            current["dll"] = line.removeprefix("Name: ")
        elif current_kind == "import" and line.startswith("Symbol: "):
            symbol = re.sub(r"\s+\(\d+\)$", "", line.removeprefix("Symbol: "))
            current["symbols"].append(symbol)
        elif current_kind == "export" and line.startswith("Ordinal: "):
            current["ordinal"] = int(line.removeprefix("Ordinal: "))
        elif current_kind == "export" and line.startswith("Name: "):
            current["name"] = line.removeprefix("Name: ")

    result["imports"] = sorted(imports, key=lambda item: item["dll"].lower())
    result["exports"] = sorted(exports, key=lambda item: (item["ordinal"], item["name"]))
    return result


def parse_import_library_symbols(output: str) -> list[str]:
    return sorted(
        {
            line.strip().lstrip("\x7f")
            for line in output.splitlines()
            if "TerminalTry" in line and not line.rstrip().endswith(":")
        }
    )


def run_tool(arguments: list[str]) -> str:
    return subprocess.check_output(arguments, text=True, errors="replace")


def harvest_architecture(
    archive: zipfile.ZipFile,
    architecture: str,
    llvm_readobj: str,
    llvm_nm: str,
    directory: Path,
) -> dict[str, Any]:
    dll_path = f"runtimes/win10-{architecture}/native/TerminalThemeHelpers.dll"
    lib_path = f"runtimes/win10-{architecture}/lib/uap10.0/TerminalThemeHelpers.lib"
    dll_payload = archive.read(dll_path)
    lib_payload = archive.read(lib_path)
    temporary_dll = directory / f"{architecture}.dll"
    temporary_lib = directory / f"{architecture}.lib"
    temporary_dll.write_bytes(dll_payload)
    temporary_lib.write_bytes(lib_payload)

    pe = parse_llvm_readobj(
        run_tool(
            [
                llvm_readobj,
                "--file-headers",
                "--coff-exports",
                "--coff-imports",
                str(temporary_dll),
            ]
        )
    )
    symbols = parse_import_library_symbols(
        run_tool(
            [
                llvm_nm,
                "--defined-only",
                "--extern-only",
                "--format=just-symbols",
                str(temporary_lib),
            ]
        )
    )
    return {
        "dll": {"path": dll_path, "size": len(dll_payload), "sha256": sha256(dll_payload), **pe},
        "import_library": {
            "path": lib_path,
            "size": len(lib_payload),
            "sha256": sha256(lib_payload),
            "symbols": symbols,
        },
    }


def harvest(
    package_path: Path,
    source: str,
    llvm_readobj: str,
    llvm_nm: str,
) -> dict[str, Any]:
    package_payload = package_path.read_bytes()
    with zipfile.ZipFile(package_path) as archive:
        names = sorted(archive.namelist())
        nuspec_path = next(name for name in names if name.lower().endswith(".nuspec"))
        header_path = "inc/TerminalThemeHelpers.h"
        targets_path = "build/native/Microsoft.Internal.Windows.Terminal.ThemeHelpers.targets"
        header_payload = archive.read(header_path)
        targets_payload = archive.read(targets_path)
        metadata = package_metadata(read_text(archive, nuspec_path))
        if metadata["id"] != PACKAGE_ID:
            raise RuntimeError(f"unexpected package id: {metadata['id']}")

        with tempfile.TemporaryDirectory(prefix="openterminal-themehelpers-") as directory:
            architectures = {
                architecture: harvest_architecture(
                    archive,
                    architecture,
                    llvm_readobj,
                    llvm_nm,
                    Path(directory),
                )
                for architecture in ARCHITECTURES
            }

        file_manifest = [
            {
                "path": info.filename,
                "size": info.file_size,
                "sha256": sha256(archive.read(info.filename)),
            }
            for info in sorted(archive.infolist(), key=lambda item: item.filename)
            if not info.is_dir()
        ]
        header = read_text(archive, header_path)
        targets = read_text(archive, targets_path)

    functions = header_declarations(header)
    header_names = sorted(function["name"] for function in functions)
    export_maps = {
        architecture: [
            {"name": export["name"], "ordinal": export["ordinal"]}
            for export in data["dll"]["exports"]
        ]
        for architecture, data in architectures.items()
    }
    import_library_exports = {
        architecture: sorted(
            symbol
            for symbol in data["import_library"]["symbols"]
            if not symbol.startswith("__imp")
        )
        for architecture, data in architectures.items()
    }

    return {
        "schema_version": 1,
        "source": source,
        "package": {
            **metadata,
            "sha256": sha256(package_payload),
            "size": len(package_payload),
            "files": file_manifest,
        },
        "header": {
            "path": header_path,
            "sha256": sha256(header_payload),
            "language_linkage": "C when included from C++",
            "declared_calling_convention": "compiler default",
            "functions": functions,
        },
        "msbuild_integration": {
            "path": targets_path,
            "sha256": sha256(targets_payload),
            **targets_metadata(targets),
        },
        "architectures": architectures,
        "abi_consistency": {
            "exports_match_header": all(
                sorted(export["name"] for export in exports) == header_names
                for exports in export_maps.values()
            ),
            "export_ordinals_identical_across_architectures": len(
                {
                    tuple((export["ordinal"], export["name"]) for export in exports)
                    for exports in export_maps.values()
                }
            )
            == 1,
            "import_libraries_define_all_exports": all(
                sorted(symbol.lstrip("_") for symbol in symbols) == header_names
                for symbols in import_library_exports.values()
            ),
        },
        "abi_observations": {
            "x86_calling_convention_evidence": (
                "leading underscore without an @N suffix in the import library, consistent with MSVC __cdecl"
            ),
            "x64_and_arm64_decoration": "undecorated C symbol names",
        },
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("package", type=Path)
    parser.add_argument("--source", required=True)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--llvm-readobj", default="llvm-readobj")
    parser.add_argument("--llvm-nm", default="llvm-nm")
    args = parser.parse_args()

    result = harvest(args.package, args.source, args.llvm_readobj, args.llvm_nm)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )


if __name__ == "__main__":
    main()
