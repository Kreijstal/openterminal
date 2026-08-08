// Style, Setter, BasedOn, and the two ways a style reaches an element.
//
// A style is a named bundle of property assignments. It is the first thing in
// this corpus that writes a dependency property from somewhere other than the
// markup that names the element, and that is the whole point of it: the value
// lands in a slot of its own, below a local value and above an inherited one,
// so that <Border Width="10"/> under a style that says Width="60" is still ten
// wide and a Border that says nothing is sixty.
//
// Two routes in, and they are not two mechanisms:
//
//   * explicit -- <Style x:Key="Boxy" TargetType="Border">, referenced by
//     Style="{StaticResource Boxy}". An ordinary resource lookup that happens
//     to land on a Style.
//   * implicit -- the same <Style TargetType="Border"> with no x:Key, which
//     applies to every Border in scope that has no Style of its own. Keyed by
//     the target type rather than by a string.
//
// Both produce one Style object and one application, so nothing downstream can
// tell them apart -- which is the reference behaviour, where both feed the
// single `Style` value source and an element has at most one style at a time.
//
// Setters are resolved and parsed when the style is built, not when it is
// applied. A setter for a property the target type does not have is a load
// failure at the dictionary, exactly as a bad attribute is at the element, and
// it says the same thing in the same words -- see markup.cpp's host, which
// reaches the ordinary attribute parser to do it. The alternative, carrying the
// literal to application time, would let a dictionary hold a setter that can
// never fire and report nothing until something used it.
//
// Ported in shape from dotnet/wpf's Style/StyleHelper (MIT,
// 2ca037562c207924e53cfcc99286e523d3694de3) and checked against
// microsoft/microsoft-ui-xaml's CStyle (MIT, 188f602b). Everything the two
// disagree about, or that neither states plainly, is an L5-styles case carrying
// its question rather than a decision made here quietly.

#ifndef OPENXAML_STYLE_H
#define OPENXAML_STYLE_H

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "property.h"

namespace openxaml {

// One resolved assignment. The property is already looked up against the
// target type and the value already parsed, so applying a style is a walk and
// a store write with nothing left that can fail.
struct StyleSetter {
    const DependencyProperty* property = nullptr;
    PropertyValue value;
};

struct Style {
    // The short markup name of the type this style may be applied to.
    std::string target_type;

    // Every setter that applies, base first. The BasedOn chain is flattened
    // when the style is built, so nothing downstream walks it and no style
    // holds a reference to the dictionary its base lives in.
    std::vector<StyleSetter> setters;

    // The x:Key it was declared under, or empty for an implicit style. Only
    // ever used to say which style a message is about.
    std::string key;

    // The setter for `property`, or null. Identity, not name: two properties
    // called Padding on different owners are different properties, and a
    // merge that matched on the name would have one override the other with
    // nothing measurable ever noticing.
    const StyleSetter* Find(const DependencyProperty& property) const;
};

// The implicit styles one element's dictionary declares: target type -> style.
//
// Kept apart from the ResourceDictionary rather than filed in it under the
// type name, because an implicit style's key is a *type*, not a string. Filing
// it as "Border" would invent a collision with <x:Double x:Key="Border"/> that
// the runtime does not have, and inventing a failure is as wrong as missing
// one.
class ImplicitStyleTable {
public:
    void Add(const std::string& target_type, std::shared_ptr<const Style> style);
    std::shared_ptr<const Style> Find(const std::string& target_type) const;
    bool empty() const { return entries_.empty(); }
    size_t size() const { return entries_.size(); }

private:
    std::map<std::string, std::shared_ptr<const Style>> entries_;
};

// The tables an implicit lookup walks, innermost first -- the element's own
// dictionary, then each ancestor's. The same shape and the same order as
// ResourceScope, and deliberately so: an implicit style is found by a resource
// lookup in the reference implementation, and a scope that differed from
// {StaticResource}'s would be a second set of rules to be wrong in.
using ImplicitStyleScope = std::vector<const ImplicitStyleTable*>;

// The implicit style for `type`, or null when no dictionary in scope has one.
//
// Exact type match. WPF looks the implicit key up by the element's own type and
// stops -- a Style with TargetType="Control" does not reach a Button through
// the implicit route, only through an explicit Style= -- and this follows it.
// Whether WinUI 2 agrees is `L5-styles-implicit-derived-type`, which is a
// question rather than an assertion.
std::shared_ptr<const Style> FindImplicitStyle(const ImplicitStyleScope& scope,
                                               const std::string& type);

// --- building a style ---------------------------------------------------------

// One tag out of the markup, reduced to what a style needs from it.
//
// A view rather than the scanner's own type, so that the style grammar can be
// written here instead of inside the XML scanner -- markup.cpp keeps the
// scanning, this keeps the meaning, and neither has to know how the other
// works.
struct StyleTag {
    std::string name;
    std::map<std::string, std::string> attributes;
    bool self_closing = false;
    bool closing = false;
    std::string text_before;
};

class StyleTagSource {
public:
    virtual ~StyleTagSource() = default;
    // False at end of input, which inside a <Style> is always an error.
    virtual bool Next(StyleTag& tag) = 0;
};

// What building a style needs from the markup layer: the type system, the
// attribute parser, and the resource scope. All three already exist there and
// none of them is a style's business.
class StyleHost {
public:
    virtual ~StyleHost() = default;

