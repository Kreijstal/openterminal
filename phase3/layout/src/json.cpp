#include "json.h"

#include <cctype>
#include <cstdlib>

namespace openxaml {
namespace {

class Parser {
public:
    explicit Parser(const std::string& text) : text_(text) {}

    JsonValue Parse() {
        JsonValue value = ParseValue();
        SkipWhitespace();
        if (position_ != text_.size()) Fail("trailing content");
        return value;
    }

private:
    [[noreturn]] void Fail(const std::string& what) const {
        throw JsonError(what + " at offset " + std::to_string(position_));
    }

    void SkipWhitespace() {
        while (position_ < text_.size() &&
               (text_[position_] == ' ' || text_[position_] == '\t' || text_[position_] == '\n' ||
                text_[position_] == '\r')) {
            ++position_;
        }
    }

    char Peek() {
        SkipWhitespace();
        if (position_ >= text_.size()) Fail("unexpected end of input");
        return text_[position_];
    }

    void Expect(char c) {
        if (Peek() != c) Fail(std::string("expected '") + c + "'");
        ++position_;
    }

    bool Literal(const char* word) {
        const size_t length = std::char_traits<char>::length(word);
        if (text_.compare(position_, length, word) != 0) return false;
        position_ += length;
        return true;
    }

    JsonValue ParseValue() {
        switch (Peek()) {
            case '{': return ParseObject();
            case '[': return ParseArray();
            case '"': {
                JsonValue value;
                value.kind = JsonValue::Kind::String;
                value.string = ParseString();
                return value;
            }
            case 't': case 'f': {
                JsonValue value;
                value.kind = JsonValue::Kind::Bool;
                if (Literal("true")) value.boolean = true;
                else if (Literal("false")) value.boolean = false;
                else Fail("bad literal");
                return value;
            }
            case 'n': {
                JsonValue value;
                if (!Literal("null")) Fail("bad literal");
                return value;
            }
            default: return ParseNumber();
        }
    }

    JsonValue ParseObject() {
        JsonValue value;
        value.kind = JsonValue::Kind::Object;
        Expect('{');
        if (Peek() == '}') { ++position_; return value; }
        for (;;) {
            const std::string key = ParseString();
            Expect(':');
            value.object[key] = ParseValue();
            const char c = Peek();
            ++position_;
            if (c == '}') return value;
            if (c != ',') Fail("expected ',' or '}'");
        }
    }

    JsonValue ParseArray() {
        JsonValue value;
        value.kind = JsonValue::Kind::Array;
        Expect('[');
        if (Peek() == ']') { ++position_; return value; }
        for (;;) {
            value.array.push_back(ParseValue());
            const char c = Peek();
            ++position_;
            if (c == ']') return value;
            if (c != ',') Fail("expected ',' or ']'");
        }
    }

    unsigned int ReadHex4() {
        if (position_ + 4 > text_.size()) Fail("truncated \\u escape");
        unsigned int value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text_[position_ + static_cast<size_t>(i)];
            if (!std::isxdigit(static_cast<unsigned char>(c))) Fail("a malformed \\u escape");
            value = value * 16 + static_cast<unsigned int>(
                                     c <= '9' ? c - '0'
                                              : (std::tolower(static_cast<unsigned char>(c)) - 'a' +
                                                 10));
        }
        position_ += 4;
        return value;
    }

    static void AppendUtf8(std::string& out, unsigned int code) {
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

    std::string ParseString() {
        Expect('"');
        std::string out;
        while (position_ < text_.size()) {
            const char c = text_[position_++];
            if (c == '"') return out;
            if (c != '\\') { out += c; continue; }
            if (position_ >= text_.size()) Fail("unterminated escape");
            const char escape = text_[position_++];
            switch (escape) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    // JSON escapes are UTF-16 code units, so a character
                    // outside the basic plane arrives as a surrogate pair and
                    // has to be recombined before it can be encoded. The
                    // corpus is no longer ASCII: an icon glyph is a private-use
                    // character, and refusing to read one would fail the case
                    // in the reader rather than in the layout that cannot
                    // measure it yet.
                    unsigned int code = ReadHex4();
                    if (code >= 0xD800 && code <= 0xDBFF) {
                        if (position_ + 2 > text_.size() || text_[position_] != '\\' ||
                            text_[position_ + 1] != 'u') {
                            Fail("a high surrogate with no low surrogate after it");
                        }
                        position_ += 2;
                        const unsigned int low = ReadHex4();
                        if (low < 0xDC00 || low > 0xDFFF) Fail("a malformed surrogate pair");
                        code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                    } else if (code >= 0xDC00 && code <= 0xDFFF) {
                        Fail("a low surrogate with no high surrogate before it");
                    }
                    AppendUtf8(out, code);
                    break;
                }
                default: out += escape;
            }
        }
        Fail("unterminated string");
    }

    JsonValue ParseNumber() {
        SkipWhitespace();
        const size_t start = position_;
        if (position_ < text_.size() && (text_[position_] == '-' || text_[position_] == '+'))
            ++position_;
        while (position_ < text_.size() &&
               (std::isdigit(static_cast<unsigned char>(text_[position_])) ||
                text_[position_] == '.' || text_[position_] == 'e' || text_[position_] == 'E' ||
                text_[position_] == '-' || text_[position_] == '+')) {
            ++position_;
        }
        if (position_ == start) Fail("expected a value");
        JsonValue value;
        value.kind = JsonValue::Kind::Number;
        value.number = std::strtod(text_.substr(start, position_ - start).c_str(), nullptr);
        return value;
    }

    const std::string& text_;
    size_t position_ = 0;
};

}  // namespace

const JsonValue& JsonValue::At(const std::string& key) const {
    auto found = object.find(key);
    if (found == object.end()) throw JsonError("missing field \"" + key + "\"");
    return found->second;
}

JsonValue ParseJson(const std::string& text) { return Parser(text).Parse(); }

std::string JsonEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (c == '\n') {
            out += "\\n";
        } else {
            out += c;
        }
    }
    return out;
}

}  // namespace openxaml
