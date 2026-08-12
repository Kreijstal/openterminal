// The XBF loader's gate: the same markup, both ways, and the numbers have to
// match.
//
// Every corpus case is a XAML document whose layout is already verified against
// recorded oracle measurements. Compiling that document with the SDK's real
// genxbf and reading it back through the XBF loader has to produce the same
// tree of numbers as parsing the text did -- byte for byte, on every case the
// compiler accepts. That is a gate with no new expectations in it: the only way
// to pass is to decode the format correctly, and the only thing it can be
// wrong about is itself.
//
// Usage: xbf_equivalence <cases-dir> <xbf-dir> [fonts-dir] [theme-resources]
// Writes a JSON summary on stdout. Exit 0 when everything that compiled
// matched, 1 when anything did not, 2+ for a usage or environment fault.

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "element.h"
#include "fonts.h"
#include "json.h"
#include "markup.h"
#include "measure_report.h"
#include "resources.h"
#include "text.h"
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

// One case measured, or the reason it was not.
struct Measurement {
    bool ok = false;
    std::string error;
    std::string tree;
};

Measurement Measure(std::unique_ptr<Element> root, const Size& available) {
    Measurement result;
    root->Measure(available);
    const Size desired = root->desired_size();
    // An infinite final rect is not a legal arrange input, so an unbounded axis
    // falls back to what the element asked for -- the same rule measure_cases
    // and the oracle probe use.
    root->Arrange({0.0, 0.0, std::isinf(available.width) ? desired.width : available.width,
                   std::isinf(available.height) ? desired.height : available.height});
    std::vector<std::string> lines;
    WalkTree(*root, "/" + root->TypeName(), lines);
    std::ostringstream out;
    for (const std::string& line : lines) out << line << "\n";
    result.tree = out.str();
    result.ok = true;
    return result;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: xbf_equivalence <cases-dir> <xbf-dir> [fonts-dir] [theme-resources]\n";
        return 2;
    }
    const fs::path cases = argv[1];
    const fs::path xbf_dir = argv[2];
    const fs::path fonts = argc >= 4 ? fs::path(argv[3]) : cases.parent_path() / "fonts";
    const fs::path theme_resources =
        argc >= 5 ? fs::path(argv[4]) : cases.parent_path() / "theme-resources";

    if (!fs::exists(cases)) {
        std::cerr << "no such directory: " << cases.string() << "\n";
        return 4;
    }
    if (!fs::exists(xbf_dir)) {
        std::cerr << "no such directory: " << xbf_dir.string() << "\n";
        return 4;
    }

    try {
        LoadFontDirectory(FontLibrary::Default(), fonts.string());
    } catch (const std::exception& e) {
        std::cerr << "cannot load font metrics from " << fonts.string() << ": " << e.what() << "\n";
        return 4;
    }
    try {
        // The probe host's ceiling, like every host the corpus is measured
        // in: the oracle has no XamlControlsResources merged, so neither does
        // any run compared against it. See LoadThemeResources in resources.h.
        LoadThemeResources(ThemeResourceLibrary::Default(), theme_resources.string(),
                           ResourceLayer::GlobalThemeResources);
    } catch (const std::exception& e) {
        std::cerr << "cannot load theme resources from " << theme_resources.string() << ": "
                  << e.what() << "\n";
        return 4;
    }

    std::vector<fs::path> files;
    for (const auto& entry : fs::recursive_directory_iterator(cases)) {
        if (entry.path().extension() == ".json") files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());

    int considered = 0;
    int identical = 0;
    // Of the cases that agreed, how many agreed on a *measurement* rather than
    // on a refusal. Both are agreement and both are reported, because "the two
    // paths refused identically" is a much weaker statement than "the two paths
    // measured the same tree", and a summary that blurred them could hide a
    // corpus that stopped measuring.
    int identical_measured = 0;
    std::vector<std::string> mismatched;
    std::vector<std::string> refused;

    for (const fs::path& file : files) {
        const JsonValue document = ParseJson(Slurp(file));
        const std::string id = document.At("id").string;
        const fs::path compiled = xbf_dir / (id + ".xbf");
        // A case the compiler rejected has no .xbf. That is not this tool's
        // finding to report -- the driver already recorded the compiler's own
        // error for it -- so it is passed over here rather than counted.
        if (!fs::exists(compiled)) continue;
        ++considered;

        const JsonValue& environment = document.At("environment");
        if (environment.Has("theme")) {
            ThemeResourceLibrary::Default().SetActiveTheme(environment.At("theme").string);
        }
        const JsonValue& extent = environment.At("available_size");
        if (extent.array.size() != 2) {
            std::cerr << id << ": available_size needs two entries\n";
            return 4;
        }
        const Size available{ReadExtent(extent.array[0]), ReadExtent(extent.array[1])};

        Measurement from_text;
        try {
            from_text = Measure(LoadMarkup(document.At("markup").string), available);
        } catch (const std::exception& e) {
            from_text.error = e.what();
        }

        Measurement from_xbf;
        try {
            from_xbf = Measure(LoadXbf(Slurp(compiled)), available);
        } catch (const std::exception& e) {
            from_xbf.error = e.what();
        }

        // The text path failing is a fact about the corpus, not about the
        // loader. What the gate asks is only that the two paths agree, so a
        // case the text path refuses must be refused identically -- and a case
        // it measures must be measured identically.
        if (!from_text.ok && !from_xbf.ok) {
            if (from_text.error == from_xbf.error) {
                ++identical;
            } else {
                mismatched.push_back(id + ": text refused with \"" + from_text.error +
                                     "\" but XBF refused with \"" + from_xbf.error + "\"");
            }
            continue;
        }
        if (!from_xbf.ok) {
            refused.push_back(id + ": " + from_xbf.error);
            continue;
        }
        if (!from_text.ok) {
            mismatched.push_back(id + ": text refused with \"" + from_text.error +
                                 "\" but XBF measured");
            continue;
        }
        if (from_text.tree == from_xbf.tree) {
            ++identical;
            ++identical_measured;
            continue;
        }
        mismatched.push_back(id + ": the two paths measured differently");
    }

    const auto array = [](const std::vector<std::string>& items) {
        std::ostringstream out;
        out << "[";
        for (size_t i = 0; i < items.size(); ++i) {
            out << (i ? ",\n    \"" : "\n    \"") << JsonEscape(items[i]) << "\"";
        }
        out << (items.empty() ? "]" : "\n  ]");
        return out.str();
    };

    std::cout << "{\n  \"cases\": " << considered << ",\n  \"identical\": " << identical
              << ",\n  \"identical_measured\": " << identical_measured
              << ",\n  \"identical_refusal\": " << (identical - identical_measured)
              << ",\n  \"mismatched\": " << array(mismatched)
              << ",\n  \"loader_refused\": " << array(refused) << "\n}\n";

    return (mismatched.empty() && refused.empty()) ? 0 : 1;
}
