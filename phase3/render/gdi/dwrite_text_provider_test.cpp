#include "dwrite_text_provider.h"

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <string>

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", message);
    ++failures;
}

}  // namespace

int main(int argc, char** argv) {
    std::string diagnostic;

    // Optional: a font file and the family inside it, the way build_render.py
    // passes the ink font. A face handed to this process privately has to be
    // resolvable by the name that is written in it -- that is the whole of what
    // "the font is loaded" can mean for a family nothing installed.
    const char* private_font_file = argc > 2 ? argv[1] : nullptr;
    const std::string private_font_family = argc > 2 ? argv[2] : std::string();
    if (private_font_file) {
        diagnostic.clear();
        Check(openxaml::render::AddPrivateDirectWriteFontFile(private_font_file, diagnostic),
              diagnostic.empty() ? "add a private DirectWrite font file"
                                 : diagnostic.c_str());
    }

    Check(openxaml::render::InstallDirectWriteRuntimeTextProvider(diagnostic),
          diagnostic.empty() ? "install DirectWrite provider" : diagnostic.c_str());
    const std::shared_ptr<openxaml::RuntimeTextProvider> provider =
        openxaml::GetRuntimeTextProvider();
    Check(provider != nullptr, "runtime provider is visible to layout");
    if (!provider) return 1;

    if (GetEnvironmentVariableW(L"OPENXAML_FONT_ALIAS_MANIFEST", nullptr, 0)) {
        openxaml::RuntimeTextResult aliased_icon;
        diagnostic.clear();
        const bool alias_layout = provider->Layout(
            {"Segoe Fluent Icons, Segoe MDL2 Assets", u8"\uea18", 20.0,
             100.0, false, false}, aliased_icon, diagnostic);
        Check(alias_layout,
              diagnostic.empty() ? "private compatibility font alias lays out icon"
                                 : diagnostic.c_str());
        if (alias_layout) {
            Check(aliased_icon.resolved_family == "Symbols",
                  "private compatibility font resolves the declared alias family");
            Check(aliased_icon.advances.size() == 1,
                  "private compatibility font shapes one icon scalar");
        }

        // The alias is a second name for the face, not the only name it has.
        // A private font that answers only to the name some other product uses
        // is unusable by anything that knows what it actually loaded.
        openxaml::RuntimeTextResult own_name;
        diagnostic.clear();
        const bool own_name_layout = provider->Layout(
            {"Symbols", u8"\uea18", 20.0, 100.0, false, false}, own_name, diagnostic);
        Check(own_name_layout,
              diagnostic.empty() ? "private font resolves under its own family name"
                                 : diagnostic.c_str());
        if (own_name_layout) {
            Check(own_name.resolved_family == "Symbols",
                  "private font reports the family name written in the file");
            Check(own_name.advances == aliased_icon.advances,
                  "the alias and the file's own name reach the same face");
        }
    }

    if (private_font_file) {
        // The point of a privately added file: this family is on no machine
        // this repository builds on, and the process was handed the glyphs.
        openxaml::RuntimeTextResult private_measured;
        diagnostic.clear();
        const bool private_layout = provider->Layout(
            {private_font_family, "Terminal", 14.0, 200.0, false, false},
            private_measured, diagnostic);
        Check(private_layout,
              diagnostic.empty() ? "privately added font file lays out"
                                 : diagnostic.c_str());
        if (private_layout) {
            Check(private_measured.resolved_family == private_font_family,
                  "privately added font file resolves the requested family");
            Check(private_measured.advances.size() == 8,
                  "privately added font file shapes one advance per scalar");
            openxaml::render::Surface painted(200, 40, {255, 255, 255, 255});
            const std::vector<std::uint32_t> untouched = painted.pixels();
            openxaml::render::TextOp private_run;
            private_run.bounds = {0.0, 0.0, 200.0, 40.0};
            private_run.text = "Terminal";
            private_run.font_family = private_font_family;
            private_run.font_size = 14.0;
            private_run.baseline = private_measured.baseline;
            private_run.advances = private_measured.advances;
            diagnostic.clear();
            Check(openxaml::render::DrawDirectWriteTextRun(
                      painted, private_run, {255, 0, 0, 0}, diagnostic),
                  diagnostic.empty() ? "rasterize a privately added font file"
                                     : diagnostic.c_str());
            Check(painted.pixels() != untouched,
                  "privately added font file puts glyphs on the surface");
        }

        // The collection is built once. A file offered after that would never
        // arrive, so saying so is the only honest answer.
        diagnostic.clear();
        Check(!openxaml::render::AddPrivateDirectWriteFontFile(private_font_file,
                                                               diagnostic) &&
                  diagnostic.find("installed") != std::string::npos,
              "a private font offered after installation is a named refusal");
    }

    openxaml::RuntimeTextResult measured;
    diagnostic.clear();
    std::string test_family = "Segoe UI";
    const bool has_segoe = provider->Layout(
        {test_family, "Terminal", 14.0,
         std::numeric_limits<double>::infinity(), false, false}, measured, diagnostic);
    if (!has_segoe) {
        Check(diagnostic.find("could not resolve") != std::string::npos,
              "an absent Segoe UI installation is a named refusal");
        std::printf("SKIP: Segoe UI is not installed in this Wine prefix: %s\n",
                    diagnostic.c_str());
        test_family = "Liberation Sans";
        diagnostic.clear();
        Check(provider->Layout(
                  {test_family, "Terminal", 14.0,
                   std::numeric_limits<double>::infinity(), false, false},
                  measured, diagnostic),
              diagnostic.empty() ? "measure installed Latin test face"
                                 : diagnostic.c_str());
    }
    Check(measured.size.width > 0.0 && measured.size.height > 0.0 &&
              measured.baseline > 0.0 && measured.advances.size() == 8,
          "DirectWrite Latin returns size, baseline and one advance per scalar");

    openxaml::RuntimeTextResult explicit_second;
    diagnostic.clear();
    Check(provider->Layout(
              {"OpenXaml Definitely Missing, " + test_family, "Terminal", 14.0,
               100.0, false, false}, explicit_second, diagnostic) &&
              explicit_second.resolved_family == test_family,
          diagnostic.empty() ? "explicit family list reaches its next installed face"
                             : diagnostic.c_str());

    openxaml::RuntimeTextResult devanagari_face;
    diagnostic.clear();
    if (provider->Layout({"Noto Sans Devanagari", u8"क", 14.0, 100.0,
                          false, false}, devanagari_face, diagnostic)) {
        openxaml::RuntimeTextResult explicit_coverage;
        diagnostic.clear();
        Check(provider->Layout(
                  {test_family + ", Noto Sans Devanagari", u8"क", 14.0,
                   100.0, false, false}, explicit_coverage, diagnostic) &&
                  explicit_coverage.resolved_family == "Noto Sans Devanagari",
              diagnostic.empty() ? "explicit family list continues until glyph coverage"
                                 : diagnostic.c_str());
    } else {
        std::printf("SKIP: Noto Sans Devanagari coverage fixture is unavailable: %s\n",
                    diagnostic.c_str());
    }

    openxaml::TextBlock block;
    int layout_invalidations = 0;
    auto sink = std::make_shared<openxaml::RenderInvalidationSink>(
        [&](bool layout) {
            if (layout) ++layout_invalidations;
        });
    Check(block.AttachRenderInvalidationSink(sink), "attach TextBlock invalidation sink");
    block.set_font_family(test_family);
    block.set_font_size(14.0);
    block.set_text("Terminal");
    block.Measure({400.0, 100.0});
    const double first_width = block.desired_size().width;
    const int before_mutation = layout_invalidations;
    block.set_text("Terminal runtime");
    block.Measure({400.0, 100.0});
    Check(layout_invalidations == before_mutation + 1 &&
              block.desired_size().width > first_width,
          "text mutation invalidates layout and remeasures through DirectWrite");
    block.DetachRenderInvalidationSink(sink);
    sink->Close();

    openxaml::render::Surface opaque(200, 40, {255, 255, 255, 255});
    const std::vector<std::uint32_t> before = opaque.pixels();
    openxaml::render::TextOp run;
    run.bounds = {0.0, 0.0, 200.0, 40.0};
    run.text = "Terminal";
    run.font_family = test_family;
    run.font_size = 14.0;
    run.baseline = measured.baseline;
    run.advances = measured.advances;
    diagnostic.clear();
    Check(openxaml::render::DrawDirectWriteTextRun(
              opaque, run, {255, 0, 0, 0}, diagnostic),
          diagnostic.empty() ? "rasterize Segoe UI Latin" : diagnostic.c_str());
    Check(opaque.pixels() != before, "DirectWrite glyph run changes opaque frame pixels");
    bool all_opaque = true;
    for (std::uint32_t pixel : opaque.pixels()) all_opaque &= (pixel >> 24) == 255;
    Check(all_opaque, "ClearType rasterization preserves premultiplied opaque alpha");

    openxaml::RuntimeTextResult wrapped;
    diagnostic.clear();
    Check(provider->Layout({test_family, "Terminal runtime", 14.0, 50.0,
                            true, true}, wrapped, diagnostic),
          diagnostic.empty() ? "measure wrapped bold run" : diagnostic.c_str());
    openxaml::render::TextOp wrapped_run;
    wrapped_run.bounds = {0.0, 0.0, 50.0, std::ceil(wrapped.size.height) + 2.0};
    wrapped_run.text = "Terminal runtime";
    wrapped_run.font_family = test_family;
    wrapped_run.font_size = 14.0;
    wrapped_run.baseline = wrapped.baseline;
    wrapped_run.advances = wrapped.advances;
    wrapped_run.wrap = true;
    wrapped_run.bold = true;
    openxaml::render::Surface wrapped_surface(60, 80, {255, 255, 255, 255});
    diagnostic.clear();
    Check(openxaml::render::DrawDirectWriteTextRun(
              wrapped_surface, wrapped_run, {255, 180, 0, 0}, diagnostic),
          diagnostic.empty() ? "rasterize wrapped bold run with authored color"
                             : diagnostic.c_str());
    bool found_authored_red = false;
    for (std::uint32_t pixel : wrapped_surface.pixels()) {
        const unsigned red = (pixel >> 16) & 0xffu;
        const unsigned green = (pixel >> 8) & 0xffu;
        if (red > green) found_authored_red = true;
    }
    Check(found_authored_red, "DirectWrite run uses the authored foreground color");

    openxaml::render::Surface clipped(200, 40, {255, 255, 255, 255});
    run.has_clip = true;
    run.clip = {0.0, 0.0, 4.0, 40.0};
    diagnostic.clear();
    Check(openxaml::render::DrawDirectWriteTextRun(
              clipped, run, {255, 0, 0, 0}, diagnostic),
          diagnostic.empty() ? "rasterize clipped Segoe UI Latin" : diagnostic.c_str());
    bool escaped_clip = false;
    for (int y = 0; y < clipped.height(); ++y) {
        for (int x = 4; x < clipped.width(); ++x) {
            escaped_clip |= clipped.At(x, y) != 0xffffffffu;
        }
    }
    Check(!escaped_clip, "DirectWrite glyph pixels do not escape the retained clip");
    run.has_clip = false;

    openxaml::render::Surface sparse_alpha(200, 40, {255, 255, 255, 255});
    sparse_alpha.pixels().back() = 0;
    diagnostic.clear();
    Check(openxaml::render::DrawDirectWriteTextRun(
              sparse_alpha, run, {255, 0, 0, 0}, diagnostic),
          diagnostic.empty() ? "ignore untouched transparent pixels"
                             : diagnostic.c_str());
    Check(sparse_alpha.pixels().back() == 0,
          "untouched transparent pixels do not cause a coarse run refusal");

    openxaml::render::Surface fractional(200, 40, {255, 255, 255, 255});
    run.has_clip = true;
    run.clip = {0.5, 0.0, 20.0, 40.0};
    diagnostic.clear();
    Check(!openxaml::render::DrawDirectWriteTextRun(
              fractional, run, {255, 0, 0, 0}, diagnostic) &&
              diagnostic.find("fractional") != std::string::npos,
          "fractional text clip is a named refusal instead of whole-pixel expansion");
    run.has_clip = false;

    openxaml::render::Surface transparent(200, 40, {0, 0, 0, 0});
    const std::vector<std::uint32_t> transparent_before = transparent.pixels();
    diagnostic.clear();
    Check(!openxaml::render::DrawDirectWriteTextRun(
              transparent, run, {255, 0, 0, 0}, diagnostic) &&
              diagnostic.find("non-opaque") != std::string::npos &&
              transparent.pixels() == transparent_before,
          "nonopaque ClearType target is a named no-change refusal");

    openxaml::RuntimeTextResult missing;
    diagnostic.clear();
    Check(!provider->Layout({"OpenXaml Definitely Missing", "A", 14.0, 100.0,
                             false, false}, missing, diagnostic) &&
              diagnostic.find("could not resolve") != std::string::npos,
          "missing family is named instead of substituted");

    if (!failures) std::puts("DirectWrite runtime text provider checks passed");
    return failures ? 1 : 0;
}
