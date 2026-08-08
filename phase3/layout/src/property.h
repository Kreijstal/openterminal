// The dependency-property engine.
//
// A XAML property is not a field. Its effective value is chosen from a
// precedence chain rather than stored once: a local value beats one inherited
// from an ancestor, which beats the default the property was registered with.
// Clearing a local value restores whatever was underneath it, which a field
// cannot express -- for most types there is no value that means "unset", and
// zero is a perfectly good Opacity.
//
// Ported in shape from dotnet/wpf's DependencyObject and its property store
// (MIT, 2ca037562c207924e53cfcc99286e523d3694de3), reduced to the sources the
// corpus can currently see. WPF's chain has a dozen entries -- coercion,
// animation, triggers, styles, template parents. Every source beyond the four
// below is still a level of the corpus that has not been reached yet.
//
// Five of them are here now, in this order, highest first:
//
//   animation  active storyboards and VisualState setters
//   local      what the markup wrote on the element itself
//   style      what the element's Style says, BasedOn already merged
//   built-in   what the framework's own generic.xaml says for this type
//   inherited  the nearest ancestor's effective value
//   default    what the property was registered with
//
// That order is WPF's `BaseValueSourceInternal`, where Local outranks Style and
// Style outranks Inherited. The consequence worth stating, because it is the
// one that surprises: a style setter beats a value inherited from an ancestor,
// so a TextBlock under a FontSize="22" control measures at whatever its style
// says and not at 22. Inheritance reads the ancestor's *effective* value, so a
// FontSize a style set does flow down to elements that have neither.
//
// The built-in slot is the newest and the one with the sharpest justification.
// microsoft-ui-xaml (MIT, 188f602b) carries `docs/design-notes/styles.md`,
// which states the rule plainly: the property system has a
// `BaseValueSourceStyle` layer and a `BaseValueSourceBuiltInStyle` layer, the
// two coexist, and where both set the same property the `Style` layer wins.
// `CControl::ApplyBuiltInStyle` is the only thing that ever writes the lower
// one, and it writes the style `Control.DefaultStyleKey` names in
// generic.xaml. Two layers rather than one is not an optimisation: a control
// whose application-level implicit style sets only `Padding` still gets its
// `Template` from generic.xaml, and a single slot would have the narrower
// style erase the wider one.
//
// The whole chain is looked up in one place, so the next source -- a trigger
// or coercion -- slots in beside these rather than being threaded through
// callers.

#ifndef OPENXAML_PROPERTY_H
#define OPENXAML_PROPERTY_H

#include <map>
#include <functional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "layout.h"

namespace openxaml {

// Thrown for a property used against its own registration -- read as the wrong
// type, or registered twice. Never for anything a case can write: markup that
// names a property no type has is a MarkupError, which is what the runtime
// reports for it.
class PropertyError : public std::runtime_error {
public:
    explicit PropertyError(const std::string& what) : std::runtime_error(what) {}
};

// What a property can hold. XAML's store is `object`; this is the closed set
// the corpus can write, which keeps values copyable and comparable without
// dragging in a type system. Enumerations ride as int, the way a boxed enum
// does in the runtime.
//
// std::monostate is null -- what a property whose type is `object` holds when
// it holds nothing, which is what `PropertyMetadata(nullptr)` registers as a
// default. No type in the layout core ever produces one and no measurement can
// contain one; it exists because a property registered through the ABI may be
// declared that way, and storing a plausible zero for it instead would be a
// wrong value rather than an absent one. Every typed read refuses it by name.
using PropertyValue =
    std::variant<double, int, bool, Thickness, std::string, std::monostate>;

// Whether two values are the same as far as anything reading the store is
// concerned. NaN needs the special case rather than `==`: it is how XAML
// spells Auto, and NaN != NaN would report an unset Width as changing every
// time it was assigned.
bool SameValue(const PropertyValue& a, const PropertyValue& b);

class DependencyObject;
class DependencyProperty;

// What PropertyMetadata's second constructor argument is in WinUI: a callback
// the property itself carries, run whenever an object's effective value for it
// moves. Registered against the property rather than against an object, so
// every instance gets it -- which is what makes it different from
// RegisterPropertyChangedCallback, which is per object.
using PropertyChangedCallback =
    std::function<void(DependencyObject&, const DependencyProperty&,
                       const PropertyValue& old_value, const PropertyValue& new_value)>;

struct PropertyMetadata {
    // Spelled out rather than left an aggregate, so that adding a field does
    // not turn every `{value, false, true}` at a registration site into a
    // missing-initializer warning -- and so that a caller supplying only a
    // default value, which is PropertyMetadata's one-argument constructor in
    // the runtime, reads the same here.
    PropertyMetadata() = default;
    PropertyMetadata(PropertyValue value, bool inherits_value = false,
                     bool affects_measure_value = false,
                     PropertyChangedCallback changed_callback = {})
        : default_value(std::move(value)),
          inherits(inherits_value),
          affects_measure(affects_measure_value),
          changed(std::move(changed_callback)) {}

