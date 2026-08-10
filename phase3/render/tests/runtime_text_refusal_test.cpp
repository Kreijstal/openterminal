#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#include "display_list.h"
#include "icon.h"
#include "stack_panel.h"
#include "text.h"

namespace {

class RefusingRuntimeText final : public openxaml::RuntimeTextProvider {
public:
    bool ResolveFontMetrics(const std::string&, openxaml::FontMetrics&,
                            std::string&, std::string& diagnostic) override {
        diagnostic = reason;
        return false;
    }
    bool Layout(const openxaml::RuntimeTextRequest&,
                openxaml::RuntimeTextResult&, std::string& diagnostic) override {
        diagnostic = reason;
        return false;
    }
    static constexpr const char* reason =
        "DirectWrite could not resolve any requested family in \"Missing Icons\"";
};

void Check(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

}  // namespace

int main() {
    openxaml::SetRuntimeTextProvider(std::make_shared<RefusingRuntimeText>());

    openxaml::StackPanel root;
    root.set_background_brush(
        openxaml::BrushValue{true, true, openxaml::Color{255, 90, 40, 20}});
    auto icon = std::make_unique<openxaml::FontIcon>();
    icon->set_font_family("Missing Icons");
    icon->set_glyph("x");
    root.AddChild(std::move(icon));

    // The unsupported leaf contributes no invented size but cannot unwind the
    // panel's layout or discard its independently renderable background.
    root.Measure({120, 80});
    root.Arrange({0, 0, 120, 80});
    Check(root.render_size().width == 120 && root.render_size().height == 80,
          "a runtime font refusal remains leaf-local during layout");

    const openxaml::render::DisplayList list =
        openxaml::render::Build(root, {120, 80});
    Check(list.rects.size() == 1,
          "unrelated panel paint survives an unsupported text leaf");
    Check(list.refusals.size() == 1 && list.refusals[0].feature == "Text" &&
              list.refusals[0].reason == RefusingRuntimeText::reason,
          "the exact runtime text refusal reaches the retained display list");
    Check(list.texts.empty(), "unsupported text is not approximated or rasterized");

    openxaml::SetRuntimeTextProvider(nullptr);
    std::cout << "runtime text refusal containment checks passed\n";
    return 0;
}
