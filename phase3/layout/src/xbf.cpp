#include "xbf.h"

#include <cstring>

namespace openxaml {
namespace xbf {
namespace {

// The metadata stream begins after the three words the container opens with,
// and the table offsets in the header are positions within it. Keeping the two
// coordinate systems apart is the whole reason this constant is named.
constexpr std::size_t kContainerPrologue = 12;  // magic, metadata size, node size
constexpr std::size_t kHashSize = 64;           // Parser::c_xbfHashSize
// XamlBinaryFileHeader2: six 64-bit table offsets and the hash. The version
// pair sits in front of it, inside the metadata stream.
constexpr std::size_t kVersionSize = 8;
constexpr std::size_t kHeaderSize = 6 * 8 + kHashSize;

class Cursor {
public:
    Cursor(const char* data, std::size_t size, std::string where)
        : data_(data), size_(size), where_(std::move(where)) {}

    std::size_t position() const { return position_; }
    std::size_t size() const { return size_; }
    bool exhausted() const { return position_ >= size_; }

    void Require(std::size_t count, const char* what) const {
        if (position_ + count > size_) {
            throw XbfError(where_ + ": " + what + " runs past the end of the stream (" +
                           std::to_string(position_) + " + " + std::to_string(count) + " > " +
                           std::to_string(size_) + ")");
        }
    }

    std::uint8_t U8(const char* what) {
        Require(1, what);
        return static_cast<std::uint8_t>(data_[position_++]);
    }

    std::uint16_t U16(const char* what) {
        Require(2, what);
        std::uint16_t value = 0;
        std::memcpy(&value, data_ + position_, 2);
        position_ += 2;
        return Little(value);
    }

    std::uint32_t U32(const char* what) {
        Require(4, what);
        std::uint32_t value = 0;
        std::memcpy(&value, data_ + position_, 4);
        position_ += 4;
        return Little(value);
    }

    std::int32_t I32(const char* what) {
        const std::uint32_t raw = U32(what);
        std::int32_t value = 0;
        std::memcpy(&value, &raw, 4);
        return value;
    }

    std::uint64_t U64(const char* what) {
        const std::uint64_t low = U32(what);
        const std::uint64_t high = U32(what);
        return low | (high << 32);
    }

    float F32(const char* what) {
        const std::uint32_t raw = U32(what);
        float value = 0.0f;
        std::memcpy(&value, &raw, 4);
        return value;
    }

    // The 7-bit encoding the format uses for stream indices and counts: seven
    // payload bits per byte, high bit means another byte follows.
    std::uint32_t Packed(const char* what) {
        std::uint32_t value = 0;
        for (int shift = 0; shift <= 28; shift += 7) {
            const std::uint8_t byte = U8(what);
            value |= static_cast<std::uint32_t>(byte & 0x7f) << shift;
            if ((byte & 0x80) == 0) return value;
        }
        throw XbfError(where_ + ": " + what + " is not a 7-bit encoded integer");
    }

    void Skip(std::size_t count, const char* what) {
        Require(count, what);
        position_ += count;
    }

    std::string Bytes(std::size_t count, const char* what) {
        Require(count, what);
        std::string value(data_ + position_, count);
        position_ += count;
        return value;
    }

    // A counted UTF-16 string. `terminated` is the metadata-stream form, which
    // since 2.1 writes a null after the characters so the runtime can hand the
    // buffer out without copying it.
    std::string Utf16(bool terminated, const char* what) {
        const std::uint32_t length = U32(what);
        Require(std::size_t{2} * length, what);
        std::string out;
        out.reserve(length);
        for (std::uint32_t i = 0; i < length; ++i) {
            std::uint32_t unit = U16(what);
            if (unit >= 0xd800 && unit <= 0xdbff && i + 1 < length) {
                const std::size_t mark = position_;
                const std::uint32_t low = U16(what);
                if (low >= 0xdc00 && low <= 0xdfff) {
                    unit = 0x10000 + ((unit - 0xd800) << 10) + (low - 0xdc00);
                    ++i;
                } else {
                    position_ = mark;
                }
            }
            AppendUtf8(out, unit);
        }
        if (terminated) Skip(2, what);
        return out;
    }

