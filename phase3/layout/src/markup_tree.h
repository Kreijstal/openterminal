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

#include "control.h"
#include "geometry.h"
#include "grid.h"
#include "layout.h"
#include "property.h"
#include "resources.h"
#include "resw_strings.h"
#include "stack_panel.h"
#include "text.h"

namespace openxaml {

struct MarkupDefinition {
    GridLength size;
    double min_size = 0.0;
    double max_size = kInfinity;
};

// One property the markup actually wrote, ready for the store.
//
// Which properties were written is not the same question as what they are set
// to, and the property system needs the first one: an inherited property that
// the markup left alone reads its ancestor's value, and one written out by
// hand does not -- even when the two happen to be equal.
struct MarkupProperty {
    const DependencyProperty* property = nullptr;
    PropertyValue value;
};

struct MarkupNode {
    // The short name as written in the markup: "Border", "Grid", "StackPanel".
    std::string type;

    // Everything the markup set on this element, in the form the property
    // store takes it. This is what the layout core realises.
    std::vector<MarkupProperty> properties;

    // The same values again, as typed fields.
    //
    // Not redundant for the reason it looks: phase3/xamlcore's client realises
    // the same node through the real WinRT ABI, which has a typed setter per
    // property and no store to hand a PropertyValue to. Both are filled from
    // one parse, so the two realisations cannot disagree about what a case
    // says -- which is the whole reason the parse is shared.
    //
    // Width and Height are NaN when the markup says Auto or says nothing,
    // which is how XAML spells "no explicit size".
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
    double canvas_left = 0.0;
    double canvas_top = 0.0;
    bool use_layout_rounding = true;

    // UIElement. Opacity and Visibility. Opacity changes no size at all; it is
    // carried because a parser that dropped an attribute it could not use
    // would produce markup that measures identically and is not the markup
    // that was written.
    double opacity = 1.0;
    Visibility visibility = Visibility::Visible;

    // Border, and the WinUI 2 panels that grew the same properties.
    Thickness border_thickness;
    Thickness padding;
    // The short name of whatever brush the Background was set to, or empty.
    // Recorded rather than realised: it decides nothing about layout, and
    // keeping it makes the parse round-trippable for a consumer that draws.
    std::string background;

    // StackPanel.
    Orientation orientation = Orientation::Vertical;
    double spacing = 0.0;

    // Path and PathIcon. Parsed at markup time, because a geometry that
    // cannot be read is a markup error rather than a measurement of zero.
    GeometryBounds data;

    // ContentPresenter.
    HorizontalAlignment horizontal_content_alignment = HorizontalAlignment::Left;
    VerticalAlignment vertical_content_alignment = VerticalAlignment::Top;

    // Text, and the inherited text properties a Control carries as well. The
    // defaults are XAML's.
    std::string text;
    std::string font_family = "Segoe UI";
    double font_size = 14.0;
    std::string foreground;
    TextWrapping text_wrapping = TextWrapping::NoWrap;

    // Grid.
    std::vector<MarkupDefinition> column_definitions;
    std::vector<MarkupDefinition> row_definitions;

    // FrameworkElement.Resources. Kept on the node rather than consumed during
    // the parse because it is the element's own dictionary: a {StaticResource}
    // in the subtree below resolves against it, and the node is where the walk
    // up the tree finds it. Nothing downstream of the parse reads it -- a
    // resource has already done its work by the time an Element exists.
    ResourceDictionary resources;

    std::vector<MarkupNode> children;
};

// The parse is the same either way; the string table only decides what an
// x:Uid resolves to, and an empty one is the state every corpus case is in.
MarkupNode ParseMarkup(const std::string& markup, const StringTable& strings);

// Throws MarkupError for anything outside the implemented subset. The full
// runtime class name, as the oracle reports it, for a short markup name.
std::string FullTypeName(const std::string& short_name);

MarkupNode ParseMarkup(const std::string& markup);

}  // namespace openxaml

#endif  // OPENXAML_MARKUP_TREE_H
