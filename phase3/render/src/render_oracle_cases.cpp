// Render the focused native-oracle corpus into the oracle's exact surface
// contract: tightly packed, top-down, premultiplied BGRA8 with transparent
// initial contents. This is separate from render_cases.cpp because that
// harness deliberately uses a reserved opaque backdrop for its independent
// rectangle-recovery proof.

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "case_runner.h"
#include "fonts.h"
#include "json.h"
#include "resources.h"
#include "text.h"

namespace fs = std::filesystem;
using namespace openxaml;
using namespace openxaml::render;

namespace {

std::string Slurp(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

void Write(const fs::path& path, const std::string& content) {
    std::ofstream output(path, std::ios::binary);
    output << content;
    if (!output) throw std::runtime_error("cannot write " + path.string());
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: render_oracle_cases <cases-dir> <out-dir> [fonts-dir] "
                     "[theme-resources]\n";
        return 2;
    }
    const fs::path cases = argv[1];
    const fs::path out_dir = argv[2];
    const fs::path fonts = argc >= 4 ? fs::path(argv[3]) : cases.parent_path() / "fonts";
    const fs::path themes = argc >= 5 ? fs::path(argv[4])
                                      : cases.parent_path() / "theme-resources";
    if (!fs::is_directory(cases)) {
        std::cerr << "no case directory: " << cases.string() << '\n';
        return 4;
    }

    std::vector<fs::path> files;
    for (const auto& entry : fs::recursive_directory_iterator(cases))
        if (entry.path().extension() == ".json") files.push_back(entry.path());
    std::sort(files.begin(), files.end());
    if (files.empty()) {
        std::cerr << "no cases found under " << cases.string() << '\n';
        return 4;
    }

    try {
        std::cerr << "font metrics loaded: "
                  << LoadFontDirectory(FontLibrary::Default(), fonts.string()) << '\n';
        std::cerr << "theme resources loaded: "
                  << LoadThemeResources(ThemeResourceLibrary::Default(), themes.string())
                  << '\n';
    } catch (const std::exception& error) {
        std::cerr << "cannot load renderer environment: " << error.what() << '\n';
        return 4;
    }

    fs::create_directories(out_dir / "trees");
    const Color transparent{0, 0, 0, 0};
    int rendered = 0, refused = 0, load_errors = 0;
    try {
        for (const fs::path& file : files) {
            CaseResult result = LayOutCase(Slurp(file));
            if (result.id.empty()) result.id = file.stem().string();
            if (!result.load_error.empty()) {
                ++load_errors;
                std::ostringstream output;
                output << "{\n \"schema_version\": 1,\n \"case_id\": \""
                       << JsonEscape(result.id) << "\",\n \"load_error\": \""
                       << JsonEscape(result.load_error) << "\"\n}\n";
                Write(out_dir / (result.id + ".json"), output.str());
                continue;
            }
            Surface surface = PaintCase(result, nullptr, transparent);
            Write(out_dir / (result.id + ".bgra"), ToBgra(surface));
            Write(out_dir / (result.id + ".json"),
                  SidecarJson(result, surface, "oracle-software", transparent));
            Write(out_dir / "trees" / (result.id + ".json"), result.tree_json);
            if (result.list.refusals.empty()) ++rendered;
            else ++refused;
        }
    } catch (const std::exception& error) {
        std::cerr << "oracle render harness failed: " << error.what() << '\n';
        return 5;
    }
    std::cout << rendered << " rendered, " << refused << " refused, " << load_errors
              << " load errors\n";
    return 0;
}
