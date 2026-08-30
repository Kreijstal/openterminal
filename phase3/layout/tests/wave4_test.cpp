#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#include "advanced_controls.h"
#include "border.h"
#include "markup.h"

using namespace openxaml;

namespace {

// Not assert(): a condition here may carry the side effect the next line
// depends on, and NDEBUG would erase it along with the check.
void Check(bool condition, const char* what, int line) {
    if (condition) return;
    std::cerr << "wave4_test.cpp:" << line << ": CHECK failed: " << what << "\n";
    std::exit(1);
}

#define CHECK(condition) Check(static_cast<bool>(condition), #condition, __LINE__)

class FixedDesiredElement final : public Border {
public:
    explicit FixedDesiredElement(Size desired = {16.0, 16.0})
        : desired_(desired) {}
    std::string TypeName() const override { return "Test.FixedDesiredElement"; }

protected:
    Size MeasureOverride(Size) override { return desired_; }

private:
    Size desired_;
};

class TabViewItemHarness final : public TabViewItem {
public:
    TabViewItemHarness()
        : header_({110.0, 16.0}), close_({14.0, 14.0}), icon_({16.0, 16.0}) {
        CHECK(AttachVisualChild(header_));
        CHECK(AttachVisualChild(close_));
        CHECK(AttachVisualChild(icon_));
    }
    ~TabViewItemHarness() override {
        DetachVisualChild(icon_);
        DetachVisualChild(close_);
        DetachVisualChild(header_);
    }
    std::vector<Element*> Children() const override {
        return {const_cast<FixedDesiredElement*>(&header_),
                const_cast<FixedDesiredElement*>(&close_),
                const_cast<FixedDesiredElement*>(&icon_)};
    }
    const FixedDesiredElement& header() const { return header_; }
    const FixedDesiredElement& close() const { return close_; }
    const FixedDesiredElement& icon() const { return icon_; }

private:
    FixedDesiredElement header_;
    FixedDesiredElement close_;
    FixedDesiredElement icon_;
};

class TabViewHarness final : public TabView {
public:
    TabViewItem* AddTab(bool selected) {
        auto item = std::make_unique<TabViewItem>();
        item->set_selected(selected);
        TabViewItem* const pointer = item.get();
        tabs_.push_back(std::move(item));
        return pointer;
    }

