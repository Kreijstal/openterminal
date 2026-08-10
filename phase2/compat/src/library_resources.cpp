// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"

#include <ScopedResourceLoader.h>

#include <map>
#include <memory>
#include <mutex>

const ScopedResourceLoader& OpenTerminalGetResourceLoaderForScope(
    const std::wstring_view scope)
{
    static std::mutex mutex;
    static std::map<std::wstring, std::unique_ptr<ScopedResourceLoader>> loaders;
    const std::lock_guard lock{ mutex };
    auto [entry, inserted] = loaders.try_emplace(std::wstring{ scope });
    if (inserted)
        entry->second = std::make_unique<ScopedResourceLoader>(scope);
    return *entry->second;
}

winrt::hstring OpenTerminalGetResourceStringForScope(
    const std::wstring_view scope, const std::wstring_view key)
{
    return OpenTerminalGetResourceLoaderForScope(scope).GetLocalizedString(key);
}

bool OpenTerminalHasResourceWithNameForScope(
    const std::wstring_view scope, const std::wstring_view key)
{
    return OpenTerminalGetResourceLoaderForScope(scope).HasResourceWithName(key);
}
