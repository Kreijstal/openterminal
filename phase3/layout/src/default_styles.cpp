#include "default_styles.h"

#include "markup.h"

namespace openxaml {

std::unique_ptr<Element> ControlTemplate::Build(Control& parent) const {
    if (!builder_) throw MarkupError("the ControlTemplate for '" + target_type_ + "' has no body");
    return builder_(parent);
}

TemplateBindingExpression::TemplateBindingExpression(
    DependencyObject& parent, const DependencyProperty& source, DependencyObject& target,
    const DependencyProperty& destination)
    : parent_(&parent), source_(&source), target_(&target), destination_(&destination) {
    target_->SetValue(*destination_, parent_->GetValue(*source_));
    token_ = parent_->AddPropertyChangedHandler(
        [this](DependencyObject&, const DependencyProperty& property, const PropertyValue& value) {
            if (&property == source_) target_->SetValue(*destination_, value);
        });
}

TemplateBindingExpression::~TemplateBindingExpression() {
    if (parent_ && token_) parent_->RemovePropertyChangedHandler(token_);
}

DefaultStyleRegistry& DefaultStyleRegistry::Default() {
    static DefaultStyleRegistry registry;
    return registry;
}

void DefaultStyleRegistry::Register(std::string type, DefaultControlStyle style) {
    if (styles_.count(type))
        throw MarkupError("the default style for '" + type + "' is registered twice");
    if (style.style.target_type.empty()) style.style.target_type = type;
    styles_.emplace(std::move(type), std::move(style));
}

const DefaultControlStyle* DefaultStyleRegistry::Find(const std::string& type) const {
    const auto found = styles_.find(type);
    return found == styles_.end() ? nullptr : &found->second;
}

bool DefaultStyleRegistry::Apply(Control& control, const std::string& type,
                                 const std::vector<std::string>& owners) const {
    const DefaultControlStyle* found = Find(type);
    if (!found) return false;
    ApplyStyle(control, found->style, type, owners);
    control.SetTemplate(found->control_template);
    return true;
}

}  // namespace openxaml
