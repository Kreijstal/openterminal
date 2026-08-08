// What the XBF container and node stream say, byte by byte.
//
// The corpus gate (phase4/scripts/xbf_equivalence.py) proves the loader agrees
// with the text path over ~1000 real documents, which is the strong check --
// but it needs the harvested SDK compiler under Wine, and it can only ever show
// files that genxbf produced. Two things it cannot do live here: hold the
// reader to *rejecting* a malformed file, and run at all on a machine with no
// Wine on it.
//
// Fixture policy. Nothing binary is committed. The fixture below is assembled
// byte by byte in the test with a comment on every field, and the bytes it
// assembles are the exact bytes genxbf 10.0.26100.0 emitted for
//
//     <ContentControl xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation">
//       <Border/>
//     </ContentControl>
//
// -- corpus case L0-props-content-stretch, 317 bytes, source hash
// CF4C3DC689AD4D97CA9702FAD9D0349DAD138B64AA0B4C1B66D9E032E074D974. It was
// checked against that output when it was written; the equivalence gate is what
// keeps it checked, because any drift in what the compiler emits shows up there
// first. The variants below mutate one field each, so a failure names the field.

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "markup_tree.h"
#include "xbf.h"
#include "xbf_markup.h"

using namespace openxaml;

namespace {

int failures = 0;

// Not assert(): a condition here may carry the side effect the next line
// depends on, and NDEBUG would erase it along with the check.
void Check(bool condition, const char* what, int line) {
    if (condition) return;
    std::cerr << "xbf_test.cpp:" << line << ": CHECK failed: " << what << "\n";
    ++failures;
}

#define CHECK(condition) Check(static_cast<bool>(condition), #condition, __LINE__)

void Report(const std::string& what, const std::string& saw) {
    std::cerr << "  " << what << ": " << saw << "\n";
}

// --- fixture assembly ---------------------------------------------------------

void U8(std::string& out, std::uint8_t value) { out.push_back(static_cast<char>(value)); }

void U16(std::string& out, std::uint16_t value) {
    U8(out, static_cast<std::uint8_t>(value & 0xff));
    U8(out, static_cast<std::uint8_t>(value >> 8));
}

void U32(std::string& out, std::uint32_t value) {
    U16(out, static_cast<std::uint16_t>(value & 0xffff));
    U16(out, static_cast<std::uint16_t>(value >> 16));
}

void U64(std::string& out, std::uint64_t value) {
    U32(out, static_cast<std::uint32_t>(value & 0xffffffffu));
    U32(out, static_cast<std::uint32_t>(value >> 32));
}

// A counted UTF-16LE string. The metadata stream form appends a null since
// version 2.1 so the runtime can hand the buffer out without copying it; the
// node stream form does not.
void Utf16(std::string& out, const std::string& ascii, bool terminated) {
    U32(out, static_cast<std::uint32_t>(ascii.size()));
    for (const char c : ascii) U16(out, static_cast<std::uint8_t>(c));
    if (terminated) U16(out, 0);
}

// The 15-bit index plus the "this is a stable platform index" bit that
// PersistedXamlNode2 packs into one 16-bit word.
void Ref(std::string& out, std::uint16_t index, bool trusted) {
    U16(out, static_cast<std::uint16_t>(index | (trusted ? 0x8000u : 0u)));
}

constexpr const char* kPresentationNs = "http://schemas.microsoft.com/winfx/2006/xaml/presentation";

// Stable indexes, from dxaml/xcp/core/Parser/StableXbfIndexes.g.h in the pinned
// MIT checkout and confirmed by the compiler's output above.
constexpr std::uint16_t kBorder = 286;
constexpr std::uint16_t kContentControl = 435;
constexpr std::uint16_t kContentControlContent = 832;

struct FixtureOptions {
    std::uint32_t major = 2;
    std::uint32_t minor = 1;
    // Break one thing at a time; each of these is a separate test below.
    bool wrong_magic = false;
    bool wrong_declared_sizes = false;
    bool wrong_string_table_offset = false;
    bool trailing_metadata = false;
    // Replace the ContentControl with a stable index nothing implements.
    bool unimplemented_type = false;
    // Replace the last instruction with a byte no node type uses.
    bool undefined_node_type = false;
};

std::string Fixture(const FixtureOptions& options = {}) {
    // --- metadata stream ---
    std::string metadata;
    U32(metadata, options.major);  // major file version
    U32(metadata, options.minor);  // minor file version

    // The six table offsets are positions *within the metadata stream*, and the
    // reader checks each one against where it actually arrived. They are
    // computed here rather than written as constants so a change to the tables
    // below cannot leave a stale number behind -- except where a test asks for
    // exactly that.
    const std::uint64_t header_end = 8 + 6 * 8 + 64;  // version + offsets + hash
    std::string strings;
    U32(strings, 1);                          // one string in the table
    Utf16(strings, kPresentationNs, true);    // [0] the default xmlns
    std::string assemblies;
    U32(assemblies, 0);  // no assemblies: every type used is a platform type
    std::string type_namespaces;
    U32(type_namespaces, 0);
    std::string types;
    U32(types, 0);  // no table entries: both types are stable indexes
    std::string properties;
    U32(properties, 0);
    std::string xml_namespaces;
    U32(xml_namespaces, 1);  // one xml namespace ...
    U32(xml_namespaces, 0);  // ... whose uri is string [0]

    const std::uint64_t string_offset = header_end;
    const std::uint64_t assembly_offset = string_offset + strings.size();
    const std::uint64_t type_namespace_offset = assembly_offset + assemblies.size();
    const std::uint64_t type_offset = type_namespace_offset + type_namespaces.size();
    const std::uint64_t property_offset = type_offset + types.size();
    const std::uint64_t xml_namespace_offset = property_offset + properties.size();

    U64(metadata, options.wrong_string_table_offset ? string_offset + 1 : string_offset);
    U64(metadata, assembly_offset);
    U64(metadata, type_namespace_offset);
    U64(metadata, type_offset);
    U64(metadata, property_offset);
    U64(metadata, xml_namespace_offset);
    // The source hash, 64 bytes of ASCII hex as genxbf writes it.
    metadata += "CF4C3DC689AD4D97CA9702FAD9D0349DAD138B64AA0B4C1B66D9E032E074D974";

    metadata += strings;
    metadata += assemblies;
    metadata += type_namespaces;
    metadata += types;
    metadata += properties;
    metadata += xml_namespaces;
    if (options.trailing_metadata) metadata += '\0';

    // --- node stream ---
    // The instructions the compiler recorded while it walked the markup. Note
    // that the tree is not written down: it is replayed. The Border is built,
    // finished, and only then assigned to the ContentControl's Content.
    std::string nodes;
    U8(nodes, 18);                    // PushScopeAddNamespace
    Ref(nodes, 0, false);             //   xml namespace [0]
    Utf16(nodes, "", false);          //   under no prefix, i.e. the default xmlns
    U8(nodes, 23);                    // CreateTypeBeginInit
    Ref(nodes, options.unimplemented_type ? 515 : kContentControl, true);
    U8(nodes, 20);                    // PushScopeCreateTypeBeginInit
    Ref(nodes, kBorder, true);
    U8(nodes, 33);                    // EndInitPopScope -- the Border is finished
    U8(nodes, 7);                     // SetValue
    Ref(nodes, kContentControlContent, true);
    U8(nodes, options.undefined_node_type ? 200 : 33);  // EndInitPopScope -- the root

    // The line-number stream: one (stream offset delta, line delta, column
    // delta) triple per node whose position moved, each 7-bit encoded, with the
    // line and column deltas zig-zagged so a negative delta stays small. These
    // are the bytes the compiler emitted; nothing here reads them, and the
    // reader only has to know how long they are.
    const std::uint8_t line_bytes[] = {0x07, 0x02, 0x22, 0x03, 0x00, 0x88, 0x01};
    std::string lines(reinterpret_cast<const char*>(line_bytes), sizeof(line_bytes));

    std::string node_stream;
    U32(node_stream, 1);                                       // one sub-stream ...
    U32(node_stream, 0);                                       // ... nodes start at 0 ...
    U32(node_stream, static_cast<std::uint32_t>(nodes.size()));  // ... line data after them
    node_stream += nodes;
    node_stream += lines;

    // --- container ---
    std::string out;
    out += options.wrong_magic ? std::string("XBG\0", 4) : std::string("XBF\0", 4);
    U32(out, static_cast<std::uint32_t>(metadata.size() + (options.wrong_declared_sizes ? 1 : 0)));
    U32(out, static_cast<std::uint32_t>(node_stream.size()));
    out += metadata;
    out += node_stream;
    return out;
}

// --- the tests ----------------------------------------------------------------

void ContainerAndMetadata() {
    const xbf::Document document = xbf::Read(Fixture());
    CHECK(document.major_version == 2);
    CHECK(document.minor_version == 1);
    CHECK(document.strings.size() == 1);
    CHECK(document.strings[0] == kPresentationNs);
    CHECK(document.assemblies.empty());
    CHECK(document.types.empty());
    CHECK(document.properties.empty());
    CHECK(document.xml_namespaces.size() == 1);
    CHECK(document.hash.size() == 64);
    CHECK(document.hash.rfind("CF4C3DC6", 0) == 0);
    CHECK(document.sub_streams.size() == 1);
    // The line stream is not decoded, but its length has to come out right or
    // the sub-stream after it would start in the wrong place.
    CHECK(document.sub_streams[0].line_bytes == 7);
}

void NodeStream() {
    const xbf::Document document = xbf::Read(Fixture());
    if (document.sub_streams.size() != 1) return;
    const std::vector<xbf::Node>& nodes = document.sub_streams[0].nodes;
    CHECK(document.sub_streams[0].truncated_because.empty());
    CHECK(nodes.size() == 6);
    if (nodes.size() != 6) {
        Report("node count", std::to_string(nodes.size()));
        return;
    }
    CHECK(nodes[0].type == xbf::NodeType::PushScopeAddNamespace);
    CHECK(nodes[0].xml_namespace.index == 0);
    CHECK(!nodes[0].xml_namespace.trusted);
    CHECK(nodes[0].prefix.empty());
    // Offsets are what the line stream is indexed by, so they are part of the
    // decode and not bookkeeping.
    CHECK(nodes[0].offset == 0);

    CHECK(nodes[1].type == xbf::NodeType::CreateTypeBeginInit);
    CHECK(nodes[1].has_type);
    CHECK(nodes[1].type_ref.trusted);
    CHECK(nodes[1].type_ref.index == kContentControl);
    CHECK(nodes[1].offset == 7);

    CHECK(nodes[2].type == xbf::NodeType::PushScopeCreateTypeBeginInit);
    CHECK(nodes[2].type_ref.index == kBorder);

    CHECK(nodes[3].type == xbf::NodeType::EndInitPopScope);
    CHECK(!nodes[3].has_type);
    CHECK(!nodes[3].has_property);

    CHECK(nodes[4].type == xbf::NodeType::SetValue);
    CHECK(nodes[4].has_property);
    CHECK(nodes[4].property.trusted);
    CHECK(nodes[4].property.index == kContentControlContent);

    CHECK(nodes[5].type == xbf::NodeType::EndInitPopScope);
}

void Reconstruction() {
    const std::string markup = XbfToMarkup(Fixture());
    // The document the compiler was given, written back out. Attribute-free,
    // self-closing where the original was, and carrying the xmlns the node
    // stream declared.
    const std::string expected =
        std::string("<ContentControl xmlns=\"") + kPresentationNs + "\"><Border/></ContentControl>";
    CHECK(markup == expected);
    if (markup != expected) Report("reconstructed", markup);

    const MarkupNode node = ParseXbf(Fixture());
    CHECK(node.type == "ContentControl");
    CHECK(node.children.size() == 1);
    if (node.children.size() == 1) CHECK(node.children[0].type == "Border");
}

std::string Rejects(const FixtureOptions& options) {
    try {
        const std::string markup = XbfToMarkup(Fixture(options));
        return "accepted: " + markup;
    } catch (const std::exception& error) {
        return error.what();
    }
}

bool Mentions(const std::string& message, const std::string& fragment) {
    return message.find(fragment) != std::string::npos;
}

void Refusals() {
    // Every one of these is a fact the file states about itself. A reader that
    // shrugged any of them off would go on to decode plausible nonsense, which
    // is worse than stopping, because nonsense measures.
    FixtureOptions magic;
    magic.wrong_magic = true;
    const std::string bad_magic = Rejects(magic);
    CHECK(Mentions(bad_magic, "magic number"));
    if (!Mentions(bad_magic, "magic number")) Report("wrong magic", bad_magic);

    FixtureOptions sizes;
    sizes.wrong_declared_sizes = true;
    const std::string bad_sizes = Rejects(sizes);
    CHECK(Mentions(bad_sizes, "declares"));
    if (!Mentions(bad_sizes, "declares")) Report("wrong sizes", bad_sizes);

    FixtureOptions offset;
    offset.wrong_string_table_offset = true;
    const std::string bad_offset = Rejects(offset);
    CHECK(Mentions(bad_offset, "the string table starts at"));
    if (!Mentions(bad_offset, "the string table starts at")) Report("wrong offset", bad_offset);

    FixtureOptions trailing;
    trailing.trailing_metadata = true;
    const std::string bad_trailing = Rejects(trailing);
    CHECK(Mentions(bad_trailing, "after the last table"));
    if (!Mentions(bad_trailing, "after the last table")) Report("trailing", bad_trailing);

    // The version this reader implements is the one genxbf 10.0.26100.0 emits
    // for WinUI 2.8.4 and no other. A 2.0 file has no null terminators on its
    // metadata strings, so accepting it would mis-read every string it has.
    FixtureOptions older;
    older.minor = 0;
    const std::string bad_version = Rejects(older);
    CHECK(Mentions(bad_version, "XBF version 2.0 is not implemented"));
    if (!Mentions(bad_version, "XBF version 2.0 is not implemented")) {
        Report("version 2.0", bad_version);
    }

    FixtureOptions newer;
    newer.major = 3;
    CHECK(Mentions(Rejects(newer), "XBF version 3.1 is not implemented"));

    // An index this runtime has no name for is reported as the number it is.
    // Guessing a name would be worse than useless: it would name the wrong
    // element and measure it.
    FixtureOptions unknown_type;
    unknown_type.unimplemented_type = true;
    const std::string bad_type = Rejects(unknown_type);
    CHECK(Mentions(bad_type, "stable type index 515"));
    if (!Mentions(bad_type, "stable type index 515")) Report("unknown type", bad_type);

    FixtureOptions bad_node;
    bad_node.undefined_node_type = true;
    const std::string undefined = Rejects(bad_node);
    CHECK(Mentions(undefined, "node type 200"));
    if (!Mentions(undefined, "node type 200")) Report("undefined node type", undefined);
}

// The fixture claims to be genxbf's output. When the compiled corpus is on this
// machine, that claim is checked rather than believed; when it is not, this
// says so by name instead of passing quietly.
void FixtureIsWhatTheCompilerEmitted() {
    const char* from_environment = std::getenv("OPENXAML_XBF_CORPUS");
    const std::string directory =
        from_environment ? from_environment : "/tmp/openterminal-xbf-gate/xbf";
    const std::string path = directory + "/L0-props-content-stretch.xbf";
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::cerr << "xbf_test: skipped fixture/compiler byte comparison -- " << path
                  << " is not on this machine (run phase4/scripts/xbf_equivalence.py to "
                     "produce it)\n";
        return;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string compiled = buffer.str();
    const std::string fixture = Fixture();
    CHECK(fixture.size() == compiled.size());
    CHECK(fixture == compiled);
    if (fixture != compiled) {
        for (std::size_t i = 0; i < fixture.size() && i < compiled.size(); ++i) {
            if (fixture[i] != compiled[i]) {
                Report("first differing byte", std::to_string(i));
                break;
            }
        }
        Report("fixture bytes", std::to_string(fixture.size()));
        Report("compiler bytes", std::to_string(compiled.size()));
    }
}

void TruncatedInputs() {
    // A file cut short at every length has to fail, and never read past its own
    // end. This is the check that the bounds test in the cursor is real.
    const std::string whole = Fixture();
    for (std::size_t length = 0; length < whole.size(); ++length) {
        bool threw = false;
        try {
            xbf::Read(whole.substr(0, length));
        } catch (const std::exception&) {
            threw = true;
        }
        if (!threw) {
            std::cerr << "xbf_test.cpp: a " << length << "-byte prefix of the fixture was accepted"
                      << "\n";
            ++failures;
            return;
        }
    }
}

}  // namespace

int main() {
    ContainerAndMetadata();
    NodeStream();
    Reconstruction();
    Refusals();
    FixtureIsWhatTheCompilerEmitted();
    TruncatedInputs();
    if (failures == 0) std::cout << "xbf: ok\n";
    return failures == 0 ? 0 : 1;
}
