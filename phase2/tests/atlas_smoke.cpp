#include "pch.h"
#include "BuiltinGlyphs.h"
#include "dwrite_helpers.h"

#include <algorithm>

int main()
{
    using namespace Microsoft::Console::Render::Atlas;

    if (!BuiltinGlyphs::IsBuiltinGlyph(U'\u2500') ||
        !BuiltinGlyphs::IsBuiltinGlyph(U'\ue0b0') ||
        BuiltinGlyphs::IsBuiltinGlyph(U'A'))
    {
        return 1;
    }
    if (BuiltinGlyphs::GetBitmapCellIndex(U'\u2500') != 0 ||
        BuiltinGlyphs::GetBitmapCellIndex(U'\ue0b0') !=
            static_cast<i32>(BuiltinGlyphs::BoxDrawing_CharCount) ||
        BuiltinGlyphs::GetBitmapCellIndex(U'A') != -1)
    {
        return 2;
    }

    float low[4];
    float minimum[4];
    float high[4];
    float maximum[4];
    DWrite_GetGammaRatios(0.0f, low);
    DWrite_GetGammaRatios(1.0f, minimum);
    DWrite_GetGammaRatios(10.0f, high);
    DWrite_GetGammaRatios(2.2f, maximum);
    if (!std::equal(std::begin(low), std::end(low), std::begin(minimum)) ||
        !std::equal(std::begin(high), std::end(high), std::begin(maximum)))
    {
        return 3;
    }
    if (!DWrite_IsThinFontFamily(L"Courier New") ||
        DWrite_IsThinFontFamily(L"Cascadia Mono"))
    {
        return 4;
    }

    return 0;
}
