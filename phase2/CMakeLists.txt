cmake_minimum_required(VERSION 3.25)

project(OpenTerminalMinGW LANGUAGES CXX RC)

if(NOT WIN32 OR NOT MINGW)
    message(FATAL_ERROR "Phase 2 currently requires a mingw-w64 Windows target")
endif()

set(TERMINAL_SOURCE_DIR "" CACHE PATH "Pinned microsoft/terminal checkout")
set(WIL_SOURCE_DIR "" CACHE PATH "Pinned microsoft/wil checkout")
set(CPPWINRT_SDK_INCLUDE_DIR "" CACHE PATH "Generated C++/WinRT Windows SDK projection root")
set(CPPWINRT_TERMINALCORE_INCLUDE_DIR "" CACHE PATH "Generated C++/WinRT TerminalCore projection root")
set(CPPWINRT_TERMINALCONNECTION_INCLUDE_DIR "" CACHE PATH "Generated C++/WinRT TerminalConnection projection root")
set(CPPWINRT_WINUI_INCLUDE_DIR "" CACHE PATH "Generated C++/WinRT WinUI 2 and WebView2 projection root")
set(CPPWINRT_UIHELPERS_INCLUDE_DIR "" CACHE PATH "Generated C++/WinRT Microsoft.Terminal.UI projection root")
set(CPPWINRT_TERMINALCONTROL_INCLUDE_DIR "" CACHE PATH "Generated C++/WinRT Microsoft.Terminal.Control projection root")
set(CPPWINRT_SETTINGSMODEL_INCLUDE_DIR "" CACHE PATH "Generated C++/WinRT Microsoft.Terminal.Settings.Model projection root")
set(CPPWINRT_UIMARKDOWN_INCLUDE_DIR "" CACHE PATH "Generated C++/WinRT Microsoft.Terminal.UI.Markdown projection root")
set(CPPWINRT_SETTINGSEDITOR_INCLUDE_DIR "" CACHE PATH "Generated C++/WinRT Microsoft.Terminal.Settings.Editor projection root")
set(CPPWINRT_TERMINALAPP_INCLUDE_DIR "" CACHE PATH "Generated C++/WinRT TerminalApp projection root")
set(CPPWINRT_WINGET_INCLUDE_DIR "" CACHE PATH "Generated C++/WinRT WinGet projection root")
set(VISUALSTUDIO_SETUP_INCLUDE_DIR "" CACHE PATH "Extracted Visual Studio Setup Configuration header directory")
set(OPENTERMINAL_XAML_GENERATED_DIR "" CACHE PATH "Optional WinUI XAML compiler output root")
set(OPENTERMINAL_XAMLCORE_IMPORT_LIBRARY "" CACHE FILEPATH
    "Import library for Microsoft.UI.Xaml.dll's OpenXaml compatibility exports")
set(WINDOWS_SDK_BIN_DIR "" CACHE PATH "Extracted pinned Windows SDK x64 tool directory")
set(WINDOWS_SDK_WINRT_INCLUDE_DIR "" CACHE PATH "Extracted pinned Windows SDK WinRT headers")
option(OPENTERMINAL_ALLOW_UNPINNED_SOURCES "Allow source commits other than phase2/upstreams.json" OFF)

foreach(required_path IN ITEMS TERMINAL_SOURCE_DIR WIL_SOURCE_DIR)
    if(NOT IS_DIRECTORY "${${required_path}}/.git")
        message(FATAL_ERROR "${required_path} must name a Git checkout")
    endif()
endforeach()

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/upstreams.json" upstreams_json)
string(JSON expected_terminal_commit GET "${upstreams_json}" windows_terminal commit)
string(JSON expected_wil_commit GET "${upstreams_json}" wil commit)

function(require_pinned_checkout label source_dir expected_commit)
    execute_process(
        COMMAND git -C "${source_dir}" rev-parse HEAD
        OUTPUT_VARIABLE actual_commit
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE git_result
    )
    if(NOT git_result EQUAL 0)
        message(FATAL_ERROR "Could not resolve ${label} commit")
    endif()
    if(NOT actual_commit STREQUAL expected_commit)
        if(OPENTERMINAL_ALLOW_UNPINNED_SOURCES)
            message(WARNING "${label} is ${actual_commit}; the reviewed pin is ${expected_commit}")
        else()
            message(FATAL_ERROR
                "${label} is ${actual_commit}; expected ${expected_commit}. "
                "Use -DOPENTERMINAL_ALLOW_UNPINNED_SOURCES=ON only for exploratory work."
            )
        endif()
    endif()
endfunction()

require_pinned_checkout("Windows Terminal" "${TERMINAL_SOURCE_DIR}" "${expected_terminal_commit}")
require_pinned_checkout("WIL" "${WIL_SOURCE_DIR}" "${expected_wil_commit}")

if(NOT EXISTS "${CPPWINRT_SDK_INCLUDE_DIR}/winrt/Windows.Foundation.h")
    message(FATAL_ERROR "CPPWINRT_SDK_INCLUDE_DIR must contain winrt/Windows.Foundation.h")
endif()
if(NOT EXISTS "${CPPWINRT_TERMINALCORE_INCLUDE_DIR}/winrt/Microsoft.Terminal.Core.h")
    message(FATAL_ERROR "CPPWINRT_TERMINALCORE_INCLUDE_DIR must contain winrt/Microsoft.Terminal.Core.h")
endif()
if(NOT EXISTS "${CPPWINRT_TERMINALCONNECTION_INCLUDE_DIR}/winrt/Microsoft.Terminal.TerminalConnection.h")
    message(FATAL_ERROR "CPPWINRT_TERMINALCONNECTION_INCLUDE_DIR must contain the TerminalConnection projection")
endif()
if(NOT EXISTS "${CPPWINRT_TERMINALCONNECTION_INCLUDE_DIR}/Microsoft/Terminal/TerminalConnection/EchoConnection.g.cpp")
    message(FATAL_ERROR "CPPWINRT_TERMINALCONNECTION_INCLUDE_DIR must contain optimized component glue")
endif()
if(NOT EXISTS "${CPPWINRT_WINUI_INCLUDE_DIR}/winrt/Microsoft.UI.Xaml.Controls.h")
    message(FATAL_ERROR "CPPWINRT_WINUI_INCLUDE_DIR must contain the WinUI 2 projection")
endif()
if(NOT EXISTS "${CPPWINRT_WINUI_INCLUDE_DIR}/winrt/Microsoft.Web.WebView2.Core.h")
    message(FATAL_ERROR "CPPWINRT_WINUI_INCLUDE_DIR must contain the WebView2 projection")
endif()
if(NOT EXISTS "${CPPWINRT_UIHELPERS_INCLUDE_DIR}/winrt/Microsoft.Terminal.UI.h")
    message(FATAL_ERROR "CPPWINRT_UIHELPERS_INCLUDE_DIR must contain the Microsoft.Terminal.UI projection")
endif()
if(NOT EXISTS "${CPPWINRT_UIHELPERS_INCLUDE_DIR}/Microsoft/Terminal/UI/Converters.g.cpp")
    message(FATAL_ERROR "CPPWINRT_UIHELPERS_INCLUDE_DIR must contain optimized UIHelpers component glue")
endif()
if(NOT EXISTS "${CPPWINRT_TERMINALCONTROL_INCLUDE_DIR}/winrt/Microsoft.Terminal.Control.h")
    message(FATAL_ERROR "CPPWINRT_TERMINALCONTROL_INCLUDE_DIR must contain the TerminalControl projection")
endif()
if(NOT EXISTS "${CPPWINRT_TERMINALCONTROL_INCLUDE_DIR}/Microsoft/Terminal/Control/ControlCore.g.cpp")
    message(FATAL_ERROR "CPPWINRT_TERMINALCONTROL_INCLUDE_DIR must contain optimized TerminalControl component glue")
endif()
if(NOT EXISTS "${CPPWINRT_SETTINGSMODEL_INCLUDE_DIR}/winrt/Microsoft.Terminal.Settings.Model.h")
    message(FATAL_ERROR "CPPWINRT_SETTINGSMODEL_INCLUDE_DIR must contain the SettingsModel projection")
endif()
if(NOT EXISTS "${CPPWINRT_SETTINGSMODEL_INCLUDE_DIR}/Microsoft/Terminal/Settings/Model/ActionAndArgs.g.cpp")
    message(FATAL_ERROR "CPPWINRT_SETTINGSMODEL_INCLUDE_DIR must contain optimized SettingsModel component glue")
endif()
if(NOT EXISTS "${CPPWINRT_UIMARKDOWN_INCLUDE_DIR}/winrt/Microsoft.Terminal.UI.Markdown.h")
    message(FATAL_ERROR "CPPWINRT_UIMARKDOWN_INCLUDE_DIR must contain the UIMarkdown projection")
endif()
if(NOT EXISTS "${CPPWINRT_UIMARKDOWN_INCLUDE_DIR}/Microsoft/Terminal/UI/Markdown/Builder.g.cpp")
    message(FATAL_ERROR "CPPWINRT_UIMARKDOWN_INCLUDE_DIR must contain optimized UIMarkdown component glue")
endif()
if(NOT EXISTS "${CPPWINRT_SETTINGSEDITOR_INCLUDE_DIR}/winrt/Microsoft.Terminal.Settings.Editor.h")
    message(FATAL_ERROR "CPPWINRT_SETTINGSEDITOR_INCLUDE_DIR must contain the SettingsEditor projection")
endif()
if(NOT EXISTS "${CPPWINRT_SETTINGSEDITOR_INCLUDE_DIR}/Microsoft/Terminal/Settings/Editor/Actions.g.cpp")
    message(FATAL_ERROR "CPPWINRT_SETTINGSEDITOR_INCLUDE_DIR must contain optimized SettingsEditor component glue")
endif()
if(NOT EXISTS "${CPPWINRT_TERMINALAPP_INCLUDE_DIR}/winrt/TerminalApp.h")
    message(FATAL_ERROR "CPPWINRT_TERMINALAPP_INCLUDE_DIR must contain the TerminalApp projection")
endif()
if(NOT EXISTS "${CPPWINRT_TERMINALAPP_INCLUDE_DIR}/TerminalApp/App.g.cpp")
    message(FATAL_ERROR "CPPWINRT_TERMINALAPP_INCLUDE_DIR must contain optimized TerminalApp component glue")
endif()
if(NOT EXISTS "${CPPWINRT_WINGET_INCLUDE_DIR}/winrt/Microsoft.Management.Deployment.h")
    message(FATAL_ERROR "CPPWINRT_WINGET_INCLUDE_DIR must contain the WinGet projection")
endif()
if(NOT EXISTS "${VISUALSTUDIO_SETUP_INCLUDE_DIR}/Setup.Configuration.h")
    message(FATAL_ERROR "VISUALSTUDIO_SETUP_INCLUDE_DIR must contain Setup.Configuration.h")
endif()
if(NOT EXISTS "${WINDOWS_SDK_BIN_DIR}/fxc.exe" OR NOT EXISTS "${WINDOWS_SDK_BIN_DIR}/d3dcompiler_47.dll")
    message(FATAL_ERROR "WINDOWS_SDK_BIN_DIR must contain fxc.exe and d3dcompiler_47.dll")
endif()
if(NOT EXISTS "${WINDOWS_SDK_WINRT_INCLUDE_DIR}/CoreWindow.h")
    message(FATAL_ERROR "WINDOWS_SDK_WINRT_INCLUDE_DIR must contain CoreWindow.h")
endif()

find_package(fmt CONFIG REQUIRED)
find_package(ICU REQUIRED COMPONENTS data i18n uc)
find_package(jsoncpp CONFIG REQUIRED)
find_package(cmark CONFIG REQUIRED)
find_package(CLI11 CONFIG REQUIRED)
find_package(Microsoft.GSL CONFIG REQUIRED)
find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
find_program(CLANGXX_EXECUTABLE NAMES clang++ REQUIRED)
find_program(MINGW_OBJCOPY_EXECUTABLE NAMES x86_64-w64-mingw32-objcopy REQUIRED)
find_program(WINE_EXECUTABLE NAMES wine REQUIRED)
find_program(WIDL_EXECUTABLE NAMES widl REQUIRED)

set(OPENTERMINAL_BRANDING "Dev" CACHE STRING "Terminal feature branding")
set_property(CACHE OPENTERMINAL_BRANDING PROPERTY STRINGS Dev Canary Preview Release WindowsInbox)
if(NOT OPENTERMINAL_BRANDING MATCHES "^(Dev|Canary|Preview|Release|WindowsInbox)$")
    message(FATAL_ERROR "Unsupported Terminal feature branding: ${OPENTERMINAL_BRANDING}")
endif()

set(feature_staging_header "${CMAKE_CURRENT_BINARY_DIR}/generated/TilFeatureStaging.h")
add_custom_command(
    OUTPUT "${feature_staging_header}"
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_feature_staging.py"
        "${TERMINAL_SOURCE_DIR}/src/features.xml"
        "${feature_staging_header}"
        --branding "${OPENTERMINAL_BRANDING}"
        --input-label src/features.xml
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_feature_staging.py"
        "${TERMINAL_SOURCE_DIR}/src/features.xml"
    COMMENT "Generating Terminal feature flags without PowerShell/MSBuild"
    VERBATIM
)

set(prepared_header_root "${CMAKE_CURRENT_BINARY_DIR}/generated/mingw")
set(prepared_library_resources_header
    "${prepared_header_root}/cascadia/WinRTUtils/inc/LibraryResources.h")
get_filename_component(prepared_library_resources_dir
    "${prepared_library_resources_header}" DIRECTORY)
file(MAKE_DIRECTORY "${prepared_library_resources_dir}")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/compat/include/LibraryResources.h"
    "${prepared_library_resources_header}"
    COPYONLY)
set(prepared_condrv_header "${prepared_header_root}/dep/Console/condrv.h")
set(prepared_ntioapi_header "${prepared_header_root}/dep/NT/ntioapi_x.h")
set(prepared_winrt_utils_header "${prepared_header_root}/include/Utils.h")
set(prepared_terminal_control_dir
    "${prepared_header_root}/cascadia/TerminalControl")
set(prepared_xaml_uia_header
    "${prepared_terminal_control_dir}/XamlUiaTextRange.h")
set(prepared_xaml_uia_source
    "${prepared_terminal_control_dir}/XamlUiaTextRange.cpp")
set(prepared_interactivity_automation_source
    "${prepared_terminal_control_dir}/InteractivityAutomationPeer.cpp")
set(prepared_search_box_source
    "${prepared_terminal_control_dir}/SearchBoxControl.cpp")
set(prepared_term_control_header
    "${prepared_terminal_control_dir}/TermControl.h")
set(prepared_term_control_source
    "${prepared_terminal_control_dir}/TermControl.cpp")
set(prepared_xaml_lights_source
    "${prepared_terminal_control_dir}/XamlLights.cpp")
set(prepared_scroll_bar_header
    "${prepared_terminal_control_dir}/ScrollBarVisualStateManager.h")
set(prepared_scroll_bar_source
    "${prepared_terminal_control_dir}/ScrollBarVisualStateManager.cpp")
set(prepared_term_control_automation_source
    "${prepared_terminal_control_dir}/TermControlAutomationPeer.cpp")
set(prepared_uimarkdown_code_block_source
    "${prepared_header_root}/cascadia/UIMarkdown/CodeBlock.cpp")
set(prepared_icon_path_converter_source
    "${prepared_header_root}/cascadia/UIHelpers/IconPathConverter.cpp")
set(prepared_settings_editor_dir
    "${prepared_header_root}/cascadia/TerminalSettingsEditor")
set(prepared_cppwinrt_utils_header
    "${prepared_settings_editor_dir}/cppwinrt_utils_mingw.h")
