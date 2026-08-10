#include "pch.h"
#include "Converters.h"

int32_t __stdcall WINRT_GetActivationFactory(void* classId, void** factory) noexcept;

int main()
{
    using winrt::Microsoft::Terminal::UI::implementation::Converters;

    if (!Converters::InvertBoolean(false) ||
        Converters::InvertBoolean(true) ||
        Converters::PercentageToPercentageValue(0.42) != 42.0 ||
        Converters::PercentageValueToPercentage(25.0) != 0.25 ||
        Converters::StringOrEmptyIfPlaceholder(L"same", L"same") != L"")
    {
        return 1;
    }

    const auto className = winrt::hstring{ L"Microsoft.Terminal.UI.Converters" };
    void* factory{};
    if (WINRT_GetActivationFactory(winrt::get_abi(className), &factory) != S_OK ||
        factory == nullptr)
    {
        return 2;
    }
    static_cast<IActivationFactory*>(factory)->Release();
    return 0;
}
