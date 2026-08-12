#include "display_list.h"

#include <cmath>
#include <memory>
#include <stdexcept>

#include "border.h"
#include "canvas.h"
#include "chrome.h"
#include "content_presenter.h"
#include "control.h"
#include "image.h"
#include "icon.h"
#include "shape.h"
#include "text.h"

namespace openxaml {
namespace render {
namespace {

// The baseline's offset below the top of the line box, in the family that owns
// the line box -- the first installed entry of a fallback list, which is the
// same one TextBlock::LayoutText measures the height from.
//
// The line box is ascent + descent + gap. The original layout corpus records
// only line heights, so this puts the whole gap above the ascent (DirectWrite's
// default). The focused render oracle now records baselines and the strict
// acceptance test checks this decision for its explicit text programs; a font
// with a nonzero gap is still needed to distinguish every possible placement.
double BaselineOf(const std::string& family, double size) {
    const FontMetrics* line_font = FontLibrary::Default().Find(family);
    if (!line_font || line_font->units_per_em <= 0.0) return 0.0;
    return (line_font->ascender + line_font->line_gap) * size / line_font->units_per_em;
}

struct Walker {
    DisplayList out;
    std::vector<VisualNode> nodes;

    void Refuse(const std::string& path, const std::string& feature, const std::string& reason) {
        out.refusals.push_back(Refusal{path, feature, reason});
    }

    // The border thickness the element was laid out with, rounded the way
    // layout rounded it. Not re-derived: chrome.cpp already states that the
    // border is rounded *because it is drawn*, so the drawn edge and the
    // measured inset are the same number by construction.
    static bool BorderThicknessOf(const Element& element, Thickness& thickness) {
        if (const auto* border = dynamic_cast<const Border*>(&element)) {
            thickness = RoundBorderThickness(element, border->border_thickness());
            return true;
        }
        if (const auto* panel = dynamic_cast<const ChromedPanel*>(&element)) {
            thickness = RoundBorderThickness(element, panel->border_thickness());
            return true;
        }
        if (const auto* presenter = dynamic_cast<const ContentPresenter*>(&element)) {
            thickness = RoundBorderThickness(element, presenter->border_thickness());
            return true;
        }
        return false;
    }

    // The corner radius the chrome would be drawn with, from whichever of the
    // three carriers this element is. Not rounded: nothing here draws it, and
    // rounding a number on the way to a refusal would invent a rule.
    static CornerRadius CornerRadiusOf(const Element& element) {
        if (const auto* border = dynamic_cast<const Border*>(&element))
            return border->corner_radius();
        if (const auto* panel = dynamic_cast<const ChromedPanel*>(&element))
            return panel->corner_radius();
        if (const auto* presenter = dynamic_cast<const ContentPresenter*>(&element))
            return presenter->corner_radius();
        return CornerRadius{};
    }

    struct RenderGeometry {
        openxaml::Point origin;
        Size size;
        bool derived_from_explicit_canvas_state = false;
    };

    static bool HasExplicitExtent(const Element& element) {
        return !IsAuto(element.width()) && !IsAuto(element.height());
    }

    // Canvas and its Shape children are the measured exception: a root Canvas
    // can have no layout storage, and therefore never measures its children,
    // while explicit Width/Height and Canvas.Left/Top still define their
    // visuals. Keep those render facts separate from public DesiredSize and
    // ActualSize, which must continue to report the harvested zeros.
    static RenderGeometry GeometryOf(const Element& element) {
        if (element.has_layout_storage())
            return {element.render_origin(), element.render_size(), false};

        const bool is_canvas = dynamic_cast<const Canvas*>(&element) != nullptr;
        const bool is_canvas_rectangle =
            dynamic_cast<const Rectangle*>(&element) != nullptr &&
            dynamic_cast<const Canvas*>(element.visual_parent()) != nullptr;
        if ((is_canvas || is_canvas_rectangle) && HasExplicitExtent(element)) {
            openxaml::Point origin = element.render_origin();
            if (element.visual_parent())
                origin = {Canvas::GetLeft(element), Canvas::GetTop(element)};
            return {origin, element.specified_size(), true};
        }

        // The root is the other measured exception, and the corpus states it
        // outright. A Shape at the root of a tree is not a layout element, so
        // nothing measures or arranges it and it never gets layout storage --
        // but the runtime still answers ActualWidth and ActualHeight out of the
        // specified size once the element has been measured, which is what
        // Element::render_size reproduces. The recorded tree for
        // L7-terminal-65dec6afa8 is exactly one such Rectangle: desired
        // [0, 0], actual [12, 12], offset [0, 0]. The oracle gives it a rect,
        // so refusing it as unarranged contradicts the measurement.
        //
        // Root-only on purpose. Below the root it is the parent that decides
        // whether a child was arranged, and the only unarranged children the
        // corpus has are the Canvas ones the branch above already answers;
        // widening this to them would be a rule no recorded tree asked for.
        //
        // Measured, not merely constructed: render_size answers zero while the
        // element is still measure-dirty, and a zero extent is no rect at all.
        if (element.visual_parent() == nullptr && HasExplicitExtent(element)) {
            const Size size = element.render_size();
            if (size.width > 0.0 && size.height > 0.0)
                return {element.render_origin(), size, true};
        }
        return {element.render_origin(), element.render_size(), false};
    }

