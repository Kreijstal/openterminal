#include <roapi.h>
#include <windows.h>

#include <cstdio>

#include <inspectable.h>
#include <winstring.h>

inline constexpr GUID IID_IMuxcBitmapIconSource = {
    0xa6b6cccc, 0xea8f, 0x53ca,
    {0x83, 0x1f, 0x2a, 0xbe, 0x85, 0xcd, 0x6d, 0x8c}};
inline constexpr GUID IID_IMuxcBitmapIconSourceFactory = {
    0x7d484c14, 0xf5f6, 0x5e39,
    {0xb4, 0xe4, 0xb6, 0x10, 0x8d, 0x2e, 0xe0, 0x95}};

struct IMuxcBitmapIconSource : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_UriSource(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_UriSource(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ShowAsMonochrome(boolean*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_ShowAsMonochrome(boolean) = 0;
};
struct IMuxcBitmapIconSourceFactory : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE CreateInstance(
        IInspectable*, IInspectable**, IMuxcBitmapIconSource**) = 0;
};

int main() {
    const HRESULT initialized = RoInitialize(RO_INIT_SINGLETHREADED);
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) return 1;
    HSTRING_HEADER header{};
    HSTRING name = nullptr;
    static constexpr wchar_t class_name[] =
        L"Microsoft.UI.Xaml.Controls.BitmapIconSource";
    if (FAILED(WindowsCreateStringReference(class_name,
                                             ARRAYSIZE(class_name) - 1,
                                             &header, &name)))
        return 2;

    IMuxcBitmapIconSourceFactory* factory = nullptr;
    const HRESULT activation = RoGetActivationFactory(
        name, IID_IMuxcBitmapIconSourceFactory,
        reinterpret_cast<void**>(&factory));
    if (FAILED(activation) || !factory) {
        std::fprintf(stderr, "factory activation failed: 0x%08lx\n",
                     static_cast<unsigned long>(activation));
        return 3;
    }

    IInspectable* inner = nullptr;
    IMuxcBitmapIconSource* value = nullptr;
    const HRESULT created = factory->CreateInstance(nullptr, &inner, &value);
    if (FAILED(created) || !inner || !value) return 4;
    IMuxcBitmapIconSource* queried = nullptr;
    if (FAILED(inner->QueryInterface(IID_IMuxcBitmapIconSource,
                                     reinterpret_cast<void**>(&queried))) ||
        !queried)
        return 5;

    queried->Release();
    value->Release();
    inner->Release();
    factory->Release();
    if (SUCCEEDED(initialized)) RoUninitialize();
    std::puts("MUX BitmapIconSource factory checks passed");
    return 0;
}
