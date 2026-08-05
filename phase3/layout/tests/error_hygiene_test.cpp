// Whether a recorded error message belongs to the case it was recorded on.
//
// Run 31017111065 wrote 19 failures into the database and 11 of them named a
// resource key the failing case's markup does not contain -- descriptions left
// on the thread by an earlier XamlReader::Load, served to a later failure with
// a matching HRESULT. phase3/harness/error_hygiene.h is the guard behind the
// probe's RoClearError; the probe builds only on Windows, so this is where the
// guard is actually run.
//
// The cases below are the real strings out of that run, so a regression here is
// the same regression rather than an invented one.
//
// Deliberately not a framework, like the tests beside it.

#include <cstdio>
#include <string>

#include "error_hygiene.h"

namespace {

int failures = 0;
int checks = 0;

void Expect(const std::string& what, const std::string& got, const std::string& want) {
    ++checks;
    if (got == want) return;
    ++failures;
    std::printf("FAIL %s\n     got  %s\n     want %s\n",
                what.c_str(), got.c_str(), want.c_str());
}

void ExpectTrue(const std::string& what, bool got) {
    Expect(what, got ? "true" : "false", "true");
}

void ExpectFalse(const std::string& what, bool got) {
    Expect(what, got ? "true" : "false", "false");
}

const char kXmlns[] =
    "xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\" "
    "xmlns:x=\"http://schemas.microsoft.com/winfx/2006/xaml\"";

}  // namespace

