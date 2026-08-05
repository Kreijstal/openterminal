#include "markup.h"

#include "markup_tree.h"

#include <cctype>
#include <cstdlib>
#include <map>
#include <vector>

#include "border.h"
#include "control.h"
#include "grid.h"
#include "stack_panel.h"

namespace openxaml {
namespace {

// --- value parsing ------------------------------------------------------------

bool ParseBool(const std::string& text, const std::string& where) {
    if (text == "True" || text == "true") return true;
    if (text == "False" || text == "false") return false;
    throw MarkupError("cannot read \"" + text + "\" as a boolean for " + where);
}

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

// The property owner chain for a markup type name, which is what decides
// whether an attribute names a property of that type. Asking a freshly built
// element for it would work; this avoids building one to answer a question
// about the type.
const std::vector<std::string>& OwnersFor(const std::string& type) {
    if (type == "Border") return Border::Owners();
    if (type == "ContentControl") return ContentControl::Owners();
    if (type == "Grid") return Grid::Owners();
    if (type == "StackPanel") return StackPanel::Owners();
    if (type == "TextBlock") return TextBlock::Owners();
    throw MarkupError("the type '" + type + "' is not implemented");
}

// Reads one attribute value onto the node. The property is already known to
// exist on the type -- the registry decided that -- so all this has to know is
// how its value is spelled.
//
// Each case writes the typed field and hands the same value to the store, so
// the two realisations of a node cannot drift apart. See markup_tree.h for why
// there are two.
void ApplyProperty(MarkupNode& node, const DependencyProperty& property,
                   const std::string& value) {
    const std::string& name = property.name();
    const auto assign = [&](PropertyValue parsed) {
        node.properties.push_back(MarkupProperty{&property, std::move(parsed)});
    };

    if (name == "Width") return assign(node.width = ParseLength(value, name));
    if (name == "Height") return assign(node.height = ParseLength(value, name));
    if (name == "MinWidth") return assign(node.min_width = ParseDouble(value, name));
    if (name == "MaxWidth") return assign(node.max_width = ParseDouble(value, name));
    if (name == "MinHeight") return assign(node.min_height = ParseDouble(value, name));
    if (name == "MaxHeight") return assign(node.max_height = ParseDouble(value, name));
    if (name == "Margin") return assign(node.margin = ParseThickness(value, name));
    if (name == "HorizontalAlignment") {
        node.horizontal_alignment = ParseEnum(value, kHorizontalAlignments, name);
        return assign(static_cast<int>(node.horizontal_alignment));
    }
    if (name == "VerticalAlignment") {
        node.vertical_alignment = ParseEnum(value, kVerticalAlignments, name);
        return assign(static_cast<int>(node.vertical_alignment));
    }
    if (name == "UseLayoutRounding")
        return assign(node.use_layout_rounding = ParseBool(value, name));
    if (name == "Opacity") return assign(node.opacity = ParseDouble(value, name));
    if (name == "Grid.Column") return assign(node.grid_column = ParseInt(value, name));
    if (name == "Grid.Row") return assign(node.grid_row = ParseInt(value, name));
    if (name == "Grid.ColumnSpan") return assign(node.grid_column_span = ParseInt(value, name));
    if (name == "Grid.RowSpan") return assign(node.grid_row_span = ParseInt(value, name));
    if (name == "BorderThickness")
        return assign(node.border_thickness = ParseThickness(value, name));
    if (name == "Padding") return assign(node.padding = ParseThickness(value, name));
    if (name == "Orientation") {
        node.orientation = ParseEnum(value, kOrientations, name);
        return assign(static_cast<int>(node.orientation));
    }
    if (name == "TextWrapping") {
        node.text_wrapping = ParseEnum(value, kTextWrappings, name);
        return assign(static_cast<int>(node.text_wrapping));
    }
    if (name == "FontSize") return assign(node.font_size = ParseDouble(value, name));
    if (name == "FontFamily") return assign(node.font_family = value);
    // A brush, carried as the text that spells it. Nothing here paints, so the
    // value is stored and never interpreted -- which is honest about what it
    // is: enough to show the property system carried it, and not a colour.
    if (name == "Foreground") return assign(node.foreground = value);
    // The content property, so it can arrive as an attribute or as character
    // data between the tags. Both land in node.text, and BuildElement gives
    // the element whatever is there once the parse is done.
    if (name == "Text") {
        node.text = value;
        return;
    }

    throw MarkupError("the property '" + name +
                      "' is registered but this parser cannot read a value for it");
}

void ApplyAttributes(MarkupNode& node, const std::map<std::string, std::string>& attributes) {
    for (const auto& [name, value] : attributes) {
        if (name == "xmlns" || name.rfind("xmlns:", 0) == 0) continue;

        // Directives are not properties: they instruct the XAML compiler
        // rather than setting anything on the object. x:Name is accepted and
        // dropped because nothing here resolves names -- the corpus reads its
        // results out of the measured tree by path.
        if (name.rfind("x:", 0) == 0) {
            if (name == "x:Name") continue;
            throw MarkupError("the directive '" + name + "' is not implemented");
        }

        // The registry decides, not a chain of type tests. This is what makes
        // Opacity settable on every UIElement, Grid.Column settable on
        // anything at all, and FontSize a property of a Control and not of a
        // StackPanel -- which is the error the real runtime gives, and the
        // reason the corpus has the case it has.
        const DependencyProperty* property = FindProperty(OwnersFor(node.type), name);
        if (!property) {
            throw MarkupError("the property '" + name + "' was not found in type '" +
                              FullTypeName(node.type) + "'");
        }
        ApplyProperty(node, *property, value);
    }
}

MarkupDefinition MakeDefinition(const Tag& tag, bool is_column) {
    MarkupDefinition definition;
    const std::string size_property = is_column ? "Width" : "Height";
    const std::string min_property = is_column ? "MinWidth" : "MinHeight";
    const std::string max_property = is_column ? "MaxWidth" : "MaxHeight";
    for (const auto& [name, value] : tag.attributes) {
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
    if (parent.type == "ContentControl" && !parent.children.empty())
        throw MarkupError("a ContentControl takes a single piece of content");
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
        if (!node.children.empty()) border->SetChild(BuildElement(node.children.front()));
        element = std::move(border);
    } else if (node.type == "ContentControl") {
        auto control = std::make_unique<ContentControl>();
        if (!node.children.empty()) control->SetContent(BuildElement(node.children.front()));
        element = std::move(control);
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
        for (const MarkupNode& child : node.children) stack->AddChild(BuildElement(child));
        element = std::move(stack);
    } else if (node.type == "TextBlock") {
        auto text = std::make_unique<TextBlock>();
        if (!node.children.empty())
            throw MarkupError("a TextBlock takes text, not child elements");
        if (!node.text.empty()) text->set_text(node.text);
        element = std::move(text);
    } else {
        throw MarkupError("the type '" + node.type + "' is not implemented");
    }

    // Only what the markup wrote, which is not the same as every property the
    // element has. An inherited property left alone must stay unset so that it
    // reads its parent's value; assigning the default here instead would give
    // every TextBlock a local FontSize of 14 and nothing would ever inherit.
    //
    // Set after the children are attached, so that a value flowing down
    // reaches a subtree that is already there.
    for (const MarkupProperty& assignment : node.properties)
        element->SetValue(*assignment.property, assignment.value);
    return element;
}

}  // namespace

std::string FullTypeName(const std::string& short_name) {
    if (short_name == "Border" || short_name == "ContentControl" || short_name == "Grid" ||
        short_name == "StackPanel" || short_name == "TextBlock")
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
            if (!property_element.empty()) {
                if (tag.name != property_element)
                    throw MarkupError("</" + tag.name + "> closes <" + property_element + ">");
                property_element.clear();
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
            if (tag.name != "Grid.ColumnDefinitions" && tag.name != "Grid.RowDefinitions")
                throw MarkupError("the property element '" + tag.name + "' is not implemented");
            if (open.back().type != "Grid")
                throw MarkupError("<" + tag.name + "> is only valid on a Grid");
            if (tag.self_closing) continue;
            property_element = tag.name;
            continue;
        }

        if (!property_element.empty()) {
            const bool is_column = property_element == "Grid.ColumnDefinitions";
            const std::string expected = is_column ? "ColumnDefinition" : "RowDefinition";
            if (tag.name != expected)
                throw MarkupError("<" + property_element + "> takes <" + expected + "> elements");
            (is_column ? open.back().column_definitions : open.back().row_definitions)
                .push_back(MakeDefinition(tag, is_column));
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
        ApplyAttributes(node, tag.attributes);
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
