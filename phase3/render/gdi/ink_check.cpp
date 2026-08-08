// Does the box the measurement path derived actually contain the glyphs?
//
// Every text case in the corpus measures in Segoe UI, which is not
// redistributable and is on no machine this repository builds on. So the corpus
// cannot answer this question here: the GDI harness refuses all 112 of them by
// name, because substituting another face would put ink in positions nothing
// measured. That refusal is correct and it leaves a real claim unchecked.
//
// This checks it with a font that *is* available and whose metrics come from
// the same harvester the corpus uses: Cascadia Mono, out of the pinned Terminal
// checkout, which phase3's own CI already harvests for the level 7 case that
// measures in it. The font is added to this process privately, the text is laid
// out by the verified layout core against the harvested metrics, and the ink is
// drawn by GDI at the origins that layout produced.
//
// What this is not: an oracle case. Nothing recorded says what the real runtime
// does with these strings. What it is: the one property that needs no oracle --
// the advances this project sums have to cover the glyphs the platform draws in
// the same font, or the run box is a wrong number. The checker holds the dumps
// to containment and to nothing else.

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "case_runner.h"
#include "display_list.h"
#include "fonts.h"
#include "gdi_target.h"
#include "grid.h"
#include "json.h"
#include "surface.h"
#include "text.h"

namespace fs = std::filesystem;
using namespace openxaml;
using namespace openxaml::render;

namespace {

std::wstring Widen(const std::string& utf8) {
    const int needed = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                                           static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), out.data(),
                        needed);
    return out;
}

struct Sample {
    const char* id;
    const char* text;
    double size;
};

}  // namespace

int main(int argc, char** argv) {
    if (argc < 5) {
        std::cerr << "usage: ink_check <font-file> <family> <fonts-dir> <out-dir>\n";
        return 2;
    }
    const std::string font_file = argv[1];
    const std::string family = argv[2];
    const fs::path fonts = argv[3];
    const fs::path out_dir = argv[4];

    // Private to this process: nothing is installed on the machine, and the
    // font is gone when the process ends.
    if (AddFontResourceExW(Widen(font_file).c_str(), FR_PRIVATE, nullptr) == 0) {
        std::cerr << "GDI would not load " << font_file << "\n";
        return 4;
    }
    if (!FontFamilyInstalled(family)) {
        std::cerr << "the font file loaded but GDI has no family called \"" << family << "\"\n";
        return 4;
    }
    try {
        LoadFontDirectory(FontLibrary::Default(), fonts.string());
    } catch (const std::exception& e) {
        std::cerr << "cannot load font metrics: " << e.what() << "\n";
        return 4;
    }
    if (FontLibrary::Default().Find(family) == nullptr) {
        std::cerr << "no harvested metrics for \"" << family << "\"; there is nothing to hold "
                  << "the ink to\n";
        return 4;
    }

    static const Sample kSamples[] = {
        {"ink-mono-terminal-14", "Terminal", 14.0},
        {"ink-mono-terminal-24", "Terminal", 24.0},
        {"ink-mono-mixed-12", "Wq gjpy #@%&", 12.0},
        {"ink-mono-narrow-18", "lIi1|", 18.0},
        {"ink-mono-wide-20", "MMMWWW", 20.0},
        {"ink-mono-pangram-14", "The quick brown fox jumps over the lazy dog", 14.0},
    };

    fs::create_directories(out_dir);
    int written = 0;
    for (const Sample& sample : kSamples) {
        // A generous panel with the text in its top-left corner. The panel is
        // bigger than the run on purpose: ink that overflowed the measured box
        // has somewhere to land, so the checker can see it instead of a clip
        // hiding it.
        auto grid = std::make_unique<Grid>();
        auto text = std::make_unique<TextBlock>();
        text->set_text(sample.text);
        text->set_font_family(family);
        text->set_font_size(sample.size);
        text->set_horizontal_alignment(HorizontalAlignment::Left);
        text->set_vertical_alignment(VerticalAlignment::Top);
        grid->AddChild(std::move(text));

        const Size available{900.0, 120.0};
        std::unique_ptr<Element> root = std::move(grid);
        try {
            root->Measure(available);
            root->Arrange({0.0, 0.0, available.width, available.height});
        } catch (const std::exception& e) {
            std::cerr << sample.id << ": " << e.what() << "\n";
            return 5;
        }

        CaseResult result;
        result.id = sample.id;
        result.list = Build(*root, available);
        result.has_surface = true;

        const PixelRect box = SnapRect(Rect{0.0, 0.0, available.width, available.height});
        DibTarget target(box.right, box.bottom);
        if (!target.valid()) {
            std::cerr << "no DIB\n";
            return 5;
        }
        GdiTextBackend backend(target);
        Surface surface = PaintCase(result, &backend);

        std::ofstream(out_dir / (std::string(sample.id) + ".ppm"), std::ios::binary)
            << ToPpm(surface);
        std::ofstream(out_dir / (std::string(sample.id) + ".json"), std::ios::binary)
            << SidecarJson(result, surface, "gdi");
        ++written;

        for (const std::string& failure : result.text_failures)
            std::cerr << sample.id << ": " << failure << "\n";
        if (!result.text_failures.empty()) return 5;
    }

    std::cout << written << " ink sample(s) painted in \"" << family << "\"\n";
    return 0;
}
