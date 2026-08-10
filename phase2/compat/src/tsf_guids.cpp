#include <windows.h>
#include <msctf.h>

// mingw-w64 declares this public TSF property key but does not provide it in
// libuuid.a. Keep the Windows SDK value in the open compatibility archive.
extern "C" const GUID GUID_PROP_COMPOSING = {
    0xe12ac060,
    0xaf15,
    0x11d2,
    { 0xaf, 0xc5, 0x00, 0x10, 0x5a, 0x27, 0x99, 0xb5 }
};
