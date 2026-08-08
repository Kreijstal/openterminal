// Built-in generic.xaml styles and control templates.

#ifndef OPENXAML_DEFAULT_STYLES_H
#define OPENXAML_DEFAULT_STYLES_H

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "control.h"
#include "style.h"

namespace openxaml {

class ControlTemplate {
public:
    using Builder = std::function<std::unique_ptr<Element>(Control&)>;

    ControlTemplate(std::string target_type, Builder builder)
        : target_type_(std::move(target_type)), builder_(std::move(builder)) {}

    const std::string& target_type() const { return target_type_; }
    std::unique_ptr<Element> Build(Control& parent) const;

private:
    std::string target_type_;
    Builder builder_;
};

// A {TemplateBinding} is a one-way expression from the templated parent. It
// stays live after ApplyTemplate so later property changes reach the part.
class TemplateBindingExpression {
public:
    TemplateBindingExpression(DependencyObject& parent, const DependencyProperty& source,
                              DependencyObject& target, const DependencyProperty& destination);
    ~TemplateBindingExpression();
    TemplateBindingExpression(const TemplateBindingExpression&) = delete;
    TemplateBindingExpression& operator=(const TemplateBindingExpression&) = delete;

private:
    DependencyObject* parent_;
    const DependencyProperty* source_;
    DependencyObject* target_;
    const DependencyProperty* destination_;
    DependencyObject::PropertyChangedToken token_ = 0;
};

struct DefaultControlStyle {
    Style style;
    std::shared_ptr<const ControlTemplate> control_template;
};

class DefaultStyleRegistry {
public:
    static DefaultStyleRegistry& Default();

    void Register(std::string type, DefaultControlStyle style);
    const DefaultControlStyle* Find(const std::string& type) const;
    bool Apply(Control& control, const std::string& type,
               const std::vector<std::string>& owners) const;

private:
    std::map<std::string, DefaultControlStyle> styles_;
};

}  // namespace openxaml

#endif  // OPENXAML_DEFAULT_STYLES_H
