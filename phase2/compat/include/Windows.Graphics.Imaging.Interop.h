#pragma once

// Public SoftwareBitmap/WIC interop declarations present in the Windows SDK
// but not yet shipped by mingw-w64.
#include <inspectable.h>
#include <mfobjects.h>
#include <wincodec.h>

inline constexpr CLSID CLSID_SoftwareBitmapNativeFactory{
    0x84e65691,
    0x8602,
    0x4a84,
    { 0xbe, 0x46, 0x70, 0x8b, 0xe9, 0xcd, 0x4b, 0x74 }
};

MIDL_INTERFACE("94BC8415-04EA-4B2E-AF13-4DE95AA898EB")
ISoftwareBitmapNative : public IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE GetData(REFIID interfaceId, void** object) = 0;
};

MIDL_INTERFACE("C3C181EC-2914-4791-AF02-02D224A10B43")
ISoftwareBitmapNativeFactory : public IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE CreateFromWICBitmap(
        IWICBitmap* data,
        BOOL forceReadOnly,
        REFIID interfaceId,
        void** object) = 0;

    virtual HRESULT STDMETHODCALLTYPE CreateFromMF2DBuffer2(
        IMF2DBuffer2* data,
        REFGUID subtype,
        UINT32 width,
        UINT32 height,
        BOOL forceReadOnly,
        const MFVideoArea* minimumDisplayAperture,
        REFIID interfaceId,
        void** object) = 0;
};

__CRT_UUID_DECL(ISoftwareBitmapNative,
                0x94bc8415, 0x04ea, 0x4b2e, 0xaf, 0x13, 0x4d, 0xe9, 0x5a, 0xa8, 0x98, 0xeb)
__CRT_UUID_DECL(ISoftwareBitmapNativeFactory,
                0xc3c181ec, 0x2914, 0x4791, 0xaf, 0x02, 0x02, 0xd2, 0x24, 0xa1, 0x0b, 0x43)
