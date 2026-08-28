// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"

#include "ScopedResourceLoader.h"

using namespace ::winrt::Windows::ApplicationModel::Resources::Core;

ScopedResourceLoader::ScopedResourceLoader(const std::wstring_view resourceLocatorBase) :
    _resourceMap{ OpenTerminalResourceManager().MainResourceMap().GetSubtree(resourceLocatorBase) },
    _resourceContext{ OpenTerminalResourceContext() }
{
}

ResourceMap ScopedResourceLoader::GetResourceMap() const noexcept
{
    return _resourceMap;
}

winrt::hstring ScopedResourceLoader::GetLocalizedString(const std::wstring_view resourceName) const
{
    return _resourceMap.GetValue(resourceName, _resourceContext).ValueAsString();
}

bool ScopedResourceLoader::HasResourceWithName(const std::wstring_view resourceName) const
{
    return _resourceMap.HasKey(resourceName);
}

ScopedResourceLoader::ScopedResourceLoader(
    winrt::Windows::ApplicationModel::Resources::Core::ResourceMap map,
    winrt::Windows::ApplicationModel::Resources::Core::ResourceContext context) noexcept :
    _resourceMap{ std::move(map) }, _resourceContext{ std::move(context) }
{
}

ScopedResourceLoader ScopedResourceLoader::WithQualifier(const wil::zwstring_view qualifierName,
                                                         const wil::zwstring_view qualifierValue) const
{
    auto newContext = _resourceContext.Clone();
    auto qualifierValues = newContext.QualifierValues();
    qualifierValues.Insert(qualifierName, qualifierValue);
    return ScopedResourceLoader{ _resourceMap, std::move(newContext) };
}