    PropertyValue default_value;

    // Inherited properties fall through to an ancestor when they have no local
    // value -- FontSize set on a Control reaches the content inside it. This
    // is fixed at registration, as FrameworkPropertyMetadata.Inherits is: it
    // is a fact about the property, not about an object.
    bool inherits = false;

    // Changing it makes the element's measured size stale. WPF spells this
    // FrameworkPropertyMetadataOptions.AffectsMeasure, and it is the only
    // thing the store has to tell layout.
    bool affects_measure = false;

    // PropertyMetadata's PropertyChangedCallback. Empty for every property
    // registered by this repository's own types -- they override
    // OnPropertyChanged instead, which is what the runtime's built-in
    // properties do too. It is here for properties registered *through the
    // ABI*, by a caller that has no C++ type to override anything on.
    PropertyChangedCallback changed;
};

// The identity of a property. Registered once and referred to by address, the
// way DependencyProperty.Register hands back a singleton -- two properties
// with the same name on different owners are different properties, and the
// name alone never identifies one.
class DependencyProperty {
public:
    const std::string& owner() const { return owner_; }
    const std::string& name() const { return name_; }
    const PropertyValue& default_value() const { return metadata_.default_value; }
    bool inherits() const { return metadata_.inherits; }
    bool affects_measure() const { return metadata_.affects_measure; }
    const PropertyMetadata& metadata() const { return metadata_; }

    // Whether it was registered with RegisterAttached rather than Register.
    //
    // The store does not care -- an attached value is an ordinary entry under
    // this property's index, on whatever object it was written on. What the
    // flag decides is *resolution*: an attached property is found by its
    // qualified `Owner.Name` from anywhere, and a plain one only through the
    // owner chain of the object it is being read on.
    bool is_attached() const { return attached_; }

    // Dense index, assigned at registration, which is what an object's store
    // is keyed by. Comparing names on every read would work and would be the
    // slow way to do it.
    size_t index() const { return index_; }

    DependencyProperty(const DependencyProperty&) = delete;
    DependencyProperty& operator=(const DependencyProperty&) = delete;

