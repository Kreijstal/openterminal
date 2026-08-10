#include "pch.h"
#include "XamlMetaDataProvider.h"
#include "xaml_metadata_provider_compat.h"

winrt::Windows::UI::Xaml::Markup::IXamlMetadataProvider
OpenTerminalSettingsEditorXamlMetadataProvider()
{
    return winrt::make<winrt::Microsoft::Terminal::Settings::Editor::implementation::
                           XamlMetaDataProvider>();
}
