#!/usr/bin/env python3
"""Generate TerminalSettingsEditor's static search index without PowerShell."""

from __future__ import annotations

import argparse
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path


XAML_NS = "http://schemas.microsoft.com/winfx/2006/xaml"
EDITOR_NS = "using:Microsoft.Terminal.Settings.Editor"
PROHIBITED_UIDS = {
    "Extensions_Scope",
    "Profile_MissingFontFaces",
    "Profile_ProportionalFontFaces",
    "ColorScheme_InboxSchemeDuplicate",
    "ColorScheme_ColorsHeader",
    "ColorScheme_Rename",
}
PROHIBITED_FILES = {
    "AISettings.xaml",
    "Profiles_Base_Orphaned.xaml",
    "EditAction.xaml",
    "MainPage.xaml",
}


@dataclass(frozen=True, order=True)
class Entry:
    resource_name: str
    parent_page: str
    navigation_param: str
    sub_page: str
    element_name: str
    filename: str


CLASS_MAP = {
    "Microsoft::Terminal::Settings::Editor::Launch": ("Nav_Launch/Content", "Launch_Nav", "BreadcrumbSubPage::None"),
    "Microsoft::Terminal::Settings::Editor::Interaction": ("Nav_Interaction/Content", "Interaction_Nav", "BreadcrumbSubPage::None"),
    "Microsoft::Terminal::Settings::Editor::GlobalAppearance": ("Nav_Appearance/Content", "GlobalAppearance_Nav", "BreadcrumbSubPage::None"),
    "Microsoft::Terminal::Settings::Editor::ColorSchemes": ("Nav_ColorSchemes/Content", "ColorSchemes_Nav", "BreadcrumbSubPage::None"),
    "Microsoft::Terminal::Settings::Editor::Rendering": ("Nav_Rendering/Content", "Rendering_Nav", "BreadcrumbSubPage::None"),
    "Microsoft::Terminal::Settings::Editor::Compatibility": ("Nav_Compatibility/Content", "Compatibility_Nav", "BreadcrumbSubPage::None"),
    "Microsoft::Terminal::Settings::Editor::Actions": ("Nav_Actions/Content", "Actions_Nav", "BreadcrumbSubPage::None"),
    "Microsoft::Terminal::Settings::Editor::NewTabMenu": ("Nav_NewTabMenu/Content", "NewTabMenu_Nav", "BreadcrumbSubPage::None"),
    "Microsoft::Terminal::Settings::Editor::Extensions": ("Nav_Extensions/Content", "Extensions_Nav", "BreadcrumbSubPage::None"),
    "Microsoft::Terminal::Settings::Editor::Profiles_Base": ("Nav_ProfileDefaults/Content", "GlobalProfile_Nav", "BreadcrumbSubPage::None"),
    "Microsoft::Terminal::Settings::Editor::Profiles_Appearance": ("Nav_ProfileDefaults/Content", "GlobalProfile_Nav", "BreadcrumbSubPage::Profile_Appearance"),
    "Microsoft::Terminal::Settings::Editor::Profiles_Terminal": ("Nav_ProfileDefaults/Content", "GlobalProfile_Nav", "BreadcrumbSubPage::Profile_Terminal"),
    "Microsoft::Terminal::Settings::Editor::Profiles_Advanced": ("Nav_ProfileDefaults/Content", "GlobalProfile_Nav", "BreadcrumbSubPage::Profile_Advanced"),
    "Microsoft::Terminal::Settings::Editor::AddProfile": ("Nav_AddNewProfile/Content", "AddProfile", "BreadcrumbSubPage::None"),
}


def is_profile_subpage(page_class: str) -> bool:
    return page_class.endswith(("::Profiles_Appearance", "::Profiles_Terminal", "::Profiles_Advanced"))


def add_entry(entries: set[Entry], resource: str, page: str, nav: str | None,
              subpage: str, element: str | None, filename: str) -> None:
    entries.add(Entry(resource, page, nav or "", subpage, element or "", filename))


def collect(source: Path) -> list[Entry]:
    entries: set[Entry] = set()
    for path in sorted(source.glob("*.xaml"), key=lambda item: item.name.casefold()):
        filename = path.name
        if filename.casefold() in {name.casefold() for name in PROHIBITED_FILES}:
            continue
        root = ET.parse(path).getroot()
        if root.tag.rsplit("}", 1)[-1] != "Page" and filename != "Appearances.xaml":
            continue
        page = (
            "Microsoft::Terminal::Settings::Editor::Profiles_Appearance"
            if filename == "Appearances.xaml"
            else root.attrib[f"{{{XAML_NS}}}Class"].replace(".", "::")
        )
        mapping = CLASS_MAP.get(page)
        if mapping and not is_profile_subpage(page):
            add_entry(entries, mapping[0], page, mapping[1], mapping[2], None, filename)
        elif not page.endswith("::EditColorScheme") and not is_profile_subpage(page):
            print(f"warning: no class map entry for {page} ({filename}); skipping")
            continue

        if filename == "ColorSchemes.xaml":
            add_entry(entries, "ColorScheme_AddNewButton/Text", page, mapping[1], mapping[2], "AddNewButton", filename)
        elif filename == "AddProfile.xaml":
            add_entry(entries, "AddProfile_AddNewTextBlock/Text", page, mapping[1], mapping[2], "AddNewButton", filename)

        for container in root.iter(f"{{{EDITOR_NS}}}SettingContainer"):
            uid = container.attrib.get(f"{{{XAML_NS}}}Uid")
            if not uid:
                print(f"warning: SettingContainer without x:Uid in {filename}; skipping")
                continue
            if uid.casefold() in {name.casefold() for name in PROHIBITED_UIDS}:
                continue
            name = container.attrib.get(f"{{{XAML_NS}}}Name", "")
            if filename == "Appearances.xaml":
                name = "App." + name
            include_build = True
            include_partial = False
            nav = mapping[1] if mapping else None
            subpage = mapping[2] if mapping else "BreadcrumbSubPage::None"
            if page.endswith("::NewTabMenu"):
                include_partial = True
                if "NewTabMenu_CurrentFolder" in uid:
                    nav = None
                    subpage = "BreadcrumbSubPage::NewTabMenu_Folder"
                    include_build = False
            elif page.endswith("::Profiles_Base") or is_profile_subpage(page):
                include_build = name not in {"Name", "Commandline"}
                include_partial = True
            elif page.endswith("::EditColorScheme"):
                subpage = "BreadcrumbSubPage::ColorSchemes_Edit"
                include_build = False
                include_partial = True
            if include_build:
                add_entry(entries, f"{uid}/Header", page, nav, subpage, name, filename)
            if include_partial:
                partial_subpage = (
                    "BreadcrumbSubPage::NewTabMenu_Folder"
                    if page.endswith("::NewTabMenu") else subpage
                )
                add_entry(entries, f"{uid}/Header", page, None, partial_subpage, name, filename)
    return sorted(entries)


