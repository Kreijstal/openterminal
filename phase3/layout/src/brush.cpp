#include "brush.h"

#include <cctype>
#include <map>
#include <set>
#include <string>

#include "markup.h"

namespace openxaml {
namespace {

// Only the names the corpus actually uses. XAML knows the whole SVG colour
// list; carrying all of it here would be a table nothing reads, and a name
// that is missing fails loudly rather than quietly, so the list can grow when
// a case needs it to.
const std::set<std::string>& KnownColorNames() {
    static const std::set<std::string> names = {
        "Transparent", "Black", "White", "Red", "Green", "Blue", "Gray",
    };
    return names;
}

bool IsHexDigit(char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; }

}  // namespace

std::string FullBrushTypeName(const std::string& short_name) {
    static const std::map<std::string, std::string> kBrushes = {
        {"SolidColorBrush", "Windows.UI.Xaml.Media.SolidColorBrush"},
        {"ImageBrush", "Windows.UI.Xaml.Media.ImageBrush"},
    };
    const auto found = kBrushes.find(short_name);
    if (found == kBrushes.end())
        throw MarkupError("the type '" + short_name + "' is not implemented");
    return found->second;
}

void ValidateColor(const std::string& text, const std::string& where) {
    if (text.empty()) throw MarkupError("an empty colour for " + where);

    if (text[0] == '#') {
        const std::string digits = text.substr(1);
        // #RGB, #ARGB, #RRGGBB and #AARRGGBB, which is the whole set XAML
        // accepts.
        if (digits.size() != 3 && digits.size() != 4 && digits.size() != 6 && digits.size() != 8)
            throw MarkupError("\"" + text + "\" is not a colour for " + where);
        for (char c : digits) {
            if (!IsHexDigit(c)) throw MarkupError("\"" + text + "\" is not a colour for " + where);
        }
        return;
    }

    if (KnownColorNames().count(text) == 0)
        throw MarkupError("the colour name \"" + text + "\" is not implemented");
}

}  // namespace openxaml
