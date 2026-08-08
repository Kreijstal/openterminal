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
    PopupFlyoutAndMuxc();
    ControlMarkup();
}