def format_entries(entries: list[Entry]) -> str:
    return "\n".join(
        f'            IndexEntry{{ USES_RESOURCE(L"{entry.resource_name}"), '
        f'L"{entry.navigation_param}", {entry.sub_page}, L"{entry.element_name}" }}, '
        f'// {entry.filename}'
        for entry in entries
    )


def write_if_changed(path: Path, content: str) -> None:
    if path.exists() and path.read_text(encoding="utf-8") == content:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8", newline="\n")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    entries = collect(args.source)
    profiles = [entry for entry in entries if not entry.navigation_param and "Profiles_" in entry.parent_page]
    schemes = [entry for entry in entries if entry.sub_page == "BreadcrumbSubPage::ColorSchemes_Edit"]
    ntm = [entry for entry in entries if entry.sub_page == "BreadcrumbSubPage::NewTabMenu_Folder"]
    specialized = set(profiles + schemes + ntm)
    build = [entry for entry in entries if entry not in specialized]

    header = f'''/*++
Copyright (c) Microsoft Corporation
Licensed under the MIT license.
--*/
// Generated deterministically by phase2/scripts/generate_settings_index.py.
#pragma once
#include <winrt/Windows.UI.Xaml.Interop.h>

namespace winrt::Microsoft::Terminal::Settings::Editor::implementation
{{
    struct IndexEntry
    {{
        wil::zwstring_view ResourceName;
        wil::zwstring_view NavigationArgTag;
        BreadcrumbSubPage SubPage;
        wil::zwstring_view ElementName;
    }};

    const std::array<IndexEntry, {len(build)}>& LoadBuildTimeIndex();
    const std::array<IndexEntry, {len(profiles)}>& LoadProfileIndex();
    const std::array<IndexEntry, {len(ntm)}>& LoadNTMFolderIndex();
    const std::array<IndexEntry, {len(schemes)}>& LoadColorSchemeIndex();

    const IndexEntry& PartialProfileIndexEntry();
    const IndexEntry& PartialNTMFolderIndexEntry();
    const IndexEntry& PartialColorSchemeIndexEntry();
    const IndexEntry& PartialExtensionIndexEntry();
    const IndexEntry& PartialActionIndexEntry();
}}
'''

    def function(name: str, values: list[Entry]) -> str:
        return f'''    const std::array<IndexEntry, {len(values)}>& {name}()
    {{
        STATIC_INDEX_QUALIFIER std::array entries =
        {{
{format_entries(values)}
        }};
        return entries;
    }}
'''

    cpp = '''// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.
// Generated deterministically by phase2/scripts/generate_settings_index.py.

#include "pch.h"
#include <winrt/Microsoft.Terminal.Settings.Editor.h>
#include "GeneratedSettingsIndex.g.h"
#include <LibraryResources.h>

#ifdef _DEBUG
#define STATIC_INDEX_QUALIFIER static const
#else
#define STATIC_INDEX_QUALIFIER static constexpr
#endif

namespace winrt::Microsoft::Terminal::Settings::Editor::implementation
{
''' + function("LoadBuildTimeIndex", build) + "\n" + function("LoadProfileIndex", profiles) + "\n" + function("LoadNTMFolderIndex", ntm) + "\n" + function("LoadColorSchemeIndex", schemes) + '''
    const IndexEntry& PartialProfileIndexEntry()
    {
        static constexpr IndexEntry entry{ .SubPage = BreadcrumbSubPage::None };
        return entry;
    }
    const IndexEntry& PartialNTMFolderIndexEntry()
    {
        static constexpr IndexEntry entry{ .SubPage = BreadcrumbSubPage::NewTabMenu_Folder };
        return entry;
    }
    const IndexEntry& PartialColorSchemeIndexEntry()
    {
        static constexpr IndexEntry entry{ .SubPage = BreadcrumbSubPage::ColorSchemes_Edit };
        return entry;
    }
    const IndexEntry& PartialExtensionIndexEntry()
    {
        static constexpr IndexEntry entry{ .SubPage = BreadcrumbSubPage::Extensions_Extension };
        return entry;
    }
    const IndexEntry& PartialActionIndexEntry()
    {
        static constexpr IndexEntry entry{ .SubPage = BreadcrumbSubPage::Actions_Edit };
        return entry;
    }
}
'''
    write_if_changed(args.output / "GeneratedSettingsIndex.g.h", header)
    write_if_changed(args.output / "GeneratedSettingsIndex.g.cpp", cpp)
    print(f"generated {len(entries)} unique search-index entries")


if __name__ == "__main__":
    main()