    DependencyProperty(std::string owner, std::string name, PropertyMetadata metadata,
                       size_t index, bool attached = false)
        : owner_(std::move(owner)),
          name_(std::move(name)),
          metadata_(std::move(metadata)),
          index_(index),
          attached_(attached) {}

private:
    std::string owner_;
    std::string name_;
    PropertyMetadata metadata_;
    size_t index_ = 0;
    bool attached_ = false;
};

// Registers a property against an owner and returns the singleton that
// identifies it -- never null. Registering the same owner and name twice is a
// mistake in the implementation rather than a second property, and throws.
//
// A pointer rather than a reference because that is what the registrations
// hold: a namespace-scope reference bound to a function result is what a
// dangling reference looks like, whether or not it is one.
const DependencyProperty* RegisterProperty(std::string owner, std::string name,
                                           PropertyMetadata metadata);

// DependencyProperty.RegisterAttached. `name` is the bare one -- "Row", not
// "Grid.Row" -- because that is what the caller passes and what the runtime
// takes; the qualified name is this function's business, and it is what the
// property is filed and resolved under, so an attached property is written the
// way markup writes it.
const DependencyProperty* RegisterAttachedProperty(std::string owner, std::string name,
                                                   PropertyMetadata metadata);

// The property with this owner and this qualified-or-bare name, or nullptr.
// The registry's own lookup, without an owner chain: what the ABI's
// DependencyProperty statics need, since a caller naming an owner has already
// said which type it means.
const DependencyProperty* FindPropertyOnOwner(const std::string& owner, const std::string& name);

// The property a markup attribute names, or nullptr.
//
// `owners` is the object's owner chain, most derived first. A plain name is
// resolved against it, which is what makes `FontSize` a property of a Control
// and not of a StackPanel. A dotted name is an attached property and names its
// own owner, so it resolves globally and can be written on anything -- exactly
// the rule the XAML parser applies.
const DependencyProperty* FindProperty(const std::vector<std::string>& owners,
                                       const std::string& name);

// Every property registered with inherits = true. The inheritance walk needs
// the list rather than a per-object one, because re-parenting has to consider
// properties the object itself has never been told about.
const std::vector<const DependencyProperty*>& InheritedProperties();

// The property a store index belongs to, or nullptr. A store is keyed by index
// rather than by pointer, so anything that walks a store instead of a property
// list -- clearing every style value, for one -- needs the way back.
const DependencyProperty* PropertyByIndex(size_t index);

// Anything that carries dependency properties. Element derives from it; so
// would a Style or a resource dictionary entry, which is the reason it is not
// folded into Element.
class DependencyObject {
public:
    using PropertyChangedHandler =
        std::function<void(DependencyObject&, const DependencyProperty&, const PropertyValue&)>;
    using PropertyChangedToken = size_t;

    virtual ~DependencyObject() = default;

    // The property owners this object answers to, most derived first --
    // {"Border", "FrameworkElement", "UIElement"}. It is the type chain as far
    // as the property system is concerned, and nothing else here needs one.
    virtual const std::vector<std::string>& PropertyOwners() const = 0;

    // The effective value: local if there is one, then the style's, then
    // inherited from the ancestor chain if the property inherits, then the
    // registered default.
    const PropertyValue& GetValue(const DependencyProperty& property) const;

    void SetValue(const DependencyProperty& property, PropertyValue value);

    // Removes the local value, exposing whatever was underneath it. Not the
    // same as setting the default: an inherited property goes back to reading
    // its ancestor, and one under a style goes back to reading the style.
    void ClearValue(const DependencyProperty& property);

    bool HasLocalValue(const DependencyProperty& property) const;

    // DependencyObject.ReadLocalValue: the local value only, ignoring every
    // other source. nullptr is DependencyProperty.UnsetValue -- the sentinel
    // the runtime returns for "no local value here", which is not the same
    // answer as the default and not the same answer as the style's.
    const PropertyValue* ReadLocalValue(const DependencyProperty& property) const;

    // What a style setter writes. Its own slot rather than the local one, so
    // that a local value set afterwards still wins, and so that replacing the
    // style exposes what was underneath instead of what the old style left
    // behind. See style.h for who calls this.
    void SetStyleValue(const DependencyProperty& property, PropertyValue value);

    // Every style value at once, which is what changing an element's Style
    // does before the new one is applied. Not a loop over ClearStyleValue for
    // each property the caller happens to know about: the old style's setters
    // are exactly what has to go, and the store is the only thing that still
    // knows what they were.
    void ClearStyleValues();

    bool HasStyleValue(const DependencyProperty& property) const;

    // What the framework's own default style for this type writes -- the
    // `BaseValueSourceBuiltInStyle` layer. Below the style slot, so an
    // application's implicit or explicit Style overrides it property by
    // property and leaves the rest of it standing. Only a Control ever has
    // one: `docs/design-notes/styles.md` says CControl is the only caller,
    // and giving a Border one would invent a layer the runtime has not got.
    void SetBuiltInStyleValue(const DependencyProperty& property, PropertyValue value);
    void ClearBuiltInStyleValues();
    bool HasBuiltInStyleValue(const DependencyProperty& property) const;

    // Active animations sit above local values in the WinUI precedence
    // chain. VisualState setters use the same slot: leaving a state removes
    // its value and reveals the local/style/inherited value underneath.
    void SetAnimatedValue(const DependencyProperty& property, PropertyValue value);
    void ClearAnimatedValue(const DependencyProperty& property);
    void ClearAnimatedValues();
    bool HasAnimatedValue(const DependencyProperty& property) const;

