// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"

#include <ScopedResourceLoader.h>

#include <algorithm>
#include <map>
#include <fstream>
#include <memory>
#include <mutex>
#include <filesystem>

#include <json/json.h>

namespace
{
const Json::Value& resourceCatalog()
{
    static const Json::Value catalog = [] {
        wchar_t executable[32768]{};
        const auto length = GetModuleFileNameW(nullptr, executable,
                                               static_cast<DWORD>(std::size(executable)));
        if (!length || length >= std::size(executable))
            winrt::throw_last_error();
        const auto path = std::filesystem::path{ executable }.parent_path() /
                          L"OpenXaml" / L"resources.json";
        std::ifstream stream{ path, std::ios::binary };
        if (!stream)
            winrt::throw_hresult(HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND));
        Json::Value document;
        Json::CharReaderBuilder builder;
        std::string errors;
        if (!Json::parseFromStream(builder, stream, &document, &errors))
            winrt::throw_hresult(HRESULT_FROM_WIN32(ERROR_BAD_FORMAT));
        return document["runtime_resources"];
    }();
    return catalog;
}

const Json::Value* findResource(const std::wstring_view scope,
                                const std::wstring_view key)
{
    const auto scopeName = winrt::to_string(scope);
    auto keyName = winrt::to_string(key);
    const auto& resources = resourceCatalog()[scopeName];
    if (!resources.isObject()) return nullptr;
    if (resources.isMember(keyName)) return &resources[keyName];
    std::replace(keyName.begin(), keyName.end(), '/', '.');
    return resources.isMember(keyName) ? &resources[keyName] : nullptr;
}
}

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
    const auto* value = findResource(scope, key);
    if (!value || !value->isString())
        winrt::throw_hresult(HRESULT_FROM_WIN32(ERROR_MRM_NAMED_RESOURCE_NOT_FOUND));
    return winrt::to_hstring(value->asString());
}

bool OpenTerminalHasResourceWithNameForScope(
    const std::wstring_view scope, const std::wstring_view key)
{
    const auto* value = findResource(scope, key);
    return value && value->isString();
}
