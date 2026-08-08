#include "xbf_markup.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "markup.h"
#include "xbf_names.h"

namespace openxaml {
namespace {

using xbf::Constant;
using xbf::ConstantType;
using xbf::Document;
using xbf::MemberKind;
using xbf::Node;
using xbf::NodeType;
using xbf::Reference;

// The namespace XAML's own directives and primitive types live in. A page
// declares a prefix for it -- conventionally `x` -- and the reconstruction has
// to write that prefix back rather than assume one.
constexpr const char* kXamlDirectiveNamespace = "http://schemas.microsoft.com/winfx/2006/xaml";

// A name the reconstruction could not produce is a refusal, not a blank. The
// message names the index, because that is the fact -- the name is exactly what
// is missing.
[[noreturn]] void Refuse(const std::string& what) { throw MarkupError("XBF: " + what); }

// The types that live in the directive namespace rather than the presentation
// one, so `<x:Double>60</x:Double>` comes back out spelled the way it went in.
// Thickness, GridLength and the brushes are *not* here: they are presentation
// types and are written with no prefix.
bool IsDirectiveNamespaceType(const std::string& name) {
    return name == "Boolean" || name == "Double" || name == "Int32" || name == "Int64" ||
           name == "Single" || name == "String" || name == "Object";
}

std::string TypeNameOf(const Document& document, const Reference& reference) {
    if (reference.trusted) {
        if (const char* name = xbf::TypeName(reference.index)) return name;
        Refuse("stable type index " + std::to_string(reference.index) +
               " is not a type this runtime implements");
    }
    if (reference.index >= document.types.size()) {
        Refuse("type index " + std::to_string(reference.index) + " is past the type table");
    }
    const xbf::Type& type = document.types[reference.index];
    const std::string& name = document.String(type.name);
    if (type.unknown()) {
        Refuse("the page names the type '" + name +
               "', which no metadata provider available here defines");
    }
    // A type written into the file's own table comes from an assembly this
    // build has no projection for -- TerminalApp's own classes, the muxc
    // controls, a converter. Naming it is the useful half of refusing it.
    Refuse("the page names the type '" + name +
           "', which is declared by the page's own metadata provider rather than by the "
           "platform");
}

const xbf::MemberName& MemberOf(const Document& document, const Reference& reference) {
    if (reference.trusted) {
        if (const xbf::MemberName* member = xbf::PropertyName(reference.index)) return *member;
        Refuse("stable property index " + std::to_string(reference.index) +
               " is not a property this runtime implements");
    }
    if (reference.index >= document.properties.size()) {
        Refuse("property index " + std::to_string(reference.index) + " is past the property table");
    }
    const xbf::Property& property = document.properties[reference.index];
    const std::string& name = document.String(property.name);
    if (property.markup_directive()) {
        Refuse("the directive '" + name + "' is not implemented");
    }
    Refuse("the page sets the property '" + name +
           "', which is declared by the page's own metadata provider rather than by the "
           "platform");
}

// Shortest text that reads back as the same float. The value in the file is a
// float and the value the text path parsed was a double, so printing the
// shortest float representation is what makes the two agree: "12" for 12.0f,
// "0.1" for the float nearest 0.1 -- which is the text the value was compiled
// from in the first place.
std::string Number(float value) {
    if (std::isnan(value)) Refuse("a numeric constant in the page is not a number");
    if (std::isinf(value)) return value > 0 ? "Infinity" : "-Infinity";
    char buffer[32];
    for (int digits = 1; digits <= 9; ++digits) {
        std::snprintf(buffer, sizeof(buffer), "%.*g", digits, static_cast<double>(value));
        if (static_cast<float>(std::strtod(buffer, nullptr)) == value) return buffer;
    }
    std::snprintf(buffer, sizeof(buffer), "%.9g", static_cast<double>(value));
    return buffer;
}

std::string Hex2(unsigned value) {
    char buffer[3];
    std::snprintf(buffer, sizeof(buffer), "%02X", value & 0xffu);
    return buffer;
}

std::string ConstantText(const Constant& constant) {
    switch (constant.type) {
        case ConstantType::BoolTrue:
            return "True";
        case ConstantType::BoolFalse:
            return "False";
        case ConstantType::Float:
            return Number(constant.number);
        case ConstantType::Signed:
            return std::to_string(constant.signed_value);
        case ConstantType::SharedString:
        case ConstantType::UniqueString:
            return constant.text;
        case ConstantType::NullString:
            Refuse("a property in the page is set to a null string, which markup cannot spell");
        case ConstantType::Thickness:
            return Number(constant.left) + "," + Number(constant.top) + "," +
                   Number(constant.right) + "," + Number(constant.bottom);
        case ConstantType::GridLength:
            switch (constant.grid_unit) {
                case 0:
                    return "Auto";
                case 1:
                    return Number(constant.number);
                case 2:
                    return constant.number == 1.0f ? "*" : Number(constant.number) + "*";
                default:
                    Refuse("grid unit " + std::to_string(constant.grid_unit) +
                           " is not one this format defines");
            }
        case ConstantType::Color:
            return "#" + Hex2(constant.color >> 24) + Hex2(constant.color >> 16) +
                   Hex2(constant.color >> 8) + Hex2(constant.color);
        case ConstantType::Enum: {
            bool known_type = false;
            const std::string name =
                xbf::EnumValueName(constant.enum_type, constant.enum_value, &known_type);
            if (!name.empty()) return name;
            if (known_type) {
                Refuse("value " + std::to_string(constant.enum_value) +
                       " is not one the enumeration at stable type index " +
                       std::to_string(constant.enum_type) + " defines");
            }
            Refuse("stable type index " + std::to_string(constant.enum_type) +
                   " is not an enumeration this runtime implements");
        }
        default:
            Refuse("constant type " + std::to_string(static_cast<int>(constant.type)) +
                   " is not one this reader decodes");
    }
}

std::string Escape(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out.push_back(c);
        }
    }
    return out;
}

