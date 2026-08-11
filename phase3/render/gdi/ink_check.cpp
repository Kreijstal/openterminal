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
// measures in it. Same file, not merely the same name: the harvested metrics
// record `CascadiaMono.ttf` at sha256 08026f61...59fa2, and that is the file
// this is pointed at, so the advances the box comes from and the outlines
// inside it are the same font's. The font is added to this process privately,
// the text is laid out by the verified layout core against the harvested
// metrics, and the ink is drawn by DirectWrite at the origins that layout
// produced.
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
#include "dwrite_text_provider.h"
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
    // Rule 7: text set through the Text property keeps unsnapped advances,
    // inline content gets them snapped to 1/300 of a DIP. The two are different
    // numbers -- 8.203125 against 8.203333 for Cascadia Mono at size 14 -- and
    // the corpus is mostly the snapped kind, so a check that only covered one
    // of them would be checking the shape the corpus does not have.
    bool from_property;
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
    //
    // Both loaders, because both subsystems are in this pipeline and they do
    // not share a font list. GDI's AddFontResourceEx is what makes the family
    // enumerable through EnumFontFamiliesEx below -- a cheap check that the
    // file is a font at all -- and it is *not* what puts glyphs on the surface:
    // the ink is drawn by DirectWrite (gdi_target.cpp's GdiTextBackend calls
    // DrawDirectWriteTextRun), whose system collection never sees a GDI private
    // face. Loading only the GDI side is how this tool spent a wave reporting
    // `DirectWrite could not resolve any requested family in "Cascadia Mono"`
    // while holding the real Cascadia Mono file in its hand.
    if (AddFontResourceExW(Widen(font_file).c_str(), FR_PRIVATE, nullptr) == 0) {
        std::cerr << "GDI would not load " << font_file << "\n";
        return 4;
    }
    if (!FontFamilyInstalled(family)) {
        std::cerr << "the font file loaded but GDI has no family called \"" << family << "\"\n";
        return 4;
    }
    std::string diagnostic;
    if (!AddPrivateDirectWriteFontFile(font_file, diagnostic)) {
        std::cerr << "DirectWrite would not take " << font_file << ": " << diagnostic << "\n";
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
        {"ink-mono-terminal-14", "Terminal", 14.0, false},
        {"ink-mono-terminal-24", "Terminal", 24.0, false},
        {"ink-mono-mixed-12", "Wq gjpy #@%&", 12.0, false},
        {"ink-mono-narrow-18", "lIi1|", 18.0, false},
        {"ink-mono-wide-20", "MMMWWW", 20.0, false},
        {"ink-mono-pangram-14", "The quick brown fox jumps over the lazy dog", 14.0, false},
        {"ink-mono-property-14", "Terminal", 14.0, true},
        {"ink-mono-property-pangram-20",
         "The quick brown fox jumps over the lazy dog", 20.0, true},
    };

    // Two phases, and the order is the whole argument this tool makes.
    //
    // Everything is laid out first, while the harvested metrics are the only
    // thing TextBlock can measure against. Installing the DirectWrite provider
    // redirects FontLibrary itself (layout/src/text.cpp:343), so a tool that
    // installed it before laying out would be checking DirectWrite's ink
    // against DirectWrite's own advances -- a tautology. And it has to be one
    // phase rather than a loop that lays out and paints a sample at a time,
    // because this tool does install the provider, to ask DirectWrite whether
    // the family arrived at all: installed between two samples, it would
    // measure the first against the harvest and the rest against the platform,
    // with no line of output saying which.
    const Size available{900.0, 120.0};
    std::vector<CaseResult> laid_out;
    for (const Sample& sample : kSamples) {
        // A generous panel with the text in its top-left corner. The panel is
        // bigger than the run on purpose: ink that overflowed the measured box
        // has somewhere to land, so the checker can see it instead of a clip
        // hiding it.
        auto grid = std::make_unique<Grid>();
        auto text = std::make_unique<TextBlock>();
        text->set_text(sample.text);
        text->set_text_from_property(sample.from_property);
        text->set_font_family(family);
        text->set_font_size(sample.size);
        text->set_horizontal_alignment(HorizontalAlignment::Left);
        text->set_vertical_alignment(VerticalAlignment::Top);
        grid->AddChild(std::move(text));

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
        laid_out.push_back(std::move(result));
    }

    if (!InstallDirectWriteRuntimeTextProvider(diagnostic)) {
        std::cerr << "no DirectWrite provider: " << diagnostic << "\n";
        return 4;
    }
    // Ask DirectWrite for the family before painting, so a file whose family
    // name is not the one that was asked for fails here, by name, instead of
    // surfacing as six dumps with no ink in them.
    {
        FontMetrics probe{};
        std::string resolved;
        diagnostic.clear();
        const std::shared_ptr<RuntimeTextProvider> provider = GetRuntimeTextProvider();
        if (!provider || !provider->ResolveFontMetrics(family, probe, resolved, diagnostic)) {
            std::cerr << "DirectWrite has no family called \"" << family
                      << "\" after the private load: " << diagnostic << "\n";
            return 4;
        }
        if (resolved != family) {
            std::cerr << "DirectWrite resolved \"" << family << "\" to \"" << resolved
                      << "\"; the ink would not be this font's\n";
            return 4;
        }
    }

    fs::create_directories(out_dir);
    int written = 0;
    for (CaseResult& result : laid_out) {
        const PixelRect box = SnapRect(Rect{0.0, 0.0, available.width, available.height});
        DibTarget target(box.right, box.bottom);
        if (!target.valid()) {
            std::cerr << "no DIB\n";
            return 5;
        }
        GdiTextBackend backend(target);
        Surface surface = PaintCase(result, &backend);

        std::ofstream(out_dir / (result.id + ".ppm"), std::ios::binary) << ToPpm(surface);
        std::ofstream(out_dir / (result.id + ".json"), std::ios::binary)
            << SidecarJson(result, surface, "gdi");
        ++written;

        for (const std::string& failure : result.text_failures)
            std::cerr << result.id << ": " << failure << "\n";
        if (!result.text_failures.empty()) return 5;
    }

    std::cout << written << " ink sample(s) painted in \"" << family << "\"\n";
    return 0;
}
