#include "xbf.h"

#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <utility>

#ifdef OPENXAML_STABLE_XBF_SCHEMA
#include "stable_xbf_schema.h"
#endif

namespace openxaml::xbf {
namespace {

constexpr std::uint32_t kUnknownType = 0x2;
constexpr std::int32_t kVisualStateV1 = 420;
constexpr std::int32_t kResourceDictionaryV1 = 371;
constexpr std::int32_t kDeferredElementV1 = 745;

class Reader {
public:
    explicit Reader(const std::vector<std::uint8_t>& bytes) : bytes_(bytes) {}

    std::size_t position() const noexcept { return position_; }
    std::size_t size() const noexcept { return bytes_.size(); }

    void seek(std::size_t position) {
        if (position > bytes_.size()) fail("seek lies beyond the XBF stream");
        position_ = position;
    }

    void require_position(std::size_t expected, const char* section) {
        if (position_ != expected) {
            std::ostringstream message;
            message << section << " begins at " << position_ << ", expected " << expected;
            fail(message.str());
        }
    }

    std::uint8_t u8() { return scalar<std::uint8_t>(); }
    // BinaryReader.ReadBoolean, which defines the XBF tool's behavior, treats
    // every nonzero byte as true. Generated visual-state data does in fact use
    // values such as 2, so narrowing this to {0,1} rejects valid SDK output.
    bool boolean() { return u8() != 0; }
    std::uint16_t u16() { return scalar<std::uint16_t>(); }
    std::uint32_t u32() { return scalar<std::uint32_t>(); }
    std::uint64_t u64() { return scalar<std::uint64_t>(); }
    std::int32_t i32() { return static_cast<std::int32_t>(u32()); }
    std::int64_t i64() { return static_cast<std::int64_t>(u64()); }
    float f32() {
        const auto bits = u32();
        float value = 0;
        std::memcpy(&value, &bits, sizeof value);
        return value;
    }

    std::int32_t seven_bit() {
        std::uint32_t value = 0;
        for (unsigned shift = 0; shift < 35; shift += 7) {
            const auto byte = u8();
            if (shift == 28 && (byte & 0xf0) != 0) fail("7-bit integer overflows Int32");
            value |= static_cast<std::uint32_t>(byte & 0x7f) << shift;
            if ((byte & 0x80) == 0) return static_cast<std::int32_t>(value);
        }
        fail("unterminated 7-bit integer");
    }

    std::size_t count(bool seven_bit_length = false) {
        const auto value = seven_bit_length ? seven_bit() : i32();
        if (value < 0) fail("negative vector length");
        return static_cast<std::size_t>(value);
    }

    std::string xbf_string() {
        const auto length = i32();
        if (length < 0) fail("negative XBF string length");
        std::string result;
        result.reserve(static_cast<std::size_t>(length));
        for (std::int32_t index = 0; index < length; ++index) {
            const std::uint16_t first = u16();
            std::uint32_t codepoint = first;
            if (first >= 0xd800 && first <= 0xdbff) {
                if (++index >= length) fail("truncated UTF-16 surrogate pair");
                const auto second = u16();
                if (second < 0xdc00 || second > 0xdfff) fail("invalid UTF-16 surrogate pair");
                codepoint = 0x10000u + ((first - 0xd800u) << 10) + (second - 0xdc00u);
            } else if (first >= 0xdc00 && first <= 0xdfff) {
                fail("unpaired UTF-16 low surrogate");
            }
            append_utf8(result, codepoint);
        }
        return result;
    }

    Reference reference() {
        const auto value = u16();
        return {static_cast<std::uint16_t>(value & 0x7fff), (value & 0x8000) != 0};
    }

    [[noreturn]] void fail(const std::string& message) const {
        throw Error(position_, message);
    }

private:
    template <class T>
    T scalar() {
        if (sizeof(T) > bytes_.size() - position_) fail("unexpected end of XBF stream");
        T value = 0;
        for (std::size_t byte = 0; byte < sizeof(T); ++byte)
            value |= static_cast<T>(bytes_[position_++]) << (byte * 8);
        return value;
    }

