#!/usr/bin/env python3
"""Prepare open headers that need a case/path-safe MinGW build-tree copy."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


QUOTED_INCLUDE = re.compile(r'^(\s*#\s*include\s*")([^"]+)(".*)$')


def normalize_msvc_includes(content: str) -> str:
    content = content.replace(
        "DesktopWindowXamlSource{}", "OpenTerminalDesktopWindowXamlSource()"
    )
    content = content.replace(
        "winrt::Windows::System::DispatcherQueue::GetForCurrentThread()",
        "OpenTerminalDispatcherQueue()",
    )
    content = content.replace(
        "DispatcherQueue::GetForCurrentThread()",
        "OpenTerminalDispatcherQueue()",
    )
    content = content.replace(
        "ResourceManager::Current()", "OpenTerminalResourceManager()"
    )
    content = content.replace(
        "ResourceContext::GetForViewIndependentUse()",
        "OpenTerminalResourceContext()",
    )
    content = content.replace(
        "winrt::Windows::ApplicationModel::Resources::Core::"
        "OpenTerminalResourceManager()",
        "OpenTerminalResourceManager()",
    )
    content = content.replace(
        "winrt::Windows::ApplicationModel::Resources::Core::"
        "OpenTerminalResourceContext()",
        "OpenTerminalResourceContext()",
    )
    lines: list[str] = []
    for line in content.splitlines():
        match = QUOTED_INCLUDE.match(line)
        if match:
            include_path = match.group(2).replace("\\", "/")
            line = f"{match.group(1)}{include_path}{match.group(3)}"
        lines.append(line)
    return "\n".join(lines) + "\n"


def write_if_changed(path: Path, content: str) -> None:
    if path.exists() and path.read_text(encoding="utf-8") == content:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8", newline="\n")


def normalized_copy(source: Path, destination: Path) -> None:
    write_if_changed(
        destination,
        normalize_msvc_includes(source.read_text(encoding="utf-8-sig")),
    )


def prepare_winrt_utils(source: Path, destination: Path) -> None:
    content = normalize_msvc_includes(source.read_text(encoding="utf-8-sig"))
    old = "dict.as<winrt::Windows::UI::Xaml::ResourceDictionary>()"
    new = "dict.template as<winrt::Windows::UI::Xaml::ResourceDictionary>()"
    if content.count(old) != 1:
        raise RuntimeError("expected one dependent WinRT cast in WinRTUtils/Utils.h")
    write_if_changed(destination, content.replace(old, new))


def prepare_xaml_uia_text_range(source: Path, destination: Path) -> None:
    content = normalize_msvc_includes(source.read_text(encoding="utf-8-sig"))
    old = '#include "TermControlAutomationPeer.h"'
    replacement = (
        "// TermControlAutomationPeer is unrelated to this wrapper. Its weak_ref "
        "member also requires a complete TermControl type with standard C++.\n"
        "#include <winrt/Windows.UI.Xaml.Automation.Provider.h>"
    )
    if content.count(old) != 1:
        raise RuntimeError("expected one incidental TermControlAutomationPeer include")
    write_if_changed(destination, content.replace(old, replacement))


def prepare_term_control(source: Path, destination: Path) -> None:
    content = normalize_msvc_includes(source.read_text(encoding="utf-8-sig"))
    old = "friend struct TermControlT<TermControl>;"
    new = "friend TermControlT<TermControl>;"
    if content.count(old) != 1:
        raise RuntimeError("expected one MSVC-style alias friend declaration")
    write_if_changed(destination, content.replace(old, new))


def prepare_term_control_source(source: Path, destination: Path) -> None:
    content = normalize_msvc_includes(source.read_text(encoding="utf-8-sig"))
    for enum_type in (
        "winrt::Microsoft::Terminal::Control::CopyFormat",
        "winrt::Microsoft::Terminal::Control::MouseButtonState",
    ):
        old = f"DEFINE_ENUM_FLAG_OPERATORS({enum_type});\n"
        if content.count(old) != 1:
            raise RuntimeError(f"expected one flag-operator declaration for {enum_type}")
        # C++/WinRT already emits these enum operators. The SDK macro adds a
        # second global overload set that is ambiguous under GCC.
        content = content.replace(old, "")

    for constant in (
        "HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)",
        "HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND)",
        "D2DERR_SHADER_COMPILE_FAILED",
        "DWRITE_E_NOFONT",
    ):
        old = f"case {constant}:"
        new = f"case static_cast<uint32_t>({constant}):"
        if content.count(old) != 1:
            raise RuntimeError(f"expected one renderer-warning case for {constant}")
        content = content.replace(old, new)

    content += '#include "Microsoft/Terminal/Control/TermControl.xaml.g.hpp"\n'
    write_if_changed(destination, content)


def prepare_search_box_source(source: Path, destination: Path) -> None:
    content = normalize_msvc_includes(source.read_text(encoding="utf-8-sig"))
    content += '#include "Microsoft/Terminal/Control/SearchBoxControl.xaml.g.hpp"\n'
    write_if_changed(destination, content)


def prepare_uimarkdown_code_block(source: Path, destination: Path) -> None:
    content = normalize_msvc_includes(source.read_text(encoding="utf-8-sig"))
    content += '#include "Microsoft/Terminal/UI/Markdown/CodeBlock.xaml.g.hpp"\n'
    write_if_changed(destination, content)


def prepare_icon_path_converter(source: Path, destination: Path) -> None:
    content = normalize_msvc_includes(source.read_text(encoding="utf-8-sig"))
    old = """        return _convertToSoftwareBitmap(hicon.get(),
                                        BitmapPixelFormat::Bgra8,
                                        BitmapAlphaMode::Premultiplied,
                                        wicImagingFactory.get());"""
    new = """        try
        {
            return _convertToSoftwareBitmap(hicon.get(),
                                            BitmapPixelFormat::Bgra8,
                                            BitmapAlphaMode::Premultiplied,
                                            wicImagingFactory.get());
        }
        catch (const winrt::hresult_class_not_registered&)
        {
            // SoftwareBitmap's native WIC bridge is optional outside Windows.
            // A missing bridge means this profile icon is unavailable, not that
            // Terminal startup must fail.
            return nullptr;
        }"""
    if content.count(old) != 1:
        raise RuntimeError("expected SoftwareBitmap WIC conversion")
    content = content.replace(old, new)
    old_source = """        winrt::Windows::UI::Xaml::Media::Imaging::SoftwareBitmapSource bitmapSource{};
        bitmapSource.SetBitmapAsync(swBitmap);
        return bitmapSource;"""
    new_source = """        try
        {
            winrt::Windows::UI::Xaml::Media::Imaging::SoftwareBitmapSource bitmapSource{};
            bitmapSource.SetBitmapAsync(swBitmap);
            return bitmapSource;
        }
        catch (const winrt::hresult_class_not_registered&)
        {
            return nullptr;
        }
        catch (const winrt::hresult_wrong_thread&)
        {
            // OpenXaml owns this thread's XAML state, so Windows' system XAML
            // image source cannot be composed here. The profile icon is
            // optional; retain the profile and omit only its bitmap.
            return nullptr;
        }"""
    if content.count(old_source) != 1:
        raise RuntimeError("expected SoftwareBitmapSource construction")
    write_if_changed(destination, content.replace(old_source, new_source))


def prepare_cppwinrt_utils(source: Path, destination: Path) -> None:
    content = normalize_msvc_includes(source.read_text(encoding="utf-8-sig"))
    marker = "// This macro defines a dependency property for a WinRT class."
    helper = """template<typename T>
