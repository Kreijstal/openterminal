#include <cstdint>

#include "CSSLengthPercentage.h"

int main()
{
    if (CSSLengthPercentage::FromString(L"50%").Resolve(-1.0f, 96.0f, 20.0f, 8.0f) != 10.0f)
    {
        return 1;
    }
    if (CSSLengthPercentage::FromString(L"2px").Resolve(-1.0f, 96.0f, 20.0f, 8.0f) != 2.0f)
    {
        return 2;
    }
    if (CSSLengthPercentage::FromString(L"3pt").Resolve(-1.0f, 96.0f, 20.0f, 8.0f) != 4.0f)
    {
        return 3;
    }
    if (CSSLengthPercentage::FromString(L"1.5ch").Resolve(-1.0f, 96.0f, 20.0f, 8.0f) != 12.0f)
    {
        return 4;
    }
    if (CSSLengthPercentage::FromString(L"invalid").Resolve(-1.0f, 96.0f, 20.0f, 8.0f) != -1.0f)
    {
        return 5;
    }
    return 0;
}
