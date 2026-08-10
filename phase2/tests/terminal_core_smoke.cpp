#include "pch.h"
#include "Terminal.hpp"

using Microsoft::Terminal::Core::Terminal;

int main()
{
    if (Terminal::IsInputKey(VK_SHIFT) || !Terminal::IsInputKey(L'A'))
    {
        return 1;
    }

    Terminal terminal{ Terminal::TestDummyMarker{} };
    terminal.SetHighContrastMode(true);
    if (!terminal.GetWorkingDirectory().empty())
    {
        return 2;
    }

    return 0;
}
