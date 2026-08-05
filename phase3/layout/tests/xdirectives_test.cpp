// The x: directives, and the shape of the tree each one leaves behind.
//
// Three things here cannot be a corpus case, and each for its own reason.
//
// **Deferral.** An element with x:Load="False" is supposed to be absent. Absent
// and "present but zero-sized" are different trees, and the corpus compares
// trees node by node -- but only against a *recorded* measurement, and the
// runtime has not answered this one yet. Until it does, the corpus asks the
// question (L5-xdirectives-*) and this file pins what we do in the meantime, so
// that "provisional" means a decision that is written down rather than one that
// drifts.
//
// **x:Uid with a table.** No case in the corpus loads a string table, because
// the oracle probe has no resource map and a case measured against a table one
// side has and the other does not would disagree by construction. So the only
// place the lookup runs at all is here.
//
// **Refusals.** A markup that is supposed to fail has no measurement to compare.
//
// Deliberately not a framework, matching resources_test.cpp: the whole file is
// assertions about a tree or about the text of an exception.

#include <cstdio>
#include <string>

#include "markup.h"
#include "markup_tree.h"
#include "resw_strings.h"

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

// The root's child count after the parse. Deferral is only observable as an
// absence, so counting is the whole assertion.
void HasChildren(const std::string& what, const std::string& markup, size_t expected,
                 const openxaml::StringTable& strings = openxaml::NoStrings()) {
    ++checks;
    try {
        const openxaml::MarkupNode root = openxaml::ParseMarkup(markup, strings);
        if (root.children.size() != expected) {
            Fail(what, "the root has " + std::to_string(root.children.size()) +
                           " child(ren), wanted " + std::to_string(expected));
        }
    } catch (const std::exception& error) {
        Fail(what, std::string("refused it: ") + error.what());
    }
}

// A double read off the first child, which is where every case below puts the
// property under test.
void ChildWidth(const std::string& what, const std::string& markup, double expected) {
    ++checks;
    try {
        const openxaml::MarkupNode root = openxaml::ParseMarkup(markup);
        if (root.children.empty()) {
            Fail(what, "the root has no children");
            return;
        }
        if (root.children.front().width != expected) {
            Fail(what, "Width is " + std::to_string(root.children.front().width) + ", wanted " +
                           std::to_string(expected));
        }
    } catch (const std::exception& error) {
        Fail(what, std::string("refused it: ") + error.what());
    }
}

// --- x-primitives as object elements ------------------------------------------

void Primitives() {
    ChildWidth("an x:Double sets a Width written as a property element",
               Document("Border", "",
                        "<Border Height=\"30\"><Border.Width><x:Double>60</x:Double>"
                        "</Border.Width></Border>"),
               60.0);

    // The widening the resource path already commits to, reached the other way.
    ChildWidth("an x:Int32 widens into a Double property",
               Document("Border", "",
                        "<Border Height=\"30\"><Border.Width><x:Int32>60</x:Int32>"
                        "</Border.Width></Border>"),
               60.0);

    ChildWidth("XAML trims the whitespace an indented primitive is written with",
               Document("Border", "",
                        "<Border Height=\"30\"><Border.Width><x:Double>\n  60\n  </x:Double>"
                        "</Border.Width></Border>"),
               60.0);

    // The check that makes this more than text substitution.
    Rejects("an x:String does not satisfy a Width",
            Document("Border", "",
                     "<Border><Border.Width><x:String>60</x:String></Border.Width></Border>"),
            "<x:String> cannot supply 'Width'");

    Rejects("an x:Double does not satisfy a Text",
            Document("Border", "",
                     "<TextBlock><TextBlock.Text><x:Double>60</x:Double></TextBlock.Text>"
                     "</TextBlock>"),
            "<x:Double> cannot supply 'Text'");

    Rejects("a primitive as a value carries no x:Key",
            Document("Border", "",
                     "<Border><Border.Width><x:Double x:Key=\"W\">60</x:Double></Border.Width>"
                     "</Border>"),
            "as a value takes no attributes");

    Rejects("two values in one property element",
            Document("Border", "",
                     "<Border><Border.Width><x:Double>60</x:Double><x:Double>70</x:Double>"
                     "</Border.Width></Border>"),
            "was given a second value");

    // An x-primitive is not a UIElement, so it cannot stand where one is
    // expected. The refusal names the type rather than silently adding nothing.
    Rejects("a primitive as an ordinary child element",
            Document("StackPanel", "", "<x:Double>60</x:Double>"),
            "the type 'x:Double' is not implemented");

    Rejects("an x-primitive this parser does not implement",
            Document("Border", "",
                     "<Border><Border.Width><x:Single>60</x:Single></Border.Width></Border>"),
            "takes a <StaticResource> or a primitive");
}

// --- x:Load and x:DeferLoadStrategy -------------------------------------------