    void Seek(std::size_t position) { position_ = position; }

private:
    static void AppendUtf8(std::string& out, std::uint32_t code) {
        if (code < 0x80) {
            out.push_back(static_cast<char>(code));
        } else if (code < 0x800) {
            out.push_back(static_cast<char>(0xc0 | (code >> 6)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3f)));
        } else if (code < 0x10000) {
            out.push_back(static_cast<char>(0xe0 | (code >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3f)));
        } else {
            out.push_back(static_cast<char>(0xf0 | (code >> 18)));
            out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3f)));
        }
    }

    template <typename T>
    static T Little(T value) {
        // The format is little-endian on every target it ships on. Rather than
        // assume the host is, undo a big-endian host explicitly.
        const std::uint16_t probe = 1;
        if (*reinterpret_cast<const char*>(&probe) == 1) return value;
        T out = 0;
        for (std::size_t i = 0; i < sizeof(T); ++i) {
            out = static_cast<T>((out << 8) | ((value >> (8 * i)) & 0xff));
        }
        return out;
    }

    const char* data_;
    std::size_t size_;
    std::size_t position_ = 0;
    std::string where_;
};

Reference ReadReference(Cursor& cursor, const char* what) {
    const std::uint16_t raw = cursor.U16(what);
    Reference reference;
    reference.index = static_cast<std::uint16_t>(raw & 0x7fff);
    reference.trusted = (raw & 0x8000) != 0;
    return reference;
}

const std::string& SharedString(const Document& document, const Reference& reference,
                                const char* what) {
    if (reference.trusted) {
        throw XbfError(std::string(what) + " is a trusted string index, which the format has "
                                           "no table for");
    }
    return document.String(reference.index);
}

Constant ReadConstant(Cursor& cursor, const Document& document) {
    Constant constant;
    const std::uint8_t raw = cursor.U8("constant type");
    constant.type = static_cast<ConstantType>(raw);
    switch (constant.type) {
        case ConstantType::BoolFalse:
        case ConstantType::BoolTrue:
        case ConstantType::NullString:
            break;
        case ConstantType::Float:
            constant.number = cursor.F32("float constant");
            break;
        case ConstantType::Signed:
            constant.signed_value = cursor.I32("signed constant");
            break;
        case ConstantType::Color:
            constant.color = cursor.U32("color constant");
            break;
        case ConstantType::SharedString:
            constant.text = SharedString(document, ReadReference(cursor, "shared string"),
                                         "shared string constant");
            break;
        case ConstantType::UniqueString:
            constant.text = cursor.Utf16(false, "unique string constant");
            break;
        case ConstantType::Thickness:
            constant.left = cursor.F32("thickness");
            constant.top = cursor.F32("thickness");
            constant.right = cursor.F32("thickness");
            constant.bottom = cursor.F32("thickness");
            break;
        case ConstantType::GridLength:
            // XGRIDLENGTH is a byte of unit, three bytes the writer zeroes, and
            // a float. The padding is part of the on-disk shape, not an
            // accident of alignment -- minxcptypes.h says so in a comment.
            constant.grid_unit = cursor.U8("grid length unit");
            cursor.Skip(3, "grid length padding");
            constant.number = cursor.F32("grid length value");
            break;
        case ConstantType::Enum:
            constant.enum_type = cursor.U16("enum type");
            constant.enum_value = cursor.U32("enum value");
            break;
        default:
            throw XbfError("constant type " + std::to_string(raw) + " is not a value this "
                           "format defines");
    }
    return constant;
}

