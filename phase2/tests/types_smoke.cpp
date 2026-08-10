#include "LibraryIncludes.h"
#include "inc/viewport.hpp"
#include <wrl/implements.h>

struct IOpenTerminalProbe : IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetValue(int* value) noexcept = 0;
};

__CRT_UUID_DECL(IOpenTerminalProbe, 0x8497ac76, 0x45b9, 0x4ceb, 0x90, 0x87, 0x8f, 0x6c, 0xb5, 0xbc, 0x50, 0x3c)

class OpenTerminalProbe final :
    public Microsoft::WRL::RuntimeClass<
        Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom | Microsoft::WRL::InhibitFtmBase>,
        IOpenTerminalProbe>
{
public:
    HRESULT RuntimeClassInitialize(const int value) noexcept
    {
        _value = value;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetValue(int* value) noexcept override
    {
        if (value == nullptr)
        {
            return E_POINTER;
        }
        *value = _value;
        return S_OK;
    }

private:
    int _value{};
};

using Microsoft::Console::Types::Viewport;

int main()
{
    const auto viewport = Viewport::FromDimensions({ 3, 5 }, { 80, 25 });
    if (viewport.Left() != 3 || viewport.Top() != 5 || viewport.Width() != 80 || viewport.Height() != 25)
    {
        return 1;
    }

    auto position = viewport.Origin();
    if (!viewport.WalkInBounds(position, 81) || position != til::point{ 4, 6 })
    {
        return 2;
    }

    IOpenTerminalProbe* probe = nullptr;
    if (FAILED(Microsoft::WRL::MakeAndInitialize<OpenTerminalProbe>(&probe, 42)))
    {
        return 3;
    }

    int value = 0;
    IUnknown* identity = nullptr;
    const auto queryStatus = probe->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&identity));
    const auto valueStatus = probe->GetValue(&value);
    if (FAILED(queryStatus) || FAILED(valueStatus) || value != 42)
    {
        if (identity != nullptr)
        {
            identity->Release();
        }
        probe->Release();
        return 4;
    }

    identity->Release();
    probe->Release();

    return 0;
}
