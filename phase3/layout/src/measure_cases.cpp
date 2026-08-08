// Runs the case corpus through this implementation and writes results in the
// shape the oracle probe writes, so check_layout.py can diff the two directly.
//
// The command line matches phase3/harness/xaml_probe.exe on purpose: same
// arguments, same output files. The only difference is that this one needs no
// Windows, no WinRT and no Wine, which is what makes it usable as a fast local
// loop against the recorded measurements.

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "default_styles.h"
#include "element.h"
#include "fonts.h"
#include "json.h"
#include "markup.h"
#include "resources.h"
#include "resw_strings.h"
#include "text.h"

namespace fs = std::filesystem;
using namespace openxaml;

namespace {

std::string Slurp(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// Fixed precision rather than shortest-round-trip, so two runs compare
// byte-for-byte and a diff is readable.
std::string Number(double value) {
    if (std::isinf(value)) return "\"Infinity\"";
    if (std::isnan(value)) return "\"NaN\"";
    std::ostringstream out;
    out << std::fixed << std::setprecision(4) << value;
    return out.str();
}

// JSON cannot spell infinity, so the corpus carries it as a string.
double ReadExtent(const JsonValue& value) {
    if (value.kind == JsonValue::Kind::String) {
        if (value.string == "Infinity") return kInfinity;
        throw JsonError("unexpected available_size value \"" + value.string + "\"");
    }
    if (value.kind != JsonValue::Kind::Number) throw JsonError("available_size is not a number");
    return value.number;
}

void Walk(const Element& element, const std::string& path, std::vector<std::string>& out) {
    const Size desired = element.desired_size();
    const Size actual = element.render_size();
    const Rect slot = element.layout_slot();

    std::ostringstream line;
    line << "  {\"path\": \"" << JsonEscape(path) << "\""
         << ", \"type\": \"" << JsonEscape(element.TypeName()) << "\""
         << ", \"desired\": [" << Number(desired.width) << ", " << Number(desired.height) << "]"
         << ", \"actual\": [" << Number(actual.width) << ", " << Number(actual.height) << "]"
         << ", \"offset\": [" << Number(slot.x) << ", " << Number(slot.y) << "]}";
    out.push_back(line.str());

    int index = 0;
    for (const Element* child : element.RecordedChildren()) {
        Walk(*child, path + "/" + child->TypeName() + "[" + std::to_string(index++) + "]", out);
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: measure_cases <cases-dir> <out-dir> [fonts-dir] [theme-resources]"
                     " [strings.json]\n";
        return 2;
    }
    const fs::path cases = argv[1];
    const fs::path out_dir = argv[2];
    // Harvested font metrics sit beside the corpus, so the default needs no
    // argument and the layout of the database stays the only thing to know.
    const fs::path fonts = argc >= 4 ? fs::path(argv[3]) : cases.parent_path() / "fonts";
    // The application dictionary, on the same convention: generated output that
    // sits beside the corpus. Absent on a bare checkout, which is not an error
    // -- every lookup that needed it then fails naming its key.
    const fs::path theme_resources =
        argc >= 5 ? fs::path(argv[4]) : cases.parent_path() / "theme-resources";
    // The x:Uid table, distilled out of a Terminal checkout's .resw files. No
    // default, unlike the fonts and the dictionary: the oracle probe has no
    // resource map, so a run that silently found a table would measure markup
    // the oracle cannot, and every x:Uid case would disagree for a reason
    // nothing reported. Last of the three because it is the only one with no
    // default, so nothing has to be spelled out to skip it.
    const fs::path strings_file = argc >= 6 ? fs::path(argv[5]) : fs::path();

    if (!fs::exists(cases)) {
        std::cerr << "no such directory: " << cases.string() << "\n";
        return 4;
    }

    std::vector<fs::path> files;
    for (const auto& entry : fs::recursive_directory_iterator(cases)) {
        if (entry.path().extension() == ".json") files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    if (files.empty()) {
        std::cerr << "no cases found under " << cases.string() << "\n";
        return 4;
    }

    // Not fatal when absent: the cases that need a font say so individually,
    // which names the real problem, where refusing to start would blame the
    // whole corpus for a level that may not even be under test.
    int loaded = 0;
    try {
        loaded = LoadFontDirectory(FontLibrary::Default(), fonts.string());
    } catch (const std::exception& e) {
        // A directory that is there but unreadable is not a per-case failure --
        // every text case would blame itself for it. Say it once, and stop.
        std::cerr << "cannot load font metrics from " << fonts.string() << ": " << e.what()
                  << "\n";
        return 4;
    }
    std::cerr << "font metrics loaded: " << loaded << " from " << fonts.string() << "\n";

    // Same rule, and for the same reason: a database that is there but
    // unreadable is one fault, not one per case that looks up a key.
    int keys = 0;
    try {
        keys = LoadThemeResources(ThemeResourceLibrary::Default(), theme_resources.string());
    } catch (const std::exception& e) {
        std::cerr << "cannot load theme resources from " << theme_resources.string() << ": "
                  << e.what() << "\n";
        return 4;
    }
    std::cerr << "theme resources loaded: " << keys << " key(s) from " << theme_resources.string()
              << "\n";

    // The framework's own default styles, out of the same directory. Same rule
    // again -- generated rather than committed, absent is not an error -- and
    // deliberately the same directory rather than a fourth argument: the two
    // databases are two halves of the same reconstruction and a run that had
    // one without the other would resolve a `{ThemeResource}` in a default
    // style against a dictionary that is missing its floor.
    DefaultStyleReport style_report;
    int default_styles = 0;
    try {
        default_styles = LoadDefaultStyles(DefaultStyleRegistry::Default(),
                                           theme_resources.string(), &style_report);
    } catch (const std::exception& e) {
        std::cerr << "cannot load default styles from " << theme_resources.string() << ": "
                  << e.what() << "\n";
        return 4;
    }
    std::cerr << "default styles loaded: " << default_styles << " built-in style(s), "
              << style_report.dropped_setters.size() << " setter(s) dropped, "
              << style_report.unknown_types.size() << " type(s) not implemented, "
              << style_report.held.size() << " setter(s) held\n";

    // Asked for and unreadable is fatal, for the reason the fonts are not: a
    // table names what every x:Uid in the run resolves to, so half a table is
    // not a weaker run, it is a different corpus.
    StringTable strings;
    if (!strings_file.empty()) {
        try {
            strings = LoadStringTable(strings_file.string());
        } catch (const std::exception& e) {
            std::cerr << "cannot load the string table " << strings_file.string() << ": "
                      << e.what() << "\n";
            return 4;
        }
        std::cerr << "x:Uid strings loaded: " << strings.size() << " uid(s) from "
                  << strings_file.string() << "\n";
    }

    fs::create_directories(out_dir);
    int measured = 0;
    int failed = 0;

    for (const fs::path& file : files) {
        std::string id = file.stem().string();
        std::string error;
        std::vector<std::string> tree;

        try {
            const JsonValue document = ParseJson(Slurp(file));
            id = document.At("id").string;
            const JsonValue& environment = document.At("environment");
            // The case pins a theme, so the application dictionary answers as
            // that theme. Only a database that has been loaded can refuse a
            // theme name; with none loaded this is inert, which is what keeps a
            // bare checkout behaving as it did.
            if (environment.Has("theme"))
                ThemeResourceLibrary::Default().SetActiveTheme(environment.At("theme").string);
            const JsonValue& extent = environment.At("available_size");
            if (extent.array.size() != 2) throw JsonError("available_size needs two entries");
            const Size available{ReadExtent(extent.array[0]), ReadExtent(extent.array[1])};

            std::unique_ptr<Element> root = LoadMarkup(document.At("markup").string, strings);
            root->Measure(available);
            const Size desired = root->desired_size();
            // An infinite final rect is not a legal arrange input, so an
            // unbounded axis falls back to what the element asked for. The
            // oracle probe does the same; if it did not, every Infinity case
            // would be arranged differently and none of them would compare.
            root->Arrange({0.0, 0.0, std::isinf(available.width) ? desired.width : available.width,
                           std::isinf(available.height) ? desired.height : available.height});
            Walk(*root, "/" + root->TypeName(), tree);
        } catch (const std::exception& e) {
            error = e.what();
        }

        std::ostringstream out;
        out << "{\n \"schema_version\": 1,\n \"case_id\": \"" << JsonEscape(id) << "\",\n";
        if (!error.empty()) {
            out << " \"error\": \"" << JsonEscape(error) << "\"\n}\n";
            ++failed;
        } else {
            out << " \"tree\": [\n";
            for (size_t i = 0; i < tree.size(); ++i) {
                out << tree[i] << (i + 1 < tree.size() ? ",\n" : "\n");
            }
            out << " ]\n}\n";
            ++measured;
        }
        std::ofstream(out_dir / (id + ".json"), std::ios::binary) << out.str();
    }

    std::cout << measured << " measured, " << failed << " failed\n";
    return 0;
}