T dependency_property_default_value()
{
    if constexpr (std::is_same_v<T, winrt::hstring>)
    {
        return winrt::hstring{};
    }
    else if constexpr (std::is_base_of_v<winrt::Windows::Foundation::IInspectable, T>)
    {
        return { nullptr };
    }
    else
    {
        return {};
    }
}

"""
    if content.count(marker) != 1:
        raise RuntimeError("expected dependency-property macro marker")
    content = content.replace(marker, helper + marker)
    old = """        if constexpr (std::is_same_v<type, winrt::hstring>)                                   \\
        {                                                                                     \\
            return winrt::hstring{};                                                          \\
        }                                                                                     \\
        else if constexpr (std::is_base_of_v<winrt::Windows::Foundation::IInspectable, type>) \\
        {                                                                                     \\
            return { nullptr };                                                               \\
        }                                                                                     \\
        else                                                                                  \\
        {                                                                                     \\
            return {};                                                                        \\
        }                                                                                     \\
"""
    new = "        return dependency_property_default_value<type>();                                      \\\n"
    if content.count(old) != 1:
        raise RuntimeError("expected dependency-property default-value branches")
    write_if_changed(destination, content.replace(old, new))


SETTINGS_EDITOR_XAML_CLASSES = {
    "Actions",
    "EditAction",
    "AddProfile",
    "GlobalAppearance",
    "ColorSchemes",
    "NullableColorPicker",
    "IconPicker",
    "EditColorScheme",
    "Interaction",
    "Compatibility",
    "KeyChordListener",
    "Launch",
    "NewTabMenu",
    "MainPage",
    "Extensions",
    "Profiles_Base",
    "Profiles_Base_Orphaned",
    "Profiles_Advanced",
    "Profiles_Appearance",
    "Profiles_Terminal",
    "Appearances",
    "Rendering",
}

TERMINAL_APP_XAML_CLASSES = {
    "App",
    "AboutDialog",
    "MinMaxCloseControl",
    "TerminalPage",
    "TitlebarControl",
    "TabRowControl",
    "TabHeaderControl",
    "ColorPickupFlyout",
    "CommandPalette",
    "SuggestionsControl",
    "SnippetsPaneContent",
    "MarkdownPaneContent",
}


def prepare_settings_editor(source: Path, destination: Path) -> None:
    for pattern in ("*.cpp", "*.h", "*.hpp"):
        for path in sorted(source.glob(pattern)):
            content = normalize_msvc_includes(path.read_text(encoding="utf-8-sig"))
            content = re.sub(
                r'#include\s+["<][^">]*cppwinrt_utils\.h[">]',
                '#include "cppwinrt_utils_mingw.h"',
                content,
            )
            content = re.sub(
                r'#include\s+["<][^">\n]*WinRTUtils[\\/]+inc[\\/]+Utils\.h[">]',
                '#include <Utils.h>',
                content,
            )
            if path.name == "Utils.h":
                old = ".try_as<winrt::Windows::UI::Xaml::Controls::Control>()"
                if content.count(old) != 1:
                    raise RuntimeError("expected dependent SettingsEditor control cast")
                content = content.replace(old, ".template try_as<winrt::Windows::UI::Xaml::Controls::Control>()")
            if path.name == "EditAction.cpp":
                for old, new in (
                    ("args.try_as<KeyChordViewModel>()", "args.template try_as<KeyChordViewModel>()"),
                    ("container.as<Controls::ListViewItem>()", "container.template as<Controls::ListViewItem>()"),
                ):
                    if content.count(old) != 1:
                        raise RuntimeError(f"expected dependent EditAction cast: {old}")
                    content = content.replace(old, new)
            if path.name == "Appearances.h":
                old = "winrt::weak_ref<AppearanceViewModel>"
                if content.count(old) != 2:
                    raise RuntimeError("expected two weak AppearanceViewModel declarations")
                content = content.replace(
                    old, "winrt::weak_ref<Editor::AppearanceViewModel>"
                )
            if path.name == "Appearances.cpp":
                old = "winrt::weak_ref<AppearanceViewModel> vm"
                if content.count(old) != 1:
                    raise RuntimeError("expected weak AppearanceViewModel constructor")
                content = content.replace(
                    old, "winrt::weak_ref<Editor::AppearanceViewModel> vm"
                )
                old = "vm->UpdateFontSetting(this);"
                if content.count(old) != 1:
                    raise RuntimeError("expected AppearanceViewModel weak invocation")
                content = content.replace(
                    old, "winrt::get_self<AppearanceViewModel>(vm)->UpdateFontSetting(this);"
                )
                old = "winrt::make<FontKeyValuePair>(get_weak(),"
                if content.count(old) != 3:
                    raise RuntimeError("expected three weak font-setting owners")
                content = content.replace(
                    old,
                    "winrt::make<FontKeyValuePair>(winrt::make_weak(get_strong().as<Editor::AppearanceViewModel>()),",
                )
            if path.name in {"Appearances.cpp", "ProfileViewModel.cpp"}:
                old = "__uuidof(factory)"
                expected = 2 if path.name == "Appearances.cpp" else 1
                if content.count(old) != expected:
                    raise RuntimeError(
                        f"expected {expected} DirectWrite factory UUID use(s) in {path.name}"
                    )
                content = content.replace(old, "__uuidof(IDWriteFactory)")
            if path.name == "ActionsViewModel.cpp":
                if content.count("isnan(") != 4:
                    raise RuntimeError("expected four optional numeric NaN checks")
                content = content.replace("isnan(", "std::isnan(")
            if path.name == "LaunchViewModel.cpp":
                if content.count("isnan(") != 6:
                    raise RuntimeError("expected six launch-position NaN checks")
                content = content.replace("isnan(", "std::isnan(")
            if path.suffix == ".cpp" and path.stem in SETTINGS_EDITOR_XAML_CLASSES:
                if path.name == "EditAction.cpp":
                    content += '#include "NullableColorPicker.h"\n#include "KeyChordListener.h"\n'
                elif path.name == "NewTabMenu.cpp":
                    content += '#include "IconPicker.h"\n'
                elif path.name in {"Profiles_Base.cpp", "Appearances.cpp"}:
                    content += '#include "NullableColorPicker.h"\n'
                    if path.name == "Profiles_Base.cpp":
                        content += '#include "IconPicker.h"\n'
                content += (
                    f'#include "Microsoft/Terminal/Settings/Editor/'
                    f'{path.stem}.xaml.g.hpp"\n'
                )
            write_if_changed(destination / path.name, content)


def prepare_terminal_app(source: Path, destination: Path) -> None:
    for pattern in ("*.cpp", "*.h", "*.hpp"):
        for path in sorted(source.glob(pattern)):
            content = normalize_msvc_includes(path.read_text(encoding="utf-8-sig"))
            content = re.sub(
                r'#include\s+["<][^">]*cppwinrt_utils\.h[">]',
                '#include "cppwinrt_utils_mingw.h"',
                content,
            )
            content = re.sub(
                r'#include\s+["<][^">\n]*WinRTUtils[\\/]+inc[\\/]+Utils\.h[">]',
                '#include <Utils.h>',
                content,
            )
            content = content.replace("#include <ShObjIdl.h>", "#include <shobjidl.h>")
            content = content.replace("#include <Propkey.h>", "#include <propkey.h>")
            if path.name == "CommandPaletteItems.cpp":
                old = "sender.try_as<winrt::TerminalApp::Tab>()"
                if content.count(old) != 1:
                    raise RuntimeError("expected dependent command-palette Tab cast")
                content = content.replace(
                    old, "sender.template try_as<winrt::TerminalApp::Tab>()"
                )
            if path.name == "App.h":
                old = '#include "App.base.h"'
                if content.count(old) != 1:
                    raise RuntimeError("expected TerminalApp metadata-provider include point")
                content = content.replace(
                    old, '#include "XamlMetaDataProvider.h"\n#include "App.base.h"'
                )
            if path.name == "App.base.h":
                old = (
                    "struct App_baseWithProvider : public App_base<D, "
                    "::winrt::Windows::UI::Xaml::Markup::IXamlMetadataProvider>"
                )
                new = (
                    "struct App_baseWithProvider : public AppT<D, "
                    "::winrt::Windows::UI::Xaml::Markup::IXamlMetadataProvider>"
                )
                if content.count(old) != 1:
                    raise RuntimeError("expected TerminalApp custom application base")
                content = content.replace(old, new)
            if path.name == "App.cpp":
                old_include = '#include "pch.h"'
                if content.count(old_include) != 1:
                    raise RuntimeError("expected TerminalApp precompiled-header include")
                content = content.replace(
                    old_include,
                    old_include + '\n#include "xaml_metadata_provider_compat.h"',
                )
                replacements = (
                    (
                        "winrt::Microsoft::Terminal::Control::XamlMetaDataProvider{}",
                        "OpenTerminalControlXamlMetadataProvider()",
                    ),
                    (
                        "winrt::Microsoft::Terminal::Settings::Editor::XamlMetaDataProvider{}",
                        "OpenTerminalSettingsEditorXamlMetadataProvider()",
                    ),
                )
                for old, new in replacements:
                    if content.count(old) != 1:
                        raise RuntimeError(f"expected external XAML provider: {old}")
                    content = content.replace(old, new)
                old_manager = """        const auto dispatcherQueue = OpenTerminalDispatcherQueue();
        if (!dispatcherQueue)
        {
            _windowsXamlManager = xaml::Hosting::WindowsXamlManager::InitializeForCurrentThread();
        }
        else
        {
            FAIL_FAST_MSG("Terminal is not intended to run as a Universal Windows Application");
        }"""
                new_manager = """        void* manager{};
        winrt::check_hresult(OpenXamlInitializeForCurrentThread(&manager));
        _windowsXamlManager = xaml::Hosting::WindowsXamlManager{
            manager, winrt::take_ownership_from_abi};"""
                if content.count(old_manager) != 1:
                    raise RuntimeError("expected system XAML manager/dispatcher initialization")
                content = content.replace(old_manager, new_manager)
                high_contrast = (
                    "        HighContrastAdjustment(::winrt::Windows::UI::Xaml::"
                    "ApplicationHighContrastAdjustment::None);\n"
                )
                if content.count(high_contrast) != 1:
                    raise RuntimeError("expected system XAML high-contrast adjustment")
                content = content.replace(high_contrast, "")
            if path.name == "Pane.cpp":
                duration = (
                    "DurationHelper::FromTimeSpan(winrt::Windows::Foundation::"
                    "TimeSpan(std::chrono::milliseconds(AnimationDurationInMilliseconds)))"
                )
                aggregate_duration = (
                    "Duration{winrt::Windows::Foundation::TimeSpan("
                    "std::chrono::milliseconds(AnimationDurationInMilliseconds)), "
                    "DurationType::TimeSpan}"
                )
                if content.count(duration) != 1:
                    raise RuntimeError("expected static Pane animation duration")
                content = content.replace(duration, aggregate_duration)
            if path.name == "Jumplist.cpp":
                old = (
                    "DEFINE_PROPERTYKEY(PKEY_AppUserModel_DestListLogoUri, "
                    "0x9F4C2855, 0x9F79, 0x4B39, 0xA8, 0xD0, 0xE1, 0xD4, "
                    "0x2D, 0xE1, 0xD5, 0xF3, 29);"
                )
                new = (
                    "extern const PROPERTYKEY PKEY_AppUserModel_DestListLogoUri{"
                    "{ 0x9F4C2855, 0x9F79, 0x4B39, "
                    "{ 0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3 } }, 29 };"
                )
                if content.count(old) != 1:
                    raise RuntimeError("expected destination-list logo property key")
                content = content.replace(old, new)
            if path.name == "LanguageProfileNotifier.cpp":
                old = """    if (FAILED(_source->AdviseSink(IID_ITfInputProcessorProfileActivationSink, static_cast<ITfInputProcessorProfileActivationSink*>(this), &_cookie)))
    {
        _cookie = TF_INVALID_COOKIE;
        THROW_LAST_ERROR();
    }"""
                old_setup = """    const auto manager = wil::CoCreateInstance<ITfThreadMgr>(CLSID_TF_ThreadMgr);
    _source = manager.query<ITfSource>();
