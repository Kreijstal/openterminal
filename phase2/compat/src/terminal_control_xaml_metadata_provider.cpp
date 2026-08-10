#include "pch.h"
#include "XamlMetaDataProvider.h"
#include "xaml_metadata_provider_compat.h"

winrt::Windows::UI::Xaml::Markup::IXamlMetadataProvider
OpenTerminalControlXamlMetadataProvider()
{
    return winrt::make<
        winrt::Microsoft::Terminal::Control::implementation::XamlMetaDataProvider>();
}