// A literal attribute value that starts with a brace has to be written with
// XAML's escape, `{}`, or reading it back would find a markup extension where
// the page has a string. The corpus has exactly this case -- a Text that reads
// "{StaticResource NotAKey}" and means those characters -- which is why the
// escape is applied rather than assumed away. It is applied where a *constant*
// becomes an attribute, never to the extensions this reconstruction writes
// itself, so the two cannot be confused.
std::string EscapeLeadingBrace(const std::string& value) {
    if (!value.empty() && value.front() == '{') return "{}" + value;
    return value;
}

struct Object;

// One thing the node stream did to an object, in the order it did it.
//
// Order matters and is not decoration: `<Border.Resources>` followed by
// `<Border.Width>{StaticResource ...}` resolves, and the same two written the
// other way round does not -- the corpus has both, one measuring and one
// failing by name. Writing attributes out of order would silently turn one into
// the other.
struct Part {
    enum class Kind { Attribute, Member, Child, Text };
    Kind kind = Kind::Attribute;
    std::string name;                // Attribute, Member
    std::string value;               // Attribute, Text
    std::vector<Object> objects;     // Member, Child
};

struct Object {
    std::string type;
    // The prefix the type's namespace is bound to, empty for the default.
    std::string prefix;
    std::vector<Part> parts;
    std::vector<std::pair<std::string, std::string>> namespaces;

    std::string Tag() const { return prefix.empty() ? type : prefix + ":" + type; }
};

void Write(const Object& object, std::string& out);

void WritePart(const Object& owner, const Part& part, std::string& out) {
    switch (part.kind) {
        case Part::Kind::Text:
            out += Escape(part.value);
            return;
        case Part::Kind::Child:
            for (const Object& child : part.objects) Write(child, out);
            return;
        case Part::Kind::Member:
            out += "<" + owner.Tag() + "." + part.name + ">";
            for (const Object& value : part.objects) Write(value, out);
            out += "</" + owner.Tag() + "." + part.name + ">";
            return;
        case Part::Kind::Attribute:
            // An attribute that is not on the tag has to be written in element
            // form, and only a resource reference can be: a bare literal there
            // is not something this reconstruction can spell, so it says so.
            if (part.value.rfind("{StaticResource ", 0) == 0 ||
                part.value.rfind("{ThemeResource ", 0) == 0) {
                const std::size_t space = part.value.find(' ');
                const std::string extension = part.value.substr(1, space - 1);
                const std::string key = part.value.substr(space + 1, part.value.size() - space - 2);
                out += "<" + owner.Tag() + "." + part.name + ">";
                out += "<" + extension + " ResourceKey=\"" + Escape(key) + "\"/>";
                out += "</" + owner.Tag() + "." + part.name + ">";
                return;
            }
            Refuse("the page sets '" + part.name +
                   "' after a property element, which markup can only spell for a resource "
                   "reference (the value here is \"" + part.value + "\")");
    }
}

