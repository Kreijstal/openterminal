// Wave-4 framework controls: virtualization, navigation and overlays.

#ifndef OPENXAML_ADVANCED_CONTROLS_H
#define OPENXAML_ADVANCED_CONTROLS_H

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "control.h"

namespace openxaml {

class UserControl : public ContentControl {
public:
    std::string TypeName() const override { return "Windows.UI.Xaml.Controls.UserControl"; }
    static const std::vector<std::string>& Owners();
    const std::vector<std::string>& PropertyOwners() const override { return Owners(); }

protected:
    // UserControl is a Control with one direct content child. Unlike a generic
    // ContentControl, that child is measured with the complete available size
    // and arranged to the complete final bounds; content alignment and padding
    // belong to templated ContentControl presentation and do not constrain it.
    Size MeasureOverride(Size available) override;
    Size ArrangeOverride(Size final_size) override;
};

class Page : public ContentControl {
public:
    std::string TypeName() const override { return "Windows.UI.Xaml.Controls.Page"; }
    static const std::vector<std::string>& Owners();
    const std::vector<std::string>& PropertyOwners() const override { return Owners(); }

protected:
    // Page is a content host, but unlike a generic ContentControl its native
    // layout contract always gives Content the complete page bounds. This is
    // independent of Control.Horizontal/VerticalContentAlignment.
    Size MeasureOverride(Size available) override;
    Size ArrangeOverride(Size final_size) override;
};

struct NavigationEntry {
    std::string page_type;
    PropertyValue parameter = std::string{};
};

class Frame : public ContentControl {
public:
    using PageFactory = std::function<std::unique_ptr<Page>(const PropertyValue&)>;

    std::string TypeName() const override { return "Windows.UI.Xaml.Controls.Frame"; }
    static const std::vector<std::string>& Owners();
    const std::vector<std::string>& PropertyOwners() const override { return Owners(); }

    void RegisterPage(std::string type, PageFactory factory);
    bool Navigate(const std::string& type, PropertyValue parameter = std::string{});
    bool GoBack();
    bool GoForward();
    bool CanGoBack() const { return position_ > 0 && !journal_.empty(); }
    bool CanGoForward() const { return position_ + 1 < journal_.size(); }
    const NavigationEntry* CurrentEntry() const;
    size_t BackStackDepth() const { return journal_.empty() ? 0 : position_; }

private:
    bool Realize(size_t position);
    std::map<std::string, PageFactory> factories_;
    std::vector<NavigationEntry> journal_;
    size_t position_ = 0;
};

class ItemsControl : public Control {
public:
    using ItemFactory = std::function<std::unique_ptr<Element>(size_t)>;

    std::string TypeName() const override { return "Windows.UI.Xaml.Controls.ItemsControl"; }
    static const std::vector<std::string>& Owners();
    const std::vector<std::string>& PropertyOwners() const override { return Owners(); }

    void SetItems(size_t count, ItemFactory factory);
    size_t item_count() const { return item_count_; }
    void SetViewport(double offset, double extent);
    void set_item_extent(double value);
    double item_extent() const { return item_extent_; }
    void set_cache_length(size_t value) { cache_length_ = value; }

    std::pair<size_t, size_t> RealizedRange() const;
    Element* ContainerFromIndex(size_t index) const;
    size_t IndexFromContainer(const Element* container) const;
    std::vector<Element*> Children() const override;

protected:
    Size MeasureOverride(Size available) override;
    Size ArrangeOverride(Size final_size) override;
    virtual void OnContainerRealized(size_t index, Element& container);
    virtual void OnContainerRecycled(size_t index, Element& container);

private:
    void UpdateRealization();
    size_t item_count_ = 0;
    ItemFactory item_factory_;
    double viewport_offset_ = 0.0;
    double viewport_extent_ = 0.0;
    double item_extent_ = 32.0;
    size_t cache_length_ = 1;
    std::map<size_t, std::unique_ptr<Element>> realized_;
};

enum class SelectionMode { None, Single, Multiple, Extended };

class ListView : public ItemsControl {
public:
    using SelectionChanged = std::function<void(const std::vector<size_t>&)>;

    std::string TypeName() const override { return "Windows.UI.Xaml.Controls.ListView"; }
    static const std::vector<std::string>& Owners();
    const std::vector<std::string>& PropertyOwners() const override { return Owners(); }

    void set_selection_mode(SelectionMode mode);
    SelectionMode selection_mode() const { return selection_mode_; }
    bool Select(size_t index, bool extend = false);
    void ClearSelection();
    std::vector<size_t> selected_indices() const;
    void SetSelectionChanged(SelectionChanged callback) { selection_changed_ = std::move(callback); }

private:
    void NotifySelection();
    SelectionMode selection_mode_ = SelectionMode::Single;
    std::set<size_t> selected_;
    SelectionChanged selection_changed_;
};

class Popup : public ContentControl {
public:
    std::string TypeName() const override {
        return "Windows.UI.Xaml.Controls.Primitives.Popup";
    }
    static const std::vector<std::string>& Owners();
    const std::vector<std::string>& PropertyOwners() const override { return Owners(); }

