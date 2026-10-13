#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace stcs {

class Json {
public:
    enum class Type { Null, Bool, Int, Double, String, Array, Object };

    static constexpr int kMaxDepth = 32;

    Json() = default;

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

    const std::vector<Json>& items() const { return values_; }
    void push_back(Json value);

    const Json* find(const std::string& key) const;
    Json& set(const std::string& key, Json value);
    const std::vector<std::string>& keys() const { return keys_; }
    std::size_t size() const { return values_.size(); }

    std::string dump() const;

    static bool parse(const std::string& text, Json& out, std::string& error);

private:
    void dump_to(std::string& out) const;

    Type type_ = Type::Null;
    bool bool_ = false;
    long long int_ = 0;
    double double_ = 0.0;
    std::string string_;
    std::vector<Json> values_;
    std::vector<std::string> keys_;
};

}