set(settings_editor_source_names
    init.cpp
    Actions.cpp
    ArgsTemplateSelectors.cpp
    EditAction.cpp
    AddProfile.cpp
    GlobalAppearance.cpp
    ColorSchemes.cpp
    NullableColorPicker.cpp
    IconPicker.cpp
    EditColorScheme.cpp
    Interaction.cpp
    Compatibility.cpp
    KeyChordListener.cpp
    Launch.cpp
    NewTabMenu.cpp
    pch.cpp
    SearchIndex.cpp
    MainPage.cpp
    ProfileViewModel.cpp
    ActionsViewModel.cpp
    TerminalColorConverters.cpp
    ColorSchemeViewModel.cpp
    ColorSchemesPageViewModel.cpp
    RenderingViewModel.cpp
    InteractionViewModel.cpp
    GlobalAppearanceViewModel.cpp
    LaunchViewModel.cpp
    NewTabMenuViewModel.cpp
    Extensions.cpp
    Profiles_Base.cpp
    Profiles_Base_Orphaned.cpp
    Profiles_Advanced.cpp
    Profiles_Appearance.cpp
    Profiles_Terminal.cpp
    Appearances.cpp
    Rendering.cpp
    SettingContainer.cpp
    Utils.cpp
    PreviewConnection.cpp)
set(prepared_settings_editor_sources)
foreach(source_name IN LISTS settings_editor_source_names)
    list(APPEND prepared_settings_editor_sources
        "${prepared_settings_editor_dir}/${source_name}")
endforeach()
set(prepared_terminal_app_dir
    "${prepared_header_root}/cascadia/TerminalApp")
set(prepared_terminal_app_cppwinrt_utils_header
    "${prepared_terminal_app_dir}/cppwinrt_utils_mingw.h")
set(terminal_app_source_names
    init.cpp
    AppCommandlineArgs.cpp
    Commandline.cpp
    CommandPaletteItems.cpp
    Jumplist.cpp
    LanguageProfileNotifier.cpp
    MinMaxCloseControl.cpp
    Remoting.cpp
    PaletteItemTemplateSelector.cpp
    Tab.cpp
    TaskbarState.cpp
    TerminalPage.cpp
    TabManagement.cpp
    ActionPreviewHandlers.cpp
    TerminalTabStatus.cpp
    TitlebarControl.cpp
    TabRowControl.cpp
    TabHeaderControl.cpp
    HighlightedTextControl.cpp
    ColorPickupFlyout.cpp
    CommandPalette.cpp
    FilteredCommand.cpp
    Pane.cpp
    Pane.LayoutSizeNode.cpp
    DebugTapConnection.cpp
    pch.cpp
    AppKeyBindings.cpp
    ShortcutActionDispatch.cpp
    AboutDialog.cpp
    App.cpp
    AppActionHandlers.cpp
    AppLogic.cpp
    ContentManager.cpp
    TerminalWindow.cpp
    TerminalPaneContent.cpp
    ScratchpadContent.cpp
    SnippetsPaneContent.cpp
    SettingsPaneContent.cpp
    Toast.cpp
    DesktopNotification.cpp
    TerminalSettingsCache.cpp
    SuggestionsControl.cpp
    MarkdownPaneContent.cpp)
set(prepared_terminal_app_sources)
foreach(source_name IN LISTS terminal_app_source_names)
    list(APPEND prepared_terminal_app_sources
        "${prepared_terminal_app_dir}/${source_name}")
endforeach()
set(prepared_windows_terminal_dir
    "${prepared_header_root}/cascadia/WindowsTerminal")
set(prepared_windows_terminal_resource
    "${prepared_windows_terminal_dir}/WindowsTerminal.rc")
set(windows_terminal_source_names
    pch.cpp
    main.cpp
    AppHost.cpp
    IslandWindow.cpp
    NonClientIslandWindow.cpp
    VirtualDesktopUtils.cpp
    WindowEmperor.cpp
    icon.cpp)
set(prepared_windows_terminal_sources)
foreach(source_name IN LISTS windows_terminal_source_names)
    list(APPEND prepared_windows_terminal_sources
        "${prepared_windows_terminal_dir}/${source_name}")
endforeach()
set(settings_model_source_names
    VisualStudioGenerator.cpp
    DefaultTerminal.cpp
    init.cpp
    pch.cpp
    ActionAndArgs.cpp
    ActionArgs.cpp
    ActionMap.cpp
    ActionMapSerialization.cpp
    AzureCloudShellGenerator.cpp
    ApplicationState.cpp
    CascadiaSettings.cpp
    CascadiaSettingsSerialization.cpp
    ColorScheme.cpp
    Theme.cpp
    Command.cpp
    DynamicProfileUtils.cpp
    FileUtils.cpp
    GlobalAppSettings.cpp
    KeyChordSerialization.cpp
    PowershellCoreProfileGenerator.cpp
    Profile.cpp
    AppearanceConfig.cpp
    FontConfig.cpp
    EnumMappings.cpp
    NewTabMenuEntry.cpp
    SeparatorEntry.cpp
    ActionEntry.cpp
    FolderEntry.cpp
    ProfileEntry.cpp
    ProfileCollectionEntry.cpp
    RemainingProfilesEntry.cpp
    MatchProfilesEntry.cpp
    MediaResourceSupport.cpp
    VsDevCmdGenerator.cpp
    VsDevShellGenerator.cpp
    VsSetupConfiguration.cpp
    WslDistroGenerator.cpp
    SshHostGenerator.cpp
    TestHooks.cpp
)
set(prepared_settings_model_dir
    "${prepared_header_root}/cascadia/TerminalSettingsModel")
set(prepared_settings_model_sources)
foreach(source_name IN LISTS settings_model_source_names)
    list(APPEND prepared_settings_model_sources
        "${prepared_settings_model_dir}/${source_name}")
endforeach()
file(GLOB settings_model_prepare_inputs CONFIGURE_DEPENDS
    "${TERMINAL_SOURCE_DIR}/src/cascadia/TerminalSettingsModel/*.cpp"
    "${TERMINAL_SOURCE_DIR}/src/cascadia/TerminalSettingsModel/*.h"
    "${TERMINAL_SOURCE_DIR}/src/cascadia/TerminalSettingsModel/*.hpp")
file(GLOB settings_editor_prepare_inputs CONFIGURE_DEPENDS
    "${TERMINAL_SOURCE_DIR}/src/cascadia/TerminalSettingsEditor/*.cpp"
    "${TERMINAL_SOURCE_DIR}/src/cascadia/TerminalSettingsEditor/*.h"
    "${TERMINAL_SOURCE_DIR}/src/cascadia/TerminalSettingsEditor/*.hpp")
file(GLOB terminal_app_prepare_inputs CONFIGURE_DEPENDS
    "${TERMINAL_SOURCE_DIR}/src/cascadia/TerminalApp/*.cpp"
    "${TERMINAL_SOURCE_DIR}/src/cascadia/TerminalApp/*.h"
    "${TERMINAL_SOURCE_DIR}/src/cascadia/TerminalApp/*.hpp")
file(GLOB windows_terminal_prepare_inputs CONFIGURE_DEPENDS
    "${TERMINAL_SOURCE_DIR}/src/cascadia/WindowsTerminal/*.cpp"
    "${TERMINAL_SOURCE_DIR}/src/cascadia/WindowsTerminal/*.h"
    "${TERMINAL_SOURCE_DIR}/src/cascadia/WindowsTerminal/*.hpp")
set(prepared_settings_editor_files)
foreach(input IN LISTS settings_editor_prepare_inputs)
    get_filename_component(input_name "${input}" NAME)
    list(APPEND prepared_settings_editor_files
        "${prepared_settings_editor_dir}/${input_name}")
endforeach()
set(prepared_terminal_app_files)
foreach(input IN LISTS terminal_app_prepare_inputs)
    get_filename_component(input_name "${input}" NAME)
    list(APPEND prepared_terminal_app_files
        "${prepared_terminal_app_dir}/${input_name}")
endforeach()
set(prepared_windows_terminal_files)
foreach(input IN LISTS windows_terminal_prepare_inputs)
    get_filename_component(input_name "${input}" NAME)
    list(APPEND prepared_windows_terminal_files
        "${prepared_windows_terminal_dir}/${input_name}")
endforeach()
add_custom_command(
    OUTPUT
        "${prepared_condrv_header}"
        "${prepared_ntioapi_header}"
        "${prepared_winrt_utils_header}"
        "${prepared_cppwinrt_utils_header}"
        "${prepared_xaml_uia_header}"
        "${prepared_xaml_uia_source}"
        "${prepared_interactivity_automation_source}"
        "${prepared_search_box_source}"
        "${prepared_term_control_header}"
        "${prepared_term_control_source}"
        "${prepared_xaml_lights_source}"
        "${prepared_scroll_bar_header}"
        "${prepared_scroll_bar_source}"
        "${prepared_term_control_automation_source}"
        "${prepared_uimarkdown_code_block_source}"
        "${prepared_icon_path_converter_source}"
        ${prepared_settings_model_sources}
        "${prepared_settings_model_dir}/JsonUtils.h"
        "${prepared_settings_model_dir}/MediaResourceSupport.h"
        "${prepared_settings_model_dir}/TerminalSettingsSerializationHelpers.h"
        "${prepared_settings_model_dir}/TerminalWarnings.h"
        ${prepared_settings_editor_files}
        ${prepared_terminal_app_files}
        "${prepared_terminal_app_cppwinrt_utils_header}"
        ${prepared_windows_terminal_files}
        "${prepared_windows_terminal_resource}"
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/prepare_mingw_headers.py"
        --terminal "${TERMINAL_SOURCE_DIR}"
        --output "${prepared_header_root}"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/prepare_mingw_headers.py"
        "${TERMINAL_SOURCE_DIR}/dep/Console/condrv.h"
        "${TERMINAL_SOURCE_DIR}/dep/NT/ntioapi_x.h"
        "${TERMINAL_SOURCE_DIR}/src/cascadia/WinRTUtils/inc/Utils.h"
        "${TERMINAL_SOURCE_DIR}/src/cascadia/inc/cppwinrt_utils.h"
        "${TERMINAL_SOURCE_DIR}/src/cascadia/TerminalControl/XamlUiaTextRange.h"
        "${TERMINAL_SOURCE_DIR}/src/cascadia/TerminalControl/XamlUiaTextRange.cpp"
        "${TERMINAL_SOURCE_DIR}/src/cascadia/TerminalControl/InteractivityAutomationPeer.cpp"
        "${TERMINAL_SOURCE_DIR}/src/cascadia/TerminalControl/SearchBoxControl.cpp"
        "${TERMINAL_SOURCE_DIR}/src/cascadia/TerminalControl/TermControl.h"
        "${TERMINAL_SOURCE_DIR}/src/cascadia/TerminalControl/TermControl.cpp"
        "${TERMINAL_SOURCE_DIR}/src/cascadia/TerminalControl/XamlLights.cpp"
        "${TERMINAL_SOURCE_DIR}/src/cascadia/TerminalControl/ScrollBarVisualStateManager.h"
        "${TERMINAL_SOURCE_DIR}/src/cascadia/TerminalControl/ScrollBarVisualStateManager.cpp"
        "${TERMINAL_SOURCE_DIR}/src/cascadia/TerminalControl/TermControlAutomationPeer.cpp"
        "${TERMINAL_SOURCE_DIR}/src/cascadia/UIMarkdown/CodeBlock.cpp"
        "${TERMINAL_SOURCE_DIR}/src/cascadia/UIHelpers/IconPathConverter.cpp"
        ${settings_model_prepare_inputs}
        ${settings_editor_prepare_inputs}
        ${terminal_app_prepare_inputs}
        ${windows_terminal_prepare_inputs}
        "${TERMINAL_SOURCE_DIR}/src/cascadia/WindowsTerminal/WindowsTerminal.rc"
    COMMENT "Preparing case-safe Terminal headers"
    VERBATIM
)
add_custom_target(openterminal_prepared_headers DEPENDS
    "${prepared_library_resources_header}"
    "${prepared_condrv_header}"
    "${prepared_ntioapi_header}"
    "${prepared_winrt_utils_header}"
    "${prepared_cppwinrt_utils_header}"
    "${prepared_xaml_uia_header}"
    "${prepared_xaml_uia_source}"
    "${prepared_interactivity_automation_source}"
    "${prepared_search_box_source}"
    "${prepared_term_control_header}"
    "${prepared_term_control_source}"
    "${prepared_xaml_lights_source}"
    "${prepared_scroll_bar_header}"
    "${prepared_scroll_bar_source}"
    "${prepared_term_control_automation_source}"
    "${prepared_uimarkdown_code_block_source}"
    ${prepared_settings_model_sources}
    "${prepared_settings_model_dir}/JsonUtils.h"
    "${prepared_settings_model_dir}/MediaResourceSupport.h"
    "${prepared_settings_model_dir}/TerminalSettingsSerializationHelpers.h"
    "${prepared_settings_model_dir}/TerminalWarnings.h"
    ${prepared_settings_editor_files}
    ${prepared_terminal_app_files}
    "${prepared_terminal_app_cppwinrt_utils_header}"
    ${prepared_windows_terminal_files}
    "${prepared_windows_terminal_resource}"
)

set(mingw_compat_header "${CMAKE_CURRENT_SOURCE_DIR}/compat/include/openterminal/mingw_compat.h")
function(configure_terminal_target target)
    add_dependencies(${target} openterminal_prepared_headers)
    target_compile_features(${target} PUBLIC cxx_std_20)
    target_compile_definitions(${target} PRIVATE
        EXTERNAL_BUILD
        UNICODE
        _UNICODE
    )
    target_include_directories(${target} PRIVATE
        "${prepared_header_root}/cascadia/WinRTUtils/inc"
        "${prepared_header_root}/include"
        "${prepared_header_root}/dep/Console"
        "${CMAKE_CURRENT_SOURCE_DIR}/compat/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/compat/relative/terminal/adapter"
        "${TERMINAL_SOURCE_DIR}/src/inc"
        "${TERMINAL_SOURCE_DIR}/dep"
        "${TERMINAL_SOURCE_DIR}/dep/Console"
        "${TERMINAL_SOURCE_DIR}/dep/Win32K"
        "${TERMINAL_SOURCE_DIR}/oss/chromium"
        "${TERMINAL_SOURCE_DIR}/oss/interval_tree"
        "${TERMINAL_SOURCE_DIR}/oss/pcg/include"
    )
    target_include_directories(${target} SYSTEM PRIVATE
        "${WIL_SOURCE_DIR}/include"
    )
    target_link_libraries(${target} PRIVATE
        fmt::fmt
        Microsoft.GSL::GSL
    )
    target_compile_options(${target} PRIVATE
        -fms-extensions
        "-include${mingw_compat_header}"
        "-include${feature_staging_header}"
        -Wno-attributes
        -Wno-deprecated
        -Wno-subobject-linkage
        -Wno-unknown-pragmas
    )
endfunction()

function(configure_terminal_resource_scope target scope)
    target_compile_definitions(${target} PRIVATE
        "OPENTERMINAL_LIBRARY_RESOURCE_SCOPE=L\"${scope}\"")
endfunction()

set(parser_dir "${TERMINAL_SOURCE_DIR}/src/terminal/parser")
add_library(openterminal_terminal_parser STATIC
    "${feature_staging_header}"
    "${parser_dir}/InputStateMachineEngine.cpp"
    "${parser_dir}/OutputStateMachineEngine.cpp"
    "${parser_dir}/base64.cpp"
    "${parser_dir}/precomp.cpp"
    "${parser_dir}/stateMachine.cpp"
    "${parser_dir}/tracing.cpp"
)
set_target_properties(openterminal_terminal_parser PROPERTIES OUTPUT_NAME ConTermParser)
configure_terminal_target(openterminal_terminal_parser)

