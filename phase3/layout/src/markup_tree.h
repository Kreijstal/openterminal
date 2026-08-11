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
#include "style.h"
#include "text.h"
#include "scroll_viewer.h"
#include "shape.h"

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

struct MarkupBinding {
    const DependencyProperty* property = nullptr;
    Binding binding;
};

struct MarkupVisualSetter {
    std::string target_name;
    std::string property;
    std::string value;
};

struct MarkupTimeline {
    std::string target_name;
    std::string property;
    std::string from;
    std::string to;
    bool has_from = false;
    double duration_seconds = 0.0;
};

struct MarkupVisualState {
    std::string name;
    std::vector<MarkupVisualSetter> setters;
    std::vector<MarkupTimeline> timelines;
};

struct MarkupVisualStateGroup {
    std::string name;
    std::vector<MarkupVisualState> states;
};

struct MarkupNode {
    // The short name as written in the markup: "Border", "Grid", "StackPanel".
    std::string type;
    std::string name;

    // Everything the markup set on this element, in the form the property
    // store takes it. This is what the layout core realises.
    std::vector<MarkupProperty> properties;
    std::vector<MarkupBinding> bindings;
    std::vector<MarkupVisualStateGroup> visual_state_groups;

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
    int canvas_z_index = 0;
    bool use_layout_rounding = true;

    // UIElement. Opacity and Visibility. Opacity changes no size at all; it is
    // carried because a parser that dropped an attribute it could not use
    // would produce markup that measures identically and is not the markup
    // that was written.
    double opacity = 1.0;
    Visibility visibility = Visibility::Visible;
    // A transform property element was present. Layout deliberately ignores
    // its payload, but the renderer must retain the declaration so it can
    // refuse unsupported transform semantics instead of painting untransformed.
    VisualTransform visual_transform;
    Point render_transform_origin;
    VisualClip visual_clip;

    // Border, and the WinUI 2 panels that grew the same properties.
    Thickness border_thickness;
    Thickness padding;
    // Retained rather than realised into anything layout reads: a corner radius
    // moves nothing. It is here so the parse round-trips and so the render pass
    // can name what it cannot draw -- see layout.h.
    CornerRadius corner_radius;
    // The short name of whatever brush the Background was set to, or empty.
    // Recorded rather than realised: it decides nothing about layout, and
    // keeping it makes the parse round-trippable for a consumer that draws.
    std::string background;
    // The same three brushes as the render pass needs them: set at all, and
    // reducible to a colour or not. A brush written as a property element is
    // declared and colourless here, which is a no-draw the renderer names --
    // see brush.h.
    BrushValue background_brush;
    BrushValue border_brush;
    BrushValue fill_brush;
    BrushValue stroke_brush;
    BrushValue foreground_brush;

    // Shape paint state. Geometry rendering is a backend boundary, but these
    // values remain observable through IShape and must survive parsing.
    double stroke_miter_limit = 10.0;
    double stroke_thickness = 0.0;
    ShapeLineCap stroke_start_line_cap = ShapeLineCap::Flat;
    ShapeLineCap stroke_end_line_cap = ShapeLineCap::Flat;
    ShapeLineJoin stroke_line_join = ShapeLineJoin::Miter;
    double stroke_dash_offset = 0.0;
    ShapeLineCap stroke_dash_cap = ShapeLineCap::Flat;
    bool has_stroke_dash_array = false;
    ShapeStretch shape_stretch = ShapeStretch::None;
    // Rectangle.RadiusX / RadiusY. Zero is a square corner; anything else is a
    // rounded rectangle, which the render pass has to be able to see before it
    // decides whether it can paint one.
    double radius_x = 0.0;
    double radius_y = 0.0;

    // StackPanel.
    Orientation orientation = Orientation::Vertical;
    double spacing = 0.0;

    // Path and PathIcon. Parsed at markup time, because a geometry that
    // cannot be read is a markup error rather than a measurement of zero.
    GeometryBounds data;

    // ContentPresenter.
    HorizontalAlignment horizontal_content_alignment = HorizontalAlignment::Stretch;
    VerticalAlignment vertical_content_alignment = VerticalAlignment::Stretch;

    // Text, and the inherited text properties a Control carries as well. The
    // defaults are XAML's.
    std::string text;
    // Whether `text` arrived as the Text property rather than as character
    // data between the tags. The runtime measures the two differently -- see
    // rule 7 in text.cpp -- so the spelling has to survive the parse.
    bool text_from_property = false;
    std::string glyph;
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

    // The implicit styles this element's dictionary declares -- the <Style>
    // entries with a TargetType and no x:Key. Beside `resources` rather than
    // in it because their key is a type and not a string; see style.h.
    ImplicitStyleTable implicit_styles;

    // The style that applies to this element: what Style="{StaticResource K}"
    // named, or the implicit style found for its type, or null.
    //
    // Resolved during the parse and carried rather than applied, for the same
    // reason `properties` is: which values a style supplies is decided here,
    // and *when* they reach the store is the realiser's business. The typed
    // fields above deliberately do not reflect it -- they are what the markup
    // wrote locally, and a style is not that.
    std::shared_ptr<const Style> style;

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
