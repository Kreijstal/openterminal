#pragma once

// MSVC's __uuidof accepts WIL com_ptr expressions and resolves the held COM
// interface. mingw-w64 applies __uuidof to the wrapper type itself, so provide
// UUID declarations for the four wrapper instantiations used by Atlas.

#include <d2d1.h>
#include <d3d11shader.h>
#include <dwrite_2.h>
#include <dxgi1_2.h>
#include <wil/com.h>

using openterminal_d2d_factory_ptr =
    wil::com_ptr_t<ID2D1Factory, wil::err_exception_policy>;
using openterminal_dwrite_factory_ptr =
    wil::com_ptr_t<IDWriteFactory, wil::err_exception_policy>;
using openterminal_dwrite_factory2_ptr =
    wil::com_ptr_t<IDWriteFactory2, wil::err_exception_policy>;
using openterminal_dxgi_factory2_ptr =
    wil::com_ptr_t<IDXGIFactory2, wil::err_exception_policy>;

__CRT_UUID_DECL(openterminal_d2d_factory_ptr,
                0x06152247,
                0x6F50,
                0x465A,
                0x92,
                0x45,
                0x11,
                0x8B,
                0xFD,
                0x3B,
                0x60,
                0x07)
__CRT_UUID_DECL(openterminal_dwrite_factory_ptr,
                0xB859EE5A,
                0xD838,
                0x4B5B,
                0xA2,
                0xE8,
                0x1A,
                0xDC,
                0x7D,
                0x93,
                0xDB,
                0x48)
__CRT_UUID_DECL(openterminal_dwrite_factory2_ptr,
                0x0439FC60,
                0xCA44,
                0x4994,
                0x8D,
                0xEE,
                0x3A,
                0x9A,
                0xF7,
                0xB7,
                0x32,
                0xEC)
__CRT_UUID_DECL(openterminal_dxgi_factory2_ptr,
                0x50C83A1C,
                0xE072,
                0x4C48,
                0x87,
                0xB0,
                0x36,
                0x30,
                0xFA,
                0x36,
                0xA6,
                0xD0)

// d3d11shader.h declares the interface and IID, but older mingw-w64 releases
// omit the C++ __uuidof specialization used by IID_PPV_ARGS.
__CRT_UUID_DECL(ID3D11ShaderReflection,
                0x8D536CA1,
                0x0CCA,
                0x4956,
                0xA8,
                0x37,
                0x78,
                0x69,
                0x63,
                0x75,
                0x55,
                0x84)
