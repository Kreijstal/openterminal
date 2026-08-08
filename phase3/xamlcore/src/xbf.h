// Portable reader for the XBF 2.0/2.1 format consumed by Windows.UI.Xaml.
//
// The format implementation follows the MIT-licensed WidgetSpinner reader in
// microsoft/microsoft-ui-xaml at the revision recorded in phase2/upstreams.json.
// It deliberately has no WinRT or Windows dependency so malformed input can be
// tested on the host before it ever reaches the activation DLL.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace openxaml::xbf {

class Error : public std::runtime_error {
public:
    Error(std::size_t offset, const std::string& message);
    std::size_t offset() const noexcept { return offset_; }

private:
    std::size_t offset_;
};

struct Reference {
    std::uint16_t id = 0;
    bool trusted = false;
};

struct Type {
    std::uint32_t flags = 0;
    std::int32_t namespace_id = -1;
    std::string name;
    std::string full_name;
};

struct Property {
    std::uint32_t flags = 0;
    std::int32_t declaring_type_id = -1;
    std::string name;
};

enum class ConstantKind : std::uint8_t {
    None = 0,
    False = 1,
    True = 2,
    Float = 3,
    Signed = 4,
    SharedString = 5,
    Thickness = 6,
    GridLength = 7,
    Color = 8,
    UniqueString = 9,
    NullString = 10,
    Enum = 11,
};

struct Constant {
    ConstantKind kind = ConstantKind::None;
    std::string string_value;
    std::int32_t signed_value = 0;
    std::uint32_t unsigned_value = 0;
    Reference enum_type;
    std::vector<float> floats;
};

enum class NodeType : std::uint8_t {
    PushScope = 1,
    PopScope = 2,
    AddNamespace = 3,
    PushConstant = 4,
    SetValue = 7,
    AddToCollection = 8,
    AddToDictionary = 9,
    AddToDictionaryWithKey = 10,
    CheckPeerType = 11,
    SetConnectionId = 12,
    SetName = 13,
    GetResourcePropertyBag = 14,
    SetCustomRuntimeData = 15,
    SetDeferredProperty = 17,
    PushScopeAddNamespace = 18,
    PushScopeGetValue = 19,
    PushScopeCreateTypeBeginInit = 20,
    PushScopeCreateTypeWithConstantBeginInit = 21,
    PushScopeCreateTypeWithTypeConvertedConstantBeginInit = 22,
    CreateTypeBeginInit = 23,
    CreateTypeWithConstantBeginInit = 24,
    CreateTypeWithTypeConvertedConstantBeginInit = 25,
    SetValueConstant = 26,
    SetValueTypeConvertedConstant = 27,
    SetValueTypeConvertedResolvedProperty = 28,
    SetValueTypeConvertedResolvedType = 29,
    SetValueFromStaticResource = 30,
    SetValueFromTemplateBinding = 31,
    SetValueFromMarkupExtension = 32,
    EndInitPopScope = 33,
    ProvideStaticResourceValue = 34,
    ProvideThemeResourceValue = 35,
    SetValueFromThemeResource = 36,
    BeginConditionalScope = 38,
    EndConditionalScope = 39,
    EndInitProvideValuePopScope = 40,
};

struct Node {
    NodeType type{};
    std::size_t offset = 0;
    Reference object;
    Reference second_object;
    Constant constant;
    std::string text;
    std::int32_t substream = -1;
    std::int32_t custom_data_version = 0;
    std::vector<std::string> static_resources;
    std::vector<std::string> theme_resources;
};

struct Substream {
    std::vector<Node> nodes;
    std::size_t node_bytes = 0;
    std::size_t line_bytes = 0;
};

struct Metadata {
    std::int32_t major = 0;
    std::int32_t minor = 0;
    std::vector<std::string> strings;
    std::vector<std::string> assemblies;
    std::vector<std::string> type_namespaces;
    std::vector<Type> types;
    std::vector<Property> properties;
    std::vector<std::string> xml_namespaces;
};

struct Document {
    std::uint32_t metadata_bytes = 0;
    std::uint32_t nodestream_bytes = 0;
    Metadata metadata;
    std::vector<Substream> substreams;

    std::string type_name(Reference reference) const;
    std::string property_name(Reference reference) const;
};

Document Read(const std::vector<std::uint8_t>& bytes);
Document ReadFile(const std::filesystem::path& path);

}  // namespace openxaml::xbf
