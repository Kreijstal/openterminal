// Deterministic application ResourceMap data distilled from pinned .resw files.

#ifndef OPENXAML_RESOURCE_CATALOG_H
#define OPENXAML_RESOURCE_CATALOG_H

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace openxaml::winrt {

class ResourceCatalog {
public:
    using Resources = std::map<std::string, std::string>;

    const std::string* Find(const std::string& scope,
                            const std::string& key) const noexcept;
    bool Has(const std::string& scope, const std::string& key) const noexcept {
        return Find(scope, key) != nullptr;
    }
    std::vector<std::pair<std::string, std::string>> Entries(
        const std::string& scope) const;
    std::size_t Size(const std::string& scope) const noexcept;

private:
    friend ResourceCatalog ParseResourceCatalog(const std::string&,
                                                 const std::string&);
    std::map<std::string, Resources> scopes_;
};

// Throws openxaml::JsonError for a malformed catalog. The label is included in
// diagnostics but is not retained, so machine-specific paths never enter
// deterministic state.
ResourceCatalog ParseResourceCatalog(const std::string& text,
                                     const std::string& label);
ResourceCatalog LoadResourceCatalog(const std::string& path);

}  // namespace openxaml::winrt

#endif  // OPENXAML_RESOURCE_CATALOG_H
