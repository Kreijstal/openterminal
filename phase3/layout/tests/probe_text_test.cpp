// What the oracle probe reads out of a case file, checked where it can be.
//
// The probe itself only builds on Windows, against WinRT, so the machine the
// corpus is authored on can compile none of it. That is exactly why the one
// part of it that is pure text handling had a bug nobody caught: its \uXXXX
// unescaper dropped every codepoint at or above U+0080, and Python's json
// writes every non-ASCII character that way. A FontIcon's Glyph therefore
// arrived at XamlReader::Load empty -- and an empty Glyph measures perfectly
// happily, so fifteen recorded level 7 measurements are of an icon that was
// never there, with no error anywhere to say so.
//
// phase3/harness/json_text.h holds that code now, free of every dependency, so
// this file can include it and ctest can run it on Linux with everything else.
//
// Deliberately not a framework, like the tests beside it.

#include <cstdio>
#include <string>

#include "json_text.h"

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

std::string Utf8(unsigned codepoint) {
    std::string out;
    openxaml_harness::AppendUtf8(out, codepoint);
    return out;
}

}  // namespace

int main() {
    using openxaml_harness::JsonUnescape;

    // ASCII, which is all the corpus needed until FontIcon arrived.
    Expect("plain text", JsonUnescape("<Border/>"), "<Border/>");
    Expect("quote and backslash", JsonUnescape("a\\\"b\\\\c"), "a\"b\\c");
    Expect("newline", JsonUnescape("a\\nb"), "a\nb");
    Expect("ascii escape", JsonUnescape("\\u0041"), "A");

    // The regression. U+E76C is one of the glyphs Terminal's markup names, and
    // it has to survive as three bytes of UTF-8 rather than as nothing.
    Expect("a private-use glyph survives", JsonUnescape("\\ue76c"), Utf8(0xE76C));
    Expect("a glyph inside markup survives",
           JsonUnescape("<FontIcon Glyph=\\\"\\ue76c\\\"/>"),
           "<FontIcon Glyph=\"" + Utf8(0xE76C) + "\"/>");
    Expect("uppercase hex reads the same", JsonUnescape("\\uE76C"), Utf8(0xE76C));

    // The two-byte and three-byte boundaries, so an off-by-one in the encoder
    // shows up as a wrong byte count rather than as a wrong glyph much later.
    Expect("U+0080", JsonUnescape("\\u0080"), Utf8(0x80));
    Expect("U+07FF", JsonUnescape("\\u07ff"), Utf8(0x7FF));
    Expect("U+0800", JsonUnescape("\\u0800"), Utf8(0x800));
    Expect("U+FFFF", JsonUnescape("\\uffff"), Utf8(0xFFFF));

    // An astral character is written as a surrogate pair, and the two halves
    // are one character only together. Terminal has none today; a corpus that
    // takes each half separately would emit two invalid characters rather than
    // failing, which is the kind of silence this file exists to stop.
    Expect("a surrogate pair is one character",
           JsonUnescape("\\ud83d\\ude00"), Utf8(0x1F600));

    // Nothing here may run off the end of the string. A truncated escape is
    // malformed input we never emit, so what it produces matters less than
    // that it stays in bounds -- the digits come through as themselves.
    Expect("a truncated escape does not read past the end",
           JsonUnescape("abc\\u00"), "abc00");
    Expect("a trailing backslash does not read past the end",
           JsonUnescape("abc\\"), "abc");

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
