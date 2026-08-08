#include "icon.h"

namespace openxaml {
namespace {

const DependencyProperty* const kData =
    RegisterProperty("PathIcon", "Data", {std::string(), false, true});

// IconElement is where Foreground would live. It is not in the chain: the
// inherited text properties are registered against kTextPropertyOwner, which
// Control and TextBlock share, and an icon that accepted a FontSize would be
// claiming an inheritance nothing here implements.
const std::vector<std::string> kOwners = {"PathIcon", "IconElement", "FrameworkElement",
                                          "UIElement"};
const DependencyProperty* const kGlyph =
    RegisterProperty("FontIcon", "Glyph", {std::string(), false, true});
const DependencyProperty* const kIconFontSize =
    RegisterProperty("IconElement", "FontSize", {20.0, true, true});
const DependencyProperty* const kIconFontFamily = RegisterProperty(
    "FontIcon", "FontFamily", {std::string("Segoe MDL2 Assets"), true, true});
const DependencyProperty* const kFontWeight =
    RegisterProperty("FontIcon", "FontWeight", {std::string("Normal"), false, true});
const DependencyProperty* const kMirrored =
    RegisterProperty("FontIcon", "MirroredWhenRightToLeft", {false, false, false});
const std::vector<std::string> kFontIconOwners = {
    "FontIcon", "IconElement", "FrameworkElement", "UIElement"};

}  // namespace

const DependencyProperty& PathIcon::DataProperty() { return *kData; }

const std::vector<std::string>& PathIcon::Owners() { return kOwners; }

Size PathIcon::MeasureOverride(Size available) {
    // The icon's Data is handed to the Path it builds, and the icon's desired
    // size is the Path's answer -- there is no separate icon-level sizing.
    content_.data = data;
    content_.Measure(available);
    return content_.desired_size();
}

Size PathIcon::ArrangeOverride(Size final_size) {
    content_.Arrange({0.0, 0.0, final_size.width, final_size.height});
    return final_size;
}

const std::vector<std::string>& FontIcon::Owners() { return kFontIconOwners; }
const DependencyProperty& FontIcon::GlyphProperty() { return *kGlyph; }
const DependencyProperty& FontIcon::FontSizeProperty() { return *kIconFontSize; }
const DependencyProperty& FontIcon::FontFamilyProperty() { return *kIconFontFamily; }
const DependencyProperty& FontIcon::FontWeightProperty() { return *kFontWeight; }

// The metrics are harvested from one file per family, which is one weight. A
// heavier one is a different face, and the corpus says the runtime measures it
// wider -- FontWeight="Black" on a square icon reports 11 at size 10 and 15 at
// size 14, where the unweighted glyph reports 10 and 14. Two observations, and
// more than one rule reproduces both: a whole extra DIP, a twenty-fourth of the
// em and two percent of it all land there. So this refuses rather than picking
// one; see phase3/xaml-db/fonts/README.md.
void FontIcon::RequireHarvestedWeight() const {
    const std::string& weight = font_weight();
    if (weight.empty() || weight == "Normal") return;
    throw TextError("the harvested metrics for \"" + font_family() +
                    "\" were read at Normal weight; FontWeight=\"" + weight +
                    "\" is a face this corpus has no metrics for, and the two sizes "
                    "that measure it do not pin what the weight adds");
}

Size FontIcon::MeasureOverride(Size available) {
    RequireHarvestedWeight();
    content_.set_text(glyph());
    content_.set_font_size(font_size());
    content_.set_font_family(font_family());
    content_.Measure(available);
    return content_.desired_size();
}

Size FontIcon::ArrangeOverride(Size final_size) {
    // The slot, not the glyph. A FontIcon is a FrameworkElement and stretches
    // like one: the corpus arranges a root FontIcon into 400x300 and records
    // 400x300, and into nothing and records nothing. The glyph inside it is
    // what the icon draws, not what the icon occupies.
    content_.Arrange({0, 0, final_size.width, final_size.height});
    return final_size;
}

}  // namespace openxaml