""" + old
                new = """    try
    {
        const auto manager = wil::CoCreateInstance<ITfThreadMgr>(CLSID_TF_ThreadMgr);
        _source = manager.query<ITfSource>();
        if (FAILED(_source->AdviseSink(IID_ITfInputProcessorProfileActivationSink, static_cast<ITfInputProcessorProfileActivationSink*>(this), &_cookie)))
        {
            _cookie = TF_INVALID_COOKIE;
            _source.reset();
        }
    }
    catch (...)
    {
        // Language-profile notifications are optional. In unpackaged and
        // compatibility environments TSF creation, ITfSource acquisition, or
        // sink registration may fail even though the terminal is usable.
        _cookie = TF_INVALID_COOKIE;
        _source.reset();
    }"""
                if content.count(old_setup) != 1:
                    raise RuntimeError("expected TSF language-profile subscription")
                content = content.replace(old_setup, new)
            if path.name == "Tab.cpp":
                dependent_casts = (
                    "p->GetContent().try_as<SnippetsPaneContent>()",
                    "p->GetContent().try_as<MarkdownPaneContent>()",
                    "content.try_as<winrt::TerminalApp::TerminalPaneContent>()",
                    "sender.try_as<TermControl>()",
                )
                expected_counts = (1, 1, 3, 3)
                for old, expected in zip(dependent_casts, expected_counts):
                    if content.count(old) != expected:
                        raise RuntimeError(f"expected {expected} dependent Tab cast(s): {old}")
                    content = content.replace(old, old.replace(".try_as<", ".template try_as<"))
            if path.name == "TerminalPage.cpp":
                dependent_casts = (
                    "sender.try_as<MUX::Controls::CommandBarFlyout>()",
                    "sender.try_as<Controls::MenuFlyout>()",
                    "p->GetContent().try_as<SnippetsPaneContent>()",
                    "conn.try_as<winrt::Microsoft::Terminal::TerminalConnection::ConptyConnection>()",
                )
                expected_counts = (2, 1, 1, 1)
                for old, expected in zip(dependent_casts, expected_counts):
                    if content.count(old) != expected:
                        raise RuntimeError(
                            f"expected {expected} dependent TerminalPage cast(s): {old}"
                        )
                    content = content.replace(old, old.replace(".try_as<", ".template try_as<"))
                old = "TerminalTrySetWindowAssociatedProcesses("
                if content.count(old) != 1:
                    raise RuntimeError("expected Terminal process-association API call")
                # Preserve the upstream diagnostic message while renaming the call.
                content = content.replace(
                    "const auto hr = TerminalTrySetWindowAssociatedProcesses(",
                    "const auto hr = OpenTerminalTrySetWindowAssociatedProcesses(",
                )
                old = """        if (_tabs.Size() == 0)
        {
            CloseWindowRequested.raise(*this, nullptr);
            co_return;
        }
        else
        {"""
                new = """        // OpenXaml dispatches startup actions asynchronously. The first
        // new-tab action can still be queued when initialization reaches this
        // point, so zero tabs is not proof that an elevation handoff occurred.
        // Queue the zero-tab decision behind that action.
        {"""
                if content.count(old) != 1:
                    raise RuntimeError("expected zero-tab startup close check")
                content = content.replace(old, new)
                old = """            Dispatcher().RunAsync(CoreDispatcherPriority::Low, [weak = get_weak()]() {
                if (auto self{ weak.get() })
                {
                    self->Initialized.raise(*self, nullptr);
                }
            });"""
                new = """            Dispatcher().RunAsync(CoreDispatcherPriority::Low, [weak = get_weak()]() {
                if (auto self{ weak.get() })
                {
                    if (self->_tabs.Size() == 0)
                    {
                        self->CloseWindowRequested.raise(*self, nullptr);
                    }
                    else
                    {
                        self->Initialized.raise(*self, nullptr);
                    }
                }
            });"""
                if content.count(old) != 1:
                    raise RuntimeError("expected deferred initialization callback")
                content = content.replace(old, new)
                old = """            });
        }
    }

    // Method Description:
    // - Show a dialog with \"About\" information."""
                new = """            });
        }
        co_return;
    }

    // Method Description:
    // - Show a dialog with \"About\" information."""
                if content.count(old) != 1:
                    raise RuntimeError("expected initialization completion tail")
                content = content.replace(old, new)
                # C++/WinRT's get_self helper takes the address of a member
                # through the projected ABI pointer. MSVC happens to preserve
                # null here, while GCC materializes the implementation offset
                # (for Tab it becomes -16) and com_ptr::copy_from then reads
                # its reference count at -8. Terminal intentionally passes a
                # null source tab when opening its first session, so retain
                # the API's documented nullable contract before get_self.
                old = """    winrt::com_ptr<Tab> TerminalPage::_GetTabImpl(const TerminalApp::Tab& tab)
    {
        winrt::com_ptr<Tab> tabImpl;
"""
                new = """    winrt::com_ptr<Tab> TerminalPage::_GetTabImpl(const TerminalApp::Tab& tab)
    {
        if (!tab)
        {
            return nullptr;
        }
        winrt::com_ptr<Tab> tabImpl;
"""
                if content.count(old) != 1:
                    raise RuntimeError("expected nullable TerminalPage tab helper")
                content = content.replace(old, new)
            if path.suffix == ".cpp" and path.stem in TERMINAL_APP_XAML_CLASSES:
                content += f'#include "TerminalApp/{path.stem}.xaml.g.hpp"\n'
            write_if_changed(destination / path.name, content)


