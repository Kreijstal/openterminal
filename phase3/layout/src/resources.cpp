#include "resources.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "json.h"
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
        case ValueKind::Color: return "a Color";
        case ValueKind::Brush: return "a Brush";
        case ValueKind::CornerRadius: return "a CornerRadius";
        case ValueKind::Duration: return "a Duration";
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
//
// Exact, in both directions. The obvious exception -- an Int32 widening into a
// Double, since every value it can hold fits -- is not one the runtime makes:
// L5-xprimitives-int32-width writes <x:Int32>60</x:Int32> into Width and is
// recorded as a load failure (0x802b000a). An Int32 into an Int32 property is
// fine and measured, by L5-resources-int32-attached.
bool Assignable(ValueKind have, ValueKind want) {
    if (want == ValueKind::Unknown) return true;
    return have == want;
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
        // The brush properties, which only became worth declaring once the
        // application dictionary arrived: it is full of colours *and* of
        // brushes made from them, and the two are spelled the same way.
        {"Background", ValueKind::Brush},
        {"Foreground", ValueKind::Brush},
        {"BorderBrush", ValueKind::Brush},
        {"Spacing", ValueKind::Number},
        {"Opacity", ValueKind::Number},
        {"Canvas.Left", ValueKind::Number},
        {"Canvas.Top", ValueKind::Number},
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

    // {ThemeResource} and {StaticResource} take the same argument and search the
    // same chain. They differ in *when*: a ThemeResource is re-evaluated when
    // the application's theme changes, a StaticResource is resolved once and
    // frozen. Nothing here ever changes theme -- a case declares one and is
    // measured under it -- so the two are the same operation, and resolving
    // them by one path keeps a {ThemeResource} case comparable to its inlined
    // twin for the same reason a {StaticResource} one is.
    if (extension.name != "StaticResource" && extension.name != "ThemeResource") {
        throw MarkupError("the markup extension '{" + extension.name + "}' is not implemented, on '" +
                          property + "'");
    }
    const std::string spelled = "'{" + extension.name + "}'";
    if (extension.argument.empty())
        throw MarkupError(spelled + " on '" + property + "' names no resource key");
    if (extension.argument.find(',') != std::string::npos) {
        throw MarkupError(spelled + " takes one argument, got \"" + extension.argument + "\" on '" +
                          property + "'");
    }

    std::string key = extension.argument;
    const size_t equals = key.find('=');
    if (equals != std::string::npos) {
        const std::string name = Trim(key.substr(0, equals));
        if (name != "ResourceKey") {
            throw MarkupError(spelled + " has no argument named '" + name + "', on '" + property +
                              "'");
        }
        key = Trim(key.substr(equals + 1));
    }
    if (key.empty())
        throw MarkupError(spelled + " on '" + property + "' names no resource key");
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

// --- the application dictionary -----------------------------------------------

namespace {

// The types the extracted database uses, mapped to the shapes this parser
// checks against. A type outside this set is a key the database carries and
// this implementation cannot use -- a Style, a ControlTemplate, a brush whose
// colour never resolved -- and it is *dropped at load* rather than stored as an
// unusable entry, so that a lookup for it fails with "not found" like any other
// key we cannot supply, instead of resolving to text no property can read.
const std::map<std::string, ValueKind>& ThemeResourceTypes() {
    static const std::map<std::string, ValueKind> kTypes = {
        {"x:Double", ValueKind::Number},
        {"x:Int32", ValueKind::Integer},
        {"x:String", ValueKind::String},
        {"x:Boolean", ValueKind::Boolean},
        {"Thickness", ValueKind::Thickness},
        {"GridLength", ValueKind::GridLength},
        {"CornerRadius", ValueKind::CornerRadius},
        {"Color", ValueKind::Color},
        {"Duration", ValueKind::Duration},
        {"FontFamily", ValueKind::String},
    };
    return kTypes;
}

const JsonValue* Member(const JsonValue& object, const std::string& name) {
    const auto found = object.object.find(name);
    return found == object.object.end() ? nullptr : &found->second;
}

const JsonValue& Text(const JsonValue& object, const std::string& name, const std::string& where) {
    const JsonValue* value = Member(object, name);
    if (!value || value->kind != JsonValue::Kind::String)
        throw JsonError(where + ": \"" + name + "\" is not a string");
    return *value;
}

// One database entry as this implementation can use it, or nothing.
//
// An alias is followed by the *extractor*, which records where the chain landed
// alongside the chain itself; this reads the landing. Following it here as well
// would be a second implementation of the same walk, free to disagree with the
// first.
bool ReadEntry(const JsonValue& entry, const std::string& key, ResourceValue& out) {
    const JsonValue* status = Member(entry, "status");
    if (!status || status->kind != JsonValue::Kind::String)
        throw JsonError("the entry for '" + key + "' has no status");

    const JsonValue* resolved = status->string == "alias" ? Member(entry, "resolved") : &entry;
    if (!resolved || resolved->kind != JsonValue::Kind::Object) return false;

    const JsonValue* kind = Member(*resolved, "status");
    if (!kind || kind->kind != JsonValue::Kind::String) return false;

    if (kind->string == "brush") {
        // A brush is usable here for one reason only: the attribute form of a
        // brush is a colour, so a brush whose colour the extractor resolved
        // hands the property exactly the text `Background="#FF1F1F1F"` would.
        // A brush whose colour it could not resolve -- one built on the OS's
        // SystemAccentColor, say -- has no such text and is not loaded.
        const JsonValue* colour = Member(*resolved, "color");
        if (!colour || colour->kind != JsonValue::Kind::String) return false;
        // No style object: the application dictionary carries no Style this
        // parser can use, and one that named a Style is dropped at load above.
        out = ResourceValue{"SolidColorBrush", colour->string, ValueKind::Brush, nullptr};
        return true;
    }
    if (kind->string != "value") return false;

    const JsonValue* type = Member(*resolved, "type");
    const JsonValue* text = Member(*resolved, "text");
    if (!type || type->kind != JsonValue::Kind::String) return false;
    if (!text || text->kind != JsonValue::Kind::String) return false;
    const auto known = ThemeResourceTypes().find(type->string);
    if (known == ThemeResourceTypes().end()) return false;
    out = ResourceValue{type->string, text->string, known->second, nullptr};
    return true;
}

void ReadDatabase(const std::string& json, const std::string& where, ThemeResourceLibrary& library) {
    const JsonValue document = ParseJson(json);
    const JsonValue* themes = Member(document, "themes");
    if (!themes || themes->kind != JsonValue::Kind::Object)
        throw JsonError(where + ": no \"themes\" object");

    // Recorded so a database that silently came from somewhere else is visible
    // in the one place a reader looks.
    Text(document.At("source"), "commit", where);

    const JsonValue* shared = Member(*themes, "Shared");
    for (const auto& [name, entries] : themes->object) {
        if (name == "Shared") continue;
        if (entries.kind != JsonValue::Kind::Object)
            throw JsonError(where + ": theme \"" + name + "\" is not an object");

        ResourceDictionary dictionary;
        ResourceValue value;
        // Theme-independent entries first, so that a theme's own entry for the
        // same key would be the one that wins. The merge upstream already makes
        // that impossible -- a key in a theme dictionary is removed from the
        // root one -- so this is belt and braces, not a rule being relied on.
        if (shared && shared->kind == JsonValue::Kind::Object) {
            for (const auto& [key, entry] : shared->object) {
                if (ReadEntry(entry, key, value)) dictionary.Add(key, value);
            }
        }
        for (const auto& [key, entry] : entries.object) {
            if (ReadEntry(entry, key, value)) dictionary.Add(key, value);
        }
        library.Add(name, std::move(dictionary));
    }
}

}  // namespace

ThemeResourceLibrary& ThemeResourceLibrary::Default() {
    static ThemeResourceLibrary library;
    return library;
}

void ThemeResourceLibrary::Add(const std::string& theme, ResourceDictionary dictionary) {
    if (themes_.count(theme))
        throw JsonError("the theme '" + theme + "' is already loaded; two dictionaries claiming "
                        "one theme have no defensible resolution");
    themes_.emplace(theme, std::move(dictionary));
}

void ThemeResourceLibrary::SetActiveTheme(const std::string& theme) {
    if (!themes_.empty() && !themes_.count(theme)) {
        std::string loaded;
        for (const auto& entry : themes_) loaded += (loaded.empty() ? "" : ", ") + entry.first;
        throw MarkupError("no theme dictionary for '" + theme + "'; loaded: " + loaded);
    }
    active_ = theme;
}

const ResourceDictionary* ThemeResourceLibrary::Active() const {
    const auto found = themes_.find(active_);
    return found == themes_.end() ? nullptr : &found->second;
}

std::vector<std::string> ThemeResourceLibrary::themes() const {
    std::vector<std::string> names;
    for (const auto& entry : themes_) names.push_back(entry.first);
    return names;
}

int LoadThemeResources(ThemeResourceLibrary& library, const std::string& path) {
    namespace fs = std::filesystem;
    std::error_code failure;

    std::vector<fs::path> files;
    if (fs::is_directory(path, failure)) {
        for (const auto& entry : fs::directory_iterator(path)) {
            if (entry.path().extension() == ".json") files.push_back(entry.path());
        }
        std::sort(files.begin(), files.end());
    } else if (fs::is_regular_file(path, failure)) {
        files.push_back(path);
    } else {
        return 0;
    }

    for (const fs::path& file : files) {
        std::ifstream in(file, std::ios::binary);
        std::ostringstream buffer;
        buffer << in.rdbuf();
        ReadDatabase(buffer.str(), file.filename().string(), library);
    }

    const ResourceDictionary* active = library.Active();
    return static_cast<int>(active ? active->size() : 0);
}

}  // namespace openxaml
