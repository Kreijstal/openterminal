#pragma once

// Compiler and Windows-SDK compatibility needed by WIL and Terminal when the
// target is mingw-w64 rather than MSVC's Windows SDK.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _ITERATOR_DEBUG_LEVEL
#define _ITERATOR_DEBUG_LEVEL 0
#endif

#include <windows.h>
#include <inspectable.h>
#include <cfloat>
#include <memory_resource>
#include <mutex>

// Terminal supplies FILE_MODE_INFORMATION itself because the Microsoft SDK
// omits it. Keep mingw-w64's otherwise useful winternl declarations without
// colliding with that source-level definition.
#define _FILE_MODE_INFORMATION _OPENTERMINAL_MINGW_FILE_MODE_INFORMATION
#define FILE_MODE_INFORMATION OPENTERMINAL_MINGW_FILE_MODE_INFORMATION
#define PFILE_MODE_INFORMATION POPENTERMINAL_MINGW_FILE_MODE_INFORMATION
#include <winternl.h>
#undef PFILE_MODE_INFORMATION
#undef FILE_MODE_INFORMATION
#undef _FILE_MODE_INFORMATION

#include <intrin.h>

extern "C" HRESULT WINAPI OpenXamlLoadComponent(IInspectable* component,
                                                  HSTRING resourceUri);
extern "C" HRESULT WINAPI OpenXamlInitializeForCurrentThread(void** manager);
extern "C" HRESULT WINAPI OpenXamlGetApplicationFactory(void** factory);
extern "C" HRESULT WINAPI OpenXamlGetDispatcherQueue(void** queue);
extern "C" HRESULT WINAPI OpenXamlGetResourceManager(void** manager);
extern "C" HRESULT WINAPI OpenXamlGetResourceContext(void** context);
extern "C" HRESULT WINAPI OpenXamlCreateDesktopWindowXamlSource(void** source);
extern "C" HRESULT WINAPI OpenXamlGetActivationFactory(HSTRING classId,
                                                         REFIID iid,
                                                         void** factory);
void OpenTerminalInstallActivationHandler() noexcept;

namespace winrt::Windows::System
{
    struct DispatcherQueue;
}
winrt::Windows::System::DispatcherQueue OpenTerminalDispatcherQueue();
namespace winrt::Windows::ApplicationModel::Resources::Core
{
    struct ResourceManager;
    struct ResourceContext;
}
winrt::Windows::ApplicationModel::Resources::Core::ResourceManager
OpenTerminalResourceManager();
winrt::Windows::ApplicationModel::Resources::Core::ResourceContext
OpenTerminalResourceContext();
namespace winrt::Windows::UI::Xaml::Hosting
{
    struct DesktopWindowXamlSource;
}
winrt::Windows::UI::Xaml::Hosting::DesktopWindowXamlSource
OpenTerminalDesktopWindowXamlSource();

#ifndef E_ILLEGAL_STATE_CHANGE
#define E_ILLEGAL_STATE_CHANGE _HRESULT_TYPEDEF_(0x8000000DL)
#endif
#ifndef WEB_E_INVALID_JSON_STRING
#define WEB_E_INVALID_JSON_STRING _HRESULT_TYPEDEF_(0x83750007L)
#endif
#ifndef SIZE_T_MAX
#define SIZE_T_MAX SIZE_MAX
#endif
#ifndef DWRITE_MAKE_FONT_FEATURE_TAG
#define DWRITE_MAKE_FONT_FEATURE_TAG(a, b, c, d) \
    (static_cast<DWRITE_FONT_FEATURE_TAG>(DWRITE_MAKE_OPENTYPE_TAG(a, b, c, d)))
#endif
#ifndef NTSTATUS_FROM_WIN32
#define NTSTATUS_FROM_WIN32(x)                                                             \
    ((NTSTATUS)(x) <= 0 ? (NTSTATUS)(x) :                                                  \
                          (NTSTATUS)(((x) & 0x0000FFFF) | (FACILITY_WIN32 << 16) |          \
                                     ERROR_SEVERITY_ERROR))
