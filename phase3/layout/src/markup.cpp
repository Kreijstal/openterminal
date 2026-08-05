#include "markup.h"

#include "markup_tree.h"

#include <cctype>
#include <cstdlib>
#include <map>
#include <vector>

#include "border.h"
#include "brush.h"
#include "canvas.h"
#include "content_presenter.h"
#include "control.h"
#include "geometry.h"
#include "grid.h"
#include "icon.h"
#include "image.h"
#include "resources.h"
#include "shape.h"
#include "stack_panel.h"
#include "style.h"
#include "xdirectives.h"

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

// Hidden is missing on purpose. It renders nothing but still takes part in
// layout, so it is indistinguishable from Visible in every number the probe
// records -- there is no case that could tell a correct implementation of it
// from a wrong one.
const std::map<std::string, Visibility> kVisibilities = {
    {"Visible", Visibility::Visible},
    {"Collapsed", Visibility::Collapsed},
};

// The property owner chain for a markup type name, which is what decides
// whether an attribute names a property of that type. Asking a freshly built
// element for it would work; this avoids building one to answer a question
// about the type.
//
// This is also where BorderThickness and Padding stop being a per-type
// question: Grid, StackPanel and ContentPresenter carry the chrome owner in
// their chains and Canvas does not, so the registry refuses a Canvas a Padding
// without anything here having to say so.
const std::vector<std::string>& OwnersFor(const std::string& type) {
    if (type == "Border") return Border::Owners();
    if (type == "Canvas") return Canvas::Owners();
    if (type == "ContentControl") return ContentControl::Owners();
    if (type == "ContentPresenter") return ContentPresenter::Owners();
    if (type == "Grid") return Grid::Owners();
    if (type == "Image") return Image::Owners();
    if (type == "Path") return Path::Owners();
    if (type == "PathIcon") return PathIcon::Owners();
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
    if (name == "Canvas.Left") return assign(node.canvas_left = ParseDouble(value, name));
    if (name == "Canvas.Top") return assign(node.canvas_top = ParseDouble(value, name));
    if (name == "Visibility") {
        node.visibility = ParseEnum(value, kVisibilities, name);
        return assign(static_cast<int>(node.visibility));
    }
    if (name == "BorderThickness")
        return assign(node.border_thickness = ParseThickness(value, name));
    if (name == "Padding") return assign(node.padding = ParseThickness(value, name));
    // The attribute shorthand for a brush is always a colour, and a colour is
    // always a SolidColorBrush. The colour is validated and dropped: a typo in
    // a page must not load silently, and nothing here paints.
    if (name == "Background") {
        ValidateColor(value, name);
        return assign(node.background = "SolidColorBrush");
    }
    if (name == "Orientation") {
        node.orientation = ParseEnum(value, kOrientations, name);
        return assign(static_cast<int>(node.orientation));
    }
    if (name == "Spacing") return assign(node.spacing = ParseDouble(value, name));
    if (name == "HorizontalContentAlignment") {
        node.horizontal_content_alignment = ParseEnum(value, kHorizontalAlignments, name);
        return assign(static_cast<int>(node.horizontal_content_alignment));
    }
    if (name == "VerticalContentAlignment") {
        node.vertical_content_alignment = ParseEnum(value, kVerticalAlignments, name);
        return assign(static_cast<int>(node.vertical_content_alignment));
    }
    // Parsed here rather than at build time, because a geometry that cannot be
    // read is a markup error and not a measurement of zero. The bounds are all
    // layout wants and are not a value the store can hold, so they travel on
    // the node -- as a Grid's definitions do.
    if (name == "Data") {
        node.data = PathGeometryBounds(value);
        return;
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

        // A name registers the element in a namescope so that code-behind can
        // reach it. There is no code-behind here and no namescope, and neither
        // spelling has ever moved an element. `Name` is not a directive, but
        // it is the same non-effect as x:Name, which xdirectives.cpp drops.
        if (name == "Name") continue;

        // Directives are taken off the attributes before this point, by
        // TakeXDirectives, so that an unimplemented one fails by its own name
        // rather than as a property the registry could not find. One arriving
        // here is a caller that skipped that step.
        if (name.rfind("x:", 0) == 0)
            throw MarkupError("the directive '" + name + "' is not implemented");

        // Accessibility metadata. Attached, so the registry would have to be
        // told about every one of them to reject the rest; every property on
        // it describes the element to a screen reader and none is read by
        // measure or arrange, so the whole stem is dropped.
        if (name.rfind("AutomationProperties.", 0) == 0) continue;

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

// --- styles -------------------------------------------------------------------

// The scanner, as the style parser wants to see it. The style grammar lives in
// style.cpp and the XML scanning lives here; this is the whole of what passes
// between the two, so neither has to know how the other works.
class ScannerTags : public StyleTagSource {
public:
    explicit ScannerTags(Scanner& scanner) : scanner_(scanner) {}

    bool Next(StyleTag& out) override {
        Tag tag;
        if (!scanner_.Next(tag)) return false;
        out.name = tag.name;
        out.attributes = tag.attributes;
        out.self_closing = tag.self_closing;
        out.closing = tag.closing;
        out.text_before = tag.text_before;
        return true;
    }

private:
    Scanner& scanner_;
};

// What building a style needs from the markup layer: the type registry, the
// attribute parser, and the resource scope the style was declared in.
//
// ParseSetter runs the *ordinary* attribute path against a scratch node of the
// target type, and that is the point of it. `<Setter Property="Width"
// Value="60"/>` and `Width="60"` written on the element reach the property
// through one piece of code, so they cannot disagree about what "60" means, or
// about which types have a Width, or about the message when the value is not a
// number. It is the same argument the resource system makes for carrying
// literals rather than parsed values, applied one level up.
class MarkupStyleHost : public StyleHost {
public:
    explicit MarkupStyleHost(ResourceScope scope) : scope_(std::move(scope)) {}

    void ValidateTargetType(const std::string& type) override {
        // Both, because they answer different questions: FullTypeName says the
        // corpus knows the type, OwnersFor says the property registry does.
        FullTypeName(type);
        OwnersFor(type);
    }

    const std::vector<std::string>& OwnerChain(const std::string& type) override {
        return OwnersFor(type);
    }

    StyleSetter ParseSetter(const std::string& target_type, const std::string& property,
                            const std::string& value) override {
        MarkupNode scratch;
        scratch.type = target_type;
        ApplyAttributes(scratch, {{property, ResolveAttributeValue(scope_, property, value)}});
        if (scratch.properties.size() != 1) {
            // ApplyAttributes drops the handful of names that set nothing --
            // x:Name, an AutomationProperties stem -- and ApplyProperty carries
            // Text and Data on the node instead of in the store. A Setter for
            // any of those would apply nothing at all, which is the one outcome
            // this parser never allows silently.
            throw MarkupError("a Setter for '" + property + "' on '" + target_type +
                              "' is not implemented: nothing it sets reaches the property store");
        }
        return StyleSetter{scratch.properties.front().property,
                           std::move(scratch.properties.front().value)};
    }

    std::shared_ptr<const Style> LookUpBasedOn(const std::string& raw) override {
        // Through the ordinary resolver, so the named form, the missing key
        // and the wrong-shaped key are all refused in the words every other
        // lookup is refused in. A Style is a declared shape, so a BasedOn on
        // an x:Double fails as "cannot supply 'BasedOn'" and not as a null.
        //
        // The scope is the dictionary's, not the styled element's: BasedOn is
        // read where the style is written.
        const std::shared_ptr<const Style>& style =
            ResolveResourceReference(scope_, "BasedOn", raw).style;
        if (!style) throw MarkupError("BasedOn names something that is not a Style");
        return style;
    }

    std::string ResolveResourceElement(const std::map<std::string, std::string>& attributes,
                                       const std::string& property) override {
        const std::string key = StaticResourceElementKey(attributes, /*allow_key=*/false);
        return ResolveResource(scope_, key, property);
    }

private:
    ResourceScope scope_;
};

// The Style a `Style="{StaticResource K}"` attribute names.
//
// The scope is the styled element's, which is the ordinary attribute rule --
// an explicit reference is resolved where it is written, like every other
// {StaticResource}. That is not the rule for the implicit route; see
// FindImplicitStyle's caller.
std::shared_ptr<const Style> ResolveStyleReference(const ResourceScope& scope,
                                                   const std::string& raw) {
    const std::shared_ptr<const Style>& style =
        ResolveResourceReference(scope, "Style", raw).style;
    if (!style) throw MarkupError("Style names something that is not a Style");
    return style;
}

// --- property elements --------------------------------------------------------

// Reads the one brush inside a <Something.Background> and the closing tag, and
// returns the brush's short type name. The brush itself is discarded: a
// background has no effect on any number the probe records. What matters is
// that an unimplemented brush type still fails by name here rather than being
// dropped.
std::string ParseBrushPropertyElement(Scanner& scanner, const std::string& property_element) {
    std::string brush;
    Tag tag;
    while (scanner.Next(tag)) {
        if (tag.text_before.find_first_not_of(" \t\r\n") != std::string::npos)
            throw MarkupError("unexpected text content in <" + property_element + ">");
        if (tag.closing) {
            if (tag.name != property_element)
                throw MarkupError("</" + tag.name + "> closes <" + property_element + ">");
            if (brush.empty())
                throw MarkupError("<" + property_element + "> is empty");
            return brush;
        }
        if (!brush.empty())
            throw MarkupError("<" + property_element + "> takes a single brush");
        FullBrushTypeName(tag.name);
        brush = tag.name;
        if (!tag.self_closing) {
            // A brush with content -- gradient stops, an image source -- is a
            // brush whose type is understood but whose contents are not.
            Tag close;
            if (!scanner.Next(close) || !close.closing || close.name != brush)
                throw MarkupError("a <" + brush + "> with content is not implemented");
        }
    }
    throw MarkupError("<" + property_element + "> was not closed");
}

// Reads a <VisualStateManager.VisualStateGroups> and checks that it changes
// nothing.
//
// A visual state is a set of setters and a storyboard, applied when the state
// is entered. At the moment the probe measures, no state has been entered, so
// a group of *empty* states is genuinely inert and can be skipped. A state
// that carries anything is a different matter -- Terminal's pages use them to
// change sizes -- and the element inside it is named rather than skipped, so
// that a case cannot pass by having its styling silently ignored.
void ParseVisualStateGroups(Scanner& scanner) {
    const std::string property_element = "VisualStateManager.VisualStateGroups";
    std::vector<std::string> open;
    Tag tag;
    while (scanner.Next(tag)) {
        if (tag.text_before.find_first_not_of(" \t\r\n") != std::string::npos)
            throw MarkupError("unexpected text content in <" + property_element + ">");
        if (tag.closing) {
            if (open.empty()) {
                if (tag.name != property_element)
                    throw MarkupError("</" + tag.name + "> closes <" + property_element + ">");
                return;
            }
            if (tag.name != open.back())
                throw MarkupError("</" + tag.name + "> does not close the open element");
            open.pop_back();
            continue;
        }
        const std::string expected = open.empty() ? "VisualStateGroup" : "VisualState";
        if (open.size() >= 2 || tag.name != expected)
            throw MarkupError("the visual state element '" + tag.name + "' is not implemented");
        for (const auto& [name, value] : tag.attributes) {
            (void)value;
            if (name != "x:Name" && name != "Name")
                throw MarkupError("the property '" + name + "' was not found in type '" +
                                  "Windows.UI.Xaml." + tag.name + "'");
        }
        if (!tag.self_closing) open.push_back(tag.name);
    }
    throw MarkupError("<" + property_element + "> was not closed");
}

// --- resources ----------------------------------------------------------------

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

// --- x:Uid --------------------------------------------------------------------

// Writes the properties a uid names into the element's attributes.
//
// A uid with no entry sets nothing and is not an error. That is the state every
// case in the corpus is in -- no table is loaded, and the oracle probe has no
// resource map to load one from -- and it is also what a real page does before
// its resources are available. Making it fatal would make the common case the
// broken one.
//
// **Precedence is a provisional choice, not a measured fact.** The uid's value
// is written over whatever the element wrote in place, on the reading that
// x:Uid exists to let a translator override the markup: an author who could
// win by writing the attribute would defeat the localisation the directive is
// for. WPF has no analogue that settles it and WinUI 2 does not document the
// order. L5-xdirectives-uid-precedence asks the runtime; until it answers, a
// case that sets both is the one place this guess is visible.
void ApplyUid(std::map<std::string, std::string>& attributes, const XDirectives& directives,
              const StringTable& strings) {
    if (!directives.has_uid) return;
    const StringTable::Properties* properties = strings.Find(directives.uid);
    if (!properties) return;
    for (const auto& [name, value] : *properties) attributes[name] = value;
}

// One entry of a resource dictionary: <x:Double x:Key="W">60</x:Double>, or the
// aliasing form <StaticResource x:Key="W" ResourceKey="Base"/> that Terminal's
// theme dictionaries are built out of.
//
// The entry's closing tag is consumed here rather than by the main loop, so
// that its content is read as the resource's literal instead of being offered
// to whatever element encloses the dictionary.
void AddResourceEntry(MarkupNode& owner, const Tag& tag, Scanner& scanner,
                      const ResourceScope& scope) {
    ResourceDictionary& dictionary = owner.resources;
    std::map<std::string, std::string> attributes = tag.attributes;
    const auto key_attribute = attributes.find("x:Key");
    const bool keyed = key_attribute != attributes.end();
    const std::string key = keyed ? key_attribute->second : std::string();
    attributes.erase("x:Key");

    // A Style is the one dictionary entry that may have no key at all: without
    // one it is implicit and is filed under the type it targets instead. Read
    // before the x:Key check below for exactly that reason.
    if (tag.name == "Style") {
        MarkupStyleHost host(scope);
        ScannerTags tags(scanner);
        std::shared_ptr<const Style> style =
            ParseStyle(attributes, tag.self_closing, tags, host, key);
        if (keyed) {
            ResourceValue entry;
            entry.type = "Style";
            entry.kind = ValueKind::Style;
            entry.style = style;
            dictionary.Add(key, std::move(entry));
        } else {
            // The type is read out before the pointer is handed over: the two
            // arguments are unsequenced, and a moved-from shared_ptr is null.
            const std::string target = style->target_type;
            owner.implicit_styles.Add(target, std::move(style));
        }
        return;
    }

    if (!keyed) throw MarkupError("<" + tag.name + "> in a resource dictionary needs an x:Key");

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
    if ((parent.type == "Border" || parent.type == "ContentPresenter") &&
        !parent.children.empty()) {
        throw MarkupError("a " + parent.type + " takes a single child");
    }
    if (parent.type == "ContentControl" && !parent.children.empty())
        throw MarkupError("a ContentControl takes a single piece of content");
    if (parent.type == "Path" || parent.type == "Image" || parent.type == "PathIcon")
        throw MarkupError("a " + parent.type + " takes no child elements");
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
    } else if (node.type == "Canvas") {
        auto canvas = std::make_unique<Canvas>();
        for (const MarkupNode& child : node.children) canvas->AddChild(BuildElement(child));
        element = std::move(canvas);
    } else if (node.type == "ContentPresenter") {
        auto presenter = std::make_unique<ContentPresenter>();
        if (!node.children.empty()) presenter->SetContent(BuildElement(node.children.front()));
        element = std::move(presenter);
    } else if (node.type == "Image") {
        element = std::make_unique<Image>();
    } else if (node.type == "Path") {
        auto path = std::make_unique<Path>();
        path->data = node.data;
        element = std::move(path);
    } else if (node.type == "PathIcon") {
        auto icon = std::make_unique<PathIcon>();
        icon->data = node.data;
        element = std::move(icon);
    } else if (node.type == "TextBlock") {
        auto text = std::make_unique<TextBlock>();
        if (!node.children.empty())
            throw MarkupError("a TextBlock takes text, not child elements");
        if (!node.text.empty()) text->set_text(node.text);
        element = std::move(text);
    } else {
        throw MarkupError("the type '" + node.type + "' is not implemented");
    }

    // The style first, then the local values. Not because the order decides
    // anything -- the two go into different slots and precedence is read, not
    // written -- but because it is the order the runtime applies them in, and
    // an implementation whose answer depended on the order would be one where
    // a later local value silently overwrote a style setter instead of
    // shadowing it.
    //
    // Both are set after the children are attached, so that an inherited value
    // flowing down from either reaches a subtree that is already there.
    if (node.style)
        ApplyStyle(*element, *node.style, node.type, OwnersFor(node.type));

    // Only what the markup wrote, which is not the same as every property the
    // element has. An inherited property left alone must stay unset so that it
    // reads its parent's value; assigning the default here instead would give
    // every TextBlock a local FontSize of 14 and nothing would ever inherit.
    for (const MarkupProperty& assignment : node.properties)
        element->SetValue(*assignment.property, assignment.value);
    return element;
}

}  // namespace

