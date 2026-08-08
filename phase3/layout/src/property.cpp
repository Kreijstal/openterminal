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

namespace {

const DependencyProperty* Register(std::string owner, std::string name,
                                   PropertyMetadata metadata, bool attached) {
    Registry& registry = Table();
    const auto key = std::make_pair(owner, name);
    if (registry.by_owner_and_name.count(key))
        throw PropertyError("the property '" + name + "' is already registered on '" + owner + "'");

    registry.properties.push_back(std::make_unique<DependencyProperty>(
        std::move(owner), std::move(name), std::move(metadata), registry.properties.size(),
        attached));
    const DependencyProperty* property = registry.properties.back().get();
    registry.by_owner_and_name[key] = property;
    if (property->inherits()) registry.inherited.push_back(property);
    return property;
}

}  // namespace

const DependencyProperty* RegisterProperty(std::string owner, std::string name,
                                           PropertyMetadata metadata) {
    return Register(std::move(owner), std::move(name), std::move(metadata), false);
}

const DependencyProperty* RegisterAttachedProperty(std::string owner, std::string name,
                                                   PropertyMetadata metadata) {
    // Filed under the qualified name, because that is the only name an
    // attached property is ever written or looked up by -- see FindProperty,
    // where a dotted name resolves against the registry directly instead of
    // against the object's owner chain.
    if (name.find('.') != std::string::npos)
        throw PropertyError("an attached property is registered under its bare name, not '" +
                            name + "'");
    std::string qualified = owner + "." + name;
    return Register(std::move(owner), std::move(qualified), std::move(metadata), true);
}

const DependencyProperty* FindPropertyOnOwner(const std::string& owner, const std::string& name) {
    const Registry& registry = Table();
    const auto found = registry.by_owner_and_name.find({owner, name});
    if (found != registry.by_owner_and_name.end()) return found->second;
    // A caller that named the owner may still be spelling an attached property
    // by its bare name, which is filed qualified.
    const auto attached = registry.by_owner_and_name.find({owner, owner + "." + name});
    return attached == registry.by_owner_and_name.end() ? nullptr : attached->second;
}