void Deferral() {
    HasChildren("x:Load=\"False\" leaves the element out of the tree",
                Document("StackPanel", "",
                         "<Border Width=\"10\" Height=\"10\"/>"
                         "<Border x:Load=\"False\" Width=\"10\" Height=\"10\"/>"),
                1);

    HasChildren("x:Load=\"True\" is the ordinary case and changes nothing",
                Document("StackPanel", "",
                         "<Border x:Load=\"True\" Width=\"10\" Height=\"10\"/>"),
                1);

    HasChildren("x:DeferLoadStrategy=\"Lazy\" is the older spelling of the same thing",
                Document("StackPanel", "",
                         "<Border x:DeferLoadStrategy=\"Lazy\" Width=\"10\" Height=\"10\"/>"),
                0);

    // The subtree goes with it: it was attached to an element that was dropped.
    HasChildren("a deferred element takes its children with it",
                Document("StackPanel", "",
                         "<Border x:Load=\"False\"><StackPanel><Border Width=\"10\"/>"
                         "</StackPanel></Border>"),
                0);

    // Nothing here can bring one back, and the corpus has no code-behind to do
    // it, so a deferred element stays deferred for the whole measurement.
    HasChildren("a deferred element inside a realised one is still absent",
                Document("StackPanel", "",
                         "<Border><StackPanel><Border x:Load=\"False\" Width=\"10\"/>"
                         "</StackPanel></Border>"),
                1);

    Rejects("a deferred root realises nothing, and says so",
            Document("Border", "x:Load=\"False\"", "<Border Width=\"10\"/>"),
            "defers its own creation");

    Rejects("x:Load takes a Boolean",
            Document("StackPanel", "", "<Border x:Load=\"Maybe\"/>"),
            "x:Load takes True or False");

    Rejects("x:DeferLoadStrategy takes Lazy and nothing else",
            Document("StackPanel", "", "<Border x:DeferLoadStrategy=\"Eager\"/>"),
            "x:DeferLoadStrategy takes Lazy");

    Rejects("the two spellings may not contradict each other",
            Document("StackPanel", "",
                     "<Border x:Load=\"True\" x:DeferLoadStrategy=\"Lazy\"/>"),
            "disagree about whether it is realised");
}

// --- x:Uid --------------------------------------------------------------------

void Uid() {
    openxaml::StringTable table;
    table.Add("Caption", "Text", "M");
    table.Add("Caption", "FontSize", "24");
    table.Add("Sized", "Width", "60");

    // No table is the state every corpus case is in, and it is not an error:
    // a uid that resolves to nothing sets nothing.
    HasChildren("an x:Uid with no table loaded sets nothing and still loads",
                Document("StackPanel", "", "<Border x:Uid=\"Caption\" Width=\"10\"/>"), 1);

    HasChildren("a uid that is in no table sets nothing and still loads",
                Document("StackPanel", "", "<Border x:Uid=\"Absent\" Width=\"10\"/>"), 1,
                table);

    ++checks;
    try {
        const openxaml::MarkupNode root = openxaml::ParseMarkup(
            Document("StackPanel", "", "<Border x:Uid=\"Sized\" Height=\"30\"/>"), table);
        if (root.children.front().width != 60.0)
            Fail("a uid supplies a property the markup did not write",
                 "Width is " + std::to_string(root.children.front().width));
    } catch (const std::exception& error) {
        Fail("a uid supplies a property the markup did not write",
             std::string("refused it: ") + error.what());
    }

    // The provisional choice, pinned here so that changing it is a deliberate
    // edit rather than a drift: the uid's value wins over the local attribute.
    // L5-xdirectives-uid-precedence is the case that asks the runtime.
    ++checks;
    try {
        const openxaml::MarkupNode root = openxaml::ParseMarkup(
            Document("StackPanel", "", "<Border x:Uid=\"Sized\" Width=\"11\" Height=\"30\"/>"),
            table);
        if (root.children.front().width != 60.0) {
            Fail("the uid beats a local attribute (provisional)",
                 "Width is " + std::to_string(root.children.front().width) + ", wanted 60");
        }
    } catch (const std::exception& error) {
        Fail("the uid beats a local attribute (provisional)",
             std::string("refused it: ") + error.what());
    }

    // A property the parser cannot read is a named failure rather than a
    // dropped string, exactly as it would be written as an attribute. This is
    // most of what a real resw holds -- Content, Header, HelpText -- and saying
    // so by name is what keeps the coverage number honest.
    openxaml::StringTable unsupported;
    unsupported.Add("Labelled", "Content", "Save");
    ++checks;
    try {
        openxaml::ParseMarkup(Document("StackPanel", "", "<Border x:Uid=\"Labelled\"/>"),
                              unsupported);
        Fail("a uid property this parser has no home for", "loaded, and should not have");
    } catch (const std::exception& error) {
        const std::string message = error.what();
        if (message.find("'Content' was not found") == std::string::npos)
            Fail("a uid property this parser has no home for", message);
    }

    Rejects("an empty x:Uid names no resource",
            Document("StackPanel", "", "<Border x:Uid=\"\"/>"),
            "x:Uid names no resource");
}

// --- what stays blocked --------------------------------------------------------

void StillBlocked() {
    // The rule the whole file exists to protect: a directive that is not
    // implemented fails by its own name. Dropping one would change what the
    // markup means with nothing in any number to show for it.
    Rejects("x:Bind", Document("StackPanel", "", "<Border Width=\"{x:Bind W}\"/>"),
            "'{x:Bind}' is not implemented");
    Rejects("x:DataType", Document("StackPanel", "", "<Border x:DataType=\"local:Profile\"/>"),
            "the directive 'x:DataType' is not implemented");
    Rejects("x:Class", Document("StackPanel", "x:Class=\"App.Page\"", "<Border/>"),
            "the directive 'x:Class' is not implemented");
    Rejects("x:FieldModifier",
            Document("StackPanel", "", "<Border x:FieldModifier=\"public\"/>"),
            "the directive 'x:FieldModifier' is not implemented");

    // x:Name still works, and x:Key outside a dictionary still does not.
    HasChildren("x:Name is dropped, as it always was",
                Document("StackPanel", "", "<Border x:Name=\"Box\" Width=\"10\"/>"), 1);
    Rejects("x:Key outside a resource dictionary",
            Document("StackPanel", "", "<Border x:Key=\"Box\"/>"),
            "the directive 'x:Key' is not implemented");
}

}  // namespace

int main() {
    Primitives();
    Deferral();
    Uid();
    StillBlocked();

    std::printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
