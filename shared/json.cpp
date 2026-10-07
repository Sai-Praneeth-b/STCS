// json.cpp -- see json.hpp.
#include "shared/json.hpp"

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "shared/utf8.hpp"

namespace stcs {

// ---------------------------------------------------------------- Json value

Json Json::boolean(bool value) {
    Json j;
    j.type_ = Type::Bool;
    j.bool_ = value;
    return j;
}

Json Json::integer(long long value) {
    Json j;
    j.type_ = Type::Int;
    j.int_ = value;
    return j;
}

Json Json::real(double value) {
    Json j;
    j.type_ = Type::Double;
    j.double_ = value;
    return j;
}

Json Json::text(std::string value) {
    Json j;
    j.type_ = Type::String;
    j.string_ = std::move(value);
    return j;
}

Json Json::array() {
    Json j;
    j.type_ = Type::Array;
    return j;
}

Json Json::object() {
    Json j;
    j.type_ = Type::Object;
    return j;
}

void Json::push_back(Json value) {
    values_.push_back(std::move(value));
}

const Json* Json::find(const std::string& key) const {
    if (type_ != Type::Object) {
        return nullptr;
    }
    for (std::size_t i = 0; i < keys_.size(); ++i) {
        if (keys_[i] == key) {
            return &values_[i];
        }
    }
    return nullptr;
}

Json& Json::set(const std::string& key, Json value) {
    for (std::size_t i = 0; i < keys_.size(); ++i) {
        if (keys_[i] == key) {
            values_[i] = std::move(value);
            return values_[i];
        }
    }
    keys_.push_back(key);
    values_.push_back(std::move(value));
    return values_.back();
}

// ------------------------------------------------------------- serialization

namespace {

void append_escaped_string(std::string& out, const std::string& s) {
    out.push_back('"');
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    // Remaining control characters become \u00XX so that no
                    // raw control byte (in particular no newline) is emitted.
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    out.push_back('"');
}

}  // namespace

void Json::dump_to(std::string& out) const {
    switch (type_) {
        case Type::Null:
            out += "null";
            break;
        case Type::Bool:
            out += bool_ ? "true" : "false";
            break;
        case Type::Int:
            out += std::to_string(int_);
            break;
        case Type::Double: {
            char buf[40];
            std::snprintf(buf, sizeof buf, "%.17g", double_);
            out += buf;
            break;
        }
        case Type::String:
            append_escaped_string(out, string_);
            break;
        case Type::Array: {
            out.push_back('[');
            for (std::size_t i = 0; i < values_.size(); ++i) {
                if (i > 0) out.push_back(',');
                values_[i].dump_to(out);
            }
            out.push_back(']');
            break;
        }
        case Type::Object: {
            out.push_back('{');
            for (std::size_t i = 0; i < values_.size(); ++i) {
                if (i > 0) out.push_back(',');
                append_escaped_string(out, keys_[i]);
                out.push_back(':');
                values_[i].dump_to(out);
            }
            out.push_back('}');
            break;
        }
    }
}

std::string Json::dump() const {
    std::string out;
    dump_to(out);
    return out;
}

// ------------------------------------------------------------------- parsing

namespace {

class Parser {
public:
    explicit Parser(const std::string& text) : text_(text) {}

    bool parse_document(Json& out, std::string& error) {
        skip_whitespace();
        if (!parse_value(out, 0)) {
            error = error_;
            return false;
        }
        skip_whitespace();
        if (pos_ != text_.size()) {
            error = "unexpected characters after the JSON value";
            return false;
        }
        return true;
    }

private:
    bool fail(const char* message) {
        error_ = message;
        return false;
    }