// Decodes one sub-stream. Stops -- and says why -- at the first instruction
// whose payload this reader cannot measure, because guessing its length would
// silently mis-read everything after it.
SubStream ReadSubStream(const char* data, std::size_t size, const Document& document,
                        std::size_t index) {
    SubStream stream;
    stream.node_bytes = static_cast<std::uint32_t>(size);
    Cursor cursor(data, size, "sub-stream " + std::to_string(index));
    while (!cursor.exhausted()) {
        Node node;
        node.offset = static_cast<std::uint32_t>(cursor.position());
        node.type = static_cast<NodeType>(cursor.U8("node type"));
        switch (node.type) {
            case NodeType::AddNamespace:
            case NodeType::PushScopeAddNamespace:
                node.xml_namespace = ReadReference(cursor, "xml namespace");
                node.prefix = cursor.Utf16(false, "namespace prefix");
                break;

            case NodeType::CreateTypeBeginInit:
            case NodeType::PushScopeCreateTypeBeginInit:
                node.type_ref = ReadReference(cursor, "type");
                node.has_type = true;
                break;

            case NodeType::CreateTypeWithConstantBeginInit:
            case NodeType::CreateTypeWithTypeConvertedConstantBeginInit:
            case NodeType::PushScopeCreateTypeWithConstantBeginInit:
            case NodeType::PushScopeCreateTypeWithTypeConvertedConstantBeginInit:
                node.type_ref = ReadReference(cursor, "type");
                node.has_type = true;
                node.constant = ReadConstant(cursor, document);
                node.has_constant = true;
                break;

            case NodeType::SetValue:
            case NodeType::SetValueFromMarkupExtension:
            case NodeType::PushScopeGetValue:
                node.property = ReadReference(cursor, "property");
                node.has_property = true;
                break;

            case NodeType::SetValueConstant:
            case NodeType::SetValueTypeConvertedConstant:
            case NodeType::SetValueFromStaticResource:
            case NodeType::SetValueFromThemeResource:
                node.property = ReadReference(cursor, "property");
                node.has_property = true;
                node.constant = ReadConstant(cursor, document);
                node.has_constant = true;
                break;

            case NodeType::SetValueTypeConvertedResolvedType:
                node.property = ReadReference(cursor, "property");
                node.has_property = true;
                node.type_ref = ReadReference(cursor, "resolved type");
                node.has_type = true;
                break;

            case NodeType::SetValueTypeConvertedResolvedProperty:
            case NodeType::SetValueFromTemplateBinding:
                node.property = ReadReference(cursor, "property");
                node.has_property = true;
                node.property_proxy = ReadReference(cursor, "resolved property");
                node.has_property_proxy = true;
                break;

            case NodeType::SetName:
            case NodeType::SetConnectionId:
            case NodeType::PushConstant:
            case NodeType::AddToDictionaryWithKey:
            case NodeType::GetResourcePropertyBag:
            case NodeType::ProvideStaticResourceValue:
            case NodeType::ProvideThemeResourceValue:
                node.constant = ReadConstant(cursor, document);
                node.has_constant = true;
                break;

            case NodeType::CheckPeerType:
                node.peer_type = cursor.Utf16(false, "peer type name");
                break;

            case NodeType::BeginConditionalScope:
                node.type_ref = ReadReference(cursor, "predicate type");
                node.has_type = true;
                node.constant.type = ConstantType::SharedString;
                node.constant.text =
                    SharedString(document, ReadReference(cursor, "predicate arguments"),
                                 "predicate arguments");
                node.has_constant = true;
                break;

            case NodeType::SetDeferredProperty: {
                node.property = ReadReference(cursor, "deferred property");
                node.has_property = true;
                node.sub_stream = cursor.Packed("deferred sub-stream index");
                node.has_sub_stream = true;
                const std::uint32_t statics = cursor.Packed("static resource count");
                const std::uint32_t themes = cursor.Packed("theme resource count");
                for (std::uint32_t i = 0; i < statics; ++i) {
                    node.static_resources.push_back(SharedString(
                        document, ReadReference(cursor, "static resource key"),
                        "static resource key"));
                }
                for (std::uint32_t i = 0; i < themes; ++i) {
                    node.theme_resources.push_back(SharedString(
                        document, ReadReference(cursor, "theme resource key"),
                        "theme resource key"));
                }
                break;
            }

            case NodeType::AddToCollection:
            case NodeType::AddToDictionary:
            case NodeType::EndInitPopScope:
            case NodeType::EndInitProvideValuePopScope:
            case NodeType::PushScope:
            case NodeType::PopScope:
            case NodeType::EndConditionalScope:
            case NodeType::SetResourceDictionaryItems:
            case NodeType::EndOfStream:
                break;

            case NodeType::SetCustomRuntimeData: {
                // The payload is a CustomWriterRuntimeData blob whose shape
                // depends on which custom writer produced it, and whose length
                // cannot be known without decoding it. A writer this reader
                // does not know therefore ends the sub-stream: everything after
                // it is unreachable, and pretending otherwise would assemble a
                // tree out of misaligned bytes.
                node.sub_stream = cursor.Packed("custom data sub-stream index");
                node.has_sub_stream = true;
                // Pre-resolved resources. XBFv2 always writes zeros here; the
                // fields survive from an earlier release and are read so the
                // stream stays aligned.
                const std::uint32_t statics = cursor.Packed("static resource count");
                const std::uint32_t themes = cursor.Packed("theme resource count");
                for (std::uint32_t i = 0; i < statics + themes; ++i) {
                    ReadReference(cursor, "pre-resolved resource");
                }
                node.custom_data_kind = cursor.Packed("custom writer kind");
                const auto kind = static_cast<CustomDataKind>(node.custom_data_kind);
                const auto shared = [&](const char* what) {
                    return SharedString(document, ReadReference(cursor, what), what);
                };
                if (kind == CustomDataKind::Style_v1 || kind == CustomDataKind::Style_v2) {
                    // StyleCustomRuntimeData: a vector of setters, each a flag
                    // byte, the property it targets, and the value. The flag
                    // bits are StyleSetterEssence::valueFlags::encode(), in
                    // dxaml/xcp/components/deferral/inc/StyleCustomRuntimeData.h.
                    const std::uint32_t setters = cursor.Packed("style setter count");
                    for (std::uint32_t i = 0; i < setters; ++i) {
                        StyleSetter setter;
                        const std::uint32_t flags = cursor.Packed("style setter flags");
                        setter.property_resolved = (flags & (1u << 4)) != 0;
                        if (setter.property_resolved) {
                            setter.property = ReadReference(cursor, "style setter property");
                        } else {
                            setter.property_name = shared("style setter property name");
                            setter.property_owner = ReadReference(cursor, "style setter owner");
                        }
                        if (flags & (1u << 2)) {
                            setter.value_kind = StyleSetter::ValueKind::String;
                            setter.text = shared("style setter string value");
                        } else if (flags & (1u << 5)) {
                            setter.value_kind = StyleSetter::ValueKind::Container;
                            setter.value = ReadConstant(cursor, document);
                        } else if (flags & (1u << 1)) {
                            setter.value_kind = StyleSetter::ValueKind::StaticResource;
                            setter.token = cursor.Packed("style setter value offset");
                        } else if (flags & 1u) {
                            setter.value_kind = StyleSetter::ValueKind::ThemeResource;
                            setter.token = cursor.Packed("style setter value offset");
                        } else if (flags & (1u << 3)) {
                            setter.value_kind = StyleSetter::ValueKind::Object;
                            setter.token = cursor.Packed("style setter value offset");
                        } else if (flags & (1u << 6)) {
                            setter.value_kind = StyleSetter::ValueKind::Self;
                            setter.token = cursor.Packed("style setter value offset");
                        }
                        node.style_setters.push_back(std::move(setter));
                    }
                    break;
                }
                if (kind != CustomDataKind::ResourceDictionary_v3) {
                    stream.nodes.push_back(node);
                    stream.truncated_because =
                        "the " + CustomDataKindName(node.custom_data_kind) +
                        " custom writer's runtime data at offset " +
                        std::to_string(node.offset) + " is not one this reader decodes";
                    return stream;
                }
                // ResourceDictionary_v3, as ResourceDictionaryCustomRuntimeData
                // Serializer writes it: an explicit-key map, the keys marked for
                // automatic undeferral, an implicit-key map, then the two
                // conditional maps and the conditionally-declared objects. Each
                // map entry is a shared string and an offset into the deferred
                // sub-stream, which is where that entry's value is built.
                const std::uint32_t explicit_count = cursor.Packed("explicit resource count");
                for (std::uint32_t i = 0; i < explicit_count; ++i) {
                    const std::string key = shared("explicit resource key");
                    node.deferred_resources.emplace_back(
                        key, cursor.Packed("explicit resource offset"));
                }
                const std::uint32_t undeferral = cursor.Packed("auto-undeferral count");
                for (std::uint32_t i = 0; i < undeferral; ++i) shared("auto-undeferred key");
                const std::uint32_t implicit_count = cursor.Packed("implicit resource count");
                for (std::uint32_t i = 0; i < implicit_count; ++i) {
                    const std::string key = shared("implicit resource key");
                    node.deferred_resources.emplace_back(
                        key, cursor.Packed("implicit resource offset"));
                }
                bool conditional = false;
                for (int map = 0; map < 2; ++map) {
                    const std::uint32_t count = cursor.Packed("conditional resource count");
                    conditional = conditional || count != 0;
                    for (std::uint32_t i = 0; i < count; ++i) {
                        shared("conditional resource key");
                        const std::uint32_t tokens = cursor.Packed("conditional token count");
                        for (std::uint32_t t = 0; t < tokens; ++t) {
                            cursor.Packed("conditional resource offset");
                        }
                    }
                }
                const std::uint32_t declared = cursor.Packed("conditionally declared count");
                conditional = conditional || declared != 0;
                for (std::uint32_t i = 0; i < declared; ++i) {
                    cursor.Packed("conditionally declared offset");
                    const std::uint32_t predicates = cursor.Packed("predicate count");
                    for (std::uint32_t p = 0; p < predicates; ++p) {
                        ReadReference(cursor, "predicate type");
                        ReadReference(cursor, "predicate arguments");
                    }
                }
                if (conditional) {
                    stream.nodes.push_back(node);
                    stream.truncated_because =
                        "the deferred resource dictionary at offset " +
                        std::to_string(node.offset) +
                        " has conditionally declared entries, which this reader does not "
                        "evaluate";
                    return stream;
                }
                break;
            }

            default:
                stream.nodes.push_back(node);
                stream.truncated_because =
                    "node type " + std::to_string(static_cast<int>(node.type)) + " at offset " +
                    std::to_string(node.offset) + " is not a value this format defines";
                return stream;
        }
        stream.nodes.push_back(std::move(node));
    }
    return stream;
}

}  // namespace