set(terminal_input_dir "${TERMINAL_SOURCE_DIR}/src/terminal/input")
add_library(openterminal_terminal_input STATIC
    "${feature_staging_header}"
    "${terminal_input_dir}/mouseInput.cpp"
    "${terminal_input_dir}/terminalInput.cpp"
    "${terminal_input_dir}/TestHook.cpp"
    "${terminal_input_dir}/precomp.cpp"
)
set_target_properties(openterminal_terminal_input PROPERTIES OUTPUT_NAME TerminalInput)
configure_terminal_target(openterminal_terminal_input)

set(types_dir "${TERMINAL_SOURCE_DIR}/src/types")
add_library(openterminal_types STATIC
    "${feature_staging_header}"
    "${types_dir}/CodepointWidthDetector.cpp"
    "${types_dir}/ColorFix.cpp"
    "${types_dir}/convert.cpp"
    "${types_dir}/colorTable.cpp"
    "${types_dir}/GlyphWidth.cpp"
    "${types_dir}/ScreenInfoUiaProviderBase.cpp"
    "${types_dir}/sgrStack.cpp"
    "${types_dir}/ThemeUtils.cpp"
    "${types_dir}/UiaTextRangeBase.cpp"
    "${types_dir}/UiaTracing.cpp"
    "${types_dir}/TermControlUiaTextRange.cpp"
    "${types_dir}/TermControlUiaProvider.cpp"
    "${types_dir}/viewport.cpp"
    "${types_dir}/precomp.cpp"
    "${types_dir}/utils.cpp"
)
set_target_properties(openterminal_types PROPERTIES OUTPUT_NAME ConTypes)
configure_terminal_target(openterminal_types)
target_include_directories(openterminal_types PRIVATE "${types_dir}")
target_link_libraries(openterminal_types PRIVATE ICU::data ICU::i18n ICU::uc)

set(buffer_dir "${TERMINAL_SOURCE_DIR}/src/buffer/out")
add_library(openterminal_buffer_out STATIC
    "${feature_staging_header}"
    "${CMAKE_CURRENT_SOURCE_DIR}/compat/src/isa_availability.cpp"
    "${buffer_dir}/cursor.cpp"
    "${buffer_dir}/ImageSlice.cpp"
    "${buffer_dir}/OutputCell.cpp"
    "${buffer_dir}/OutputCellIterator.cpp"
    "${buffer_dir}/OutputCellRect.cpp"
    "${buffer_dir}/OutputCellView.cpp"
    "${buffer_dir}/Row.cpp"
    "${buffer_dir}/search.cpp"
    "${buffer_dir}/TextColor.cpp"
    "${buffer_dir}/TextAttribute.cpp"
    "${buffer_dir}/textBuffer.cpp"
    "${buffer_dir}/textBufferCellIterator.cpp"
    "${buffer_dir}/textBufferTextIterator.cpp"
    "${buffer_dir}/precomp.cpp"
    "${buffer_dir}/UTextAdapter.cpp"
)
set_target_properties(openterminal_buffer_out PROPERTIES OUTPUT_NAME ConBufferOut)
configure_terminal_target(openterminal_buffer_out)
target_include_directories(openterminal_buffer_out PRIVATE "${buffer_dir}" "${types_dir}")
target_link_libraries(openterminal_buffer_out PRIVATE
    openterminal_types
    ICU::data
    ICU::i18n
    ICU::uc
)

set(renderer_base_dir "${TERMINAL_SOURCE_DIR}/src/renderer/base")
set(renderer_include_dir "${TERMINAL_SOURCE_DIR}/src/renderer/inc")
add_library(openterminal_renderer_base STATIC
    "${feature_staging_header}"
    "${renderer_base_dir}/CSSLengthPercentage.cpp"
    "${renderer_base_dir}/fontinfo.cpp"
    "${renderer_base_dir}/FontInfoBase.cpp"
    "${renderer_base_dir}/FontInfoDesired.cpp"
    "${renderer_base_dir}/FontResource.cpp"
    "${renderer_base_dir}/RenderEngineBase.cpp"
    "${renderer_base_dir}/RenderSettings.cpp"
    "${renderer_base_dir}/renderer.cpp"
    "${renderer_base_dir}/precomp.cpp"
)
set_target_properties(openterminal_renderer_base PROPERTIES OUTPUT_NAME ConRenderBase)
configure_terminal_target(openterminal_renderer_base)
target_include_directories(openterminal_renderer_base PRIVATE
    "${renderer_base_dir}"
    "${renderer_include_dir}"
    "${buffer_dir}"
    "${types_dir}"
)
target_link_libraries(openterminal_renderer_base PRIVATE
    openterminal_buffer_out
    openterminal_types
    ICU::data
    ICU::i18n
    ICU::uc
    gdi32
)

set(renderer_gdi_dir "${TERMINAL_SOURCE_DIR}/src/renderer/gdi")
add_library(openterminal_renderer_gdi STATIC
    "${feature_staging_header}"
    "${renderer_gdi_dir}/invalidate.cpp"
    "${renderer_gdi_dir}/math.cpp"
    "${renderer_gdi_dir}/paint.cpp"
    "${renderer_gdi_dir}/state.cpp"
    "${renderer_gdi_dir}/precomp.cpp"
)
set_target_properties(openterminal_renderer_gdi PROPERTIES OUTPUT_NAME ConRenderGdi)
configure_terminal_target(openterminal_renderer_gdi)
target_include_directories(openterminal_renderer_gdi PRIVATE
    "${TERMINAL_SOURCE_DIR}/src/renderer"
    "${renderer_gdi_dir}"
    "${renderer_base_dir}"
    "${renderer_include_dir}"
    "${buffer_dir}"
    "${types_dir}"
)
target_link_libraries(openterminal_renderer_gdi PRIVATE
    openterminal_renderer_base
    openterminal_buffer_out
    openterminal_types
    ICU::data
    ICU::i18n
    ICU::uc
    gdi32
    user32
    usp10
)