def prepare_windows_terminal(source: Path, destination: Path) -> None:
    for pattern in ("*.cpp", "*.h", "*.hpp"):
        for path in sorted(source.glob(pattern)):
            content = normalize_msvc_includes(path.read_text(encoding="utf-8-sig"))
            content = re.sub(
                r'#include\s+["<][^">]*cppwinrt_utils\.h[">]',
                '#include "cppwinrt_utils_mingw.h"',
                content,
            )
            content = re.sub(
                r'#include\s+"(?:\.\./)+(?:types/inc/)?([^"/]+)"',
                r'#include <\1>',
                content,
            )
            content = content.replace(
                '#include "../inc/LibraryIncludes.h"', '#include <LibraryIncludes.h>'
            )
            for old, new in (
                ("<Unknwn.h>", "<unknwn.h>"),
                ("<UIAutomation.h>", "<uiautomation.h>"),
                ("<ShObjIdl.h>", "<shobjidl.h>"),
                ("<shlobj_core.h>", "<shlobj.h>"),
                ("<WinUser.h>", "<winuser.h>"),
                ("<Viewport.hpp>", "<viewport.hpp>"),
                ("<CoreWindow.h>", '"CoreWindow_mingw.h"'),
            ):
                content = content.replace(old, new)
            if path.name == "pch.h":
                old = "#include <wil/win32_helpers.h>"
                if content.count(old) != 1:
                    raise RuntimeError("expected WindowsTerminal WIL include point")
                content = content.replace(
                    old, old + '\n#include "wil_prop_variant_compat.h"'
                )
            if path.name == "main.cpp":
                old = "int __stdcall wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int nCmdShow)\n{"
                new = old + "\n    OpenTerminalInstallActivationHandler();"
                if content.count(old) != 1:
                    raise RuntimeError("expected WindowsTerminal entry point")
                content = content.replace(old, new)
            if path.name == "WindowEmperor.cpp":
                old = """            // If we created no windows, e.g. because the args are \"/?\" we can just exit now.
            _postQuitMessageIfNeeded();"""
                new = """            // OpenXaml's compatibility DispatcherQueue enqueues window creation
            // asynchronously. An eager zero-window check here races that queued
            // callback and posts WM_QUIT before the first AppHost exists. Actual
            // window removal still calls _postQuitMessageIfNeeded(), so defer
            // lifetime decisions until a window has really been created."""
                if content.count(old) != 1:
                    raise RuntimeError("expected eager startup quit check")
                content = content.replace(old, new)
            write_if_changed(destination / path.name, content)

    resource = source / "WindowsTerminal.rc"
    resource_content = resource.read_text(encoding="utf-8-sig").replace("\\\\", "/")
    write_if_changed(destination / resource.name, resource_content)


