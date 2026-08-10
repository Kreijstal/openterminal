#pragma once

// Public TSF declarations present in the Windows SDK but still absent from
// mingw-w64's msctf.h.

#include <msctf.h>

#ifndef TF_CLIENTID_NULL
#define TF_CLIENTID_NULL 0
#endif

#ifndef TF_INVALID_GUIDATOM
#define TF_INVALID_GUIDATOM 0
#endif

#ifndef __ITfContextOwner_INTERFACE_DEFINED__
#define __ITfContextOwner_INTERFACE_DEFINED__

DEFINE_GUID(IID_ITfContextOwner,
            0xAA80E80C,
            0x2021,
            0x11D2,
            0x93,
            0xE0,
            0x00,
            0x60,
            0xB0,
            0x67,
            0xB8,
            0x6E);

MIDL_INTERFACE("AA80E80C-2021-11D2-93E0-0060B067B86E")
ITfContextOwner : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE GetACPFromPoint(
        const POINT* screenPoint,
        DWORD flags,
        LONG* characterPosition) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetTextExt(
        LONG start,
        LONG end,
        RECT* rectangle,
        BOOL* clipped) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetScreenExt(RECT* rectangle) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetStatus(TF_STATUS* status) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetWnd(HWND* window) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetAttribute(
        REFGUID attribute,
        VARIANT* value) = 0;
};

#ifdef __MINGW32__
__CRT_UUID_DECL(ITfContextOwner,
                0xAA80E80C,
                0x2021,
                0x11D2,
                0x93,
                0xE0,
                0x00,
                0x60,
                0xB0,
                0x67,
                0xB8,
                0x6E)
#endif

#endif