    static void append_utf8(std::string& output, std::uint32_t value) {
        if (value <= 0x7f) {
            output.push_back(static_cast<char>(value));
        } else if (value <= 0x7ff) {
            output.push_back(static_cast<char>(0xc0 | (value >> 6)));
            output.push_back(static_cast<char>(0x80 | (value & 0x3f)));
        } else if (value <= 0xffff) {
            output.push_back(static_cast<char>(0xe0 | (value >> 12)));
            output.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
            output.push_back(static_cast<char>(0x80 | (value & 0x3f)));
        } else {
            output.push_back(static_cast<char>(0xf0 | (value >> 18)));
            output.push_back(static_cast<char>(0x80 | ((value >> 12) & 0x3f)));
            output.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
            output.push_back(static_cast<char>(0x80 | (value & 0x3f)));
        }
    }

    const std::vector<std::uint8_t>& bytes_;
    std::size_t position_ = 0;
};

std::string at(const std::vector<std::string>& values, std::int32_t index,
               Reader& reader, const char* table) {
    if (index < 0 || static_cast<std::size_t>(index) >= values.size()) {
        std::ostringstream message;
        message << table << " index " << index << " is out of range";
        reader.fail(message.str());
    }
    return values[static_cast<std::size_t>(index)];
}

std::string shared_string(Reader& reader, const Metadata& metadata) {
    const auto reference = reader.reference();
    if (reference.trusted || reference.id >= metadata.strings.size())
        reader.fail("invalid shared-string reference");
    return metadata.strings[reference.id];
}

Constant constant(Reader& reader, const Metadata& metadata) {
    Constant result;
    result.kind = static_cast<ConstantKind>(reader.u8());
    switch (result.kind) {
    case ConstantKind::False:
    case ConstantKind::True:
    case ConstantKind::NullString:
        break;
    case ConstantKind::Float:
        result.floats.push_back(reader.f32());
        break;
    case ConstantKind::Signed:
        result.signed_value = reader.i32();
        break;
    case ConstantKind::SharedString:
        result.string_value = shared_string(reader, metadata);
        break;
    case ConstantKind::Thickness:
        for (int index = 0; index < 4; ++index) result.floats.push_back(reader.f32());
        break;
    case ConstantKind::GridLength:
        result.unsigned_value = reader.u32();
        result.floats.push_back(reader.f32());
        break;
    case ConstantKind::Color:
        result.unsigned_value = reader.u32();
        break;
    case ConstantKind::UniqueString:
        result.string_value = reader.xbf_string();
        break;
    case ConstantKind::Enum:
        result.enum_type = {reader.u16(), true};
        result.unsigned_value = reader.u32();
        break;
    default:
        reader.fail("unrecognized persisted constant kind");
    }
    return result;
}

void skip_predicate(Reader& reader, const Metadata& metadata) {
    (void)reader.reference();
    (void)shared_string(reader, metadata);
}

template <class Function>
void repeat(Reader& reader, bool seven_bit_length, Function function) {
    const auto length = reader.count(seven_bit_length);
    for (std::size_t index = 0; index < length; ++index) function();
}

void skip_conditionally_declared(Reader& reader, const Metadata& metadata) {
    repeat(reader, true, [&] {
        (void)reader.seven_bit();
        repeat(reader, true, [&] { skip_predicate(reader, metadata); });
    });
}

StyleSetter read_style_setter(Reader& reader, const Metadata& metadata) {
    constexpr int has_theme = 1 << 0;
    constexpr int has_static = 1 << 1;
    constexpr int has_string = 1 << 2;
    constexpr int has_object = 1 << 3;
    constexpr int property_resolved = 1 << 4;
    constexpr int has_container = 1 << 5;
    constexpr int has_self_token = 1 << 6;
    StyleSetter setter;
    const int flags = reader.seven_bit();
    if ((flags & has_self_token) == 0) {
        if ((flags & property_resolved) != 0) {
            setter.property_resolved = true;
            setter.property = reader.reference();
        } else {
            setter.property_name = shared_string(reader, metadata);
            setter.property_owner = reader.reference();
        }
    }
    if ((flags & has_string) != 0) {
        setter.value_kind = StyleSetter::ValueKind::String;
        setter.text = shared_string(reader, metadata);
    } else if ((flags & has_container) != 0) {
        setter.value_kind = StyleSetter::ValueKind::Container;
        setter.value = constant(reader, metadata);
    } else if ((flags & has_static) != 0) {
        setter.value_kind = StyleSetter::ValueKind::StaticResource;
        setter.token = reader.seven_bit();
    } else if ((flags & has_theme) != 0) {
        setter.value_kind = StyleSetter::ValueKind::ThemeResource;
        setter.token = reader.seven_bit();
    } else if ((flags & has_object) != 0) {
        setter.value_kind = StyleSetter::ValueKind::Object;
        setter.token = reader.seven_bit();
    } else if ((flags & has_self_token) != 0) {
        setter.value_kind = StyleSetter::ValueKind::Self;
        setter.token = reader.seven_bit();
    }
    return setter;
}

void skip_visual_state(Reader& reader, const Metadata& metadata, int version) {
    (void)shared_string(reader, metadata);
    (void)reader.seven_bit();
    (void)reader.boolean();
    if (version != kVisualStateV1) {
        repeat(reader, true, [&] { (void)reader.seven_bit(); });
        repeat(reader, true, [&] {
            repeat(reader, true, [&] { (void)reader.seven_bit(); });
        });
        if (version != 1) {
            repeat(reader, true, [&] { (void)reader.seven_bit(); });
            repeat(reader, true, [&] { (void)reader.seven_bit(); });
            if (version != 3 && version != 4)
                repeat(reader, true, [&] { (void)reader.seven_bit(); });
        }
    }
}

void read_custom_data(Reader& reader, const Metadata& metadata, Node& node) {
    const int version = node.custom_data_version;
    if (version == kDeferredElementV1 || version == 6 || version == 9) {
        (void)shared_string(reader, metadata);
        if (version != kDeferredElementV1) {
            repeat(reader, true, [&] {
                (void)reader.reference();
                (void)constant(reader, metadata);
            });
            if (version != 6) (void)reader.boolean();
        }
        return;
    }

    if (version == kResourceDictionaryV1 || version == 7 || version == 10 ||
        version == 12) {
        if (version == 12) {
            repeat(reader, true, [&] {
                (void)shared_string(reader, metadata);
                (void)reader.u64();
                (void)reader.seven_bit();
            });
            repeat(reader, true, [&] { (void)shared_string(reader, metadata); });
            repeat(reader, true, [&] {
                (void)shared_string(reader, metadata);
                (void)reader.u64();
                repeat(reader, true, [&] { (void)reader.seven_bit(); });
            });
            skip_conditionally_declared(reader, metadata);
            return;
        }
        auto keyed = [&] {
            repeat(reader, true, [&] {
                (void)shared_string(reader, metadata);
                (void)reader.seven_bit();
            });
        };
        keyed();
        repeat(reader, true, [&] { (void)shared_string(reader, metadata); });
        keyed();
        if (version == kResourceDictionaryV1)
            repeat(reader, true, [&] { (void)shared_string(reader, metadata); });
        if (version == kResourceDictionaryV1 || version == 7)
            repeat(reader, true, [&] { (void)shared_string(reader, metadata); });
        if (version == 10) {
            auto conditional_keys = [&] {
                repeat(reader, true, [&] {
                    (void)shared_string(reader, metadata);
                    repeat(reader, true, [&] { (void)reader.seven_bit(); });
                });
            };
            conditional_keys();
            conditional_keys();
            skip_conditionally_declared(reader, metadata);
        }
        return;
    }

    if (version == 2 || version == 8 || version == 11) {
        repeat(reader, true, [&] {
            node.style_setters.push_back(read_style_setter(reader, metadata));
        });
        if (version == 11) skip_conditionally_declared(reader, metadata);
        return;
    }

    if (version == kVisualStateV1 || (version >= 1 && version <= 5)) {
        repeat(reader, true, [&] { (void)reader.seven_bit(); });
        repeat(reader, true, [&] { skip_visual_state(reader, metadata, version); });
        repeat(reader, true, [&] {
            (void)shared_string(reader, metadata);
            (void)reader.boolean();
            (void)reader.seven_bit();
        });
        repeat(reader, true, [&] {
            (void)shared_string(reader, metadata);
            (void)shared_string(reader, metadata);
            (void)reader.seven_bit();
        });
        (void)reader.boolean();
        repeat(reader, true, [&] {
            (void)reader.seven_bit();
            (void)reader.seven_bit();
            (void)reader.seven_bit();
        });
        repeat(reader, true, [&] { (void)reader.seven_bit(); });
        (void)reader.seven_bit();
        if (version == 4 || version == 5)
            repeat(reader, true, [&] { (void)shared_string(reader, metadata); });
        return;
    }

    reader.fail("unrecognized custom runtime-data version " + std::to_string(version));
}

Node read_node(Reader& reader, const Metadata& metadata) {
    Node node;
    node.offset = reader.position();
    node.type = static_cast<NodeType>(reader.u8());
    switch (node.type) {
    case NodeType::PushScope:
    case NodeType::PopScope:
    case NodeType::AddToCollection:
    case NodeType::AddToDictionary:
    case NodeType::EndInitPopScope:
    case NodeType::EndConditionalScope:
    case NodeType::EndInitProvideValuePopScope:
        break;
    case NodeType::AddNamespace:
    case NodeType::PushScopeAddNamespace:
        node.object = reader.reference();
        node.text = reader.xbf_string();
        break;
    case NodeType::PushConstant:
    case NodeType::AddToDictionaryWithKey:
    case NodeType::SetConnectionId:
    case NodeType::SetName:
    case NodeType::GetResourcePropertyBag:
    case NodeType::ProvideStaticResourceValue:
    case NodeType::ProvideThemeResourceValue:
        node.constant = constant(reader, metadata);
        break;
    case NodeType::SetValue:
    case NodeType::SetValueFromMarkupExtension:
    case NodeType::PushScopeGetValue:
        node.object = reader.reference();
        break;
    case NodeType::CheckPeerType:
        node.text = reader.xbf_string();
        break;
    case NodeType::SetDeferredProperty: {
        node.object = reader.reference();
        node.substream = reader.seven_bit();
        const auto static_count = reader.count(true);
        const auto theme_count = reader.count(true);
        for (std::size_t index = 0; index < static_count; ++index)
            node.static_resources.push_back(shared_string(reader, metadata));
        for (std::size_t index = 0; index < theme_count; ++index)
            node.theme_resources.push_back(shared_string(reader, metadata));
        break;
    }
    case NodeType::SetCustomRuntimeData: {
        node.substream = reader.seven_bit();
        const auto static_count = reader.count(true);
        const auto theme_count = reader.count(true);
        for (std::size_t index = 0; index < static_count + theme_count; ++index)
            (void)reader.reference();
        node.custom_data_version = reader.seven_bit();
        read_custom_data(reader, metadata, node);
        break;
    }
    case NodeType::CreateTypeBeginInit:
    case NodeType::PushScopeCreateTypeBeginInit:
        node.object = reader.reference();
        break;
    case NodeType::CreateTypeWithConstantBeginInit:
    case NodeType::PushScopeCreateTypeWithConstantBeginInit:
    case NodeType::CreateTypeWithTypeConvertedConstantBeginInit:
    case NodeType::PushScopeCreateTypeWithTypeConvertedConstantBeginInit:
        node.object = reader.reference();
        node.constant = constant(reader, metadata);
        break;
    case NodeType::SetValueConstant:
    case NodeType::SetValueTypeConvertedConstant:
    case NodeType::SetValueFromStaticResource:
    case NodeType::SetValueFromThemeResource:
        node.object = reader.reference();
        node.constant = constant(reader, metadata);
        break;
    case NodeType::SetValueTypeConvertedResolvedType:
    case NodeType::SetValueTypeConvertedResolvedProperty:
    case NodeType::SetValueFromTemplateBinding:
        node.object = reader.reference();
        node.second_object = reader.reference();
        break;
    case NodeType::BeginConditionalScope:
        node.object = reader.reference();
        node.text = shared_string(reader, metadata);
        break;
    default:
        reader.fail("unrecognized XBF node opcode " +
                    std::to_string(static_cast<unsigned>(node.type)));
    }
    return node;
}

Metadata read_metadata(Reader& reader, std::size_t metadata_start) {
    Metadata metadata;
    metadata.major = reader.i32();
    metadata.minor = reader.i32();
    if (metadata.major != 2 || metadata.minor < 0 || metadata.minor > 1)
        reader.fail("unsupported XBF version");

    const auto string_offset = reader.i64();
    const auto assembly_offset = reader.i64();
    const auto type_namespace_offset = reader.i64();
    const auto type_offset = reader.i64();
    const auto property_offset = reader.i64();
    const auto xml_namespace_offset = reader.i64();
    for (int index = 0; index < 64; ++index) (void)reader.u8();

    auto absolute = [&](std::int64_t offset, const char* section) {
        if (offset < 0 || static_cast<std::uint64_t>(offset) >
                              std::numeric_limits<std::size_t>::max() - metadata_start)
            reader.fail(std::string("invalid ") + section + " offset");
        return metadata_start + static_cast<std::size_t>(offset);
    };

    reader.require_position(absolute(string_offset, "string table"), "string table");
    repeat(reader, false, [&] {
        metadata.strings.push_back(reader.xbf_string());
        if (metadata.minor >= 1 && reader.u16() != 0)
            reader.fail("XBF metadata string lacks its null terminator");
    });

    reader.require_position(absolute(assembly_offset, "assembly table"), "assembly table");
    repeat(reader, false, [&] {
        (void)reader.i32();
        metadata.assemblies.push_back(at(metadata.strings, reader.i32(), reader, "string"));
    });

    reader.require_position(absolute(type_namespace_offset, "type namespace table"),
                            "type namespace table");
    repeat(reader, false, [&] {
        const auto assembly = reader.i32();
        const auto name = at(metadata.strings, reader.i32(), reader, "string");
        if (assembly < 0 || static_cast<std::size_t>(assembly) >= metadata.assemblies.size())
            reader.fail("assembly index is out of range");
        metadata.type_namespaces.push_back(name);
    });

    reader.require_position(absolute(type_offset, "type table"), "type table");
    repeat(reader, false, [&] {
        Type type;
        type.flags = reader.u32();
        type.namespace_id = reader.i32();
        type.name = at(metadata.strings, reader.i32(), reader, "string");
        if ((type.flags & kUnknownType) != 0) {
            // Unknown/custom types point at the XML namespace table, which is
            // read later. Resolve their full names after all metadata exists.
        } else if (type.namespace_id >= 0 &&
                   static_cast<std::size_t>(type.namespace_id) < metadata.type_namespaces.size()) {
            type.full_name = metadata.type_namespaces[type.namespace_id] + "." + type.name;
        } else {
            reader.fail("type namespace index is out of range");
        }
        metadata.types.push_back(std::move(type));
    });

    reader.require_position(absolute(property_offset, "property table"), "property table");
    repeat(reader, false, [&] {
        Property property;
        property.flags = reader.u32();
        property.declaring_type_id = reader.i32();
        property.name = at(metadata.strings, reader.i32(), reader, "string");
        if (property.declaring_type_id < 0 ||
            static_cast<std::size_t>(property.declaring_type_id) >= metadata.types.size())
            reader.fail("property declaring-type index is out of range");
        metadata.properties.push_back(std::move(property));
    });

    reader.require_position(absolute(xml_namespace_offset, "XML namespace table"),
                            "XML namespace table");
    repeat(reader, false, [&] {
        metadata.xml_namespaces.push_back(at(metadata.strings, reader.i32(), reader, "string"));
    });

    for (auto& type : metadata.types) {
        if ((type.flags & kUnknownType) == 0) continue;
        if (type.namespace_id < 0 ||
            static_cast<std::size_t>(type.namespace_id) >= metadata.xml_namespaces.size())
            reader.fail("custom type XML namespace index is out of range");
        auto name = metadata.xml_namespaces[type.namespace_id];
        const auto condition = name.find('?');
        if (condition != std::string::npos) name.resize(condition);
        constexpr const char using_prefix[] = "using:";
        if (name.rfind(using_prefix, 0) == 0) name.erase(0, sizeof(using_prefix) - 1);
        type.full_name = name.empty() ? type.name : name + "." + type.name;
    }
    return metadata;
}

}  // namespace

Error::Error(std::size_t offset, const std::string& message)
    : std::runtime_error("XBF offset " + std::to_string(offset) + ": " + message),
      offset_(offset) {}

Document Read(const std::vector<std::uint8_t>& bytes) {
    Reader reader(bytes);
    if (reader.u8() != 'X' || reader.u8() != 'B' || reader.u8() != 'F' || reader.u8() != 0)
        reader.fail("missing XBF magic number");

    Document document;
    document.metadata_bytes = reader.u32();
    document.nodestream_bytes = reader.u32();
    const auto metadata_start = reader.position();
    if (document.metadata_bytes > reader.size() - metadata_start)
        reader.fail("metadata section exceeds the XBF stream");
    document.metadata = read_metadata(reader, metadata_start);
    reader.require_position(metadata_start + document.metadata_bytes, "nodestream section");

    struct StreamInfo { std::uint32_t nodes; std::uint32_t lines; };
    std::vector<StreamInfo> table;
    repeat(reader, false, [&] { table.push_back({reader.u32(), reader.u32()}); });
    const auto streams_start = reader.position();
    for (std::size_t index = 0; index < table.size(); ++index) {
        const auto node_start = streams_start + table[index].nodes;
        const auto line_start = streams_start + table[index].lines;
        const auto next_start = index + 1 < table.size()
                                    ? streams_start + table[index + 1].nodes
                                    : bytes.size();
        if (line_start < node_start || next_start < line_start || next_start > bytes.size())
            reader.fail("invalid XBF substream offsets");
        reader.require_position(node_start, "node substream");
        Substream substream;
        substream.node_bytes = line_start - node_start;
        substream.line_bytes = next_start - line_start;
        while (reader.position() < line_start) {
            const auto node_offset = reader.position();
            try {
                substream.nodes.push_back(read_node(reader, document.metadata));
            } catch (const Error& error) {
                throw Error(error.offset(), "while decoding node at " +
                                              std::to_string(node_offset) + ": " +
                                              error.what());
            }
        }
        reader.require_position(line_start, "line substream");
        reader.seek(next_start);
        document.substreams.push_back(std::move(substream));
    }
    return document;
}

Document ReadFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw Error(0, "cannot open " + path.string());
    stream.seekg(0, std::ios::end);
    const auto length = stream.tellg();
    if (length < 0) throw Error(0, "cannot determine size of " + path.string());
    stream.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    if (!bytes.empty() && !stream.read(reinterpret_cast<char*>(bytes.data()), length))
        throw Error(0, "cannot read " + path.string());
    return Read(bytes);
}

std::string Document::type_name(Reference reference) const {
    if (reference.trusted) {
#ifdef OPENXAML_STABLE_XBF_SCHEMA
        if (const char* name = generated::TypeName(reference.id)) return name;
#endif
        return "stable-type:" + std::to_string(reference.id);
    }
    if (reference.id >= metadata.types.size()) return {};
    return metadata.types[reference.id].full_name;
}

std::string Document::property_name(Reference reference) const {
    if (reference.trusted) {
#ifdef OPENXAML_STABLE_XBF_SCHEMA
        if (const char* name = generated::PropertyName(reference.id)) return name;
#endif
        return "stable-property:" + std::to_string(reference.id);
    }
    if (reference.id >= metadata.properties.size()) return {};
    const auto& property = metadata.properties[reference.id];
    if (property.declaring_type_id < 0 ||
        static_cast<std::size_t>(property.declaring_type_id) >= metadata.types.size())
        return property.name;
    return metadata.types[property.declaring_type_id].full_name + "." + property.name;
}

}  // namespace openxaml::xbf
