#include "style.h"

#include <algorithm>

#include "markup.h"

namespace openxaml {
namespace {

std::string Trim(const std::string& text) {
    const size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

bool IsBlank(const std::string& text) {
    return text.find_first_not_of(" \t\r\n") == std::string::npos;
}

// How a message names the style it is complaining about. An implicit style has
// no key, so it is named by what it targets instead of by nothing.
std::string Describe(const std::string& key, const std::string& target_type) {
    if (!key.empty()) return "the Style '" + key + "'";
    if (!target_type.empty()) return "the implicit Style for '" + target_type + "'";
    return "a Style";
}

}  // namespace

const StyleSetter* Style::Find(const DependencyProperty& property) const {
    for (const StyleSetter& setter : setters) {
        if (setter.property == &property) return &setter;
    }
    return nullptr;
}

void ImplicitStyleTable::Add(const std::string& target_type, std::shared_ptr<const Style> style) {
    if (entries_.count(target_type)) {
        throw MarkupError("two implicit Styles for '" + target_type +
                          "' are declared in one dictionary");
    }
    entries_.emplace(target_type, std::move(style));
}

std::shared_ptr<const Style> ImplicitStyleTable::Find(const std::string& target_type) const {
    const auto found = entries_.find(target_type);
    return found == entries_.end() ? nullptr : found->second;
}

std::shared_ptr<const Style> FindImplicitStyle(const ImplicitStyleScope& scope,
                                               const std::string& type) {
    for (const ImplicitStyleTable* table : scope) {
        // Innermost first, and the first hit wins outright: a dictionary
        // nearer the element shadows one further away, exactly as it does for
        // a keyed resource. Nothing merges the two.
        if (std::shared_ptr<const Style> found = table->Find(type)) return found;
    }
    return nullptr;
}

// --- building -----------------------------------------------------------------

namespace {

// Adds `setter` to `into`, overriding an entry for the same property.
//
// Identity, not name: BasedOn from a Border style to a Border style overrides
// Border's Padding, and would not override the panels' Padding even though the
// two are spelled the same.
//
// Later wins, which covers both cases the core has. Across a BasedOn, the
// derived style's setter replaces the base's -- `CStyle::CreateMergedBasedOn-
// SetterCollection` looks each base setter up in the derived style's own and
// substitutes it. Within one style, a property set twice takes the last:
// `CStyle::GetPropertyValue` searches its setters in reverse and says so --
// "if a property is duplicated, the last setter wins". Neither is an error in
// the runtime, and neither is one here.
void Merge(std::vector<StyleSetter>& into, StyleSetter setter) {
    for (StyleSetter& existing : into) {
        if (existing.property == setter.property) {
            existing = std::move(setter);
            return;
        }
    }
    into.push_back(std::move(setter));
}

}  // namespace

std::shared_ptr<const Style> ParseStyle(const std::map<std::string, std::string>& attributes,
                                        bool self_closing, StyleTagSource& tags, StyleHost& host,
                                        const std::string& key) {
    auto style = std::make_shared<Style>();
    style->key = key;

    std::string based_on;
    for (const auto& [name, value] : attributes) {
        if (name == "TargetType") {
            style->target_type = Trim(value);
        } else if (name == "BasedOn") {
            based_on = value;
        } else {
            throw MarkupError("<Style> does not take '" + name + "'");
        }
    }

    // Required, and required first: everything else a style says is resolved
    // against it, so a style without one has nothing to check its setters
    // against and would accept any name at all. The runtime's own wording, as
    // far as a message without a resource table can carry it.
    if (style->target_type.empty())
        throw MarkupError(Describe(key, "") + " must have a non-null value for TargetType");
    host.ValidateTargetType(style->target_type);

    if (!based_on.empty()) {
        const Style& base = *host.LookUpBasedOn(based_on);
        // The base style must target the same type or a base of it. Both
        // references say so and both refuse the pairing rather than silently
        // applying setters that could never resolve -- `CStyle::Validate-
        // BasedOnTargetType` in the core, `Style.CheckTargetType` in WPF.
        // Every TargetType here is a concrete implemented type, so in practice
        // this admits only an exact match; the rule is the reference's rather
        // than that coincidence.
        //
        // A cycle is not reachable. A style enters its dictionary only once it
        // is finished, so a BasedOn can only name something already complete
        // and a self-reference fails as an unresolved key. The runtime needs
        // its loop check because a Style there is a mutable object that can be
        // pointed at anything before it is sealed.
        const std::vector<std::string>& owners = host.OwnerChain(style->target_type);
        if (base.target_type != style->target_type &&
            std::find(owners.begin(), owners.end(), base.target_type) == owners.end()) {
            throw MarkupError("can only base on a Style whose target type is a base type of '" +
                              style->target_type + "'; " + Describe(base.key, base.target_type) +
                              " targets '" + base.target_type + "'");
        }
        // Flattened here, once. The base is already flat by the same rule, so
        // no walk of the chain survives past construction and a style outlives
        // nothing it depends on.
        style->setters = base.setters;
    }

    if (self_closing) return style;

    bool wrapper_seen = false;
    bool wrapper_open = false;
    bool setter_open = false;
    std::string property;
    std::string value;
    bool value_given = false;
    bool value_element_open = false;

    StyleTag tag;
    while (tags.Next(tag)) {
        if (tag.closing) {
            if (value_element_open) {
                if (tag.name != "Setter.Value")
                    throw MarkupError("</" + tag.name + "> closes <Setter.Value>");
                if (!value_given) {
                    // The text form: <Setter.Value>30</Setter.Value>. Trimmed,
                    // as an indented dictionary's whitespace is everywhere
                    // else in this parser.
                    value = Trim(tag.text_before);
                    if (value.empty())
                        throw MarkupError("<Setter.Value> for '" + property + "' is empty");
                    value_given = true;
                } else if (!IsBlank(tag.text_before)) {
                    throw MarkupError("unexpected text content in <Setter.Value>");
                }
                value_element_open = false;
                continue;
            }
            if (setter_open) {
                if (tag.name != "Setter")
                    throw MarkupError("</" + tag.name + "> closes <Setter>");
                if (!value_given)
                    throw MarkupError("the Setter for '" + property + "' was given no Value");
                Merge(style->setters, host.ParseSetter(style->target_type, property, value));
                setter_open = false;
                continue;
            }
            if (wrapper_open) {
                if (tag.name != "Style.Setters")
                    throw MarkupError("</" + tag.name + "> closes <Style.Setters>");
                wrapper_open = false;
                continue;
            }
            if (tag.name != "Style") throw MarkupError("</" + tag.name + "> closes <Style>");
            return style;
        }

        if (!IsBlank(tag.text_before))
            throw MarkupError("unexpected text content in " + Describe(key, style->target_type));

        if (value_element_open) {
            // The only element a value can be is a lookup. A Setter whose value
            // is an object -- a Brush, a nested Style -- is not implemented,
            // and says so rather than being dropped.
            if (tag.name != "StaticResource") {
                throw MarkupError("<Setter.Value> for '" + property +
                                  "' takes a <StaticResource>, not <" + tag.name + ">");
            }
            if (value_given)
                throw MarkupError("<Setter.Value> for '" + property + "' was given a second value");
            if (!tag.self_closing) {
                StyleTag close;
                if (!tags.Next(close) || !close.closing || close.name != "StaticResource")
                    throw MarkupError("<StaticResource> must be empty");
            }
            value = host.ResolveResourceElement(tag.attributes, property);
            value_given = true;
            continue;
        }

        if (setter_open) {
            if (tag.name != "Setter.Value") {
                throw MarkupError("a <Setter> takes a <Setter.Value>, not <" + tag.name + ">");
            }
            if (value_given) {
                throw MarkupError("the Setter for '" + property +
                                  "' has a Value attribute and a <Setter.Value> as well");
            }
            if (!tag.attributes.empty())
                throw MarkupError("<Setter.Value> cannot carry attributes");
            if (tag.self_closing)
                throw MarkupError("<Setter.Value> for '" + property + "' is empty");
            value_element_open = true;
            continue;
        }

        // The optional wrapper around the setter collection. XAML lets Setters
        // be written out or left implicit; it carries nothing either way.
        if (tag.name == "Style.Setters") {
            if (wrapper_seen)
                throw MarkupError("<Style.Setters> is written twice in one <Style>");
            wrapper_seen = true;
            if (!tag.self_closing) wrapper_open = true;
            continue;
        }

        if (tag.name != "Setter") {
            throw MarkupError(Describe(key, style->target_type) + " holds <Setter> elements, not <" +
                              tag.name + ">");
        }

        property.clear();
        value.clear();
        value_given = false;
        for (const auto& [name, raw] : tag.attributes) {
            if (name == "Property") {
                property = Trim(raw);
            } else if (name == "Value") {
                value = raw;
                value_given = true;
            } else {
                throw MarkupError("a <Setter> does not take '" + name + "'");
            }
        }
        if (property.empty()) throw MarkupError("a <Setter> has no Property");
        // "Can't style the Style property" -- CSetter::SetValue refuses it
        // outright, before anything asks what it would mean. A style that
        // installed another style is a recursion the core does not have.
        if (property == "Style")
            throw MarkupError("a <Setter> cannot set 'Style'");

        if (tag.self_closing) {
            if (!value_given)
                throw MarkupError("the Setter for '" + property + "' was given no Value");
            Merge(style->setters, host.ParseSetter(style->target_type, property, value));
            continue;
        }
        setter_open = true;
    }

    throw MarkupError(Describe(key, style->target_type) + " was not closed");
}

// --- applying -----------------------------------------------------------------

void ApplyStyle(DependencyObject& target, const Style& style, const std::string& type,
                const std::vector<std::string>& owners) {
    // An is-a check, not an exact one: `CFrameworkElement::ValidateTargetType`
    // asks `OfTypeByIndex`, and WPF's `Style.CheckTargetType` asks
    // `IsAssignableFrom`. So a Style with TargetType="Control" may be assigned
    // to a Button explicitly -- while the *implicit* lookup is an exact match
    // on the concrete type and would never have found it. The two rules differ
    // on purpose and this is the lax one.
    if (style.target_type != type &&
        std::find(owners.begin(), owners.end(), style.target_type) == owners.end()) {
        throw MarkupError("cannot apply a Style with TargetType '" + style.target_type +
                          "' to an object of type '" + type + "'");
    }
    for (const StyleSetter& setter : style.setters)
        target.SetStyleValue(*setter.property, setter.value);
}

void ApplyBuiltInStyle(DependencyObject& target, const Style& style, const std::string& type,
                       const std::vector<std::string>& owners) {
    // The same target-type check, because the same thing can go wrong: a
    // built-in style table keyed by the wrong name would write a Button's
    // setters onto a TextBox and produce numbers with nothing to explain them.
    // The lookup that reaches here is an exact match on the concrete type, so
    // in practice this only ever fires for a malformed table -- which is
    // exactly when it is worth having.
    if (style.target_type != type &&
        std::find(owners.begin(), owners.end(), style.target_type) == owners.end()) {
        throw MarkupError("cannot apply a built-in Style with TargetType '" + style.target_type +
                          "' to an object of type '" + type + "'");
    }
    for (const StyleSetter& setter : style.setters)
        target.SetBuiltInStyleValue(*setter.property, setter.value);
}

}  // namespace openxaml
