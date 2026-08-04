#include "json.h"

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
                    if (position_ + 4 > text_.size()) Fail("truncated \\u escape");
                    const int code =
                        std::stoi(text_.substr(position_, 4), nullptr, 16);
                    position_ += 4;
                    // The corpus is ASCII; anything above it would need real
                    // UTF-16 surrogate handling, so say so rather than guess.
                    if (code > 0x7f) Fail("non-ASCII \\u escape is not supported");
                    out += static_cast<char>(code);
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