#endif

// mingw-w64 defines this as `(P) = (P)`, which rejects const and move-only
// values. The Windows SDK spelling is observational only, so discard it.
#ifdef UNREFERENCED_PARAMETER
#undef UNREFERENCED_PARAMETER
#endif
#define UNREFERENCED_PARAMETER(P) static_cast<void>(P)

// Terminal carries selected public DDK declarations for MSVC SDK builds.
// mingw-w64's winternl.h already declares those types and APIs.
#ifndef _DDK_INCLUDED
#define _DDK_INCLUDED 1
#endif
#ifndef RTL_CONSTANT_STRING
#define RTL_CONSTANT_STRING(value)                                                     \
    {                                                                                  \
        sizeof(value) - sizeof((value)[0]), sizeof(value), const_cast<PWSTR>(value)    \
    }
#endif

// mingw's C++ configuration gives NOMINMAX the value 1. Terminal's common
// header defines it without a value, so leave it undefined after windows.h has
// consumed it and avoid a diagnostically noisy but harmless redefinition.
#undef NOMINMAX

#ifndef __clang__
#ifndef __nullptr
#define __nullptr nullptr
#endif
#ifndef __pragma
#define __pragma(...) /* MSVC compile/link diagnostic only */
#endif
#ifndef __is_convertible_to
#define __is_convertible_to(from, to) __is_convertible(from, to)
#endif
#ifndef __is_literal
#define __is_literal(type) __is_literal_type(type)
#endif
#ifndef __assume
#define __assume(condition)         \
    do                              \
    {                               \
        if (!(condition))           \
            __builtin_unreachable(); \
    } while (false)
#endif
#ifndef __except
// GCC has C++ exceptions but not Microsoft's SEH syntax. WIL uses this only
// as a last-chance guard around its ordinary C++ exception translator.
#define __except(...) catch (...)
#endif
#ifndef sealed
#define sealed final
#endif
#endif

#ifndef InterlockedIncrementNoFence
#define InterlockedIncrementNoFence InterlockedIncrement
#endif
#ifndef InterlockedDecrementNoFence
#define InterlockedDecrementNoFence InterlockedDecrement
#endif
#ifndef InterlockedExchangeNoFence
#define InterlockedExchangeNoFence InterlockedExchange
#endif
#ifndef _ReturnAddress
#define _ReturnAddress() __builtin_return_address(0)
#endif

inline LONG ReadAcquire(const volatile LONG* value) noexcept
{
    return InterlockedCompareExchange(const_cast<volatile LONG*>(value), 0, 0);
}

inline void WriteRelease(volatile LONG* destination, const LONG value) noexcept
{
    InterlockedExchange(destination, value);
}

// Recent Windows SDKs expose this optional foreground-priority API through a
// compatibility wrapper that reports S_FALSE on systems without support.
// mingw-w64 does not yet provide that declaration or import, so resolve it at
// runtime and retain the same unsupported-system contract.
inline HRESULT OpenTerminalTrySetWindowAssociatedProcesses(
    HWND window,
    DWORD processCount,
    HANDLE* processes) noexcept
{
    using Function = HRESULT (*)(HWND, DWORD, HANDLE*);
    static const auto module = LoadLibraryW(L"TerminalThemeHelpers.dll");
    static const auto function = reinterpret_cast<Function>(module ? GetProcAddress(
        module, "TerminalTrySetWindowAssociatedProcesses") : nullptr);
    return function ? function(window, processCount, processes) : S_FALSE;
}

#ifndef __clang__
inline void __iso_volatile_store16(volatile short* destination, const short value) noexcept
{
    *destination = value;
}
#endif

// MSVC makes a friend declaration visible to later class members. Standard
// lookup (and GCC) requires the namespace-scope declaration explicitly.
namespace Microsoft::Console::VirtualTerminal
{
    class SixelParser;
}
