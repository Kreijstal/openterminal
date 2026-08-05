#include "content_presenter.h"

#include <algorithm>

namespace openxaml {
namespace {

// Content alignment moves the content inside the presenter without changing
// what the presenter asked for, so neither affects measure.
const DependencyProperty* const kHorizontalContentAlignment =
    RegisterProperty("ContentPresenter", "HorizontalContentAlignment",
                     {static_cast<int>(HorizontalAlignment::Left), false, false});
const DependencyProperty* const kVerticalContentAlignment =
    RegisterProperty("ContentPresenter", "VerticalContentAlignment",
                     {static_cast<int>(VerticalAlignment::Top), false, false});

// Its own rather than Panel's: a ContentPresenter derives straight from
// FrameworkElement. Nothing reads it -- see brush.h -- but the registry has to
// know the property exists, or markup setting it is refused as a typo.
const DependencyProperty* const kBackground =
    RegisterProperty("ContentPresenter", "Background", {std::string(), false, false});

// kChromePropertyOwner brings BorderThickness and Padding, which behave here
// exactly as they do on a Border.
const std::vector<std::string> kOwners = {"ContentPresenter", kChromePropertyOwner,
                                          "FrameworkElement", "UIElement"};

}  // namespace

const DependencyProperty& ContentPresenter::HorizontalContentAlignmentProperty() {
    return *kHorizontalContentAlignment;
}
const DependencyProperty& ContentPresenter::VerticalContentAlignmentProperty() {
    return *kVerticalContentAlignment;
}
const DependencyProperty& ContentPresenter::BackgroundProperty() { return *kBackground; }

const std::vector<std::string>& ContentPresenter::Owners() { return kOwners; }

void ContentPresenter::SetContent(std::unique_ptr<Element> content) {
    if (content) Adopt(*content);
    content_ = std::move(content);
}

Size ContentPresenter::MeasureOverride(Size available) {
    // Not ChromeInnerRect: that layout-rounds the content rect, as a panel
    // does, and a presenter follows Border instead -- the chrome is used as
    // written. No measured case has a presenter with content to tell the two
    // apart, so the one that matches the type it is modelled on wins.
    const Thickness border = border_thickness();
    const Thickness inner = padding();
    const double chrome_width = border.horizontal() + inner.horizontal();
    const double chrome_height = border.vertical() + inner.vertical();

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

        const Thickness border = border_thickness();
        const Thickness inner = padding();
        const HorizontalAlignment horizontal = horizontal_content_alignment();
        const VerticalAlignment vertical = vertical_content_alignment();

        const double left = border.left + inner.left;
        const double top = border.top + inner.top;
        const Size client{
            std::max(0.0, final_size.width - border.horizontal() - inner.horizontal()),
            std::max(0.0, final_size.height - border.vertical() - inner.vertical())};

        // Stretch content takes the whole inner rect; anything else takes what
        // it asked for and is positioned in the leftover. This is the same
        // decision Arrange makes for the element itself, one level in.
        const Size content_size{
            horizontal == HorizontalAlignment::Stretch ? client.width
                                                       : content->desired_size().width,
            vertical == VerticalAlignment::Stretch ? client.height
                                                   : content->desired_size().height};

        content->Arrange(
            {left + AlignmentOffset(horizontal, client.width, content_size.width),
             top + AlignmentOffset(vertical, client.height, content_size.height),
             content_size.width, content_size.height});
    }

    // The presenter itself always keeps the whole slot, whatever the content
    // was placed at inside it.
    return final_size;
}

}  // namespace openxaml