    // Throws MarkupError, by name, for a type this parser does not implement.
    virtual void ValidateTargetType(const std::string& type) = 0;

    // The owner chain of a type, most derived first. Used to decide whether a
    // style may be applied to an element and whether a BasedOn is compatible.
    virtual const std::vector<std::string>& OwnerChain(const std::string& type) = 0;

    // One setter, resolved against the target type and parsed. Throws
    // MarkupError naming the property when the type does not have it, and
    // naming the value when it cannot be read as that property's type.
    // `value` may still be a {StaticResource} or a {} escape.
    virtual StyleSetter ParseSetter(const std::string& target_type, const std::string& property,
                                    const std::string& value) = 0;

    // The style a BasedOn attribute names. `raw` is the attribute as written,
    // which is always a markup extension. Throws naming the key when the
    // lookup fails or lands on something that is not a Style.
    virtual std::shared_ptr<const Style> LookUpBasedOn(const std::string& raw) = 0;

    // The value a <Setter.Value><StaticResource ResourceKey="K"/></Setter.Value>
    // stands for, as the literal an attribute would have carried.
    virtual std::string ResolveResourceElement(
        const std::map<std::string, std::string>& attributes, const std::string& property) = 0;
};

// Reads a <Style> whose start tag has already been consumed.
//
// `attributes` are that tag's with x:Key removed; `key` is what was removed, or
// empty for an implicit style. `self_closing` says there is no body, which is a
// style with no setters -- legal, and inert.
std::shared_ptr<const Style> ParseStyle(const std::map<std::string, std::string>& attributes,
                                        bool self_closing, StyleTagSource& tags, StyleHost& host,
                                        const std::string& key);

// --- applying one -------------------------------------------------------------

// Writes every setter into `target`'s style slot.
//
// `type` is the element's markup type name and `owners` its owner chain. A
// style may be applied to its TargetType or to anything derived from it, which
// is WPF's rule for an explicit Style= (`Style.CheckTargetType`), and this is
// the one place it is enforced -- an implicit style cannot reach a mismatched
// element to begin with, so the check only ever fires for an explicit one.
void ApplyStyle(DependencyObject& target, const Style& style, const std::string& type,
                const std::vector<std::string>& owners);

// The same, into the built-in slot -- the `BaseValueSourceBuiltInStyle` layer
// that only a Control's own default style writes. Separate from ApplyStyle
// rather than a flag on it, because the two are separate layers that coexist:
// applying one must not disturb the other, and a caller that could pick the
// layer by argument would eventually pick the wrong one silently.
void ApplyBuiltInStyle(DependencyObject& target, const Style& style, const std::string& type,
                       const std::vector<std::string>& owners);

}  // namespace openxaml

#endif  // OPENXAML_STYLE_H
