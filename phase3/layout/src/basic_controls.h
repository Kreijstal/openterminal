#ifndef OPENXAML_BASIC_CONTROLS_H
#define OPENXAML_BASIC_CONTROLS_H

#include "control.h"

namespace openxaml {

class Button : public ContentControl {
public:
    std::string TypeName() const override { return "Windows.UI.Xaml.Controls.Button"; }
    static const std::vector<std::string>& Owners();
    const std::vector<std::string>& PropertyOwners() const override { return Owners(); }
};

// A one-child scaling decorator. Viewbox measures its child at the child's
// natural size, then uniformly scales and centers that visual into its final
// slot. This is the path Terminal's caption-button template uses to fit the
// MDL2 FontIcon into a 10-DIP square.
class Viewbox : public Element {
public:
    std::string TypeName() const override { return "Windows.UI.Xaml.Controls.Viewbox"; }
    bool IsLayoutElement() const override { return true; }
    static const std::vector<std::string>& Owners();
    const std::vector<std::string>& PropertyOwners() const override { return Owners(); }
    void SetChild(std::unique_ptr<Element> child);
    std::vector<Element*> Children() const override {
        return child_ ? std::vector<Element*>{child_.get()} : std::vector<Element*>{};
    }

protected:
    Size MeasureOverride(Size available) override;
    Size ArrangeOverride(Size final_size) override;

private:
    std::unique_ptr<Element> child_;
};

class ComboBox : public ContentControl {
public:
    ComboBox();
    std::string TypeName() const override { return "Windows.UI.Xaml.Controls.ComboBox"; }
protected:
    Size MeasureOverride(Size available) override;
    Size ArrangeOverride(Size final_size) override;
};

class ToggleSwitch : public ContentControl {
public:
    ToggleSwitch();
    std::string TypeName() const override { return "Windows.UI.Xaml.Controls.ToggleSwitch"; }
    void set_thumb(Element* value) { thumb_ = value; }
    void set_is_on(bool value) { is_on_ = value; InvalidateRender(true); }
protected:
    Size MeasureOverride(Size available) override;
    Size ArrangeOverride(Size final_size) override;
private:
    Element* thumb_ = nullptr;
    bool is_on_ = false;
};

class CheckBox : public Button {
public:
    std::string TypeName() const override { return "Windows.UI.Xaml.Controls.CheckBox"; }
    void set_indicator(Element* value) { indicator_ = value; }
protected:
    Size MeasureOverride(Size available) override;
    Size ArrangeOverride(Size final_size) override;
private:
    Element* indicator_ = nullptr;
};

class TextBox : public Control {
public:
    std::string TypeName() const override { return "Windows.UI.Xaml.Controls.TextBox"; }
    static const std::vector<std::string>& Owners();
    const std::vector<std::string>& PropertyOwners() const override { return Owners(); }
    const std::string& text() const { return GetString(TextProperty()); }
    void set_text(std::string value) { SetValue(TextProperty(), std::move(value)); }
    const std::string& placeholder_text() const { return GetString(PlaceholderTextProperty()); }
    void set_placeholder_text(std::string value) { SetValue(PlaceholderTextProperty(), std::move(value)); }
    static const DependencyProperty& TextProperty();
    static const DependencyProperty& PlaceholderTextProperty();
protected:
    Size MeasureOverride(Size available) override;
    Size ArrangeOverride(Size final_size) override { return final_size; }
};

class ToolTip : public ContentControl {
public:
    // Carries the default style generic.xaml gives the type, which is a fact
    // about a ToolTip rather than about the markup that made one -- see
    // basic_controls.cpp for the two values and what pins them.
    ToolTip();

    std::string TypeName() const override { return "Windows.UI.Xaml.Controls.ToolTip"; }
    static const std::vector<std::string>& Owners();
    const std::vector<std::string>& PropertyOwners() const override { return Owners(); }
};

class Thumb : public Control {
public:
    std::string TypeName() const override { return "Windows.UI.Xaml.Controls.Primitives.Thumb"; }
    static const std::vector<std::string>& Owners();
    const std::vector<std::string>& PropertyOwners() const override { return Owners(); }
};

}  // namespace openxaml
#endif
