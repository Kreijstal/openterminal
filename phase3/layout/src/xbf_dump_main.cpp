// What the loader makes of a compiled page.
//
// Terminal ships its UI as .xbf, and those files use most of the format the
// corpus never reaches: deferred sections, x:Bind connection ids, types from
// TerminalApp's own metadata provider. This reports, per file, either the
// markup the loader reconstructed or the named reason it could not -- so the
// boundary between what loads and what does not is a table rather than an
// impression, and each named reason is a line item for the wave that clears it.
//
// Usage: xbf_dump <file.xbf>...    JSON on stdout, one entry per file.

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "json.h"
#include "markup.h"
#include "markup_tree.h"
#include "xbf.h"
#include "xbf_markup.h"

namespace fs = std::filesystem;
using namespace openxaml;

namespace {

std::string Slurp(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: xbf_dump <file.xbf>...\n";
        return 2;
    }
    std::cout << "[\n";
    for (int i = 1; i < argc; ++i) {
        const fs::path path = argv[i];
        std::ostringstream entry;
        entry << "  {\"file\": \"" << JsonEscape(path.filename().string()) << "\"";

        std::string bytes;
        try {
            bytes = Slurp(path);
        } catch (const std::exception& e) {
            entry << ", \"container\": \"unreadable: " << JsonEscape(e.what()) << "\"}";
            std::cout << entry.str() << (i + 1 < argc ? ",\n" : "\n");
            continue;
        }

        // Three questions, answered separately, because a file can pass one and
        // fail the next and the difference is the whole point of the table:
        // does the container parse, does the node stream decode, and does the
        // decoded stream reconstruct into markup this runtime can build.
        try {
            const xbf::Document document = xbf::Read(bytes);
            entry << ", \"version\": \"" << document.major_version << "."
                  << document.minor_version << "\"";
            entry << ", \"bytes\": " << bytes.size();
            entry << ", \"strings\": " << document.strings.size();
            entry << ", \"types\": " << document.types.size();
            entry << ", \"properties\": " << document.properties.size();
            entry << ", \"sub_streams\": " << document.sub_streams.size();
            std::size_t nodes = 0;
            std::vector<std::string> truncations;
            for (const xbf::SubStream& stream : document.sub_streams) {
                nodes += stream.nodes.size();
                if (!stream.truncated_because.empty()) {
                    truncations.push_back(stream.truncated_because);
                }
            }
            entry << ", \"nodes\": " << nodes;
            entry << ", \"container\": \"parsed\"";
            entry << ", \"node_stream\": \""
                  << (truncations.empty() ? "decoded" : "partly decoded") << "\"";
            if (!truncations.empty()) {
                entry << ", \"undecoded\": \"" << JsonEscape(truncations.front()) << "\"";
            }
            try {
                const std::string markup = XbfToMarkup(document);
                ParseMarkup(markup);
                entry << ", \"tree\": \"built\"";
                entry << ", \"markup_bytes\": " << markup.size();
            } catch (const std::exception& e) {
                entry << ", \"tree\": \"refused\", \"reason\": \"" << JsonEscape(e.what()) << "\"";
            }
        } catch (const std::exception& e) {
            entry << ", \"container\": \"refused\", \"reason\": \"" << JsonEscape(e.what())
                  << "\"";
        }
        entry << "}";
        std::cout << entry.str() << (i + 1 < argc ? ",\n" : "\n");
    }
    std::cout << "]\n";
    return 0;
}