set(renderer_atlas_dir "${TERMINAL_SOURCE_DIR}/src/renderer/atlas")
set(renderer_atlas_shader_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/RendererAtlas")
set(renderer_atlas_shader_names
    custom_shader_ps
    custom_shader_vs
    shader_ps
    shader_vs
)
set(renderer_atlas_shader_profiles
    ps_4_0
    vs_4_0
    ps_4_0
    vs_4_0
)
set(renderer_atlas_hlsl_names
    ${renderer_atlas_shader_names}
    dwrite_helpers
    shader_common
)

# fxc emits an internal `#hlsl_full_path` directive for included files and
# mis-tokenizes it when an absolute directory component contains a digit under
# Wine. Stage the six exact inputs at a short, content-keyed, alphabetic path.
# configure_file also makes CMake regenerate this staging area when an input
# changes. Hex digits are mapped to letters g-p solely for path compatibility.
set(renderer_atlas_hlsl_fingerprint_input "")
foreach(hlsl_name IN LISTS renderer_atlas_hlsl_names)
    file(SHA256 "${renderer_atlas_dir}/${hlsl_name}.hlsl" hlsl_sha256)
    string(APPEND renderer_atlas_hlsl_fingerprint_input "${hlsl_sha256}")
endforeach()
string(SHA256 renderer_atlas_hlsl_fingerprint "${renderer_atlas_hlsl_fingerprint_input}")
string(SUBSTRING "${renderer_atlas_hlsl_fingerprint}" 0 16 renderer_atlas_hlsl_fingerprint)
set(hex_digits 0 1 2 3 4 5 6 7 8 9)
set(hex_letters g h i j k l m n o p)
foreach(hex_digit hex_letter IN ZIP_LISTS hex_digits hex_letters)
    string(REPLACE "${hex_digit}" "${hex_letter}"
        renderer_atlas_hlsl_fingerprint
        "${renderer_atlas_hlsl_fingerprint}")
endforeach()
set(renderer_atlas_hlsl_stage_dir "/tmp/openterminal-hlsl-${renderer_atlas_hlsl_fingerprint}")
file(MAKE_DIRECTORY "${renderer_atlas_hlsl_stage_dir}")
foreach(hlsl_name IN LISTS renderer_atlas_hlsl_names)
    configure_file(
        "${renderer_atlas_dir}/${hlsl_name}.hlsl"
        "${renderer_atlas_hlsl_stage_dir}/${hlsl_name}.hlsl"
        COPYONLY
    )
endforeach()

set(renderer_atlas_shader_headers)
# Concurrent fxc processes under one Wine prefix can corrupt each other's
# include-preprocessor temporaries. Serialize this small harvested-tool stage
# while leaving all native compilation in Ninja's normal parallel pool.
set_property(GLOBAL PROPERTY JOB_POOLS openterminal_fxc_pool=1)
foreach(shader_name profile IN ZIP_LISTS renderer_atlas_shader_names renderer_atlas_shader_profiles)
    set(shader_header "${renderer_atlas_shader_dir}/${shader_name}.h")
    set(shader_binary "${renderer_atlas_shader_dir}/${shader_name}.dxbc")
    add_custom_command(
        OUTPUT "${shader_header}"
        BYPRODUCTS "${shader_binary}"
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/compile_hlsl.py"
            --wine "${WINE_EXECUTABLE}"
            --fxc "${WINDOWS_SDK_BIN_DIR}/fxc.exe"
            --source "${renderer_atlas_hlsl_stage_dir}/${shader_name}.hlsl"
            --output "${shader_header}"
            --profile "${profile}"
            --variable "${shader_name}"
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/compile_hlsl.py"
            "${renderer_atlas_dir}/${shader_name}.hlsl"
            "${renderer_atlas_dir}/dwrite_helpers.hlsl"
            "${renderer_atlas_dir}/shader_common.hlsl"
            "${WINDOWS_SDK_BIN_DIR}/fxc.exe"
            "${WINDOWS_SDK_BIN_DIR}/d3dcompiler_47.dll"
        COMMENT "Compiling ${shader_name}.hlsl to a DXBC header"
        JOB_POOL openterminal_fxc_pool
        VERBATIM
    )
    list(APPEND renderer_atlas_shader_headers "${shader_header}")
endforeach()

add_library(openterminal_renderer_atlas STATIC
    "${feature_staging_header}"
    ${renderer_atlas_shader_headers}
    "${renderer_atlas_dir}/AtlasEngine.cpp"
    "${renderer_atlas_dir}/AtlasEngine.api.cpp"
    "${renderer_atlas_dir}/AtlasEngine.r.cpp"
    "${renderer_atlas_dir}/Backend.cpp"
    "${renderer_atlas_dir}/BackendD2D.cpp"
    "${renderer_atlas_dir}/BackendD3D.cpp"
    "${renderer_atlas_dir}/BuiltinGlyphs.cpp"
    "${renderer_atlas_dir}/dwrite_helpers.cpp"
    "${renderer_atlas_dir}/DWriteTextAnalysis.cpp"
    "${renderer_atlas_dir}/pch.cpp"
    "${renderer_atlas_dir}/stb_rect_pack.cpp"
    "${renderer_atlas_dir}/wic.cpp"
)
set_target_properties(openterminal_renderer_atlas PROPERTIES OUTPUT_NAME ConRenderAtlas)
configure_terminal_target(openterminal_renderer_atlas)
target_compile_options(openterminal_renderer_atlas PRIVATE
    "-include${CMAKE_CURRENT_SOURCE_DIR}/compat/include/openterminal/atlas_uuid_compat.h")
target_include_directories(openterminal_renderer_atlas PRIVATE
    "${renderer_atlas_shader_dir}"
    "${renderer_atlas_dir}"
    "${TERMINAL_SOURCE_DIR}/src/renderer"
    "${renderer_base_dir}"
    "${renderer_include_dir}"
    "${buffer_dir}"
    "${types_dir}"
    "${TERMINAL_SOURCE_DIR}/oss/stb"
)
target_link_libraries(openterminal_renderer_atlas PRIVATE
    openterminal_renderer_base
    openterminal_buffer_out
    openterminal_types
    ICU::data
    ICU::i18n
    ICU::uc
    d2d1
    d3d11
    d3dcompiler
    dcomp
    dwrite
    dxgi
    dxguid
    shlwapi
    windowscodecs
)

set(renderer_uia_dir "${TERMINAL_SOURCE_DIR}/src/renderer/uia")
add_library(openterminal_renderer_uia STATIC
    "${feature_staging_header}"
    "${renderer_uia_dir}/UiaRenderer.cpp"
    "${renderer_uia_dir}/precomp.cpp"
)
set_target_properties(openterminal_renderer_uia PROPERTIES OUTPUT_NAME ConRenderUia)
configure_terminal_target(openterminal_renderer_uia)
target_include_directories(openterminal_renderer_uia PRIVATE
    "${renderer_uia_dir}"
    "${renderer_base_dir}"
    "${renderer_include_dir}"
    "${buffer_dir}"
    "${types_dir}"
)
target_link_libraries(openterminal_renderer_uia PRIVATE
    openterminal_renderer_base
    openterminal_buffer_out
    openterminal_types
)

# GCC cannot select between VTParameter's templated enum conversion and its
# integer conversion in two switch expressions in adaptDispatchGraphics.cpp.
# Clang accepts the upstream source and emits the same mingw-w64 GNU/COFF ABI,
# so compile that one object with Clang and keep the other eight units on GCC.
set(adapter_dir "${TERMINAL_SOURCE_DIR}/src/terminal/adapter")
get_target_property(fmt_include_directories fmt::fmt INTERFACE_INCLUDE_DIRECTORIES)
get_target_property(gsl_include_directories Microsoft.GSL::GSL INTERFACE_INCLUDE_DIRECTORIES)
set(adapter_clang_include_directories
    "${prepared_header_root}/include"
    "${prepared_header_root}/dep/Console"
    "${CMAKE_CURRENT_SOURCE_DIR}/compat/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/compat/relative/terminal/adapter"
    "${CMAKE_CURRENT_SOURCE_DIR}/compat/host"
    "${CMAKE_CURRENT_SOURCE_DIR}/compat/renderer/base"
    "${CMAKE_CURRENT_SOURCE_DIR}/compat/terminal/parser"
    "${CMAKE_CURRENT_SOURCE_DIR}/compat/types"
    "${TERMINAL_SOURCE_DIR}/src/inc"
    "${TERMINAL_SOURCE_DIR}/src"
    "${TERMINAL_SOURCE_DIR}/src/terminal"
    "${TERMINAL_SOURCE_DIR}/src/types"
    "${TERMINAL_SOURCE_DIR}/src/types/inc"
    "${TERMINAL_SOURCE_DIR}/src/buffer/out"
    "${TERMINAL_SOURCE_DIR}/src/host"
    "${TERMINAL_SOURCE_DIR}/src/renderer/base"
    "${adapter_dir}"
    "${parser_dir}"
    "${TERMINAL_SOURCE_DIR}/dep"
    "${TERMINAL_SOURCE_DIR}/dep/Console"
    "${TERMINAL_SOURCE_DIR}/dep/Win32K"
    "${TERMINAL_SOURCE_DIR}/oss/chromium"
    "${TERMINAL_SOURCE_DIR}/oss/interval_tree"
    "${TERMINAL_SOURCE_DIR}/oss/pcg/include"
    "${WIL_SOURCE_DIR}/include"
    ${ICU_INCLUDE_DIRS}
    ${fmt_include_directories}
    ${gsl_include_directories}
)
list(REMOVE_DUPLICATES adapter_clang_include_directories)
set(adapter_clang_include_options)
foreach(include_directory IN LISTS adapter_clang_include_directories)
    list(APPEND adapter_clang_include_options "-I${include_directory}")
endforeach()

set(adapter_graphics_raw_object "${CMAKE_CURRENT_BINARY_DIR}/clang/adaptDispatchGraphics.cpp.raw.obj")
set(adapter_graphics_object "${CMAKE_CURRENT_BINARY_DIR}/clang/adaptDispatchGraphics.cpp.obj")
set(adapter_graphics_depfile "${adapter_graphics_object}.d")
add_custom_command(
    OUTPUT "${adapter_graphics_object}"
    BYPRODUCTS "${adapter_graphics_raw_object}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/clang"
    COMMAND "${CLANGXX_EXECUTABLE}"
        --target=x86_64-w64-windows-gnu
        -DEXTERNAL_BUILD
        -DUNICODE
        -D_UNICODE
        -O2
        -DNDEBUG
        -std=gnu++20
        -fms-extensions
        -fno-zero-initialized-in-bss
        -Wno-attributes
        -Wno-deprecated
        -Wno-microsoft-sealed
        -Wno-unknown-pragmas
        "-include${mingw_compat_header}"
        "-include${feature_staging_header}"
        ${adapter_clang_include_options}
        -MD
        -MT "${adapter_graphics_object}"
        -MF "${adapter_graphics_depfile}"
        -c "${adapter_dir}/adaptDispatchGraphics.cpp"
        -o "${adapter_graphics_raw_object}"
    COMMAND "${MINGW_OBJCOPY_EXECUTABLE}"
        --remove-section=.drectve
        "${adapter_graphics_raw_object}"
        "${adapter_graphics_object}"
    DEPENDS
        "${adapter_dir}/adaptDispatchGraphics.cpp"
        "${feature_staging_header}"
        "${prepared_condrv_header}"
        "${prepared_ntioapi_header}"
        "${mingw_compat_header}"
    DEPFILE "${adapter_graphics_depfile}"
    COMMENT "Compiling Terminal adapter graphics with Clang's mingw-w64 target"
    COMMAND_EXPAND_LISTS
    VERBATIM
)
set_source_files_properties("${adapter_graphics_object}" PROPERTIES
    EXTERNAL_OBJECT TRUE
    GENERATED TRUE
)

add_library(openterminal_terminal_adapter STATIC
    "${feature_staging_header}"
    "${adapter_dir}/adaptDispatch.cpp"
    "${adapter_dir}/FontBuffer.cpp"
    "${adapter_dir}/InteractDispatch.cpp"
    "${adapter_dir}/MacroBuffer.cpp"
    "${adapter_dir}/PageManager.cpp"
    "${adapter_dir}/SixelParser.cpp"
    "${adapter_graphics_object}"
    "${adapter_dir}/terminalOutput.cpp"
    "${adapter_dir}/precomp.cpp"
)
set_target_properties(openterminal_terminal_adapter PROPERTIES OUTPUT_NAME ConTermAdapt)
configure_terminal_target(openterminal_terminal_adapter)
target_include_directories(openterminal_terminal_adapter PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/compat/host"
    "${CMAKE_CURRENT_SOURCE_DIR}/compat/renderer/base"
    "${CMAKE_CURRENT_SOURCE_DIR}/compat/terminal/parser"
    "${CMAKE_CURRENT_SOURCE_DIR}/compat/types"
    "${TERMINAL_SOURCE_DIR}/src"
    "${TERMINAL_SOURCE_DIR}/src/terminal"
    "${TERMINAL_SOURCE_DIR}/src/types"
    "${TERMINAL_SOURCE_DIR}/src/types/inc"
    "${buffer_dir}"
    "${TERMINAL_SOURCE_DIR}/src/host"
    "${TERMINAL_SOURCE_DIR}/src/renderer/base"
    "${adapter_dir}"
    "${parser_dir}"
)
target_link_libraries(openterminal_terminal_adapter PRIVATE
    openterminal_terminal_parser
    openterminal_terminal_input
    openterminal_types
    openterminal_buffer_out
    openterminal_renderer_base
    ICU::data
    ICU::i18n
    ICU::uc
    shlwapi
)

set(terminal_core_dir "${TERMINAL_SOURCE_DIR}/src/cascadia/TerminalCore")
add_library(openterminal_terminal_core STATIC
    "${feature_staging_header}"
    "${terminal_core_dir}/terminalrenderdata.cpp"
    "${terminal_core_dir}/TerminalSelection.cpp"
    "${terminal_core_dir}/TerminalApi.cpp"
    "${terminal_core_dir}/Terminal.cpp"
    "${terminal_core_dir}/pch.cpp"
)
set_target_properties(openterminal_terminal_core PROPERTIES OUTPUT_NAME TerminalCore)
configure_terminal_target(openterminal_terminal_core)
target_compile_definitions(openterminal_terminal_core PRIVATE WINRT_LEAN_AND_MEAN)
target_include_directories(openterminal_terminal_core PRIVATE
    "${CPPWINRT_TERMINALCORE_INCLUDE_DIR}"
    "${CPPWINRT_SDK_INCLUDE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/compat/cascadia/TerminalCore"
    "${TERMINAL_SOURCE_DIR}/src"
    "${terminal_core_dir}"
    "${buffer_dir}"
    "${types_dir}"
    "${renderer_base_dir}"
    "${renderer_include_dir}"
    "${adapter_dir}"
    "${parser_dir}"
)
target_link_libraries(openterminal_terminal_core PRIVATE
    openterminal_terminal_adapter
    openterminal_terminal_parser
    openterminal_terminal_input
    openterminal_buffer_out
    openterminal_types
    openterminal_renderer_base
    ICU::data
    ICU::i18n
    ICU::uc
    runtimeobject
    windowsapp
)

set(winrt_utils_dir "${TERMINAL_SOURCE_DIR}/src/cascadia/WinRTUtils")
add_library(openterminal_winrt_utils STATIC
    "${feature_staging_header}"
    "${CMAKE_CURRENT_SOURCE_DIR}/compat/src/library_resources.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/compat/src/scoped_resource_loader.cpp"
    "${winrt_utils_dir}/Utils.cpp"
    "${winrt_utils_dir}/pch.cpp"
)
set_target_properties(openterminal_winrt_utils PROPERTIES OUTPUT_NAME WinRTUtils)
configure_terminal_target(openterminal_winrt_utils)
target_compile_definitions(openterminal_winrt_utils PRIVATE WINRT_LEAN_AND_MEAN)
target_include_directories(openterminal_winrt_utils PRIVATE
    "${CPPWINRT_SDK_INCLUDE_DIR}"
    "${TERMINAL_SOURCE_DIR}/src/cascadia/inc"
    "${winrt_utils_dir}"
    "${winrt_utils_dir}/inc"
    "${types_dir}"
)
target_link_libraries(openterminal_winrt_utils PRIVATE
    openterminal_types
    runtimeobject
    windowsapp
    ole32
    shell32
)

set(terminal_connection_dir "${TERMINAL_SOURCE_DIR}/src/cascadia/TerminalConnection")
set(terminal_connection_generated_dir
    "${CPPWINRT_TERMINALCONNECTION_INCLUDE_DIR}/Microsoft/Terminal/TerminalConnection")
set(midi_dir "${TERMINAL_SOURCE_DIR}/src/audio/midi")

set(ui_helpers_dir "${TERMINAL_SOURCE_DIR}/src/cascadia/UIHelpers")
set(ui_helpers_generated_dir
    "${CPPWINRT_UIHELPERS_INCLUDE_DIR}/Microsoft/Terminal/UI")
add_library(openterminal_ui_helpers STATIC
    "${feature_staging_header}"
    "${ui_helpers_dir}/init.cpp"
    "${ui_helpers_dir}/pch.cpp"
    "${ui_helpers_dir}/Converters.cpp"
    "${prepared_icon_path_converter_source}"
    "${ui_helpers_dir}/ResourceString.cpp"
    "${ui_helpers_dir}/TextMenuFlyout.cpp"
    "${CPPWINRT_UIHELPERS_INCLUDE_DIR}/module.g.cpp"
)
set_target_properties(openterminal_ui_helpers PROPERTIES
    OUTPUT_NAME Microsoft.Terminal.UI)
configure_terminal_target(openterminal_ui_helpers)
configure_terminal_resource_scope(openterminal_ui_helpers
    "Microsoft.Terminal.UI/Resources")
target_compile_definitions(openterminal_ui_helpers PRIVATE WINRT_LEAN_AND_MEAN)
target_compile_options(openterminal_ui_helpers PRIVATE -mcx16)
target_include_directories(openterminal_ui_helpers PRIVATE
    "${CPPWINRT_UIHELPERS_INCLUDE_DIR}"
    "${ui_helpers_generated_dir}"
    "${CPPWINRT_WINUI_INCLUDE_DIR}"
    "${CPPWINRT_SDK_INCLUDE_DIR}"
    "${TERMINAL_SOURCE_DIR}/src/cascadia"
    "${TERMINAL_SOURCE_DIR}/src/cascadia/inc"
    "${winrt_utils_dir}/inc"
    "${ui_helpers_dir}"
    "${types_dir}"
)
target_link_libraries(openterminal_ui_helpers PRIVATE
    openterminal_winrt_utils
    openterminal_types
    runtimeobject
    windowsapp
    ole32
    shell32
    shlwapi
    user32
    windowscodecs
)

set(tsf_dir "${TERMINAL_SOURCE_DIR}/src/tsf")
add_library(openterminal_tsf STATIC
    "${feature_staging_header}"
    compat/src/tsf_guids.cpp
    "${tsf_dir}/Handle.cpp"
    "${tsf_dir}/Implementation.cpp"
    "${tsf_dir}/precomp.cpp"
)
set_target_properties(openterminal_tsf PROPERTIES OUTPUT_NAME ConTSF)
configure_terminal_target(openterminal_tsf)
target_include_directories(openterminal_tsf PRIVATE
    "${TERMINAL_SOURCE_DIR}/src"
    "${tsf_dir}"
    "${buffer_dir}"
    "${types_dir}"
    "${renderer_base_dir}"
    "${renderer_include_dir}"
)
target_link_libraries(openterminal_tsf PRIVATE
    openterminal_renderer_base
    openterminal_buffer_out
    openterminal_types
    ole32
)

# These 13 TerminalControl units implement the model, control core, accessibility,
# and HWND hosting layers. When genuine WinUI XAML compiler output is supplied,
# the two XAML-backed units are added as well.
set(terminal_control_dir "${TERMINAL_SOURCE_DIR}/src/cascadia/TerminalControl")
set(terminal_control_generated_dir
    "${CPPWINRT_TERMINALCONTROL_INCLUDE_DIR}/Microsoft/Terminal/Control")
add_library(openterminal_terminal_control_model STATIC
    "${feature_staging_header}"
    "${terminal_control_dir}/ControlCore.cpp"
    "${terminal_control_dir}/ControlInteractivity.cpp"
    "${terminal_control_dir}/EventArgs.cpp"
    "${terminal_control_dir}/HwndTerminal.cpp"
    "${terminal_control_dir}/HwndTerminalAutomationPeer.cpp"
    "${prepared_interactivity_automation_source}"
    "${terminal_control_dir}/KeyChord.cpp"
    "${prepared_scroll_bar_header}"
    "${prepared_scroll_bar_source}"
    "${prepared_term_control_automation_source}"
    "${prepared_xaml_uia_header}"
    "${prepared_xaml_uia_source}"
    "${prepared_term_control_header}"
    "${prepared_xaml_lights_source}"
    "${terminal_control_dir}/init.cpp"
    "${terminal_control_dir}/pch.cpp"
)
if(OPENTERMINAL_XAML_GENERATED_DIR)
    foreach(xaml_output IN ITEMS
        SearchBoxControl.xaml.g.h
        SearchBoxControl.xaml.g.hpp
        TermControl.xaml.g.h
        TermControl.xaml.g.hpp
        XamlBindingInfo.xaml.g.h
        XamlBindingInfo.xaml.g.hpp
        XamlMetaDataProvider.h
        XamlTypeInfo.xaml.g.h
        XamlTypeInfo.Impl.g.cpp
        XamlTypeInfo.g.cpp)
        if(NOT EXISTS "${OPENTERMINAL_XAML_GENERATED_DIR}/Microsoft/Terminal/Control/${xaml_output}")
            message(FATAL_ERROR
                "OPENTERMINAL_XAML_GENERATED_DIR is missing Microsoft/Terminal/Control/${xaml_output}")
        endif()
    endforeach()
    target_sources(openterminal_terminal_control_model PRIVATE
        "${prepared_search_box_source}"
        "${prepared_term_control_source}"
        "${OPENTERMINAL_XAML_GENERATED_DIR}/Microsoft/Terminal/Control/XamlTypeInfo.Impl.g.cpp"
        "${OPENTERMINAL_XAML_GENERATED_DIR}/Microsoft/Terminal/Control/XamlTypeInfo.g.cpp"
        compat/src/terminal_control_xaml_metadata_provider.cpp
    )
    target_include_directories(openterminal_terminal_control_model PRIVATE
        "${OPENTERMINAL_XAML_GENERATED_DIR}"
        "${OPENTERMINAL_XAML_GENERATED_DIR}/Microsoft/Terminal/Control")
endif()
set_target_properties(openterminal_terminal_control_model PROPERTIES
    OUTPUT_NAME Microsoft.Terminal.Control.Model)
configure_terminal_target(openterminal_terminal_control_model)
configure_terminal_resource_scope(openterminal_terminal_control_model
    "Microsoft.Terminal.Control/Resources")
target_compile_definitions(openterminal_terminal_control_model PRIVATE
    WINRT_LEAN_AND_MEAN)
target_compile_options(openterminal_terminal_control_model PRIVATE -mcx16)
target_include_directories(openterminal_terminal_control_model PRIVATE
    "${CPPWINRT_TERMINALCONTROL_INCLUDE_DIR}"
    "${terminal_control_generated_dir}"
    "${prepared_terminal_control_dir}"
    "${terminal_control_dir}"
    "${CPPWINRT_UIHELPERS_INCLUDE_DIR}"
    "${ui_helpers_generated_dir}"
    "${CPPWINRT_WINUI_INCLUDE_DIR}"
    "${CPPWINRT_TERMINALCONNECTION_INCLUDE_DIR}"
    "${terminal_connection_generated_dir}"
    "${CPPWINRT_TERMINALCORE_INCLUDE_DIR}"
    "${CPPWINRT_SDK_INCLUDE_DIR}"
    "${TERMINAL_SOURCE_DIR}/src"
    "${TERMINAL_SOURCE_DIR}/src/cascadia"
    "${TERMINAL_SOURCE_DIR}/src/cascadia/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/compat/cascadia/TerminalCore"
    "${winrt_utils_dir}/inc"
    "${ui_helpers_dir}"
    "${terminal_core_dir}"
    "${buffer_dir}"
    "${types_dir}"
    "${renderer_base_dir}"
    "${renderer_include_dir}"
    "${renderer_atlas_dir}"
    "${renderer_uia_dir}"
    "${TERMINAL_SOURCE_DIR}/src/audio"
    "${midi_dir}"
    "${adapter_dir}"
    "${parser_dir}"
)
target_link_libraries(openterminal_terminal_control_model PRIVATE
    openterminal_terminal_core
    openterminal_terminal_connection
    openterminal_ui_helpers
    openterminal_renderer_atlas
    openterminal_renderer_uia
    openterminal_midi_audio
    openterminal_tsf
    delayimp
    runtimeobject
    uiautomationcore
    windowsapp
    user32
)

add_library(openterminal_midi_audio STATIC
    "${feature_staging_header}"
    "${midi_dir}/MidiAudio.cpp"
    "${midi_dir}/precomp.cpp"
)
set_target_properties(openterminal_midi_audio PROPERTIES OUTPUT_NAME MidiAudio)
configure_terminal_target(openterminal_midi_audio)
target_include_directories(openterminal_midi_audio PRIVATE
    "${TERMINAL_SOURCE_DIR}/src/audio"
    "${midi_dir}"
    "${parser_dir}"
)
target_link_libraries(openterminal_midi_audio PRIVATE
    openterminal_terminal_parser
    dsound
    synchronization
    winmm
)

set(terminal_handoff_header
    "${CMAKE_CURRENT_BINARY_DIR}/generated/TerminalConnection/ITerminalHandoff.h")
add_custom_command(
    OUTPUT "${terminal_handoff_header}"
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_widl_header.py"
        --widl "${WIDL_EXECUTABLE}"
        --source "${TERMINAL_SOURCE_DIR}/src/host/proxy/ITerminalHandoff.idl"
        --output "${terminal_handoff_header}"
        --input-label src/host/proxy/ITerminalHandoff.idl
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_widl_header.py"
        "${TERMINAL_SOURCE_DIR}/src/host/proxy/ITerminalHandoff.idl"
    COMMENT "Generating the Terminal handoff COM header with Wine widl"
    VERBATIM
)
set(winconpty_dir "${TERMINAL_SOURCE_DIR}/src/winconpty")
set(server_dir "${TERMINAL_SOURCE_DIR}/src/server")
add_library(openterminal_winconpty STATIC
    "${feature_staging_header}"
    "${winconpty_dir}/winconpty.cpp"
    "${winconpty_dir}/precomp.cpp"
    "${server_dir}/DeviceHandle.cpp"
    "${server_dir}/WinNTControl.cpp"
)
set_target_properties(openterminal_winconpty PROPERTIES OUTPUT_NAME conptylib)
configure_terminal_target(openterminal_winconpty)
target_compile_definitions(openterminal_winconpty PRIVATE CONPTY_IMPEXP=)
target_include_directories(openterminal_winconpty PRIVATE
    "${TERMINAL_SOURCE_DIR}/src"
    "${TERMINAL_SOURCE_DIR}/src/host"
    "${server_dir}"
    "${winconpty_dir}"
)
target_link_libraries(openterminal_winconpty PRIVATE ntdll)

add_library(openterminal_terminal_connection STATIC
    "${feature_staging_header}"
    "${terminal_handoff_header}"
    "${terminal_connection_dir}/CTerminalHandoff.cpp"
    "${terminal_connection_dir}/init.cpp"
    "${terminal_connection_dir}/ConnectionInformation.cpp"
    "${terminal_connection_dir}/AzureConnection.cpp"
    "${terminal_connection_dir}/pch.cpp"
    "${terminal_connection_dir}/EchoConnection.cpp"
    "${terminal_connection_dir}/ConptyConnection.cpp"
    "${CPPWINRT_TERMINALCONNECTION_INCLUDE_DIR}/module.g.cpp"
)
set_target_properties(openterminal_terminal_connection PROPERTIES
    OUTPUT_NAME TerminalConnection)
configure_terminal_target(openterminal_terminal_connection)
configure_terminal_resource_scope(openterminal_terminal_connection
    "Microsoft.Terminal.TerminalConnection/Resources")
target_compile_definitions(openterminal_terminal_connection PRIVATE
    CONPTY_IMPEXP=
    WINRT_LEAN_AND_MEAN
)
target_compile_options(openterminal_terminal_connection PRIVATE
    -mcx16
    "-include${CMAKE_CURRENT_SOURCE_DIR}/compat/include/openterminal/winhttp_compat.h"
    "-include${CMAKE_CURRENT_SOURCE_DIR}/compat/include/openterminal/terminal_handoff_compat.h"
)
target_include_directories(openterminal_terminal_connection PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/generated/TerminalConnection"
    "${CPPWINRT_TERMINALCONNECTION_INCLUDE_DIR}"
    "${terminal_connection_generated_dir}"
    "${CPPWINRT_SDK_INCLUDE_DIR}"
    "${TERMINAL_SOURCE_DIR}/src"
    "${TERMINAL_SOURCE_DIR}/src/cascadia/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/compat/cascadia/TerminalConnection"
    "${winrt_utils_dir}/inc"
    "${terminal_connection_dir}"
    "${types_dir}"
    "${winconpty_dir}"
    "${server_dir}"
)
target_link_libraries(openterminal_terminal_connection PRIVATE
    openterminal_winconpty
    openterminal_winrt_utils
    openterminal_types
    runtimeobject
    windowsapp
    winhttp
    ole32
    shell32
    ntdll
)

set(settings_model_dir "${TERMINAL_SOURCE_DIR}/src/cascadia/TerminalSettingsModel")
set(settings_model_generated_dir
    "${CPPWINRT_SETTINGSMODEL_INCLUDE_DIR}/Microsoft/Terminal/Settings/Model")

# TerminalSettingsModel is a DLL in the upstream build, so its three RCDATA
# resources naturally live in that module. This port links the model
# statically; embed the same resources in WindowsTerminal.exe so
# LoadStringResource(IDR_DEFAULTS) observes the real inbox profiles instead of
# constructing a settings graph from dynamic profiles alone.
set(OPENTERMINAL_DEFAULTS_JSON
    "${settings_model_dir}/defaults.json")
set(OPENTERMINAL_USER_DEFAULTS_JSON
    "${settings_model_dir}/userDefaults.json")
set(OPENTERMINAL_ENABLE_COLOR_SELECTION_JSON
    "${settings_model_dir}/enableColorSelection.json")
foreach(settings_resource IN ITEMS
        OPENTERMINAL_DEFAULTS_JSON
        OPENTERMINAL_USER_DEFAULTS_JSON
        OPENTERMINAL_ENABLE_COLOR_SELECTION_JSON)
    if(NOT EXISTS "${${settings_resource}}")
        message(FATAL_ERROR
            "Terminal settings resource is missing: ${${settings_resource}}")
    endif()
endforeach()
set(prepared_settings_model_resource
    "${CMAKE_CURRENT_BINARY_DIR}/generated/mingw/cascadia/TerminalSettingsModel/TerminalSettingsModel.rc")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/compat/resources/TerminalSettingsModel.rc.in"
    "${prepared_settings_model_resource}"
    @ONLY)

