#pragma once

#include <windows.h>
#include <unknwn.h>

namespace openterminal::theme_helpers
{
    inline FARPROC resolve(const char* name) noexcept
    {
        static const auto module = LoadLibraryW(L"TerminalThemeHelpers.dll");
        return module ? GetProcAddress(module, name) : nullptr;
    }
}

inline HRESULT TerminalTrySetTransparentBackground(const bool enabled) noexcept
{
    using Function = HRESULT (*)(bool);
    const auto function = reinterpret_cast<Function>(
        openterminal::theme_helpers::resolve("TerminalTrySetTransparentBackground"));
    return function ? function(enabled) : S_FALSE;
}

inline HRESULT TerminalTrySetAutoCompleteAnimationsWhenOccluded(
    IUnknown* target,
    bool enabled) noexcept
{
    using Function = HRESULT (*)(IUnknown*, bool);
    const auto function = reinterpret_cast<Function>(openterminal::theme_helpers::resolve(
        "TerminalTrySetAutoCompleteAnimationsWhenOccluded"));
    return function ? function(target, enabled) : S_FALSE;
}