void Write(const Object& object, std::string& out) {
    out += "<" + object.Tag();
    for (const auto& [prefix, uri] : object.namespaces) {
        out += " xmlns" + (prefix.empty() ? std::string() : ":" + prefix) + "=\"" + Escape(uri) +
               "\"";
    }
    // Leading attributes go on the tag; everything from the first non-attribute
    // onwards is written as elements, in order.
    std::size_t first_element = 0;
    while (first_element < object.parts.size() &&
           object.parts[first_element].kind == Part::Kind::Attribute) {
        const Part& part = object.parts[first_element];
        out += " " + part.name + "=\"" + Escape(part.value) + "\"";
        ++first_element;
    }
    if (first_element == object.parts.size()) {
        out += "/>";
        return;
    }
    out += ">";
    for (std::size_t i = first_element; i < object.parts.size(); ++i) {
        WritePart(object, object.parts[i], out);
    }
    out += "</" + object.Tag() + ">";
}

// One level of the object writer's scope stack.
struct Frame {
    enum class Kind { Object, Collection };
    Kind kind = Kind::Object;
    Object object;           // Kind::Object
    bool created = false;    // whether a type has been put in it yet
    std::string collection;  // Kind::Collection: the member being filled
    MemberKind collection_kind = MemberKind::Element;
    std::vector<Object> items;
    std::string text;
    bool has_text = false;
    // Namespaces declared at this level, waiting for the object created here.
    std::vector<std::pair<std::string, std::string>> namespaces;
};

class Interpreter {
public:
    explicit Interpreter(const Document& document) : document_(document) {}

    Object Run() {
        if (document_.sub_streams.empty()) Refuse("the page has no node stream");
        Interpret(0);
        if (!root_) Refuse("the page's node stream produced no root element");
        return std::move(*root_);
    }

private:
    void Interpret(std::size_t index, std::size_t skip = 0) {
        if (index >= document_.sub_streams.size()) {
            Refuse("sub-stream " + std::to_string(index) + " does not exist");
        }
        if (depth_ > 32) Refuse("the page's deferred sections nest deeper than this reader goes");
        const xbf::SubStream& stream = document_.sub_streams[index];
        if (!stream.truncated_because.empty()) Refuse(stream.truncated_because);
        ++depth_;
        for (std::size_t i = skip; i < stream.nodes.size(); ++i) Step(stream.nodes[i]);
        --depth_;
    }

    Frame& Current(const char* what) {
        if (stack_.empty()) Refuse(std::string(what) + " with no scope open");
        return stack_.back();
    }

    Frame& CurrentObject(const char* what) {
        Frame& frame = Current(what);
        if (frame.kind != Frame::Kind::Object) {
            Refuse(std::string(what) + " inside a collection scope");
        }
        if (!frame.created) Refuse(std::string(what) + " before its element was created");
        return frame;
    }

    void AddPart(Object& owner, Part part) { owner.parts.push_back(std::move(part)); }

    void AddChild(Object& owner, Object child) {
        Part part;
        part.kind = Part::Kind::Child;
        part.objects.push_back(std::move(child));
        AddPart(owner, std::move(part));
    }

    void AddText(Object& owner, const std::string& text) {
        if (!owner.parts.empty() && owner.parts.back().kind == Part::Kind::Text) {
            owner.parts.back().value += text;
            return;
        }
        Part part;
        part.kind = Part::Kind::Text;
        part.value = text;
        AddPart(owner, std::move(part));
    }

