#!/usr/bin/env python3
"""Harvest reviewable WinUI/XAML contract data without redistributing binaries."""

from __future__ import annotations

import argparse
import hashlib
import io
import json
import re
import struct
import subprocess
import tempfile
import xml.etree.ElementTree as ET
import zipfile
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any

import dnfile

from harvest_themehelpers_abi import local_name, package_metadata


PACKAGE_ID = "Microsoft.UI.Xaml"
ARCHITECTURES = ("arm", "arm64", "x64", "x86")
APPX_PATH = "tools/AppX/{architecture}/Release/Microsoft.UI.Xaml.2.8.appx"
WINMD_PATH = "lib/uap10.0/Microsoft.UI.Xaml.winmd"
DOCUMENTATION_PATH = "lib/uap10.0/Microsoft.UI.Xaml.xml"
GENERIC_XAML_PATH = "lib/uap10.0/Microsoft.UI.Xaml/Themes/Generic.xaml"
PRI_PATH = "lib/uap10.0/Microsoft.UI.Xaml.pri"
GUID_ATTRIBUTE = "Windows.Foundation.Metadata.GuidAttribute"


def sha256(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def json_sha256(value: Any) -> str:
    return sha256(json.dumps(value, sort_keys=True, separators=(",", ":")).encode())


def read_text(archive: zipfile.ZipFile, path: str) -> str:
    return archive.read(path).decode("utf-8-sig")


def heap_text(value: Any) -> str:
    return "" if value is None else str(value)


def blob_hex(value: Any) -> str:
    payload = getattr(value, "value", b"") or b""
    return bytes(payload).hex()


def true_flags(flags: Any) -> list[str]:
    return sorted(name for name, enabled in vars(flags).items() if enabled)


def type_name(index: Any) -> str | None:
    if index is None or getattr(index, "row", None) is None:
        return None
    row = index.row
    if hasattr(row, "TypeName"):
        namespace = heap_text(row.TypeNamespace)
        name = heap_text(row.TypeName)
        return f"{namespace}.{name}" if namespace else name
    if hasattr(row, "Signature"):
        return f"TypeSpec({blob_hex(row.Signature)})"
    return f"{type(index.table).__name__}#{index.row_index}"


def attribute_type_name(attribute_type: Any) -> str | None:
    if attribute_type is None or getattr(attribute_type, "row", None) is None:
        return None
    constructor = attribute_type.row
    owner = getattr(constructor, "Class", None)
    return type_name(owner)


def decode_guid_attribute(payload: bytes) -> str:
    if len(payload) < 20 or payload[:2] != b"\x01\x00":
        raise ValueError("invalid GuidAttribute payload")
    data1, data2, data3, *tail = struct.unpack("<IHH8B", payload[2:18])
    return (
        f"{data1:08x}-{data2:04x}-{data3:04x}-"
        f"{tail[0]:02x}{tail[1]:02x}-"
        + "".join(f"{value:02x}" for value in tail[2:])
    )


def decode_constant(element_type: int, payload: bytes) -> Any:
    formats = {
        2: "<?",
        3: "<H",
        4: "<b",
        5: "<B",
        6: "<h",
        7: "<H",
        8: "<i",
        9: "<I",
        10: "<q",
        11: "<Q",
        12: "<f",
        13: "<d",
    }
    if element_type in formats and len(payload) == struct.calcsize(formats[element_type]):
        return struct.unpack(formats[element_type], payload)[0]
    if element_type == 14:
        return payload.decode("utf-16-le", errors="replace")
    return None


def winmd_metadata(path: Path) -> dict[str, Any]:
    pe = dnfile.dnPE(str(path))
    tables = pe.net.mdtables

    property_maps: dict[int, list[dict[str, Any]]] = defaultdict(list)
    if tables.PropertyMap:
        for row in tables.PropertyMap.rows:
            property_maps[row.Parent.row_index] = [
                {
                    "name": heap_text(entry.row.Name),
                    "signature": blob_hex(entry.row.Type),
                }
                for entry in row.PropertyList
            ]

    event_maps: dict[int, list[dict[str, Any]]] = defaultdict(list)
    if tables.EventMap:
        for row in tables.EventMap.rows:
            event_maps[row.Parent.row_index] = [
                {
                    "name": heap_text(entry.row.Name),
                    "type": type_name(entry.row.EventType),
                }
                for entry in row.EventList
            ]

    interface_maps: dict[int, list[str]] = defaultdict(list)
    if tables.InterfaceImpl:
        for row in tables.InterfaceImpl.rows:
            interface = type_name(row.Interface)
            if interface:
                interface_maps[row.Class.row_index].append(interface)

    constants: dict[int, dict[str, Any]] = {}
    if tables.Constant:
        for row in tables.Constant.rows:
            if type(row.Parent.table).__name__ != "Field":
                continue
            payload = bytes(row.Value.value or b"")
            constants[row.Parent.row_index] = {
                "element_type": row.Type,
                "value": decode_constant(row.Type, payload),
                "value_hex": payload.hex(),
            }

    guids: dict[int, str] = {}
    if tables.CustomAttribute:
        for row in tables.CustomAttribute.rows:
            if (
                type(row.Parent.table).__name__ == "TypeDef"
                and attribute_type_name(row.Type) == GUID_ATTRIBUTE
            ):
                guids[row.Parent.row_index] = decode_guid_attribute(
                    bytes(row.Value.value or b"")
                )

    type_definitions: list[dict[str, Any]] = []
    kind_counts: Counter[str] = Counter()
    namespace_counts: Counter[str] = Counter()
    for index, row in enumerate(tables.TypeDef.rows, 1):
        name = heap_text(row.TypeName)
        if not name:
            continue
        namespace = heap_text(row.TypeNamespace)
        full_name = f"{namespace}.{name}" if namespace else name
        extends = type_name(row.Extends)
        if row.Flags.tdInterface:
            kind = "interface"
        elif extends == "System.Enum":
            kind = "enum"
        elif extends == "System.ValueType":
            kind = "struct"
        elif extends == "System.MulticastDelegate":
            kind = "delegate"
        else:
            kind = "class"
        kind_counts[kind] += 1
        namespace_counts[namespace] += 1

        methods = []
        for method_index in row.MethodList:
            method = method_index.row
            methods.append(
                {
                    "flags": true_flags(method.Flags),
                    "name": heap_text(method.Name),
                    "parameters": [
                        {
                            "name": heap_text(parameter.row.Name),
                            "sequence": parameter.row.Sequence,
                        }
                        for parameter in method.ParamList
                    ],
                    "signature": blob_hex(method.Signature),
                }
            )

        fields = []
        for field_index in row.FieldList:
            field = field_index.row
            record: dict[str, Any] = {
                "flags": true_flags(field.Flags),
                "name": heap_text(field.Name),
                "signature": blob_hex(field.Signature),
            }
            if field_index.row_index in constants:
                record["constant"] = constants[field_index.row_index]
            fields.append(record)

        type_definitions.append(
            {
                "events": sorted(event_maps[index], key=lambda item: item["name"]),
                "extends": extends,
                "fields": fields,
                "flags": true_flags(row.Flags),
                "full_name": full_name,
                "guid": guids.get(index),
                "interfaces": sorted(interface_maps[index]),
                "kind": kind,
                "methods": methods,
                "properties": sorted(
                    property_maps[index], key=lambda item: item["name"]
                ),
            }
        )

    assembly_references = [
        {
            "name": heap_text(row.Name),
            "version": (
                f"{row.MajorVersion}.{row.MinorVersion}."
                f"{row.BuildNumber}.{row.RevisionNumber}"
            ),
        }
        for row in tables.AssemblyRef.rows
    ]
    return {
        "assembly_references": sorted(
            assembly_references, key=lambda item: item["name"]
        ),
        "metadata_version": pe.net.metadata.struct.Version.decode(
            "ascii", errors="replace"
        ).rstrip("\x00"),
        "namespace_type_counts": dict(sorted(namespace_counts.items())),
        "table_counts": {
            name: len(getattr(tables, name).rows)
            for name in (
                "AssemblyRef",
                "CustomAttribute",
                "Event",
                "Field",
                "InterfaceImpl",
                "MethodDef",
                "Property",
                "TypeDef",
                "TypeRef",
            )
        },
        "type_counts": dict(sorted(kind_counts.items())),
        "types": sorted(type_definitions, key=lambda item: item["full_name"]),
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
            current = {"ordinals": [], "symbols": []}
        elif line == "Export {":
            current_kind = "export"
            current = {}
        elif current_kind and line == "}":
            if current_kind == "import" and current.get("dll"):
                current["symbols"] = sorted(set(current["symbols"]))
                current["ordinals"] = sorted(set(current["ordinals"]))
                imports.append(current)
            elif current_kind == "export" and current.get("name"):
                exports.append(current)
            current_kind = None
            current = {}
        elif current_kind == "import" and line.startswith("Name: "):
            current["dll"] = line.removeprefix("Name: ")
        elif current_kind == "import" and line.startswith("Symbol: "):
            symbol_text = line.removeprefix("Symbol: ")
            match = re.match(r"^(.*?)\s*\((\d+)\)$", symbol_text)
            if match and match.group(1).strip():
                current["symbols"].append(match.group(1).strip())
            elif match:
                current["ordinals"].append(int(match.group(2)))
            elif symbol_text.strip():
                current["symbols"].append(symbol_text.strip())
        elif current_kind == "export" and line.startswith("Ordinal: "):
            current["ordinal"] = int(line.removeprefix("Ordinal: "))
        elif current_kind == "export" and line.startswith("Name: "):
            current["name"] = line.removeprefix("Name: ")

    result["imports"] = sorted(imports, key=lambda item: item["dll"].lower())
    result["exports"] = sorted(
        exports, key=lambda item: (item.get("ordinal", -1), item["name"])
    )
    return result


def appx_manifest_metadata(payload: bytes) -> dict[str, Any]:
    root = ET.fromstring(payload.decode("utf-8-sig"))
    identity = next(element for element in root.iter() if local_name(element.tag) == "Identity")
    properties_node = next(element for element in root.iter() if local_name(element.tag) == "Properties")
    return {
        "activatable_classes": sorted(
            [
                {
                    "id": element.get("ActivatableClassId", ""),
                    "threading_model": element.get("ThreadingModel", ""),
                }
                for element in root.iter()
                if local_name(element.tag) == "ActivatableClass"
            ],
            key=lambda item: item["id"],
        ),
        "extension_categories": sorted(
            {
                element.get("Category", "")
                for element in root.iter()
                if local_name(element.tag) == "Extension"
            }
        ),
        "identity": dict(sorted(identity.attrib.items())),
        "in_process_servers": sorted(
            {
                (element.text or "").strip()
                for server in root.iter()
                if local_name(server.tag) == "InProcessServer"
                for element in server
                if local_name(element.tag) == "Path"
            }
        ),
        "properties": {
            local_name(element.tag): (element.text or "").strip()
            for element in properties_node
        },
        "target_device_families": sorted(
            [
                dict(sorted(element.attrib.items()))
                for element in root.iter()
                if local_name(element.tag) == "TargetDeviceFamily"
            ],
            key=lambda item: json.dumps(item, sort_keys=True),
        ),
    }


def file_manifest(archive: zipfile.ZipFile) -> list[dict[str, Any]]:
    return [
        {
            "path": info.filename,
            "sha256": sha256(archive.read(info.filename)),
            "size": info.file_size,
        }
        for info in sorted(archive.infolist(), key=lambda item: item.filename)
        if not info.is_dir()
    ]


def xaml_metadata(payload: bytes) -> dict[str, Any]:
    namespaces: list[dict[str, str]] = []
    elements: Counter[str] = Counter()
    keys: list[str] = []
    names: list[str] = []
    style_targets: Counter[str] = Counter()
    template_targets: Counter[str] = Counter()
    for event, value in ET.iterparse(
        io.BytesIO(payload), events=("start", "start-ns")
    ):
        if event == "start-ns":
            prefix, uri = value
            record = {"prefix": prefix or "", "uri": uri}
            if record not in namespaces:
                namespaces.append(record)
            continue
        element = value
        tag = local_name(element.tag)
        elements[tag] += 1
        for attribute, attribute_value in element.attrib.items():
            attribute_name = local_name(attribute)
            if attribute_name == "Key":
                keys.append(attribute_value)
            elif attribute_name == "Name":
                names.append(attribute_value)
        target_type = element.get("TargetType")
        if tag == "Style" and target_type:
            style_targets[target_type] += 1
        elif tag == "ControlTemplate" and target_type:
            template_targets[target_type] += 1
    return {
        "control_template_targets": dict(sorted(template_targets.items())),
        "element_counts": dict(sorted(elements.items())),
        "named_elements": sorted(names),
        "namespaces": sorted(namespaces, key=lambda item: (item["prefix"], item["uri"])),
        "resource_keys": sorted(keys),
        "style_targets": dict(sorted(style_targets.items())),
    }


def documentation_metadata(payload: bytes) -> dict[str, Any]:
    root = ET.fromstring(payload.decode("utf-8-sig"))
    members = sorted(
        element.get("name", "")
        for element in root.iter()
        if local_name(element.tag) == "member"
    )
    return {
        "member_counts": dict(sorted(Counter(name.partition(":")[0] for name in members).items())),
        "members": members,
    }


def msbuild_metadata(archive: zipfile.ZipFile, paths: list[str]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for path in paths:
        root = ET.fromstring(read_text(archive, path))
        imports = []
        properties = []
        items = []
        targets = []
        for element in root.iter():
            tag = local_name(element.tag)
            if tag == "Import":
                imports.append(dict(sorted(element.attrib.items())))
            elif tag == "Target":
                targets.append(dict(sorted(element.attrib.items())))
        for group in root:
            group_tag = local_name(group.tag)
            if group_tag == "PropertyGroup":
                for element in group:
                    properties.append(
                        {
                            "condition": element.get("Condition"),
                            "name": local_name(element.tag),
                            "value": (element.text or "").strip(),
                        }
                    )
            elif group_tag == "ItemGroup":
                for element in group:
                    record: dict[str, Any] = {
                        "condition": element.get("Condition"),
                        "item_type": local_name(element.tag),
                        "metadata": {
                            local_name(child.tag): (child.text or "").strip()
                            for child in element
                        },
                    }
                    for name in ("Include", "Remove", "Update"):
                        if element.get(name) is not None:
                            record[name.lower()] = element.get(name)
                    items.append(record)
        result[path] = {
            "imports": imports,
            "items": items,
            "properties": properties,
            "targets": targets,
        }
    return result


def harvest_architecture(
    archive: zipfile.ZipFile,
    architecture: str,
    llvm_readobj: str,
    directory: Path,
) -> dict[str, Any]:
    path = APPX_PATH.format(architecture=architecture)
    appx_payload = archive.read(path)
    with zipfile.ZipFile(io.BytesIO(appx_payload)) as appx:
        dll_payload = appx.read("Microsoft.UI.Xaml.dll")
        winmd_payload = appx.read("Microsoft.UI.Xaml.winmd")
        pri_payload = appx.read("resources.pri")
        manifest_payload = appx.read("AppxManifest.xml")
        dll_path = directory / f"{architecture}.dll"
        winmd_path = directory / f"{architecture}.winmd"
        dll_path.write_bytes(dll_payload)
        winmd_path.write_bytes(winmd_payload)
        pe = parse_llvm_readobj(
            subprocess.check_output(
                [
                    llvm_readobj,
                    "--file-headers",
                    "--coff-exports",
                    "--coff-imports",
                    str(dll_path),
                ],
                text=True,
                errors="replace",
            )
        )
        nested_files = file_manifest(appx)
    return {
        "appx": {
            "files": nested_files,
            "path": path,
            "sha256": sha256(appx_payload),
            "size": len(appx_payload),
        },
        "dll": {
            "path": "Microsoft.UI.Xaml.dll",
            "sha256": sha256(dll_payload),
            "size": len(dll_payload),
            **pe,
        },
        "manifest": appx_manifest_metadata(manifest_payload),
        "pri": {
            "path": "resources.pri",
            "sha256": sha256(pri_payload),
            "size": len(pri_payload),
        },
        "winmd": {
            "path": "Microsoft.UI.Xaml.winmd",
            "semantic_contract_sha256": json_sha256(winmd_metadata(winmd_path)),
            "sha256": sha256(winmd_payload),
            "size": len(winmd_payload),
        },
    }


def harvest(package_path: Path, source: str, llvm_readobj: str) -> dict[str, Any]:
    package_payload = package_path.read_bytes()
    with zipfile.ZipFile(package_path) as archive:
        names = sorted(archive.namelist())
        nuspec_path = next(name for name in names if name.lower().endswith(".nuspec"))
        metadata = package_metadata(read_text(archive, nuspec_path))
        if metadata["id"] != PACKAGE_ID:
            raise RuntimeError(f"unexpected package id: {metadata['id']}")

        winmd_payload = archive.read(WINMD_PATH)
        documentation_payload = archive.read(DOCUMENTATION_PATH)
        xaml_payload = archive.read(GENERIC_XAML_PATH)
        pri_payload = archive.read(PRI_PATH)
        build_paths = sorted(
            name
            for name in names
            if (
                name.startswith(("build/", "buildTransitive/"))
                and name.lower().endswith((".props", ".targets"))
            )
        )
        with tempfile.TemporaryDirectory(prefix="openterminal-winui-") as temp:
            directory = Path(temp)
            winmd_path = directory / "Microsoft.UI.Xaml.winmd"
            winmd_path.write_bytes(winmd_payload)
            winmd = winmd_metadata(winmd_path)
            winmd_contract_sha256 = json_sha256(winmd)
            architectures = {
                architecture: harvest_architecture(
                    archive, architecture, llvm_readobj, directory
                )
                for architecture in ARCHITECTURES
            }
        package_files = file_manifest(archive)
        msbuild = msbuild_metadata(archive, build_paths)

    appx_winmd_hashes = {
        architecture: data["winmd"]["sha256"]
        for architecture, data in architectures.items()
    }
    appx_winmd_contract_hashes = {
        architecture: data["winmd"]["semantic_contract_sha256"]
        for architecture, data in architectures.items()
    }
    activatable_sets = {
        architecture: tuple(
            entry["id"] for entry in data["manifest"]["activatable_classes"]
        )
        for architecture, data in architectures.items()
    }
    export_sets = {
        architecture: tuple(
            (entry.get("ordinal"), entry["name"])
            for entry in data["dll"]["exports"]
        )
        for architecture, data in architectures.items()
    }
    return {
        "schema_version": 1,
        "source": source,
        "package": {
            **metadata,
            "files": package_files,
            "sha256": sha256(package_payload),
            "size": len(package_payload),
        },
        "winmd": {
            "path": WINMD_PATH,
            "sha256": sha256(winmd_payload),
            "size": len(winmd_payload),
            "semantic_contract_sha256": winmd_contract_sha256,
            **winmd,
        },
        "documentation_contract": {
            "path": DOCUMENTATION_PATH,
            "sha256": sha256(documentation_payload),
            **documentation_metadata(documentation_payload),
        },
        "generic_xaml": {
            "path": GENERIC_XAML_PATH,
            "sha256": sha256(xaml_payload),
            "size": len(xaml_payload),
            **xaml_metadata(xaml_payload),
        },
        "root_pri": {
            "path": PRI_PATH,
            "sha256": sha256(pri_payload),
            "size": len(pri_payload),
        },
        "msbuild_integration": msbuild,
        "architectures": architectures,
        "consistency": {
            "activatable_classes_identical_across_architectures": len(
                set(activatable_sets.values())
            )
            == 1,
            "appx_winmd_hashes_identical_across_architectures": len(
                set(appx_winmd_hashes.values())
            )
            == 1,
            "appx_winmd_semantic_contracts_identical": len(
                set(appx_winmd_contract_hashes.values())
            )
            == 1,
            "dll_exports_identical_across_architectures": len(
                set(export_sets.values())
            )
            == 1,
            "package_winmd_matches_appx_architectures": sorted(
                architecture
                for architecture, digest in appx_winmd_hashes.items()
                if digest == sha256(winmd_payload)
            ),
            "package_winmd_semantically_matches_all_appx_architectures": all(
                digest == winmd_contract_sha256
                for digest in appx_winmd_contract_hashes.values()
            ),
        },
        "harvest_limits": {
            "pri_payloads": "hashed and sized; semantic PRI dump requires MakePri or an open PRI parser",
            "winmd_signatures": "ECMA-335 signature blobs are recorded exactly as hexadecimal",
        },
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("package", type=Path)
    parser.add_argument("--source", required=True)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--llvm-readobj", default="llvm-readobj")
    args = parser.parse_args()
    result = harvest(args.package, args.source, args.llvm_readobj)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )


if __name__ == "__main__":
    main()
