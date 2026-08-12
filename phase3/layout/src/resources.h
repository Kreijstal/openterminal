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
#include <memory>
#include <string>
#include <vector>

namespace openxaml {

// A dictionary entry may be a Style, which is the one resource in the corpus
// with no textual form at all. Declared rather than included: styles are built
// on the property registry and resource lookup is not, and nothing here reads
// anything out of one.
struct Style;

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
    // Not a literal. A Style is an object, and the only property it can supply
    // is FrameworkElement.Style -- which is exactly what this shape is for:
    // {StaticResource BoxStyle} on a Width has to fail saying so, rather than
    // resolving to the empty text a style has.
    Style,
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
    // Empty for a Style, which has none.
    std::string text;
    ValueKind kind = ValueKind::Unknown;
    // Set exactly when kind is Style. Shared rather than owned, because a
    // BasedOn and an alias entry both make a second dictionary refer to the
    // same style object, and neither of them copies it.
    std::shared_ptr<const Style> style;
};

// One dictionary. Keys are unique within it, which XAML enforces at load time
// rather than letting the later entry win silently.
class ResourceDictionary {
public:
    void Add(const std::string& key, ResourceValue value);
    const ResourceValue* Find(const std::string& key) const;
    bool empty() const { return entries_.empty(); }
    size_t size() const { return entries_.size(); }
    // Every key, so that a layered dictionary can be counted without the
    // counter having to know how entries are stored.
    std::vector<std::string> keys() const;

private:
    std::map<std::string, ResourceValue> entries_;
};

// The dictionaries a lookup walks, innermost first: the element, then each
// ancestor, then Application.Resources.
using ResourceScope = std::vector<const ResourceDictionary*>;

// Which of the application dictionary's layers an entry came from.
//
// `Application.Resources` is not one dictionary, and the difference decides
// which value a key has. Bottom to top, as a running WinUI 2 application
// stacks them:
//
//   GlobalThemeResources   the framework's own generic.xaml -- the theme
//                          dictionaries `Windows.UI.Xaml.Controls.dll`
//                          carries. Nothing in the markup mentions it and
//                          nothing can remove it; it is the floor every
//                          lookup ends on.
//   XamlControlsResources  what `<XamlControlsResources/>` merges in.
//                          `dev/dll/XamlControlsResources.cpp` (MIT, 2.8.4)
//                          shows exactly what that is: a ResourceDictionary
//                          whose Source is
//                          `ms-appx:///Microsoft.UI.Xaml/Themes/21h1_themeresources.xaml`
//                          for ControlsResourcesVersion2 on a current
//                          Windows, which is the file
//                          `extract_winui_theme_resources.py` reconstructs.
//
// A merged dictionary shadows what is under it and does not replace it: WinUI
// 2 redefines most of the `SystemControl*` family and leaves three of them
// alone, and those three have to keep resolving to the OS's. Loading the two
// halves into one flat dictionary would have made that indistinguishable from
// a key nobody defines.
enum class ResourceLayer {
    GlobalThemeResources = 0,
    XamlControlsResources = 1,
};

// The application-level dictionary -- the tail of every lookup chain, and the
// one no markup declares.
//
// Both layers are now reconstructed from pinned MIT source: the WinUI 2 half by
// `phase3/scripts/extract_winui_theme_resources.py` out of the 2.8.4 tree, and
// the framework half by `phase3/scripts/extract_default_styles.py` out of the
// system `generic.xaml` the XAML core publishes at 188f602b. What remains
// missing is only what no source tree can hold -- the signed-in user's accent
// colour and the OS's `SystemColor*` palette -- and a key from that set still
// fails by name, which is the point of loading a real dictionary rather than a
// permissive one.
//
// One dictionary per theme per layer, each already merged with that layer's
// theme-independent entries, because that is the only shape a lookup needs. The
// theme is set per case: the corpus declares one, and Default and Light really
// do differ -- in 106 colours and in one x:Double.
class ThemeResourceLibrary {
public:
    static ThemeResourceLibrary& Default();

    // Adds `dictionary` as `theme`'s entry for `layer`. Two dictionaries
    // claiming one theme *and* one layer is still refused: that is a
    // duplicate, not a merge, and it has no defensible resolution.
    void Add(const std::string& theme, ResourceDictionary dictionary,
             ResourceLayer layer = ResourceLayer::XamlControlsResources);
    // Throws naming the themes that are loaded, so a case asking for one that
    // is not says which are.
    void SetActiveTheme(const std::string& theme);
    const std::string& active_theme() const { return active_; }

    // The active theme's layers, highest precedence first, for the scope a
    // lookup walks. Empty when nothing is loaded, which is a bare checkout and
    // not an error: every lookup that would have needed it then fails naming
    // its key, the way it did before there was a dictionary at all.
    std::vector<const ResourceDictionary*> ActiveLayers() const;

    // The highest layer that has a dictionary for the active theme, or null.
    // Kept for the callers that only want to know whether anything is loaded
    // and how big it is; a lookup must use ActiveLayers().
    const ResourceDictionary* Active() const;

    // How many distinct keys the active theme resolves, across every layer --
    // which is what "the application dictionary has N keys" means once the
    // dictionary has more than one layer in it.
    size_t ActiveKeyCount() const;

