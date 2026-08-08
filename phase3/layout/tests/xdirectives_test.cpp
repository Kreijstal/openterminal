// The x: directives, and the shape of the tree each one leaves behind.
//
// Two things here cannot be a corpus case, and each for its own reason.
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

    ChildWidth("XAML trims the whitespace an indented primitive is written with",
               Document("Border", "",
                        "<Border Height=\"30\"><Border.Width><x:Double>\n  60\n  </x:Double>"
                        "</Border.Width></Border>"),
               60.0);

    // There is no widening. L5-xprimitives-int32-width is recorded as a load
    // failure -- the runtime refused it with 0x802b000a -- so an Int32 does not
    // reach a Double property even though every value it can hold would fit.
    // Its Integer properties are a different question and are answered the
    // other way: L5-resources-int32-attached measures an x:Int32 into
    // Grid.Column and Grid.ColumnSpan.
    Rejects("an x:Int32 does not satisfy a Width",
            Document("Border", "",
                     "<Border Height=\"30\"><Border.Width><x:Int32>60</x:Int32>"
                     "</Border.Width></Border>"),
            "<x:Int32> cannot supply 'Width'");

    ChildWidth("and the same value written as an attribute is still a Double",
               Document("Border", "", "<Border Height=\"30\" Width=\"60\"/>"), 60.0);

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
    // The runtime answered, and the answer is that a XamlReader load does not
    // defer at all. L5-xdirectives-load-false-sibling records a StackPanel with
    // three 18-high children as 54 high, and load-false-only-child records the
    // deferring child of a Border at its full 30 x 18. Deferral is the XAML
    // compiler's, and there is no compiler in front of a string.
    HasChildren("x:Load=\"False\" is understood and does not defer anything",
                Document("StackPanel", "",
                         "<Border Width=\"10\" Height=\"10\"/>"
                         "<Border x:Load=\"False\" Width=\"10\" Height=\"10\"/>"),
                2);

    HasChildren("x:Load=\"True\" is the ordinary case and changes nothing",
                Document("StackPanel", "",
                         "<Border x:Load=\"True\" Width=\"10\" Height=\"10\"/>"),
                1);

    HasChildren("x:DeferLoadStrategy=\"Lazy\" is the older spelling of the same thing",
                Document("StackPanel", "",
                         "<Border x:DeferLoadStrategy=\"Lazy\" Width=\"10\" Height=\"10\"/>"),
                1);

    // The subtree stays with it, because it stays.
    HasChildren("the children of a deferring element load with it",
                Document("StackPanel", "",
                         "<Border x:Load=\"False\"><StackPanel><Border Width=\"10\"/>"
                         "</StackPanel></Border>"),
                1);

    HasChildren("a deferring element inside a realised one loads too",
                Document("StackPanel", "",
                         "<Border><StackPanel><Border x:Load=\"False\" Width=\"10\"/>"
                         "</StackPanel></Border>"),
                1);

    // No case records a deferring root, but it takes the same rule: the
    // directive instructs a compiler that is not here, so the root loads.
    HasChildren("a deferring root loads, like every other deferring element",
                Document("Border", "x:Load=\"False\"", "<Border Width=\"10\"/>"), 1);

    // The value is still read. A directive whose value this parser cannot make
    // sense of is a named refusal rather than a silently ignored attribute --
    // the runtime is not recorded refusing these, but a wrong number is worse
    // than a refusal, and "x:Load=0" quietly meaning nothing hides a typo.
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
    Rejects("x:Bind without a generated source",
            Document("StackPanel", "", "<Border Width=\"{x:Bind W}\"/>"),
            "the binding path 'W' has no data source");
    HasChildren("x:DataType metadata",
                Document("StackPanel", "", "<Border x:DataType=\"local:Profile\"/>"), 1);
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
