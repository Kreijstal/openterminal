#include "content_presenter.h"

#include <algorithm>

namespace openxaml {

Size ContentPresenter::MeasureOverride(Size available) {
    const double chrome_width = border_thickness.horizontal() + padding.horizontal();
    const double chrome_height = border_thickness.vertical() + padding.vertical();

    const std::vector<Element*> children = Children();
    if (children.empty()) return {chrome_width, chrome_height};

    Element* content = children.front();
    content->Measure({std::max(0.0, available.width - chrome_width),
                      std::max(0.0, available.height - chrome_height)});
    const Size content_size = content->desired_size();
    return {content_size.width + chrome_width, content_size.height + chrome_height};
}

Size ContentPresenter::ArrangeOverride(Size final_size) {
    const std::vector<Element*> children = Children();
    if (!children.empty()) {
        Element* content = children.front();

        const double left = border_thickness.left + padding.left;
        const double top = border_thickness.top + padding.top;
        const Size client{
            std::max(0.0, final_size.width - border_thickness.horizontal() - padding.horizontal()),
            std::max(0.0, final_size.height - border_thickness.vertical() - padding.vertical())};

        // Stretch content takes the whole inner rect; anything else takes what
        // it asked for and is positioned in the leftover. This is the same
        // decision Arrange makes for the element itself, one level in.
        const Size content_size{
            horizontal_content_alignment == HorizontalAlignment::Stretch
                ? client.width
                : content->desired_size().width,
            vertical_content_alignment == VerticalAlignment::Stretch
                ? client.height
                : content->desired_size().height};

        content->Arrange(
            {left + AlignmentOffset(horizontal_content_alignment, client.width, content_size.width),
             top + AlignmentOffset(vertical_content_alignment, client.height, content_size.height),
             content_size.width, content_size.height});
    }

    // The presenter itself always keeps the whole slot, whatever the content
    // was placed at inside it.
    return final_size;
}

}  // namespace openxaml