add_library(openterminal_settings_model STATIC
    "${feature_staging_header}"
    "${TERMINAL_SOURCE_DIR}/src/internal/stubs.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/compat/src/delegation_config.cpp"
    ${prepared_settings_model_sources}
    "${CPPWINRT_SETTINGSMODEL_INCLUDE_DIR}/module.g.cpp"
)
set_target_properties(openterminal_settings_model PROPERTIES
    OUTPUT_NAME Microsoft.Terminal.Settings.Model)
configure_terminal_target(openterminal_settings_model)
configure_terminal_resource_scope(openterminal_settings_model
    "Microsoft.Terminal.Settings.Model/Resources")
target_compile_definitions(openterminal_settings_model PRIVATE WINRT_LEAN_AND_MEAN)
target_compile_options(openterminal_settings_model PRIVATE -mcx16)
target_include_directories(openterminal_settings_model PRIVATE
    "${CPPWINRT_SETTINGSMODEL_INCLUDE_DIR}"
    "${settings_model_generated_dir}"
    "${VISUALSTUDIO_SETUP_INCLUDE_DIR}"
    "${CPPWINRT_TERMINALCONTROL_INCLUDE_DIR}"
    "${terminal_control_generated_dir}"
    "${CPPWINRT_TERMINALCONNECTION_INCLUDE_DIR}"
    "${terminal_connection_generated_dir}"
    "${CPPWINRT_TERMINALCORE_INCLUDE_DIR}"
    "${CPPWINRT_WINUI_INCLUDE_DIR}"
    "${CPPWINRT_SDK_INCLUDE_DIR}"
    "${TERMINAL_SOURCE_DIR}/src"
    "${TERMINAL_SOURCE_DIR}/src/cascadia"
    "${TERMINAL_SOURCE_DIR}/src/cascadia/inc"
    "${TERMINAL_SOURCE_DIR}/src/cascadia/TerminalSettingsAppAdapterLib"
    "${TERMINAL_SOURCE_DIR}/src/propslib"
    "${winrt_utils_dir}/inc"
    "${prepared_settings_model_dir}"
    "${settings_model_dir}"
    "${terminal_control_dir}"
    "${terminal_core_dir}"
    "${buffer_dir}"
    "${types_dir}"
    "${renderer_base_dir}"
    "${renderer_include_dir}"
    "${adapter_dir}"
    "${parser_dir}"
)
target_link_libraries(openterminal_settings_model PRIVATE
    openterminal_terminal_control_model
    openterminal_terminal_connection
    openterminal_terminal_core
    openterminal_winrt_utils
    openterminal_types
    JsonCpp::JsonCpp
    runtimeobject
    windowsapp
    ole32
    shell32
    shlwapi
    user32
)

set(settings_adapter_dir
    "${TERMINAL_SOURCE_DIR}/src/cascadia/TerminalSettingsAppAdapterLib")
add_library(openterminal_settings_adapter STATIC
    "${feature_staging_header}"
    "${settings_adapter_dir}/pch.cpp"
    "${settings_adapter_dir}/TerminalSettings.cpp"
)
set_target_properties(openterminal_settings_adapter PROPERTIES
    OUTPUT_NAME TerminalSettingsAppAdapterLib)
configure_terminal_target(openterminal_settings_adapter)
target_compile_definitions(openterminal_settings_adapter PRIVATE WINRT_LEAN_AND_MEAN)
target_compile_options(openterminal_settings_adapter PRIVATE -mcx16)
target_include_directories(openterminal_settings_adapter PRIVATE
    "${CPPWINRT_SETTINGSMODEL_INCLUDE_DIR}"
    "${settings_model_generated_dir}"
    "${CPPWINRT_TERMINALCONTROL_INCLUDE_DIR}"
    "${terminal_control_generated_dir}"
    "${CPPWINRT_TERMINALCONNECTION_INCLUDE_DIR}"
    "${terminal_connection_generated_dir}"
    "${CPPWINRT_TERMINALCORE_INCLUDE_DIR}"
    "${CPPWINRT_WINUI_INCLUDE_DIR}"
    "${CPPWINRT_SDK_INCLUDE_DIR}"
    "${TERMINAL_SOURCE_DIR}/src"
    "${TERMINAL_SOURCE_DIR}/src/cascadia"
    "${TERMINAL_SOURCE_DIR}/src/cascadia/inc"
    "${settings_adapter_dir}"
    "${prepared_settings_model_dir}"
    "${settings_model_dir}"
    "${terminal_control_dir}"
    "${terminal_core_dir}"
    "${terminal_connection_dir}"
    "${winrt_utils_dir}/inc"
    "${buffer_dir}"
    "${types_dir}"
    "${renderer_base_dir}"
    "${renderer_include_dir}"
    "${adapter_dir}"
    "${parser_dir}"
)
target_link_libraries(openterminal_settings_adapter PRIVATE
    openterminal_settings_model
    openterminal_terminal_control_model
    openterminal_terminal_connection
    openterminal_terminal_core
    openterminal_winrt_utils
    openterminal_types
    runtimeobject
    windowsapp
    ole32
    shell32
    user32
)

