#pragma once

// mingw-w64's open WinHTTP header uses Wine's __WINE_WINHTTP_H guard, while
// WIL enables its WinHTTP RAII wrappers when the Microsoft SDK's _WINHTTPX_
// guard is present. The declarations are equivalent for WIL's HINTERNET
// wrapper, so publish the expected feature guard after loading the real API.
#include <winhttp.h>

#ifndef _WINHTTPX_
#define _WINHTTPX_
#endif