    void PaintFill(const std::string& path, const std::string& what, const Rect& bounds,
                   const Rect& local_bounds, const BrushValue& brush,
                   LocalDisplayList& content) {
        if (!brush.declared) return;
        if (brush.kind == BrushKind::Image) {
            // A default ImageBrush has a null ImageSource and is exactly a
            // transparent no-op. Keep a supplied source in the scene so that
            // the selected backend, rather than the compiler, owns the
            // decode/sampling capability boundary.
            if (!brush.has_image_source) return;
            if (brush.image_source.empty()) {
                Refuse(path, what,
                       "ImageBrush has a runtime ImageSource, but its source resource "
                       "identity is not retained by the platform-neutral scene");
                return;
            }
            if (bounds.width <= 0.0 || bounds.height <= 0.0) return;
            content.commands.push_back(
                LocalImageBrushFill{local_bounds, brush.image_source});
            return;
        }
        if (!brush.has_color) {
            Refuse(path, what,
                   "the declared brush did not retain a supported concrete brush type");
            return;
        }
        if (brush.color.a == 0) return;  // Transparent paints nothing, which is correct.
        if (brush.color == ProbeInkColor()) {
            Refuse(path, what,
                   "the case paints the colour the dumps reserve for text ink, so ink and "
                   "background could not be told apart in the round trip");
            return;
        }
        if (bounds.width <= 0.0 || bounds.height <= 0.0) return;
        out.rects.push_back(RectOp{bounds, brush.color, path, what});
        content.commands.push_back(LocalFillRect{local_bounds, brush.color});
    }

