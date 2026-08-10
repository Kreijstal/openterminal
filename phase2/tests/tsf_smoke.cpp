#include <LibraryIncludes.h>
#include "Handle.h"

int main()
{
    Microsoft::Console::TSF::Handle first;
    if (first)
    {
        return 1;
    }

    Microsoft::Console::TSF::Handle second{ std::move(first) };
    return second ? 2 : 0;
}
