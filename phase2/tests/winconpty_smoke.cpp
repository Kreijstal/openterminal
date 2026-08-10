#include <conpty-static.h>

int main()
{
    HPCON pseudoConsole = reinterpret_cast<HPCON>(1);
    if (ConptyCreatePseudoConsole({ 80, 24 }, nullptr, nullptr, 0, &pseudoConsole) !=
            E_INVALIDARG ||
        pseudoConsole != nullptr)
    {
        return 1;
    }
    if (ConptyResizePseudoConsole(nullptr, { 80, 24 }) != E_INVALIDARG)
    {
        return 2;
    }
    if (ConptyPackPseudoConsole(nullptr, nullptr, nullptr, &pseudoConsole) != E_INVALIDARG)
    {
        return 3;
    }
    return 0;
}