    // Puts the value that was just finished where the member says it goes.
    void PlaceValue(Object& owner, const xbf::MemberName& member) {
        if (!has_pending_) Refuse("a property was set with no value to set it to");
        has_pending_ = false;
        if (pending_is_text_) {
            if (member.kind != MemberKind::Content) {
                Refuse("the property '" + std::string(member.name) +
                       "' was given a bare string value");
            }
            AddText(owner, pending_text_);
            return;
        }
        switch (member.kind) {
            case MemberKind::Content:
                AddChild(owner, std::move(pending_));
                return;
            case MemberKind::Element: {
                Part part;
                part.kind = Part::Kind::Member;
                part.name = member.name;
                part.objects.push_back(std::move(pending_));
                AddPart(owner, std::move(part));
                return;
            }
            case MemberKind::Attribute:
            case MemberKind::Attached: {
                // A scalar property set from an element rather than an
                // attribute: <Border.UseLayoutRounding><x:Boolean>False
                // </x:Boolean></Border.UseLayoutRounding>. XAML spells that as
                // a property element, and so does the reconstruction -- the
                // attribute form would lose the type the markup named.
                Part part;
                part.kind = Part::Kind::Member;
                part.name = member.name;
                part.objects.push_back(std::move(pending_));
                AddPart(owner, std::move(part));
                return;
            }
        }
    }

    void SetScalar(Object& owner, const xbf::MemberName& member, const std::string& value) {
        if (member.kind == MemberKind::Content) {
            AddText(owner, value);
            return;
        }
        if (member.kind == MemberKind::Element) {
            Refuse("the property element '" + std::string(member.name) +
                   "' was set from a constant");
        }
        Part part;
        part.kind = Part::Kind::Attribute;
        part.name = member.name;
        part.value = value;
        AddPart(owner, std::move(part));
    }

    void TakePending(Object object) {
        pending_ = std::move(object);
        pending_is_text_ = false;
        has_pending_ = true;
    }

    void TakePendingText(std::string text) {
        pending_text_ = std::move(text);
        pending_is_text_ = true;
        has_pending_ = true;
    }

    // Adds whatever was just finished to the dictionary that is open, under the
    // key if there is one. A dictionary is either a Resources collection scope
    // or an explicit <ResourceDictionary> element scope, and both spellings
    // appear in the corpus.
    void AddToDictionary(const std::string* key, const char* what) {
        if (!has_pending_) Refuse(std::string(what) + " with no value to add");
        if (pending_is_text_) Refuse(std::string(what) + " was given a bare string");
        Object entry = std::move(pending_);
        has_pending_ = false;
        if (key) {
            Part part;
            part.kind = Part::Kind::Attribute;
            part.name = XamlPrefix() + ":Key";
            part.value = *key;
            entry.parts.insert(entry.parts.begin(), std::move(part));
        }
        Frame& frame = Current(what);
        if (frame.kind == Frame::Kind::Collection) {
            frame.items.push_back(std::move(entry));
            return;
        }
        if (!frame.created) Refuse(std::string(what) + " before its dictionary was created");
        AddChild(frame.object, std::move(entry));
    }

    std::string XamlPrefix() const {
        if (xaml_prefix_) return *xaml_prefix_;
        Refuse("the page uses a XAML directive but declares no prefix for " +
               std::string(kXamlDirectiveNamespace));
    }

    Object MakeResourceReference(const std::string& extension, const std::string& key) {
        Object object;
        object.type = extension;
        Part part;
        part.kind = Part::Kind::Attribute;
        part.name = "ResourceKey";
        part.value = key;
        object.parts.push_back(std::move(part));
        return object;
    }

    Object MakeTypedConstant(const std::string& type, const std::string& text) {
        Object object;
        object.type = type;
        if (IsDirectiveNamespaceType(type)) object.prefix = XamlPrefix();
        Part part;
        part.kind = Part::Kind::Text;
        part.value = text;
        object.parts.push_back(std::move(part));
        return object;
    }