    std::vector<Element*> Children() const override {
        std::vector<Element*> children;
        children.reserve(tabs_.size());
        for (const auto& tab : tabs_) children.push_back(tab.get());
        return children;
    }

private:
    std::vector<std::unique_ptr<TabViewItem>> tabs_;
};

void VirtualizationAndSelection() {
    ListView list;
    list.set_item_extent(32.0);
    list.set_cache_length(1);
    list.SetViewport(64.0, 64.0);
    list.SetItems(100, [](size_t index) {
        auto item = std::make_unique<Border>();
        item->set_width(static_cast<double>(index + 1));
        return item;
    });
    CHECK((list.RealizedRange() == std::make_pair<size_t, size_t>(1, 5)));
    CHECK(list.ContainerFromIndex(0) == nullptr);
    CHECK(list.ContainerFromIndex(2) != nullptr);
    list.Measure({200.0, 64.0});
    list.Arrange({0.0, 0.0, 200.0, 64.0});
    CHECK(list.ContainerFromIndex(2)->layout_slot().y == 0.0);

    int notifications = 0;
    list.SetSelectionChanged([&](const std::vector<size_t>&) { ++notifications; });
    list.set_selection_mode(SelectionMode::Multiple);
    CHECK(list.Select(2));
    CHECK(list.Select(3, true));
    CHECK((list.selected_indices() == std::vector<size_t>{2, 3}));
    CHECK(notifications >= 2);
}

void NavigationJournal() {
    Frame frame;
    frame.RegisterPage("First", [](const PropertyValue&) {
        auto page = std::make_unique<Page>();
        auto content = std::make_unique<Border>();
        content->set_width(10.0);
        page->SetContent(std::move(content));
        return page;
    });
    frame.RegisterPage("Second", [](const PropertyValue&) {
        return std::make_unique<Page>();
    });
    CHECK(frame.Navigate("First", std::string("one")));
    CHECK(frame.Navigate("Second", std::string("two")));
    CHECK(frame.CanGoBack());
    CHECK(frame.GoBack());
    CHECK(frame.CurrentEntry()->page_type == "First");
    CHECK(frame.CanGoForward());
    CHECK(frame.GoForward());
    CHECK(frame.CurrentEntry()->page_type == "Second");
}

void PageContentAlwaysUsesThePageBounds() {
    Page page;
    auto content = std::make_unique<Border>();
    Border* const content_pointer = content.get();
    page.SetContent(std::move(content));

    page.Measure({320.0, 200.0});
    CHECK(content_pointer->desired_size().width == 0.0);
    CHECK(content_pointer->desired_size().height == 0.0);

    page.Arrange({0.0, 0.0, 320.0, 200.0});
    CHECK(content_pointer->layout_slot().x == 0.0);
    CHECK(content_pointer->layout_slot().y == 0.0);
    CHECK(content_pointer->layout_slot().width == 320.0);
    CHECK(content_pointer->layout_slot().height == 200.0);
    CHECK(content_pointer->render_size().width == 320.0);
    CHECK(content_pointer->render_size().height == 200.0);
}

void UserControlContentAlwaysUsesTheControlBounds() {
    UserControl control;
    auto content = std::make_unique<FixedDesiredElement>();
    FixedDesiredElement* const content_pointer = content.get();
    control.SetContent(std::move(content));

    control.Measure({320.0, 200.0});
    CHECK(content_pointer->desired_size().width == 16.0);
    CHECK(content_pointer->desired_size().height == 16.0);

    control.Arrange({0.0, 0.0, 320.0, 200.0});
    CHECK(content_pointer->layout_slot().x == 0.0);
    CHECK(content_pointer->layout_slot().y == 0.0);
    CHECK(content_pointer->layout_slot().width == 320.0);
    CHECK(content_pointer->layout_slot().height == 200.0);
    CHECK(content_pointer->render_size().width == 320.0);
    CHECK(content_pointer->render_size().height == 200.0);
}

void PopupFlyoutAndMuxc() {
    Border placement;
    Flyout flyout;
    flyout.SetContent(std::make_unique<Border>());
    int events = 0;
    flyout.SetOpening([&] { ++events; });
    flyout.SetOpened([&] { ++events; });
    flyout.SetClosing([&] { ++events; });
    flyout.SetClosed([&] { ++events; });
    flyout.ShowAt(placement);
    CHECK(flyout.is_open());
    CHECK(flyout.placement_target() == &placement);
    flyout.Hide();
    CHECK(!flyout.is_open());
    CHECK(events == 4);

    NavigationView navigation;
    NumberBox number;
    TeachingTip tip;
    CHECK(navigation.TypeName() == "Microsoft.UI.Xaml.Controls.NavigationView");
    CHECK(number.TypeName() == "Microsoft.UI.Xaml.Controls.NumberBox");
    CHECK(tip.TypeName() == "Microsoft.UI.Xaml.Controls.TeachingTip");
}

void TabViewLaysOutItsItemCollection() {
    TabViewHarness row;
    TabViewItem* const first = row.AddTab(true);
    TabViewItem* const second = row.AddTab(false);

    row.Measure({500.0, 200.0});
    CHECK(row.desired_size().width == 240.0);
    CHECK(row.desired_size().height == 32.0);

    row.Arrange({0.0, 0.0, 500.0, 32.0});
    CHECK(first->layout_slot().x == 0.0);
    CHECK(first->layout_slot().width == 120.0);
    CHECK(second->layout_slot().x == 120.0);
    CHECK(second->layout_slot().width == 120.0);
    CHECK(first->selected());
    CHECK(!second->selected());

    int closes = 0;
    first->SetCloseRequested([&] { ++closes; });
    first->RequestClose();
    CHECK(closes == 1);
}

void TabViewItemMeasuresAndArrangesItsCloseAffordance() {
    TabViewItemHarness item;
    item.Measure({500.0, 32.0});
    CHECK(item.desired_size().width == 190.0);
    CHECK(item.close().has_layout_storage());
    CHECK(item.icon().has_layout_storage());

    item.Arrange({0.0, 0.0, item.desired_size().width, 32.0});
    CHECK(item.header().layout_slot().x == 36.0);
    CHECK(item.header().layout_slot().width == 110.0);
    CHECK(item.close().layout_slot().x == 158.0);
    CHECK(item.close().layout_slot().width == 32.0);
    CHECK(item.close().render_size().width == 32.0);
    CHECK(item.close().render_size().height == 32.0);
    CHECK(item.icon().layout_slot().x == 12.0);
    CHECK(item.icon().layout_slot().y == 8.0);
    CHECK(item.icon().render_size().width == 16.0);
    CHECK(item.icon().render_size().height == 16.0);
}

void ControlMarkup() {
    std::unique_ptr<Element> page = LoadMarkup("<Page><Border Width=\"20\"/></Page>");
    CHECK(page->TypeName() == "Windows.UI.Xaml.Controls.Page");
    CHECK(page->Children().size() == 1);

    std::unique_ptr<Element> list = LoadMarkup(
        "<ListView><Border Height=\"10\"/><Border Height=\"10\"/></ListView>");
    auto* items = dynamic_cast<ListView*>(list.get());
    CHECK(items != nullptr);
    CHECK(items->item_count() == 2);
    CHECK(items->ContainerFromIndex(0) != nullptr);

    std::unique_ptr<Element> mux = LoadMarkup(
        "<NavigationView><Border Width=\"30\"/></NavigationView>");
    CHECK(mux->TypeName() == "Microsoft.UI.Xaml.Controls.NavigationView");
    CHECK(mux->Children().size() == 1);
}

}  // namespace

int main() {
    VirtualizationAndSelection();
    NavigationJournal();
    PageContentAlwaysUsesThePageBounds();
    UserControlContentAlwaysUsesTheControlBounds();
    PopupFlyoutAndMuxc();
    TabViewLaysOutItsItemCollection();
    TabViewItemMeasuresAndArrangesItsCloseAffordance();
    ControlMarkup();
}
