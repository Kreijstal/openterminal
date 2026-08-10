#include "pch.h"
#include "Utils.h"
#include "WtExeUtils.h"

namespace
{
    [[maybe_unused]] auto volatile image_picker_entrypoint = &OpenImagePicker;
}

int main()
{
    if (QuoteAndEscapeCommandlineArg(L"plain") != L"\"plain\"")
    {
        return 1;
    }
    if (QuoteAndEscapeCommandlineArg(L"a;b") != L"\"a\\;b\"")
    {
        return 2;
    }
    if (QuoteAndEscapeCommandlineArg(L"a\"b") != L"\"a\\\"b\"")
    {
        return 3;
    }
    if (QuoteAndEscapeCommandlineArg(L"tail\\") != L"\"tail\\\\\"")
    {
        return 4;
    }

    return image_picker_entrypoint == nullptr ? 5 : 0;
}