    // How many nodes at the head of a deferred sub-stream repeat what has
    // already been done.
    //
    // A deferred section is written so it can stand on its own: the custom
    // writer replays the namespace declarations and the instruction that opened
    // the dictionary's scope before the entries themselves. A loader that does
    // not defer is already standing in that scope, so the replay has to be
    // skipped -- and skipped exactly, which means finding the instruction that
    // matches the open scope rather than counting nodes. Not finding it means
    // the stream is not shaped the way this reader believes, which is worth
    // saying rather than working around.
    std::size_t PrologueOf(std::uint32_t index) {
        const xbf::SubStream& stream = SubStream(index);
        const Frame& frame = Current("a deferred section");
        for (std::size_t i = 0; i < stream.nodes.size(); ++i) {
            const Node& candidate = stream.nodes[i];
            if (frame.kind == Frame::Kind::Collection) {
                if (candidate.type == NodeType::PushScopeGetValue &&
                    MemberOf(document_, candidate.property).name == frame.collection) {
                    return i + 1;
                }
                continue;
            }
            if (!frame.created) continue;
            if ((candidate.type == NodeType::CreateTypeBeginInit ||
                 candidate.type == NodeType::PushScopeCreateTypeBeginInit) &&
                TypeNameOf(document_, candidate.type_ref) == frame.object.type) {
                return i + 1;
            }
        }
        Refuse("the deferred sub-stream " + std::to_string(index) +
               " never repeats the instruction that opened the scope it belongs to (" +
               (frame.kind == Frame::Kind::Collection ? frame.collection : frame.object.type) +
               ")");
    }

    // A setter whose value is a resource reference does not carry the key: it
    // carries the offset of the node that would have set it, in the deferred
    // sub-stream. That node is a single instruction, so reading exactly it is
    // enough -- and anything else at that offset is refused rather than
    // interpreted, because a run of nodes has no end this reader can find.
    std::string ResourceSetterValue(const Node& node, const xbf::StyleSetter& setter,
                                    const std::string& target) {
        if (!node.has_sub_stream) {
            Refuse("a deferred style names no sub-stream for its setter values");
        }
        const xbf::SubStream& stream = SubStream(node.sub_stream);
        for (const Node& candidate : stream.nodes) {
            if (candidate.offset != setter.token) continue;
            if (candidate.type == NodeType::SetValueFromStaticResource) {
                return "{StaticResource " + ConstantText(candidate.constant) + "}";
            }
            if (candidate.type == NodeType::SetValueFromThemeResource) {
                return "{ThemeResource " + ConstantText(candidate.constant) + "}";
            }
            Refuse("the style's setter for '" + target + "' points at " +
                   xbf::NodeTypeName(candidate.type) + " in a deferred sub-stream, which this "
                   "reader does not realise as a value");
        }
        Refuse("the style's setter for '" + target + "' points at offset " +
               std::to_string(setter.token) + " of sub-stream " +
               std::to_string(node.sub_stream) + ", where no node begins");
    }

    // A deferred <Style>'s setters, written back as the <Setter> elements they
    // were compiled from. The style writer boils each one down to a property
    // and a value, so nothing is left of the elements but this -- which is
    // exactly enough to write them again.
    void RealiseStyleSetters(const Node& node) {
        Frame& frame = Current("SetCustomRuntimeData");
        if (frame.kind != Frame::Kind::Object || !frame.created ||
            frame.object.type != "Style") {
            Refuse("a deferred style's setters do not follow a <Style>");
        }
        for (const xbf::StyleSetter& setter : node.style_setters) {
            Object element;
            element.type = "Setter";
            const std::string target = setter.property_resolved
                                           ? std::string(MemberOf(document_, setter.property).name)
                                           : setter.property_name;
            Part property;
            property.kind = Part::Kind::Attribute;
            property.name = "Property";
            property.value = target;
            element.parts.push_back(std::move(property));

            Part value;
            value.kind = Part::Kind::Attribute;
            value.name = "Value";
            switch (setter.value_kind) {
                case xbf::StyleSetter::ValueKind::Container:
                    value.value = EscapeLeadingBrace(ConstantText(setter.value));
                    break;
                case xbf::StyleSetter::ValueKind::String:
                    value.value = EscapeLeadingBrace(setter.text);
                    break;
                case xbf::StyleSetter::ValueKind::StaticResource:
                case xbf::StyleSetter::ValueKind::ThemeResource:
                    value.value = ResourceSetterValue(node, setter, target);
                    break;
                case xbf::StyleSetter::ValueKind::Object:
                case xbf::StyleSetter::ValueKind::Self:
                    // The value is a whole object built by a run of nodes in the
                    // deferred sub-stream. Realising it means interpreting from
                    // the middle of a stream and knowing where to stop, which
                    // this reader does not do -- and the property it would have
                    // set is named, because that is the useful part of saying
                    // no.
                    Refuse("the style's setter for '" + target +
                           "' builds its value from offset " + std::to_string(setter.token) +
                           " of a deferred sub-stream, which this reader does not realise");
                case xbf::StyleSetter::ValueKind::None:
                    Refuse("the style's setter for '" + target +
                           "' has no value");
            }
            element.parts.push_back(std::move(value));
            AddChild(frame.object, std::move(element));
        }
    }