def prepare_atlas_engine(source: Path, destination: Path) -> None:
    content = normalize_msvc_includes(source.read_text(encoding="utf-8-sig"))
    old = """        const auto module = GetModuleHandleW(L\"dcomp.dll\");
        const auto DCompositionCreateSurfaceHandle = GetProcAddressByFunctionDeclaration(module, DCompositionCreateSurfaceHandle);"""
    new = """        // The unpackaged OpenTerminal XAML presenter can use its CPU
        // backend without loading DirectComposition. Load the system DLL here
        // before resolving the optional surface-handle API instead of assuming
        // another component happened to load it first.
        wil::unique_hmodule module{ LoadLibraryW(L\"dcomp.dll\") };
        THROW_LAST_ERROR_IF(!module);
        const auto DCompositionCreateSurfaceHandle = GetProcAddressByFunctionDeclaration(module.get(), DCompositionCreateSurfaceHandle);"""
    if content.count(old) != 1:
        raise RuntimeError("expected DirectComposition surface-handle lookup")
    write_if_changed(destination, content.replace(old, new))


def prepare_scroll_bar_header(source: Path, destination: Path) -> None:
    content = normalize_msvc_includes(source.read_text(encoding="utf-8-sig"))
    old = '#include "ScrollBarVisualStateManager.g.h"'
    new = '#include "ScrollBarVisualStateManager.g.h"\n#include "TermControl.h"'
    if content.count(old) != 1:
        raise RuntimeError("expected ScrollBarVisualStateManager projection include")
    write_if_changed(destination, content.replace(old, new))


