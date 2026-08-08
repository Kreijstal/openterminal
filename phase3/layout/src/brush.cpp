#include "brush.h"

#include <cctype>
#include <map>
#include <string>

#include "markup.h"

namespace openxaml {
namespace {

// Only the names the corpus actually uses. XAML knows the whole SVG colour
// list; carrying all of it here would be a table nothing reads, and a name
// that is missing fails loudly rather than quietly, so the list can grow when
// a case needs it to.
//
// The values are the SVG/CSS named-colour list, which is what XAML's
// Colors class publishes. `Green` is the one worth spelling out: it is
// #FF008000 and not #FF00FF00 -- the bright one is `Lime`. Getting that wrong
// would paint a plausible colour that is not the runtime's, which is the
// failure mode this whole file exists to avoid. Of these seven only
// `Transparent` is reached by any case in the corpus; the rest are here so
// that markup naming them loads, and they paint what the list says.
const std::map<std::string, Color>& KnownColorNames() {
    static const std::map<std::string, Color> names = {
        {"Transparent", Color{0x00, 0x00, 0x00, 0x00}},
        {"Black", Color{0xff, 0x00, 0x00, 0x00}},
        {"White", Color{0xff, 0xff, 0xff, 0xff}},
        {"Red", Color{0xff, 0xff, 0x00, 0x00}},
        {"Green", Color{0xff, 0x00, 0x80, 0x00}},
        {"Blue", Color{0xff, 0x00, 0x00, 0xff}},
        {"Gray", Color{0xff, 0x80, 0x80, 0x80}},
    };
    return names;
}

bool IsHexDigit(char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; }

unsigned char HexValue(char c) {
    if (c >= '0' && c <= '9') return static_cast<unsigned char>(c - '0');
    if (c >= 'a' && c <= 'f') return static_cast<unsigned char>(c - 'a' + 10);
    return static_cast<unsigned char>(c - 'A' + 10);
}

// A single hex digit stands for a channel with both nibbles set to it, so #f00
// is #ff0000 and not #f00000. That is the rule every implementation of the
// short form uses, and it is the only one that makes #fff white.
unsigned char Nibble(char c) {
    const unsigned char v = HexValue(c);
    return static_cast<unsigned char>(v * 16 + v);
}

unsigned char Byte(char hi, char lo) {
    return static_cast<unsigned char>(HexValue(hi) * 16 + HexValue(lo));
}

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

Color ParseColor(const std::string& text, const std::string& where) {
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
        switch (digits.size()) {
            case 3:
                return Color{0xff, Nibble(digits[0]), Nibble(digits[1]), Nibble(digits[2])};
            case 4:
                return Color{Nibble(digits[0]), Nibble(digits[1]), Nibble(digits[2]),
                             Nibble(digits[3])};
            case 6:
                return Color{0xff, Byte(digits[0], digits[1]), Byte(digits[2], digits[3]),
                             Byte(digits[4], digits[5])};
            default:
                return Color{Byte(digits[0], digits[1]), Byte(digits[2], digits[3]),
                             Byte(digits[4], digits[5]), Byte(digits[6], digits[7])};
        }
    }

    const auto found = KnownColorNames().find(text);
    if (found == KnownColorNames().end())
        throw MarkupError("the colour name \"" + text + "\" is not implemented");
    return found->second;
}

void ValidateColor(const std::string& text, const std::string& where) {
    ParseColor(text, where);
}

}  // namespace openxaml