    void DeclareNamespace(Frame& frame, const Node& node) {
        const std::string uri = NamespaceUri(node.xml_namespace);
        if (uri == kXamlDirectiveNamespace) xaml_prefix_ = node.prefix;
        frame.namespaces.emplace_back(node.prefix, uri);
    }

    void Step(const Node& node) {
        switch (node.type) {
            case NodeType::PushScopeAddNamespace: {
                Frame frame;
                DeclareNamespace(frame, node);
                stack_.push_back(std::move(frame));
                return;
            }
            case NodeType::AddNamespace:
                DeclareNamespace(Current("AddNamespace"), node);
                return;

            case NodeType::PushScope:
                stack_.push_back(Frame{});
                return;

            case NodeType::CreateTypeBeginInit: {
                Frame& frame = Current("CreateTypeBeginInit");
                if (frame.kind != Frame::Kind::Object || frame.created) {
                    Refuse("CreateTypeBeginInit in a scope that already holds an element");
                }
                frame.object.type = TypeNameOf(document_, node.type_ref);
                frame.object.namespaces = std::move(frame.namespaces);
                frame.namespaces.clear();
                frame.created = true;
                return;
            }
            case NodeType::PushScopeCreateTypeBeginInit: {
                Frame frame;
                frame.object.type = TypeNameOf(document_, node.type_ref);
                frame.created = true;
                stack_.push_back(std::move(frame));
                return;
            }

            // A primitive written as an element: <x:Double>60</x:Double>,
            // <Thickness>4,8,12,16</Thickness>. The value is in the node, so
            // nothing further is pushed for it.
            case NodeType::CreateTypeWithConstantBeginInit:
            case NodeType::CreateTypeWithTypeConvertedConstantBeginInit: {
                Frame& frame = Current("CreateTypeWithConstantBeginInit");
                if (frame.kind != Frame::Kind::Object || frame.created) {
                    Refuse("CreateTypeWithConstantBeginInit in a scope that already holds an "
                           "element");
                }
                frame.object = MakeTypedConstant(TypeNameOf(document_, node.type_ref),
                                                 ConstantText(node.constant));
                frame.object.namespaces = std::move(frame.namespaces);
                frame.namespaces.clear();
                frame.created = true;
                return;
            }
            case NodeType::PushScopeCreateTypeWithConstantBeginInit:
            case NodeType::PushScopeCreateTypeWithTypeConvertedConstantBeginInit: {
                Frame frame;
                frame.object = MakeTypedConstant(TypeNameOf(document_, node.type_ref),
                                                 ConstantText(node.constant));
                frame.created = true;
                stack_.push_back(std::move(frame));
                return;
            }

            case NodeType::EndInitPopScope: {
                Frame frame = std::move(Current("EndInitPopScope"));
                stack_.pop_back();
                if (frame.kind != Frame::Kind::Object || !frame.created) {
                    Refuse("EndInitPopScope on a scope that holds no element");
                }
                TakePending(std::move(frame.object));
                if (stack_.empty()) {
                    if (root_) Refuse("the node stream produced two root elements");
                    root_ = std::move(pending_);
                    has_pending_ = false;
                }
                return;
            }

            case NodeType::PushScopeGetValue: {
                const xbf::MemberName& member = MemberOf(document_, node.property);
                Frame frame;
                frame.kind = Frame::Kind::Collection;
                frame.collection = member.name;
                frame.collection_kind = member.kind;
                stack_.push_back(std::move(frame));
                return;
            }
            case NodeType::PopScope: {
                Frame frame = std::move(Current("PopScope"));
                stack_.pop_back();
                if (frame.kind != Frame::Kind::Collection) {
                    Refuse("PopScope on a scope that is not a collection");
                }
                Object& owner = CurrentObject("PopScope").object;
                if (frame.collection_kind == MemberKind::Content) {
                    if (frame.has_text) AddText(owner, frame.text);
                    for (Object& item : frame.items) AddChild(owner, std::move(item));
                    return;
                }
                if (frame.has_text) {
                    Refuse("the property element '" + frame.collection +
                           "' collected a bare string");
                }
                Part part;
                part.kind = Part::Kind::Member;
                part.name = frame.collection;
                part.objects = std::move(frame.items);
                AddPart(owner, std::move(part));
                return;
            }
            case NodeType::AddToCollection: {
                Frame& frame = Current("AddToCollection");
                if (frame.kind != Frame::Kind::Collection) {
                    Refuse("AddToCollection outside a collection scope");
                }
                if (!has_pending_) Refuse("AddToCollection with no value to add");
                if (pending_is_text_) {
                    // A TextBlock's character data reaches its Inlines
                    // collection as a bare string. That is how the format tells
                    // <TextBlock>x</TextBlock> apart from Text="x" -- the two
                    // are different node streams, not one.
                    if (frame.collection_kind != MemberKind::Content) {
                        Refuse("the collection '" + frame.collection + "' was given a string");
                    }
                    frame.text += pending_text_;
                    frame.has_text = true;
                    has_pending_ = false;
                    return;
                }
                frame.items.push_back(std::move(pending_));
                has_pending_ = false;
                return;
            }

            case NodeType::SetValue:
            case NodeType::SetValueFromMarkupExtension:
                PlaceValue(CurrentObject("SetValue").object, MemberOf(document_, node.property));
                return;

            case NodeType::SetValueConstant:
            case NodeType::SetValueTypeConvertedConstant:
                SetScalar(CurrentObject("SetValueConstant").object,
                          MemberOf(document_, node.property),
                          EscapeLeadingBrace(ConstantText(node.constant)));
                return;

            case NodeType::SetValueFromStaticResource:
                SetScalar(CurrentObject("SetValueFromStaticResource").object,
                          MemberOf(document_, node.property),
                          "{StaticResource " + ConstantText(node.constant) + "}");
                return;
            case NodeType::SetValueFromThemeResource:
                SetScalar(CurrentObject("SetValueFromThemeResource").object,
                          MemberOf(document_, node.property),
                          "{ThemeResource " + ConstantText(node.constant) + "}");
                return;
            case NodeType::SetValueFromTemplateBinding:
                SetScalar(CurrentObject("SetValueFromTemplateBinding").object,
                          MemberOf(document_, node.property),
                          "{TemplateBinding " +
                              std::string(MemberOf(document_, node.property_proxy).name) + "}");
                return;
            case NodeType::SetValueTypeConvertedResolvedType:
                SetScalar(CurrentObject("SetValueTypeConvertedResolvedType").object,
                          MemberOf(document_, node.property),
                          TypeNameOf(document_, node.type_ref));
                return;
            case NodeType::SetValueTypeConvertedResolvedProperty:
                SetScalar(CurrentObject("SetValueTypeConvertedResolvedProperty").object,
                          MemberOf(document_, node.property),
                          MemberOf(document_, node.property_proxy).name);
                return;

            case NodeType::SetName: {
                Object& owner = CurrentObject("SetName").object;
                Part part;
                part.kind = Part::Kind::Attribute;
                part.name = XamlPrefix() + ":Name";
                part.value = ConstantText(node.constant);
                AddPart(owner, std::move(part));
                return;
            }

            case NodeType::PushConstant:
                TakePendingText(ConstantText(node.constant));
                return;

            case NodeType::ProvideStaticResourceValue:
                TakePending(MakeResourceReference("StaticResource", ConstantText(node.constant)));
                return;
            case NodeType::ProvideThemeResourceValue:
                TakePending(MakeResourceReference("ThemeResource", ConstantText(node.constant)));
                return;

            case NodeType::AddToDictionaryWithKey: {
                const std::string key = ConstantText(node.constant);
                AddToDictionary(&key, "AddToDictionaryWithKey");
                return;
            }
            case NodeType::AddToDictionary:
                AddToDictionary(nullptr, "AddToDictionary");
                return;

            case NodeType::SetCustomRuntimeData: {
                // A deferred resource dictionary. The runtime realises it
                // lazily; a loader that does not defer has to realise it now,
                // which means interpreting the sub-stream the blob points at.
                //
                // That sub-stream opens by repeating the very instruction that
                // opened the dictionary's scope here -- the custom writer
                // records it so the deferred stream can stand alone. The scope
                // is already open, so the repeat is skipped, and checked rather
                // than assumed: if the first node is not that instruction, the
                // stream is not shaped the way this reader believes.
                if (!node.has_sub_stream) {
                    Refuse("a deferred section names no sub-stream");
                }
                const auto kind = static_cast<xbf::CustomDataKind>(node.custom_data_kind);
                if (kind == xbf::CustomDataKind::Style_v1 ||
                    kind == xbf::CustomDataKind::Style_v2) {
                    RealiseStyleSetters(node);
                    return;
                }
                if (kind != xbf::CustomDataKind::ResourceDictionary_v3) {
                    Refuse("the " + xbf::CustomDataKindName(node.custom_data_kind) +
                           " custom writer's runtime data is not one this reader realises");
                }
                Interpret(node.sub_stream, PrologueOf(node.sub_stream));
                return;
            }

            // Everything below is a feature of the format this runtime has not
            // reached yet. Each one is named for what it is, because these
            // names are the work list for the wave that does reach them.
            case NodeType::SetDeferredProperty:
                Refuse("the page defers the property '" +
                       std::string(MemberOf(document_, node.property).name) + "' to sub-stream " +
                       std::to_string(node.sub_stream) + ", which this reader does not realise");
            case NodeType::SetResourceDictionaryItems:
                Refuse("the page defers a resource dictionary's items "
                       "(SetResourceDictionaryItems)");
            case NodeType::SetConnectionId:
                Refuse("the page has a code-behind connection (x:Bind or an event handler) with "
                       "connection id " +
                       ConstantText(node.constant));
            case NodeType::CheckPeerType:
                Refuse("the page has code-behind: it checks for the peer type '" + node.peer_type +
                       "'");
            case NodeType::GetResourcePropertyBag: {
                // x:Uid. The compiler turns it into "look this element's
                // properties up in the resource map"; the directive itself is
                // what the markup engine takes, and it resolves the uid against
                // whatever table it was given -- an empty one here, exactly as
                // the text path has.
                Object& owner = CurrentObject("GetResourcePropertyBag").object;
                Part part;
                part.kind = Part::Kind::Attribute;
                part.name = XamlPrefix() + ":Uid";
                part.value = ConstantText(node.constant);
                AddPart(owner, std::move(part));
                return;
            }
            case NodeType::BeginConditionalScope:
            case NodeType::EndConditionalScope:
                Refuse("the page uses conditional XAML, which this reader does not evaluate");
            case NodeType::EndInitProvideValuePopScope:
                Refuse("the page builds a markup extension object, whose provided value this "
                       "reader does not evaluate");
            case NodeType::EndOfStream:
                return;
            default:
                Refuse(xbf::NodeTypeName(node.type) +
                       " is not an instruction this reader realises");
        }
    }

