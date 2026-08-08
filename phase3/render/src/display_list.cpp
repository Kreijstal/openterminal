#include "display_list.h"

#include <cmath>

#include "border.h"
#include "canvas.h"
#include "chrome.h"
#include "content_presenter.h"
#include "control.h"
#include "image.h"
#include "shape.h"
#include "text.h"

namespace openxaml {
namespace render {
namespace {

struct Walker {
    DisplayList out;

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

    void PaintFill(const std::string& path, const std::string& what, const Rect& bounds,
                   const BrushValue& brush) {
        if (!brush.declared) return;
        if (!brush.has_color) {
            Refuse(path, what,
                   "a brush with no colour of its own -- a property-element brush or one a "
                   "Style supplied; nothing here knows what colour it is");
            return;
        }
        if (brush.color.a == 0) return;  // Transparent paints nothing, which is correct.
        if (brush.color.a != 0xff) {
            Refuse(path, what,
                   "a partly transparent brush needs alpha composition over what is under "
                   "it, and no recorded measurement says what the runtime composes");
            return;
        }
        if (brush.color == ProbeInkColor()) {
            Refuse(path, what,
                   "the case paints the colour the dumps reserve for text ink, so ink and "
                   "background could not be told apart in the round trip");
            return;
        }
        if (bounds.width <= 0.0 || bounds.height <= 0.0) return;
        out.rects.push_back(RectOp{bounds, brush.color, path, what});
    }

    void Walk(const Element& element, const std::string& path, double parent_x, double parent_y) {
        const Rect slot = element.layout_slot();
        const Size actual = element.render_size();
        const double x = parent_x + slot.x;
        const double y = parent_y + slot.y;

        NodeGeometry node;
        node.path = path;
        node.type = element.TypeName();
        node.slot = slot;
        node.actual = actual;
        node.abs_x = x;
        node.abs_y = y;
        node.has_layout_storage = element.has_layout_storage();
        node.visible = element.visibility() == Visibility::Visible;
        out.geometry.push_back(node);

        const bool paints_something = element.background_brush().declared ||
                                      element.fill_brush().declared ||
                                      element.border_brush().declared ||
                                      dynamic_cast<const TextBlock*>(&element) != nullptr;

        if (element.visibility() == Visibility::Collapsed) {
            // Collapsed is not "invisible": it is out of layout entirely, and
            // painting nothing is the whole of the correct behaviour. No
            // refusal -- there is nothing here that was not drawn.
            return;
        }

        if (!element.has_layout_storage()) {
            // No layout storage means the runtime never gave this element a
            // measured or arranged rect, and the corpus records both as zero.
            // A Rectangle under a Canvas really does render at its specified
            // size somewhere; where, and at what size, is not in any recording.
            if (paints_something) {
                Refuse(path, "unarranged element",
                       "the element takes no part in layout, so no recorded measurement gives "
                       "it a rect -- see Element::IsLayoutElement");
            }
            int unarranged_index = 0;
            for (const Element* child : element.RecordedChildren()) {
                Walk(*child,
                     path + "/" + child->TypeName() + "[" +
                         std::to_string(unarranged_index++) + "]",
                     x, y);
            }
            return;
        }

        if (!AreClose(element.opacity(), 1.0)) {
            Refuse(path, "Opacity",
                   "an opacity other than 1 makes the subtree a composed layer, and no "
                   "recorded measurement says what it composes with");
        }

        // Where the element sits inside a slot bigger than it is.
        //
        // The layout core arranges an element at its slot's origin; only a
        // ContentPresenter and a Control pass an already-aligned rect down. So
        // for a Grid or StackPanel child that is not Stretch and did not fill
        // its slot, the runtime moves it and nothing recorded says by how much
        // -- the corpus records the slot and the size, never the offset between
        // them. Painting at the slot origin would be a guess.
        if (element.horizontal_alignment() != HorizontalAlignment::Stretch &&
            element.horizontal_alignment() != HorizontalAlignment::Left &&
            GreaterThan(slot.width, actual.width) && paints_something) {
            Refuse(path, "HorizontalAlignment inside a wider slot",
                   "the runtime moves the element within its slot and the corpus records only "
                   "the slot; the render origin is not derivable");
        }
        if (element.vertical_alignment() != VerticalAlignment::Stretch &&
            element.vertical_alignment() != VerticalAlignment::Top &&
            GreaterThan(slot.height, actual.height) && paints_something) {
            Refuse(path, "VerticalAlignment inside a taller slot",
                   "the runtime moves the element within its slot and the corpus records only "
                   "the slot; the render origin is not derivable");
        }

        const Rect bounds{x, y, actual.width, actual.height};

        PaintFill(path, "background", bounds, element.background_brush());

        // Shape.Fill. A Shape is never a layout element, so this is only ever
        // reached through the branch above; it is here so the refusal names
        // Fill rather than being silently folded into "unarranged".
        if (element.fill_brush().declared) {
            Refuse(path, "Fill",
                   "a Shape takes no part in layout, so the corpus records no rect to fill");
        }

        Thickness border{};
        if (BorderThicknessOf(element, border)) {
            const bool has_border = border.left > 0.0 || border.top > 0.0 ||
                                    border.right > 0.0 || border.bottom > 0.0;
            if (has_border && element.border_brush().declared) {
                // Four rectangles, mitred the way a border is: the top and
                // bottom run the full width, the sides fill what is left.
                const double inner_top = y + border.top;
                const double inner_bottom = y + actual.height - border.bottom;
                PaintFill(path, "border-top", Rect{x, y, actual.width, border.top},
                          element.border_brush());
                PaintFill(path, "border-bottom",
                          Rect{x, inner_bottom, actual.width, border.bottom},
                          element.border_brush());
                PaintFill(path, "border-left",
                          Rect{x, inner_top, border.left, inner_bottom - inner_top},
                          element.border_brush());
                PaintFill(path, "border-right",
                          Rect{x + actual.width - border.right, inner_top, border.right,
                               inner_bottom - inner_top},
                          element.border_brush());
            }
            // A BorderThickness with no BorderBrush draws nothing in the
            // runtime either, so that is not a refusal -- it is the answer.
        }

        if (const auto* text = dynamic_cast<const TextBlock*>(&element)) {
            if (!text->text().empty()) {
                TextOp op;
                op.bounds = bounds;
                op.text = text->text();
                op.font_family = text->font_family();
                op.font_size = text->font_size();
                op.path = path;
                out.texts.push_back(op);
            }
        }

        int index = 0;
        for (const Element* child : element.RecordedChildren())
            Walk(*child, path + "/" + child->TypeName() + "[" + std::to_string(index++) + "]", x,
                 y);
    }

};

}  // namespace

DisplayList Build(const Element& root, Size surface) {
    Walker walker;
    walker.out.surface = surface;
    walker.Walk(root, "/" + root.TypeName(), 0.0, 0.0);
    return std::move(walker.out);
}

}  // namespace render
}  // namespace openxaml
