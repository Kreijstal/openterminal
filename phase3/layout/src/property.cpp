#include "property.h"

#include <memory>

namespace openxaml {
namespace {

// The registry. Held in function-local statics so that a property registered
// at static-initialisation time -- which every one of them is -- cannot run
// before the tables it registers into exist.
struct Registry {
    std::vector<std::unique_ptr<DependencyProperty>> properties;
    std::map<std::pair<std::string, std::string>, const DependencyProperty*> by_owner_and_name;
    std::vector<const DependencyProperty*> inherited;
};

Registry& Table() {
    static Registry registry;
    return registry;
}

}  // namespace

bool SameValue(const PropertyValue& a, const PropertyValue& b) {
    if (a.index() != b.index()) return false;
    if (const double* left = std::get_if<double>(&a)) {
        const double right = std::get<double>(b);
        // Two Autos are the same Auto. Everything else compares exactly: the
        // store is not layout and has no business applying a tolerance.
        if (std::isnan(*left) && std::isnan(right)) return true;
        return *left == right;
    }
    return a == b;
}

const DependencyProperty* RegisterProperty(std::string owner, std::string name,
                                           PropertyMetadata metadata) {
    Registry& registry = Table();
    const auto key = std::make_pair(owner, name);
    if (registry.by_owner_and_name.count(key))
        throw PropertyError("the property '" + name + "' is already registered on '" + owner + "'");

    registry.properties.push_back(std::make_unique<DependencyProperty>(
        std::move(owner), std::move(name), std::move(metadata), registry.properties.size()));
    const DependencyProperty* property = registry.properties.back().get();
    registry.by_owner_and_name[key] = property;
    if (property->inherits()) registry.inherited.push_back(property);
    return property;
}

const DependencyProperty* FindProperty(const std::vector<std::string>& owners,
                                       const std::string& name) {
    const Registry& registry = Table();

    // A dotted name carries its own owner: Grid.Column is Grid's property
    // whatever it is written on, and resolving it against the element's chain
    // would refuse it everywhere except on a Grid.
    const size_t dot = name.find('.');
    if (dot != std::string::npos) {
        const auto found = registry.by_owner_and_name.find({name.substr(0, dot), name});
        return found == registry.by_owner_and_name.end() ? nullptr : found->second;
    }

    for (const std::string& owner : owners) {
        const auto found = registry.by_owner_and_name.find({owner, name});
        if (found != registry.by_owner_and_name.end()) return found->second;
    }
    return nullptr;
}

const std::vector<const DependencyProperty*>& InheritedProperties() { return Table().inherited; }

// --- DependencyObject ---------------------------------------------------------

const PropertyValue& DependencyObject::GetValue(const DependencyProperty& property) const {
    const auto found = local_.find(property.index());
    if (found != local_.end()) return found->second;

    if (property.inherits()) {
        // Up the chain rather than out of a cache. The trees the corpus
        // measures are a handful of elements deep, and a cache that can go
        // stale is a source of wrong answers that a walk cannot have.
        for (const DependencyObject* ancestor = inheritance_parent_; ancestor != nullptr;
             ancestor = ancestor->inheritance_parent_) {
            const auto inherited = ancestor->local_.find(property.index());
            if (inherited != ancestor->local_.end()) return inherited->second;
        }
    }

    return property.default_value();
}

void DependencyObject::SetValue(const DependencyProperty& property, PropertyValue value) {
    const PropertyValue before = GetValue(property);
    local_[property.index()] = std::move(value);
    if (SameValue(before, GetValue(property))) return;

    OnPropertyChanged(property);
    if (property.inherits()) {
        for (DependencyObject* child : InheritanceChildren())
            child->InvalidateInherited(property, before);
    }
}

void DependencyObject::ClearValue(const DependencyProperty& property) {
    const auto found = local_.find(property.index());
    if (found == local_.end()) return;

    const PropertyValue before = found->second;
    local_.erase(found);
    if (SameValue(before, GetValue(property))) return;

    OnPropertyChanged(property);
    if (property.inherits()) {
        for (DependencyObject* child : InheritanceChildren())
            child->InvalidateInherited(property, before);
    }
}

bool DependencyObject::HasLocalValue(const DependencyProperty& property) const {
    return local_.count(property.index()) != 0;
}

double DependencyObject::GetDouble(const DependencyProperty& property) const {
    const PropertyValue& value = GetValue(property);
    if (const double* number = std::get_if<double>(&value)) return *number;
    throw PropertyError("the property '" + property.name() + "' does not hold a double");
}

int DependencyObject::GetInt(const DependencyProperty& property) const {
    const PropertyValue& value = GetValue(property);
    if (const int* number = std::get_if<int>(&value)) return *number;
    throw PropertyError("the property '" + property.name() + "' does not hold an int");
}

bool DependencyObject::GetBool(const DependencyProperty& property) const {
    const PropertyValue& value = GetValue(property);
    if (const bool* flag = std::get_if<bool>(&value)) return *flag;
    throw PropertyError("the property '" + property.name() + "' does not hold a bool");
}

const Thickness& DependencyObject::GetThickness(const DependencyProperty& property) const {
    const PropertyValue& value = GetValue(property);
    if (const Thickness* thickness = std::get_if<Thickness>(&value)) return *thickness;
    throw PropertyError("the property '" + property.name() + "' does not hold a Thickness");
}

const std::string& DependencyObject::GetString(const DependencyProperty& property) const {
    const PropertyValue& value = GetValue(property);
    if (const std::string* text = std::get_if<std::string>(&value)) return *text;
    throw PropertyError("the property '" + property.name() + "' does not hold a string");
}

void DependencyObject::SetInheritanceParent(DependencyObject* parent) {
    if (inheritance_parent_ == parent) return;

    // Every inherited property is compared before and after, not just the ones
    // this object has heard of: the value it was reading came from the old
    // parent's store, and there is nothing here that records which.
    const std::vector<const DependencyProperty*>& inherited = InheritedProperties();
    std::vector<PropertyValue> before;
    before.reserve(inherited.size());
    for (const DependencyProperty* property : inherited) before.push_back(GetValue(*property));

    inheritance_parent_ = parent;
    InvalidateAllInherited(before);
}

void DependencyObject::OnPropertyChanged(const DependencyProperty& property) { (void)property; }

void DependencyObject::InvalidateInherited(const DependencyProperty& property,
                                           const PropertyValue& before) {
    // A local value shadows whatever the ancestor did, so the subtree below it
    // did not move either. This is the stop condition that keeps re-parenting
    // proportional to what actually changed.
    if (HasLocalValue(property)) return;
    if (SameValue(before, GetValue(property))) return;

    OnPropertyChanged(property);
    for (DependencyObject* child : InheritanceChildren())
        child->InvalidateInherited(property, before);
}

void DependencyObject::InvalidateAllInherited(const std::vector<PropertyValue>& before) {
    const std::vector<const DependencyProperty*>& inherited = InheritedProperties();
    for (size_t i = 0; i < inherited.size(); ++i) {
        const DependencyProperty& property = *inherited[i];
        if (HasLocalValue(property)) continue;
        if (SameValue(before[i], GetValue(property))) continue;
        OnPropertyChanged(property);
        for (DependencyObject* child : InheritanceChildren())
            child->InvalidateInherited(property, before[i]);
    }
}

}  // namespace openxaml