    NodeId Walk(const Element& element, const std::string& path, NodeId parent,
                std::uint64_t order, const Matrix3x2& parent_to_root) {
        const Rect slot = element.layout_slot();
        const Size actual = element.render_size();
        const RenderGeometry geometry = GeometryOf(element);
        const openxaml::Point origin = geometry.origin;
        const Size visual_size = geometry.size;
        Matrix3x2 content_transform = Matrix3x2::Identity();
        if (element.visual_transform().kind == VisualTransformKind::Rotate) {
            const openxaml::Point pivot{
                visual_size.width * element.render_transform_origin().x,
                visual_size.height * element.render_transform_origin().y};
            content_transform = Matrix3x2::RotationAround(
                element.visual_transform().angle_degrees, pivot);
        } else if (element.visual_transform().kind == VisualTransformKind::Scale) {
            const openxaml::Point pivot{
                visual_size.width * element.render_transform_origin().x +
                    element.visual_transform().center_x,
                visual_size.height * element.render_transform_origin().y +
                    element.visual_transform().center_y};
            content_transform = Matrix3x2::ScaleAround(
                element.visual_transform().scale_x,
                element.visual_transform().scale_y, pivot);
        }
        const Matrix3x2 local_transform =
            content_transform.Then(Matrix3x2::Translation(origin.x, origin.y));
        const Matrix3x2 transform_to_root = local_transform.Then(parent_to_root);
        const openxaml::Point absolute_origin =
            transform_to_root.TransformPoint(openxaml::Point{});

        VisualNode visual;
        visual.id = NodeId{element.render_node_id()};
        visual.parent = parent;
        visual.local_bounds = Rect{0.0, 0.0, visual_size.width, visual_size.height};
        visual.local_transform = local_transform;
        if (element.visual_transform().kind == VisualTransformKind::Unsupported)
            visual.unsupported_transform = element.visual_transform().type;
        if (element.visual_clip().kind == VisualClipKind::Rectangle)
            visual.clip = Clip::FromRect(element.visual_clip().bounds);
        visual.opacity = element.opacity();
        visual.visible = element.visibility() == Visibility::Visible;
        visual.z_index = Canvas::GetZIndex(element);
        visual.order = order;
        const NodeId visual_id = visual.id;
        const std::size_t visual_index = nodes.size();
        nodes.push_back(std::move(visual));
        auto content = std::make_shared<LocalDisplayList>();

        NodeGeometry node;
        node.path = path;
        node.type = element.TypeName();
        node.slot = slot;
        node.actual = actual;
        node.origin = origin;
        node.margin = element.margin();
        node.horizontal_alignment = element.horizontal_alignment();
        node.vertical_alignment = element.vertical_alignment();
        node.layout_rounding = element.use_layout_rounding();
        node.dpi_scale_x = element.dpi_scale_x;
        node.dpi_scale_y = element.dpi_scale_y;
        node.abs_x = absolute_origin.x;
        node.abs_y = absolute_origin.y;
        node.transform_to_root = transform_to_root;
        node.opacity = element.opacity();
        node.clip = nodes[visual_index].clip;
        node.z_index = Canvas::GetZIndex(element);
        node.has_layout_storage = element.has_layout_storage();
        node.visible = element.visibility() == Visibility::Visible;
        out.geometry.push_back(node);

        ExternalSurfaceReference external_surface =
            element.CaptureExternalSurface();

        const auto potentially_paints = [](const BrushValue& brush) {
            if (!brush.declared) return false;
            if (brush.kind == BrushKind::Image) return brush.has_image_source;
            if (brush.has_color) return brush.color.a != 0;
            return true;  // Unknown declared brush must remain a refusal.
        };
        const bool paints_something = potentially_paints(element.background_brush()) ||
                                      potentially_paints(element.fill_brush()) ||
                                      (dynamic_cast<const Shape*>(&element) &&
                                       static_cast<const Shape&>(element).stroke_thickness() > 0.0 &&
                                       potentially_paints(element.stroke_brush())) ||
                                      potentially_paints(element.border_brush()) ||
                                      (dynamic_cast<const Image*>(&element) &&
                                       static_cast<const Image&>(element).has_source()) ||
                                      dynamic_cast<const TextBlock*>(&element) != nullptr ||
                                      static_cast<bool>(external_surface);

        if (element.visibility() == Visibility::Collapsed) {
            // Collapsed is not "invisible": it is out of layout entirely, and
            // painting nothing is the whole of the correct behaviour. No
            // refusal -- there is nothing here that was not drawn.
            nodes[visual_index].content = std::move(content);
            return visual_id;
        }

        if (!element.has_layout_storage() && !geometry.derived_from_explicit_canvas_state) {
            // No layout storage means the runtime never gave this element a
            // measured or arranged rect, and the corpus records both as zero.
            // Explicit Canvas/Rectangle visual state was handled separately;
            // anything left here has no complete render extent to compile.
            if (paints_something) {
                Refuse(path, "unarranged element",
                       "the element takes no part in layout, so no recorded measurement gives "
                       "it a rect -- see Element::IsLayoutElement");
            }
            int unarranged_index = 0;
            for (const Element* child : element.RecordedChildren()) {
                const std::uint64_t child_order = static_cast<std::uint64_t>(unarranged_index);
                const NodeId child_id = Walk(
                    *child,
                    path + "/" + child->TypeName() + "[" +
                        std::to_string(unarranged_index++) + "]",
                    visual_id, child_order, transform_to_root);
                nodes[visual_index].children.push_back(child_id);
            }
            nodes[visual_index].content = std::move(content);
            return visual_id;
        }

        if (element.visual_transform().kind == VisualTransformKind::Unsupported) {
            Refuse(path, "RenderTransform",
                   "the declared " + element.visual_transform().type +
                       " is retained but only RotateTransform is implemented");
        }
        if (element.visual_clip().kind == VisualClipKind::Unsupported) {
            Refuse(path, "Clip",
                   "the declared " + element.visual_clip().type +
                       " is retained but only RectangleGeometry clipping is implemented");
        }

        const Rect local_bounds{0.0, 0.0, visual_size.width, visual_size.height};
        const Rect bounds = transform_to_root.TransformRect(local_bounds);

        PaintFill(path, "background", bounds, local_bounds, element.background_brush(),
                  *content);

        if (external_surface) {
            out.externals.push_back(ExternalSurfaceOp{
                bounds, path, external_surface.kind, external_surface.generation});
            content->commands.push_back(
                LocalExternalSurface{local_bounds, std::move(external_surface)});
        }

        if (const auto* image = dynamic_cast<const Image*>(&element);
            image && image->has_source() &&
            local_bounds.width > 0.0 && local_bounds.height > 0.0) {
            Refuse(path, "Source",
                   "the live ImageSource type '" + image->source_type() +
                       "' is retained, but immutable resource decoding and sampling "
                       "are not implemented");
        }

        // A rounded Rectangle is named for the reason a rounded Border is.
        //
        // RadiusX and RadiusY round the corners with the same antialiased arc
        // CornerRadius draws, and phase4/scripts/check_render.py recovers
        // axis-aligned rectangles out of the pixels and nothing else. Painting
        // the fill as a sharp rectangle would put pixels in the four corners
        // the runtime leaves empty, and the round trip would then confirm the
        // rectangle this project drew rather than the shape the markup asked
        // for. Refusing the arc is the same answer, under the same rule.
        //
        // Only when something would be drawn with it, as CornerRadius does: a
        // radius on a Rectangle whose Fill and Stroke both paint nothing
        // changes no pixel, and refusing that would report a no-draw as a gap.
        if (const auto* rectangle = dynamic_cast<const Rectangle*>(&element)) {
            const bool rounded =
                rectangle->radius_x() != 0.0 || rectangle->radius_y() != 0.0;
            const bool paints_shape =
                potentially_paints(element.fill_brush()) ||
                (rectangle->stroke_thickness() > 0.0 &&
                 potentially_paints(element.stroke_brush()));
            if (rounded && paints_shape) {
                // Named for whichever property actually rounds them: either
                // one alone is enough, and reporting the other would name a
                // property this markup never set.
                Refuse(path, rectangle->radius_x() != 0.0 ? "RadiusX" : "RadiusY",
                       "the Rectangle is drawn with rounded corners, which are not the "
                       "axis-aligned rectangles this pass paints and the round trip "
                       "recovers; no recorded measurement gives their pixels");
            }
        }

        if (element.fill_brush().declared) {
            if (dynamic_cast<const Rectangle*>(&element)) {
                PaintFill(path, "fill", bounds, local_bounds, element.fill_brush(), *content);
            } else {
                Refuse(path, "Fill",
                       "this Shape's geometry is not a rectangular local fill");
            }
        }

        if (const auto* shape = dynamic_cast<const Shape*>(&element)) {
            if (shape->shape_stretch() != ShapeStretch::None) {
                Refuse(path, "Stretch",
                       "Shape geometry stretching is retained but not implemented");
            }
            if (shape->stroke_thickness() > 0.0 &&
                element.stroke_brush().declared) {
                Refuse(path, "Stroke",
                       "Shape outline rasterization is retained but not implemented");
            }
        }

        Thickness border{};
        if (BorderThicknessOf(element, border)) {
            const bool has_border = border.left > 0.0 || border.top > 0.0 ||
                                    border.right > 0.0 || border.bottom > 0.0;
            // A rounded corner is not one of the axis-aligned rectangles this
            // pass paints, and it is not a rectangle the round-trip check could
            // recover out of the pixels either: the runtime antialiases the arc,
            // and phase4/scripts/check_render.py exists precisely because a
            // solid rect at whole-pixel edges is exactly invertible and nothing
            // else is. No recorded measurement pins those pixels -- the three
            // corpus cases that name a CornerRadius are cases the oracle
            // refused -- so drawing an arc here would be a picture this project
            // made up and then checked against itself. It is named instead.
            //
            // Only when something would actually be drawn with it. A radius on
            // an element whose chrome paints nothing changes no pixel, and
            // refusing that would report a no-draw as a gap.
            const CornerRadius radius = CornerRadiusOf(element);
            if (!radius.IsZero() &&
                ((has_border && potentially_paints(element.border_brush())) ||
                 potentially_paints(element.background_brush()))) {
                Refuse(path, "CornerRadius",
                       "the chrome is drawn with rounded corners, which are not the "
                       "axis-aligned rectangles this pass paints and the round trip "
                       "recovers; no recorded measurement gives their pixels");
            }
            if (has_border && element.border_brush().declared) {
                // Four rectangles, mitred the way a border is: the top and
                // bottom run the full width, the sides fill what is left.
                const Rect top{0.0, 0.0, actual.width, border.top};
                const Rect bottom{0.0, actual.height - border.bottom, actual.width,
                                  border.bottom};
                const Rect left{0.0, border.top, border.left,
                                actual.height - border.top - border.bottom};
                const Rect right{actual.width - border.right, border.top, border.right,
                                 actual.height - border.top - border.bottom};
                PaintFill(path, "border-top", transform_to_root.TransformRect(top), top,
                          element.border_brush(), *content);
                PaintFill(path, "border-bottom", transform_to_root.TransformRect(bottom),
                          bottom,
                          element.border_brush(), *content);
                PaintFill(path, "border-left", transform_to_root.TransformRect(left), left,
                          element.border_brush(), *content);
                PaintFill(path, "border-right", transform_to_root.TransformRect(right), right,
                          element.border_brush(), *content);
            }
            // A BorderThickness with no BorderBrush draws nothing in the
            // runtime either, so that is not a refusal -- it is the answer.
        }

        if (const auto* text = dynamic_cast<const TextBlock*>(&element)) {
            if (!text->text().empty()) {
                if (!text->runtime_text_refusal().empty()) {
                    Refuse(path, "Text", text->runtime_text_refusal());
                } else {
                    Color text_color = ProbeInkColor();
                    if (element.foreground_brush().declared) {
                        if (!element.foreground_brush().has_color) {
                            Refuse(path, "Foreground",
                                   "the declared text brush has no retained solid colour");
                        } else {
                            text_color = element.foreground_brush().color;
                        }
                    }
                    TextOp op;
                    op.bounds = bounds;
                    op.text = text->text();
                    op.font_family = text->font_family();
                    op.font_size = text->font_size();
                    op.baseline = BaselineOf(op.font_family, op.font_size);
                    op.advances = text->ShapedAdvances();
                    op.wrap = text->text_wrapping() == TextWrapping::Wrap;
                    op.bold = text->simulates_bold();
                    op.path = path;
                    out.texts.push_back(op);

                    LocalText local;
                    local.bounds = local_bounds;
                    local.text = op.text;
                    local.font_family = op.font_family;
                    local.font_size = op.font_size;
                    local.baseline = op.baseline;
                    local.advances = op.advances;
                    local.color = text_color;
                    local.wrap = op.wrap;
                    local.bold = op.bold;
                    local.language = op.language;
                    local.path = op.path;
                    content->commands.push_back(std::move(local));
                }
            }
        }
        if (const auto* icon = dynamic_cast<const FontIcon*>(&element)) {
            if (!icon->runtime_text_refusal().empty())
                Refuse(path, "Text", icon->runtime_text_refusal());
        }

        int index = 0;
        for (const Element* child : element.RecordedChildren()) {
            const std::uint64_t child_order = static_cast<std::uint64_t>(index);
            const NodeId child_id =
                Walk(*child,
                     path + "/" + child->TypeName() + "[" + std::to_string(index++) + "]",
                     visual_id, child_order, transform_to_root);
            nodes[visual_index].children.push_back(child_id);
        }

        nodes[visual_index].content = std::move(content);
        return visual_id;
    }

};

}  // namespace

DisplayList Build(const Element& root, Size surface) {
    Walker walker;
    walker.out.surface = surface;
    const NodeId root_id =
        walker.Walk(root, "/" + root.TypeName(), NodeId{}, 0, Matrix3x2::Identity());
    auto scene = std::make_shared<SceneSnapshot>(surface, root_id, std::move(walker.nodes));
    std::string validation_error;
    if (!scene->Validate(&validation_error)) {
        throw std::logic_error("render scene is invalid: " + validation_error);
    }
    walker.out.scene = std::move(scene);
    return std::move(walker.out);
}

}  // namespace render
}  // namespace openxaml
