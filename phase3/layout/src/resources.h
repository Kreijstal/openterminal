// Resource dictionaries, x:Key, and {StaticResource} resolution.
//
// A resource is kept as the literal text its declaring element carried, next to
// the name of that element -- "x:Double", "Thickness". Resolving a reference
// therefore hands the property exactly the string an inlined literal would have
// written, and the same property parser runs either way.
//
// That is deliberate, and it is what makes a {StaticResource} case comparable
// to its inlined twin: the two are not separate implementations that happen to
// agree on a number, they are one implementation reached by two routes. Where
// the oracle has not measured these cases yet, that identity is the only thing
// standing in for it, so it is worth not weakening -- a resource system that
// parsed values into its own representation could round or widen differently
// from the attribute path and the twins would still both be "right".
//
// The one thing text substitution alone would not catch is the check WinUI
// makes before it substitutes: an x:String cannot satisfy a Width. So a
// resource also carries the shape of its value, and the shape is checked
// against the property before the text is handed over.

#ifndef OPENXAML_RESOURCES_H
#define OPENXAML_RESOURCES_H

#include <map>
#include <string>
#include <vector>

namespace openxaml {

// The shapes a value can have, at the granularity the property parsers care
// about. Not a type system -- just enough to tell a length from a Thickness.
enum class ValueKind {
    // A property this parser has no declared shape for. Resolution still
    // happens; only the type check is skipped, and the property's own parser
    // rejects what it cannot read. Deliberately not fatal, so that a property
    // added to the parser later degrades to a weaker check rather than to a
    // wrong one.
    Unknown,
    Number,
    Integer,
    Thickness,
    GridLength,
    String,
    Boolean,
    // The shapes only the application dictionary supplies. A Color is not a
    // Brush: WinUI refuses a Color handed to a Background, and the extracted
    // dictionary holds plenty of both, so collapsing them would let a case
    // resolve something the real runtime rejects.
    Color,
    Brush,
    CornerRadius,
    Duration,
};

struct ResourceValue {
    // The element that declared it, as written: "x:Double", "Thickness".
    std::string type;
    // The literal, spelled exactly as an inlined attribute would spell it.
    std::string text;
    ValueKind kind = ValueKind::Unknown;
};

// One dictionary. Keys are unique within it, which XAML enforces at load time
// rather than letting the later entry win silently.
class ResourceDictionary {
public:
    void Add(const std::string& key, ResourceValue value);
    const ResourceValue* Find(const std::string& key) const;
    bool empty() const { return entries_.empty(); }
    size_t size() const { return entries_.size(); }

private:
    std::map<std::string, ResourceValue> entries_;
};

// The dictionaries a lookup walks, innermost first: the element, then each
// ancestor, then Application.Resources.
using ResourceScope = std::vector<const ResourceDictionary*>;

// The application-level dictionary -- the tail of every lookup chain, and the
// one no markup declares.
//
// In a running Terminal this is what `<XamlControlsResources/>` puts in
// `Application.Resources`: WinUI 2's theme dictionary, merged over the OS's own
// `Windows.UI.Xaml` one. Only the first of those two is open source, so only
// the first is here; `phase3/scripts/extract_winui_theme_resources.py` builds
// it out of the pinned WinUI 2.8.4 tree, and the OS half is still missing. A
// key from the missing half fails by name exactly as it did before, which is
// the point of loading a real dictionary rather than a permissive one.
//
// One dictionary per theme, each already merged with the theme-independent
// entries, because that is the only shape a lookup needs. The theme is set per
// case: the corpus declares one, and Default and Light really do differ -- in
// 106 colours and in one x:Double.
class ThemeResourceLibrary {
public:
    static ThemeResourceLibrary& Default();

    void Add(const std::string& theme, ResourceDictionary dictionary);
    // Throws naming the themes that are loaded, so a case asking for one that
    // is not says which are.
    void SetActiveTheme(const std::string& theme);
    const std::string& active_theme() const { return active_; }

    // Null when nothing is loaded, which is a bare checkout and not an error:
    // every lookup that would have needed it then fails naming its key, the
    // way it did before there was a dictionary at all.
    const ResourceDictionary* Active() const;
    bool empty() const { return themes_.empty(); }
    std::vector<std::string> themes() const;

private:
    std::map<std::string, ResourceDictionary> themes_;
    std::string active_ = "Default";
};

// Loads the extracted database into `library`, and returns how many keys the
// largest theme carried. `path` may be a file or a directory of them; a
// directory that is not there loads nothing and is not an error, because the
// database is generated rather than committed and a checkout without it must
// still run.
int LoadThemeResources(ThemeResourceLibrary& library, const std::string& path);

// True for the element names that declare a resource of a type this parser
// knows: the x-namespace primitives and Thickness.
bool IsResourceType(const std::string& type);

// Builds a resource from its declaring element and content. Throws for a type
// outside the implemented set, so an unhandled resource type is a named load
// failure rather than a value that quietly never resolves.
ResourceValue MakeResource(const std::string& type, const std::string& text);

// The shape a property's value has to have. Unknown for anything not listed.
ValueKind ExpectedValueKind(const std::string& property);

// Walks `scope` for `key`. Throws MarkupError naming the key when no
// dictionary in the chain has it.
//
// The chain runs from the element to the root of the markup and then into the
// application dictionary, which is what makes
// {ThemeResource SystemControlForegroundBaseHighBrush} resolve here at all.
// Merged dictionaries inside a page are still a gap, and so is the OS half of
// the application dictionary; a lookup that needed either fails by name instead
// of returning something plausible.
const ResourceValue& LookUpResource(const ResourceScope& scope, const std::string& key,
                                    const std::string& where);

// Looks `key` up and returns its literal text, after checking that the
// resource's shape can supply `property`.
std::string ResolveResource(const ResourceScope& scope, const std::string& key,
                            const std::string& property);

struct MarkupExtension {
    // "StaticResource", "ThemeResource", "x:Bind"...
    std::string name;
    // The single argument, positional or named. {StaticResource Foo} and
    // {StaticResource ResourceKey=Foo} are the same reference written twice.
    std::string argument;
};

// Recognises `{Name argument}` in an attribute value.
//
// `{}` at the start is XAML's escape for a literal brace and is not an
// extension: the leading two characters are stripped and false is returned.
bool TryParseMarkupExtension(const std::string& text, MarkupExtension& extension);

// The attribute value a property should actually parse: the literal for a plain
// value, the resource's text for a {StaticResource}, and a named error for any
// other extension.
std::string ResolveAttributeValue(const ResourceScope& scope, const std::string& property,
                                  const std::string& raw);

// The key a <StaticResource ResourceKey="..."/> element names, from its
// attributes. Rejects anything else the element carries.
std::string StaticResourceElementKey(const std::map<std::string, std::string>& attributes,
                                     bool allow_key);

}  // namespace openxaml

#endif  // OPENXAML_RESOURCES_H
