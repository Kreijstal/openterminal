#include <cmath>
#include <iostream>
#include <stdexcept>

#include "basic_controls.h"
#include "markup.h"
#include "icon.h"

using namespace openxaml;

namespace {
void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void TerminalBlockersLoad() {
    auto tip = LoadMarkup(
        R"(<ToolTip xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
             xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"><TextBlock><Run
             x:Name="MaximizeToolTip"/></TextBlock></ToolTip>)");
    Check(tip->TypeName() == "Windows.UI.Xaml.Controls.ToolTip", "ToolTip/Run load");

    auto thumb = LoadMarkup(
        R"(<Thumb xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"><Thumb.Template>
             <ControlTemplate TargetType="Thumb"><Rectangle Fill="Transparent"/>
             </ControlTemplate></Thumb.Template></Thumb>)");
    auto* control = dynamic_cast<Thumb*>(thumb.get());
    Check(control && control->TemplateRoot(), "Thumb inline ControlTemplate");
    Check(control->TemplateRoot()->TypeName() == "Windows.UI.Xaml.Shapes.Rectangle",
          "Rectangle template root");

    auto form = LoadMarkup(
        R"(<StackPanel xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
             Orientation="Horizontal"><TextBox Width="400" PlaceholderText="{}{guid here}"/>
             <Button>Create</Button></StackPanel>)");
    Check(form->Children().size() == 2, "TextBox/Button content load");

    auto icon = LoadMarkup(
        u8R"(<FontIcon xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
              FontFamily="Segoe UI, Segoe Fluent Icons, Segoe MDL2 Assets"
              FontSize="12" Glyph=""/>)");
    Check(icon->TypeName() == "Windows.UI.Xaml.Controls.FontIcon", "FontIcon load");
}

void FontFallback() {
    FontMetrics latin;
    latin.units_per_em = 1000;
    latin.ascender = 800;
    latin.descender = -200;
    latin.advances[U'M'] = 800;
    FontMetrics icons = latin;
    icons.advances.clear();
    icons.advances[0xE932] = 1000;
    FontLibrary::Default().Add("Fallback Latin", latin);
    FontLibrary::Default().Add("Fallback Icons", icons);
    FontIcon icon;
    icon.set_font_family("Fallback Latin, Fallback Icons");
    icon.set_font_size(20);
    icon.set_glyph(u8"\ue932");
    icon.Measure({400, 300});
    Check(icon.desired_size().width == 20 && icon.desired_size().height == 20,
          "FontIcon per-glyph family fallback");
}
}  // namespace

int main() {
    try {
        TerminalBlockersLoad();
        FontFallback();
        std::cout << "Windows.UI.Xaml completion tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
