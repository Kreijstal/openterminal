#include <windows.h>

#include <string>

#include "base64.hpp"

using Microsoft::Console::VirtualTerminal::Base64;

int main()
{
    std::wstring decoded;
    if (FAILED(Base64::Decode(L"SGVsbG8sIFRlcm1pbmFsIQ==", decoded)))
    {
        return 1;
    }
    if (decoded != L"Hello, Terminal!")
    {
        return 2;
    }
    if (SUCCEEDED(Base64::Decode(L"not base64", decoded)))
    {
        return 3;
    }
    return 0;
}