def prepare_term_control_automation(source: Path, destination: Path) -> None:
    content = normalize_msvc_includes(source.read_text(encoding="utf-8-sig"))
    old = '#include "TermControlAutomationPeer.h"\n#include "TermControl.h"'
    new = '#include "TermControl.h"\n#include "TermControlAutomationPeer.h"'
    if content.count(old) != 1:
        raise RuntimeError("expected Terminal automation include order")
    write_if_changed(destination, content.replace(old, new))


def prepare_settings_model(source: Path, destination: Path) -> None:
    specialization = (
        "struct ::Microsoft::Terminal::Settings::Model::JsonUtils::ConversionTrait"
    )
    replacement = (
        "struct Microsoft::Terminal::Settings::Model::JsonUtils::ConversionTrait"
    )
    uuid_declarations = """

#ifdef __CRT_UUID_DECL
__CRT_UUID_DECL(IMediaResourceContainer,
                0x6068ee1b, 0x1ea0, 0x4804,
                0x99, 0x3a, 0x42, 0xef, 0x0c, 0x58, 0xd8, 0x67)
__CRT_UUID_DECL(IPathlessMediaResourceContainer,
                0x9f11361c, 0x7c8f, 0x45c9,
                0x89, 0x48, 0x36, 0xb6, 0x6d, 0x67, 0xec, 0xa8)
#endif
"""

    for pattern in ("*.cpp", "*.h", "*.hpp"):
        for path in sorted(source.glob(pattern)):
            content = normalize_msvc_includes(path.read_text(encoding="utf-8-sig"))
            content = content.replace(
                "../../types/inc/Utils.hpp", "../../types/inc/utils.hpp"
            )

            if path.name == "TerminalWarnings.h":
                old = "const char* what() const override"
                if content.count(old) != 1:
                    raise RuntimeError("expected one SettingsException::what declaration")
                content = content.replace(old, "const char* what() const noexcept override")

            if path.name in {
                "JsonUtils.h",
                "TerminalSettingsSerializationHelpers.h",
                "Theme.cpp",
            }:
                if specialization not in content:
                    raise RuntimeError(
                        f"expected qualified JSON specializations in {path.name}"
                    )
                content = content.replace(specialization, replacement)

            if path.name == "Theme.cpp":
                old = "type{##__VA_ARGS__ }"
                if content.count(old) != 1:
                    raise RuntimeError("expected one MSVC variadic token-paste expression")
                content = content.replace(old, "type{ __VA_ARGS__ }")

            if path.name == "MediaResourceSupport.h":
                if "__CRT_UUID_DECL(IMediaResourceContainer" in content:
                    raise RuntimeError(
                        "unexpected pre-existing MinGW media UUID declarations"
                    )
                content += uuid_declarations

            if path.name == "VsSetupConfiguration.h":
                old = '#include "Setup.Configuration.h"'
                if content.count(old) != 1:
                    raise RuntimeError("expected Visual Studio Setup SDK include")
                content = content.replace(
                    old,
                    old + '\n#include "visual_studio_setup_uuid_compat.h"',
                )

            if path.name == "FileUtils.cpp":
                # Wine does not implement the package-redirection-only known
                # folder flags and returns E_INVALIDARG for them. This build is
                # explicitly unpackaged, so both variants have the same
                # correct desktop meaning as KF_FLAG_DEFAULT.
                for flag in (
                    "KF_FLAG_FORCE_APP_DATA_REDIRECTION",
                    "KF_FLAG_NO_PACKAGE_REDIRECTION",
                ):
                    call_argument = f", {flag},"
                    if content.count(call_argument) != 1:
                        raise RuntimeError(
                            f"expected one unpackaged known-folder flag: {flag}"
                        )
                    content = content.replace(call_argument, ", KF_FLAG_DEFAULT,")

            if path.name == "ActionArgs.h":
                # This name appears inside the implementation namespace, where
                # GCC resolves it to implementation::NewTerminalArgs. A failed
                # try_as then forms a com_ptr to the implementation by applying
                # its interface offset to a null ABI pointer. Query the public
                # projected runtime class instead, which is what this type test
                # is intended to express and which preserves null on failure.
                old = "_ContentArgs.try_as<NewTerminalArgs>()"
                new = "_ContentArgs.try_as<Model::NewTerminalArgs>()"
                if content.count(old) != 1:
                    raise RuntimeError(
                        "expected one SplitPane NewTerminalArgs projected cast"
                    )
                content = content.replace(old, new)

            if path.name == "CascadiaSettingsSerialization.cpp":
                extension_start = "    // Search through app extensions.\n"
                extension_end = (
                    "    }\n"
                    "}\n\n"
                    "// See FindFragmentsAndMergeIntoUserSettings.\n"
                )
                if content.count(extension_start) != 1 or content.count(extension_end) != 1:
                    raise RuntimeError("expected the app-extension fragment-discovery block")
                content = content.replace(
                    extension_start,
                    "#ifndef __MINGW32__\n" + extension_start,
                )
                content = content.replace(
                    extension_end,
                    "    }\n#endif\n}\n\n"
                    "// See FindFragmentsAndMergeIntoUserSettings.\n",
                )

            if path.name == "CascadiaSettings.cpp":
                # A coroutine lambda retains a pointer to its closure, not a
                # copy of that closure. MSVC extends the lifetime of the
                # immediately-invoked temporary used upstream; GCC follows
                # the standard lifetime and resumes through a dangling `this`
                # after resume_background(). Give the closure a named lifetime
                # through latch.wait() without changing Terminal behavior.
                old = """    std::ignore = [&]() -> safe_void_coroutine {
        const auto cleanup = wil::scope_exit([&]() {
            latch.count_down();
        });
        co_await winrt::resume_background();
        result = DefaultTerminal::Available();
    }();"""
                new = """    auto refresh = [&]() -> safe_void_coroutine {
        const auto cleanup = wil::scope_exit([&]() {
            latch.count_down();
        });
        co_await winrt::resume_background();
        result = DefaultTerminal::Available();
    };
    std::ignore = refresh();"""
                if content.count(old) != 1:
                    raise RuntimeError(
                        "expected one default-terminal coroutine lambda"
                    )
                content = content.replace(old, new)

            write_if_changed(destination / path.name, content)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--terminal", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    # Remove the obsolete Wine ICU harvest produced by earlier phase-2 builds;
    # the checked-in aggregator now consumes the pinned open ICU package.
    (args.output / "include" / "icu.h").unlink(missing_ok=True)
    (args.output / "include" / "cppwinrt_utils.h").unlink(missing_ok=True)
    normalized_copy(
        args.terminal / "dep" / "Console" / "condrv.h",
        args.output / "dep" / "Console" / "condrv.h",
    )
    normalized_copy(
        args.terminal / "dep" / "NT" / "ntioapi_x.h",
        args.output / "dep" / "NT" / "ntioapi_x.h",
    )
    prepare_winrt_utils(
        args.terminal / "src" / "cascadia" / "WinRTUtils" / "inc" / "Utils.h",
        args.output / "include" / "Utils.h",
    )
    prepare_cppwinrt_utils(
        args.terminal / "src" / "cascadia" / "inc" / "cppwinrt_utils.h",
        args.output
        / "cascadia"
        / "TerminalSettingsEditor"
        / "cppwinrt_utils_mingw.h",
    )
    prepare_cppwinrt_utils(
        args.terminal / "src" / "cascadia" / "inc" / "cppwinrt_utils.h",
        args.output / "cascadia" / "TerminalApp" / "cppwinrt_utils_mingw.h",
    )
    prepare_windows_terminal(
        args.terminal / "src" / "cascadia" / "WindowsTerminal",
        args.output / "cascadia" / "WindowsTerminal",
    )
    prepare_atlas_engine(
        args.terminal / "src" / "renderer" / "atlas" / "AtlasEngine.r.cpp",
        args.output / "renderer" / "atlas" / "AtlasEngine.r.cpp",
    )
    prepare_icon_path_converter(
        args.terminal / "src" / "cascadia" / "UIHelpers" / "IconPathConverter.cpp",
        args.output / "cascadia" / "UIHelpers" / "IconPathConverter.cpp",
    )
    terminal_control = args.terminal / "src" / "cascadia" / "TerminalControl"
    prepared_terminal_control = args.output / "cascadia" / "TerminalControl"
    prepare_search_box_source(
        terminal_control / "SearchBoxControl.cpp",
        prepared_terminal_control / "SearchBoxControl.cpp",
    )
    prepare_xaml_uia_text_range(
        terminal_control / "XamlUiaTextRange.h",
        prepared_terminal_control / "XamlUiaTextRange.h",
    )
    normalized_copy(
        terminal_control / "XamlUiaTextRange.cpp",
        prepared_terminal_control / "XamlUiaTextRange.cpp",
    )
    normalized_copy(
        terminal_control / "InteractivityAutomationPeer.cpp",
        prepared_terminal_control / "InteractivityAutomationPeer.cpp",
    )
    prepare_term_control(
        terminal_control / "TermControl.h",
        prepared_terminal_control / "TermControl.h",
    )
    prepare_term_control_source(
        terminal_control / "TermControl.cpp",
        prepared_terminal_control / "TermControl.cpp",
    )
    normalized_copy(
        terminal_control / "XamlLights.cpp",
        prepared_terminal_control / "XamlLights.cpp",
    )
    prepare_scroll_bar_header(
        terminal_control / "ScrollBarVisualStateManager.h",
        prepared_terminal_control / "ScrollBarVisualStateManager.h",
    )
    normalized_copy(
        terminal_control / "ScrollBarVisualStateManager.cpp",
        prepared_terminal_control / "ScrollBarVisualStateManager.cpp",
    )
    prepare_term_control_automation(
        terminal_control / "TermControlAutomationPeer.cpp",
        prepared_terminal_control / "TermControlAutomationPeer.cpp",
    )
    prepare_settings_model(
        args.terminal / "src" / "cascadia" / "TerminalSettingsModel",
        args.output / "cascadia" / "TerminalSettingsModel",
    )
    prepare_uimarkdown_code_block(
        args.terminal / "src" / "cascadia" / "UIMarkdown" / "CodeBlock.cpp",
        args.output / "cascadia" / "UIMarkdown" / "CodeBlock.cpp",
    )
    prepare_settings_editor(
        args.terminal / "src" / "cascadia" / "TerminalSettingsEditor",
        args.output / "cascadia" / "TerminalSettingsEditor",
    )
    prepare_terminal_app(
        args.terminal / "src" / "cascadia" / "TerminalApp",
        args.output / "cascadia" / "TerminalApp",
    )


if __name__ == "__main__":
    main()
