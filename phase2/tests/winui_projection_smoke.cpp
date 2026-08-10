#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.Web.WebView2.Core.h>

int main()
{
    const auto winuiGuid = winrt::guid_of<
        winrt::Microsoft::UI::Xaml::Controls::IWebView2>();
    const auto webViewGuid = winrt::guid_of<
        winrt::Microsoft::Web::WebView2::Core::ICoreWebView2>();

    return winuiGuid.Data1 != 0 && webViewGuid.Data1 != 0 ? 0 : 1;
}
