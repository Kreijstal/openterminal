#include "xbf.h"
#include "xbf_object.h"

#include <filesystem>
#include <cstdint>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {

void U32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8)
        bytes.push_back(static_cast<std::uint8_t>(value >> shift));
}

void U64(std::vector<std::uint8_t>& bytes, std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8)
        bytes.push_back(static_cast<std::uint8_t>(value >> shift));
}

std::vector<std::uint8_t> MinimalXbf() {
    std::vector<std::uint8_t> bytes{'X', 'B', 'F', 0};
    U32(bytes, 144);  // metadata bytes
    U32(bytes, 13);   // substream table plus one node
    U32(bytes, 2);
    U32(bytes, 1);
    for (std::uint64_t offset : {120, 124, 128, 132, 136, 140}) U64(bytes, offset);
    bytes.resize(bytes.size() + 64);  // source hash
    for (int table = 0; table < 6; ++table) U32(bytes, 0);
    U32(bytes, 1);  // substream count
    U32(bytes, 0);  // node offset
    U32(bytes, 1);  // line offset
    bytes.push_back(static_cast<std::uint8_t>(openxaml::xbf::NodeType::PushScope));
    return bytes;
}

const char* KindName(openxaml::xbf::Value::Kind kind) {
    using Kind = openxaml::xbf::Value::Kind;
    switch (kind) {
    case Kind::Empty: return "empty";
    case Kind::Constant: return "constant";
    case Kind::Object: return "object";
    case Kind::Type: return "type";
    case Kind::Property: return "property";
    case Kind::Resource: return "resource";
    }
    return "unknown";
}

void DumpValue(const openxaml::xbf::Value& value, std::size_t depth);

void DumpObject(const std::shared_ptr<openxaml::xbf::Object>& object, std::size_t depth) {
    const std::string indent(depth * 2, ' ');
    if (!object) {
        std::cout << indent << "<null object>\n";
        return;
    }
    std::cout << indent << object->type;
    if (!object->x_class.empty()) std::cout << " x:Class=" << object->x_class;
    std::cout << '\n';
    for (const auto& [name, value] : object->properties) {
        std::cout << indent << "  ." << name << " = ";
        DumpValue(value, depth + 2);
    }
    for (const auto& value : object->items) {
        std::cout << indent << "  + ";
        DumpValue(value, depth + 2);
    }
    for (const auto& [key, value] : object->dictionary) {
        std::cout << indent << "  [" << key << "] = ";
        DumpValue(value, depth + 2);
    }
}

void DumpValue(const openxaml::xbf::Value& value, std::size_t depth) {
    if (value.kind == openxaml::xbf::Value::Kind::Object && value.object) {
        std::cout << '\n';
        DumpObject(value.object, depth);
        return;
    }
    std::cout << KindName(value.kind);
    if (!value.text.empty()) std::cout << ':' << value.text;
    if (!value.constant.string_value.empty()) std::cout << ':' << value.constant.string_value;
    if (value.constant.kind == openxaml::xbf::ConstantKind::Signed ||
        value.constant.kind == openxaml::xbf::ConstantKind::Enum)
        std::cout << ':' << value.constant.signed_value;
    if (value.constant.kind == openxaml::xbf::ConstantKind::Color)
        std::cout << ":0x" << std::hex << value.constant.unsigned_value << std::dec;
    for (float number : value.constant.floats) std::cout << ':' << number;
    std::cout << '\n';
}

int SelfTest() {
    const auto document = openxaml::xbf::Read(MinimalXbf());
    if (document.metadata.major != 2 || document.metadata.minor != 1 ||
        document.substreams.size() != 1 || document.substreams[0].nodes.size() != 1 ||
        document.substreams[0].nodes[0].type != openxaml::xbf::NodeType::PushScope) {
        std::cerr << "minimal XBF did not round-trip through the reader\n";
        return 1;
    }
    auto malformed = MinimalXbf();
    malformed[0] = 0;
    try {
        (void)openxaml::xbf::Read(malformed);
        std::cerr << "malformed XBF magic was accepted\n";
        return 1;
    } catch (const openxaml::xbf::Error&) {
    }
    std::cout << "XBF reader self-test passed\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) return SelfTest();
    bool dump = false;
    bool dump_all = false;
    int first_file = 1;
    if (std::string(argv[1]) == "--dump") {
        dump = true;
        first_file = 2;
    } else if (std::string(argv[1]) == "--dump-all") {
        dump = true;
        dump_all = true;
        first_file = 2;
    }
    if (first_file == argc) {
        std::cerr << "--dump requires at least one XBF path\n";
        return 2;
    }
    std::size_t files = 0;
    std::size_t nodes = 0;
    try {
        for (int index = first_file; index < argc; ++index) {
            const auto document = openxaml::xbf::ReadFile(argv[index]);
            if (document.metadata.major != 2 || document.metadata.minor != 1)
                throw std::runtime_error("expected XBF 2.1: " + std::string(argv[index]));
            if (document.substreams.empty())
                throw std::runtime_error("XBF has no node streams: " + std::string(argv[index]));
            for (const auto& stream : document.substreams) nodes += stream.nodes.size();
            const auto graph = openxaml::xbf::WriteObjectGraph(document);
            if (!graph || graph->type.empty() || openxaml::xbf::CountObjects(graph) == 0)
                throw std::runtime_error("XBF object graph is empty: " + std::string(argv[index]));
            if (dump) DumpObject(graph, 0);
            if (dump_all) {
                for (std::size_t stream = 1; stream < document.substreams.size(); ++stream) {
                    std::cout << "--- substream " << stream << " ---\n";
                    try {
                        DumpObject(openxaml::xbf::WriteObjectGraph(document, stream), 0);
                    } catch (const std::exception& error) {
                        std::cout << "<unmaterialized: " << error.what() << ">\n";
                    }
                }
            }
            ++files;
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "parsed " << files << " XBF 2.1 files and " << nodes << " nodes\n";
    return 0;
}
