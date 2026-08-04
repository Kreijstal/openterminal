// The markup, parsed but not yet realised.
//
// Two things build from the same corpus: the layout core, which turns a node
// into an Element directly, and phase3/xamlcore's client, which turns it into
// WinRT objects through the real ABI. Splitting the parse out means those two
// cannot disagree about what a case says -- a divergence between them is then
// necessarily in the ABI, which is the whole point of running both.

#ifndef OPENXAML_MARKUP_TREE_H
#define OPENXAML_MARKUP_TREE_H

#include <string>
#include <vector>

#include "grid.h"
#include "layout.h"
#include "stack_panel.h"
#include "text.h"

namespace openxaml {

struct MarkupDefinition {
    GridLength size;
    double min_size = 0.0;
    double max_size = kInfinity;
};

struct MarkupNode {
    // The short name as written in the markup: "Border", "Grid", "StackPanel".
    std::string type;

    // FrameworkElement. Width and Height are NaN when the markup says Auto or
    // says nothing, which is how XAML spells "no explicit size".
    double width = Auto();
    double height = Auto();
    double min_width = 0.0;
    double max_width = kInfinity;
    double min_height = 0.0;
    double max_height = kInfinity;
    Thickness margin;
    HorizontalAlignment horizontal_alignment = HorizontalAlignment::Stretch;
    VerticalAlignment vertical_alignment = VerticalAlignment::Stretch;
    int grid_column = 0;
    int grid_row = 0;
    int grid_column_span = 1;
    int grid_row_span = 1;

    // Border.
    Thickness border_thickness;
    Thickness padding;

    // StackPanel.
    Orientation orientation = Orientation::Vertical;

    // TextBlock. The defaults are XAML's; every case in the corpus sets all
    // three explicitly, so they are not exercised.
    std::string text;
    std::string font_family = "Segoe UI";
    double font_size = 14.0;
    TextWrapping text_wrapping = TextWrapping::NoWrap;

    // Grid.
    std::vector<MarkupDefinition> column_definitions;
    std::vector<MarkupDefinition> row_definitions;

    std::vector<MarkupNode> children;
};

// Throws MarkupError for anything outside the implemented subset. The full
// runtime class name, as the oracle reports it, for a short markup name.
std::string FullTypeName(const std::string& short_name);

MarkupNode ParseMarkup(const std::string& markup);

}  // namespace openxaml

#endif  // OPENXAML_MARKUP_TREE_H