const std::string& Document::String(std::uint32_t index) const {
    if (index >= strings.size()) {
        throw XbfError("string index " + std::to_string(index) + " is past the end of a " +
                       std::to_string(strings.size()) + "-entry string table");
    }
    return strings[index];
}

std::string CustomDataKindName(std::uint32_t kind) {
    switch (static_cast<CustomDataKind>(kind)) {
        case CustomDataKind::VisualStateGroupCollection_v1: return "VisualStateGroupCollection v1";
        case CustomDataKind::VisualStateGroupCollection_v2: return "VisualStateGroupCollection v2";
        case CustomDataKind::VisualStateGroupCollection_v3: return "VisualStateGroupCollection v3";
        case CustomDataKind::VisualStateGroupCollection_v4: return "VisualStateGroupCollection v4";
        case CustomDataKind::VisualStateGroupCollection_v5: return "VisualStateGroupCollection v5";
        case CustomDataKind::Style_v1: return "Style v1";
        case CustomDataKind::Style_v2: return "Style v2";
        case CustomDataKind::Style_v3: return "Style v3";
        case CustomDataKind::DeferredElement_v1: return "DeferredElement v1";
        case CustomDataKind::DeferredElement_v2: return "DeferredElement v2";
        case CustomDataKind::DeferredElement_v3: return "DeferredElement v3";
        case CustomDataKind::ResourceDictionary_v1: return "ResourceDictionary v1";
        case CustomDataKind::ResourceDictionary_v2: return "ResourceDictionary v2";
        case CustomDataKind::ResourceDictionary_v3: return "ResourceDictionary v3";
        case CustomDataKind::ResourceDictionary_v4: return "ResourceDictionary v4";
        case CustomDataKind::Unknown: break;
    }
    return "custom writer kind " + std::to_string(kind);
}