int main() {
    using openxaml_harness::HygienicError;
    using openxaml_harness::NamesAnAbsentResourceKey;
    using openxaml_harness::ResourceKeyNamedBy;

    // --- the key a message names --------------------------------------------
    Expect("the key is read out of the message",
           ResourceKeyNamedBy(
               "Cannot find a Resource with the Name/Key BoxWidth "
               "[Line: 1 Position: 138]"),
           "BoxWidth");
    Expect("the position suffix is not part of the key",
           ResourceKeyNamedBy(
               "Cannot find a Resource with the Name/Key "
               "ScrollBarVerticalThumbMinHeight[Line: 1 Position: 173]"),
           "ScrollBarVerticalThumbMinHeight");
    Expect("a key at the very end of the message reads whole",
           ResourceKeyNamedBy("Cannot find a Resource with the Name/Key BoxWidth"),
           "BoxWidth");
    Expect("a message with no key names none",
           ResourceKeyNamedBy(
               "Failed to assign to property "
               "'Windows.UI.Xaml.FrameworkElement.Width' because the type "
               "'Windows.Foundation.Int32' cannot be assigned to the type "
               "'Windows.Foundation.Double'. [Line: 1 Position: 322]"),
           "");
    Expect("an empty message names none", ResourceKeyNamedBy(""), "");

    // --- the case's own message is kept, exactly ----------------------------
    // L5-resources-forward-reference-child, the one failure in that run that
    // was genuinely its own: the key is declared after the reference to it.
    {
        std::string markup =
            std::string("<Border ") + kXmlns
            + "><Border Width=\"{StaticResource BoxWidth}\" Height=\"30\"/>"
              "<Border.Resources><x:Double x:Key=\"BoxWidth\">60</x:Double>"
              "</Border.Resources></Border>";
        std::string message =
            "Cannot find a Resource with the Name/Key BoxWidth "
            "[Line: 1 Position: 138]";
        ExpectFalse("a key the markup declares is not absent",
                    NamesAnAbsentResourceKey(markup, message));
        Expect("its own message is recorded verbatim",
               HygienicError(markup, message, 0x802B000A), message);
    }

    // A key that occurs only inside an attribute value -- which is where a
    // {ThemeResource} reference always lives -- counts as present. This is the
    // shape nearly every real lookup failure has, so a matcher that wanted a
    // token or an element name would call all of them stale.
    {
        std::string markup =
            "<Border xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/"
            "presentation\" Height=\"1\" VerticalAlignment=\"Bottom\" "
            "Background=\"{ThemeResource CardStrokeColorDefaultBrush}\"/>";
        std::string message =
            "Cannot find a Resource with the Name/Key "
            "CardStrokeColorDefaultBrush [Line: 1 Position: 113]";
        ExpectFalse("a key inside an attribute value is present",
                    NamesAnAbsentResourceKey(markup, message));
        Expect("so the message is kept",
               HygienicError(markup, message, 0x802B000A), message);
    }

    // --- a foreign message is replaced --------------------------------------
    // L5-xprimitives-int32-width as that run recorded it. The markup asks for
    // no resource whatsoever; the description belongs to L5-theme-double-height.
    {
        std::string markup =
            std::string("<Border ") + kXmlns
            + "><Border Height=\"30\"><Border.Width><x:Int32>60</x:Int32>"
              "</Border.Width></Border></Border>";
        std::string message =
            "Cannot find a Resource with the Name/Key "
            "ScrollBarVerticalThumbMinHeight [Line: 1 Position: 173]";
        ExpectTrue("a key the markup never mentions is absent",
                   NamesAnAbsentResourceKey(markup, message));
        std::string recorded = HygienicError(markup, message, 0x802B000A);
        ExpectFalse("the foreign description is not what gets recorded",
                    recorded == message);
        ExpectTrue("the hresult names the failure instead",
                   recorded.find("hresult 0x802b000a") != std::string::npos);
        ExpectTrue("and the record says the info was stale",
                   recorded.find("stale restricted error info") != std::string::npos);
        // Set aside, not deleted: it is still evidence, about another case.
        ExpectTrue("the suspect text is kept behind the label",
                   recorded.find(message) != std::string::npos);
    }

    // A harvested subtree failing on an authored case's key. Same verdict, and
    // the reason this matters at L7: a quarantined harvest candidate is only
    // worth anything if the blocker named is the blocker it hit.
    {
        std::string markup =
            "<Border xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/"
            "presentation\" Grid.Column=\"1\" Grid.ColumnSpan=\"2\" "
            "Height=\"1\" VerticalAlignment=\"Bottom\" "
            "Background=\"{ThemeResource CardStrokeColorDefaultBrush}\"/>";
        std::string message =
            "Cannot find a Resource with the Name/Key BoxWidth "
            "[Line: 1 Position: 149]";
        ExpectTrue("L7-terminal-09a589957c-s0 did not fail on BoxWidth",
                   NamesAnAbsentResourceKey(markup, message));
        ExpectFalse("so BoxWidth is not what gets recorded for it",
                    HygienicError(markup, message, 0x802B000A) == message);
    }

    // --- a message with no key shape is kept, whatever it says --------------
    // The other half of that run's contamination arrived as assignment
    // failures, which carry no key. Nothing here can tell those apart from a
    // real one, and guessing would be worse than the contamination: they are
    // kept, and only the probe clearing the thread's error state fixes them.
    {
        std::string markup =
            "<Border xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/"
            "presentation\" Width=\"400\" Height=\"180\"/>";
        std::string message =
            "Failed to assign to property "
            "'Windows.UI.Xaml.Controls.ColumnDefinition.Width' because the "
            "type 'Windows.Foundation.Double' cannot be assigned to the type "
            "'Windows.UI.Xaml.GridLength'. [Line: 1 Position: 206]";
        ExpectFalse("an assignment failure names no key",
                    NamesAnAbsentResourceKey(markup, message));
        Expect("and is recorded as it arrived",
               HygienicError(markup, message, 0x802B000A), message);
    }

    // An empty message is not this guard's business: the probe names it by
    // HRESULT before it gets here, and that behaviour has to keep working.
    Expect("an empty message passes through unchanged",
           HygienicError("<Border/>", "", 0x80070490), "");

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
