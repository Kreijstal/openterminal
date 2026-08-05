// Reading text back out of a case file.
//
// This is separated from xaml_probe.cpp for one reason: the probe only builds
// on Windows, against WinRT, so nothing about it can be checked on the machine
// the corpus is written on -- and the one thing in it that is pure text
// handling had a bug that silently invalidated fifteen recorded measurements.
//
// Python's json writes every non-ASCII character as a \uXXXX escape, so a
// FontIcon's glyph arrives here as the six characters `\uE76C` and never as a
// character. An unescaper that dropped those, which this one did, produced markup with
// an empty Glyph -- and an empty Glyph measures perfectly happily. The result
// was fifteen level 7 FontIcon cases whose recorded desired width is their
// margin and nothing else, with no error anywhere to say so.
//
// Header-only and free of every dependency, so phase3/layout's test suite can
// include it and run this on Linux with the rest of ctest.

#ifndef OPENXAML_HARNESS_JSON_TEXT_H
#define OPENXAML_HARNESS_JSON_TEXT_H

#include <cstdint>
#include <string>

namespace openxaml_harness {

// A codepoint as UTF-8. The probe hands the result to winrt::to_hstring, which
// reads its input as UTF-8, so this is the encoding that gets the character
// through to XamlReader::Load intact.
inline void AppendUtf8(std::string& out, uint32_t codepoint) {
    if (codepoint < 0x80) {
        out += static_cast<char>(codepoint);
    } else if (codepoint < 0x800) {
        out += static_cast<char>(0xC0 | (codepoint >> 6));
        out += static_cast<char>(0x80 | (codepoint & 0x3F));
    } else if (codepoint < 0x10000) {
        out += static_cast<char>(0xE0 | (codepoint >> 12));
        out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (codepoint & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (codepoint >> 18));
        out += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (codepoint & 0x3F));
    }
}

inline uint32_t ParseHex4(const std::string& text, size_t at) {
    uint32_t value = 0;
    for (size_t i = at; i < at + 4; ++i) {
        char c = text[i];
        uint32_t digit;
        if (c >= '0' && c <= '9') digit = static_cast<uint32_t>(c - '0');
        else if (c >= 'a' && c <= 'f') digit = static_cast<uint32_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') digit = static_cast<uint32_t>(c - 'A' + 10);
        else return value;  // malformed escape; take what there was
        value = (value << 4) | digit;
    }
    return value;
}

// The escapes our generators emit, and nothing more: JSON's own set plus
// \uXXXX, including the surrogate pairs an astral character is written as.
inline std::string JsonUnescape(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\') { out += s[i]; continue; }
        if (++i >= s.size()) break;
        switch (s[i]) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'u': {
                if (i + 4 >= s.size()) break;
                uint32_t codepoint = ParseHex4(s, i + 1);
                i += 4;
                // A surrogate on its own is not a character. The two halves are
                // one, and only together.
                if (codepoint >= 0xD800 && codepoint <= 0xDBFF
                    && i + 6 < s.size() && s[i + 1] == '\\' && s[i + 2] == 'u') {
                    uint32_t low = ParseHex4(s, i + 3);
                    if (low >= 0xDC00 && low <= 0xDFFF) {
                        codepoint = 0x10000 + ((codepoint - 0xD800) << 10)
                                    + (low - 0xDC00);
                        i += 6;
                    }
                }
                AppendUtf8(out, codepoint);
                break;
            }
            default: out += s[i];  // \\ and \" and \/
        }
    }
    return out;
}

}  // namespace openxaml_harness

#endif  // OPENXAML_HARNESS_JSON_TEXT_H
