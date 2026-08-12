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
