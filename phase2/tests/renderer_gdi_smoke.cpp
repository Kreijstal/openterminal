#include "precomp.h"
#include "gdirenderer.hpp"

int main()
{
    try
    {
        Microsoft::Console::Render::GdiEngine engine;
    }
    catch (...)
    {
        return 1;
    }
    return 0;
}
