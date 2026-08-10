#include "pch.h"
#include "ControlCore.h"
#include "EventArgs.h"
#include "KeyChord.h"

UTILS_DEFINE_LIBRARY_RESOURCE_SCOPE(L"OpenTerminal.Tests/Resources")

void* winrt_make_Microsoft_Terminal_Control_ControlInteractivity();

int main()
{
    using namespace winrt::Microsoft::Terminal::Control;

    const KeyChord chord{ true, false, true, false, 'A', 0x1e };
    if (chord.Vkey() != 'A' || chord.ScanCode() != 0x1e || chord.Hash() == 0)
    {
        return 1;
    }

    const OpenHyperlinkEventArgs hyperlink{ L"https://example.invalid/" };
    if (hyperlink.Uri() != L"https://example.invalid/")
    {
        return 2;
    }

    SelectionColor color;
    color.Color({ 7, 0, 0, 255 });
    color.IsIndex16(true);
    if (!color.IsIndex16() || color.Color().R != 7)
    {
        return 3;
    }

    auto factory = static_cast<IActivationFactory*>(
        winrt_make_Microsoft_Terminal_Control_ControlInteractivity());
    if (factory == nullptr)
    {
        return 4;
    }
    factory->Release();
    return 0;
}
