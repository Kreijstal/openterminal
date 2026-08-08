// ScrollViewer layout: a single content viewport with optional scrollbar
// reservations. Painting, input and compositor scrolling sit above this core;
// this class owns the observable measure/arrange contract.

#ifndef OPENXAML_SCROLL_VIEWER_H
#define OPENXAML_SCROLL_VIEWER_H

#include <memory>
#include <string>
#include <vector>

#include "control.h"

namespace openxaml {

enum class ScrollBarVisibility { Disabled, Auto, Hidden, Visible };
enum class ScrollMode { Disabled, Enabled, Auto };

class ScrollViewer : public ContentControl {
public:
    std::string TypeName() const override { return "Windows.UI.Xaml.Controls.ScrollViewer"; }
    static const std::vector<std::string>& Owners();
    const std::vector<std::string>& PropertyOwners() const override { return Owners(); }

    void SetContent(std::unique_ptr<Element> content);
    std::vector<Element*> Children() const override {
        return content_ ? std::vector<Element*>{content_.get()} : std::vector<Element*>{};
    }

    const Thickness& padding() const { return GetThickness(PaddingProperty()); }
    void set_padding(Thickness value) { SetValue(PaddingProperty(), value); }
    ScrollBarVisibility horizontal_scroll_bar_visibility() const {
        return static_cast<ScrollBarVisibility>(GetInt(HorizontalScrollBarVisibilityProperty()));
    }
    void set_horizontal_scroll_bar_visibility(ScrollBarVisibility value) {
        SetValue(HorizontalScrollBarVisibilityProperty(), static_cast<int>(value));
    }
    ScrollBarVisibility vertical_scroll_bar_visibility() const {
        return static_cast<ScrollBarVisibility>(GetInt(VerticalScrollBarVisibilityProperty()));
    }
    void set_vertical_scroll_bar_visibility(ScrollBarVisibility value) {
        SetValue(VerticalScrollBarVisibilityProperty(), static_cast<int>(value));
    }
    ScrollMode horizontal_scroll_mode() const {
        return static_cast<ScrollMode>(GetInt(HorizontalScrollModeProperty()));
    }
    void set_horizontal_scroll_mode(ScrollMode value) {
        SetValue(HorizontalScrollModeProperty(), static_cast<int>(value));
    }
    ScrollMode vertical_scroll_mode() const {
        return static_cast<ScrollMode>(GetInt(VerticalScrollModeProperty()));
    }
    void set_vertical_scroll_mode(ScrollMode value) {
        SetValue(VerticalScrollModeProperty(), static_cast<int>(value));
    }

    bool horizontal_bar_visible() const { return horizontal_bar_visible_; }
    bool vertical_bar_visible() const { return vertical_bar_visible_; }
    Size extent() const { return extent_; }
    Size viewport() const { return viewport_; }
    void ScrollTo(double horizontal, double vertical);
    double horizontal_offset() const { return horizontal_offset_; }
    double vertical_offset() const { return vertical_offset_; }

    static const DependencyProperty& PaddingProperty();
    static const DependencyProperty& HorizontalScrollBarVisibilityProperty();
    static const DependencyProperty& VerticalScrollBarVisibilityProperty();
    static const DependencyProperty& HorizontalScrollModeProperty();
    static const DependencyProperty& VerticalScrollModeProperty();
    static const DependencyProperty& BringIntoViewOnFocusChangeProperty();
    static const DependencyProperty& IsVerticalScrollChainingEnabledProperty();

protected:
    Size MeasureOverride(Size available) override;
    Size ArrangeOverride(Size final_size) override;

private:
    bool AxisCanScroll(ScrollBarVisibility visibility, ScrollMode mode) const;
    std::unique_ptr<Element> content_;
    Size extent_;
    Size viewport_;
    bool horizontal_bar_visible_ = false;
    bool vertical_bar_visible_ = false;
    double horizontal_offset_ = 0.0;
    double vertical_offset_ = 0.0;
};

}  // namespace openxaml

#endif
