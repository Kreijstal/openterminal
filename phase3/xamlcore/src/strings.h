// UTF-8 to UTF-16 and back.
//
// The layout core carries text as UTF-8 and the ABI carries it as UTF-16, so
// something has to convert. Doing it here rather than through
// WideCharToMultiByte keeps the DLL's imports to what it already had.
//
// In a header of its own because both the element projection and the property
// projection need it, and neither is the natural owner of the other.

#ifndef OPENXAML_STRINGS_H
#define OPENXAML_STRINGS_H

#include "sdk.h"

#include <string>

namespace openxaml::winrt {

inline std::string Utf8FromHString(HSTRING text) {
    UINT32 length = 0;
    const wchar_t* buffer = WindowsGetStringRawBuffer(text, &length);
    std::string out;
    for (UINT32 index = 0; index < length; ++index) {
        char32_t code = buffer[index];
        // A surrogate pair is one codepoint written as two UTF-16 units.
        if (code >= 0xD800 && code <= 0xDBFF && index + 1 < length &&
            buffer[index + 1] >= 0xDC00 && buffer[index + 1] <= 0xDFFF) {
            code = 0x10000 + ((code - 0xD800) << 10) + (buffer[++index] - 0xDC00);
        }
        if (code < 0x80) {
            out += static_cast<char>(code);
        } else if (code < 0x800) {
            out += static_cast<char>(0xC0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else if (code < 0x10000) {
            out += static_cast<char>(0xE0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (code >> 18));
            out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        }
    }
    return out;
}

inline HRESULT HStringFromUtf8(const std::string& text, HSTRING* out) {
    if (!out) return E_POINTER;
    std::wstring wide;
    size_t index = 0;
    while (index < text.size()) {
        const auto lead = static_cast<unsigned char>(text[index]);
        size_t extra = lead < 0x80 ? 0 : (lead & 0xE0) == 0xC0 ? 1 : (lead & 0xF0) == 0xE0 ? 2 : 3;
        char32_t code = lead < 0x80 ? lead : lead & (0x3F >> extra);
        if (index + extra >= text.size()) return E_INVALIDARG;
        for (size_t step = 1; step <= extra; ++step)
            code = (code << 6) | (static_cast<unsigned char>(text[index + step]) & 0x3F);
        index += extra + 1;
        if (code < 0x10000) {
            wide += static_cast<wchar_t>(code);
        } else {
            code -= 0x10000;
            wide += static_cast<wchar_t>(0xD800 + (code >> 10));
            wide += static_cast<wchar_t>(0xDC00 + (code & 0x3FF));
        }
    }
    return WindowsCreateString(wide.c_str(), static_cast<UINT32>(wide.size()), out);
}

}  // namespace openxaml::winrt

#endif  // OPENXAML_STRINGS_H
