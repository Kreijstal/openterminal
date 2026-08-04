#include "markup.h"

#include <cctype>
#include <cstdlib>
#include <map>
#include <vector>

#include "border.h"
#include "grid.h"
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
};

class Scanner {
public:
    explicit Scanner(const std::string& text) : text_(text) {}

    // Returns false at end of input. Text between tags is rejected: no case in
    // this subset has content, and accepting it would quietly drop it.
    bool Next(Tag& tag) {
        while (position_ < text_.size() &&
               std::isspace(static_cast<unsigned char>(text_[position_]))) {
            ++position_;
        }
        if (position_ >= text_.size()) return false;
        if (text_[position_] != '<')
            throw MarkupError("unexpected text content in markup");

        ++position_;
        tag = Tag{};
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

// --- element construction -----------------------------------------------------

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

std::unique_ptr<Element> CreateElement(const std::string& name) {
    if (name == "Border") return std::make_unique<Border>();
    if (name == "Grid") return std::make_unique<Grid>();
    if (name == "StackPanel") return std::make_unique<StackPanel>();
    throw MarkupError("the type '" + name + "' is not implemented");
}

// Attributes every FrameworkElement takes. Returns false if the name is not
// one of them, so the caller can try the type's own properties next.
bool ApplyCommonAttribute(Element& element, const std::string& name, const std::string& value) {
    if (name == "xmlns" || name.rfind("xmlns:", 0) == 0) return true;
    if (name == "Width") { element.width = ParseLength(value, "Width"); return true; }
    if (name == "Height") { element.height = ParseLength(value, "Height"); return true; }
    if (name == "MinWidth") { element.min_width = ParseDouble(value, "MinWidth"); return true; }
    if (name == "MaxWidth") { element.max_width = ParseDouble(value, "MaxWidth"); return true; }
    if (name == "MinHeight") { element.min_height = ParseDouble(value, "MinHeight"); return true; }
    if (name == "MaxHeight") { element.max_height = ParseDouble(value, "MaxHeight"); return true; }
    if (name == "Margin") { element.margin = ParseThickness(value, "Margin"); return true; }
    if (name == "HorizontalAlignment") {
        element.horizontal_alignment = ParseEnum(value, kHorizontalAlignments, "HorizontalAlignment");
        return true;
    }
    if (name == "VerticalAlignment") {
        element.vertical_alignment = ParseEnum(value, kVerticalAlignments, "VerticalAlignment");
        return true;
    }
    if (name == "Grid.Column") { element.grid_column = ParseInt(value, "Grid.Column"); return true; }
    if (name == "Grid.Row") { element.grid_row = ParseInt(value, "Grid.Row"); return true; }
    if (name == "Grid.ColumnSpan") {
        element.grid_column_span = ParseInt(value, "Grid.ColumnSpan");
        return true;
    }
    if (name == "Grid.RowSpan") {
        element.grid_row_span = ParseInt(value, "Grid.RowSpan");
        return true;
    }
    return false;
}

void ApplyAttributes(Element& element, const std::map<std::string, std::string>& attributes) {
    for (const auto& [name, value] : attributes) {
        if (ApplyCommonAttribute(element, name, value)) continue;

        if (auto* border = dynamic_cast<Border*>(&element)) {
            if (name == "BorderThickness") {
                border->border_thickness = ParseThickness(value, "BorderThickness");
                continue;
            }
            if (name == "Padding") {
                border->padding = ParseThickness(value, "Padding");
                continue;
            }
        }
        if (auto* stack = dynamic_cast<StackPanel*>(&element)) {
            if (name == "Orientation") {
                stack->orientation = ParseEnum(value, kOrientations, "Orientation");
                continue;
            }
        }
        throw MarkupError("the property '" + name + "' was not found in type '" +
                          element.TypeName() + "'");
    }
}

Definition MakeDefinition(const Tag& tag, bool is_column) {
    Definition definition;
    const std::string size_property = is_column ? "Width" : "Height";
    const std::string min_property = is_column ? "MinWidth" : "MinHeight";
    const std::string max_property = is_column ? "MaxWidth" : "MaxHeight";
    for (const auto& [name, value] : tag.attributes) {
        if (name == size_property) {
            definition.user_size = ParseGridLength(value, size_property);
        } else if (name == min_property) {
            definition.user_min_size = ParseDouble(value, min_property);
        } else if (name == max_property) {
            definition.user_max_size = ParseDouble(value, max_property);
        } else {
            throw MarkupError("the property '" + name + "' was not found in type '" + tag.name +
                              "'");
        }
    }
    return definition;
}

// Attaches a finished element to whatever is currently open above it.
void AttachChild(Element& parent, std::unique_ptr<Element> child) {
    if (auto* border = dynamic_cast<Border*>(&parent)) {
        if (!border->Children().empty())
            throw MarkupError("a Border takes a single child");
        border->SetChild(std::move(child));
        return;
    }
    if (auto* panel = dynamic_cast<Panel*>(&parent)) {
        panel->AddChild(std::move(child));
        return;
    }
    throw MarkupError("the type '" + parent.TypeName() + "' does not take children");
}

}  // namespace

std::unique_ptr<Element> LoadMarkup(const std::string& markup) {
    Scanner scanner(markup);

    std::unique_ptr<Element> root;
    // Open elements, innermost last. The root is held separately so it can be
    // returned by value once the stack empties.
    std::vector<std::unique_ptr<Element>> open;
    // The property element currently being filled, e.g. Grid.ColumnDefinitions.
    // Empty when ordinary child elements are expected.
    std::string property_element;

    Tag tag;
    while (scanner.Next(tag)) {
        if (tag.closing) {
            if (!property_element.empty()) {
                if (tag.name != property_element)
                    throw MarkupError("</" + tag.name + "> closes <" + property_element + ">");
                property_element.clear();
                continue;
            }
            if (open.empty()) throw MarkupError("</" + tag.name + "> with nothing open");
            std::unique_ptr<Element> finished = std::move(open.back());
            open.pop_back();
            if (tag.name != finished->TypeName().substr(finished->TypeName().rfind('.') + 1))
                throw MarkupError("</" + tag.name + "> does not close the open element");
            if (open.empty()) {
                root = std::move(finished);
            } else {
                AttachChild(*open.back(), std::move(finished));
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
            if (!dynamic_cast<Grid*>(open.back().get()))
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
            auto* grid = static_cast<Grid*>(open.back().get());
            (is_column ? grid->column_definitions : grid->row_definitions)
                .push_back(MakeDefinition(tag, is_column));
            if (!tag.self_closing) {
                Tag close;
                if (!scanner.Next(close) || !close.closing || close.name != expected)
                    throw MarkupError("<" + expected + "> must be empty");
            }
            continue;
        }

        std::unique_ptr<Element> element = CreateElement(tag.name);
        ApplyAttributes(*element, tag.attributes);
        if (tag.self_closing) {
            if (open.empty()) {
                root = std::move(element);
            } else {
                AttachChild(*open.back(), std::move(element));
            }
        } else {
            open.push_back(std::move(element));
        }
    }

    if (!open.empty()) throw MarkupError("markup ended with elements still open");
    if (!root) throw MarkupError("markup contains no root element");
    return root;
}

}  // namespace openxaml
