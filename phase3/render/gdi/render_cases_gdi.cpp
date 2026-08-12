// The same corpus render as ../src/render_cases.cpp, with glyphs.
//
// Identical in every other respect on purpose: same layout, same display list,
// same rectangle rasteriser, same dump format. So a diff between this run's
// dumps and the native run's is exactly the ink -- which is the one thing this
// binary adds and the one thing the project does not claim to own.

#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "case_runner.h"
#include "default_styles.h"
#include "fonts.h"
#include "gdi_target.h"
#include "glyph_outlines.h"
#include "json.h"
#include "markup.h"
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
    std::ofstream(path, std::ios::binary) << content;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> positional;
    std::string glyph_outlines;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--glyph-outlines") {
            if (index + 1 >= argc) {
                std::cerr << "--glyph-outlines needs a directory\n";
                return 2;
            }
            glyph_outlines = argv[++index];
            continue;
        }
        positional.push_back(argument);
    }
    if (positional.size() < 2) {
        std::cerr << "usage: render_cases_gdi <cases-dir> <out-dir> [fonts-dir]"
                     " [theme-resources] [--glyph-outlines <dir>]\n";
        return 2;
    }
    const fs::path cases = positional[0];
    const fs::path out_dir = positional[1];
    const fs::path fonts =
        positional.size() >= 3 ? fs::path(positional[2]) : cases.parent_path() / "fonts";
    const fs::path theme_resources =
        positional.size() >= 4 ? fs::path(positional[3])
                               : cases.parent_path() / "theme-resources";

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

    try {
        const int loaded = LoadFontDirectory(FontLibrary::Default(), fonts.string());
        std::cerr << "font metrics loaded: " << loaded << "\n";
    } catch (const std::exception& e) {
        std::cerr << "cannot load font metrics: " << e.what() << "\n";
        return 4;
    }
    try {
        // The probe host's ceiling, like the measurement path this pass is
        // held to -- see LoadThemeResources in resources.h.
        const int keys = LoadThemeResources(ThemeResourceLibrary::Default(),
                                            theme_resources.string(),
                                            ResourceLayer::GlobalThemeResources);
        std::cerr << "theme resources loaded: " << keys << " key(s)\n";
    } catch (const std::exception& e) {
        std::cerr << "cannot load theme resources: " << e.what() << "\n";
        return 4;
    }

    // The framework's own default styles, out of the same directory the theme
    // dictionary came from. The measurement path loads both halves of that
    // reconstruction, and this pass is held to arranging the very tree that
    // path measures -- a run with the dictionary but not the styles stretches
    // an element the measurement path had aligned.
    try {
        DefaultStyleReport style_report;
        const int default_styles = LoadDefaultStyles(
            DefaultStyleRegistry::Default(), theme_resources.string(), &style_report);
        std::cerr << "default styles loaded: " << default_styles << " built-in style(s)\n";
    } catch (const std::exception& e) {
        std::cerr << "cannot load default styles from " << theme_resources.string() << ": "
                  << e.what() << "\n";
        return 4;
    }

    // Recorded glyph outlines, into the process-wide library the GDI text
    // backend selects painters from: recorded outlines first for a family the
    // recording carries, DirectWrite for the rest. Loading nothing changes
    // nothing -- every run keeps going to DirectWrite.
    if (!glyph_outlines.empty()) {
        try {
            const int outline_families = LoadGlyphOutlineDirectory(
                GlyphOutlineLibrary::Default(), glyph_outlines);
            std::cerr << "glyph outlines loaded: " << outline_families
                      << " famil(ies) from " << glyph_outlines << "\n";
        } catch (const std::exception& e) {
            std::cerr << "cannot load glyph outlines from " << glyph_outlines << ": "
                      << e.what() << "\n";
            return 4;
        }
    }

    fs::create_directories(out_dir);
    fs::create_directories(out_dir / "trees");

    int painted = 0;
    int refused = 0;
    int not_laid_out = 0;
    int inked = 0;

    for (const fs::path& file : files) {
        CaseResult result = LayOutCase(Slurp(file));
        if (result.id.empty()) result.id = file.stem().string();
        if (!result.load_error.empty()) {
            ++not_laid_out;
            std::ostringstream out;
            out << "{\n \"schema_version\": " << kSidecarSchemaVersion
                << ",\n \"case_id\": \"" << JsonEscape(result.id)
                << "\",\n \"load_error\": \"" << JsonEscape(result.load_error) << "\"\n}\n";
            Write(out_dir / (result.id + ".json"), out.str());
            continue;
        }

        const PixelRect box =
            SnapRect(Rect{0.0, 0.0, result.list.surface.width, result.list.surface.height});
        Surface surface(box.right, box.bottom, BackdropColor());
        if (box.empty() || result.list.texts.empty()) {
            // Nothing for GDI to do: the rectangles are the software pass's and
            // there is no run to draw. Painting through a DIB anyway would only
            // add a way for the two backends to differ.
            surface = PaintCase(result, nullptr);
        } else {
            DibTarget target(box.right, box.bottom);
            if (!target.valid()) {
                result.text_failures.push_back(
                    "no device context: the DIB could not be created");
                surface = PaintCase(result, nullptr);
            } else {
                GdiTextBackend backend(target);
                surface = PaintCase(result, &backend);
                if (result.text_failures.empty()) ++inked;
            }
        }

        Write(out_dir / (result.id + ".ppm"), ToPpm(surface));
        Write(out_dir / (result.id + ".json"), SidecarJson(result, surface, "gdi"));
        Write(out_dir / "trees" / (result.id + ".json"), result.tree_json);
        if (result.list.refusals.empty() && result.text_failures.empty())
            ++painted;
        else
            ++refused;
    }

    std::cout << painted << " painted, " << refused << " refused by name, " << not_laid_out
              << " not laid out, " << inked << " with glyphs drawn\n";
    return 0;
}
