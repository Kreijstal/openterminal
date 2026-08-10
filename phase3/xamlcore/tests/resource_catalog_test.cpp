#include "resource_catalog.h"

#include <cstdlib>
#include <iostream>
#include <string>

#include "json.h"

namespace {

void Require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

}  // namespace

int main() {
    const std::string catalog = R"json({
      "schema_version": 2,
      "runtime_resources": {
        "Microsoft.Terminal.Control/Resources": {
          "Copy": "Copy selection",
          "Unicode": "Grüße 世界"
        },
        "Microsoft.Terminal.Settings.Model/Resources": {
          "Copy": "Copy"
        },
        "TerminalApp/Resources": {
          "NewTabRun.Text": "Open a new tab",
          "CommandButton.[using:Windows.UI.Xaml.Controls]ToolTipService.ToolTip": "Run",
          "Literal/Key": "literal slash",
          "Literal.Key": "canonical dot"
        }
      }
    })json";

    const auto parsed = openxaml::winrt::ParseResourceCatalog(catalog, "fixture");
    const auto* control = parsed.Find("Microsoft.Terminal.Control/Resources", "Copy");
    Require(control && *control == "Copy selection", "scope-specific value");
    const auto* settings = parsed.Find(
        "Microsoft.Terminal.Settings.Model/Resources", "Copy");
    Require(settings && *settings == "Copy", "same key in another scope");
    Require(parsed.Has("Microsoft.Terminal.Control/Resources", "Unicode"),
            "Unicode key is present");
    Require(parsed.Size("Microsoft.Terminal.Control/Resources") == 2,
            "scope size");
    const auto entries = parsed.Entries("Microsoft.Terminal.Control/Resources");
    Require(entries.size() == 2 && entries[0].first == "Copy" &&
                entries[1].first == "Unicode",
            "entries are deterministic");
    Require(!parsed.Has("Microsoft.Terminal.Control/Resources", "Missing"),
            "missing key");
    Require(parsed.Size("Missing/Resources") == 0, "missing scope");
    const auto* xaml_property = parsed.Find(
        "TerminalApp/Resources", "NewTabRun/Text");
    Require(xaml_property && *xaml_property == "Open a new tab",
            "XAML property slash resolves the .resw dot spelling");
    const auto* attached_property = parsed.Find(
        "TerminalApp/Resources",
        "CommandButton/[using:Windows.UI.Xaml.Controls]ToolTipService/ToolTip");
    Require(attached_property && *attached_property == "Run",
            "every attached-property path separator is canonicalized");
    const auto* literal_slash = parsed.Find(
        "TerminalApp/Resources", "Literal/Key");
    Require(literal_slash && *literal_slash == "literal slash",
            "an exact literal slash key wins over canonical fallback");

    bool rejected = false;
    try {
        (void)openxaml::winrt::ParseResourceCatalog(
            R"json({"schema_version":1,"runtime_resources":{}})json", "old");
    } catch (const openxaml::JsonError&) {
        rejected = true;
    }
    Require(rejected, "unsupported schema is rejected");

    std::cout << "resource catalog checks passed\n";
    return 0;
}
