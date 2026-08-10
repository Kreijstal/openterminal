// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

// Upstream links WinRTUtils statically into each component DLL. Consequently
// every DLL has its own GetLibraryResourceLoader() and its own
// g_WinRTUtilsLibraryResourceScope. The MinGW port currently links those
// components into one executable; selectany would collapse all scopes to one
// arbitrary value. Bind the loader to the owning CMake target's explicit
// scope instead, preserving the same module-local behavior without guessing
// across ResourceMap subtrees.

#ifdef _DEBUG

#pragma detect_mismatch("winrt_utils_debug", "1")

#pragma section(".util$res$m", read)

namespace Microsoft::Console::Utils
{
    struct StaticResource
    {
        const wchar_t* resourceKey;
        const wchar_t* filename;
        unsigned int line;
    };
}

#define USES_RESOURCE(x) ([]() {                                        \
    static const ::Microsoft::Console::Utils::StaticResource res{       \
        (x), __FILEW__, __LINE__                                        \
    };                                                                  \
    __declspec(allocate(".util$res$m")) static const auto pRes{ &res }; \
    return pRes->resourceKey;                                           \
}())
#define RS_(x) GetLibraryResourceString(USES_RESOURCE(x))

#else

#pragma detect_mismatch("winrt_utils_debug", "0")

#define USES_RESOURCE(x) (x)
#define RS_(x) GetLibraryResourceString((x))

#endif

#define RS_A(x) (winrt::to_string(RS_(x)))

#ifdef OPENTERMINAL_LIBRARY_RESOURCE_SCOPE

// init.cpp still declares the upstream scope. It is compile-time state in the
// monolithic port, so no process-global data symbol is required.
#define UTILS_DEFINE_LIBRARY_RESOURCE_SCOPE(x) \
    static_assert(std::wstring_view{ x } ==                        \
                  std::wstring_view{ OPENTERMINAL_LIBRARY_RESOURCE_SCOPE });

winrt::hstring OpenTerminalGetResourceStringForScope(
    std::wstring_view scope, std::wstring_view key);
bool OpenTerminalHasResourceWithNameForScope(
    std::wstring_view scope, std::wstring_view key);
class ScopedResourceLoader;
const ScopedResourceLoader& OpenTerminalGetResourceLoaderForScope(
    std::wstring_view scope);

#define GetLibraryResourceLoader() \
    OpenTerminalGetResourceLoaderForScope( \
        OPENTERMINAL_LIBRARY_RESOURCE_SCOPE)

static inline winrt::hstring GetLibraryResourceString(
    const std::wstring_view key)
{
    return OpenTerminalGetResourceStringForScope(
        OPENTERMINAL_LIBRARY_RESOURCE_SCOPE, key);
}

static inline bool HasLibraryResourceWithName(const std::wstring_view key)
{
    return OpenTerminalHasResourceWithNameForScope(
        OPENTERMINAL_LIBRARY_RESOURCE_SCOPE, key);
}

#else

// WinRTUtils itself only supplies ScopedResourceLoader in this build. Keep the
// declarations available to its headers, but any component that performs a
// lookup must declare OPENTERMINAL_LIBRARY_RESOURCE_SCOPE explicitly.
class ScopedResourceLoader;
const ScopedResourceLoader& GetLibraryResourceLoader();
winrt::hstring GetLibraryResourceString(const std::wstring_view key);
bool HasLibraryResourceWithName(const std::wstring_view key);

#define UTILS_DEFINE_LIBRARY_RESOURCE_SCOPE(x) \
    static_assert(false, "resource-owning target has no declared scope");

#endif

#define RS_fmt(x, ...) RS_fmt_impl(USES_RESOURCE(x), __VA_ARGS__)

template<typename... Args>
std::wstring RS_fmt_impl(std::wstring_view key, Args&&... args)
{
    const auto format = GetLibraryResourceString(key);
    return fmt::format(fmt::runtime(std::wstring_view{ format }),
                       std::forward<Args>(args)...);
}
