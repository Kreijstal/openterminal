#include "markup.h"

#include "markup_tree.h"

#include <cctype>
#include <cstdlib>
#include <map>
#include <vector>

#include "border.h"
#include "grid.h"
#include "resources.h"
#include "stack_panel.h"

namespace openxaml {
namespace {

// --- value parsing ------------------------------------------------------------

double ParseDouble(const std::string& text, const std::string& where) {
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0')
        throw MarkupError("cannot read \"" + text + "\" as a number for " + where);
    return value;
}

double ParseLength(const std::string& text, const std::string& where) {
    if (text == "Auto") return Auto();
    return ParseDouble(text, where);
}

std::vector<std::string> Split(const std::string& text, char separator) {
    std::vector<std::string> parts;
    std::string current;
    for (char c : text) {
        if (c == separator) {
            parts.push_back(current);
            current.clear();
        } else if (!std::isspace(static_cast<unsigned char>(c))) {
            current += c;
        }
    }
    parts.push_back(current);
    return parts;
}

Thickness ParseThickness(const std::string& text, const std::string& where) {
    const std::vector<std::string> parts = Split(text, ',');
    switch (parts.size()) {
        case 1: {
            const double all = ParseDouble(parts[0], where);
            return {all, all, all, all};
        }
        case 2: {
            const double horizontal = ParseDouble(parts[0], where);
            const double vertical = ParseDouble(parts[1], where);
            return {horizontal, vertical, horizontal, vertical};
        }
        case 4:
            return {ParseDouble(parts[0], where), ParseDouble(parts[1], where),
                    ParseDouble(parts[2], where), ParseDouble(parts[3], where)};
        default:
            throw MarkupError("a Thickness takes 1, 2 or 4 numbers, got \"" + text + "\"");
    }
}

GridLength ParseGridLength(const std::string& text, const std::string& where) {
    if (text == "Auto") return {GridUnitType::Auto, 1.0};
    if (text == "*") return {GridUnitType::Star, 1.0};
    if (!text.empty() && text.back() == '*')
        return {GridUnitType::Star, ParseDouble(text.substr(0, text.size() - 1), where)};
    return {GridUnitType::Pixel, ParseDouble(text, where)};
}

int ParseInt(const std::string& text, const std::string& where) {
    const double value = ParseDouble(text, where);
    return static_cast<int>(value);
}

template <typename T>
T ParseEnum(const std::string& text, const std::map<std::string, T>& names,
            const std::string& where) {
    auto found = names.find(text);
    if (found == names.end())
        throw MarkupError("\"" + text + "\" is not a valid " + where);
    return found->second;
}

// --- XML scanning -------------------------------------------------------------

struct Tag {
    std::string name;
    std::map<std::string, std::string> attributes;
    bool self_closing = false;
    bool closing = false;
    // Character data found between the previous tag and this one, already
    // unescaped. Only a TextBlock accepts it; anywhere else it is an error.
    std::string text_before;
};

// XML character references. Entities are decoded rather than passed through
// because a TextBlock measures its text: an undecoded "&amp;" would be four
// characters wide instead of one, and nothing would report a problem.
std::string Unescape(const std::string& text) {
    static const std::map<std::string, char32_t> kNamed = {
        {"amp", U'&'}, {"lt", U'<'}, {"gt", U'>'}, {"quot", U'"'}, {"apos", U'\''},
    };

    std::string out;
    size_t position = 0;
    while (position < text.size()) {
        if (text[position] != '&') {
            out += text[position++];
            continue;
        }
        const size_t end = text.find(';', position);
        if (end == std::string::npos)
            throw MarkupError("an unterminated character reference in text content");
        const std::string body = text.substr(position + 1, end - position - 1);

        char32_t code = 0;
        if (!body.empty() && body[0] == '#') {
            const bool hex = body.size() > 1 && (body[1] == 'x' || body[1] == 'X');
            const std::string digits = body.substr(hex ? 2 : 1);
            if (digits.empty()) throw MarkupError("an empty character reference in text content");
            size_t consumed = 0;
            code = static_cast<char32_t>(std::stoul(digits, &consumed, hex ? 16 : 10));
            if (consumed != digits.size())
                throw MarkupError("\"&" + body + ";\" is not a character reference");
        } else {
            const auto found = kNamed.find(body);
            if (found == kNamed.end())
                throw MarkupError("the entity \"&" + body + ";\" is not implemented");
            code = found->second;
        }

        // Back to UTF-8, which is how the rest of the pipeline carries text.
        if (code < 0x80) {
            out += static_cast<char>(code);
        } else if (code < 0x800) {
            out += static_cast<char>(0xC0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else if (code < 0x10000) {
            out += static_cast<char>(0xE0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (code >> 18));
            out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        }
        position = end + 1;
    }
    return out;
}

class Scanner {
public:
    explicit Scanner(const std::string& text) : text_(text) {}

    // Returns false at end of input. Character data between tags is carried on
    // the tag that follows it; whoever is holding the open element decides
    // whether it is content or a mistake.
    bool Next(Tag& tag) {
        const size_t text_start = position_;
        while (position_ < text_.size() && text_[position_] != '<') ++position_;
        std::string before = text_.substr(text_start, position_ - text_start);

        if (position_ >= text_.size()) {
            if (before.find_first_not_of(" \t\r\n") != std::string::npos)
                throw MarkupError("unexpected text content in markup");
            return false;
        }

        ++position_;
        tag = Tag{};
        tag.text_before = Unescape(before);
        if (Peek() == '/') {
            ++position_;
            tag.closing = true;
        }
        tag.name = ReadName();
        SkipSpace();

        while (position_ < text_.size() && text_[position_] != '>' && text_[position_] != '/') {
            const std::string name = ReadName();
            SkipSpace();
            if (Peek() != '=') throw MarkupError("attribute " + name + " has no value");
            ++position_;
            SkipSpace();
            const char quote = Peek();
            if (quote != '"' && quote != '\'')
                throw MarkupError("attribute " + name + " has an unquoted value");
            ++position_;
            const size_t start = position_;
            while (position_ < text_.size() && text_[position_] != quote) ++position_;
            if (position_ >= text_.size()) throw MarkupError("unterminated attribute value");
            tag.attributes[name] = text_.substr(start, position_ - start);
            ++position_;
            SkipSpace();
        }

        if (Peek() == '/') {
            ++position_;
            tag.self_closing = true;
        }
        if (Peek() != '>') throw MarkupError("malformed tag <" + tag.name + ">");
        ++position_;
        return true;
    }

private:
    char Peek() const {
        if (position_ >= text_.size()) throw MarkupError("unexpected end of markup");
        return text_[position_];
    }

    void SkipSpace() {
        while (position_ < text_.size() &&
               std::isspace(static_cast<unsigned char>(text_[position_]))) {
            ++position_;
        }
    }

    std::string ReadName() {
        const size_t start = position_;
        while (position_ < text_.size() &&
               (std::isalnum(static_cast<unsigned char>(text_[position_])) ||
                text_[position_] == '.' || text_[position_] == ':' || text_[position_] == '_' ||
                text_[position_] == '-')) {
            ++position_;
        }
        if (position_ == start) throw MarkupError("expected a name in markup");
        return text_.substr(start, position_ - start);
    }

    const std::string& text_;
    size_t position_ = 0;
};

// --- element description ------------------------------------------------------

const std::map<std::string, HorizontalAlignment> kHorizontalAlignments = {
    {"Left", HorizontalAlignment::Left},
    {"Center", HorizontalAlignment::Center},
    {"Right", HorizontalAlignment::Right},
    {"Stretch", HorizontalAlignment::Stretch},
};

const std::map<std::string, VerticalAlignment> kVerticalAlignments = {
    {"Top", VerticalAlignment::Top},
    {"Center", VerticalAlignment::Center},
    {"Bottom", VerticalAlignment::Bottom},
    {"Stretch", VerticalAlignment::Stretch},
};

const std::map<std::string, Orientation> kOrientations = {
    {"Horizontal", Orientation::Horizontal},
    {"Vertical", Orientation::Vertical},
};

// XAML also has WrapWholeWords, which differs from Wrap only in whether a word
// too long for the line may be broken. No case in the corpus uses it, so there
// is nothing to check an implementation of it against, and a wrong one would
// look exactly like a right one until some page wrapped oddly.
const std::map<std::string, TextWrapping> kTextWrappings = {
    {"NoWrap", TextWrapping::NoWrap},
    {"Wrap", TextWrapping::Wrap},
};

// Attributes every FrameworkElement takes. Returns false if the name is not
// one of them, so the caller can try the type's own properties next.
bool ApplyCommonAttribute(MarkupNode& node, const std::string& name, const std::string& value) {
    if (name == "xmlns" || name.rfind("xmlns:", 0) == 0) return true;
    if (name == "Width") { node.width = ParseLength(value, "Width"); return true; }
    if (name == "Height") { node.height = ParseLength(value, "Height"); return true; }
    if (name == "MinWidth") { node.min_width = ParseDouble(value, "MinWidth"); return true; }
    if (name == "MaxWidth") { node.max_width = ParseDouble(value, "MaxWidth"); return true; }
    if (name == "MinHeight") { node.min_height = ParseDouble(value, "MinHeight"); return true; }
    if (name == "MaxHeight") { node.max_height = ParseDouble(value, "MaxHeight"); return true; }
    if (name == "Margin") { node.margin = ParseThickness(value, "Margin"); return true; }
    if (name == "HorizontalAlignment") {
        node.horizontal_alignment = ParseEnum(value, kHorizontalAlignments, "HorizontalAlignment");
        return true;
    }
    if (name == "VerticalAlignment") {
        node.vertical_alignment = ParseEnum(value, kVerticalAlignments, "VerticalAlignment");
        return true;
    }
    if (name == "Grid.Column") { node.grid_column = ParseInt(value, "Grid.Column"); return true; }
    if (name == "Grid.Row") { node.grid_row = ParseInt(value, "Grid.Row"); return true; }
    if (name == "Grid.ColumnSpan") {
        node.grid_column_span = ParseInt(value, "Grid.ColumnSpan");
        return true;
    }
    if (name == "Grid.RowSpan") {
        node.grid_row_span = ParseInt(value, "Grid.RowSpan");
        return true;
    }
    return false;
}

void ApplyAttributes(MarkupNode& node, const std::map<std::string, std::string>& attributes) {
    for (const auto& [name, value] : attributes) {
        if (ApplyCommonAttribute(node, name, value)) continue;

        if (node.type == "Border") {
            if (name == "BorderThickness") {
                node.border_thickness = ParseThickness(value, "BorderThickness");
                continue;
            }
            if (name == "Padding") {
                node.padding = ParseThickness(value, "Padding");
                continue;
            }
        }
        if (node.type == "StackPanel") {
            if (name == "Orientation") {
                node.orientation = ParseEnum(value, kOrientations, "Orientation");
                continue;
            }
        }
        if (node.type == "TextBlock") {
            if (name == "Text") { node.text = value; continue; }
            if (name == "FontFamily") { node.font_family = value; continue; }
            if (name == "FontSize") { node.font_size = ParseDouble(value, "FontSize"); continue; }
            if (name == "TextWrapping") {
                node.text_wrapping = ParseEnum(value, kTextWrappings, "TextWrapping");
                continue;
            }
        }
        throw MarkupError("the property '" + name + "' was not found in type '" +
                          FullTypeName(node.type) + "'");
    }
}

// Attribute values with every {StaticResource} already replaced by the literal
// the resource holds.
//
// Resolution happens here, once, ahead of the property parsers rather than
// inside them: whatever a property does with its text, it does the same thing
// to a resolved resource as to an inlined literal, and cannot do otherwise.
std::map<std::string, std::string> ResolveAttributes(
    const std::map<std::string, std::string>& attributes, const ResourceScope& scope) {
    std::map<std::string, std::string> resolved;
    for (const auto& [name, value] : attributes)
        resolved.emplace(name, ResolveAttributeValue(scope, name, value));
    return resolved;
}

// One entry of a resource dictionary: <x:Double x:Key="W">60</x:Double>, or the
// aliasing form <StaticResource x:Key="W" ResourceKey="Base"/> that Terminal's
// theme dictionaries are built out of.
//
// The entry's closing tag is consumed here rather than by the main loop, so
// that its content is read as the resource's literal instead of being offered
// to whatever element encloses the dictionary.
void AddResourceEntry(ResourceDictionary& dictionary, const Tag& tag, Scanner& scanner,
                      const ResourceScope& scope) {
    std::map<std::string, std::string> attributes = tag.attributes;
    const auto key_attribute = attributes.find("x:Key");
    if (key_attribute == attributes.end())
        throw MarkupError("<" + tag.name + "> in a resource dictionary needs an x:Key");
    const std::string key = key_attribute->second;
    attributes.erase("x:Key");

    std::string content;
    if (!tag.self_closing) {
        Tag close;
        if (!scanner.Next(close) || !close.closing || close.name != tag.name)
            throw MarkupError("<" + tag.name + "> in a resource dictionary holds only text");
        content = close.text_before;
    }

    if (tag.name == "StaticResource") {
        // An alias: this key stands for whatever another key holds. Resolved
        // now, against the dictionaries already in scope -- including the one
        // being filled, so an alias may name an earlier entry beside it.
        const std::string aliased = StaticResourceElementKey(attributes, /*allow_key=*/false);
        if (!content.empty()) throw MarkupError("<StaticResource> takes no content");
        dictionary.Add(key, LookUpResource(scope, aliased, "the resource '" + key + "'"));
        return;
    }

    if (!attributes.empty()) {
        throw MarkupError("<" + tag.name + "> in a resource dictionary takes only x:Key, not '" +
                          attributes.begin()->first + "'");
    }
    dictionary.Add(key, MakeResource(tag.name, content));
}

MarkupDefinition MakeDefinition(const Tag& tag, bool is_column, const ResourceScope& scope) {
    MarkupDefinition definition;
    const std::string size_property = is_column ? "Width" : "Height";
    const std::string min_property = is_column ? "MinWidth" : "MinHeight";
    const std::string max_property = is_column ? "MaxWidth" : "MaxHeight";
    // Qualified by the owning type, because a definition's Width is a
    // GridLength while a FrameworkElement's is a number, and a resource is
    // checked against the property's type rather than reparsed.
    for (const auto& [name, raw] : tag.attributes) {
        const std::string value = ResolveAttributeValue(scope, tag.name + "." + name, raw);
        if (name == size_property) {
            definition.size = ParseGridLength(value, size_property);
        } else if (name == min_property) {
            definition.min_size = ParseDouble(value, min_property);
        } else if (name == max_property) {
            definition.max_size = ParseDouble(value, max_property);
        } else {
            throw MarkupError("the property '" + name + "' was not found in type '" + tag.name +
                              "'");
        }
    }
    return definition;
}

// Attaches a finished node to whatever is currently open above it.
void AttachChild(MarkupNode& parent, MarkupNode child) {
    if (parent.type == "Border" && !parent.children.empty())
        throw MarkupError("a Border takes a single child");
    parent.children.push_back(std::move(child));
}

// --- realising the description ------------------------------------------------

Definition ToDefinition(const MarkupDefinition& source) {
    Definition definition;
    definition.user_size = source.size;
    definition.user_min_size = source.min_size;
    definition.user_max_size = source.max_size;
    return definition;
}

std::unique_ptr<Element> BuildElement(const MarkupNode& node) {
    std::unique_ptr<Element> element;
    if (node.type == "Border") {
        auto border = std::make_unique<Border>();
        border->border_thickness = node.border_thickness;
        border->padding = node.padding;
        if (!node.children.empty()) border->SetChild(BuildElement(node.children.front()));
        element = std::move(border);
    } else if (node.type == "Grid") {
        auto grid = std::make_unique<Grid>();
        for (const MarkupDefinition& definition : node.column_definitions)
            grid->column_definitions.push_back(ToDefinition(definition));
        for (const MarkupDefinition& definition : node.row_definitions)
            grid->row_definitions.push_back(ToDefinition(definition));
        for (const MarkupNode& child : node.children) grid->AddChild(BuildElement(child));
        element = std::move(grid);
    } else if (node.type == "StackPanel") {
        auto stack = std::make_unique<StackPanel>();
        stack->orientation = node.orientation;
        for (const MarkupNode& child : node.children) stack->AddChild(BuildElement(child));
        element = std::move(stack);
    } else if (node.type == "TextBlock") {
        auto text = std::make_unique<TextBlock>();
        text->text = node.text;
        text->font_family = node.font_family;
        text->font_size = node.font_size;
        text->text_wrapping = node.text_wrapping;
        if (!node.children.empty())
            throw MarkupError("a TextBlock takes text, not child elements");
        element = std::move(text);
    } else {
        throw MarkupError("the type '" + node.type + "' is not implemented");
    }

    element->width = node.width;
    element->height = node.height;
    element->min_width = node.min_width;
    element->max_width = node.max_width;
    element->min_height = node.min_height;
    element->max_height = node.max_height;
    element->margin = node.margin;
    element->horizontal_alignment = node.horizontal_alignment;
    element->vertical_alignment = node.vertical_alignment;
    element->grid_column = node.grid_column;
    element->grid_row = node.grid_row;
    element->grid_column_span = node.grid_column_span;
    element->grid_row_span = node.grid_row_span;
    return element;
}

}  // namespace

std::string FullTypeName(const std::string& short_name) {
    if (short_name == "Border" || short_name == "Grid" || short_name == "StackPanel" ||
        short_name == "TextBlock")
        return "Windows.UI.Xaml.Controls." + short_name;
    throw MarkupError("the type '" + short_name + "' is not implemented");
}

MarkupNode ParseMarkup(const std::string& markup) {
    Scanner scanner(markup);

    MarkupNode root;
    bool have_root = false;
    // Open elements, innermost last. The root is held separately so it can be
    // returned by value once the stack empties.
    std::vector<MarkupNode> open;
    // The property element currently being filled, e.g. Grid.ColumnDefinitions.
    // Empty when ordinary child elements are expected.
    std::string property_element;
    // What that property element expects to be filled with. The three kinds
    // read their contents differently enough that the name alone is not enough
    // to dispatch on.
    enum class Section { None, Definitions, Resources, Value };
    Section section = Section::None;
    // An explicit <ResourceDictionary> wrapper inside a Resources section. The
    // wrapper is optional in XAML and carries nothing this parser reads, so it
    // is tracked only far enough to match its closing tag.
    bool dictionary_open = false;
    // Whether a <X.Property> element has already been given its one value.
    bool value_filled = false;

    // The dictionaries a lookup walks: the element being filled, then each
    // ancestor, innermost first.
    //
    // An element's own dictionary is *not* in scope for its own attributes.
    // Attributes are read when the tag opens, and <X.Resources> is a child, so
    // it has not been seen yet -- whereas a <X.Property> element written after
    // <X.Resources> does see it. Whether the real runtime defers far enough to
    // erase that distinction is one of the questions the L5 probe cases exist
    // to put to it; until it answers, this is the WPF behaviour, where
    // StaticResource is resolved at parse time against what has been parsed.
    auto scope = [&open]() {
        ResourceScope chain;
        for (auto it = open.rbegin(); it != open.rend(); ++it) {
            if (!it->resources.empty()) chain.push_back(&it->resources);
        }
        return chain;
    };

    // Character data belongs to the element it sits inside, and only a
    // TextBlock has anywhere to put it. Everywhere else the old rule stands:
    // reject it rather than drop it silently.
    auto take_text = [&](const Tag& scanned) {
        if (scanned.text_before.empty()) return;
        if (!open.empty() && open.back().type == "TextBlock") {
            open.back().text += scanned.text_before;
            return;
        }
        if (scanned.text_before.find_first_not_of(" \t\r\n") != std::string::npos)
            throw MarkupError("unexpected text content in markup");
    };

    Tag tag;
    while (scanner.Next(tag)) {
        take_text(tag);
        if (tag.closing) {
            if (dictionary_open && tag.name == "ResourceDictionary") {
                dictionary_open = false;
                continue;
            }
            if (!property_element.empty()) {
                if (tag.name != property_element)
                    throw MarkupError("</" + tag.name + "> closes <" + property_element + ">");
                if (section == Section::Value && !value_filled)
                    throw MarkupError("<" + property_element + "> was given no value");
                property_element.clear();
                section = Section::None;
                continue;
            }
            if (open.empty()) throw MarkupError("</" + tag.name + "> with nothing open");
            MarkupNode finished = std::move(open.back());
            open.pop_back();
            if (tag.name != finished.type)
                throw MarkupError("</" + tag.name + "> does not close the open element");
            if (open.empty()) {
                root = std::move(finished);
                have_root = true;
            } else {
                AttachChild(open.back(), std::move(finished));
            }
            continue;
        }

        // A dotted name is a property element -- <Grid.ColumnDefinitions> sets
        // a property of its parent rather than adding a child to it.
        if (tag.name.find('.') != std::string::npos) {
            if (!property_element.empty())
                throw MarkupError("<" + tag.name + "> inside <" + property_element + ">");
            if (open.empty()) throw MarkupError("<" + tag.name + "> with no element to set it on");
            if (!tag.attributes.empty())
                throw MarkupError("<" + tag.name + "> cannot carry attributes");
            const size_t dot = tag.name.find('.');
            const std::string owner = tag.name.substr(0, dot);
            const std::string member = tag.name.substr(dot + 1);
            // Only the concrete type is accepted as the owner. XAML also allows
            // a base type -- <FrameworkElement.Width> on a Border -- and an
            // attached property in element form; neither appears in the corpus,
            // so both are rejected by name rather than half-supported.
            if (owner != open.back().type) {
                throw MarkupError("<" + tag.name + "> is not a property of the open <" +
                                  open.back().type + ">");
            }
            if (member == "ColumnDefinitions" || member == "RowDefinitions") {
                if (open.back().type != "Grid")
                    throw MarkupError("<" + tag.name + "> is only valid on a Grid");
                section = Section::Definitions;
            } else if (member == "Resources") {
                section = Section::Resources;
            } else {
                // A scalar property, set as an element so that a resource
                // reference can be written as one.
                if (tag.self_closing)
                    throw MarkupError("<" + tag.name + "> was given no value");
                section = Section::Value;
                value_filled = false;
            }
            if (tag.self_closing) {
                section = Section::None;
                continue;
            }
            property_element = tag.name;
            continue;
        }

        if (section == Section::Resources) {
            if (tag.name == "ResourceDictionary") {
                // The wrapper XAML lets a dictionary be written with or
                // without. It changes nothing about the entries inside it.
                if (dictionary_open)
                    throw MarkupError("<ResourceDictionary> inside <ResourceDictionary>");
                if (!tag.attributes.empty()) {
                    throw MarkupError("<ResourceDictionary> takes no attributes here; '" +
                                      tag.attributes.begin()->first + "' is not implemented");
                }
                if (!tag.self_closing) dictionary_open = true;
                continue;
            }
            AddResourceEntry(open.back().resources, tag, scanner, scope());
            continue;
        }

        if (section == Section::Value) {
            if (tag.name != "StaticResource") {
                throw MarkupError("<" + property_element + "> takes a <StaticResource>, not <" +
                                  tag.name + ">");
            }
            if (value_filled)
                throw MarkupError("<" + property_element + "> was given a second value");
            if (!tag.self_closing) {
                Tag close;
                if (!scanner.Next(close) || !close.closing || close.name != "StaticResource")
                    throw MarkupError("<StaticResource> must be empty");
            }
            const std::string property = property_element.substr(property_element.find('.') + 1);
            const std::string key = StaticResourceElementKey(tag.attributes, /*allow_key=*/false);
            // Resolved to its literal and then applied through the ordinary
            // attribute path, so <Border.Width><StaticResource .../></Border.Width>
            // and Width="60" reach the property by the same code.
            ApplyAttributes(open.back(), {{property, ResolveResource(scope(), key, property)}});
            value_filled = true;
            continue;
        }

        if (section == Section::Definitions) {
            const bool is_column = property_element == "Grid.ColumnDefinitions";
            const std::string expected = is_column ? "ColumnDefinition" : "RowDefinition";
            if (tag.name != expected)
                throw MarkupError("<" + property_element + "> takes <" + expected + "> elements");
            (is_column ? open.back().column_definitions : open.back().row_definitions)
                .push_back(MakeDefinition(tag, is_column, scope()));
            if (!tag.self_closing) {
                Tag close;
                if (!scanner.Next(close) || !close.closing || close.name != expected)
                    throw MarkupError("<" + expected + "> must be empty");
                take_text(close);
            }
            continue;
        }

        // Rejects an unimplemented type here rather than at build time, so
        // that a case naming one fails identically whichever consumer is
        // realising the tree.
        FullTypeName(tag.name);
        MarkupNode node;
        node.type = tag.name;
        ApplyAttributes(node, ResolveAttributes(tag.attributes, scope()));
        if (tag.self_closing) {
            if (open.empty()) {
                root = std::move(node);
                have_root = true;
            } else {
                AttachChild(open.back(), std::move(node));
            }
        } else {
            open.push_back(std::move(node));
        }
    }

    if (!open.empty()) throw MarkupError("markup ended with elements still open");
    if (!have_root) throw MarkupError("markup contains no root element");
    return root;
}

std::unique_ptr<Element> LoadMarkup(const std::string& markup) {
    return BuildElement(ParseMarkup(markup));
}

}  // namespace openxaml