if(OPENTERMINAL_XAML_GENERATED_DIR)
    foreach(xaml_output IN ITEMS
        CodeBlock.xaml.g.h
        CodeBlock.xaml.g.hpp
        XamlBindingInfo.xaml.g.h
        XamlBindingInfo.xaml.g.hpp
        XamlMetaDataProvider.h
        XamlTypeInfo.xaml.g.h
        XamlTypeInfo.Impl.g.cpp
        XamlTypeInfo.g.cpp)
        if(NOT EXISTS "${OPENTERMINAL_XAML_GENERATED_DIR}/Microsoft/Terminal/UI/Markdown/${xaml_output}")
            message(FATAL_ERROR
                "OPENTERMINAL_XAML_GENERATED_DIR is missing Microsoft/Terminal/UI/Markdown/${xaml_output}")
        endif()
    endforeach()

    set(uimarkdown_dir "${TERMINAL_SOURCE_DIR}/src/cascadia/UIMarkdown")
    set(uimarkdown_generated_dir
        "${CPPWINRT_UIMARKDOWN_INCLUDE_DIR}/Microsoft/Terminal/UI/Markdown")
    add_library(openterminal_ui_markdown STATIC
        "${feature_staging_header}"
        "${uimarkdown_dir}/init.cpp"
        "${uimarkdown_dir}/pch.cpp"
        "${uimarkdown_dir}/Builder.cpp"
        "${prepared_uimarkdown_code_block_source}"
        "${uimarkdown_dir}/MarkdownToXaml.cpp"
        "${CPPWINRT_UIMARKDOWN_INCLUDE_DIR}/module.g.cpp"
        "${OPENTERMINAL_XAML_GENERATED_DIR}/Microsoft/Terminal/UI/Markdown/XamlTypeInfo.Impl.g.cpp"
        "${OPENTERMINAL_XAML_GENERATED_DIR}/Microsoft/Terminal/UI/Markdown/XamlTypeInfo.g.cpp"
    )
    set_target_properties(openterminal_ui_markdown PROPERTIES
        OUTPUT_NAME Microsoft.Terminal.UI.Markdown)
    configure_terminal_target(openterminal_ui_markdown)
    target_compile_definitions(openterminal_ui_markdown PRIVATE WINRT_LEAN_AND_MEAN)
    target_compile_options(openterminal_ui_markdown PRIVATE -mcx16)
    target_include_directories(openterminal_ui_markdown PRIVATE
        "${OPENTERMINAL_XAML_GENERATED_DIR}"
        "${OPENTERMINAL_XAML_GENERATED_DIR}/Microsoft/Terminal/UI/Markdown"
        "${CPPWINRT_UIMARKDOWN_INCLUDE_DIR}"
        "${uimarkdown_generated_dir}"
        "${CPPWINRT_UIHELPERS_INCLUDE_DIR}"
        "${ui_helpers_generated_dir}"
        "${CPPWINRT_WINUI_INCLUDE_DIR}"
        "${CPPWINRT_SDK_INCLUDE_DIR}"
        "${TERMINAL_SOURCE_DIR}/src"
        "${TERMINAL_SOURCE_DIR}/src/cascadia"
        "${TERMINAL_SOURCE_DIR}/src/cascadia/inc"
        "${uimarkdown_dir}"
        "${ui_helpers_dir}"
        "${winrt_utils_dir}/inc"
        "${types_dir}"
    )
    target_link_libraries(openterminal_ui_markdown PRIVATE
        openterminal_ui_helpers
        openterminal_winrt_utils
        openterminal_types
        cmark::cmark
        runtimeobject
        windowsapp
        ole32
        shell32
        user32
    )

    set(settings_editor_dir
        "${TERMINAL_SOURCE_DIR}/src/cascadia/TerminalSettingsEditor")
    set(settings_editor_generated_dir
        "${CPPWINRT_SETTINGSEDITOR_INCLUDE_DIR}/Microsoft/Terminal/Settings/Editor")
    set(settings_index_header
        "${prepared_settings_editor_dir}/GeneratedSettingsIndex.g.h")
    set(settings_index_source
        "${prepared_settings_editor_dir}/GeneratedSettingsIndex.g.cpp")
    add_custom_command(
        OUTPUT "${settings_index_header}" "${settings_index_source}"
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_settings_index.py"
            --source "${settings_editor_dir}"
            --output "${prepared_settings_editor_dir}"
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_settings_index.py"
            ${settings_editor_prepare_inputs}
        COMMENT "Generating the Terminal Settings search index"
        VERBATIM
    )

    set(settings_editor_xaml_classes
        Actions EditAction AddProfile GlobalAppearance ColorSchemes
        NullableColorPicker IconPicker EditColorScheme Interaction Compatibility
        KeyChordListener Launch NewTabMenu MainPage Extensions Profiles_Base
        Profiles_Base_Orphaned Profiles_Advanced Profiles_Appearance
        Profiles_Terminal Appearances Rendering)
    foreach(page IN LISTS settings_editor_xaml_classes)
        foreach(suffix IN ITEMS xaml.g.h xaml.g.hpp)
            if(NOT EXISTS "${OPENTERMINAL_XAML_GENERATED_DIR}/Microsoft/Terminal/Settings/Editor/${page}.${suffix}")
                message(FATAL_ERROR
                    "OPENTERMINAL_XAML_GENERATED_DIR is missing Microsoft/Terminal/Settings/Editor/${page}.${suffix}")
            endif()
        endforeach()
    endforeach()
    foreach(binding IN ITEMS XamlBindingInfo.xaml.g.h XamlBindingInfo.xaml.g.hpp)
        if(NOT EXISTS "${OPENTERMINAL_XAML_GENERATED_DIR}/Microsoft/Terminal/Settings/Editor/${binding}")
            message(FATAL_ERROR
                "OPENTERMINAL_XAML_GENERATED_DIR is missing Microsoft/Terminal/Settings/Editor/${binding}")
        endif()
    endforeach()
    foreach(metadata IN ITEMS
        XamlMetaDataProvider.h
        XamlTypeInfo.xaml.g.h
        XamlTypeInfo.Impl.g.cpp
        XamlTypeInfo.g.cpp)
        if(NOT EXISTS "${OPENTERMINAL_XAML_GENERATED_DIR}/Microsoft/Terminal/Settings/Editor/${metadata}")
            message(FATAL_ERROR
                "OPENTERMINAL_XAML_GENERATED_DIR is missing Microsoft/Terminal/Settings/Editor/${metadata}")
        endif()
    endforeach()

    add_library(openterminal_settings_editor STATIC
        "${feature_staging_header}"
        ${prepared_settings_editor_sources}
        "${CPPWINRT_SETTINGSEDITOR_INCLUDE_DIR}/module.g.cpp"
        "${settings_index_source}"
        "${TERMINAL_SOURCE_DIR}/src/cascadia/fzf/fzf.cpp"
        "${OPENTERMINAL_XAML_GENERATED_DIR}/Microsoft/Terminal/Settings/Editor/XamlTypeInfo.Impl.g.cpp"
        "${OPENTERMINAL_XAML_GENERATED_DIR}/Microsoft/Terminal/Settings/Editor/XamlTypeInfo.g.cpp"
        compat/src/settings_editor_xaml_metadata_provider.cpp
    )
    set_target_properties(openterminal_settings_editor PROPERTIES
        OUTPUT_NAME Microsoft.Terminal.Settings.Editor)
    configure_terminal_target(openterminal_settings_editor)
    configure_terminal_resource_scope(openterminal_settings_editor
        "Microsoft.Terminal.Settings.Editor/Resources")
    target_compile_definitions(openterminal_settings_editor PRIVATE WINRT_LEAN_AND_MEAN)
    target_compile_options(openterminal_settings_editor PRIVATE -mcx16)
    target_include_directories(openterminal_settings_editor PRIVATE
        "${OPENTERMINAL_XAML_GENERATED_DIR}"
        "${OPENTERMINAL_XAML_GENERATED_DIR}/Microsoft/Terminal/Settings/Editor"
        "${CPPWINRT_SETTINGSEDITOR_INCLUDE_DIR}"
        "${settings_editor_generated_dir}"
        "${CPPWINRT_SETTINGSMODEL_INCLUDE_DIR}"
        "${settings_model_generated_dir}"
        "${CPPWINRT_TERMINALCONTROL_INCLUDE_DIR}"
        "${terminal_control_generated_dir}"
        "${CPPWINRT_TERMINALCONNECTION_INCLUDE_DIR}"
        "${terminal_connection_generated_dir}"
        "${CPPWINRT_TERMINALCORE_INCLUDE_DIR}"
        "${CPPWINRT_UIHELPERS_INCLUDE_DIR}"
        "${ui_helpers_generated_dir}"
        "${CPPWINRT_UIMARKDOWN_INCLUDE_DIR}"
        "${uimarkdown_generated_dir}"
        "${CPPWINRT_WINUI_INCLUDE_DIR}"
        "${CPPWINRT_SDK_INCLUDE_DIR}"
        "${TERMINAL_SOURCE_DIR}/src"
        "${TERMINAL_SOURCE_DIR}/src/cascadia"
        "${TERMINAL_SOURCE_DIR}/src/cascadia/inc"
        "${prepared_settings_editor_dir}"
        "${settings_editor_dir}"
        "${settings_adapter_dir}"
        "${prepared_settings_model_dir}"
        "${settings_model_dir}"
        "${terminal_control_dir}"
        "${terminal_connection_dir}"
        "${terminal_core_dir}"
        "${ui_helpers_dir}"
        "${winrt_utils_dir}/inc"
        "${buffer_dir}"
        "${types_dir}"
        "${renderer_base_dir}"
        "${renderer_include_dir}"
        "${TERMINAL_SOURCE_DIR}/src/cascadia/fzf"
    )
    target_link_libraries(openterminal_settings_editor PRIVATE
        openterminal_settings_adapter
        openterminal_settings_model
        openterminal_terminal_control_model
        openterminal_terminal_connection
        openterminal_terminal_core
        openterminal_ui_markdown
        openterminal_ui_helpers
        openterminal_winrt_utils
        openterminal_renderer_base
        openterminal_types
        fmt::fmt
        runtimeobject
        windowsapp
        ole32
        shell32
        shlwapi
        user32
        dwrite
    )

    set(terminal_app_dir "${TERMINAL_SOURCE_DIR}/src/cascadia/TerminalApp")
    set(terminal_app_generated_dir
        "${CPPWINRT_TERMINALAPP_INCLUDE_DIR}/TerminalApp")
    set(terminal_app_xaml_classes
        App AboutDialog MinMaxCloseControl TerminalPage TitlebarControl
        TabRowControl TabHeaderControl ColorPickupFlyout CommandPalette
        SuggestionsControl SnippetsPaneContent MarkdownPaneContent)
    foreach(page IN LISTS terminal_app_xaml_classes)
        foreach(suffix IN ITEMS xaml.g.h xaml.g.hpp)
            if(NOT EXISTS "${OPENTERMINAL_XAML_GENERATED_DIR}/TerminalApp/${page}.${suffix}")
                message(FATAL_ERROR
                    "OPENTERMINAL_XAML_GENERATED_DIR is missing TerminalApp/${page}.${suffix}")
            endif()
        endforeach()
    endforeach()
    foreach(binding IN ITEMS XamlBindingInfo.xaml.g.h XamlBindingInfo.xaml.g.hpp)
        if(NOT EXISTS "${OPENTERMINAL_XAML_GENERATED_DIR}/TerminalApp/${binding}")
            message(FATAL_ERROR
                "OPENTERMINAL_XAML_GENERATED_DIR is missing TerminalApp/${binding}")
        endif()
    endforeach()
    foreach(metadata IN ITEMS
        XamlMetaDataProvider.h
        XamlTypeInfo.xaml.g.h
        XamlTypeInfo.Impl.g.cpp
        XamlTypeInfo.g.cpp)
        if(NOT EXISTS "${OPENTERMINAL_XAML_GENERATED_DIR}/TerminalApp/${metadata}")
            message(FATAL_ERROR
                "OPENTERMINAL_XAML_GENERATED_DIR is missing TerminalApp/${metadata}")
        endif()
    endforeach()

    add_library(openterminal_terminal_app STATIC
        "${feature_staging_header}"
        ${prepared_terminal_app_sources}
        "${CPPWINRT_TERMINALAPP_INCLUDE_DIR}/module.g.cpp"
        "${TERMINAL_SOURCE_DIR}/src/cascadia/fzf/fzf.cpp"
        "${OPENTERMINAL_XAML_GENERATED_DIR}/TerminalApp/XamlTypeInfo.Impl.g.cpp"
        "${OPENTERMINAL_XAML_GENERATED_DIR}/TerminalApp/XamlTypeInfo.g.cpp"
    )
    set_target_properties(openterminal_terminal_app PROPERTIES
        OUTPUT_NAME TerminalAppLib)
    configure_terminal_target(openterminal_terminal_app)
    configure_terminal_resource_scope(openterminal_terminal_app
        "TerminalApp/Resources")
    target_compile_definitions(openterminal_terminal_app PRIVATE WINRT_LEAN_AND_MEAN)
    target_compile_options(openterminal_terminal_app PRIVATE -mcx16)
    target_include_directories(openterminal_terminal_app PRIVATE
        "${OPENTERMINAL_XAML_GENERATED_DIR}"
        "${OPENTERMINAL_XAML_GENERATED_DIR}/TerminalApp"
        "${CPPWINRT_TERMINALAPP_INCLUDE_DIR}"
        "${terminal_app_generated_dir}"
        "${CPPWINRT_SETTINGSEDITOR_INCLUDE_DIR}"
        "${settings_editor_generated_dir}"
        "${CPPWINRT_SETTINGSMODEL_INCLUDE_DIR}"
        "${settings_model_generated_dir}"
        "${CPPWINRT_TERMINALCONTROL_INCLUDE_DIR}"
        "${terminal_control_generated_dir}"
        "${CPPWINRT_TERMINALCONNECTION_INCLUDE_DIR}"
        "${terminal_connection_generated_dir}"
        "${CPPWINRT_TERMINALCORE_INCLUDE_DIR}"
        "${CPPWINRT_UIHELPERS_INCLUDE_DIR}"
        "${ui_helpers_generated_dir}"
        "${CPPWINRT_UIMARKDOWN_INCLUDE_DIR}"
        "${uimarkdown_generated_dir}"
        "${CPPWINRT_WINGET_INCLUDE_DIR}"
        "${CPPWINRT_WINUI_INCLUDE_DIR}"
        "${CPPWINRT_SDK_INCLUDE_DIR}"
        "${TERMINAL_SOURCE_DIR}/src"
        "${TERMINAL_SOURCE_DIR}/src/cascadia"
        "${TERMINAL_SOURCE_DIR}/src/cascadia/inc"
        "${prepared_terminal_app_dir}"
        "${terminal_app_dir}"
        "${prepared_settings_editor_dir}"
        "${settings_editor_dir}"
        "${settings_adapter_dir}"
        "${prepared_settings_model_dir}"
        "${settings_model_dir}"
        "${terminal_control_dir}"
        "${terminal_connection_dir}"
        "${terminal_core_dir}"
        "${ui_helpers_dir}"
        "${winrt_utils_dir}/inc"
        "${buffer_dir}"
        "${types_dir}"
        "${renderer_base_dir}"
        "${renderer_include_dir}"
        "${adapter_dir}"
        "${parser_dir}"
        "${TERMINAL_SOURCE_DIR}/src/cascadia/fzf"
    )
    target_link_libraries(openterminal_terminal_app PRIVATE
        openterminal_settings_editor
        openterminal_settings_adapter
        openterminal_settings_model
        openterminal_terminal_control_model
        openterminal_terminal_connection
        openterminal_terminal_core
        openterminal_ui_markdown
        openterminal_ui_helpers
        openterminal_winrt_utils
        openterminal_renderer_base
        openterminal_buffer_out
        openterminal_terminal_adapter
        openterminal_terminal_parser
        openterminal_terminal_input
        openterminal_types
        CLI11::CLI11
        fmt::fmt
        ICU::i18n
        ICU::uc
        runtimeobject
        windowsapp
        ole32
        shell32
        shlwapi
        user32
        dwrite
        dwmapi
    )

    set(windows_terminal_dir
        "${TERMINAL_SOURCE_DIR}/src/cascadia/WindowsTerminal")
    add_executable(openterminal_windows_terminal WIN32
        "${feature_staging_header}"
        ${prepared_windows_terminal_sources}
        "${prepared_windows_terminal_resource}"
        "${prepared_settings_model_resource}"
        "${CMAKE_CURRENT_SOURCE_DIR}/compat/windows_terminal_manifest.rc"
    )
    set_target_properties(openterminal_windows_terminal PROPERTIES
        OUTPUT_NAME WindowsTerminal)
    if(NOT EXISTS "${OPENTERMINAL_XAMLCORE_IMPORT_LIBRARY}")
        message(FATAL_ERROR
            "OPENTERMINAL_XAMLCORE_IMPORT_LIBRARY must name the OpenXaml import library")
    endif()
    configure_terminal_target(openterminal_windows_terminal)
    configure_terminal_resource_scope(openterminal_windows_terminal
        "TerminalApp/Resources")
    add_dependencies(openterminal_windows_terminal openterminal_terminal_app)
    target_compile_definitions(openterminal_windows_terminal PRIVATE
        WINRT_LEAN_AND_MEAN)
    target_compile_options(openterminal_windows_terminal PRIVATE -mcx16)
    target_link_options(openterminal_windows_terminal PRIVATE -municode)
    target_include_directories(openterminal_windows_terminal PRIVATE
        "${prepared_windows_terminal_dir}"
        "${prepared_terminal_app_dir}"
        "${CPPWINRT_TERMINALAPP_INCLUDE_DIR}"
        "${CPPWINRT_TERMINALCONTROL_INCLUDE_DIR}"
        "${CPPWINRT_TERMINALCORE_INCLUDE_DIR}"
        "${CPPWINRT_SETTINGSMODEL_INCLUDE_DIR}"
        "${CPPWINRT_SETTINGSEDITOR_INCLUDE_DIR}"
        "${CPPWINRT_TERMINALCONNECTION_INCLUDE_DIR}"
        "${CPPWINRT_UIHELPERS_INCLUDE_DIR}"
        "${CPPWINRT_UIMARKDOWN_INCLUDE_DIR}"
        "${CPPWINRT_WINGET_INCLUDE_DIR}"
        "${CPPWINRT_WINUI_INCLUDE_DIR}"
        "${CPPWINRT_SDK_INCLUDE_DIR}"
        "${WINDOWS_SDK_WINRT_INCLUDE_DIR}"
        "${TERMINAL_SOURCE_DIR}/src"
        "${TERMINAL_SOURCE_DIR}/src/inc"
        "${TERMINAL_SOURCE_DIR}/src/types/inc"
        "${TERMINAL_SOURCE_DIR}/src/cascadia"
        "${TERMINAL_SOURCE_DIR}/src/cascadia/inc"
        "${TERMINAL_SOURCE_DIR}/src/cascadia/TerminalApp"
        "${TERMINAL_SOURCE_DIR}/src/cascadia/WinRTUtils/inc"
        "${windows_terminal_dir}"
    )
    target_link_libraries(openterminal_windows_terminal PRIVATE
        -Wl,--start-group
        openterminal_terminal_app
        openterminal_settings_editor
        openterminal_settings_adapter
        openterminal_settings_model
        openterminal_terminal_control_model
        openterminal_terminal_connection
        openterminal_terminal_core
        openterminal_ui_markdown
        openterminal_ui_helpers
        openterminal_winrt_utils
        openterminal_renderer_atlas
        openterminal_renderer_uia
        openterminal_renderer_base
        openterminal_buffer_out
        openterminal_terminal_adapter
        openterminal_terminal_parser
        openterminal_terminal_input
        openterminal_midi_audio
        openterminal_tsf
        openterminal_winconpty
        openterminal_types
        CLI11::CLI11
        fmt::fmt
        ICU::i18n
        ICU::uc
        ICU::data
        -Wl,--end-group
        runtimeobject
        windowsapp
        ntdll
        ole32
        user32
        gdi32
        dwmapi
        shcore
        uxtheme
        shell32
        shlwapi
        comctl32
        propsys
        "${OPENTERMINAL_XAMLCORE_IMPORT_LIBRARY}"
    )
endif()