    bool is_open() const { return is_open_; }
    void set_is_open(bool value) { is_open_ = value; }
    bool is_light_dismiss_enabled() const { return light_dismiss_; }
    void set_is_light_dismiss_enabled(bool value) { light_dismiss_ = value; }
    void LightDismiss() { if (light_dismiss_) is_open_ = false; }

protected:
    Size MeasureOverride(Size available) override;
    Size ArrangeOverride(Size final_size) override;

private:
    bool is_open_ = false;
    bool light_dismiss_ = false;
};

class Flyout {
public:
    using Event = std::function<void()>;

    void SetContent(std::unique_ptr<Element> content) { popup_.SetContent(std::move(content)); }
    void ShowAt(Element& placement_target);
    void Hide();
    bool is_open() const { return popup_.is_open(); }
    Element* placement_target() const { return placement_target_; }
    Popup& popup() { return popup_; }
    void SetOpening(Event event) { opening_ = std::move(event); }
    void SetOpened(Event event) { opened_ = std::move(event); }
    void SetClosing(Event event) { closing_ = std::move(event); }
    void SetClosed(Event event) { closed_ = std::move(event); }

private:
    Popup popup_;
    Element* placement_target_ = nullptr;
    Event opening_;
    Event opened_;
    Event closing_;
    Event closed_;
};

// WinUI 2 (muxc:) controls share ContentControl's layout contract until their
// generic.xaml templates are installed. Keeping one implementation with the
// exact runtime class name avoids a second, divergent content pipeline.
class MuxContentControl : public ContentControl {
public:
    explicit MuxContentControl(std::string name);
    std::string TypeName() const override { return type_name_; }
    const std::vector<std::string>& PropertyOwners() const override { return owners_; }

private:
    std::string type_name_;
    std::vector<std::string> owners_;
};

#define OPENXAML_MUXC_CONTROL(cpp_name, runtime_name)                    \
    class cpp_name : public MuxContentControl {                          \
    public:                                                              \
        cpp_name() : MuxContentControl("Microsoft.UI.Xaml.Controls." runtime_name) {} \
    }

OPENXAML_MUXC_CONTROL(BreadcrumbBar, "BreadcrumbBar");
OPENXAML_MUXC_CONTROL(ColorPicker, "ColorPicker");
OPENXAML_MUXC_CONTROL(DropDownButton, "DropDownButton");
OPENXAML_MUXC_CONTROL(Expander, "Expander");
OPENXAML_MUXC_CONTROL(InfoBadge, "InfoBadge");
OPENXAML_MUXC_CONTROL(InfoBar, "InfoBar");
OPENXAML_MUXC_CONTROL(NavigationViewItem, "NavigationViewItem");
OPENXAML_MUXC_CONTROL(ProgressRing, "ProgressRing");
OPENXAML_MUXC_CONTROL(SplitButton, "SplitButton");
OPENXAML_MUXC_CONTROL(TeachingTip, "TeachingTip");
OPENXAML_MUXC_CONTROL(TreeView, "TreeView");

#undef OPENXAML_MUXC_CONTROL

// NavigationView's WinUI template always gives its content presenter the
// available content region. Treating it as an ordinary ContentControl leaves
// an unsized settings Grid at 0x0 because ContentControl defaults to Left/Top.
class NavigationView : public MuxContentControl {
public:
    NavigationView()
        : MuxContentControl("Microsoft.UI.Xaml.Controls.NavigationView") {}

protected:
    Size MeasureOverride(Size available) override;
    Size ArrangeOverride(Size final_size) override;
};

class NumberBox : public MuxContentControl {
public:
    NumberBox();
protected:
    Size MeasureOverride(Size available) override;
    Size ArrangeOverride(Size final_size) override;
};

// TabView is populated through its WinRT TabItems vector rather than through
// ContentControl.Content. It therefore needs a real multi-child layout
// contract even before the complete WinUI generic.xaml template is available.
class TabView : public MuxContentControl {
public:
    TabView() : MuxContentControl("Microsoft.UI.Xaml.Controls.TabView") {}

protected:
    Size MeasureOverride(Size available) override;
    Size ArrangeOverride(Size final_size) override;
};

class TabViewItem : public MuxContentControl {
public:
    TabViewItem()
        : MuxContentControl("Microsoft.UI.Xaml.Controls.TabViewItem") {}

    void set_selected(bool value);
    bool selected() const { return selected_; }
    void SetCloseRequested(std::function<void()> callback) {
        close_requested_ = std::move(callback);
    }
    void RequestClose() {
        if (close_requested_) close_requested_();
    }

protected:
    Size MeasureOverride(Size available) override;
    Size ArrangeOverride(Size final_size) override;

private:
    bool selected_ = false;
    std::function<void()> close_requested_;
};

}  // namespace openxaml

#endif  // OPENXAML_ADVANCED_CONTROLS_H
