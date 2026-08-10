#pragma once

// The mingw-w64 headers do not currently expose the public desktop interop
// interfaces for SwapChainPanel. Windows Terminal only consumes these two
// interfaces from the SDK header.

#include <dxgi.h>
#include <unknwn.h>

MIDL_INTERFACE("F92F19D2-3ADE-45A6-A20C-F6F1EA90554B")
ISwapChainPanelNative : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE SetSwapChain(IDXGISwapChain* swapChain) = 0;
};

MIDL_INTERFACE("D5A2F60C-37B2-44A2-937B-8D8EB9726821")
ISwapChainPanelNative2 : public ISwapChainPanelNative
{
public:
    virtual HRESULT STDMETHODCALLTYPE SetSwapChainHandle(HANDLE swapChainHandle) = 0;
};

#ifdef __MINGW32__
__CRT_UUID_DECL(ISwapChainPanelNative,
                0xF92F19D2,
                0x3ADE,
                0x45A6,
                0xA2,
                0x0C,
                0xF6,
                0xF1,
                0xEA,
                0x90,
                0x55,
                0x4B)
__CRT_UUID_DECL(ISwapChainPanelNative2,
                0xD5A2F60C,
                0x37B2,
                0x44A2,
                0x93,
                0x7B,
                0x8D,
                0x8E,
                0xB9,
                0x72,
                0x68,
                0x21)
#endif
