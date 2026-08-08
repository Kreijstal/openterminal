#include "markup.h"

#include "markup_tree.h"

#include <cctype>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <vector>

// The default-style database arrives as JSON, the way the font metrics and the
// theme resources do.
#include "json.h"

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
#include "advanced_controls.h"
#include "basic_controls.h"
#include "default_styles.h"
#include "scroll_viewer.h"

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
    if (type == "FontIcon") return FontIcon::Owners();
    if (type == "Rectangle") return Rectangle::Owners();
    if (type == "ScrollViewer") return ScrollViewer::Owners();
    if (type == "Button") return Button::Owners();
    if (type == "TextBox") return TextBox::Owners();
    if (type == "ToolTip") return ToolTip::Owners();
    if (type == "Thumb") return Thumb::Owners();
    // Run is an inline, not a FrameworkElement. It is folded into its owning
    // TextBlock before realization, so the TextBlock text-property surface is
    // exactly the useful portion of its markup surface here.
    if (type == "Run") return TextBlock::Owners();
    if (type == "StackPanel") return StackPanel::Owners();
    if (type == "TextBlock") return TextBlock::Owners();
    // A type identity a Style may target and no element tag can name. It is
    // here and not in FullTypeName's table because the two answer different
    // questions: this is the property registry, which a <Setter Property="..."/>
    // is resolved against, and that is the set of elements this parser builds.
    static const std::vector<std::string> kControlOwners = {"Control", kTextPropertyOwner,
                                                            "FrameworkElement", "UIElement"};
    if (type == "Control") return kControlOwners;
    if (type == "Page") return Page::Owners();
    if (type == "Frame") return Frame::Owners();
    if (type == "ItemsControl") return ItemsControl::Owners();
    if (type == "ListView") return ListView::Owners();
    if (type == "Popup") return Popup::Owners();
    // All muxc controls currently share the ContentControl property surface;
    // their generic.xaml templates distinguish them after load.
    static const std::vector<std::string> kMuxOwners = {
        "ContentControl", "Control", kTextPropertyOwner, "FrameworkElement", "UIElement"};
    static const std::vector<std::string> kMuxTypes = {
        "BreadcrumbBar", "ColorPicker", "DropDownButton", "Expander", "InfoBadge", "InfoBar",
        "NavigationView", "NavigationViewItem", "NumberBox", "ProgressRing", "SplitButton",
        "TabView", "TeachingTip", "TreeView"};
    if (std::find(kMuxTypes.begin(), kMuxTypes.end(), type) != kMuxTypes.end()) return kMuxOwners;
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
    if (name == "RenderTransformOrigin") return assign(value);
    if (name == "Control.IsTemplateFocusTarget") return assign(ParseBool(value, name));
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
    // always a SolidColorBrush. The property store still receives the type
    // name and not the colour -- that is what the corpus and the twin checks
    // have always seen -- while the colour itself travels on the node for the
    // render pass. A typo still fails here rather than loading silently.
    if (name == "Background") {
        node.background_brush = BrushValue{true, true, ParseColor(value, name)};
        return assign(node.background = "SolidColorBrush");
    }
    if (name == "Fill") {
        node.fill_brush = BrushValue{true, true, ParseColor(value, name)};
        return assign(std::string("SolidColorBrush"));
    }
    // No BorderBrush here on purpose. No type registers one, so the three
    // corpus cases that set it fail at load naming the property -- which is
    // what the oracle answers for them too. The render pass can paint a border
    // brush (see phase3/render) and no markup can currently give it one; when
    // the property is registered, this is where its colour joins the node.
    if (name == "RadiusX" || name == "RadiusY") return assign(ParseDouble(value, name));
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
    if (name == "Glyph") return assign(node.glyph = value);
    if (name == "HorizontalScrollBarVisibility" || name == "VerticalScrollBarVisibility") {
        static const std::map<std::string, ScrollBarVisibility> names = {
            {"Disabled", ScrollBarVisibility::Disabled}, {"Auto", ScrollBarVisibility::Auto},
            {"Hidden", ScrollBarVisibility::Hidden}, {"Visible", ScrollBarVisibility::Visible}};
        return assign(static_cast<int>(ParseEnum(value, names, name)));
    }
    if (name == "HorizontalScrollMode" || name == "VerticalScrollMode") {
        static const std::map<std::string, ScrollMode> names = {
            {"Disabled", ScrollMode::Disabled}, {"Enabled", ScrollMode::Enabled},
            {"Auto", ScrollMode::Auto}};
        return assign(static_cast<int>(ParseEnum(value, names, name)));
    }
    if (name == "BringIntoViewOnFocusChange" ||
        name == "IsVerticalScrollChainingEnabled" || name == "AcceptsReturn" ||
        name == "IsReadOnly" || name == "IsSpellCheckEnabled" ||
        name == "MirroredWhenRightToLeft")
        return assign(ParseBool(value, name));
    if (name == "MaxLength") return assign(ParseInt(value, name));
    if (name == "PlaceholderText" || name == "FontWeight" || name == "Placement")
        return assign(value);
    // A brush, carried as the text that spells it. Nothing here paints, so the
    // value is stored and never interpreted -- which is honest about what it
    // is: enough to show the property system carried it, and not a colour.
    if (name == "Foreground") return assign(node.foreground = value);
    // The content property, so it can arrive as an attribute or as character
    // data between the tags. Both land in node.text, and BuildElement gives
    // the element whatever is there once the parse is done.
    //
    // Which of the two it was is remembered, because the runtime measures them
    // differently: content becomes an implicit Run in the Inlines collection
    // and the property does not, and the two are not the same arithmetic. See
    // rule 7 in text.cpp.
    if (name == "Text") {
        node.text = value;
        node.text_from_property = true;
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

void ApplyNodeAttributes(MarkupNode& node,
                         const std::map<std::string, std::string>& attributes,
                         const ResourceScope& scope) {
    for (const auto& [name, raw] : attributes) {
        if (name == "Name") {
            if (raw.empty()) throw MarkupError("Name cannot be empty");
            node.name = raw;
            continue;
        }
        MarkupExtension extension;
        if (TryParseMarkupExtension(raw, extension) &&
            (extension.name == "Binding" || extension.name == "x:Bind")) {
            const DependencyProperty* property = FindProperty(OwnersFor(node.type), name);
            if (!property) {
                throw MarkupError("the property '" + name + "' was not found in type '" +
                                  FullTypeName(node.type) + "'");
            }
            try {
                Binding binding = ParseBindingMarkup(extension.name, extension.argument);
                if (binding.fallback_value) {
                    if (const std::string* literal =
                            std::get_if<std::string>(&*binding.fallback_value)) {
                        MarkupNode fallback;
                        ApplyProperty(fallback, *property, *literal);
                        if (fallback.properties.size() != 1)
                            throw BindingError("FallbackValue cannot supply '" + name + "'");
                        binding.fallback_value = fallback.properties.front().value;
                    }
                }
                node.bindings.push_back({property, std::move(binding)});
            } catch (const BindingError& error) {
                throw MarkupError(error.what());
            }
            continue;
        }
        ApplyAttributes(node, {{name, ResolveAttributeValue(scope, name, raw)}});
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
        //
        // An abstract base has only the second answer, and needs only the
        // second: `L5-styles-explicit-derived-target` records a
        // TargetType="Control" style applied to a ContentControl, so the type
        // is a legal target even though no <Control> can be built. Asking the
        // element registry about it would refuse the style for a fact about
        // tags rather than about types.
        if (type != "Control") FullTypeName(type);
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

std::string RequiredName(const Tag& tag) {
    auto found = tag.attributes.find("x:Name");
    if (found == tag.attributes.end()) found = tag.attributes.find("Name");
    if (found == tag.attributes.end() || found->second.empty())
        throw MarkupError("<" + tag.name + "> needs x:Name");
    return found->second;
}

std::string NormalizeTargetProperty(std::string property) {
    if (property.size() >= 2 && property.front() == '(' && property.back() == ')')
        property = property.substr(1, property.size() - 2);
    const size_t dot = property.find_last_of('.');
    return dot == std::string::npos ? property : property.substr(dot + 1);
}

double ParseDurationSeconds(const std::string& text) {
    if (text.empty() || text == "0") return 0.0;
    const std::vector<std::string> fields = Split(text, ':');
    if (fields.size() == 1) return ParseDouble(fields[0], "Duration");
    if (fields.size() != 3) throw MarkupError("cannot read \"" + text + "\" as a Duration");
    return ParseDouble(fields[0], "Duration") * 3600.0 +
           ParseDouble(fields[1], "Duration") * 60.0 +
           ParseDouble(fields[2], "Duration");
}

// Reads setters and endpoint-sampled DoubleAnimations. State entry is explicit
// through Element::visual_state_manager(), exactly as VisualStateManager.GoToState
// is in WinUI; loading markup alone does not guess an initial state.
void ParseVisualStateGroups(Scanner& scanner, MarkupNode& owner) {
    const std::string property_element = "VisualStateManager.VisualStateGroups";
    MarkupVisualStateGroup* group = nullptr;
    MarkupVisualState* state = nullptr;
    bool setters_open = false;
    bool storyboard_open = false;
    Tag tag;
    while (scanner.Next(tag)) {
        if (tag.text_before.find_first_not_of(" \t\r\n") != std::string::npos)
            throw MarkupError("unexpected text content in <" + property_element + ">");
        if (tag.closing) {
            if (tag.name == "VisualState.Setters" && setters_open) {
                setters_open = false;
            } else if (tag.name == "Storyboard" && storyboard_open) {
                storyboard_open = false;
            } else if (tag.name == "VisualState" && state && !setters_open && !storyboard_open) {
                state = nullptr;
            } else if (tag.name == "VisualStateGroup" && group && !state) {
                group = nullptr;
            } else if (tag.name == property_element && !group && !state &&
                       !setters_open && !storyboard_open) {
                return;
            } else {
                throw MarkupError("</" + tag.name + "> does not close the open visual-state element");
            }
            continue;
        }

        if (!group && tag.name == "VisualStateGroup") {
            owner.visual_state_groups.push_back({RequiredName(tag), {}});
            group = &owner.visual_state_groups.back();
            if (tag.self_closing) group = nullptr;
            continue;
        }
        if (group && !state && tag.name == "VisualState") {
            group->states.push_back({RequiredName(tag), {}, {}});
            state = &group->states.back();
            if (tag.self_closing) state = nullptr;
            continue;
        }
        if (state && tag.name == "VisualState.Setters" && !setters_open && !storyboard_open) {
            setters_open = !tag.self_closing;
            continue;
        }
        if (state && tag.name == "Storyboard" && !setters_open && !storyboard_open) {
            storyboard_open = !tag.self_closing;
            continue;
        }
        if (state && setters_open && tag.name == "Setter") {
            if (!tag.self_closing) throw MarkupError("a VisualState Setter must be empty");
            std::string target;
            std::string property;
            std::string value;
            for (const auto& [name, raw] : tag.attributes) {
                if (name == "Target") {
                    const size_t dot = raw.find_last_of('.');
                    if (dot == std::string::npos)
                        throw MarkupError("a VisualState Setter Target needs a target and property");
                    target = raw.substr(0, dot);
                    property = NormalizeTargetProperty(raw.substr(dot + 1));
                } else if (name == "Property") {
                    property = NormalizeTargetProperty(raw);
                } else if (name == "Value") {
                    value = raw;
                } else {
                    throw MarkupError("a VisualState Setter does not take '" + name + "'");
                }
            }
            if (property.empty() || value.empty())
                throw MarkupError("a VisualState Setter needs a property and value");
            state->setters.push_back({target, property, value});
            continue;
        }
        if (state && storyboard_open && tag.name == "DoubleAnimation") {
            if (!tag.self_closing) throw MarkupError("a DoubleAnimation must be empty here");
            MarkupTimeline timeline;
            for (const auto& [name, raw] : tag.attributes) {
                if (name == "Storyboard.TargetName") timeline.target_name = raw;
                else if (name == "Storyboard.TargetProperty")
                    timeline.property = NormalizeTargetProperty(raw);
                else if (name == "From") { timeline.from = raw; timeline.has_from = true; }
                else if (name == "To") timeline.to = raw;
                else if (name == "Duration") timeline.duration_seconds = ParseDurationSeconds(raw);
                else if (name == "EnableDependentAnimation") {
                    (void)ParseBool(raw, name);
                } else {
                    throw MarkupError("a DoubleAnimation does not take '" + name + "'");
                }
            }
            if (timeline.property.empty() || timeline.to.empty())
                throw MarkupError("a DoubleAnimation needs TargetProperty and To");
            state->timelines.push_back(std::move(timeline));
            continue;
        }
        throw MarkupError("the visual state element '" + tag.name + "' is not implemented");
    }
    throw MarkupError("<" + property_element + "> was not closed");
}

// --- resources ----------------------------------------------------------------

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

    if (tag.name == "StaticResource" || tag.name == "ThemeResource") {
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

void SkipPropertySubtree(Scanner& scanner, const std::string& property) {
    int depth = 1;
    Tag nested;
    while (depth && scanner.Next(nested)) {
        if (nested.closing) {
            --depth;
            if (depth == 0 && nested.name != property)
                throw MarkupError("</" + nested.name + "> closes <" + property + ">");
        } else if (!nested.self_closing) {
            ++depth;
        }
    }
    if (depth) throw MarkupError("<" + property + "> was not closed");
}

// The Transform types Windows.UI.Xaml has. Named rather than assumed, so that
// a misspelt one is refused instead of being waved through as layout-inert.
bool IsTransformType(const std::string& name) {
    return name == "CompositeTransform" || name == "MatrixTransform" ||
           name == "RotateTransform" || name == "ScaleTransform" || name == "SkewTransform" ||
           name == "TranslateTransform" || name == "TransformGroup";
}

// <Rectangle.RenderTransform><CompositeTransform/></Rectangle.RenderTransform>.
//
// A transform is applied to the element's visual once layout has already
// decided the element's size and position, so it reaches neither MeasureOverride
// nor ArrangeOverride: L7-terminal-65dec6afa8 carries one and records exactly
// what the same Rectangle without one records. So the transform is read for its
// type and then dropped -- there is nothing here to store it in, and storing it
// would suggest something reads it.
void ReadTransformPropertyElement(Scanner& scanner, const std::string& property) {
    Tag transform;
    if (!scanner.Next(transform) || transform.closing)
        throw MarkupError("<" + property + "> was given no transform");
    if (!IsTransformType(transform.name)) {
        throw MarkupError("<" + property + "> takes a transform, not <" + transform.name +
                          ">");
    }
    if (!transform.self_closing) SkipPropertySubtree(scanner, transform.name);
    Tag close;
    if (!scanner.Next(close) || !close.closing || close.name != property)
        throw MarkupError("<" + property + "> holds one transform");
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
    if (parent.type == "TextBlock" && child.type == "Run") {
        parent.text += child.text;
        return;
    }
    if ((parent.type == "Border" || parent.type == "ContentPresenter") &&
        !parent.children.empty()) {
        throw MarkupError("a " + parent.type + " takes a single child");
    }
    if ((parent.type == "ContentControl" || parent.type == "Page" || parent.type == "Frame" ||
         parent.type == "Button" || parent.type == "ToolTip" || parent.type == "ScrollViewer" ||
         parent.type == "Popup" || parent.type == "BreadcrumbBar" ||
         parent.type == "ColorPicker" || parent.type == "DropDownButton" ||
         parent.type == "Expander" || parent.type == "InfoBadge" || parent.type == "InfoBar" ||
         parent.type == "NavigationView" || parent.type == "NavigationViewItem" ||
         parent.type == "NumberBox" || parent.type == "ProgressRing" ||
         parent.type == "SplitButton" || parent.type == "TabView" ||
         parent.type == "TeachingTip" || parent.type == "TreeView") &&
        !parent.children.empty())
        throw MarkupError("a " + parent.type + " takes a single piece of content");
    if (parent.type == "Path" || parent.type == "Image" || parent.type == "PathIcon" ||
        parent.type == "FontIcon" || parent.type == "Rectangle" || parent.type == "TextBox" ||
        parent.type == "Run")
        throw MarkupError("a " + parent.type + " takes no child elements");
    parent.children.push_back(std::move(child));
}

// Gives `node` and everything already below it the implicit style its type
// resolves to. `outer` is the tables in scope above `node`, innermost first.
//
// This runs when a <X.Resources> is attached and nowhere else, which is what
// decides how far an implicit style reaches. The recorded answers are what say
// so: `L5-styles-implicit-target-type` declares one on a StackPanel and writes
// two Borders under it, and both are recorded unstyled, while
// `L5-styles-implicit-forward-dictionary` writes the Border first and the
// dictionary below it, and that Border *is* styled. So the reach is not "the
// subtree below the dictionary" -- it is whatever is in the tree at the moment
// the dictionary arrives, which for a XAML parse is the owner plus everything
// written above the <X.Resources>. `L5-styles-implicit-own-dictionary` pins the
// owner half: the Border holding the dictionary is styled by it and the Border
// written below it is not.
//
// In the runtime this is `CFrameworkElement`'s resources-changed notification
// invalidating the implicit style of the subtree it is set on. Nothing runs it
// again afterwards -- there is no live tree here to enter, and the recorded
// zeroes say no second pass happened.
//
// A node that already has a style keeps it. An explicit Style= takes the slot
// whole, and an implicit style that got there first came from a nearer
// dictionary: a nested <X.Resources> is always attached before the enclosing
// one, so first-writer-wins is innermost-wins.
void ApplyImplicitStyles(MarkupNode& node, ImplicitStyleScope outer) {
    if (!node.implicit_styles.empty()) outer.insert(outer.begin(), &node.implicit_styles);
    if (!node.style) node.style = FindImplicitStyle(outer, node.type);
    for (MarkupNode& child : node.children) ApplyImplicitStyles(child, outer);
}

// --- realising the description ------------------------------------------------

Definition ToDefinition(const MarkupDefinition& source) {
    Definition definition;
    definition.user_size = source.size;
    definition.user_min_size = source.min_size;
    definition.user_max_size = source.max_size;
    return definition;
}

std::unique_ptr<Element> BuildElement(const MarkupNode& node, ObservableObject* binding_source,
                                      const std::shared_ptr<NameScope>& namescope) {
    std::unique_ptr<Element> element;
    if (node.type == "Border") {
        auto border = std::make_unique<Border>();
        if (!node.children.empty()) border->SetChild(BuildElement(node.children.front(), binding_source, namescope));
        element = std::move(border);
    } else if (node.type == "ContentControl") {
        auto control = std::make_unique<ContentControl>();
        if (!node.children.empty()) control->SetContent(BuildElement(node.children.front(), binding_source, namescope));
        element = std::move(control);
    } else if (node.type == "Grid") {
        auto grid = std::make_unique<Grid>();
        for (const MarkupDefinition& definition : node.column_definitions)
            grid->column_definitions.push_back(ToDefinition(definition));
        for (const MarkupDefinition& definition : node.row_definitions)
            grid->row_definitions.push_back(ToDefinition(definition));
        for (const MarkupNode& child : node.children) grid->AddChild(BuildElement(child, binding_source, namescope));
        element = std::move(grid);
    } else if (node.type == "StackPanel") {
        auto stack = std::make_unique<StackPanel>();
        for (const MarkupNode& child : node.children) stack->AddChild(BuildElement(child, binding_source, namescope));
        element = std::move(stack);
    } else if (node.type == "Canvas") {
        auto canvas = std::make_unique<Canvas>();
        for (const MarkupNode& child : node.children) canvas->AddChild(BuildElement(child, binding_source, namescope));
        element = std::move(canvas);
    } else if (node.type == "ContentPresenter") {
        auto presenter = std::make_unique<ContentPresenter>();
        if (!node.children.empty()) presenter->SetContent(BuildElement(node.children.front(), binding_source, namescope));
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
    } else if (node.type == "FontIcon") {
        auto icon = std::make_unique<FontIcon>();
        icon->set_glyph(node.glyph);
        element = std::move(icon);
    } else if (node.type == "Rectangle") {
        element = std::make_unique<Rectangle>();
    } else if (node.type == "TextBlock") {
        auto text = std::make_unique<TextBlock>();
        if (!node.children.empty())
            throw MarkupError("a TextBlock takes text, not child elements");
        if (!node.text.empty()) text->set_text(node.text);
        // Which of the two spellings the markup used, because the runtime
        // measures them differently -- see rule 7 in text.cpp.
        text->set_text_from_property(node.text_from_property);
        element = std::move(text);
    } else if (node.type == "Page") {
        auto page = std::make_unique<Page>();
        if (!node.children.empty()) page->SetContent(BuildElement(node.children.front(), binding_source, namescope));
        element = std::move(page);
    } else if (node.type == "Frame") {
        auto frame = std::make_unique<Frame>();
        if (!node.children.empty()) frame->SetContent(BuildElement(node.children.front(), binding_source, namescope));
        element = std::move(frame);
    } else if (node.type == "ItemsControl" || node.type == "ListView") {
        std::unique_ptr<ItemsControl> items = node.type == "ListView"
            ? std::unique_ptr<ItemsControl>(std::make_unique<ListView>())
            : std::make_unique<ItemsControl>();
        const std::vector<MarkupNode> children = node.children;
        items->SetItems(children.size(), [children, binding_source, namescope](size_t index) {
            return BuildElement(children.at(index), binding_source, namescope);
        });
        element = std::move(items);
    } else if (node.type == "Popup") {
        auto popup = std::make_unique<Popup>();
        if (!node.children.empty()) popup->SetContent(BuildElement(node.children.front(), binding_source, namescope));
        element = std::move(popup);
    } else if (node.type == "ScrollViewer") {
        auto viewer = std::make_unique<ScrollViewer>();
        if (!node.children.empty())
            viewer->SetContent(BuildElement(node.children.front(), binding_source, namescope));
        element = std::move(viewer);
    } else if (node.type == "Button") {
        auto button = std::make_unique<Button>();
        if (!node.children.empty()) {
            button->SetContent(BuildElement(node.children.front(), binding_source, namescope));
        } else if (!node.text.empty()) {
            // Not a TextBlock. The runtime sets Content to the string itself,
            // and L7-terminal-0e66f8e18d records what that is worth: no node
            // under the Button, and a Button 20 wide around a word that is 41
            // wide on its own.
            button->set_content_text(node.text);
        }
        element = std::move(button);
    } else if (node.type == "TextBox") {
        auto box = std::make_unique<TextBox>();
        box->set_text(node.text);
        element = std::move(box);
    } else if (node.type == "ToolTip") {
        auto tip = std::make_unique<ToolTip>();
        if (!node.children.empty())
            tip->SetContent(BuildElement(node.children.front(), binding_source, namescope));
        element = std::move(tip);
    } else if (node.type == "Thumb") {
        auto thumb = std::make_unique<Thumb>();
        if (!node.children.empty()) {
            const MarkupNode template_root = node.children.front();
            thumb->SetTemplate(std::make_shared<ControlTemplate>("Thumb",
                [template_root, binding_source, namescope](Control&) {
                    return BuildElement(template_root, binding_source, namescope);
                }));
            thumb->ApplyTemplate();
        }
        element = std::move(thumb);
    } else if (node.type == "BreadcrumbBar") {
        element = std::make_unique<BreadcrumbBar>();
    } else if (node.type == "ColorPicker") {
        element = std::make_unique<ColorPicker>();
    } else if (node.type == "DropDownButton") {
        element = std::make_unique<DropDownButton>();
    } else if (node.type == "Expander") {
        element = std::make_unique<Expander>();
    } else if (node.type == "InfoBadge") {
        element = std::make_unique<InfoBadge>();
    } else if (node.type == "InfoBar") {
        element = std::make_unique<InfoBar>();
    } else if (node.type == "NavigationView") {
        element = std::make_unique<NavigationView>();
    } else if (node.type == "NavigationViewItem") {
        element = std::make_unique<NavigationViewItem>();
    } else if (node.type == "NumberBox") {
        element = std::make_unique<NumberBox>();
    } else if (node.type == "ProgressRing") {
        element = std::make_unique<ProgressRing>();
    } else if (node.type == "SplitButton") {
        element = std::make_unique<SplitButton>();
    } else if (node.type == "TabView") {
        element = std::make_unique<TabView>();
    } else if (node.type == "TeachingTip") {
        element = std::make_unique<TeachingTip>();
    } else if (node.type == "TreeView") {
        element = std::make_unique<TreeView>();
    } else {
        throw MarkupError("the type '" + node.type + "' is not implemented");
    }

    if (!node.children.empty()) {
        if (auto* mux = dynamic_cast<MuxContentControl*>(element.get()))
            mux->SetContent(BuildElement(node.children.front(), binding_source, namescope));
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
    // The framework's own style, below both. `CControl::ApplyBuiltInStyle` is
    // the only thing that writes this layer and it is a Control's method, so
    // the cast is the rule rather than a convenience: a Border has no built-in
    // style in the runtime and must not grow one here.
    //
    // Registered only when the reconstructed database has been loaded, which a
    // bare checkout has not done -- so this is inert unless something asked
    // for it, exactly as the theme dictionary is.
    if (auto* control = dynamic_cast<Control*>(element.get()))
        (void)DefaultStyleRegistry::Default().Apply(*control, node.type, OwnersFor(node.type));

    if (node.style)
        ApplyStyle(*element, *node.style, node.type, OwnersFor(node.type));

    // The brushes, which go beside the property store rather than into it --
    // see brush.h. Nothing in layout reads them; the render pass does.
    element->set_background_brush(node.background_brush);
    element->set_border_brush(node.border_brush);
    element->set_fill_brush(node.fill_brush);

    // Only what the markup wrote, which is not the same as every property the
    // element has. An inherited property left alone must stay unset so that it
    // reads its parent's value; assigning the default here instead would give
    // every TextBlock a local FontSize of 14 and nothing would ever inherit.
    for (const MarkupProperty& assignment : node.properties)
        element->SetValue(*assignment.property, assignment.value);
    for (const MarkupBinding& assignment : node.bindings) {
        if (!binding_source) {
            throw MarkupError("the binding path '" + assignment.binding.path +
                              "' has no data source");
        }
        try {
            element->KeepBinding(std::make_unique<BindingExpression>(
                *element, *assignment.property, *binding_source, assignment.binding));
        } catch (const BindingError& error) {
            throw MarkupError(error.what());
        }
    }

    if (!node.name.empty()) namescope->Register(node.name, *element);
    if (!node.visual_state_groups.empty()) {
        auto manager = std::make_unique<VisualStateManager>(*element, *namescope);
        for (const MarkupVisualStateGroup& described_group : node.visual_state_groups) {
            VisualStateGroup group(described_group.name);
            for (const MarkupVisualState& described_state : described_group.states) {
                VisualState state;
                state.name = described_state.name;
                for (const MarkupVisualSetter& described : described_state.setters) {
                    DependencyObject* target = described.target_name.empty()
                        ? static_cast<DependencyObject*>(element.get())
                        : namescope->Find(described.target_name);
                    if (!target)
                        throw MarkupError("the visual-state target '" + described.target_name +
                                          "' was not found");
                    const DependencyProperty* property =
                        FindProperty(target->PropertyOwners(), described.property);
                    if (!property)
                        throw MarkupError("the visual-state property '" + described.property +
                                          "' was not found on its target");
                    MarkupNode scratch;
                    ApplyProperty(scratch, *property, described.value);
                    if (scratch.properties.size() != 1)
                        throw MarkupError("the visual-state property '" + described.property +
                                          "' cannot be animated by this runtime");
                    state.setters.push_back({described.target_name, property,
                                             scratch.properties.front().value});
                }
                for (const MarkupTimeline& described : described_state.timelines) {
                    DependencyObject* target = described.target_name.empty()
                        ? static_cast<DependencyObject*>(element.get())
                        : namescope->Find(described.target_name);
                    if (!target)
                        throw MarkupError("the storyboard target '" + described.target_name +
                                          "' was not found");
                    const DependencyProperty* property =
                        FindProperty(target->PropertyOwners(), described.property);
                    if (!property)
                        throw MarkupError("the storyboard property '" + described.property +
                                          "' was not found on its target");
                    Timeline timeline;
                    timeline.target_name = described.target_name;
                    timeline.target_property = property;
                    timeline.duration_seconds = described.duration_seconds;
                    MarkupNode to;
                    ApplyProperty(to, *property, described.to);
                    if (to.properties.size() != 1)
                        throw MarkupError("the storyboard property '" + described.property +
                                          "' cannot be animated by this runtime");
                    timeline.to = to.properties.front().value;
                    if (described.has_from) {
                        MarkupNode from;
                        ApplyProperty(from, *property, described.from);
                        if (from.properties.size() != 1)
                            throw MarkupError("the storyboard From value cannot be converted");
                        timeline.from = from.properties.front().value;
                    }
                    state.storyboard.timelines.push_back(std::move(timeline));
                }
                group.Add(std::move(state));
            }
            manager->AddGroup(std::move(group));
        }
        element->KeepVisualStateManager(namescope, std::move(manager));
    }
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
        {"FontIcon", "Windows.UI.Xaml.Controls.FontIcon"},
        {"Rectangle", "Windows.UI.Xaml.Shapes.Rectangle"},
        {"ScrollViewer", "Windows.UI.Xaml.Controls.ScrollViewer"},
        {"Button", "Windows.UI.Xaml.Controls.Button"},
        {"TextBox", "Windows.UI.Xaml.Controls.TextBox"},
        {"ToolTip", "Windows.UI.Xaml.Controls.ToolTip"},
        {"Thumb", "Windows.UI.Xaml.Controls.Primitives.Thumb"},
        {"Run", "Windows.UI.Xaml.Documents.Run"},
        {"StackPanel", "Windows.UI.Xaml.Controls.StackPanel"},
        {"TextBlock", "Windows.UI.Xaml.Controls.TextBlock"},
        {"Page", "Windows.UI.Xaml.Controls.Page"},
        {"Frame", "Windows.UI.Xaml.Controls.Frame"},
        {"ItemsControl", "Windows.UI.Xaml.Controls.ItemsControl"},
        {"ListView", "Windows.UI.Xaml.Controls.ListView"},
        {"Popup", "Windows.UI.Xaml.Controls.Primitives.Popup"},
        {"BreadcrumbBar", "Microsoft.UI.Xaml.Controls.BreadcrumbBar"},
        {"ColorPicker", "Microsoft.UI.Xaml.Controls.ColorPicker"},
        {"DropDownButton", "Microsoft.UI.Xaml.Controls.DropDownButton"},
        {"Expander", "Microsoft.UI.Xaml.Controls.Expander"},
        {"InfoBadge", "Microsoft.UI.Xaml.Controls.InfoBadge"},
        {"InfoBar", "Microsoft.UI.Xaml.Controls.InfoBar"},
        {"NavigationView", "Microsoft.UI.Xaml.Controls.NavigationView"},
        {"NavigationViewItem", "Microsoft.UI.Xaml.Controls.NavigationViewItem"},
        {"NumberBox", "Microsoft.UI.Xaml.Controls.NumberBox"},
        {"ProgressRing", "Microsoft.UI.Xaml.Controls.ProgressRing"},
        {"SplitButton", "Microsoft.UI.Xaml.Controls.SplitButton"},
        {"TabView", "Microsoft.UI.Xaml.Controls.TabView"},
        {"TeachingTip", "Microsoft.UI.Xaml.Controls.TeachingTip"},
        {"TreeView", "Microsoft.UI.Xaml.Controls.TreeView"},
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
    // The property element currently being filled, e.g. Grid.ColumnDefinitions.
    // Empty when ordinary child elements are expected.
    std::string property_element;
    // What that property element expects to be filled with. The three kinds
    // read their contents differently enough that the name alone is not enough
    // to dispatch on.
    enum class Section { None, Definitions, Resources, Value, Template };
    Section section = Section::None;
    // An explicit <ResourceDictionary> wrapper inside a Resources section. The
    // wrapper is optional in XAML and carries nothing this parser reads, so it
    // is tracked only far enough to match its closing tag.
    bool dictionary_open = false;
    // Whether a <X.Property> element has already been given its one value.
    bool value_filled = false;
    bool control_template_open = false;

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
    // The chain ends at the application dictionary, which is where WinUI's own
    // theme resources live. It is loaded from the extracted database if there
    // is one and is absent otherwise, so a bare checkout behaves exactly as it
    // did before -- one dictionary shorter, and failing the same lookups by
    // the same names.
    auto scope = [&open]() {
        ResourceScope chain;
        for (auto it = open.rbegin(); it != open.rend(); ++it) {
            if (!it->resources.empty()) chain.push_back(&it->resources);
        }
        // Every layer of the application dictionary, highest first --
        // XamlControlsResources over the framework's own generic.xaml. A key
        // WinUI 2 redefines answers from WinUI 2; the three `SystemControl*`
        // brushes it leaves alone fall through to the framework's.
        for (const ResourceDictionary* layer : ThemeResourceLibrary::Default().ActiveLayers())
            chain.push_back(layer);
        return chain;
    };

    // The tables above the element whose dictionary has just been attached.
    // `open.back()` is that owner and supplies its own table in
    // ApplyImplicitStyles, so the chain here starts one level up.
    auto ancestor_implicit_scope = [&open]() {
        ImplicitStyleScope chain;
        for (auto it = open.rbegin() + 1; it != open.rend(); ++it) {
            if (!it->implicit_styles.empty()) chain.push_back(&it->implicit_styles);
        }
        return chain;
    };

    // Character data belongs to the element it sits inside, and only a
    // TextBlock has anywhere to put it. Everywhere else the old rule stands:
    // reject it rather than drop it silently.
    auto take_text = [&](const Tag& scanned) {
        if (scanned.text_before.empty()) return;
        if (!open.empty() && (open.back().type == "TextBlock" || open.back().type == "Run" ||
                              open.back().type == "Button")) {
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
            if (control_template_open && tag.name == "ControlTemplate") {
                control_template_open = false;
                continue;
            }
            if (dictionary_open && tag.name == "ResourceDictionary") {
                dictionary_open = false;
                continue;
            }
            if (!control_template_open && !property_element.empty()) {
                if (tag.name != property_element)
                    throw MarkupError("</" + tag.name + "> closes <" + property_element + ">");
                if (section == Section::Value && !value_filled)
                    throw MarkupError("<" + property_element + "> was given no value");
                // Attaching the dictionary is what applies its implicit styles;
                // see ApplyImplicitStyles.
                if (section == Section::Resources)
                    ApplyImplicitStyles(open.back(), ancestor_implicit_scope());
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

            // Attached, so it names the class that defines the property rather
            // than the element it is being set on, and the owner check below
            // does not apply to it.
            if (tag.name == "VisualStateManager.VisualStateGroups") {
                if (tag.self_closing) continue;
                ParseVisualStateGroups(scanner, open.back());
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
            } else if (member == "ContentTransitions") {
                if (!tag.self_closing) SkipPropertySubtree(scanner, tag.name);
                continue;
            } else if (member == "RenderTransform") {
                if (tag.self_closing)
                    throw MarkupError("<" + tag.name + "> was given no value");
                ReadTransformPropertyElement(scanner, tag.name);
                continue;
            } else if (member == "Template") {
                section = Section::Template;
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
                // Declared, and colourless: the property-element form carries
                // the colour on the brush's own Color attribute, which that
                // parser drops along with everything else inside the brush.
                // The render pass turns this into a named no-draw rather than
                // painting a colour nothing supplied.
                open.back().background_brush = BrushValue{true, false, Color{}};
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

        if (section == Section::Template && tag.name == "ControlTemplate") {
            const auto target = tag.attributes.find("TargetType");
            if (target != tag.attributes.end() && target->second != open.back().type)
                throw MarkupError("the ControlTemplate target does not match <" +
                                  open.back().type + ">");
            if (tag.self_closing) {
                value_filled = true;
            } else {
                control_template_open = true;
            }
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

            // What is left is the element spelling of the two lookup
            // extensions. They resolve identically here for the reason
            // ResolveAttributeValue gives, so both are accepted and the
            // refusal names the third case: an element that is neither a
            // lookup nor a primitive.
            if (tag.name != "StaticResource" && tag.name != "ThemeResource") {
                throw MarkupError("<" + property_element + "> takes a <StaticResource> or a "
                                  "primitive such as <x:Double>, not <" + tag.name + ">");
            }
            if (!tag.self_closing) {
                Tag close;
                if (!scanner.Next(close) || !close.closing || close.name != tag.name)
                    throw MarkupError("<" + tag.name + "> must be empty");
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
        node.name = directives.name;
        ApplyUid(attributes, directives, strings);

        // Style comes off next, before the rest: it is the one attribute whose
        // value is an object rather than a literal, so the text-substitution
        // path every other attribute takes has nothing to hand it.
        const auto style_attribute = attributes.find("Style");
        if (style_attribute != attributes.end()) {
            node.style = ResolveStyleReference(scope(), style_attribute->second);
            attributes.erase(style_attribute);
        }
        ApplyNodeAttributes(node, attributes, scope());

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

MarkupNode ParseMarkup(const std::string& markup) { return ParseMarkup(markup, NoStrings()); }

std::unique_ptr<Element> LoadMarkup(const std::string& markup) {
    return BuildElement(ParseMarkup(markup, NoStrings()), nullptr, std::make_shared<NameScope>());
}

std::unique_ptr<Element> LoadMarkup(const std::string& markup, const StringTable& strings) {
    return BuildElement(ParseMarkup(markup, strings), nullptr, std::make_shared<NameScope>());
}

std::unique_ptr<Element> LoadMarkup(const std::string& markup, ObservableObject& source) {
    return BuildElement(ParseMarkup(markup, NoStrings()), &source, std::make_shared<NameScope>());
}

std::unique_ptr<Element> LoadMarkup(const std::string& markup, const StringTable& strings,
                                    ObservableObject& source) {
    return BuildElement(ParseMarkup(markup, strings), &source, std::make_shared<NameScope>());
}

// --- the framework's own default styles ---------------------------------------

namespace {

// Setters the corpus refuses.
//
// This is the one place where the reconstruction is overruled, and it is
// overruled by measurement. `extract_default_styles.py`'s output comes from
// the WinUI 3 lineage of the system generic.xaml, not from the
// `Windows.UI.Xaml` build the oracle records, and the two have had years to
// drift. Where a setter out of that file changes a measurement the corpus has
// already recorded, the corpus is right and the setter is wrong -- and a
// setter that is wrong must not be applied at all, because a wrong number is
// worse than a missing one.
//
// Every entry here is therefore a claim about a specific recorded case, and
// each one carries the case that refuses it. None of them may be added
// without one: a hold list that could grow on suspicion would quietly become
// a way of making the corpus agree.
//
// Both entries were found by running the corpus, not by reading the source.
const std::map<std::string, std::string>& HeldSetters() {
    static const std::map<std::string, std::string> held = {
        {"Button.Padding",
         "L7-terminal-0e66f8e18d-s0 records a bare Button desiring [20, 32]; applying "
         "ButtonPadding gives [42, 32]. A Control's Padding is consumed by the "
         "ContentPresenter its template puts inside it, and the recorded tree has no "
         "template -- see the note on templateless measurement above. Until this project "
         "builds the reconstructed ControlTemplate, applying the padding directly moves a "
         "number the oracle has already answered"},
        {"ToolTip.Padding",
         "the same rule as Button.Padding, held for the same reason before a case can "
         "record it: L7-terminal-24911ba19e is a templateless ToolTip and its recorded "
         "size does not include ToolTipBorderThemePadding"},
    };
    return held;
}

// A value that names no font.
//
// `ContentControlThemeFontFamily` is the literal string `XamlAutoFontFamily` in
// the system generic.xaml, and it is not a family: it is the framework's
// sentinel for "whatever the system UI font is", resolved inside the text
// stack against the running system's locale and font settings. This project
// has no such mapping and -- more to the point -- no measurement to derive one
// from, so handing it to the font library produces a named failure for every
// element that inherits it (`L7-terminal-24911ba19e`, a ToolTip, went from a
// measured tree to "no harvested metrics for the font family
// XamlAutoFontFamily" the moment the framework dictionary started resolving).
//
// So a setter that resolves to it is held, by value rather than by name: five
// keys in the dictionary carry it, and a list of the keys would go stale the
// moment a sixth appeared. What it should resolve to is an oracle question
// (`L5-defaults-autofontfamily`), not a decision to be made here.
bool IsUnresolvableFontSentinel(const std::string& resolved) {
    return resolved == "XamlAutoFontFamily";
}

// The attribute text a classified setter value stands for.
//
// The database keeps a reference as the reference it is written as, so this
// writes it back out in its markup spelling and hands it to the ordinary
// attribute path. That is the same argument the resource system makes for
// storing literals rather than parsed values, one level up again: a
// `{ThemeResource ButtonPadding}` in generic.xaml and one written in a page
// are not two implementations that agree, they are one reached twice.
//
// Returns false for a value with no textual form -- a ControlTemplate, a
// DataTemplate, a brush object. Those are real content and are reported as
// dropped rather than skipped silently.
bool SetterText(const JsonValue& value, std::string& out, std::string& why) {
    const JsonValue* kind = value.Has("kind") ? &value.At("kind") : nullptr;
    if (!kind || kind->kind != JsonValue::Kind::String) {
        why = "the value has no kind";
        return false;
    }
    if (kind->string == "literal") {
        if (!value.Has("text")) {
            why = "a literal with no text";
            return false;
        }
        out = value.At("text").string;
        return true;
    }
    if (kind->string == "resource") {
        const std::string extension =
            value.Has("extension") ? value.At("extension").string : "StaticResource";
        const std::string key = value.Has("key") ? value.At("key").string : "";
        if (key.empty()) {
            why = "a " + extension + " naming no key";
            return false;
        }
        out = "{" + extension + " " + key + "}";
        return true;
    }
    if (kind->string == "opaque") {
        why = "a " + (value.Has("type") ? value.At("type").string : std::string("value")) +
              ", which has no textual form";
        return false;
    }
    why = "an unsupported value";
    return false;
}

// One half of the database -- the system's table or WinUI 2's.
void ReadDefaultStyleTable(const JsonValue& table, const std::string& half,
                           DefaultStyleRegistry& registry, DefaultStyleReport& report,
                           int& registered) {
    // The application dictionary and nothing above it: a default style is
    // declared in generic.xaml, so a {ThemeResource} inside one resolves where
    // generic.xaml is, not where the styled element happens to sit.
    ResourceScope application;
    for (const ResourceDictionary* layer : ThemeResourceLibrary::Default().ActiveLayers())
        application.push_back(layer);
    MarkupStyleHost host(application);

    for (const auto& [type, entry] : table.object) {
        // A type both halves style is a collision this cannot resolve. The two
        // tables are keyed by the markup name, and `Button` in WinUI 2's
        // default styles is `Microsoft.UI.Xaml.Controls.Button` -- a different
        // type that happens to be spelled the same. Giving a
        // `Windows.UI.Xaml` Button WinUI 2's setters would be a wrong number by
        // construction, so the framework's table wins and the muxc entry is
        // named rather than merged. The types that only exist on the muxc side
        // -- TabView, Expander, NumberBox and the rest of Terminal's demand
        // list -- have no such ambiguity and are taken.
        if (registry.Find(type)) {
            report.unknown_types.push_back(half + ":" + type +
                                           " (a type of this name is already styled)");
            continue;
        }
        try {
            host.ValidateTargetType(type);
        } catch (const MarkupError&) {
            // No element of this type can exist here, so no measurement can
            // depend on its style. Counted, not dropped silently: the list is
            // the coverage gap, stated in types.
            report.unknown_types.push_back(half + ":" + type);
            continue;
        }

        Style style;
        style.target_type = type;
        if (!entry.Has("setters") || entry.At("setters").kind != JsonValue::Kind::Array)
            throw JsonError("the default style for '" + type + "' has no setters array");

        for (const JsonValue& setter : entry.At("setters").array) {
            if (!setter.Has("property") || !setter.Has("value"))
                throw JsonError("a setter of the default style for '" + type + "' is malformed");
            const std::string property = setter.At("property").string;

            const auto held = HeldSetters().find(type + "." + property);
            if (held != HeldSetters().end()) {
                report.held.push_back(type + "." + property + ": " + held->second);
                continue;
            }

            std::string text;
            std::string why;
            if (!SetterText(setter.At("value"), text, why)) {
                report.dropped_setters.push_back(type + "." + property + ": " + why);
                continue;
            }
            try {
                // Resolved once here to look at, and then the *unresolved*
                // text is what ParseSetter is given -- resolving a literal is
                // the identity, so the setter that ends up in the style is the
                // one the ordinary path would have built, not a
                // pre-substituted copy of it.
                if (IsUnresolvableFontSentinel(ResolveAttributeValue(application, property, text))) {
                    report.held.push_back(type + "." + property +
                                          ": resolves to XamlAutoFontFamily, which names no font "
                                          "this project can measure; see "
                                          "L5-defaults-autofontfamily");
                    continue;
                }
                style.setters.push_back(host.ParseSetter(type, property, text));
            } catch (const MarkupError& error) {
                // A property this parser does not have, or a literal it cannot
                // read. Neither can change a measurement -- nothing here reads
                // a property that does not exist -- but both are the coverage
                // gap in its most useful form, one line per missing member.
                report.dropped_setters.push_back(type + "." + property + ": " + error.what());
            }
        }

        if (style.setters.empty()) continue;
        DefaultControlStyle control_style;
        control_style.style = std::move(style);
        registry.Register(type, std::move(control_style));
        report.applied.push_back(type);
        ++registered;
    }
}

}  // namespace

int LoadDefaultStyles(DefaultStyleRegistry& registry, const std::string& path,
                      DefaultStyleReport* report) {
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

    DefaultStyleReport discard;
    DefaultStyleReport& into = report ? *report : discard;
    int registered = 0;
    for (const fs::path& file : files) {
        std::ifstream in(file, std::ios::binary);
        std::ostringstream buffer;
        buffer << in.rdbuf();
        const JsonValue document = ParseJson(buffer.str());
        if (!document.Has("default_styles")) continue;
        const JsonValue& tables = document.At("default_styles");
        if (tables.kind != JsonValue::Kind::Object)
            throw JsonError(file.filename().string() + ": \"default_styles\" is not an object");
        // The system's table first, then WinUI 2's. Both halves register under
        // their own target types and the registry refuses a duplicate, so a
        // type both define -- and there are several, `Button` among them --
        // is a collision this cannot resolve and must not paper over. WinUI 2's
        // types are muxc types, spelled the same and not the same type, which
        // is the whole of why they are held out here rather than merged: this
        // parser has one namespace, and giving a `Windows.UI.Xaml` Button
        // WinUI 2's setters would be a wrong number by construction.
        if (tables.Has("system"))
            ReadDefaultStyleTable(tables.At("system"), "system", registry, into, registered);
        if (tables.Has("muxc"))
            ReadDefaultStyleTable(tables.At("muxc"), "muxc", registry, into, registered);
    }
    return registered;
}

}  // namespace openxaml
