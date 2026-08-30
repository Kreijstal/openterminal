#include "resource_catalog.h"

#include <fstream>
#include <sstream>

#include "json.h"

namespace openxaml::winrt {

const std::string* ResourceCatalog::Find(const std::string& scope,
                                         const std::string& key) const noexcept {
    const auto scoped = scopes_.find(scope);
    if (scoped == scopes_.end()) return nullptr;
    const auto resource = scoped->second.find(key);
    if (resource != scoped->second.end()) return &resource->second;

    // MRT addresses a property-style .resw name with URI path separators:
    // `Control.Text` is requested as `Control/Text`, and attached-property
    // paths may contain several separators. Preserve an exact literal match
    // above, then compare the canonical spelling without allocating inside
    // this noexcept lookup boundary.
    if (key.find('/') == std::string::npos) return nullptr;
    for (const auto& [stored_key, value] : scoped->second) {
        if (stored_key.size() != key.size()) continue;
        bool matches = true;
        for (std::size_t index = 0; index < key.size(); ++index) {
            const char canonical = key[index] == '/' ? '.' : key[index];
            if (stored_key[index] != canonical) {
                matches = false;
                break;
            }
        }
        if (matches) return &value;
    }
    return nullptr;
}

const std::string* ResourceCatalog::FindAny(const std::string& key) const noexcept {
    for (const auto& [scope, _] : scopes_) {
        if (const std::string* value = Find(scope, key)) return value;
    }
    return nullptr;
}

std::vector<std::pair<std::string, std::string>> ResourceCatalog::Entries(
    const std::string& scope) const {
    const auto scoped = scopes_.find(scope);
    if (scoped == scopes_.end()) return {};
    return {scoped->second.begin(), scoped->second.end()};
}

std::size_t ResourceCatalog::Size(const std::string& scope) const noexcept {
    const auto scoped = scopes_.find(scope);
    return scoped == scopes_.end() ? 0 : scoped->second.size();
}

ResourceCatalog ParseResourceCatalog(const std::string& text,
                                     const std::string& label) {
    const JsonValue document = ParseJson(text);
    if (document.kind != JsonValue::Kind::Object)
        throw JsonError(label + ": root is not an object");

    const JsonValue& schema = document.At("schema_version");
    if (schema.kind != JsonValue::Kind::Number || schema.number != 2.0)
        throw JsonError(label + ": unsupported schema_version");

    const JsonValue& maps = document.At("runtime_resources");
    if (maps.kind != JsonValue::Kind::Object)
        throw JsonError(label + ": runtime_resources is not an object");

    ResourceCatalog result;
    for (const auto& [scope, resources] : maps.object) {
        if (scope.empty()) throw JsonError(label + ": an empty resource scope");
        if (resources.kind != JsonValue::Kind::Object)
            throw JsonError(label + ": scope '" + scope + "' is not an object");
        auto& destination = result.scopes_[scope];
        for (const auto& [key, value] : resources.object) {
            if (key.empty())
                throw JsonError(label + ": scope '" + scope + "' has an empty key");
            if (value.kind != JsonValue::Kind::String)
                throw JsonError(label + ": " + scope + "/" + key +
                                " is not a string");
            destination.emplace(key, value.string);
        }
    }
    return result;
}

ResourceCatalog LoadResourceCatalog(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw JsonError("cannot read resource catalog " + path);
    std::ostringstream text;
    text << input.rdbuf();
    return ParseResourceCatalog(text.str(), path);
}

}  // namespace openxaml::winrt
