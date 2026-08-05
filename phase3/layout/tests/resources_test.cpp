// What the resource system does when it cannot do what was asked.
//
// The corpus checks the cases that work. This checks the ones that do not, and
// it exists because those are the cases a corpus cannot check: a case that is
// supposed to fail to load has no measurement, so there is nothing for
// check_layout.py to compare and nothing for the oracle to record beyond an
// error string.
//
// The property being tested throughout is that a lookup which cannot be
// satisfied *says so, by name*. A resource system that quietly left Width
// unset instead would produce a layout that is merely wrong -- and wrong in a
// way no number in the corpus could be traced back to a missing key.
//
// Deliberately not a framework. The whole file is assertions about the text of
// an exception, and pulling in a test runner to hold them would be more moving
// parts than the thing under test.

#include <cstdio>
#include <string>
#include <vector>

#include "markup.h"
#include "markup_tree.h"
#include "resources.h"

namespace {

int failures = 0;
int checks = 0;

const char* kPresentation = "http://schemas.microsoft.com/winfx/2006/xaml/presentation";
const char* kXaml = "http://schemas.microsoft.com/winfx/2006/xaml";

std::string Document(const std::string& root_tag, const std::string& attributes,
                     const std::string& body) {
    std::string head = "<" + root_tag + " xmlns=\"" + kPresentation + "\" xmlns:x=\"" + kXaml + "\"";
    if (!attributes.empty()) head += " " + attributes;
    return head + ">" + body + "</" + root_tag + ">";
}

void Fail(const std::string& what, const std::string& detail) {
    ++failures;
    std::printf("FAIL %s\n     %s\n", what.c_str(), detail.c_str());
}

// The markup must be rejected, and the rejection must contain `expected`. Both
// halves matter: a load that fails for an unrelated reason would otherwise
// count as this test passing.
void Rejects(const std::string& what, const std::string& markup, const std::string& expected) {
    ++checks;
    try {
        openxaml::LoadMarkup(markup);
    } catch (const std::exception& error) {
        const std::string message = error.what();
        if (message.find(expected) == std::string::npos)
            Fail(what, "rejected, but with \"" + message + "\" rather than \"" + expected + "\"");
        return;
    }
    Fail(what, "loaded, and should not have");
}

void Loads(const std::string& what, const std::string& markup) {
    ++checks;
    try {
        openxaml::LoadMarkup(markup);
    } catch (const std::exception& error) {
        Fail(what, std::string("refused it: ") + error.what());
    }
}

// The resolved value has to be the one the dictionary holds, which is a
// separate question from whether the load succeeded.
//
// Read off the innermost element, which is where every case below puts the
// Width under test -- the enclosing elements are only there to carry the
// dictionaries.
void ResolvesWidth(const std::string& what, const std::string& markup, double expected) {
    ++checks;
    try {
        const openxaml::MarkupNode root = openxaml::ParseMarkup(markup);
        const openxaml::MarkupNode* node = &root;
        while (!node->children.empty()) node = &node->children.front();
        if (node->width != expected) {
            Fail(what, "Width resolved to " + std::to_string(node->width) + ", wanted " +
                           std::to_string(expected));
        }
    } catch (const std::exception& error) {
        Fail(what, std::string("refused it: ") + error.what());
    }
}

const std::string kDouble = "<x:Double x:Key=\"BoxWidth\">60</x:Double>";
const std::string kUse = "<Border Width=\"{StaticResource BoxWidth}\" Height=\"30\"/>";

std::string Resources(const std::string& owner, const std::string& entries) {
    return "<" + owner + ".Resources>" + entries + "</" + owner + ".Resources>";
}

// --- the lookup itself --------------------------------------------------------

void MissingKeys() {
    Rejects("a key that is in no dictionary",
            Document("Border", "", Resources("Border", kDouble) +
                                       "<Border Width=\"{StaticResource NotThere}\"/>"),
            "resource 'NotThere' not found");

    Rejects("a key with no dictionary anywhere in the tree",
            Document("Border", "", kUse),
            "resource 'BoxWidth' not found");

    // Scope is the ancestor chain, not the document. A dictionary on a sibling
    // is out of reach, and has to say so rather than being searched because it
    // happens to be in the same file.
    Rejects("a sibling's dictionary is not in scope",
            Document("StackPanel", "",
                     "<Border>" + Resources("Border", kDouble) + "</Border>" + kUse),
            "resource 'BoxWidth' not found");

    Rejects("an alias to a key that does not exist",
            Document("Border", "",
                     Resources("Border",
                               "<StaticResource x:Key=\"BoxWidth\" ResourceKey=\"Missing\"/>") +
                         kUse),
            "resource 'Missing' not found");

    Rejects("a <StaticResource> element naming nothing that exists",
            Document("Border", "",
                     Resources("Border", kDouble) +
                         "<Border><Border.Width><StaticResource ResourceKey=\"Nope\"/>"
                         "</Border.Width></Border>"),
            "resource 'Nope' not found");
}

// --- types --------------------------------------------------------------------

void Types() {
    Rejects("a string cannot supply a length",
            Document("Border", "",
                     Resources("Border", "<x:String x:Key=\"BoxWidth\">wide</x:String>") + kUse),
            "cannot supply 'Width'");

    Rejects("a Thickness cannot supply a length either, even a one-number one",
            Document("Border", "",
                     Resources("Border", "<Thickness x:Key=\"BoxWidth\">60</Thickness>") + kUse),
            "cannot supply 'Width'");

    Rejects("a double cannot supply an Int32 attached property",
            Document("Grid", "",
                     Resources("Grid", "<x:Double x:Key=\"Column\">1</x:Double>") +
                         "<Border Grid.Column=\"{StaticResource Column}\"/>"),
            "cannot supply 'Grid.Column'");

    // The one conversion XAML does make on the way into a property.
    Loads("an Int32 widens to a Double",
          Document("Border", "",
                   Resources("Border", "<x:Int32 x:Key=\"BoxWidth\">60</x:Int32>") + kUse));

    Rejects("a resource type outside the implemented set",
            Document("Border", "",
                     Resources("Border", "<SolidColorBrush x:Key=\"BoxWidth\"/>") + kUse),
            "the resource type 'SolidColorBrush' is not implemented");
}

// --- dictionaries -------------------------------------------------------------

void Dictionaries() {
    Rejects("an entry with no x:Key",
            Document("Border", "", Resources("Border", "<x:Double>60</x:Double>") + kUse),
            "needs an x:Key");

    Rejects("the same key twice in one dictionary",
            Document("Border", "", Resources("Border", kDouble + kDouble) + kUse),
            "declared twice in one dictionary");

    Rejects("an entry carrying a property as well as its key",
            Document("Border", "",
                     Resources("Border", "<x:Double x:Key=\"BoxWidth\" Width=\"3\">60</x:Double>") +
                         kUse),
            "takes only x:Key");

    Rejects("a <Resources> section that is not the open element's",
            Document("Border", "", "<Grid.Resources/>"),
            "is not a property of the open <Border>");

    // Shadowing is the rule this one exercises; getting it backwards would
    // still load, and would still be wrong.
    ResolvesWidth("the innermost dictionary wins",
                  Document("StackPanel", "",
                           Resources("StackPanel", kDouble) + "<Border>" +
                               Resources("Border", "<x:Double x:Key=\"BoxWidth\">100</x:Double>") +
                               kUse + "</Border>"),
                  100.0);
}

// --- markup extensions --------------------------------------------------------

void Extensions() {
    Rejects("an extension that is not implemented",
            Document("Border", "", "<Border Width=\"{Binding Width}\"/>"),
            "the markup extension '{Binding}' is not implemented");

    Rejects("a binding is not a resource",
            Document("Border", "", "<Border Width=\"{x:Bind Width}\"/>"),
            "the markup extension '{x:Bind}' is not implemented");

    Rejects("{StaticResource} with no key",
            Document("Border", "", "<Border Width=\"{StaticResource}\"/>"),
            "names no resource key");

    Rejects("a named argument that is not ResourceKey",
            Document("Border", "",
                     Resources("Border", kDouble) + "<Border Width=\"{StaticResource Key=BoxWidth}\"/>"),
            "has no argument named 'Key'");

    Rejects("an unclosed extension",
            Document("Border", "", "<Border Width=\"{StaticResource BoxWidth\"/>"),
            "is not closed");

    Rejects("more arguments than {StaticResource} takes",
            Document("Border", "",
                     Resources("Border", kDouble) +
                         "<Border Width=\"{StaticResource BoxWidth, Other}\"/>"),
            "takes one argument");

    // The escape has to survive as text, not be read as a lookup and not be
    // handed to a property with the braces still on it.
    ResolvesWidth("{} escapes a leading brace",
                  Document("Border", "", "<Border Width=\"{}60\" Height=\"30\"/>"), 60.0);

    ResolvesWidth("the named form resolves to the same value as the positional one",
                  Document("Border", "",
                           Resources("Border", kDouble) +
                               "<Border Width=\"{StaticResource ResourceKey=BoxWidth}\"/>"),
                  60.0);
}

// --- property elements --------------------------------------------------------

void PropertyElements() {
    Rejects("a property element given no value",
            Document("Border", "",
                     Resources("Border", kDouble) + "<Border><Border.Width></Border.Width></Border>"),
            "was given no value");

    Rejects("a property element given two",
            Document("Border", "",
                     Resources("Border", kDouble) +
                         "<Border><Border.Width><StaticResource ResourceKey=\"BoxWidth\"/>"
                         "<StaticResource ResourceKey=\"BoxWidth\"/></Border.Width></Border>"),
            "was given a second value");

    Rejects("<StaticResource> with no ResourceKey",
            Document("Border", "",
                     Resources("Border", kDouble) +
                         "<Border><Border.Width><StaticResource/></Border.Width></Border>"),
            "needs a ResourceKey");

    Rejects("a property element holding something other than a lookup",
            Document("Border", "", "<Border><Border.Width><Border/></Border.Width></Border>"),
            "takes a <StaticResource>");

    Rejects("a property that does not exist, reached through an element",
            Document("Border", "",
                     Resources("Border", kDouble) +
                         "<Border><Border.Nonsense><StaticResource ResourceKey=\"BoxWidth\"/>"
                         "</Border.Nonsense></Border>"),
            "the property 'Nonsense' was not found");

    // An element reading its own dictionary, which is the case that only the
    // element form can express -- an attribute is read before the dictionary is.
    ResolvesWidth("an element resolves against its own dictionary",
                  Document("Border",
                           "", Resources("Border", kDouble) +
                                   "<Border.Width><StaticResource ResourceKey=\"BoxWidth\"/>"
                                   "</Border.Width>"),
                  60.0);
}

// --- the application dictionary -----------------------------------------------

// Everything above runs with no application dictionary loaded, which is the
// bare-checkout state and the one the failure messages above describe. These
// load one, so both halves are covered by the same file: what the tail of the
// chain resolves, and that it is still the tail.
void ApplicationScope() {
    using openxaml::ResourceValue;
    using openxaml::ValueKind;

    openxaml::ResourceDictionary theme;
    theme.Add("BoxWidth", ResourceValue{"x:Double", "80", ValueKind::Number});
    theme.Add("ThemeWidth", ResourceValue{"x:Double", "44", ValueKind::Number});
    theme.Add("ThemePad", ResourceValue{"Thickness", "1,2,3,4", ValueKind::Thickness});
    theme.Add("ThemeFill", ResourceValue{"SolidColorBrush", "#FF1F1F1F", ValueKind::Brush});
    theme.Add("ThemeInk", ResourceValue{"Color", "#FF1F1F1F", ValueKind::Color});
    openxaml::ThemeResourceLibrary::Default().Add("Default", std::move(theme));

    ResolvesWidth("{ThemeResource} resolves against the application dictionary",
                  Document("Border", "", "<Border Width=\"{ThemeResource ThemeWidth}\"/>"), 44.0);

    // The two extensions are one operation here, and a case that says so has to
    // keep saying so: a corpus twin pairs the resolved form with an inlined
    // literal, and it would pair with the wrong one if the spellings diverged.
    ResolvesWidth("{StaticResource} reaches the same dictionary",
                  Document("Border", "", "<Border Width=\"{StaticResource ThemeWidth}\"/>"), 44.0);

    ResolvesWidth("the element form of {ThemeResource} resolves too",
                  Document("Border", "",
                           "<Border.Width><ThemeResource ResourceKey=\"ThemeWidth\"/>"
                           "</Border.Width>"),
                  44.0);

    // The application dictionary is the *last* place looked, not the first.
    ResolvesWidth("markup shadows the application dictionary",
                  Document("Border", "", Resources("Border", kDouble) + kUse), 60.0);

    Loads("a brush resource supplies a Background",
          Document("Border", "", "<Border Background=\"{ThemeResource ThemeFill}\" Width=\"5\"/>"));

    // A Color and a Brush are spelled identically and are not interchangeable;
    // the extracted dictionary is full of both, so this is the check that keeps
    // the database from resolving something the runtime would refuse.
    Rejects("a Color cannot supply a Background",
            Document("Border", "", "<Border Background=\"{ThemeResource ThemeInk}\"/>"),
            "which cannot supply 'Background'");

    Rejects("a Thickness still cannot supply a Width",
            Document("Border", "", "<Border Width=\"{ThemeResource ThemePad}\"/>"),
            "cannot supply 'Width'");

    Rejects("a key the application dictionary does not have still fails by name",
            Document("Border", "", "<Border Width=\"{ThemeResource NotInWinUI}\"/>"),
            "resource 'NotInWinUI' not found");

    ++checks;
    try {
        openxaml::ThemeResourceLibrary::Default().SetActiveTheme("Chartreuse");
        Fail("a theme that is not loaded", "accepted it");
    } catch (const std::exception& error) {
        const std::string message = error.what();
        if (message.find("no theme dictionary for 'Chartreuse'") == std::string::npos)
            Fail("a theme that is not loaded", std::string("rejected with: ") + error.what());
    }
}

}  // namespace

int main() {
    MissingKeys();
    Types();
    Dictionaries();
    Extensions();
    PropertyElements();
    // Last, because it loads an application dictionary into the process and
    // every check above is written against there not being one.
    ApplicationScope();

    std::printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
