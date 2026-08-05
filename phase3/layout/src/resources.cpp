#include "resources.h"

#include <cctype>

#include "markup.h"

namespace openxaml {
namespace {

std::string Trim(const std::string& text) {
    const size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::string KindName(ValueKind kind) {
    switch (kind) {
        case ValueKind::Number: return "a number";
        case ValueKind::Integer: return "an integer";
        case ValueKind::Thickness: return "a Thickness";
        case ValueKind::GridLength: return "a GridLength";
        case ValueKind::String: return "a string";
        case ValueKind::Boolean: return "a Boolean";
        case ValueKind::Style: return "a Style";
        case ValueKind::Unknown: break;
    }
    return "a value of no declared shape";
}

// The x-namespace primitives WinUI actually accepts as object elements, plus
// Thickness. Every one of these appears in Terminal's own markup; the list is
// what Terminal uses, not what XAML in principle allows, so that nothing here
// is implemented against a guess.
//
// The prefix is matched literally rather than resolved through the document's
// namespace declarations. Every case in the corpus binds `x` to the XAML
// language namespace, which is the only binding the runtime itself would
// accept for these types -- but a document that bound some other prefix to it
// would not be understood here. That is a real limitation of this parser and
// not a claim about XAML.
const std::map<std::string, ValueKind>& ResourceTypes() {
    static const std::map<std::string, ValueKind> kTypes = {
        {"x:Double", ValueKind::Number},
        {"x:Int32", ValueKind::Integer},
        {"x:String", ValueKind::String},
        {"x:Boolean", ValueKind::Boolean},
        {"Thickness", ValueKind::Thickness},
        {"GridLength", ValueKind::GridLength},
    };
    return kTypes;
}

// Whether a resource of `have` can be assigned to a property that wants `want`.
bool Assignable(ValueKind have, ValueKind want) {
    if (want == ValueKind::Unknown) return true;
    if (have == want) return true;
    // An Int32 widens to a Double, which is the one conversion XAML performs on
    // the way into a property. It does not go the other way: WinUI rejects a
    // Double handed to an Int32 property rather than truncating it.
    return have == ValueKind::Integer && want == ValueKind::Number;
}

}  // namespace

void ResourceDictionary::Add(const std::string& key, ResourceValue value) {
    if (key.empty()) throw MarkupError("a resource needs a non-empty x:Key");
    if (entries_.count(key))
        throw MarkupError("the resource key '" + key + "' is declared twice in one dictionary");
    entries_.emplace(key, std::move(value));
}

const ResourceValue* ResourceDictionary::Find(const std::string& key) const {
    const auto found = entries_.find(key);
    return found == entries_.end() ? nullptr : &found->second;
}

bool IsResourceType(const std::string& type) { return ResourceTypes().count(type) != 0; }

ResourceValue MakeResource(const std::string& type, const std::string& text) {
    const auto found = ResourceTypes().find(type);
    if (found == ResourceTypes().end())
        throw MarkupError("the resource type '" + type + "' is not implemented");
    // A primitive's content is its literal, and XAML trims the whitespace an
    // indented dictionary puts around it.
    return ResourceValue{type, Trim(text), found->second, nullptr};
}

ValueKind ExpectedValueKind(const std::string& property) {
    // The properties this parser knows the shape of. A property missing from
    // here still resolves; see the note on ValueKind::Unknown.
    static const std::map<std::string, ValueKind> kProperties = {
        {"Width", ValueKind::Number},
        {"Height", ValueKind::Number},
        {"MinWidth", ValueKind::Number},
        {"MaxWidth", ValueKind::Number},
        {"MinHeight", ValueKind::Number},
        {"MaxHeight", ValueKind::Number},
        {"FontSize", ValueKind::Number},
        // The one property whose value is an object rather than a literal.
        // Naming it here is what turns Width="{StaticResource BoxStyle}" and
        // Style="{StaticResource BoxWidth}" into two different named failures
        // instead of one silent empty string.
        {"Style", ValueKind::Style},
        // Style.BasedOn. Named here for the same reason, so that a BasedOn
        // pointing at an x:Double is refused in the words every other
        // wrong-shaped lookup is refused in.
        {"BasedOn", ValueKind::Style},
        {"Margin", ValueKind::Thickness},
        {"Padding", ValueKind::Thickness},
        {"BorderThickness", ValueKind::Thickness},
        {"Grid.Column", ValueKind::Integer},
        {"Grid.Row", ValueKind::Integer},
        {"Grid.ColumnSpan", ValueKind::Integer},
        {"Grid.RowSpan", ValueKind::Integer},
        {"Text", ValueKind::String},
        {"FontFamily", ValueKind::String},
        // Qualified, because the same name means something else here: a
        // definition sizes in GridLength, not in device-independent pixels.
        {"ColumnDefinition.Width", ValueKind::GridLength},
        {"RowDefinition.Height", ValueKind::GridLength},
        {"ColumnDefinition.MinWidth", ValueKind::Number},
        {"ColumnDefinition.MaxWidth", ValueKind::Number},
        {"RowDefinition.MinHeight", ValueKind::Number},
        {"RowDefinition.MaxHeight", ValueKind::Number},
    };
    const auto found = kProperties.find(property);
    return found == kProperties.end() ? ValueKind::Unknown : found->second;
}

const ResourceValue& LookUpResource(const ResourceScope& scope, const std::string& key,
                                    const std::string& where) {
    for (const ResourceDictionary* dictionary : scope) {
        if (const ResourceValue* value = dictionary->Find(key)) return *value;
    }
    throw MarkupError("resource '" + key + "' not found for " + where +
                      "; the lookup walked " + std::to_string(scope.size()) +
                      " dictionary(ies) from the element to the root of the markup");
}

std::string ValueForProperty(const ResourceValue& value, const std::string& property) {
    const ValueKind wanted = ExpectedValueKind(property);
    if (!Assignable(value.kind, wanted)) {
        throw MarkupError("<" + value.type + "> cannot supply '" + property +
                          "': that property takes " + KindName(wanted));
    }
    return value.text;
}

namespace {

// The entry `key` names, once it is known that `property` can take it. The one
// place the shape check lives, so that the three ways of writing a reference
// -- an attribute, a <StaticResource> element, a BasedOn -- all refuse a
// wrong-shaped resource in the same words.
const ResourceValue& CheckedResource(const ResourceScope& scope, const std::string& key,
                                     const std::string& property) {
    const std::string where = "'" + property + "'";
    const ResourceValue& value = LookUpResource(scope, key, where);
    const ValueKind wanted = ExpectedValueKind(property);
    if (!Assignable(value.kind, wanted)) {
        throw MarkupError("the resource '" + key + "' is " + value.type + ", which cannot supply '" +
                          property + "': that property takes " + KindName(wanted));
    }
    return value;
}

}  // namespace

std::string ResolveResource(const ResourceScope& scope, const std::string& key,
                            const std::string& property) {
    return CheckedResource(scope, key, property).text;
}

bool TryParseMarkupExtension(const std::string& text, MarkupExtension& extension) {
    const std::string trimmed = Trim(text);
    if (trimmed.size() < 2 || trimmed.front() != '{') return false;
    if (trimmed.compare(0, 2, "{}") == 0) return false;
    if (trimmed.back() != '}')
        throw MarkupError("the markup extension \"" + trimmed + "\" is not closed");

    const std::string body = Trim(trimmed.substr(1, trimmed.size() - 2));
    if (body.find('{') != std::string::npos)
        throw MarkupError("a nested markup extension in \"" + trimmed + "\" is not implemented");

    const size_t split = body.find_first_of(" \t\r\n");
    extension.name = split == std::string::npos ? body : body.substr(0, split);
    extension.argument = split == std::string::npos ? "" : Trim(body.substr(split));
    if (extension.name.empty())
        throw MarkupError("the markup extension \"" + trimmed + "\" has no name");
    return true;
}

const ResourceValue& ResolveResourceReference(const ResourceScope& scope,
                                              const std::string& property,
                                              const std::string& raw) {
    MarkupExtension extension;
    if (!TryParseMarkupExtension(raw, extension))
        throw MarkupError("'" + property + "' takes a {StaticResource}, got \"" + raw + "\"");

    if (extension.name != "StaticResource") {
        throw MarkupError("the markup extension '{" + extension.name + "}' is not implemented, on '" +
                          property + "'");
    }
    if (extension.argument.empty())
        throw MarkupError("'{StaticResource}' on '" + property + "' names no resource key");
    if (extension.argument.find(',') != std::string::npos) {
        throw MarkupError("'{StaticResource}' takes one argument, got \"" + extension.argument +
                          "\" on '" + property + "'");
    }

    std::string key = extension.argument;
    const size_t equals = key.find('=');
    if (equals != std::string::npos) {
        const std::string name = Trim(key.substr(0, equals));
        if (name != "ResourceKey") {
            throw MarkupError("'{StaticResource}' has no argument named '" + name + "', on '" +
                              property + "'");
        }
        key = Trim(key.substr(equals + 1));
    }
    if (key.empty())
        throw MarkupError("'{StaticResource}' on '" + property + "' names no resource key");
    return CheckedResource(scope, key, property);
}

std::string ResolveAttributeValue(const ResourceScope& scope, const std::string& property,
                                  const std::string& raw) {
    MarkupExtension extension;
    if (!TryParseMarkupExtension(raw, extension)) {
        // `{}` escapes a value that starts with a brace. Strip it here rather
        // than in the property parsers, which should never see the escape.
        const std::string trimmed = Trim(raw);
        if (trimmed.size() >= 2 && trimmed.compare(0, 2, "{}") == 0) return trimmed.substr(2);
        return raw;
    }
    return ResolveResourceReference(scope, property, raw).text;
}

std::string StaticResourceElementKey(const std::map<std::string, std::string>& attributes,
                                     bool allow_key) {
    std::string key;
    for (const auto& [name, value] : attributes) {
        if (name == "ResourceKey") {
            key = Trim(value);
        } else if (name == "x:Key" && allow_key) {
            continue;  // the caller consumes it; this is the alias form
        } else {
            throw MarkupError("<StaticResource> does not take '" + name + "'");
        }
    }
    if (key.empty()) throw MarkupError("<StaticResource> needs a ResourceKey");
    return key;
}

}  // namespace openxaml
