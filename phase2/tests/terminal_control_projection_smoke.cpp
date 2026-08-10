#include <winrt/Microsoft.Terminal.Control.h>

namespace
{
    bool is_nonzero(const winrt::guid& value) noexcept
    {
        return value.Data1 != 0 || value.Data2 != 0 || value.Data3 != 0 ||
               value.Data4[0] != 0 || value.Data4[1] != 0 ||
               value.Data4[2] != 0 || value.Data4[3] != 0 ||
               value.Data4[4] != 0 || value.Data4[5] != 0 ||
               value.Data4[6] != 0 || value.Data4[7] != 0;
    }
}

int main()
{
    const auto settings =
        winrt::guid_of<winrt::Microsoft::Terminal::Control::IControlSettings>();
    const auto state =
        winrt::guid_of<winrt::Microsoft::Terminal::Control::ICoreState>();
    const auto keyBindings =
        winrt::guid_of<winrt::Microsoft::Terminal::Control::IKeyBindings>();

    return is_nonzero(settings) && is_nonzero(state) && is_nonzero(keyBindings)
               ? 0
               : 1;
}