std::string NodeTypeName(NodeType type) {
    switch (type) {
        case NodeType::None: return "None";
        case NodeType::PushScope: return "PushScope";
        case NodeType::PopScope: return "PopScope";
        case NodeType::AddNamespace: return "AddNamespace";
        case NodeType::PushConstant: return "PushConstant";
        case NodeType::PushResolvedType: return "PushResolvedType";
        case NodeType::PushResolvedProperty: return "PushResolvedProperty";
        case NodeType::SetValue: return "SetValue";
        case NodeType::AddToCollection: return "AddToCollection";
        case NodeType::AddToDictionary: return "AddToDictionary";
        case NodeType::AddToDictionaryWithKey: return "AddToDictionaryWithKey";
        case NodeType::CheckPeerType: return "CheckPeerType";
        case NodeType::SetConnectionId: return "SetConnectionId";
        case NodeType::SetName: return "SetName";
        case NodeType::GetResourcePropertyBag: return "GetResourcePropertyBag";
        case NodeType::SetCustomRuntimeData: return "SetCustomRuntimeData";
        case NodeType::SetResourceDictionaryItems: return "SetResourceDictionaryItems";
        case NodeType::SetDeferredProperty: return "SetDeferredProperty";
        case NodeType::PushScopeAddNamespace: return "PushScopeAddNamespace";
        case NodeType::PushScopeGetValue: return "PushScopeGetValue";
        case NodeType::PushScopeCreateTypeBeginInit: return "PushScopeCreateTypeBeginInit";
        case NodeType::PushScopeCreateTypeWithConstantBeginInit:
            return "PushScopeCreateTypeWithConstantBeginInit";
        case NodeType::PushScopeCreateTypeWithTypeConvertedConstantBeginInit:
            return "PushScopeCreateTypeWithTypeConvertedConstantBeginInit";
        case NodeType::CreateTypeBeginInit: return "CreateTypeBeginInit";
        case NodeType::CreateTypeWithConstantBeginInit: return "CreateTypeWithConstantBeginInit";
        case NodeType::CreateTypeWithTypeConvertedConstantBeginInit:
            return "CreateTypeWithTypeConvertedConstantBeginInit";
        case NodeType::SetValueConstant: return "SetValueConstant";
        case NodeType::SetValueTypeConvertedConstant: return "SetValueTypeConvertedConstant";
        case NodeType::SetValueTypeConvertedResolvedProperty:
            return "SetValueTypeConvertedResolvedProperty";
        case NodeType::SetValueTypeConvertedResolvedType:
            return "SetValueTypeConvertedResolvedType";
        case NodeType::SetValueFromStaticResource: return "SetValueFromStaticResource";
        case NodeType::SetValueFromTemplateBinding: return "SetValueFromTemplateBinding";
        case NodeType::SetValueFromMarkupExtension: return "SetValueFromMarkupExtension";
        case NodeType::EndInitPopScope: return "EndInitPopScope";
        case NodeType::ProvideStaticResourceValue: return "ProvideStaticResourceValue";
        case NodeType::ProvideThemeResourceValue: return "ProvideThemeResourceValue";
        case NodeType::SetValueFromThemeResource: return "SetValueFromThemeResource";
        case NodeType::EndOfStream: return "EndOfStream";
        case NodeType::BeginConditionalScope: return "BeginConditionalScope";
        case NodeType::EndConditionalScope: return "EndConditionalScope";
        case NodeType::EndInitProvideValuePopScope: return "EndInitProvideValuePopScope";
    }
    return "node type " + std::to_string(static_cast<int>(type));
}

