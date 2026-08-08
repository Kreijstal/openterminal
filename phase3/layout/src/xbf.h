// XBF2 -- the binary form the SDK's XAML compiler produces and the runtime
// actually loads.
//
// Windows Terminal ships its pages as .xbf; the text .xaml is a build input, not
// a runtime one. So a runtime that only reads text reads nothing the real
// application feeds it. This is the format layer: it turns the bytes into the
// node stream the format is written in -- metadata tables plus a flat list of
// object-writer instructions -- and knows nothing about elements or layout.
// xbf_markup.h is what turns that node stream into a tree.
//
// The format is not documented. Two sources pin it, in this order:
//
//   * microsoft/microsoft-ui-xaml at 188f602b, MIT, which publishes the reader
//     and writer this is written against: dxaml/xcp/core/Parser/
//     XamlBinaryFormatWriter2.cpp, XamlBinaryFormatSubWriter2.cpp,
//     xamlbinaryformatsubreader2.cpp, XamlBinaryMetadataReader2.cpp and
//     dxaml/xcp/core/inc/XamlBinaryMetadata.h.
//   * the actual output of genxbf.dll from Windows SDK 10.0.26100.0, which is
//     what phase 2 runs under Wine. Every structure size and field order below
//     was checked against real output before it was written down -- the
//     equivalence gate (phase4/scripts/xbf_equivalence.py) is what keeps it
//     checked.
//
// Only version 2.1 is implemented, because that is the only version genxbf
// 10.0.26100.0 emits for WinUI 2.8.4. Any other version is refused by number
// rather than guessed at.