    // Binding expressions and the ABI event layer observe effective changes,
    // not writes. A write shadowed by an animation therefore raises nothing;
    // clearing that animation raises the newly exposed value once.
    PropertyChangedToken AddPropertyChangedHandler(PropertyChangedHandler handler);
    void RemovePropertyChangedHandler(PropertyChangedToken token);

    // DependencyObject.RegisterPropertyChangedCallback: the same observation,
    // narrowed to one property. Kept apart from the handler list above rather
    // than filtered out of it because the runtime keeps them apart -- one
    // event source per property, created on first registration and destroyed
    // with its last handler (dxaml `DependencyObject::m_pNotificationVector`).
    //
    // The order the three observers run in is the runtime's, see the note on
    // ValueMoved in property.cpp.
    PropertyChangedToken RegisterPropertyChangedCallback(const DependencyProperty& property,
                                                         PropertyChangedHandler handler);
    void UnregisterPropertyChangedCallback(const DependencyProperty& property,
                                           PropertyChangedToken token);

    // Typed reads. They throw if the property does not hold that type, which
    // is a registration mistake rather than anything a case can cause.
    double GetDouble(const DependencyProperty& property) const;
    int GetInt(const DependencyProperty& property) const;
    bool GetBool(const DependencyProperty& property) const;
    const Thickness& GetThickness(const DependencyProperty& property) const;
    const std::string& GetString(const DependencyProperty& property) const;

    // The object inherited properties read through. Set when a child is
    // attached to a parent; every property that inherits is re-evaluated
    // across the subtree, so an element that changes hands does not keep
    // reading values from where it used to be.
    void SetInheritanceParent(DependencyObject* parent);
    DependencyObject* inheritance_parent() const { return inheritance_parent_; }

protected:
    // Called after an effective value moved, whatever moved it -- a local set,
    // a clear, or an ancestor changing under it.
    virtual void OnPropertyChanged(const DependencyProperty& property);

    // The objects an inherited value flows on to. Kept separate from the
    // layout children so the walk does not depend on anything below the
    // property system.
    virtual std::vector<DependencyObject*> InheritanceChildren() const { return {}; }

private:
    // Notifies this object and its subtree that an inherited property may have
    // moved, stopping wherever a local value shadows it.
    void InvalidateInherited(const DependencyProperty& property, const PropertyValue& before);
    void InvalidateAllInherited(const std::vector<PropertyValue>& before);

    // What this object says about a property on its own account -- the local
    // value, then the style's -- or nullptr when it says nothing. This is the
    // value an inheriting descendant reads, and the condition that stops the
    // inheritance walk, so the two cannot drift apart.
    const PropertyValue* OwnValue(const DependencyProperty& property) const;

    // Reports an effective value that may have moved: notifies this object and
    // pushes the change down the inheritance chain. One place, so that every
    // source writes into its slot and then says the same thing.
    void ValueMoved(const DependencyProperty& property, const PropertyValue& before);

    // Sparse on purpose: an element that sets two attributes stores two
    // values, not one slot per registered property.
    std::map<size_t, PropertyValue> local_;
    // The same, for what the element's Style supplies. A second map rather
    // than a tagged one: the two are read in a fixed order and never merged,
    // and keeping them apart is what makes clearing one of them possible.
    std::map<size_t, PropertyValue> style_;
    // And again for the framework's own style, for the same reason: the three
    // are read in a fixed order and never merged, so replacing an element's
    // Style cannot disturb what generic.xaml put underneath it.
    std::map<size_t, PropertyValue> built_in_style_;
    std::map<size_t, PropertyValue> animated_;
    std::map<PropertyChangedToken, PropertyChangedHandler> property_changed_handlers_;
    // Per-property observers, keyed by the property's store index. The inner
    // map is the runtime's per-property event source; an empty one is erased,
    // so `count()` on this map answers "does this property have observers"
    // without a second flag.
    std::map<size_t, std::map<PropertyChangedToken, PropertyChangedHandler>>
        per_property_handlers_;
    PropertyChangedToken next_property_changed_token_ = 1;
    DependencyObject* inheritance_parent_ = nullptr;
};

}  // namespace openxaml

#endif  // OPENXAML_PROPERTY_H