std::string FullTypeName(const std::string& short_name) {
    // The namespace is part of the answer, not decoration: it is what the
    // probe reports as the node's type and what the measured tree is keyed on,
    // and Shapes do not live where Controls do.
    static const std::map<std::string, std::string> kTypes = {
        {"Border", "Windows.UI.Xaml.Controls.Border"},
        {"Canvas", "Windows.UI.Xaml.Controls.Canvas"},
        {"ContentControl", "Windows.UI.Xaml.Controls.ContentControl"},
        {"ContentPresenter", "Windows.UI.Xaml.Controls.ContentPresenter"},
        {"Grid", "Windows.UI.Xaml.Controls.Grid"},
        {"Image", "Windows.UI.Xaml.Controls.Image"},
        {"Path", "Windows.UI.Xaml.Shapes.Path"},
        {"PathIcon", "Windows.UI.Xaml.Controls.PathIcon"},
        {"StackPanel", "Windows.UI.Xaml.Controls.StackPanel"},
        {"TextBlock", "Windows.UI.Xaml.Controls.TextBlock"},
    };
    const auto found = kTypes.find(short_name);
    if (found == kTypes.end())
        throw MarkupError("the type '" + short_name + "' is not implemented");
    return found->second;
}

MarkupNode ParseMarkup(const std::string& markup, const StringTable& strings) {
    Scanner scanner(markup);

    MarkupNode root;
    bool have_root = false;
    // Open elements, innermost last. The root is held separately so it can be
    // returned by value once the stack empties.
    std::vector<MarkupNode> open;
    // Which of those are deferred, in step with `open`. Alongside rather than
    // on the node, because deferral is a fact about the parse and not about the
    // tree: a deferred element never reaches the tree, so a field recording it
    // there would be false in every node that survives.
    std::vector<bool> deferred;
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

    // The same walk again, for implicit styles. Same order, same shape, and a
    // separate table because an implicit key is a type rather than a string --
    // see style.h.
    //
    // `self` is the element being finished, whose *own* dictionary is in scope
    // for it: `ResolveImplicitStyleKeyImpl` starts its walk at the element and
    // only then goes to the parent, so a Border whose dictionary holds an
    // implicit Border style is styled by it. That is the one place the
    // implicit route is wider than {StaticResource}'s, which cannot see a
    // dictionary declared below the attribute that reads it.
    auto implicit_scope = [&open](const MarkupNode& self) {
        ImplicitStyleScope chain;
        if (!self.implicit_styles.empty()) chain.push_back(&self.implicit_styles);
        for (auto it = open.rbegin(); it != open.rend(); ++it) {
            if (!it->implicit_styles.empty()) chain.push_back(&it->implicit_styles);
        }
        return chain;
    };

    // Called once an element has been read whole. An implicit style is looked
    // up here rather than when the start tag was scanned, because the
    // element's own dictionary is a child of it and does not exist yet at that
    // point -- and because an explicit Style= wins outright, so there is
    // nothing to look up when one was written.
    //
    // The runtime does this later still: `CFrameworkElement::ApplyStyle` runs
    // at CreationComplete and again on entering a live tree, which is after
    // the whole document is parsed. The difference shows only for a dictionary
    // written *below* the elements it targets, which is
    // `L5-styles-implicit-forward-dictionary` and is a question rather than a
    // decision.
    auto finish_node = [&](MarkupNode& node) {
        if (!node.style) node.style = FindImplicitStyle(implicit_scope(node), node.type);
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
            const bool was_deferred = deferred.back();
            deferred.pop_back();
            if (tag.name != finished.type)
                throw MarkupError("</" + tag.name + "> does not close the open element");
            // A deferred element is described and then dropped: it is not
            // attached, so it is measured by nothing and occupies no slot. Its
            // subtree went with it, having been attached to it. Nothing will
            // measure it, so it is not worth an implicit style lookup either.
            if (was_deferred) continue;
            finish_node(finished);
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

            // Attached, so it names the class that defines the property rather
            // than the element it is being set on, and the owner check below
            // does not apply to it.
            if (tag.name == "VisualStateManager.VisualStateGroups") {
                if (tag.self_closing) continue;
                ParseVisualStateGroups(scanner);
                continue;
            }

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
            } else if (member == "Background") {
                // A brush written as a property element rather than as an
                // attribute. Whether the type has the property is the
                // registry's question here too, so a <Canvas.Padding> is
                // refused with the same message a Padding="..." on a Canvas
                // gets. Not a Section::Value: a brush is an element with a
                // type of its own rather than a scalar, so the whole of it is
                // read here instead of being resolved to a literal.
                const DependencyProperty* found =
                    FindProperty(OwnersFor(open.back().type), member);
                if (!found) {
                    throw MarkupError("the property '" + member + "' was not found in type '" +
                                      FullTypeName(open.back().type) + "'");
                }
                if (tag.self_closing) throw MarkupError("<" + tag.name + "> is empty");
                const std::string brush = ParseBrushPropertyElement(scanner, tag.name);
                open.back().background = brush;
                open.back().properties.push_back(MarkupProperty{found, brush});
                continue;
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
            AddResourceEntry(open.back(), tag, scanner, scope());
            continue;
        }

        if (section == Section::Value) {
            if (value_filled)
                throw MarkupError("<" + property_element + "> was given a second value");
            const std::string member = property_element.substr(property_element.find('.') + 1);

            // A primitive written in place rather than looked up:
            // <Border.Width><x:Double>60</x:Double></Border.Width>. This is the
            // form Terminal reaches for when the property is typed `object` and
            // an attribute would give it a string -- <ToggleButton.Tag> with an
            // <x:Int32>, <DiscreteObjectKeyFrame.Value> with an <x:Boolean>.
            //
            // It goes through MakeResource, so the same shape check runs here
            // as on a {StaticResource} of the same primitive: an x:String does
            // not satisfy a Width either way. Then it is applied through the
            // ordinary attribute path, so the value reaches the property by the
            // code Width="60" would have used.
            if (IsResourceType(tag.name)) {
                if (!tag.attributes.empty()) {
                    throw MarkupError("<" + tag.name + "> as a value takes no attributes; '" +
                                      tag.attributes.begin()->first + "' is not implemented");
                }
                std::string content;
                if (!tag.self_closing) {
                    Tag close;
                    if (!scanner.Next(close) || !close.closing || close.name != tag.name)
                        throw MarkupError("<" + tag.name + "> holds only text");
                    content = close.text_before;
                }
                ApplyAttributes(open.back(),
                                {{member, ValueForProperty(MakeResource(tag.name, content),
                                                           member)}});
                value_filled = true;
                continue;
            }

            if (tag.name != "StaticResource") {
                throw MarkupError("<" + property_element + "> takes a <StaticResource> or a "
                                  "primitive such as <x:Double>, not <" + tag.name + ">");
            }
            if (!tag.self_closing) {
                Tag close;
                if (!scanner.Next(close) || !close.closing || close.name != "StaticResource")
                    throw MarkupError("<StaticResource> must be empty");
            }
            const std::string key = StaticResourceElementKey(tag.attributes, /*allow_key=*/false);
            // Resolved to its literal and then applied through the ordinary
            // attribute path, so <Border.Width><StaticResource .../></Border.Width>
            // and Width="60" reach the property by the same code.
            ApplyAttributes(open.back(), {{member, ResolveResource(scope(), key, member)}});
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

        // Directives come off first. They are not properties, so handing them
        // to the registry would report x:Load as a missing member of Border --
        // the wrong reason for the right refusal, and no reason at all for the
        // ones that are implemented.
        std::map<std::string, std::string> attributes = tag.attributes;
        const XDirectives directives = TakeXDirectives(attributes);
        ApplyUid(attributes, directives, strings);

        // Style comes off next, before the rest: it is the one attribute whose
        // value is an object rather than a literal, so the text-substitution
        // path every other attribute takes has nothing to hand it.
        const auto style_attribute = attributes.find("Style");
        if (style_attribute != attributes.end()) {
            node.style = ResolveStyleReference(scope(), style_attribute->second);
            attributes.erase(style_attribute);
        }
        ApplyAttributes(node, ResolveAttributes(attributes, scope()));

        // A deferred root would realise nothing at all, and a measurement of
        // nothing is not a measurement. Named here rather than left to produce
        // an empty tree that reads like a parser bug.
        if (directives.deferred && open.empty() && !have_root) {
            throw MarkupError("the root <" + tag.name +
                              "> defers its own creation, so this markup realises no element");
        }

        if (tag.self_closing) {
            if (directives.deferred) continue;
            finish_node(node);
            if (open.empty()) {
                root = std::move(node);
                have_root = true;
            } else {
                AttachChild(open.back(), std::move(node));
            }
        } else {
            open.push_back(std::move(node));
            deferred.push_back(directives.deferred);
        }
    }

    if (!open.empty()) throw MarkupError("markup ended with elements still open");
    if (!have_root) throw MarkupError("markup contains no root element");
    return root;
}

MarkupNode ParseMarkup(const std::string& markup) { return ParseMarkup(markup, NoStrings()); }

std::unique_ptr<Element> LoadMarkup(const std::string& markup) {
    return BuildElement(ParseMarkup(markup, NoStrings()));
}

std::unique_ptr<Element> LoadMarkup(const std::string& markup, const StringTable& strings) {
    return BuildElement(ParseMarkup(markup, strings));
}

}  // namespace openxaml
