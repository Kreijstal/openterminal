#include "advanced_controls.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "markup.h"

namespace openxaml {
namespace {

const std::vector<std::string> kUserControlOwners = {
    "UserControl", "ContentControl", "Control", kTextPropertyOwner,
    "FrameworkElement", "UIElement"};
const std::vector<std::string> kPageOwners = {
    "Page", "UserControl", "ContentControl", "Control", kTextPropertyOwner,
    "FrameworkElement", "UIElement"};
const std::vector<std::string> kFrameOwners = {
    "Frame", "ContentControl", "Control", kTextPropertyOwner, "FrameworkElement", "UIElement"};
const std::vector<std::string> kItemsOwners = {
    "ItemsControl", "Control", kTextPropertyOwner, "FrameworkElement", "UIElement"};
const std::vector<std::string> kListOwners = {
    "ListView", "ListViewBase", "Selector", "ItemsControl", "Control", kTextPropertyOwner,
    "FrameworkElement", "UIElement"};
const std::vector<std::string> kPopupOwners = {
    "Popup", "ContentControl", "Control", kTextPropertyOwner, "FrameworkElement", "UIElement"};

}  // namespace

const std::vector<std::string>& UserControl::Owners() { return kUserControlOwners; }
const std::vector<std::string>& Page::Owners() { return kPageOwners; }
const std::vector<std::string>& Frame::Owners() { return kFrameOwners; }
const std::vector<std::string>& ItemsControl::Owners() { return kItemsOwners; }
const std::vector<std::string>& ListView::Owners() { return kListOwners; }
const std::vector<std::string>& Popup::Owners() { return kPopupOwners; }

Size UserControl::MeasureOverride(Size available) {
    const std::vector<Element*> children = Children();
    if (children.empty()) return {};
    Element* const content = children.front();
    content->Measure(available);
    return content->desired_size();
}

Size UserControl::ArrangeOverride(Size final_size) {
    const std::vector<Element*> children = Children();
    if (!children.empty()) {
        children.front()->Arrange({0.0, 0.0, final_size.width, final_size.height});
    }
    return final_size;
}

Size Page::MeasureOverride(Size available) {
    const std::vector<Element*> children = Children();
    if (children.empty()) return {};
    Element* const content = children.front();
    content->Measure(available);
    return content->desired_size();
}

Size Page::ArrangeOverride(Size final_size) {
    const std::vector<Element*> children = Children();
    if (!children.empty()) {
        children.front()->Arrange({0.0, 0.0, final_size.width, final_size.height});
    }
    return final_size;
}

void Frame::RegisterPage(std::string type, PageFactory factory) {
    if (type.empty() || !factory) throw MarkupError("a Frame page registration needs a type and factory");
    factories_[std::move(type)] = std::move(factory);
}

bool Frame::Realize(size_t position) {
    if (position >= journal_.size()) return false;
    const NavigationEntry& entry = journal_[position];
    const auto found = factories_.find(entry.page_type);
    if (found == factories_.end()) return false;
    std::unique_ptr<Page> page = found->second(entry.parameter);
    if (!page) throw MarkupError("the page factory for '" + entry.page_type + "' returned null");
    SetContent(std::move(page));
    position_ = position;
    return true;
}

bool Frame::Navigate(const std::string& type, PropertyValue parameter) {
    if (!factories_.count(type)) return false;
    if (!journal_.empty() && position_ + 1 < journal_.size())
        journal_.erase(journal_.begin() + static_cast<std::ptrdiff_t>(position_ + 1), journal_.end());
    journal_.push_back({type, std::move(parameter)});
    return Realize(journal_.size() - 1);
}

bool Frame::GoBack() { return CanGoBack() && Realize(position_ - 1); }
bool Frame::GoForward() { return CanGoForward() && Realize(position_ + 1); }

const NavigationEntry* Frame::CurrentEntry() const {
    return journal_.empty() ? nullptr : &journal_[position_];
}

void ItemsControl::SetItems(size_t count, ItemFactory factory) {
    if (count && !factory) throw MarkupError("an ItemsControl with items needs an item factory");
    for (auto& [index, container] : realized_) OnContainerRecycled(index, *container);
    realized_.clear();
    item_count_ = count;
    item_factory_ = std::move(factory);
    UpdateRealization();
}

void ItemsControl::SetViewport(double offset, double extent) {
    if (offset < 0.0 || extent < 0.0)
        throw MarkupError("an ItemsControl viewport cannot be negative");
    viewport_offset_ = offset;
    viewport_extent_ = extent;
    UpdateRealization();
}

void ItemsControl::set_item_extent(double value) {
    if (!(value > 0.0) || !std::isfinite(value))
        throw MarkupError("an ItemsControl item extent must be finite and positive");
    item_extent_ = value;
    UpdateRealization();
}

void ItemsControl::UpdateRealization() {
    size_t first = 0;
    size_t last = 0;
    if (item_count_) {
        first = std::min(item_count_ - 1, static_cast<size_t>(viewport_offset_ / item_extent_));
        const double visible = viewport_extent_ > 0.0 ? viewport_extent_ : item_extent_;
        last = std::min(item_count_, static_cast<size_t>(std::ceil(
                                          (viewport_offset_ + visible) / item_extent_)));
        first = first > cache_length_ ? first - cache_length_ : 0;
        last = std::min(item_count_, last + cache_length_);
    }

    for (auto it = realized_.begin(); it != realized_.end();) {
        if (it->first < first || it->first >= last) {
            OnContainerRecycled(it->first, *it->second);
            it = realized_.erase(it);
        } else {
            ++it;
        }
    }
    for (size_t index = first; index < last; ++index) {
        if (realized_.count(index)) continue;
        std::unique_ptr<Element> container = item_factory_(index);
        if (!container) throw MarkupError("the item factory returned null");
        Adopt(*container);
        OnContainerRealized(index, *container);
        realized_.emplace(index, std::move(container));
    }
}

std::pair<size_t, size_t> ItemsControl::RealizedRange() const {
    if (realized_.empty()) return {0, 0};
    return {realized_.begin()->first, realized_.rbegin()->first + 1};
}

Element* ItemsControl::ContainerFromIndex(size_t index) const {
    const auto found = realized_.find(index);
    return found == realized_.end() ? nullptr : found->second.get();
}

size_t ItemsControl::IndexFromContainer(const Element* container) const {
    for (const auto& [index, item] : realized_)
        if (item.get() == container) return index;
    return std::numeric_limits<size_t>::max();
}

std::vector<Element*> ItemsControl::Children() const {
    std::vector<Element*> result;
    result.reserve(realized_.size());
    for (const auto& [index, child] : realized_) {
        (void)index;
        result.push_back(child.get());
    }
    return result;
}

Size ItemsControl::MeasureOverride(Size available) {
    if (viewport_extent_ <= 0.0 && std::isfinite(available.height))
        viewport_extent_ = available.height;
    UpdateRealization();
    double width = 0.0;
    for (auto& [index, child] : realized_) {
        (void)index;
        child->Measure({available.width, item_extent_});
        width = std::max(width, child->desired_size().width);
    }
    const double total = item_count_ * item_extent_;
    const double height = std::isfinite(available.height) ? std::min(total, available.height) : total;
    return {width, height};
}

Size ItemsControl::ArrangeOverride(Size final_size) {
    for (auto& [index, child] : realized_) {
        const double y = index * item_extent_ - viewport_offset_;
        child->Arrange({0.0, y, final_size.width, item_extent_});
    }
    return final_size;
}

void ItemsControl::OnContainerRealized(size_t index, Element& container) {
    (void)index;
    (void)container;
}
void ItemsControl::OnContainerRecycled(size_t index, Element& container) {
    (void)index;
    (void)container;
}

void ListView::set_selection_mode(SelectionMode mode) {
    if (selection_mode_ == mode) return;
    selection_mode_ = mode;
    if (mode == SelectionMode::None) selected_.clear();
    if (mode == SelectionMode::Single && selected_.size() > 1) {
        const size_t keep = *selected_.begin();
        selected_.clear();
        selected_.insert(keep);
    }
    NotifySelection();
}

bool ListView::Select(size_t index, bool extend) {
    if (index >= item_count() || selection_mode_ == SelectionMode::None) return false;
    const std::set<size_t> before = selected_;
    if (selection_mode_ == SelectionMode::Single || !extend) selected_.clear();
    selected_.insert(index);
    if (selected_ != before) NotifySelection();
    return true;
}

void ListView::ClearSelection() {
    if (selected_.empty()) return;
    selected_.clear();
    NotifySelection();
}

std::vector<size_t> ListView::selected_indices() const {
    return {selected_.begin(), selected_.end()};
}

void ListView::NotifySelection() {
    if (selection_changed_) selection_changed_(selected_indices());
}

namespace {

constexpr double kTabRowHeight = 32.0;
constexpr double kMinimumTabWidth = 120.0;
constexpr double kMaximumTabWidth = 220.0;

double TabChildWidth(Element& child) {
    if (dynamic_cast<TabViewItem*>(&child)) {
        return std::clamp(child.desired_size().width, kMinimumTabWidth,
                          kMaximumTabWidth);
    }
    return child.desired_size().width;
}

}  // namespace

Size TabView::MeasureOverride(Size available) {
    double width = 0.0;
    double height = kTabRowHeight;
    for (Element* child : Children()) {
        child->Measure({available.width, kTabRowHeight});
        width += TabChildWidth(*child);
        height = std::max(height, child->desired_size().height);
    }
    if (std::isfinite(available.width)) width = std::min(width, available.width);
    return {width, height};
}

Size TabView::ArrangeOverride(Size final_size) {
    double x = 0.0;
    for (Element* child : Children()) {
        const double remaining = std::max(0.0, final_size.width - x);
        const double width = std::min(TabChildWidth(*child), remaining);
        child->Arrange({x, 0.0, width, final_size.height});
        x += width;
    }
    return final_size;
}

void TabViewItem::set_selected(bool value) {
    if (selected_ == value) return;
    selected_ = value;
    set_background_brush(BrushValue::SolidColor(
        selected_ ? Color{0xff, 0x3a, 0x3a, 0x3a}
                  : Color{0xff, 0x24, 0x24, 0x24}));
    InvalidateRender(false);
}

Size TabViewItem::MeasureOverride(Size available) {
    const std::vector<Element*> children = Children();
    const bool close_visible =
        children.size() > 1 &&
        children[1]->visibility() == Visibility::Visible;
    const bool icon_visible =
        children.size() > 2 &&
        children[2]->visibility() == Visibility::Visible;
    constexpr double horizontal_padding = 24.0;
    constexpr double close_width = 32.0;
    constexpr double icon_width = 16.0;
    constexpr double icon_column_width = 24.0;

    // ContentControl only measures its first child. The template-less TabView
    // item has a second retained child for the close affordance, and an
    // unmeasured child has no layout storage: Arrange records its slot, but the
    // renderer correctly refuses to compile it. Measure the close child and
    // reserve its width just as the WinUI template's close column does.
    const double chrome = horizontal_padding +
                          (close_visible ? close_width : 0.0) +
                          (icon_visible ? icon_column_width : 0.0);
    const Size content_available{
        std::max(0.0, available.width - chrome), available.height};
    const Size measured = ContentControl::MeasureOverride(content_available);
    Size close_desired{};
    if (close_visible) {
        children[1]->Measure({close_width, available.height});
        close_desired = children[1]->desired_size();
    }
    Size icon_desired{};
    if (icon_visible) {
        children[2]->Measure({icon_width, icon_width});
        icon_desired = children[2]->desired_size();
    }
    return {std::max(kMinimumTabWidth, measured.width + chrome),
            std::max({kTabRowHeight, measured.height, close_desired.height,
                      icon_desired.height})};
}

Size TabViewItem::ArrangeOverride(Size final_size) {
    const std::vector<Element*> children = Children();
    if (!children.empty()) {
        const bool close_visible =
            children.size() > 1 &&
            children[1]->visibility() == Visibility::Visible;
        const bool icon_visible =
            children.size() > 2 &&
            children[2]->visibility() == Visibility::Visible;
        constexpr double close_width = 32.0;
        constexpr double icon_width = 16.0;
        constexpr double icon_column_width = 24.0;
        const double content_x = 12.0 +
                                 (icon_visible ? icon_column_width : 0.0);
        children.front()->Arrange({content_x, 0.0,
                                   std::max(0.0, final_size.width - 24.0 -
                                                     (close_visible ? close_width : 0.0) -
                                                     (icon_visible ? icon_column_width : 0.0)),
                                   final_size.height});
        if (close_visible) {
            children[1]->Arrange({std::max(0.0, final_size.width - close_width),
                                  0.0, close_width, final_size.height});
        }
        if (icon_visible) {
            children[2]->Arrange(
                {12.0, std::max(0.0, (final_size.height - icon_width) / 2.0),
                 icon_width, icon_width});
        }
    }
    return final_size;
}

Size Popup::MeasureOverride(Size available) {
    return is_open_ ? ContentControl::MeasureOverride(available) : Size{};
}

Size Popup::ArrangeOverride(Size final_size) {
    return is_open_ ? ContentControl::ArrangeOverride(final_size) : Size{};
}

void Flyout::ShowAt(Element& placement_target) {
    if (popup_.is_open()) return;
    if (opening_) opening_();
    placement_target_ = &placement_target;
    popup_.set_is_open(true);
    if (opened_) opened_();
}

void Flyout::Hide() {
    if (!popup_.is_open()) return;
    if (closing_) closing_();
    popup_.set_is_open(false);
    placement_target_ = nullptr;
    if (closed_) closed_();
}

MuxContentControl::MuxContentControl(std::string name) : type_name_(std::move(name)) {
    const size_t dot = type_name_.find_last_of('.');
    owners_ = {dot == std::string::npos ? type_name_ : type_name_.substr(dot + 1),
               "ContentControl", "Control", kTextPropertyOwner, "FrameworkElement", "UIElement"};
}

}  // namespace openxaml