set(launcher_resource_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/launchers")
file(MAKE_DIRECTORY "${launcher_resource_dir}")
configure_file(
    "${TERMINAL_SOURCE_DIR}/res/terminal.ico"
    "${launcher_resource_dir}/terminal.ico"
    COPYONLY
)
foreach(launcher_name IN ITEMS wt elevate-shim)
    if(launcher_name STREQUAL "wt")
        set(launcher_source_dir "${TERMINAL_SOURCE_DIR}/src/cascadia/wt")
    else()
        set(launcher_source_dir "${TERMINAL_SOURCE_DIR}/src/cascadia/ElevateShim")
    endif()
    file(READ "${launcher_source_dir}/${launcher_name}.rc" launcher_resource_content)
    string(REPLACE [[\\]] "/" launcher_resource_content
        "${launcher_resource_content}")
    string(REPLACE "../../../res/terminal.ico"
        "${launcher_resource_dir}/terminal.ico"
        launcher_resource_content
        "${launcher_resource_content}")
    file(CONFIGURE
        OUTPUT "${launcher_resource_dir}/${launcher_name}.rc"
        CONTENT "${launcher_resource_content}"
        @ONLY
        NEWLINE_STYLE UNIX
    )
endforeach()

set(wt_launcher_dir "${TERMINAL_SOURCE_DIR}/src/cascadia/wt")
add_executable(openterminal_wt_launcher WIN32
    "${feature_staging_header}"
    "${wt_launcher_dir}/shim.cpp"
    "${launcher_resource_dir}/wt.rc"
)
set_target_properties(openterminal_wt_launcher PROPERTIES OUTPUT_NAME wt)
configure_terminal_target(openterminal_wt_launcher)
target_include_directories(openterminal_wt_launcher PRIVATE "${wt_launcher_dir}")
target_link_options(openterminal_wt_launcher PRIVATE
    -municode -static -static-libgcc -static-libstdc++)

set(elevate_shim_dir "${TERMINAL_SOURCE_DIR}/src/cascadia/ElevateShim")
add_executable(openterminal_elevate_shim WIN32
    "${feature_staging_header}"
    "${elevate_shim_dir}/elevate-shim.cpp"
    "${launcher_resource_dir}/elevate-shim.rc"
)
set_target_properties(openterminal_elevate_shim PROPERTIES OUTPUT_NAME elevate-shim)
configure_terminal_target(openterminal_elevate_shim)
target_compile_definitions(openterminal_elevate_shim PRIVATE _MINAPPMODEL_H_)
target_include_directories(openterminal_elevate_shim PRIVATE "${elevate_shim_dir}")
target_link_libraries(openterminal_elevate_shim PRIVATE shell32)
target_link_options(openterminal_elevate_shim PRIVATE
    -municode -static -static-libgcc -static-libstdc++)

set(openterminal_deploy_targets
    openterminal_wt_launcher
    openterminal_elevate_shim)
if(TARGET openterminal_windows_terminal)
    list(PREPEND openterminal_deploy_targets openterminal_windows_terminal)

    # GCC's PE runtime DLLs live beside the cross toolchain, not necessarily
    # on the host PATH used to launch Wine. Resolve them from the configured
    # compiler rather than encoding a distribution-specific /usr path.
    execute_process(
        COMMAND "${CMAKE_CXX_COMPILER}" -dumpmachine
        OUTPUT_VARIABLE mingw_target_triple
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE mingw_target_result
    )
    if(NOT mingw_target_result EQUAL 0 OR mingw_target_triple STREQUAL "")
        message(FATAL_ERROR "Could not determine the MinGW target triple")
    endif()
    get_filename_component(mingw_compiler_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    get_filename_component(mingw_compiler_parent "${mingw_compiler_dir}" DIRECTORY)
    set(mingw_runtime_hints
        "${mingw_compiler_dir}"
        "${mingw_compiler_parent}/${mingw_target_triple}/bin")
    foreach(mingw_root IN LISTS CMAKE_FIND_ROOT_PATH)
        list(APPEND mingw_runtime_hints "${mingw_root}/bin")
    endforeach()

    set(openterminal_mingw_runtime_dlls)
    foreach(runtime_name IN ITEMS
            libgcc_s_seh-1.dll
            libstdc++-6.dll
            libwinpthread-1.dll)
        find_file(runtime_path
            NAMES "${runtime_name}"
            HINTS ${mingw_runtime_hints}
            NO_DEFAULT_PATH
            NO_CACHE)
        if(NOT runtime_path)
            message(FATAL_ERROR
                "Could not locate ${runtime_name} for ${mingw_target_triple}")
        endif()
        list(APPEND openterminal_mingw_runtime_dlls "${runtime_path}")
        unset(runtime_path)
    endforeach()

    # Terminal's default face is Cascadia Mono. Its renderer deliberately
    # discovers packaged fonts beside WindowsTerminal.exe through the nearby
    # font collection, matching the MSIX payload without installing them into
    # the machine-wide font store. Keep all four upstream family/style files
    # together so fallback and italic selection use the same collection.
    set(openterminal_nearby_fonts
        "${TERMINAL_SOURCE_DIR}/res/fonts/CascadiaMono.ttf"
        "${TERMINAL_SOURCE_DIR}/res/fonts/CascadiaMonoItalic.ttf"
        "${TERMINAL_SOURCE_DIR}/res/fonts/CascadiaCode.ttf"
        "${TERMINAL_SOURCE_DIR}/res/fonts/CascadiaCodeItalic.ttf")
    foreach(font_path IN LISTS openterminal_nearby_fonts)
        if(NOT EXISTS "${font_path}")
            message(FATAL_ERROR "Terminal nearby font is missing: ${font_path}")
        endif()
    endforeach()

    add_custom_command(TARGET openterminal_windows_terminal POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            ${openterminal_mingw_runtime_dlls}
            "$<TARGET_FILE_DIR:openterminal_windows_terminal>"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            ${openterminal_nearby_fonts}
            "$<TARGET_FILE_DIR:openterminal_windows_terminal>"
        COMMENT "Staging MinGW runtime DLLs and Terminal nearby fonts"
        VERBATIM)
    add_custom_target(openterminal_stage_runtime
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            ${openterminal_mingw_runtime_dlls}
            "$<TARGET_FILE_DIR:openterminal_windows_terminal>"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            ${openterminal_nearby_fonts}
            "$<TARGET_FILE_DIR:openterminal_windows_terminal>"
        DEPENDS ${openterminal_deploy_targets}
        COMMENT "Staging the native Terminal launch bundle"
        VERBATIM)

    install(FILES ${openterminal_mingw_runtime_dlls}
        DESTINATION .
        COMPONENT Runtime)
    install(FILES ${openterminal_nearby_fonts}
        DESTINATION .
        COMPONENT Runtime)
endif()

install(TARGETS ${openterminal_deploy_targets}
    RUNTIME DESTINATION .
    COMPONENT Runtime)

add_executable(openterminal_parser_smoke tests/parser_smoke.cpp)
target_compile_features(openterminal_parser_smoke PRIVATE cxx_std_20)
target_compile_definitions(openterminal_parser_smoke PRIVATE UNICODE _UNICODE)
target_include_directories(openterminal_parser_smoke PRIVATE "${parser_dir}")
target_link_libraries(openterminal_parser_smoke PRIVATE openterminal_terminal_parser)
target_link_options(openterminal_parser_smoke PRIVATE -static -static-libgcc -static-libstdc++)

add_executable(openterminal_types_smoke tests/types_smoke.cpp)
configure_terminal_target(openterminal_types_smoke)
target_include_directories(openterminal_types_smoke PRIVATE "${types_dir}")
target_link_libraries(openterminal_types_smoke PRIVATE openterminal_types)
target_link_options(openterminal_types_smoke PRIVATE -static -static-libgcc -static-libstdc++)

add_executable(openterminal_buffer_smoke tests/buffer_smoke.cpp)
configure_terminal_target(openterminal_buffer_smoke)
target_include_directories(openterminal_buffer_smoke PRIVATE "${buffer_dir}")
target_link_libraries(openterminal_buffer_smoke PRIVATE openterminal_buffer_out)
target_link_options(openterminal_buffer_smoke PRIVATE -static -static-libgcc -static-libstdc++)

add_executable(openterminal_renderer_base_smoke tests/renderer_base_smoke.cpp)
configure_terminal_target(openterminal_renderer_base_smoke)
target_include_directories(openterminal_renderer_base_smoke PRIVATE "${renderer_include_dir}")
target_link_libraries(openterminal_renderer_base_smoke PRIVATE openterminal_renderer_base)
target_link_options(openterminal_renderer_base_smoke PRIVATE -static -static-libgcc -static-libstdc++)

add_executable(openterminal_renderer_gdi_smoke tests/renderer_gdi_smoke.cpp)
configure_terminal_target(openterminal_renderer_gdi_smoke)
target_include_directories(openterminal_renderer_gdi_smoke PRIVATE
    "${renderer_gdi_dir}"
    "${renderer_include_dir}"
)
target_link_libraries(openterminal_renderer_gdi_smoke PRIVATE
    "$<LINK_GROUP:RESCAN,openterminal_renderer_gdi,openterminal_renderer_base,openterminal_buffer_out,openterminal_types,ICU::i18n,ICU::uc,ICU::data>"
    ntdll
    bcrypt
    mincore
)
target_link_options(openterminal_renderer_gdi_smoke PRIVATE -static -static-libgcc -static-libstdc++)

add_executable(openterminal_renderer_uia_smoke tests/renderer_uia_smoke.cpp)
configure_terminal_target(openterminal_renderer_uia_smoke)
target_include_directories(openterminal_renderer_uia_smoke PRIVATE
    "${renderer_uia_dir}"
    "${renderer_include_dir}"
    "${types_dir}"
)
target_link_libraries(openterminal_renderer_uia_smoke PRIVATE openterminal_renderer_uia)
target_link_options(openterminal_renderer_uia_smoke PRIVATE -static -static-libgcc -static-libstdc++)

add_executable(openterminal_renderer_atlas_smoke tests/atlas_smoke.cpp)
configure_terminal_target(openterminal_renderer_atlas_smoke)
target_include_directories(openterminal_renderer_atlas_smoke PRIVATE
    "${renderer_atlas_dir}"
    "${renderer_include_dir}"
    "${buffer_dir}"
    "${types_dir}"
)
target_link_libraries(openterminal_renderer_atlas_smoke PRIVATE
    "$<LINK_GROUP:RESCAN,openterminal_renderer_atlas,openterminal_renderer_base,openterminal_buffer_out,openterminal_types,ICU::i18n,ICU::uc,ICU::data>"
    ntdll
    bcrypt
    mincore
)
target_link_options(openterminal_renderer_atlas_smoke PRIVATE -static -static-libgcc -static-libstdc++)

add_executable(openterminal_adapter_smoke tests/adapter_smoke.cpp)
configure_terminal_target(openterminal_adapter_smoke)
target_include_directories(openterminal_adapter_smoke PRIVATE "${adapter_dir}" "${parser_dir}")
target_link_libraries(openterminal_adapter_smoke PRIVATE openterminal_terminal_adapter)
target_link_options(openterminal_adapter_smoke PRIVATE -static -static-libgcc -static-libstdc++)

add_executable(openterminal_terminal_core_smoke tests/terminal_core_smoke.cpp)
configure_terminal_target(openterminal_terminal_core_smoke)
target_compile_definitions(openterminal_terminal_core_smoke PRIVATE WINRT_LEAN_AND_MEAN)
target_include_directories(openterminal_terminal_core_smoke PRIVATE
    "${CPPWINRT_TERMINALCORE_INCLUDE_DIR}"
    "${CPPWINRT_SDK_INCLUDE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/compat/cascadia/TerminalCore"
    "${TERMINAL_SOURCE_DIR}/src"
    "${terminal_core_dir}"
    "${buffer_dir}"
    "${types_dir}"
    "${renderer_include_dir}"
    "${adapter_dir}"
    "${parser_dir}"
)
target_link_libraries(openterminal_terminal_core_smoke PRIVATE
    "$<LINK_GROUP:RESCAN,openterminal_terminal_core,openterminal_terminal_adapter,openterminal_terminal_parser,openterminal_terminal_input,openterminal_renderer_base,openterminal_buffer_out,openterminal_types,ICU::i18n,ICU::uc,ICU::data>"
    ntdll
    bcrypt
    mincore
)
target_link_options(openterminal_terminal_core_smoke PRIVATE -static -static-libgcc -static-libstdc++)

add_executable(openterminal_winrt_utils_smoke tests/winrt_utils_smoke.cpp)
configure_terminal_target(openterminal_winrt_utils_smoke)
target_compile_definitions(openterminal_winrt_utils_smoke PRIVATE WINRT_LEAN_AND_MEAN)
target_include_directories(openterminal_winrt_utils_smoke PRIVATE
    "${CPPWINRT_SDK_INCLUDE_DIR}"
    "${TERMINAL_SOURCE_DIR}/src/cascadia/inc"
    "${winrt_utils_dir}"
    "${winrt_utils_dir}/inc"
    "${types_dir}"
)
target_link_libraries(openterminal_winrt_utils_smoke PRIVATE
    openterminal_winrt_utils
    ntdll
    bcrypt
    mincore
)
target_link_options(openterminal_winrt_utils_smoke PRIVATE -static -static-libgcc -static-libstdc++)

add_executable(openterminal_midi_smoke tests/midi_smoke.cpp)
configure_terminal_target(openterminal_midi_smoke)
target_include_directories(openterminal_midi_smoke PRIVATE
    "${TERMINAL_SOURCE_DIR}/src/audio"
    "${midi_dir}"
    "${parser_dir}"
)
target_link_libraries(openterminal_midi_smoke PRIVATE
    openterminal_midi_audio
    openterminal_terminal_parser
)
target_link_options(openterminal_midi_smoke PRIVATE -static -static-libgcc -static-libstdc++)

add_executable(openterminal_tsf_smoke tests/tsf_smoke.cpp)
configure_terminal_target(openterminal_tsf_smoke)
target_include_directories(openterminal_tsf_smoke PRIVATE
    "${TERMINAL_SOURCE_DIR}/src"
    "${tsf_dir}"
    "${buffer_dir}"
    "${types_dir}"
    "${renderer_base_dir}"
    "${renderer_include_dir}"
)
target_link_libraries(openterminal_tsf_smoke PRIVATE
    "$<LINK_GROUP:RESCAN,openterminal_tsf,openterminal_renderer_base,openterminal_buffer_out,openterminal_types,ICU::i18n,ICU::uc,ICU::data,fmt::fmt>"
    ntdll
    bcrypt
    mincore
    synchronization
)
target_link_options(openterminal_tsf_smoke PRIVATE
    -static -static-libgcc -static-libstdc++)

add_executable(openterminal_terminal_connection_smoke
    tests/terminal_connection_echo_smoke.cpp)
configure_terminal_target(openterminal_terminal_connection_smoke)
target_compile_definitions(openterminal_terminal_connection_smoke PRIVATE
    WINRT_LEAN_AND_MEAN)
target_include_directories(openterminal_terminal_connection_smoke PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/generated/TerminalConnection"
    "${CPPWINRT_TERMINALCONNECTION_INCLUDE_DIR}"
    "${terminal_connection_generated_dir}"
    "${CPPWINRT_SDK_INCLUDE_DIR}"
    "${TERMINAL_SOURCE_DIR}/src/cascadia/inc"
    "${winrt_utils_dir}/inc"
    "${terminal_connection_dir}"
    "${types_dir}"
)
target_link_libraries(openterminal_terminal_connection_smoke PRIVATE
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,openterminal_terminal_connection>"
    "$<LINK_GROUP:RESCAN,ICU::i18n,ICU::uc,ICU::data>"
    ntdll
    bcrypt
    mincore
)
target_link_options(openterminal_terminal_connection_smoke PRIVATE
    -static -static-libgcc -static-libstdc++)

add_executable(openterminal_winconpty_smoke tests/winconpty_smoke.cpp)
configure_terminal_target(openterminal_winconpty_smoke)
target_compile_definitions(openterminal_winconpty_smoke PRIVATE CONPTY_IMPEXP=)
target_include_directories(openterminal_winconpty_smoke PRIVATE
    "${winconpty_dir}"
)
target_link_libraries(openterminal_winconpty_smoke PRIVATE
    openterminal_winconpty
    ntdll
    bcrypt
    mincore
)
target_link_options(openterminal_winconpty_smoke PRIVATE
    -static -static-libgcc -static-libstdc++)

# The one PE that links both halves of a live session: the pseudoconsole from
# src/winconpty and the terminal from src/cascadia/TerminalCore. It spawns a
# shell through the former and answers whether the shell's output reached the
# latter's text buffer. phase4/scripts/conpty_live.py drives it under Wine;
# it is not in the ctest list because it needs a Wine prefix with a working
# console host, which is a phase-4 integration concern, not a smoke test.
add_executable(openterminal_conpty_terminal_core_gate tests/conpty_terminal_core_gate.cpp)
configure_terminal_target(openterminal_conpty_terminal_core_gate)
target_compile_definitions(openterminal_conpty_terminal_core_gate PRIVATE
    CONPTY_IMPEXP=
    WINRT_LEAN_AND_MEAN
)
target_include_directories(openterminal_conpty_terminal_core_gate PRIVATE
    "${CPPWINRT_TERMINALCORE_INCLUDE_DIR}"
    "${CPPWINRT_SDK_INCLUDE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/compat/cascadia/TerminalCore"
    "${TERMINAL_SOURCE_DIR}/src"
    "${terminal_core_dir}"
    "${buffer_dir}"
    "${types_dir}"
    "${renderer_include_dir}"
    "${adapter_dir}"
    "${parser_dir}"
    "${server_dir}"
    "${winconpty_dir}"
)
target_link_libraries(openterminal_conpty_terminal_core_gate PRIVATE
    "$<LINK_GROUP:RESCAN,openterminal_terminal_core,openterminal_terminal_adapter,openterminal_terminal_parser,openterminal_terminal_input,openterminal_renderer_base,openterminal_buffer_out,openterminal_types,openterminal_winconpty,ICU::i18n,ICU::uc,ICU::data>"
    ntdll
    bcrypt
    mincore
)
target_link_options(openterminal_conpty_terminal_core_gate PRIVATE
    -municode -static -static-libgcc -static-libstdc++)

add_executable(openterminal_winui_projection_smoke tests/winui_projection_smoke.cpp)
target_compile_features(openterminal_winui_projection_smoke PRIVATE cxx_std_20)
target_compile_definitions(openterminal_winui_projection_smoke PRIVATE
    UNICODE _UNICODE WINRT_LEAN_AND_MEAN)
target_include_directories(openterminal_winui_projection_smoke PRIVATE
    "${CPPWINRT_WINUI_INCLUDE_DIR}"
    "${CPPWINRT_SDK_INCLUDE_DIR}"
)
target_link_libraries(openterminal_winui_projection_smoke PRIVATE
    runtimeobject
    windowsapp
)
target_link_options(openterminal_winui_projection_smoke PRIVATE
    -static -static-libgcc -static-libstdc++)

add_executable(openterminal_ui_helpers_smoke tests/ui_helpers_smoke.cpp)
configure_terminal_target(openterminal_ui_helpers_smoke)
target_compile_definitions(openterminal_ui_helpers_smoke PRIVATE WINRT_LEAN_AND_MEAN)
target_include_directories(openterminal_ui_helpers_smoke PRIVATE
    "${CPPWINRT_UIHELPERS_INCLUDE_DIR}"
    "${ui_helpers_generated_dir}"
    "${CPPWINRT_WINUI_INCLUDE_DIR}"
    "${CPPWINRT_SDK_INCLUDE_DIR}"
    "${TERMINAL_SOURCE_DIR}/src/cascadia"
    "${TERMINAL_SOURCE_DIR}/src/cascadia/inc"
    "${winrt_utils_dir}/inc"
    "${ui_helpers_dir}"
    "${types_dir}"
)
target_link_libraries(openterminal_ui_helpers_smoke PRIVATE
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,openterminal_ui_helpers>"
    "$<LINK_GROUP:RESCAN,openterminal_winrt_utils,openterminal_types,ICU::i18n,ICU::uc,ICU::data>"
    ntdll
    bcrypt
    mincore
)
target_link_options(openterminal_ui_helpers_smoke PRIVATE
    -static -static-libgcc -static-libstdc++)

add_executable(openterminal_terminal_control_projection_smoke
    tests/terminal_control_projection_smoke.cpp)
target_compile_features(openterminal_terminal_control_projection_smoke PRIVATE cxx_std_20)
target_compile_definitions(openterminal_terminal_control_projection_smoke PRIVATE
    UNICODE _UNICODE WINRT_LEAN_AND_MEAN)
target_include_directories(openterminal_terminal_control_projection_smoke PRIVATE
    "${CPPWINRT_TERMINALCONTROL_INCLUDE_DIR}"
    "${CPPWINRT_UIHELPERS_INCLUDE_DIR}"
    "${CPPWINRT_WINUI_INCLUDE_DIR}"
    "${CPPWINRT_TERMINALCONNECTION_INCLUDE_DIR}"
    "${CPPWINRT_TERMINALCORE_INCLUDE_DIR}"
    "${CPPWINRT_SDK_INCLUDE_DIR}"
)
target_link_libraries(openterminal_terminal_control_projection_smoke PRIVATE
    runtimeobject
    windowsapp
)
target_link_options(openterminal_terminal_control_projection_smoke PRIVATE
    -static -static-libgcc -static-libstdc++)

add_executable(openterminal_terminal_control_model_smoke
    tests/terminal_control_model_smoke.cpp)
configure_terminal_target(openterminal_terminal_control_model_smoke)
target_compile_definitions(openterminal_terminal_control_model_smoke PRIVATE
    WINRT_LEAN_AND_MEAN)
target_compile_options(openterminal_terminal_control_model_smoke PRIVATE -mcx16)
target_include_directories(openterminal_terminal_control_model_smoke PRIVATE
    "${CPPWINRT_TERMINALCONTROL_INCLUDE_DIR}"
    "${terminal_control_generated_dir}"
    "${terminal_control_dir}"
    "${CPPWINRT_UIHELPERS_INCLUDE_DIR}"
    "${ui_helpers_generated_dir}"
    "${CPPWINRT_WINUI_INCLUDE_DIR}"
    "${CPPWINRT_TERMINALCONNECTION_INCLUDE_DIR}"
    "${terminal_connection_generated_dir}"
    "${CPPWINRT_TERMINALCORE_INCLUDE_DIR}"
    "${CPPWINRT_SDK_INCLUDE_DIR}"
    "${TERMINAL_SOURCE_DIR}/src"
    "${TERMINAL_SOURCE_DIR}/src/cascadia"
    "${TERMINAL_SOURCE_DIR}/src/cascadia/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/compat/cascadia/TerminalCore"
    "${winrt_utils_dir}/inc"
    "${ui_helpers_dir}"
    "${terminal_core_dir}"
    "${buffer_dir}"
    "${types_dir}"
    "${renderer_base_dir}"
    "${renderer_include_dir}"
    "${adapter_dir}"
    "${parser_dir}"
)
target_link_libraries(openterminal_terminal_control_model_smoke PRIVATE
    "$<LINK_GROUP:RESCAN,openterminal_terminal_control_model,openterminal_terminal_core,openterminal_terminal_adapter,openterminal_terminal_parser,openterminal_terminal_input,openterminal_renderer_atlas,openterminal_renderer_uia,openterminal_renderer_base,openterminal_buffer_out,openterminal_types,openterminal_terminal_connection,openterminal_winconpty,openterminal_winrt_utils,openterminal_ui_helpers,openterminal_midi_audio,openterminal_tsf,ICU::i18n,ICU::uc,ICU::data,fmt::fmt>"
    ntdll
    bcrypt
    mincore
)
target_link_options(openterminal_terminal_control_model_smoke PRIVATE
    -static -static-libgcc -static-libstdc++)

include(CTest)
if(BUILD_TESTING)
    if(WINE_EXECUTABLE)
        add_test(NAME parser_smoke COMMAND "${WINE_EXECUTABLE}" "$<TARGET_FILE:openterminal_parser_smoke>")
        add_test(NAME types_smoke COMMAND "${WINE_EXECUTABLE}" "$<TARGET_FILE:openterminal_types_smoke>")
        add_test(NAME buffer_smoke COMMAND "${WINE_EXECUTABLE}" "$<TARGET_FILE:openterminal_buffer_smoke>")
        add_test(NAME renderer_base_smoke COMMAND "${WINE_EXECUTABLE}" "$<TARGET_FILE:openterminal_renderer_base_smoke>")
        add_test(NAME renderer_gdi_smoke COMMAND "${WINE_EXECUTABLE}" "$<TARGET_FILE:openterminal_renderer_gdi_smoke>")
        add_test(NAME renderer_uia_smoke COMMAND "${WINE_EXECUTABLE}" "$<TARGET_FILE:openterminal_renderer_uia_smoke>")
        add_test(NAME renderer_atlas_smoke COMMAND "${WINE_EXECUTABLE}" "$<TARGET_FILE:openterminal_renderer_atlas_smoke>")
        add_test(NAME adapter_smoke COMMAND "${WINE_EXECUTABLE}" "$<TARGET_FILE:openterminal_adapter_smoke>")
        add_test(NAME terminal_core_smoke COMMAND "${WINE_EXECUTABLE}" "$<TARGET_FILE:openterminal_terminal_core_smoke>")
        add_test(NAME winrt_utils_smoke COMMAND "${WINE_EXECUTABLE}" "$<TARGET_FILE:openterminal_winrt_utils_smoke>")
        add_test(NAME midi_smoke COMMAND "${WINE_EXECUTABLE}" "$<TARGET_FILE:openterminal_midi_smoke>")
        add_test(NAME tsf_smoke COMMAND "${WINE_EXECUTABLE}" "$<TARGET_FILE:openterminal_tsf_smoke>")
        add_test(NAME terminal_connection_smoke COMMAND "${WINE_EXECUTABLE}" "$<TARGET_FILE:openterminal_terminal_connection_smoke>")
        add_test(NAME winconpty_smoke COMMAND "${WINE_EXECUTABLE}" "$<TARGET_FILE:openterminal_winconpty_smoke>")
        if(TARGET openterminal_windows_terminal)
            add_test(NAME wt_launcher_starts_host COMMAND
                "${WINE_EXECUTABLE}" "$<TARGET_FILE:openterminal_wt_launcher>")
        else()
            add_test(NAME wt_launcher_missing_target COMMAND
                "${WINE_EXECUTABLE}" "$<TARGET_FILE:openterminal_wt_launcher>")
        endif()
        add_test(NAME winui_projection_smoke COMMAND
            "${WINE_EXECUTABLE}" "$<TARGET_FILE:openterminal_winui_projection_smoke>")
        add_test(NAME ui_helpers_smoke COMMAND
            "${WINE_EXECUTABLE}" "$<TARGET_FILE:openterminal_ui_helpers_smoke>")
        add_test(NAME terminal_control_projection_smoke COMMAND
            "${WINE_EXECUTABLE}" "$<TARGET_FILE:openterminal_terminal_control_projection_smoke>")
        add_test(NAME terminal_control_model_smoke COMMAND
            "${WINE_EXECUTABLE}" "$<TARGET_FILE:openterminal_terminal_control_model_smoke>")
        set_tests_properties(parser_smoke PROPERTIES
            ENVIRONMENT "WINEDEBUG=-all;WINEPREFIX=${CMAKE_BINARY_DIR}/wine-prefix"
            TIMEOUT 60
        )
        set_tests_properties(types_smoke PROPERTIES
            ENVIRONMENT "WINEDEBUG=-all;WINEPREFIX=${CMAKE_BINARY_DIR}/wine-prefix"
            TIMEOUT 60
        )
        set_tests_properties(buffer_smoke PROPERTIES
            ENVIRONMENT "WINEDEBUG=-all;WINEPREFIX=${CMAKE_BINARY_DIR}/wine-prefix"
            TIMEOUT 60
        )
        set_tests_properties(renderer_base_smoke PROPERTIES
            ENVIRONMENT "WINEDEBUG=-all;WINEPREFIX=${CMAKE_BINARY_DIR}/wine-prefix"
            TIMEOUT 60
        )
        set_tests_properties(renderer_gdi_smoke PROPERTIES
            ENVIRONMENT "WINEDEBUG=-all;WINEPREFIX=${CMAKE_BINARY_DIR}/wine-prefix"
            TIMEOUT 60
        )
        set_tests_properties(renderer_uia_smoke PROPERTIES
            ENVIRONMENT "WINEDEBUG=-all;WINEPREFIX=${CMAKE_BINARY_DIR}/wine-prefix"
            TIMEOUT 60
        )
        set_tests_properties(renderer_atlas_smoke PROPERTIES
            ENVIRONMENT "WINEDEBUG=-all;WINEPREFIX=${CMAKE_BINARY_DIR}/wine-prefix"
            TIMEOUT 60
        )
        set_tests_properties(adapter_smoke PROPERTIES
            ENVIRONMENT "WINEDEBUG=-all;WINEPREFIX=${CMAKE_BINARY_DIR}/wine-prefix"
            TIMEOUT 60
        )
        set_tests_properties(terminal_core_smoke PROPERTIES
            ENVIRONMENT "WINEDEBUG=-all;WINEPREFIX=${CMAKE_BINARY_DIR}/wine-prefix"
            TIMEOUT 60
        )
        set_tests_properties(winrt_utils_smoke PROPERTIES
            ENVIRONMENT "WINEDEBUG=-all;WINEPREFIX=${CMAKE_BINARY_DIR}/wine-prefix"
            TIMEOUT 60
        )
        set_tests_properties(midi_smoke PROPERTIES
            ENVIRONMENT "WINEDEBUG=-all;WINEPREFIX=${CMAKE_BINARY_DIR}/wine-prefix"
            TIMEOUT 60
        )
        set_tests_properties(tsf_smoke PROPERTIES
            ENVIRONMENT "WINEDEBUG=-all;WINEPREFIX=${CMAKE_BINARY_DIR}/wine-prefix"
            TIMEOUT 60
        )
        set_tests_properties(terminal_connection_smoke PROPERTIES
            ENVIRONMENT "WINEDEBUG=-all;WINEPREFIX=${CMAKE_BINARY_DIR}/wine-prefix"
            TIMEOUT 60
        )
        set_tests_properties(winconpty_smoke PROPERTIES
            ENVIRONMENT "WINEDEBUG=-all;WINEPREFIX=${CMAKE_BINARY_DIR}/wine-prefix"
            TIMEOUT 60
        )
        if(TARGET openterminal_windows_terminal)
            set_tests_properties(wt_launcher_starts_host PROPERTIES
                ENVIRONMENT "WINEDEBUG=-all;WINEPREFIX=${CMAKE_BINARY_DIR}/wine-prefix"
                TIMEOUT 60
            )
        else()
            set_tests_properties(wt_launcher_missing_target PROPERTIES
                ENVIRONMENT "WINEDEBUG=-all;WINEPREFIX=${CMAKE_BINARY_DIR}/wine-prefix"
                TIMEOUT 60
                WILL_FAIL TRUE
            )
        endif()
        set_tests_properties(winui_projection_smoke PROPERTIES
            ENVIRONMENT "WINEDEBUG=-all;WINEPREFIX=${CMAKE_BINARY_DIR}/wine-prefix"
            TIMEOUT 60
        )
        set_tests_properties(ui_helpers_smoke PROPERTIES
            ENVIRONMENT "WINEDEBUG=-all;WINEPREFIX=${CMAKE_BINARY_DIR}/wine-prefix"
            TIMEOUT 60
        )
        set_tests_properties(terminal_control_projection_smoke PROPERTIES
            ENVIRONMENT "WINEDEBUG=-all;WINEPREFIX=${CMAKE_BINARY_DIR}/wine-prefix"
            TIMEOUT 60
        )
        set_tests_properties(terminal_control_model_smoke PROPERTIES
            ENVIRONMENT "WINEDEBUG=-all;WINEPREFIX=${CMAKE_BINARY_DIR}/wine-prefix"
            TIMEOUT 60
        )
    endif()
endif()
