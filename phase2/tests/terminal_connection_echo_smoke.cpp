#include "pch.h"
#include "CTerminalHandoff.h"
#include "EchoConnection.h"

int32_t __stdcall WINRT_GetActivationFactory(void* classId, void** factory) noexcept;

namespace
{
    bool handoffCalled = false;

    HRESULT receiveHandoff(HANDLE* input,
                           HANDLE* output,
                           HANDLE signal,
                           HANDLE reference,
                           HANDLE server,
                           HANDLE client,
                           const TERMINAL_STARTUP_INFO* startupInfo)
    {
        handoffCalled = input != nullptr && output != nullptr && signal == nullptr &&
                        reference == nullptr && server == nullptr && client == nullptr &&
                        startupInfo == nullptr;
        return handoffCalled ? S_OK : E_INVALIDARG;
    }
}

int main()
{
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    {
        const auto className = winrt::hstring{
            L"Microsoft.Terminal.TerminalConnection.EchoConnection" };
        void* factory{};
        if (WINRT_GetActivationFactory(winrt::get_abi(className), &factory) != S_OK ||
            factory == nullptr)
        {
            return 1;
        }
        static_cast<IActivationFactory*>(factory)->Release();
    }

    CTerminalHandoff::s_setCallback(receiveHandoff);
    const auto handoff = Microsoft::WRL::Make<CTerminalHandoff>();
    HANDLE inputHandle{};
    HANDLE outputHandle{};
    if (!handoff ||
        handoff->EstablishPtyHandoff(
            &inputHandle, &outputHandle, nullptr, nullptr, nullptr, nullptr, nullptr) != S_OK ||
        !handoffCalled)
    {
        return 2;
    }

    const auto echo = winrt::make_self<
        winrt::Microsoft::Terminal::TerminalConnection::implementation::EchoConnection>();

    if (echo->State() !=
        winrt::Microsoft::Terminal::TerminalConnection::ConnectionState::Connected)
    {
        return 3;
    }

    std::u16string received;
    const auto token = echo->TerminalOutput(
        [&](const winrt::array_view<const char16_t> output) {
            received.assign(output.begin(), output.end());
        });
    const std::u16string input{ u"A\n\x7f" };
    echo->WriteInput({ input.data(), input.data() + input.size() });
    echo->TerminalOutput(token);

    return received == u"A^J0x7f" ? 0 : 4;
}