#ifndef OPENXAML_XBF_H
#define OPENXAML_XBF_H

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace openxaml {
namespace xbf {

class XbfError : public std::runtime_error {
public:
    explicit XbfError(const std::string& what) : std::runtime_error(what) {}
};

// ObjectWriterNodeType, dxaml/xcp/core/inc/ObjectWriterNodeType.h. Persisted as
// one byte; the comment on the enum there reads "These values are written into
// the XBF so MUST REMAIN STATIC".
enum class NodeType : std::uint8_t {
    None = 0,
    PushScope = 1,
    PopScope = 2,
    AddNamespace = 3,
    PushConstant = 4,
    PushResolvedType = 5,
    PushResolvedProperty = 6,
    SetValue = 7,
    AddToCollection = 8,
    AddToDictionary = 9,
    AddToDictionaryWithKey = 10,
    CheckPeerType = 11,
    SetConnectionId = 12,
    SetName = 13,
    GetResourcePropertyBag = 14,
    SetCustomRuntimeData = 15,
    SetResourceDictionaryItems = 16,
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
    EndOfStream = 37,
    BeginConditionalScope = 38,
    EndConditionalScope = 39,
    EndInitProvideValuePopScope = 40,
};

// PersistedConstantType, XamlBinaryMetadata.h. Also one byte.
enum class ConstantType : std::uint8_t {
    None = 0,
    BoolFalse = 1,
    BoolTrue = 2,
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

// PersistedXamlNode2: a 16-bit field whose top bit says whether the index below
// it is a *stable* index -- a number the platform promises never to reuse, so
// no metadata table entry is needed -- or an index into this file's own table.
struct Reference {
    std::uint16_t index = 0;
    bool trusted = false;
};

// One decoded constant. Which member is meaningful is decided by `type`; the
// rest keep their defaults so a mistaken read is a zero rather than garbage.
struct Constant {
    ConstantType type = ConstantType::None;
    // Float, and the value half of GridLength.
    float number = 0.0f;
    std::int32_t signed_value = 0;
    std::uint32_t color = 0;
    // SharedString resolves through the string table; UniqueString is written
    // in place. Both land here, in UTF-8.
    std::string text;
    // Thickness, in the order the runtime stores it.
    float left = 0.0f, top = 0.0f, right = 0.0f, bottom = 0.0f;
    // GridLength. 0 = Auto, 1 = Pixel, 2 = Star (DirectUI::GridUnitType).
    std::uint8_t grid_unit = 0;
    // Enum: the stable type index of the enumeration, and the value in it.
    std::uint16_t enum_type = 0;
    std::uint32_t enum_value = 0;
};

// One setter out of a deferred style's runtime data.
//
// A <Style> is not written into the node stream as a tree of <Setter> elements:
// the style custom writer boils each setter down to a property and a value and
// puts *that* in the blob, so the runtime can apply a style without building
// any objects. Reading it back is how the setters are recovered.
struct StyleSetter {
    // Which property the setter targets. Either resolved to a property
    // reference, or left as a name plus the type it was written on.
    bool property_resolved = false;
    Reference property;
    std::string property_name;
    Reference property_owner;

    enum class ValueKind {
        None,
        // A constant, spelled in the blob.
        Container,
        // A string, through the string table.
        String,
        // An offset into the deferred sub-stream where the value is built.
        StaticResource,
        ThemeResource,
        Object,
        Self,
    };
    ValueKind value_kind = ValueKind::None;
    Constant value;
    std::string text;
    std::uint32_t token = 0;
};

// One instruction. Every field the instruction does not carry keeps its
// default, and `has_*` says which ones it did carry -- a reader that forgets to
// check gets a zero index rather than a stale one from the previous node.
struct Node {
    NodeType type = NodeType::None;
    // Offset of the node within its sub-stream. Carried because the line-number
    // stream is indexed by it and because an error message that can say where
    // in the stream it stopped is worth far more than one that cannot.
    std::uint32_t offset = 0;

    Reference type_ref;
    bool has_type = false;

    Reference property;
    bool has_property = false;

    // The second property of a pair: SetValueFromTemplateBinding's source, and
    // SetValueTypeConvertedResolvedProperty's resolved value.
    Reference property_proxy;
    bool has_property_proxy = false;

    Constant constant;
    bool has_constant = false;

    // AddNamespace/PushScopeAddNamespace: which xml namespace, under what
    // prefix ("" for the default xmlns).
    Reference xml_namespace;
    std::string prefix;

    // CheckPeerType's type name, written in place rather than through the
    // string table.
    std::string peer_type;

    // SetDeferredProperty and SetCustomRuntimeData: the sub-stream holding the
    // deferred content, and the resource keys the compiler pre-resolved for it.
    std::uint32_t sub_stream = 0;
    bool has_sub_stream = false;
    std::vector<std::string> static_resources;
    std::vector<std::string> theme_resources;

    // SetCustomRuntimeData only. Which custom writer produced the blob, and --
    // when it is one this reader decodes -- what the blob said. The blob is
    // versioned separately from the file: CustomWriterRuntimeDataTypeIndex,
    // dxaml/xcp/components/deferral/inc/CustomWriterRuntimeDataTypeIndex.h.
    std::uint32_t custom_data_kind = 0;
    // A deferred resource dictionary's entries: the key, and the offset in the
    // target sub-stream where the node that builds its value begins.
    std::vector<std::pair<std::string, std::uint32_t>> deferred_resources;
    // A deferred style's setters.
    std::vector<StyleSetter> style_setters;
};

// CustomWriterRuntimeDataTypeIndex, for the writers this reader can name. The
// three legacy values are stable *type* indexes, which is why they are large
// and out of sequence.
enum class CustomDataKind : std::uint32_t {
    Unknown = 0,
    VisualStateGroupCollection_v2 = 1,
    Style_v1 = 2,
    VisualStateGroupCollection_v3 = 3,
    VisualStateGroupCollection_v4 = 4,
    VisualStateGroupCollection_v5 = 5,
    DeferredElement_v2 = 6,
    ResourceDictionary_v2 = 7,
    Style_v2 = 8,
    DeferredElement_v3 = 9,
    ResourceDictionary_v3 = 10,
    Style_v3 = 11,
    ResourceDictionary_v4 = 12,
    ResourceDictionary_v1 = 371,
    VisualStateGroupCollection_v1 = 420,
    DeferredElement_v1 = 745,
};

std::string CustomDataKindName(std::uint32_t kind);

struct Assembly {
    std::uint32_t provider_kind = 0;
    std::uint32_t name = 0;
};

struct TypeNamespace {
    std::uint32_t assembly = 0;
    std::uint32_t name = 0;
};

struct Type {
    std::uint32_t flags = 0;
    // For an unknown type this indexes the xml namespace table instead.
    std::uint32_t type_namespace = 0;
    std::uint32_t name = 0;

    // PersistedXamlType::PersistedXamlTypeFlags.
    bool markup_directive() const { return (flags & 0x01) != 0; }
    bool unknown() const { return (flags & 0x02) != 0; }
};

struct Property {
    std::uint32_t flags = 0;
    std::uint32_t type = 0;
    std::uint32_t name = 0;

    // PersistedXamlProperty::PersistedXamlPropertyFlags.
    bool xml_property() const { return (flags & 0x01) != 0; }
    bool markup_directive() const { return (flags & 0x02) != 0; }
    bool implicit() const { return (flags & 0x04) != 0; }
    bool custom_dependency_property() const { return (flags & 0x08) != 0; }
    bool unknown() const { return (flags & 0x10) != 0; }
};

// One decoded sub-stream. A document is a list of them: the first is the page
// itself, the rest hold deferred content -- a template body, a resource
// dictionary's values -- that the runtime only realises when something asks
// for it.
struct SubStream {
    std::vector<Node> nodes;
    // Set when decoding stopped before the end of the stream because it reached
    // something this reader does not implement. The nodes above it are still
    // good; what follows them is not decoded, and saying so is the point.
    std::string truncated_because;
    std::uint32_t node_bytes = 0;
    std::uint32_t line_bytes = 0;
};

struct Document {
    std::uint32_t major_version = 0;
    std::uint32_t minor_version = 0;
    // The 64 bytes the compiler stamps in as the source hash. genxbf writes it
    // as ASCII hex; it is carried verbatim rather than interpreted.
    std::string hash;

    std::vector<std::string> strings;  // UTF-8
    std::vector<Assembly> assemblies;
    std::vector<TypeNamespace> type_namespaces;
    std::vector<Type> types;
    std::vector<Property> properties;
    std::vector<std::uint32_t> xml_namespaces;  // string table indices

    std::vector<SubStream> sub_streams;

    const std::string& String(std::uint32_t index) const;
};

// The only version this reader implements. Anything else is refused by number.
constexpr std::uint32_t kMajorVersion = 2;
constexpr std::uint32_t kMinorVersion = 1;

// Throws XbfError for anything that is not a well-formed XBF 2.1 document.
Document Read(const std::string& bytes);

// The instruction's name, for error messages and for the tests. Unknown values
// are rendered as a number rather than as a guess.
std::string NodeTypeName(NodeType type);

}  // namespace xbf
}  // namespace openxaml

#endif  // OPENXAML_XBF_H