    void skip_whitespace() {
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    bool consume_literal(const char* literal) {
        std::size_t i = 0;
        while (literal[i] != '\0') {
            if (pos_ + i >= text_.size() || text_[pos_ + i] != literal[i]) {
                return false;
            }
            ++i;
        }
        pos_ += i;
        return true;
    }

    bool parse_value(Json& out, int depth) {
        if (depth > Json::kMaxDepth) {
            return fail("nesting is too deep");
        }
        if (pos_ >= text_.size()) {
            return fail("unexpected end of input");
        }
        const char c = text_[pos_];
        switch (c) {
            case '{': return parse_object(out, depth);
            case '[': return parse_array(out, depth);
            case '"': {
                std::string s;
                if (!parse_string(s)) return false;
                out = Json::text(std::move(s));
                return true;
            }
            case 't':
                if (!consume_literal("true")) return fail("invalid literal");
                out = Json::boolean(true);
                return true;
            case 'f':
                if (!consume_literal("false")) return fail("invalid literal");
                out = Json::boolean(false);
                return true;
            case 'n':
                if (!consume_literal("null")) return fail("invalid literal");
                out = Json();
                return true;
            default:
                if (c == '-' || (c >= '0' && c <= '9')) {
                    return parse_number(out);
                }
                return fail("unexpected character");
        }
    }

    bool parse_object(Json& out, int depth) {
        ++pos_;  // '{'
        out = Json::object();
        skip_whitespace();
        if (pos_ < text_.size() && text_[pos_] == '}') {
            ++pos_;
            return true;
        }
        while (true) {
            skip_whitespace();
            if (pos_ >= text_.size() || text_[pos_] != '"') {
                return fail("object key must be a string");
            }
            std::string key;
            if (!parse_string(key)) return false;
            if (out.find(key) != nullptr) {
                return fail("duplicate object key");
            }
            skip_whitespace();
            if (pos_ >= text_.size() || text_[pos_] != ':') {
                return fail("expected ':' after object key");
            }
            ++pos_;
            skip_whitespace();
            Json value;
            if (!parse_value(value, depth + 1)) return false;
            out.set(key, std::move(value));
            skip_whitespace();
            if (pos_ >= text_.size()) return fail("unterminated object");
            if (text_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (text_[pos_] == '}') {
                ++pos_;
                return true;
            }
            return fail("expected ',' or '}' in object");
        }
    }

    bool parse_array(Json& out, int depth) {
        ++pos_;  // '['
        out = Json::array();
        skip_whitespace();
        if (pos_ < text_.size() && text_[pos_] == ']') {
            ++pos_;
            return true;
        }
        while (true) {
            skip_whitespace();
            Json value;
            if (!parse_value(value, depth + 1)) return false;
            out.push_back(std::move(value));
            skip_whitespace();
            if (pos_ >= text_.size()) return fail("unterminated array");
            if (text_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (text_[pos_] == ']') {
                ++pos_;
                return true;
            }
            return fail("expected ',' or ']' in array");
        }
    }

    bool parse_hex4(std::uint32_t& value) {
        if (pos_ + 4 > text_.size()) return false;
        value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text_[pos_ + i];
            value <<= 4;
            if (c >= '0' && c <= '9') value |= static_cast<std::uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') value |= static_cast<std::uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') value |= static_cast<std::uint32_t>(c - 'A' + 10);
            else return false;
        }
        pos_ += 4;
        return true;
    }

    bool parse_unicode_escape(std::string& out) {
        std::uint32_t unit = 0;
        if (!parse_hex4(unit)) return fail("invalid \\u escape");
        if (unit >= 0xD800 && unit <= 0xDBFF) {
            // High surrogate: must be followed by \uDC00-\uDFFF.
            if (pos_ + 2 > text_.size() || text_[pos_] != '\\' || text_[pos_ + 1] != 'u') {
                return fail("unpaired surrogate in \\u escape");
            }
            pos_ += 2;
            std::uint32_t low = 0;
            if (!parse_hex4(low) || low < 0xDC00 || low > 0xDFFF) {
                return fail("unpaired surrogate in \\u escape");
            }
            unit = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
        } else if (unit >= 0xDC00 && unit <= 0xDFFF) {
            return fail("unpaired surrogate in \\u escape");
        }
        append_utf8(out, unit);
        return true;
    }

    bool parse_string(std::string& out) {
        ++pos_;  // opening quote
        out.clear();
        while (true) {
            if (pos_ >= text_.size()) return fail("unterminated string");
            const unsigned char c = static_cast<unsigned char>(text_[pos_]);
            if (c == '"') {
                ++pos_;
                return true;
            }
            if (c < 0x20) return fail("raw control character in string");
            if (c == '\\') {
                ++pos_;
                if (pos_ >= text_.size()) return fail("unterminated escape");
                const char e = text_[pos_++];
                switch (e) {
                    case '"':  out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/':  out.push_back('/'); break;
                    case 'b':  out.push_back('\b'); break;
                    case 'f':  out.push_back('\f'); break;
                    case 'n':  out.push_back('\n'); break;
                    case 'r':  out.push_back('\r'); break;
                    case 't':  out.push_back('\t'); break;
                    case 'u':
                        if (!parse_unicode_escape(out)) return false;
                        break;
                    default:
                        return fail("invalid escape sequence");
                }
                continue;
            }
            if (c < 0x80) {
                out.push_back(static_cast<char>(c));
                ++pos_;
                continue;
            }
            std::size_t next = pos_;
            std::uint32_t code_point = 0;
            if (!decode_utf8(text_, next, code_point)) {
                return fail("invalid UTF-8 in string");
            }
            out.append(text_, pos_, next - pos_);
            pos_ = next;
        }
    }

    bool parse_number(Json& out) {
        const std::size_t start = pos_;
        if (text_[pos_] == '-') ++pos_;
        if (pos_ >= text_.size()) return fail("invalid number");
        if (text_[pos_] == '0') {
            ++pos_;
        } else if (text_[pos_] >= '1' && text_[pos_] <= '9') {
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
        } else {
            return fail("invalid number");
        }
        bool is_integer = true;
        if (pos_ < text_.size() && text_[pos_] == '.') {
            is_integer = false;
            ++pos_;
            const std::size_t digits_start = pos_;
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
            if (pos_ == digits_start) return fail("invalid number");
        }
        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
            is_integer = false;
            ++pos_;
            if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) ++pos_;
            const std::size_t digits_start = pos_;
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
            if (pos_ == digits_start) return fail("invalid number");
        }

        const std::string token = text_.substr(start, pos_ - start);
        if (is_integer) {
            errno = 0;
            char* end = nullptr;
            const long long value = std::strtoll(token.c_str(), &end, 10);
            if (errno != ERANGE) {
                out = Json::integer(value);
                return true;
            }
            // Integer too large for 64 bits: fall through and keep it as a
            // double so that field validators reject it as "not an integer".
        }
        errno = 0;
        const double value = std::strtod(token.c_str(), nullptr);
        if (!std::isfinite(value)) return fail("number out of range");
        out = Json::real(value);
        return true;
    }

    const std::string& text_;
    std::size_t pos_ = 0;
    std::string error_;
};

}  // namespace

bool Json::parse(const std::string& text, Json& out, std::string& error) {
    Parser parser(text);
    Json result;
    if (!parser.parse_document(result, error)) {
        return false;
    }
    out = std::move(result);
    return true;
}

}  // namespace stcs
