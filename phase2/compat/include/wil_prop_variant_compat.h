#pragma once

#include <propidl.h>
#include <propvarutil.h>
#include <wil/resource.h>

#if defined(__MINGW32__) && !defined(_MSC_EXTENSIONS)
namespace wil
{
    using unique_prop_variant = wil::unique_struct<
        PROPVARIANT,
        decltype(&::PropVariantClear),
        ::PropVariantClear,
        decltype(&::PropVariantInit),
        ::PropVariantInit>;
}
#endif
