#include "precomp.h"
#include "FontBuffer.hpp"

using Microsoft::Console::VirtualTerminal::DispatchTypes::CharsetSize;
using Microsoft::Console::VirtualTerminal::DispatchTypes::DrcsCellMatrix;
using Microsoft::Console::VirtualTerminal::DispatchTypes::DrcsEraseControl;
using Microsoft::Console::VirtualTerminal::DispatchTypes::DrcsFontSet;
using Microsoft::Console::VirtualTerminal::DispatchTypes::DrcsFontUsage;
using Microsoft::Console::VirtualTerminal::FontBuffer;
using Microsoft::Console::VirtualTerminal::VTParameter;

int main()
{
    FontBuffer font;
    if (!font.SetEraseControl(DrcsEraseControl::AllChars))
    {
        return 1;
    }
    if (font.SetEraseControl(static_cast<DrcsEraseControl>(99)))
    {
        return 2;
    }
    if (!font.SetAttributes(DrcsCellMatrix::Size5x10,
                            VTParameter{},
                            DrcsFontSet::Size80x24,
                            DrcsFontUsage::Text))
    {
        return 3;
    }
    if (font.SetAttributes(DrcsCellMatrix::Size5x10,
                           VTParameter{},
                           DrcsFontSet::Size80x24,
                           DrcsFontUsage::FullCell))
    {
        return 4;
    }
    if (!font.SetStartChar(VTParameter{}, CharsetSize::Size94))
    {
        return 5;
    }

    return 0;
}