const DependencyProperty* FindProperty(const std::vector<std::string>& owners,
                                       const std::string& name) {
    const Registry& registry = Table();

    // A dotted name carries its own owner: Grid.Column is Grid's property
    // whatever it is written on, and resolving it against the element's chain
    // would refuse it everywhere except on a Grid.
    //
    // The *last* dot, not the first: an owner is a type name and a type name
    // may itself be qualified. `Grid.Row` has one dot either way, and a
    // property a caller registers through the ABI against
    // `TerminalApp.TitlebarControl` has three.
    const size_t dot = name.rfind('.');
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

namespace {

// The tokens of a handler map, in registration order, as a snapshot. Taken
// before any handler runs, because a handler may add or remove others.
std::vector<DependencyObject::PropertyChangedToken> TokensOf(
    const std::map<DependencyObject::PropertyChangedToken,
                   DependencyObject::PropertyChangedHandler>& handlers) {
    std::vector<DependencyObject::PropertyChangedToken> tokens;
    tokens.reserve(handlers.size());
    for (const auto& entry : handlers) tokens.push_back(entry.first);
    return tokens;
}

}  // namespace

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
    // And the framework's own style below that, which is where a Control's
    // Padding, MinHeight and FontSize come from when nothing else says.
    const auto built_in = built_in_style_.find(property.index());
    if (built_in != built_in_style_.end()) return &built_in->second;
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

    // The order the three kinds of observer run in is the runtime's, from
    // dxaml `DependencyObject::NotifyPropertyChanged`
    // (dxaml/xcp/dxaml/lib/DependencyObject.cpp, 188f602b):
    //
    //   1. the type's own OnPropertyChanged -- what a built-in property uses,
    //      and what every property this repository registers uses;
    //   2. the metadata's PropertyChangedCallback, which is the one a caller
    //      supplies at Register time and which the runtime invokes before it
    //      raises anything;
    //   3. the per-object observers.
    //
    // Between 2 and 3 the runtime raises its DPChanged event source, which is
    // the binding engine's hook; the whole-object handler list here is this
    // implementation's equivalent of it and sits in the same place, so a
    // binding still refreshes before a caller's RegisterPropertyChangedCallback
    // sees the value.
    OnPropertyChanged(property);
    const PropertyValue after = GetValue(property);
    if (property.metadata().changed) property.metadata().changed(*this, property, before, after);

    // A handler is allowed to detach itself, or any other. Both loops therefore
    // walk a snapshot of the tokens and look each one up again before calling
    // it, so a callback that another callback removed is never invoked. The
    // per-property loop re-finds its map as well, because removing the last
    // handler for a property erases the map itself.
    for (PropertyChangedToken token : TokensOf(property_changed_handlers_)) {
        const auto found = property_changed_handlers_.find(token);
        if (found != property_changed_handlers_.end()) found->second(*this, property, after);
    }

    const auto per_property = per_property_handlers_.find(property.index());
    if (per_property != per_property_handlers_.end()) {
        for (PropertyChangedToken token : TokensOf(per_property->second)) {
            const auto handlers = per_property_handlers_.find(property.index());
            if (handlers == per_property_handlers_.end()) break;
            const auto found = handlers->second.find(token);
            if (found != handlers->second.end()) found->second(*this, property, after);
        }
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

const PropertyValue* DependencyObject::ReadLocalValue(const DependencyProperty& property) const {
    const auto found = local_.find(property.index());
    return found == local_.end() ? nullptr : &found->second;
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

void DependencyObject::SetBuiltInStyleValue(const DependencyProperty& property,
                                            PropertyValue value) {
    const PropertyValue before = GetValue(property);
    built_in_style_[property.index()] = std::move(value);
    ValueMoved(property, before);
}

void DependencyObject::ClearBuiltInStyleValues() {
    if (built_in_style_.empty()) return;

    // Read the before-values while the slots are filled, notify once they are
    // all gone -- the same whole-style rule ClearStyleValues follows, and for
    // the same reason.
    std::vector<std::pair<const DependencyProperty*, PropertyValue>> before;
    before.reserve(built_in_style_.size());
    for (const auto& [index, value] : built_in_style_) {
        const DependencyProperty* property = PropertyByIndex(index);
        if (!property) throw PropertyError("a built-in style value is filed under no known property");
        before.emplace_back(property, GetValue(*property));
    }
    built_in_style_.clear();
    for (const auto& [property, was] : before) ValueMoved(*property, was);
}

bool DependencyObject::HasBuiltInStyleValue(const DependencyProperty& property) const {
    return built_in_style_.count(property.index()) != 0;
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

DependencyObject::PropertyChangedToken DependencyObject::RegisterPropertyChangedCallback(
    const DependencyProperty& property, PropertyChangedHandler handler) {
    if (!handler) throw PropertyError("a property-changed callback cannot be empty");
    // One token sequence for both kinds of observer. They are unregistered
    // through different calls, so the sequences could be separate; sharing one
    // means a token never names a registration on the same object twice, which
    // is the property a caller that mislays which list a token came from needs.
    const PropertyChangedToken token = next_property_changed_token_++;
    per_property_handlers_[property.index()].emplace(token, std::move(handler));
    return token;
}

void DependencyObject::UnregisterPropertyChangedCallback(const DependencyProperty& property,
                                                         PropertyChangedToken token) {
    const auto handlers = per_property_handlers_.find(property.index());
    if (handlers == per_property_handlers_.end()) return;
    handlers->second.erase(token);
    // The runtime destroys a property's event source with its last handler
    // (dxaml DependencyObject::UnregisterPropertyChangedCallback). Doing the
    // same keeps "has this property any observers" a lookup rather than a scan.
    if (handlers->second.empty()) per_property_handlers_.erase(handlers);
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
