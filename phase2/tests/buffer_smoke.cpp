#include "LibraryIncludes.h"
#include "TextColor.h"

int main()
{
    TextColor color{ RGB(0x12, 0x34, 0x56) };
    if (!color.IsRgb() || color.GetRGB() != RGB(0x12, 0x34, 0x56))
    {
        return 1;
    }

    color.SetIndex(TextColor::BRIGHT_CYAN, false);
    if (!color.IsIndex16() || color.GetIndex() != TextColor::BRIGHT_CYAN)
    {
        return 2;
    }

    if (TextColor::TransposeLegacyIndex(1) != 4 || TextColor::TransposeLegacyIndex(4) != 1)
    {
        return 3;
    }

    return 0;
}
