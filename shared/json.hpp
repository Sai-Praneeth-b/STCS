// json.hpp -- a small, strict JSON value type, parser and serializer.
//
// Why hand-written: the project forbids networking frameworks and the
// protocol only needs plain JSON objects, so a ~300-line module keeps the
// build dependency-free. The serializer ALWAYS escapes control characters, so
// dump() can never produce a raw newline -- this is what makes the
// newline-delimited framing rule from Phase 1 (section 7.1) safe.
//
// Parsing is strict (RFC 8259): no trailing commas, no comments, no NaN, no
// duplicate object keys, nesting limited to kMaxDepth, UTF-8 validated.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace stcs {

class Json {
public:
    enum class Type { Null, Bool, Int, Double, String, Array, Object };

    static constexpr int kMaxDepth = 32;

    Json() = default;  // null

    // Named constructors (avoid implicit-conversion ambiguity with literals).
    static Json boolean(bool value);
    static Json integer(long long value);
    static Json real(double value);
    static Json text(std::string value);
    static Json array();
    static Json object();

    Type type() const { return type_; }
    bool is_null() const { return type_ == Type::Null; }
    bool is_bool() const { return type_ == Type::Bool; }
    bool is_int() const { return type_ == Type::Int; }
    bool is_string() const { return type_ == Type::String; }
    bool is_array() const { return type_ == Type::Array; }
    bool is_object() const { return type_ == Type::Object; }

    bool bool_value() const { return bool_; }
    long long int_value() const { return int_; }
    const std::string& string_value() const { return string_; }

    // Arrays.
    const std::vector<Json>& items() const { return values_; }
    void push_back(Json value);

    // Objects (insertion order is preserved so wire output is stable).
    // find() returns nullptr when the key is absent.
    const Json* find(const std::string& key) const;
    Json& set(const std::string& key, Json value);
    const std::vector<std::string>& keys() const { return keys_; }
    std::size_t size() const { return values_.size(); }

    // Compact one-line serialization (no spaces, no raw newlines).
    std::string dump() const;

    // Parses a complete JSON document. On failure returns false and fills
    // `error` with a short description (never echoes the input).
    static bool parse(const std::string& text, Json& out, std::string& error);

private:
    void dump_to(std::string& out) const;

    Type type_ = Type::Null;
    bool bool_ = false;
    long long int_ = 0;
    double double_ = 0.0;
    std::string string_;
    std::vector<Json> values_;        // array items or object values
    std::vector<std::string> keys_;   // object keys (parallel to values_)
};

}  // namespace stcs
