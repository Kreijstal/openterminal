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

const DependencyProperty* PropertyByIndex(size_t index) {
    const Registry& registry = Table();
    if (index >= registry.properties.size()) return nullptr;
    return registry.properties[index].get();
}

// --- DependencyObject ---------------------------------------------------------

const PropertyValue* DependencyObject::OwnValue(const DependencyProperty& property) const {
    const auto animated = animated_.find(property.index());
    if (animated != animated_.end()) return &animated->second;
    const auto local = local_.find(property.index());
    if (local != local_.end()) return &local->second;
    // Below a local value and above an inherited one. See the precedence note
    // at the top of property.h -- this ordering is the whole of what a style
    // slot means.
    const auto styled = style_.find(property.index());
    if (styled != style_.end()) return &styled->second;
    return nullptr;
}

const PropertyValue& DependencyObject::GetValue(const DependencyProperty& property) const {
    if (const PropertyValue* own = OwnValue(property)) return *own;

    if (property.inherits()) {
        // Up the chain rather than out of a cache. The trees the corpus
        // measures are a handful of elements deep, and a cache that can go
        // stale is a source of wrong answers that a walk cannot have.
        //
        // What an ancestor contributes is its effective value, not just what
        // markup wrote on it: a FontSize a style set on a Grid reaches the
        // TextBlock inside it exactly as a written one does.
        for (const DependencyObject* ancestor = inheritance_parent_; ancestor != nullptr;
             ancestor = ancestor->inheritance_parent_) {
            if (const PropertyValue* inherited = ancestor->OwnValue(property)) return *inherited;
        }
    }

    return property.default_value();
}

void DependencyObject::ValueMoved(const DependencyProperty& property,
                                  const PropertyValue& before) {
    if (SameValue(before, GetValue(property))) return;

    OnPropertyChanged(property);
    const PropertyValue after = GetValue(property);
    // A handler is allowed to detach itself. Copying the token set keeps that
    // safe without retaining a callback that another handler removed.
    std::vector<PropertyChangedToken> tokens;
    tokens.reserve(property_changed_handlers_.size());
    for (const auto& [token, handler] : property_changed_handlers_) {
        (void)handler;
        tokens.push_back(token);
    }
    for (PropertyChangedToken token : tokens) {
        const auto found = property_changed_handlers_.find(token);
        if (found != property_changed_handlers_.end()) found->second(*this, property, after);
    }
    if (property.inherits()) {
        for (DependencyObject* child : InheritanceChildren())
            child->InvalidateInherited(property, before);
    }
}

void DependencyObject::SetValue(const DependencyProperty& property, PropertyValue value) {
    const PropertyValue before = GetValue(property);
    local_[property.index()] = std::move(value);
    ValueMoved(property, before);
}

void DependencyObject::ClearValue(const DependencyProperty& property) {
    const auto found = local_.find(property.index());
    if (found == local_.end()) return;

    const PropertyValue before = found->second;
    local_.erase(found);
    ValueMoved(property, before);
}

bool DependencyObject::HasLocalValue(const DependencyProperty& property) const {
    return local_.count(property.index()) != 0;
}

void DependencyObject::SetStyleValue(const DependencyProperty& property, PropertyValue value) {
    const PropertyValue before = GetValue(property);
    style_[property.index()] = std::move(value);
    ValueMoved(property, before);
}

void DependencyObject::ClearStyleValues() {
    if (style_.empty()) return;

    // The before-values are read while the slots are still filled, and the
    // notifications are sent once they are all gone. Doing it one at a time
    // would report intermediate states that never existed as an effective
    // value -- a style is applied and removed whole.
    std::vector<std::pair<const DependencyProperty*, PropertyValue>> before;
    before.reserve(style_.size());
    for (const auto& [index, value] : style_) {
        const DependencyProperty* property = PropertyByIndex(index);
        if (!property) throw PropertyError("a style value is filed under no known property");
        before.emplace_back(property, GetValue(*property));
    }
    style_.clear();
    for (const auto& [property, was] : before) ValueMoved(*property, was);
}

bool DependencyObject::HasStyleValue(const DependencyProperty& property) const {
    return style_.count(property.index()) != 0;
}

void DependencyObject::SetAnimatedValue(const DependencyProperty& property, PropertyValue value) {
    const PropertyValue before = GetValue(property);
    animated_[property.index()] = std::move(value);
    ValueMoved(property, before);
}

void DependencyObject::ClearAnimatedValue(const DependencyProperty& property) {
    const auto found = animated_.find(property.index());
    if (found == animated_.end()) return;
    const PropertyValue before = found->second;
    animated_.erase(found);
    ValueMoved(property, before);
}

void DependencyObject::ClearAnimatedValues() {
    if (animated_.empty()) return;
    std::vector<std::pair<const DependencyProperty*, PropertyValue>> before;
    before.reserve(animated_.size());
    for (const auto& [index, value] : animated_) {
        (void)value;
        const DependencyProperty* property = PropertyByIndex(index);
        if (!property) throw PropertyError("an animated value is filed under no known property");
        before.emplace_back(property, GetValue(*property));
    }
    animated_.clear();
    for (const auto& [property, was] : before) ValueMoved(*property, was);
}

bool DependencyObject::HasAnimatedValue(const DependencyProperty& property) const {
    return animated_.count(property.index()) != 0;
}

DependencyObject::PropertyChangedToken DependencyObject::AddPropertyChangedHandler(
    PropertyChangedHandler handler) {
    if (!handler) throw PropertyError("a property-changed handler cannot be empty");
    const PropertyChangedToken token = next_property_changed_token_++;
    property_changed_handlers_.emplace(token, std::move(handler));
    return token;
}

void DependencyObject::RemovePropertyChangedHandler(PropertyChangedToken token) {
    property_changed_handlers_.erase(token);
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
    // A value of this object's own -- local or from its style -- shadows
    // whatever the ancestor did, so the subtree below it did not move either.
    // This is the stop condition that keeps re-parenting proportional to what
    // actually changed, and it has to agree with GetValue about what counts as
    // "its own" or an element under a style would be told about changes it
    // cannot see.
    if (OwnValue(property) != nullptr) return;
    if (SameValue(before, GetValue(property))) return;

    OnPropertyChanged(property);
    for (DependencyObject* child : InheritanceChildren())
        child->InvalidateInherited(property, before);
}

void DependencyObject::InvalidateAllInherited(const std::vector<PropertyValue>& before) {
    const std::vector<const DependencyProperty*>& inherited = InheritedProperties();
    for (size_t i = 0; i < inherited.size(); ++i) {
        const DependencyProperty& property = *inherited[i];
        if (OwnValue(property) != nullptr) continue;
        if (SameValue(before[i], GetValue(property))) continue;
        OnPropertyChanged(property);
        for (DependencyObject* child : InheritanceChildren())
            child->InvalidateInherited(property, before[i]);
    }
}

}  // namespace openxaml
