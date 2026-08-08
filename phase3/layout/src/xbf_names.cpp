#include "xbf_names.h"

#include <algorithm>
#include <array>
#include <utility>

namespace openxaml {
namespace xbf {
namespace {

struct TypeEntry {
    std::uint16_t index;
    const char* name;
};

// Sorted by index so a lookup is a binary search and a duplicate is a
// compile-time-obvious mistake rather than a silent shadow.
constexpr TypeEntry kTypes[] = {
    {36, "Boolean"},
    {44, "ColumnDefinition"},
    {52, "Double"},
    {60, "FontWeight"},
    {65, "GridLength"},
    {76, "Int32"},
    {106, "RowDefinition"},
    {113, "String"},
    {114, "Style"},
    {124, "Thickness"},
    {191, "ControlTemplate"},
    {263, "Setter"},
    {265, "SolidColorBrush"},
    {286, "Border"},
    {296, "ContentPresenter"},
    // Not an element any markup builds, but a target type a Style may name --
    // which the corpus does, in two cases.
    {297, "Control"},
    {326, "Image"},
    {371, "ResourceDictionary"},
    {380, "Run"},
    {396, "TextBlock"},
    {432, "Canvas"},
    {435, "ContentControl"},
    {440, "FontIcon"},
    {442, "Grid"},
    {463, "Path"},
    {464, "PathIcon"},
    {470, "Rectangle"},
    {481, "StackPanel"},
    {483, "TextBox"},
    // Terminal's pages root themselves in these two. Neither is an element the
    // layout core builds; naming them turns "stable type index 491" into a work
    // item that says what is missing.
    {491, "UserControl"},
    {506, "ContentDialog"},
    {525, "Page"},
    {538, "ToolTip"},
    {540, "Button"},
    {552, "ScrollViewer"},
};

struct PropertyEntry {
    std::uint16_t index;
    MemberName member;
};

constexpr PropertyEntry kProperties[] = {
    {95, {"Width", MemberKind::Attribute}},                    // ColumnDefinition.Width
    {227, {"Height", MemberKind::Attribute}},                  // RowDefinition.Height
    {244, {"BasedOn", MemberKind::Attribute}},                 // Style.BasedOn
    {246, {"Setters", MemberKind::Element}},                   // Style.Setters
    {247, {"TargetType", MemberKind::Attribute}},              // Style.TargetType
    {303, {"Opacity", MemberKind::Attribute}},                 // UIElement.Opacity
    {307, {"RenderTransform", MemberKind::Element}},           // UIElement.RenderTransform
    {311, {"UseLayoutRounding", MemberKind::Attribute}},       // UIElement.UseLayoutRounding
    {312, {"Visibility", MemberKind::Attribute}},              // UIElement.Visibility
    {393, {"Height", MemberKind::Attribute}},                  // FrameworkElement.Height
    {394, {"HorizontalAlignment", MemberKind::Attribute}},     // FrameworkElement
    {397, {"Margin", MemberKind::Attribute}},                  // FrameworkElement.Margin
    {398, {"MaxHeight", MemberKind::Attribute}},               // FrameworkElement.MaxHeight
    {399, {"MaxWidth", MemberKind::Attribute}},                // FrameworkElement.MaxWidth
    {400, {"MinHeight", MemberKind::Attribute}},               // FrameworkElement.MinHeight
    {401, {"MinWidth", MemberKind::Attribute}},                // FrameworkElement.MinWidth
    {403, {"RequestedTheme", MemberKind::Attribute}},          // FrameworkElement
    {404, {"Resources", MemberKind::Element}},                 // FrameworkElement.Resources
    {405, {"Style", MemberKind::Attribute}},                   // FrameworkElement.Style
    {408, {"VerticalAlignment", MemberKind::Attribute}},       // FrameworkElement
    {409, {"Width", MemberKind::Attribute}},                   // FrameworkElement.Width
    {474, {"Property", MemberKind::Attribute}},                // Setter.Property
    {475, {"Value", MemberKind::Attribute}},                   // Setter.Value
    {476, {"Color", MemberKind::Attribute}},                   // SolidColorBrush.Color
    {503, {"Background", MemberKind::Attribute}},              // Border.Background
    {505, {"BorderThickness", MemberKind::Attribute}},         // Border.BorderThickness
    {506, {"Child", MemberKind::Content}},                     // Border.Child
    {508, {"CornerRadius", MemberKind::Attribute}},            // Border.CornerRadius
    {509, {"Padding", MemberKind::Attribute}},                 // Border.Padding
    {524, {"Content", MemberKind::Content}},                   // ContentPresenter.Content
    {541, {"Background", MemberKind::Attribute}},              // Control.Background
    {543, {"BorderThickness", MemberKind::Attribute}},         // Control.BorderThickness
    {547, {"FontFamily", MemberKind::Attribute}},              // Control.FontFamily
    {548, {"FontSize", MemberKind::Attribute}},                // Control.FontSize
    {551, {"FontWeight", MemberKind::Attribute}},              // Control.FontWeight
    {552, {"Foreground", MemberKind::Attribute}},              // Control.Foreground
    {553, {"HorizontalContentAlignment", MemberKind::Attribute}},  // Control
    {554, {"IsEnabled", MemberKind::Attribute}},               // Control.IsEnabled
    {557, {"Padding", MemberKind::Attribute}},                 // Control.Padding
    {561, {"VerticalContentAlignment", MemberKind::Attribute}},    // Control
    {588, {"Source", MemberKind::Attribute}},                  // Image.Source
    {589, {"Stretch", MemberKind::Attribute}},                 // Image.Stretch
    {648, {"Children", MemberKind::Content}},                  // Panel.Children
    {686, {"Content", MemberKind::Content}},                   // ResourceDictionary
    {687, {"MergedDictionaries", MemberKind::Element}},        // ResourceDictionary
    {688, {"Source", MemberKind::Attribute}},                  // ResourceDictionary
    {689, {"ThemeDictionaries", MemberKind::Element}},         // ResourceDictionary
    {733, {"Fill", MemberKind::Attribute}},                    // Shape.Fill
    {783, {"FontFamily", MemberKind::Attribute}},              // TextBlock.FontFamily
    {784, {"FontSize", MemberKind::Attribute}},                // TextBlock.FontSize
    {787, {"FontWeight", MemberKind::Attribute}},              // TextBlock.FontWeight
    {788, {"Foreground", MemberKind::Attribute}},              // TextBlock.Foreground
    {789, {"Inlines", MemberKind::Content}},                   // TextBlock.Inlines
    {800, {"Text", MemberKind::Attribute}},                    // TextBlock.Text
    {806, {"TextWrapping", MemberKind::Attribute}},            // TextBlock.TextWrapping
    {828, {"Canvas.Left", MemberKind::Attached}},              // Canvas.Left
    {829, {"Canvas.Top", MemberKind::Attached}},               // Canvas.Top
    {832, {"Content", MemberKind::Content}},                   // ContentControl.Content
    {851, {"FontFamily", MemberKind::Attribute}},              // FontIcon.FontFamily
    {852, {"FontSize", MemberKind::Attribute}},                // FontIcon.FontSize
    {854, {"FontWeight", MemberKind::Attribute}},              // FontIcon.FontWeight
    {855, {"Glyph", MemberKind::Attribute}},                   // FontIcon.Glyph
    {857, {"Grid.Column", MemberKind::Attached}},              // Grid.Column
    {858, {"ColumnDefinitions", MemberKind::Element}},         // Grid.ColumnDefinitions
    {859, {"Grid.ColumnSpan", MemberKind::Attached}},          // Grid.ColumnSpan
    {860, {"Grid.Row", MemberKind::Attached}},                 // Grid.Row
    {861, {"RowDefinitions", MemberKind::Element}},            // Grid.RowDefinitions
    {862, {"Grid.RowSpan", MemberKind::Attached}},             // Grid.RowSpan
    {922, {"Data", MemberKind::Attribute}},                    // Path.Data
    {923, {"Data", MemberKind::Attribute}},                    // PathIcon.Data
    {962, {"Orientation", MemberKind::Attribute}},             // StackPanel.Orientation
    {1195, {"BringIntoViewOnFocusChange", MemberKind::Attribute}},   // ScrollViewer
    {1201, {"HorizontalScrollBarVisibility", MemberKind::Attribute}},  // ScrollViewer
    {1202, {"HorizontalScrollMode", MemberKind::Attribute}},   // ScrollViewer
    {1210, {"IsVerticalScrollChainingEnabled", MemberKind::Attribute}},  // ScrollViewer
    {1221, {"VerticalScrollBarVisibility", MemberKind::Attribute}},  // ScrollViewer
    {1222, {"VerticalScrollMode", MemberKind::Attribute}},     // ScrollViewer
    {1684, {"Padding", MemberKind::Attribute}},                // ContentPresenter.Padding
    {1703, {"HorizontalContentAlignment", MemberKind::Attribute}},  // ContentPresenter
    {1704, {"VerticalContentAlignment", MemberKind::Attribute}},    // ContentPresenter
    {1995, {"Spacing", MemberKind::Attribute}},                // StackPanel.Spacing
};

struct EnumEntry {
    std::uint32_t value;
    const char* name;
};

struct EnumTable {
    std::uint16_t type_index;
    const EnumEntry* values;
    std::size_t count;
};

// The names come from dxaml/xcp/components/metadata/inc/EnumValueTable.g.h in
// the same pinned checkout -- the table the runtime's own text parser reads,
// which is why these are the exact spellings the markup uses.
constexpr EnumEntry kHorizontalAlignment[] = {
    {0, "Left"}, {1, "Center"}, {2, "Right"}, {3, "Stretch"},
};
constexpr EnumEntry kVerticalAlignment[] = {
    {0, "Top"}, {1, "Center"}, {2, "Bottom"}, {3, "Stretch"},
};
constexpr EnumEntry kOrientation[] = {{0, "Vertical"}, {1, "Horizontal"}};
constexpr EnumEntry kVisibility[] = {{0, "Visible"}, {1, "Collapsed"}};
constexpr EnumEntry kStretch[] = {
    {0, "None"}, {1, "Fill"}, {2, "Uniform"}, {3, "UniformToFill"},
};
// TextWrapping starts at 1: zero is not a value the enumeration has.
constexpr EnumEntry kTextWrapping[] = {{1, "NoWrap"}, {2, "Wrap"}, {3, "WrapWholeWords"}};
constexpr EnumEntry kScrollBarVisibility[] = {
    {0, "Disabled"}, {1, "Auto"}, {2, "Hidden"}, {3, "Visible"},
};
constexpr EnumEntry kScrollMode[] = {{0, "Disabled"}, {1, "Enabled"}, {2, "Auto"}};

constexpr EnumTable kEnums[] = {
    {624, kHorizontalAlignment, std::size(kHorizontalAlignment)},
    {641, kOrientation, std::size(kOrientation)},
    {654, kScrollBarVisibility, std::size(kScrollBarVisibility)},
    {658, kScrollMode, std::size(kScrollMode)},
    {665, kStretch, std::size(kStretch)},
    {679, kTextWrapping, std::size(kTextWrapping)},
    {684, kVerticalAlignment, std::size(kVerticalAlignment)},
    {688, kVisibility, std::size(kVisibility)},
};

}  // namespace

const char* TypeName(std::uint16_t stable_index) {
    const auto* found = std::lower_bound(
        std::begin(kTypes), std::end(kTypes), stable_index,
        [](const TypeEntry& entry, std::uint16_t index) { return entry.index < index; });
    if (found == std::end(kTypes) || found->index != stable_index) return nullptr;
    return found->name;
}

const MemberName* PropertyName(std::uint16_t stable_index) {
    const auto* found = std::lower_bound(
        std::begin(kProperties), std::end(kProperties), stable_index,
        [](const PropertyEntry& entry, std::uint16_t index) { return entry.index < index; });
    if (found == std::end(kProperties) || found->index != stable_index) return nullptr;
    return &found->member;
}

std::string EnumValueName(std::uint16_t stable_type_index, std::uint32_t value, bool* known_type) {
    if (known_type) *known_type = false;
    for (const EnumTable& table : kEnums) {
        if (table.type_index != stable_type_index) continue;
        if (known_type) *known_type = true;
        for (std::size_t i = 0; i < table.count; ++i) {
            if (table.values[i].value == value) return table.values[i].name;
        }
        return std::string();
    }
    return std::string();
}

}  // namespace xbf
}  // namespace openxaml