Document Read(const std::string& bytes) {
    Cursor container(bytes.data(), bytes.size(), "container");
    if (bytes.size() < kContainerPrologue) throw XbfError("not an XBF file: shorter than a header");
    const std::string magic = container.Bytes(4, "magic number");
    if (magic != std::string("XBF\0", 4)) {
        throw XbfError("not an XBF file: magic number is not 'XBF\\0'");
    }
    const std::uint32_t metadata_size = container.U32("metadata size");
    const std::uint32_t node_size = container.U32("node stream size");
    if (kContainerPrologue + std::size_t{metadata_size} + node_size != bytes.size()) {
        throw XbfError("XBF container is " + std::to_string(bytes.size()) + " bytes but declares " +
                       std::to_string(metadata_size) + " of metadata and " +
                       std::to_string(node_size) + " of node stream");
    }

    Document document;
    Cursor metadata(bytes.data() + kContainerPrologue, metadata_size, "metadata stream");
    document.major_version = metadata.U32("major version");
    document.minor_version = metadata.U32("minor version");
    if (document.major_version != kMajorVersion || document.minor_version != kMinorVersion) {
        throw XbfError("XBF version " + std::to_string(document.major_version) + "." +
                       std::to_string(document.minor_version) +
                       " is not implemented; this reader implements " +
                       std::to_string(kMajorVersion) + "." + std::to_string(kMinorVersion) +
                       " only");
    }

    std::uint64_t offsets[6];
    for (auto& offset : offsets) offset = metadata.U64("table offset");
    document.hash = metadata.Bytes(kHashSize, "source hash");
    if (metadata.position() != kVersionSize + kHeaderSize) {
        throw XbfError("metadata header is not the size the format defines");
    }

    // Each table announces where it starts. The runtime checks that against
    // where it actually arrived and refuses the file if they differ; so does
    // this, because a table read from the wrong place decodes into plausible
    // nonsense rather than into an error.
    const auto expect = [&metadata](std::uint64_t offset, const char* table) {
        if (metadata.position() != offset) {
            throw XbfError(std::string(table) + " starts at " +
                           std::to_string(metadata.position()) + " but the header says " +
                           std::to_string(offset));
        }
    };

    expect(offsets[0], "the string table");
    const std::uint32_t string_count = metadata.U32("string table count");
    document.strings.reserve(string_count);
    for (std::uint32_t i = 0; i < string_count; ++i) {
        document.strings.push_back(metadata.Utf16(true, "table string"));
    }

    expect(offsets[1], "the assembly table");
    const std::uint32_t assembly_count = metadata.U32("assembly table count");
    for (std::uint32_t i = 0; i < assembly_count; ++i) {
        Assembly assembly;
        assembly.provider_kind = metadata.U32("assembly provider kind");
        assembly.name = metadata.U32("assembly name");
        document.assemblies.push_back(assembly);
    }

    expect(offsets[2], "the type namespace table");
    const std::uint32_t type_namespace_count = metadata.U32("type namespace table count");
    for (std::uint32_t i = 0; i < type_namespace_count; ++i) {
        TypeNamespace type_namespace;
        type_namespace.assembly = metadata.U32("type namespace assembly");
        type_namespace.name = metadata.U32("type namespace name");
        document.type_namespaces.push_back(type_namespace);
    }

    expect(offsets[3], "the type table");
    const std::uint32_t type_count = metadata.U32("type table count");
    for (std::uint32_t i = 0; i < type_count; ++i) {
        Type type;
        type.flags = metadata.U32("type flags");
        type.type_namespace = metadata.U32("type namespace");
        type.name = metadata.U32("type name");
        document.types.push_back(type);
    }

    expect(offsets[4], "the property table");
    const std::uint32_t property_count = metadata.U32("property table count");
    for (std::uint32_t i = 0; i < property_count; ++i) {
        Property property;
        property.flags = metadata.U32("property flags");
        property.type = metadata.U32("property type");
        property.name = metadata.U32("property name");
        document.properties.push_back(property);
    }

    expect(offsets[5], "the xml namespace table");
    const std::uint32_t xml_namespace_count = metadata.U32("xml namespace table count");
    for (std::uint32_t i = 0; i < xml_namespace_count; ++i) {
        document.xml_namespaces.push_back(metadata.U32("xml namespace uri"));
    }

    if (metadata.position() != metadata.size()) {
        throw XbfError("metadata stream has " + std::to_string(metadata.size() - metadata.position()) +
                       " bytes after the last table");
    }

    // The node stream opens with a table of the sub-streams inside it. Each
    // entry says where its nodes start and where its line-number data starts;
    // the next entry's node offset is where the line data ends.
    const char* node_stream = bytes.data() + kContainerPrologue + metadata_size;
    Cursor table(node_stream, node_size, "node stream table");
    const std::uint32_t sub_stream_count = table.U32("sub-stream count");
    std::vector<std::pair<std::uint32_t, std::uint32_t>> entries;
    for (std::uint32_t i = 0; i < sub_stream_count; ++i) {
        const std::uint32_t nodes = table.U32("sub-stream node offset");
        const std::uint32_t lines = table.U32("sub-stream line offset");
        entries.emplace_back(nodes, lines);
    }
    const std::size_t table_size = table.position();

    for (std::size_t i = 0; i < entries.size(); ++i) {
        const std::uint32_t node_offset = entries[i].first;
        const std::uint32_t line_offset = entries[i].second;
        const std::uint32_t line_end =
            i + 1 < entries.size() ? entries[i + 1].first : node_size - table_size;
        if (node_offset > line_offset || line_offset > line_end ||
            table_size + line_end > node_size) {
            throw XbfError("sub-stream " + std::to_string(i) + " has offsets outside the node "
                           "stream");
        }
        SubStream stream =
            ReadSubStream(node_stream + table_size + node_offset, line_offset - node_offset,
                          document, i);
        stream.line_bytes = line_end - line_offset;
        document.sub_streams.push_back(std::move(stream));
    }

    return document;
}

}  // namespace xbf
}  // namespace openxaml