    const xbf::SubStream& SubStream(std::uint32_t index) const {
        if (index >= document_.sub_streams.size()) {
            Refuse("sub-stream " + std::to_string(index) + " does not exist");
        }
        return document_.sub_streams[index];
    }

    std::string NamespaceUri(const Reference& reference) const {
        if (reference.trusted) {
            Refuse("a trusted xml namespace index, which the format has no table for");
        }
        if (reference.index >= document_.xml_namespaces.size()) {
            Refuse("xml namespace index " + std::to_string(reference.index) +
                   " is past the namespace table");
        }
        return document_.String(document_.xml_namespaces[reference.index]);
    }

    const Document& document_;
    std::vector<Frame> stack_;
    std::optional<Object> root_;
    Object pending_;
    std::string pending_text_;
    bool pending_is_text_ = false;
    bool has_pending_ = false;
    std::optional<std::string> xaml_prefix_;
    int depth_ = 0;
};

}  // namespace

std::string XbfToMarkup(const xbf::Document& document) {
    Object root = Interpreter(document).Run();
    std::string out;
    Write(root, out);
    return out;
}

std::string XbfToMarkup(const std::string& bytes) { return XbfToMarkup(xbf::Read(bytes)); }

MarkupNode ParseXbf(const std::string& bytes) { return ParseMarkup(XbfToMarkup(bytes)); }

std::unique_ptr<Element> LoadXbf(const std::string& bytes) { return LoadMarkup(XbfToMarkup(bytes)); }

}  // namespace openxaml