    bool empty() const { return themes_.empty(); }
    std::vector<std::string> themes() const;

private:
    // theme -> layer -> dictionary. An ordered map on the layer so that
    // iterating it is the stacking order and the walk cannot get it wrong.
    std::map<std::string, std::map<ResourceLayer, ResourceDictionary>> themes_;
    std::string active_ = "Default";
};

// `Microsoft.UI.Xaml.Controls.XamlControlsResources`, as far as resolution goes.
//
// The first thing Terminal's `App.xaml` merges, and the reason a WinUI 2
// application resolves anything at all. It is not a dictionary of its own: it
// is a `ResourceDictionary` whose `Source` it computes at construction and
// then loads, and everything about which file that is is decided by
// `dev/dll/XamlControlsResources.cpp` (MIT, 2.8.4) in one expression --
//
//     packagePrefix + releasePrefix + compactPrefix + postfix
//
// -- over `ms-appx:///Microsoft.UI.Xaml/Themes/`, a release prefix chosen by
// the running OS version, `compact_` when `UseCompactResources` is set, and
// `themeresources.xaml` unless `ControlsResourcesVersion` is `Version1`.
//
// It is here rather than in the DLL because the resolution is what a
// measurement can see. The class exists to say *which* dictionary an
// application gets and to put it in the right layer -- the two facts that
// decide what a key resolves to -- and to be checkable without a WinRT
// activation. Registering it as an activatable runtime class is a separate
// step and is not done: see the README.
class XamlControlsResources {
public:
    enum class Version { Version1, Version2 };

    // Version2 from WinUI 2.6 on, which is what Terminal gets: the property
    // defaults to it and Terminal's App.xaml does not set it.
    void set_controls_resources_version(Version version) { version_ = version; }
    Version controls_resources_version() const { return version_; }
    void set_use_compact_resources(bool value) { compact_ = value; }
    bool use_compact_resources() const { return compact_; }

    // `UpdateSource`'s answer, for a current Windows and a package that is
    // neither a framework package nor a CBS one -- which is how Terminal ships
    // WinUI 2. Every other branch of that expression is an OS or packaging
    // fact this project has no way to be in, and returning a URI for one would
    // claim a file that is not the one being reconstructed.
    std::string SourceUri() const;

    // Puts `dictionaries` into `library` at the XamlControlsResources layer --
    // the merged-dictionary semantics, which is the whole of what this class
    // does to a lookup. `dictionaries` are the entries of the file SourceUri()
    // names, one per theme, which is what
    // `extract_winui_theme_resources.py` reconstructs.
    void Merge(ThemeResourceLibrary& library,
               std::map<std::string, ResourceDictionary> dictionaries) const;

private:
    Version version_ = Version::Version2;
    bool compact_ = false;
};

// Loads the extracted database into `library`, and returns how many distinct
// keys the loaded layers resolve. `path` may be a file or a directory of them;
// a directory that is not there loads nothing and is not an error, because the
// database is generated rather than committed and a checkout without it must
// still run.
//
// `top_layer` is the highest layer the loading host actually has, and a
// database above it is skipped rather than loaded -- the file describes a
// dictionary that does not exist in that host. The default is the full stack,
// which is what a running WinUI 2 application has once its App.xaml merges
// XamlControlsResources. The measurement hosts pass GlobalThemeResources: the
// oracle probe (phase3/harness/xaml_probe.cpp) is a bare WindowsXamlManager
// with no Application object and nothing merged, so the framework's own
// generic.xaml is the only dictionary a lookup there can reach -- the
// recording shows every WinUI-2-only key refused by name and ButtonPadding
// answering with the framework's value. A measurement host that loaded more
// would resolve keys the recorded runtime refuses.
int LoadThemeResources(ThemeResourceLibrary& library, const std::string& path,
                       ResourceLayer top_layer = ResourceLayer::XamlControlsResources);

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

// The literal `value` supplies for `property`, after checking that its shape
// can. Throws MarkupError naming both when it cannot -- an x:String does not
// satisfy a Width whether it arrived through a dictionary or was written in
// place as <Border.Width><x:String>60</x:String></Border.Width>.
//
// Split out of ResolveResource so that both routes to a property run one check.
// A primitive written as an object element is the same value as the same
// primitive in a dictionary; only the lookup differs, and only one of the two
// does a lookup at all.
std::string ValueForProperty(const ResourceValue& value, const std::string& property);

// Looks `key` up and returns its literal text, after checking that the
// resource's shape can supply `property`.
std::string ResolveResource(const ResourceScope& scope, const std::string& key,
                            const std::string& property);

// The dictionary entry a `{StaticResource ...}` names, in either spelling,
// checked against what `property` can take.
//
// Separate from ResolveAttributeValue because a Style has no literal text: a
// property whose value is an object needs the entry itself, and every other
// caller wants the string out of it. Both go through here so that a missing
// key, a wrong-shaped key and a malformed extension are refused once and in
// the same words. Throws MarkupError if `raw` is not a markup extension at
// all -- the caller decides what a plain literal means before asking.
const ResourceValue& ResolveResourceReference(const ResourceScope& scope,
                                              const std::string& property,
                                              const std::string& raw);

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
